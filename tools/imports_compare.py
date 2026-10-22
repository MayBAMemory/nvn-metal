"""Build Kirby import/provider/D2R HLE tables and the K02 report.
Usage: python -I imports_compare.py --images <dir> --symbols <workdir> --llvm-cxxfilt <exe>
       [--repo <repository>]
All image parsing is delegated to imports.py in Python isolated mode.
"""
import argparse
import csv
import re
import subprocess
import sys
from collections import Counter, defaultdict
import os
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]                                       # 本仓库根 | this repository
DMROOT = Path(os.environ.get('D2R_MOBILE', ROOT.parent / 'd2r-mobile')).resolve()   # 上游 d2r-mobile 检出 | upstream checkout

EXPECTED_IMPORTS = {'main': 707, 'subsdk0': 657}
MODULES = ('main', 'sdk', 'subsdk0')


def strip_comments(text):
    return re.sub(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*',
                  lambda m: ' ' if m[0].startswith('/') else m[0], text, flags=re.S)


def hle_registrations(repo):
    result = defaultdict(set)
    files = list((DMROOT / 'dm/host').rglob('*.c')) + list((DMROOT / 'dm/gfx').rglob('*.c'))
    for path in sorted(files):
        text = strip_comments(path.read_text(encoding='utf-8'))
        for kind, name in re.findall(r'\b(HLE_FORCE|HLE)\s*\(\s*"([^"\n]+)"', text):
            result[name].add((kind, path.relative_to(repo).as_posix()))
    return result


def namespace(demangled):
    prefix = demangled.split('(', 1)[0]
    match = re.search(r'([A-Za-z_]\w*(?:::[A-Za-z_]\w*)+)', prefix)
    if not match: return 'libc'
    parts = match[0].split('::')
    if parts[0] in ('android', 'movie', 'nv', '__cxxabiv1'):
        return parts[0]
    if parts[0] == 'std' and parts[1] != '__1': return 'std'
    # Namespace depth requested by TASKS: nn::audio, nn::os::detail.
    if len(parts) >= 3 and parts[2] == 'detail': return '::'.join(parts[:3])
    return '::'.join(parts[:2])


def write_tsv(path, columns, rows):
    with path.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
        writer.writerow(columns); writer.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--images', type=Path, required=True)
    ap.add_argument('--symbols', type=Path, required=True)
    ap.add_argument('--llvm-cxxfilt', required=True)
    ap.add_argument('--repo', type=Path, default=DMROOT)
    args = ap.parse_args()
    args.symbols.mkdir(parents=True, exist_ok=True)
    imports, exports = {}, {}
    for mod in MODULES:
        subprocess.run([sys.executable, '-I', str(args.ROOT / 'tools/imports.py'),
                        str(args.images / (mod + '.img')), str(args.symbols / mod)], check=True)
        imports[mod] = (args.symbols / (mod + '.imports.txt')).read_text(encoding='utf-8').splitlines()
        exports[mod] = set((args.symbols / (mod + '.exports.txt')).read_text(encoding='utf-8').splitlines())
    for mod, count in EXPECTED_IMPORTS.items():
        if len(imports[mod]) != count: raise ValueError('Unexpected import count: ' + mod)
    all_names = sorted(set(imports['main'] + imports['subsdk0']))
    result = subprocess.run([args.llvm_cxxfilt, '--no-strip-underscore'],
                            input='\n'.join(all_names) + '\n', encoding='utf-8',
                            capture_output=True, check=True)
    demangled = result.stdout.splitlines()
    if len(demangled) != len(all_names): raise ValueError('Demangler row count changed')
    names = dict(zip(all_names, demangled))
    hle = hle_registrations(args.repo)
    analysis = args.ROOT / 'analysis'; analysis.mkdir(exist_ok=True)
    lines = ['# K02 导入符号与 D2R 接管对照', '', '日期：2026-10-14。状态：完成。', '',
             '## 做了什么', '',
             '从三个映像重新导出动态符号；用 LLVM cxxfilt 还原名称；扫描 dm/host 和 dm/gfx 的 C 宏登记，忽略注释。生成两份导入表及一份反向 HLE 对照表。', '',
             'defined_in 按导出表的精确 mangled 名称匹配，多提供者以分号分隔。d2r 保留所有登记类型和文件，未接管时 d2r 为空、d2r_file 为 -；仅核对接口名称，尚未验证卡比所需的运行时语义。ns 默认取前两级，detail 保留第三级，其余 C 名称归 libc。', '',
             '## 复现', '', '```powershell',
             'scoop install llvm',
             'python -I tools/imports_compare.py --images D:\\kirby-work\\img --symbols D:\\kirby-work\\symbols --llvm-cxxfilt C:\\Users\\hy367\\scoop\\apps\\llvm\\current\\bin\\llvm-cxxfilt.exe',
             '```', '', '## 数字与验收', '']
    for mod in MODULES: lines.append('- %s：导入 %d，导出 %d。' % (mod, len(imports[mod]), len(exports[mod])))
    for mod in EXPECTED_IMPORTS:
        rows = []
        for name in imports[mod]:
            providers = ';'.join(m for m in MODULES if name in exports[m]) or 'none'
            registrations = sorted(hle.get(name, []))
            kinds = ';'.join(sorted(set(kind for kind, path in registrations)))
            paths = ';'.join(sorted(set(path for kind, path in registrations))) or '-'
            rows.append([name, names[name], namespace(names[name]), providers, kinds, paths])
        write_tsv(analysis / ('imports_' + mod + '.tsv'),
                  ['mangled', 'demangled', 'ns', 'defined_in', 'd2r', 'd2r_file'], rows)
        count = Counter(row[2] for row in rows)
        covered = sum(bool(row[4]) for row in rows)
        lines += ['', '### ' + mod, '', '- TSV 数据行：%d；D2R 名称匹配：%d；待补接口：%d。' %
                  (len(rows), covered, len(rows) - covered), '', '| ns | 数量 |', '|---|---:|']
        lines += ['| %s | %d |' % pair for pair in sorted(count.items())]
        none = [row for row in rows if row[3] == 'none']
        lines += ['', 'defined_in=none 完整列表（%d 项；宿主需提供，表内 d2r 列说明是否已有接管）：' % len(none), '']
        lines += ['- `%s` — %s；D2R：%s。' % (row[0], row[1], row[4] or '未接管') for row in none]
        if not none: lines += ['无。']
    reverse = []
    for name, registrations in sorted(hle.items()):
        for kind, path in sorted(registrations):
            found = []
            if name in exports['sdk']: found.append('sdk_export')
            if name in imports['main']: found.append('main_import')
            reverse.append([name, path, kind, ';'.join(found) or 'none'])
    write_tsv(analysis / 'd2r_hle_in_kirby.tsv', ['name', 'd2r_file', 'kind', 'kirby'], reverse)
    matches = set(imports['main'] + imports['subsdk0']) & set(hle)
    lines += ['', '- D2R 登记：%d 个不同名称，%d 个名称/类型/文件登记；两个被分析模块共匹配 %d 个不同接口名称。' %
              (len(hle), len(reverse), len(matches)), '',
              '## 没解决的问题与后续建议', '',
              '名称匹配不能证明音频、图形等接口的实现满足卡比；K08/K09 应从 TSV 筛出未覆盖的命名空间。没有未解析符号遗漏到报告之外。', '',
              '## 待决定', '', '无；此任务只记录接口事实，不改变宿主设计。', '']
    report = args.ROOT / 'reports'; report.mkdir(exist_ok=True)
    report.joinpath('K02.md').write_text('\n'.join(lines).replace(chr(92) * 2, chr(92)), encoding='utf-8')
    for path in [analysis / 'imports_main.tsv', analysis / 'imports_subsdk0.tsv', analysis / 'd2r_hle_in_kirby.tsv']:
        if path.stat().st_size > 1024 * 1024: raise ValueError('Analysis file exceeds 1 MiB')
    print('K02 tables and report generated; matching interfaces:', len(matches))


if __name__ == '__main__':
    main()
