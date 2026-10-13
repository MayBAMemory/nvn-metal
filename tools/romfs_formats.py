"""Audit RomFS headers and CMP codecs; optionally stream shader-bearing containers.
Usage: python -I romfs_formats.py <RomFS> --docs <directory> --samples-out <workdir>
       [--scan-shaders]
Only small headers are decoded for the format distribution. The optional shader
scan streams FRES/SARC/VFXB containers to find embedded FSHA/BNSH headers; it never
writes those complete containers unless selected as a small unpacking sample.
"""
import argparse
import csv
import sys
from collections import Counter, defaultdict
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from cmp_unpack import cmp_header, cmp_reader, unpack

READ_SIZE = 1024 * 1024
CONTAINERS = {b'FRES', b'SARC', b'VFXB'}
PURPOSES = {
    '.bfsar': 'NintendoWare 音频归档；FSAR', '.bfsha': '着色器归档；FSHA',
    '.bfstm': 'NintendoWare 音频流；FSTM', '.bin': 'HAL XBIN 容器（样本为 Basil 字节码）；分类依路径',
    '.bnvib': '振动数据（据目录名猜测）；无 ASCII 魔数，样本 uint32=4',
    '.cmp': '资源压缩包装；uint32 长度 + Zstd/LZ4 帧',
    '.cmpbin': '同一压缩包装，常见音频归档/界面 SARC',
    '.cndbin': 'XBIN 参数容器（据目录猜测）', '.dat': 'XBIN 容器，HGDF 标签；用途未解析',
    '.msbp': '消息项目数据；MsgPrjBn', '.msbt': '消息文本；MsgStdBn',
    '.opus': 'Nintendo Opus 容器（据扩展名和头部推测），不是 OggS 包装',
    '.webm': 'WebM 视频；EBML 1A45DFA3',
}
INNER_NAMES = {b'FRES': 'BFRES（模型/材质/贴图容器）', b'XBIN': 'XBIN（样本偏移 0x14 为 YAML）',
               b'SARC': 'SARC（资源归档）', b'VFXB': 'NintendoWare 特效容器',
               b'FFNT': 'BFFNT 字体', bytes.fromhex('36f81a1e'): 'BFTTF（字体；内层编码未解析）',
               b'FSAR': 'BFSAR 音频归档'}


def shader_headers(reader, container_size):
    seen = set()
    tail, done = b'', 0
    while block := reader.read(READ_SIZE):
        data = tail + block
        base = done - len(tail)
        for magic, padding in ((b'FSHA', b'    '), (b'BNSH', bytes(4))):
            start = 0
            while (at := data.find(magic + padding, start)) >= 0:
                start = at + 1
                if at + 32 <= len(data) and data[at + 12:at + 14] == b'\xff\xfe':
                    size = int.from_bytes(data[at + 28:at + 32], 'little')
                    if size >= 32 and base + at + size <= container_size:
                        seen.add((magic.decode('ascii'), base + at))
        done += len(block)
        tail = data[-32:]
    return done, sorted(seen)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root', type=Path)
    ap.add_argument('--docs', type=Path, required=True)
    ap.add_argument('--samples-out', type=Path, required=True)
    ap.add_argument('--scan-shaders', action='store_true')
    args = ap.parse_args()
    args.docs.mkdir(parents=True, exist_ok=True)
    files = sorted(p for p in args.root.rglob('*') if p.is_file())
    groups = defaultdict(list)
    for p in files: groups[p.suffix.lower()].append(p)
    lines = ['# RomFS 文件格式摸底（K03）', '', '日期：2026-10-14，v1.0.0。', '',
             '## 外层格式', '', '| 扩展名 | 文件数 | 字节数 | 用途/魔数 |', '|---|---:|---:|---|']
    for ext, paths in sorted(groups.items()):
        lines.append('| %s | %d | %d | %s |' %
                     (ext, len(paths), sum(p.stat().st_size for p in paths), PURPOSES.get(ext, '未识别')))
    lines += ['', '## 文件头抽样', '', '每种扩展名抽 3 个；不足 3 个则全查。仅保留任务要求的 32 字节格式头，不包含资源正文。', '',
              '| 路径 | 开头 32 字节（hex） |', '|---|---|']
    for ext, paths in sorted(groups.items()):
        for p in paths[:3]:
            with p.open('rb') as stream: head = stream.read(32)
            lines.append('| %s | `%s` |' % (p.relative_to(args.root).as_posix(), head.hex(' ')))
    codecs, inners, declared = Counter(), Counter(), Counter()
    samples = {}
    shader_rows = []
    scanned = scan_bytes = 0
    compressed = [p for p in files if p.suffix.lower() in ('.cmp', '.cmpbin')]
    for index, p in enumerate(compressed, 1):
        ext = p.suffix.lower()
        with p.open('rb') as source: expected, codec = cmp_header(source)
        with cmp_reader(p) as (_, reader): head = reader.read(32)
        codecs[ext, codec] += 1; inners[ext, head[:4]] += 1; declared[ext] += expected
        key = ext, codec, head[:4]
        if key not in samples or p.stat().st_size < samples[key].stat().st_size:
            samples[key] = p
        if args.scan_shaders and head[:4] in CONTAINERS:
            with cmp_reader(p) as (_, reader): total, headers = shader_headers(reader, expected)
            if total != expected: raise ValueError('Decoded size mismatch: ' + str(p.relative_to(args.root)))
            scanned += 1; scan_bytes += total
            for magic, offset in headers:
                shader_rows.append([p.relative_to(args.root).as_posix(), magic, hex(offset)])
        if index % 500 == 0 or index == len(compressed):
            print('CMP audit:', index, '/', len(compressed), 'shader-containing files:',
                  len({row[0] for row in shader_rows}), flush=True)
    lines += ['', '## CMP/CMPBIN 格式与全量分布', '',
              '偏移 0x00 为 little-endian uint32 解压后长度；0x04 开始是标准 Zstandard（28 B5 2F FD）或 LZ4 frame（04 22 4D 18）。Zstd frame content size（存在时）必须与前缀相等；LZ4 frame 的长度字段存在时亦核对。', '',
              '| 扩展名 | 压缩 | 文件数 |', '|---|---|---:|']
    for (ext, codec), n in sorted(codecs.items()): lines.append('| %s | %s | %d |' % (ext, codec, n))
    lines += ['', '| 扩展名 | 内层前 4 字节 | 判定 | 文件数 |', '|---|---|---|---:|']
    for (ext, magic), n in sorted(inners.items()):
        lines.append('| %s | `%s` | %s | %d |' % (ext, magic.hex(' '), INNER_NAMES.get(magic, '未知'), n))
    lines += ['', '- .cmp 声明的总解压长度：%d 字节。' % declared['.cmp'],
              '- .cmpbin 声明的总解压长度：%d 字节。' % declared['.cmpbin'], '',
              '全量文件头全部落在以上两种标准帧，因此不需要按 zlib/raw deflate/LZ4 block/Yaz0/LZ77 强行解码；这些格式没有出现匹配的包装头。', '',
              '## 完整解压样本', '', '| 压缩包装 | codec | 内层头 | 输出 | 解压字节 |', '|---|---|---|---|---:|']
    sample_bytes = 0
    for index, ((ext, codec, magic), p) in enumerate(sorted(samples.items()), 1):
        target = args.samples_out / ('%02d_%s_%s.bin' % (index, ext.lstrip('.'), codec))
        total = unpack(p, target); sample_bytes += total
        lines.append('| %s | %s | `%s` | `%s` | %d |' %
                     (p.relative_to(args.root).as_posix(), codec, magic.hex(), target, total))
    lines += ['', '## 着色器容器', '',
              '文件开头没有 FSHA/BNSH 不表示容器里没有着色器。为 K06 另做 FRES/SARC/VFXB 的分块流式扫描，定位带有效 8 字节签名（FSHA 后四空格，BNSH 后四零字节）、0x0c little-endian BOM 且声明长度在容器内的 FSHA/BNSH 头；只给位置，不提取代码或猜测变体布局。完整名单见 cmp_shader_containers.tsv（每个源文件一行，偏移为解压后字节位置，以分号分隔）。', '',
              '- 容器扫描：%d 个，%d 字节；未写出全量解压资源。' % (scanned, scan_bytes),
              '- 带着色器头的压缩文件：%d 个；FSHA 头 %d 个，BNSH 头 %d 个。' %
              (len({row[0] for row in shader_rows}), sum(row[1] == 'FSHA' for row in shader_rows), sum(row[1] == 'BNSH' for row in shader_rows)), '',
              '头部扫描不能替代 K06 对指针、文件长度和着色器变体的结构解析；这里的内嵌位置供解析器核对。XBIN 参数/字体/FSAR 音频不属于此次着色器容器扫描范围。', '',
              '## 依据与限制', '',
              '- [Zstandard 官方帧格式](https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md)：用于核对帧魔数和长度字段。',
              '- [LZ4 官方帧格式](https://github.com/lz4/lz4/blob/dev/doc/lz4_Frame_format.md)：用于核对 LZ4 frame 包装。',
              '- [Khangaroo 的 Star Allies CMP 工具](https://gist.github.com/khang06/49d0fe124025a87ba661ebcf33964ef3)：旧作采用 4 字节长度加 LZ4；这里只参考事实，未复制实现。当前游戏使用两种 codec，由本地全量核对确认。',
              '- [MintWorkshop](https://github.com/firubii/MintWorkshop)：作者列出本作 Basil 7.0.2；本地 Basil XBIN 头部的 7/0/2 字段与其一致。其他 XBIN 用途仍属目录推测。',
              '- BFTTF 的内层编码及其是否加密未解析；不影响 CMP 外层格式判定或 K06 着色器定位。', '']
    args.docs.joinpath('romfs_formats.md').write_text('\n'.join(lines), encoding='utf-8')
    inventory = defaultdict(lambda: defaultdict(list))
    for source, magic, offset in shader_rows:
        inventory[source][magic].append(offset)
    table = args.docs.joinpath('cmp_shader_containers.tsv')
    with table.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
        writer.writerow(['source', 'fsha_count', 'fsha_offsets', 'bnsh_count', 'bnsh_offsets'])
        for source, headers in sorted(inventory.items()):
            fsha, bnsh = headers.get('FSHA', []), headers.get('BNSH', [])
            writer.writerow([source, len(fsha), ';'.join(fsha), len(bnsh), ';'.join(bnsh)])
    if table.stat().st_size > 1024 * 1024: raise ValueError('Shader inventory exceeds 1 MiB')
    print('Audit OK:', len(compressed), 'compressed files;', len(samples), 'full samples;',
          sample_bytes, 'sample bytes;', scanned, 'containers;', len(shader_rows), 'embedded shader headers')


if __name__ == '__main__':
    main()
