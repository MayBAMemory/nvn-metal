"""共享管线清单：把大家玩过记下的 pso_user.tsv 合进仓库，所有人第一次进场景就不卡。
Shared pipeline list: merge everyone's recorded pso_user.tsv into the repository so nobody stutters the first time they reach a scene.

  python3 -I tools/pso_merge.py add <pso_user.tsv>...      校验后合进 pso/shared.tsv（打印新增多少条），再 git 提交 | validate and merge into pso/shared.tsv, then commit
  python3 -I tools/pso_merge.py check [文件...]             校验（缺省 pso/shared.tsv）；有一行不合格就退出码 1 | validate (default pso/shared.tsv); exit 1 on any bad line
  python3 -I tools/pso_merge.py seed <输出> <输入>...        合成运行时的种子清单（run_mac.sh / mac_app.sh / ios/run.sh 用），不合格的行丢掉 | build the runtime seed list, dropping bad lines

清单每行一条，宿主运行时记下（dm/gfx/nvn/nvn_mtl.m 的 pso_user_record），只有三种：
  S <着色器键> <阶段>      用到过的着色器（键 = 机器码 sha256 前 16 位 + 变体后缀，如 _b、_b_um32、_b_ia12）
  C <着色器键>             用到过的计算管线
  R <864 位十六进制>       用到过的渲染管线：顶点 / 片元着色器键 + 颜色 / 深度格式、混合、顶点布局（pso_skey，432 字节）
全是键和格式，**不含任何游戏内容**；这里按上面的格式逐行严格校验，别的东西进不来。只对 v1.0.0、翻译链 15cf7ca29eaf 有意义。
One entry per line, recorded by the host at run time (pso_user_record in dm/gfx/nvn/nvn_mtl.m), of three kinds only: S (shader key, stage), C (compute shader key),
R (864 hex digits: a pso_skey of 432 bytes -- vertex / fragment shader keys plus color / depth formats, blending and vertex layout).
Only keys and formats, **no game content**; every line is strictly validated against the formats above so nothing else gets in. Meaningful only for v1.0.0 and translation chain 15cf7ca29eaf.
"""
import re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'pso' / 'shared.tsv'
SKEY_HEX = 432 * 2          # sizeof(pso_skey) * 2，dm/gfx/nvn/nvn_pso_skey.h（arm64） | sizeof(pso_skey) * 2 (arm64)
KEY = r'[0-9a-f]{16}(?:_[A-Za-z0-9]+)*'
LINE = re.compile(r'(?:S\t' + KEY + r'\t[0-5]|C\t' + KEY + r'|R\t[0-9a-f]{%d})' % SKEY_HEX)
HEADER = ('# 卡比 v1.0.0 共享管线清单（翻译链 15cf7ca29eaf）：只有着色器键和管线格式，不含游戏内容。用 tools/pso_merge.py add 合并，别手改\n'
          '# Kirby v1.0.0 shared pipeline list (chain 15cf7ca29eaf): shader keys and pipeline formats only, no game content. Merge with tools/pso_merge.py add, do not edit by hand\n')
ORDER = {'S': 0, 'C': 1, 'R': 2}   # 着色器先、管线后：预热按文件顺序走 | shaders first, then pipelines: warm-up follows file order


def skey_names_ok(hexs):
    """R 行开头两段各 24 字节是顶点 / 片元着色器键（NUL 补齐，片元可以为空）| the first two 24-byte fields of an R line are the vertex / fragment shader keys (NUL padded; fragment may be empty)"""
    raw = bytes.fromhex(hexs[:96])
    for i, field in enumerate((raw[:24], raw[24:48])):
        name, _, pad = field.partition(b'\0')
        if pad.strip(b'\0') or not (re.fullmatch(KEY.encode(), name) or (i == 1 and not name)):
            return False
    return True


def read(path):
    """返回 (合格的行集合, 不合格的 [(行号, 内容)]) | returns (set of valid lines, [(line number, text)] of bad ones)"""
    good, bad = set(), []
    for n, line in enumerate(Path(path).read_text(encoding='utf-8', errors='replace').splitlines(), 1):
        line = line.rstrip('\r')
        if not line or line.startswith('#'):
            continue
        if LINE.fullmatch(line) and (line[0] != 'R' or skey_names_ok(line[2:])):
            good.add(line)
        else:
            bad.append((n, line[:80]))
    return good, bad


def write(path, lines):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.tmp')
    with open(tmp, 'w', encoding='utf-8', newline='\n') as f:
        f.write(HEADER)
        for line in sorted(lines, key=lambda s: (ORDER[s[0]], s)):
            f.write(line + '\n')
    tmp.replace(path)


def count(lines):
    return '，'.join('%s %d' % (k, sum(1 for s in lines if s[0] == k)) for k in 'SCR')


def main(argv):
    if len(argv) < 1 or argv[0] not in ('add', 'check', 'seed'):
        sys.exit(__doc__)
    cmd, args = argv[0], argv[1:]
    if cmd == 'check':
        rc = 0
        for p in args or [SHARED]:
            good, bad = read(p)
            for n, t in bad[:20]:
                print('%s:%d 不合格：%r' % (p, n, t))
            print('%s：合格 %d 条（%s），不合格 %d 条' % (p, len(good), count(good), len(bad)))
            rc |= bool(bad)
        return rc
    if cmd == 'add':
        if not args:
            sys.exit('用法：pso_merge.py add <pso_user.tsv>...')
        have = read(SHARED)[0] if SHARED.exists() else set()
        new = set()
        for p in args:
            good, bad = read(p)
            if bad:
                print('⚠ %s：%d 行不合格，丢掉（第一处第 %d 行：%r）' % (p, len(bad), bad[0][0], bad[0][1]))
            new |= good
        added = new - have
        write(SHARED, have | new)
        print('pso/shared.tsv：原有 %d 条，新增 %d 条（%s），共 %d 条' % (len(have), len(added), count(added), len(have | new)))
        return 0
    if len(args) < 1:
        sys.exit('用法：pso_merge.py seed <输出> <输入>...')
    out, ins = Path(args[0]), [Path(p) for p in args[1:]]
    lines, dropped = set(), 0
    for p in ins:
        if p.exists():
            good, bad = read(p)
            lines |= good; dropped += len(bad)
    write(out, lines)
    print('管线种子清单 %s：%d 条（%s）%s' % (out, len(lines), count(lines), '，丢掉不合格 %d 行' % dropped if dropped else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
