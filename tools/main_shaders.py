"""Extract the shaders embedded in Kirby's main executable (three BNSH containers in main's data, e.g. nn::gfx / NintendoWare
utility shaders) that K06 could not see (K06 only scanned RomFS). Writes the same ctrl / code files as bnsh_extract.py into the
shader directory and an extra index part (same columns) so kirby_shlib.py / kirby_xlate.py pick them up.

usage: python -I main_shaders.py <main.img> <shader dir>   (main.img from nso_info.py; image offsets = main+offset)
writes kirby/analysis/kirby_shaders.main.tsv and appends it to kirby/analysis/shader_index_parts.txt
"""
import csv, hashlib, re, struct, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from bnsh_extract import parse_bnsh, header_size, BNSH_MAGIC

repo = Path(__file__).resolve().parents[2]
img = Path(sys.argv[1]).read_bytes(); out = Path(sys.argv[2])
existing = {}
for part in (repo / 'kirby/analysis/shader_index_parts.txt').read_text(encoding='utf-8').split():
    if part == 'kirby_shaders.main.tsv': continue
    with open(repo / 'kirby/analysis' / part, encoding='utf-8') as f:
        for r in csv.DictReader(f, delimiter='\t'): existing[(r['key'], r['code_hash'])] = r
rows, seen = [], {}
for m in re.finditer(BNSH_MAGIC, img):
    base = m.start()
    try: size = header_size(memoryview(img), base, BNSH_MAGIC)
    except ValueError: continue
    for stage, variation, ctrl_off, code_off, ctrl, code in parse_bnsh(img[base:base + size]):
        key = hashlib.sha256(bytes(ctrl)).hexdigest()[:16]; ch = hashlib.sha256(bytes(code)).hexdigest()[:16]
        if (key, ch) in seen: seen[(key, ch)]['occurrences'] = str(int(seen[(key, ch)]['occurrences']) + 1); continue
        if (key, ch) in existing: continue   # RomFS 里已有同一组合 | the same combination already exists in RomFS
        same_key = [k for k in list(existing) + list(seen) if k[0] == key]
        name = f'{key}.code' if not same_key and not (out / f'{key}.code').exists() else f'{key}.{ch}.code'
        (out / f'{key}.ctrl').write_bytes(bytes(ctrl)); (out / name).write_bytes(bytes(code))
        r = {'key': key, 'code_hash': ch, 'filename': name, 'stage': stage, 'source': f'main@{base:#x}', 'variant': f'0/{variation}',
             'bnsh_offset': f'{base:#x}', 'code_size': str(len(code)), 'occurrences': '1', 'conflict': '1' if same_key else '0'}
        seen[(key, ch)] = r; rows.append(r)
cols = ['key', 'code_hash', 'filename', 'stage', 'source', 'variant', 'bnsh_offset', 'code_size', 'occurrences', 'conflict']
with open(repo / 'kirby/analysis/kirby_shaders.main.tsv', 'w', encoding='utf-8', newline='\n') as f:
    w = csv.DictWriter(f, cols, delimiter='\t', lineterminator='\n'); w.writeheader(); w.writerows(rows)
parts_file = repo / 'kirby/analysis/shader_index_parts.txt'
parts = parts_file.read_text(encoding='utf-8').split()
if 'kirby_shaders.main.tsv' not in parts: parts_file.write_text('\n'.join(parts + ['kirby_shaders.main.tsv']) + '\n', encoding='utf-8')
from collections import Counter
print(f'main 内嵌着色器：新组合 {len(rows)}（阶段 {dict(Counter(r["stage"] for r in rows))}），与 RomFS 重复的已跳过')
