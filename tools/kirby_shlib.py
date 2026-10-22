"""H3: build Kirby's B-route shader library from K06's extracted shaders, in the format the replay layer (dm/gfx/nvn/nvn_mtl.m) loads.

Differences from D2R's dm/gfx/tools/dmsh_build.py (whose reflection helpers are copied below, see the marked section):
  * Key = first 16 hex of sha256(code[0 : ctrl+0x704]) ("code key"), not of the 0x880 control section. Verified on all 27239
    K06 combinations: ctrl+0x704 == code length and the hash equals K06's code_hash. The code alone determines the translation
    (SPH, instructions and c1 all live in it), so identical code -> identical library entry: 27239 combos -> 16946 entries.
    This also sidesteps the 766 control-key collisions and nn::gfx's SetShadersExt rewriting the control section at runtime.
    The replay layer computes the same key for non-D2R games (nvn_mtl.m prog_get, dm_game_d2r() == 0).
  * Textures: Kirby binds combined texture+sampler handles via nvnCommandBufferBindTexture; the shaders read them at
    c[2][0x20 + 8r] (r = 0..31, K07: all 61466 references). D2R's separate tables at 0x168 / 0x568 are still decoded if seen.
  * Compute: local size from ctrl+0x748/0x74c/0x750, shared memory bytes from ctrl+0x754 (found by comparing all 72 compute
    controls; D2R took these from PC reflection).
  * No answer table: K07 showed the translator asks no texture-type / image-format questions for Kirby.
  * Geometry shaders (10, two Mouthful-Mode models) are skipped: spirv-cross cannot emit them as MSL.
  * No Apple compiler on Windows: like dm/gfx/tools/nometal_run.py, "<key>_b.metallib" holds the MSL source (LF newlines) and the
    shell build compiles it on the device; on a Mac with Xcode run `xcrun metal` over the .metal files instead.

usage: python -I kirby_shlib.py --shaders D:/kirby-work/shaders --out D:/kirby-work/shlib [--jobs 12] [--batch 400]
outputs: <out>/b-<V>/<key>_b.{metallib,metal,json}   (V = DMSH_CHAIN from dm/gfx/nvn/dmsh_chain.h)
         <out>/shader_map.tsv   (columns as dm/gfx/nvn/shader_map.tsv: col 3 = stage name, col 4 = key; point DM_NVN_SHMAP at it)
         <out>/build.tsv        (per key: result / reason)
"""
import argparse, collections, concurrent.futures as cf, csv, json, os, re, struct, subprocess, sys
import os
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]                                       # 本仓库根 | this repository
DMROOT = Path(os.environ.get('D2R_MOBILE', ROOT.parent / 'd2r-mobile')).resolve()   # 上游 d2r-mobile 检出 | upstream checkout

repo = DMROOT
XC = ['--msl', '--msl-version', '20300', '--msl-argument-buffers', '--msl-texture-buffer-native', '--msl-pad-fragment-output']   # = dmsh_build.py XC
STAGE_ZH = {'vs': '顶点', 'ps': '像素', 'cs': '计算'}   # nvn_mtl.m stage_of()
COMB_BASE, COMB_END, IMG_BASE, IMG_END, TEX_BASE, SAMP_BASE = 0x20, 0x120, 0x120, 0x168, 0x168, 0x568

# ---------------- copied from dm/gfx/tools/dmsh_build.py (reflection helpers), decode_tex adapted for Kirby ----------------
def raster_facts(stage, src):
    if stage == 'ps':
        out = re.search(r'struct main0_out\s*\{(.*?)\};', src, re.S)
        return {'discard': int('discard_fragment' in src), 'sample_mask': int('sample_mask' in (out.group(1) if out else '')),
                'colors': sorted({int(x) for x in re.findall(r'\[\[color\((\d+)\)\]\]', out.group(1) if out else '')})}
    if stage != 'vs': return {}
    body = re.search(r'\bvertex\s+\w+\s+\w+\s*\(.*?\)\s*\{(.*)\}\s*$', src, re.S)
    if not body or re.search(r'\b(if|for|while|switch|do)\b|clip_distance|ClipDistance', body.group(1)): return {'pos_attr': -1}
    b = body.group(1)
    attr = {m.group(2): int(m.group(3)) for m in re.finditer(r'(\w+)\s+(\w+)\s*\[\[attribute\((\d+)\)\]\]', src)}
    var = {m.group(1): (m.group(2), m.group(3)) for m in re.finditer(r'float\s+(_\w+)\s*=\s*in\.(\w+)\.([xyzw]);', b)}
    last = {}
    for m in re.finditer(r'out\.gl_Position\.([xyzw])\s*=\s*([^;]+);', b): last[m.group(1)] = m.group(2).strip()
    src_of = {c: var.get(v) for c, v in last.items()}
    m0 = src_of.get('x')
    if not m0 or m0[0] not in attr or any(src_of.get(c) != (m0[0], c) for c in 'xyz'): return {'pos_attr': -1}
    if last.get('w') == '1.0': w = 'one'
    elif src_of.get('w') == (m0[0], 'w'): w = 'attr'
    else: return {'pos_attr': -1}
    ost = re.search(r'struct main0_out\s*\{(.*?)\};', src, re.S)
    locn = {m.group(1): int(m.group(2)) for m in re.finditer(r'(m_\d+) \[\[user\(locn(\d+)\)\]\]', ost.group(1) if ost else '')}
    olast = {}
    for m in re.finditer(r'out\.(m_\d+)\.([xyzw])\s*=\s*([^;]+);', b): olast[(m.group(1), m.group(2))] = m.group(3).strip()
    uv, aff = {}, {}
    for mem, k in locn.items():
        sx, sy = var.get(olast.get((mem, 'x'), '')), var.get(olast.get((mem, 'y'), ''))
        if sx and sy and sx[0] == sy[0] and sx[0] in attr and sx[1] == 'x' and sy[1] == 'y': uv[str(k)] = attr[sx[0]]; continue
        def fm(v_):
            m = re.search(r'float %s = fma\((_\w+), \(\*spvDescriptorSet0\.vp_c(\d+)\)\._m0\[(\d+)\]\.([xyzw]), \(\*spvDescriptorSet0\.vp_c(\d+)\)\._m0\[(\d+)\]\.([xyzw])\);' % re.escape(v_), b)
            if not m or m.group(1) not in var: return None
            return var[m.group(1)], [int(m.group(2)) - 3, int(m.group(3)), 'xyzw'.index(m.group(4))], [int(m.group(5)) - 3, int(m.group(6)), 'xyzw'.index(m.group(7))]
        fx, fy = fm(olast.get((mem, 'x'), '')), fm(olast.get((mem, 'y'), ''))
        if fx and fy and fx[0][0] == fy[0][0] and fx[0][0] in attr and fx[0][1] == 'x' and fy[0][1] == 'y' and min(fx[1][0], fx[2][0], fy[1][0], fy[2][0]) >= 0:
            aff[str(k)] = {'attr': attr[fx[0][0]], 'sx': fx[1], 'ox': fx[2], 'sy': fy[1], 'oy': fy[2]}
    return {'pos_attr': attr[m0[0]], 'pos_w': w, 'uv_from_attr': uv, 'uv_affine': aff}

def decode_tex(name):
    """Kirby: combined texture handles at c[2][0x20 + 8r]; images at c[2][0x120 + 8r]; D2R's separate tables kept as a fallback."""
    m = re.match(r'[a-z]+_[ti]\w*?_(?:cb(\d+)|tcb)_([0-9A-F]+)', name)
    if not m: return None
    slots = int(m.group(1)) if m.group(1) else -1; h = int(m.group(2), 16)
    tb = (slots & 0xffff) if slots >= 0 else 2; sb = ((slots >> 16) - 1) if slots >= 0 and (slots >> 16) else tb
    o0, o1, ty = h & 0x3fff, (h >> 14) & 0x3fff, h >> 28
    def slot(word, lo, hi, cb): return (word * 4 - lo) // 8 if cb == 2 and lo <= word * 4 < hi and (word * 4 - lo) % 8 == 0 else None
    words = [o0] + ([o1] if ty else [])
    if re.match(r'[a-z]+_i', name):
        return next((s for x in words if (s := slot(x, IMG_BASE, IMG_END, tb)) is not None), None), None, ty
    t = next((s for x in words if (s := slot(x, COMB_BASE, COMB_END, tb)) is not None), None)
    if t is None: t = next((s for x in words if (s := slot(x, TEX_BASE, SAMP_BASE, tb)) is not None), None)
    s = next((s for x in words if (s := slot(x, SAMP_BASE, SAMP_BASE + 0x400, sb)) is not None), None)
    return t, s, ty

def reflect(stage, msl, res):
    body = re.search(r'struct spvDescriptorSetBuffer0\s*\{(.*?)\};', msl, re.S)
    binds = []
    for m in re.finditer(r'^\s*(.+?)\s+(\w+)\s*\[\[id\((\d+)\)\]\];', body.group(1) if body else '', re.M):
        ty, nm, aid = m.group(1).strip(), m.group(2), int(m.group(3))
        e = {'msl_name': nm, 'msl_type': ty, 'arg_id': aid}
        if nm == 'support_buffer': e.update(kind='support')
        elif re.fullmatch(r'[a-z]+_c(\d+)', nm):
            n = int(re.fullmatch(r'[a-z]+_c(\d+)', nm).group(1))
            e.update(kind='cbuf', reg=n - 3) if 3 <= n <= 16 else e.update(kind='c1' if n == 1 else 'drvcb', bank=n)
        elif re.fullmatch(r'[a-z]+_s(\d+)', nm):
            e.update(kind='sbuf', index=int(re.fullmatch(r'[a-z]+_s(\d+)', nm).group(1)))
        elif nm.endswith('Smplr'):
            t, s, ty2 = decode_tex(nm[:-5]); e.update(kind='samp', reg=s) if ty2 else e.update(kind='samp_tex', reg=t)
        else:
            d = decode_tex(nm)
            if not d: e.update(kind='?')
            else: e.update(kind='img' if re.match(r'[a-z]+_i', nm) else ('tbuf' if 'texture_buffer' in ty else 'tex'), reg=d[0])
        binds.append(e)
    base = {'vs': 0x110, 'cs': 0x310, 'ps': 0x510}.get(stage)
    sb_sorted = sorted(res['sbufs'], key=lambda b: b['binding'])
    for e in binds:
        if e['kind'] == 'sbuf':
            b = sb_sorted[e['index']] if e['index'] < len(sb_sorted) else None
            off = b['cb_offset'] * 4 if b and b['cb_slot'] == 0 else None
            if stage == 'cs' and off is not None and 0x210 <= off < 0x310 and (off - 0x210) % 16 == 0:
                e.update(kind='ubo_ptr', reg=(off - 0x210) // 16)
            else: e['reg'] = (off - base) // 16 if off is not None and base is not None and 0 <= off - base < 0x100 and (off - base) % 16 == 0 else None
            e['rw'] = bool(b and b['flags'] & 2)
    return binds

DRV_FILL = {0: [(0x110, 0x210), (0x310, 0x410), (0x510, 0x610)], 2: [(0x20, 0x168), (0x168, 0x568), (0x568, 0x968)]}
def drv_reads(src):
    out = {}
    for bank in (0, 2):
        offs, dyn = set(), False
        for m in re.finditer(r'fp_c%d\)\._m0\[([^\]]+)\]' % bank, src):
            idx = m.group(1).rstrip('u')
            if idx.isdigit(): offs.add(int(idx) * 16)
            else: dyn = True
        if offs or dyn: out[str(bank)] = {'vec4_offsets': sorted(offs), 'dynamic': dyn}
    return out
def unfillable(dr):
    bad = []
    for bank, d in dr.items():
        if d['dynamic']: bad.append(f'c{bank} 动态下标')
        for o in d['vec4_offsets']:
            if not any(a <= o and o + 16 <= b or (a <= o < b) for a, b in DRV_FILL[int(bank)]): bad.append(f'c{bank}+{o:#x}')
    return bad
# ---------------- end of copied section ----------------

def load_index():
    rows = []
    for p in (ROOT / 'analysis/shader_index_parts.txt').read_text(encoding='utf-8').split():
        with open(ROOT / 'analysis' / p, encoding='utf-8') as f: rows += list(csv.DictReader(f, delimiter='\t'))
    return rows

def chain_version():
    m = re.search(r'#define DMSH_CHAIN "([0-9a-f]+)"', (DMROOT / 'dm/gfx/nvn/dmsh_chain.h').read_text(encoding='utf-8'))
    if not m: sys.exit('读不出 dm/gfx/nvn/dmsh_chain.h 的 DMSH_CHAIN')
    return m.group(1)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--shaders', required=True, type=Path); ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--xlate', default='D:/kirby-work/tools/dmsh_xlate/dmsh_xlate.exe')
    ap.add_argument('--spirv-cross', default='D:/kirby-work/tools/SPIRV-Cross/build/spirv-cross.exe')
    ap.add_argument('--jobs', type=int, default=12); ap.add_argument('--batch', type=int, default=400)
    a = ap.parse_args()
    V = chain_version(); lib = a.out / f'b-{V}'; work = a.out / 'work'
    for d in (lib, work): d.mkdir(parents=True, exist_ok=True)

    rows = load_index(); by_code = {}
    for r in rows: by_code.setdefault(r['code_hash'], []).append(r)
    jobs, meta, skipped = [], {}, []
    for ch, rs in sorted(by_code.items()):
        r = rs[0]; stages = {x['stage'] for x in rs}
        if len(stages) != 1: sys.exit(f'机器码 {ch} 对应多个阶段 {stages}')
        stage = {'fs': 'ps'}.get(r['stage'], r['stage'])
        if stage == 'gs': skipped.append((ch, '几何着色器：Metal 无对应阶段')); continue
        code = (a.shaders / r['filename']).read_bytes(); ctrl = (a.shaders / f"{r['key']}.ctrl").read_bytes()
        c1o, tot = struct.unpack_from('<2I', ctrl, 0x700)
        if tot != len(code): sys.exit(f'{ch}：ctrl+0x704 = {tot} 与机器码长 {len(code)} 不符')
        c1 = code[c1o:tot] if tot > c1o else b''
        cp = work / f'{ch}.bin'; cp.write_bytes(code if stage == 'cs' else code[0x30:])
        j = {'key': ch, 'stage': stage, 'code': str(cp), 'tex': {}, 'img': {}, 'attr': {}, 'fout': {}, 'c1': c1.hex(), 'debug': True, 'reduced': True}
        if stage == 'cs':
            j['local'] = list(struct.unpack_from('<3I', ctrl, 0x748)); j['shared'] = struct.unpack_from('<I', ctrl, 0x754)[0]
        jobs.append(j); meta[ch] = {'ctl': sorted({x['key'] for x in rs}), 'src': f"{r['source']}#{r['variant']}", 'c1': c1}
    print(f'机器码去重后 {len(jobs) + len(skipped)} 份：出库 {len(jobs)}，跳过 {len(skipped)}（几何）', flush=True)

    batches = [jobs[i:i + a.batch] for i in range(0, len(jobs), a.batch)]
    xerr = {}
    def run_batch(i):
        jf = work / f'batch{i:04d}.json'; jf.write_text(json.dumps(batches[i]), encoding='utf-8')
        r = subprocess.run([a.xlate, str(jf), str(work)], capture_output=True, text=True, encoding='utf-8', errors='replace')
        return {m.group(1): m.group(2)[:200] for ln in r.stderr.splitlines() if (m := re.match(r'【红】([0-9a-f]{16})：(.*)', ln))}
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        for k, e in enumerate(ex.map(run_batch, range(len(batches)))):
            xerr.update(e)
            if k % 10 == 0: print(f'  翻译批次 {k + 1}/{len(batches)}', flush=True)

    def finish(j):
        k = j['key']; spv = work / f'{k}.spv'
        if k in xerr or not spv.exists(): return k, 'SPIR-V 失败', xerr.get(k, '没有 .spv')
        msl = work / f'{k}.metal'
        r = subprocess.run([a.spirv_cross, *XC, str(spv), '--output', str(msl)], capture_output=True, text=True, encoding='utf-8', errors='replace')
        if r.returncode: return k, 'MSL 失败', (r.stderr.strip().splitlines() or ['?'])[0][:200]
        src = msl.read_text(encoding='utf-8').replace('\r\n', '\n')
        entry = re.search(r'(vertex|fragment|kernel)\s+\S+\s+(\w+)\s*\(', src)
        if not entry: return k, '入口不明', 'MSL 里找不到 vertex / fragment / kernel 入口'
        res = json.loads((work / f'{k}.res.json').read_text(encoding='utf-8'))
        dr = drv_reads(src); m = meta[k]
        refl = {'key': k + '_b', 'route': 'B', 'chain': V, 'dm_ctl': k, 'kirby_ctl_keys': m['ctl'], 'source': m['src'], 'pc_paired': False,
                'stage': j['stage'], 'entry': entry.group(2), 'msl_entry_kind': entry.group(1), 'threadgroup_size': j.get('local'),
                'shared_bytes': j.get('shared'), 'c1': m['c1'].hex(), 'notes': res['notes'], 'drv_reads': dr, 'drv_unfillable': unfillable(dr),
                'bindings': reflect(j['stage'], src, res),
                'vertex_inputs': [{'location': int(x.group(2)), 'msl_type': x.group(1)} for x in re.finditer(r'(\w+)\s+\w+\s*\[\[attribute\((\d+)\)\]\]', src)],
                'raster': raster_facts(j['stage'], src), 'chain_metal': 'none (kirby_shlib.py: MSL source, compiled on device)'}
        bad = [b for b in refl['bindings'] if b['kind'] == '?' or (b['kind'] in ('tex', 'samp_tex', 'img', 'tbuf') and b.get('reg') is None)]
        (lib / f'{k}_b.metal').write_bytes(src.encode('utf-8'))
        (lib / f'{k}_b.metallib').write_bytes(src.encode('utf-8'))   # 源码进「库」，设备上现编（同 nometal_run.py） | source goes into the "library", compiled on the device (same as nometal_run.py)
        (lib / f'{k}_b.json').write_text(json.dumps(refl, ensure_ascii=False, indent=1), encoding='utf-8')
        if bad: return k, '绑定解析不全', ', '.join(b['msl_name'] for b in bad)[:200]
        if refl['drv_unfillable']: return k, '驱动段读不可填', ', '.join(refl['drv_unfillable'])[:200]
        return k, 'ok', ''
    with cf.ThreadPoolExecutor(a.jobs) as ex: res = list(ex.map(finish, jobs))

    stage_of = {j['key']: j['stage'] for j in jobs}
    with open(a.out / 'shader_map.tsv', 'w', encoding='utf-8', newline='\n') as f:
        f.write(f'# 卡比着色器映射表（tools/kirby_shlib.py）：键 = 机器码 sha256 前 16 位；库 b-{V}\n')
        for k, st, _ in res:
            if st in ('ok', '绑定解析不全', '驱动段读不可填'): f.write(f"kirby\t{meta[k]['src']}\t{STAGE_ZH[stage_of[k]]}\t{k}\n")
    with open(a.out / 'build.tsv', 'w', encoding='utf-8', newline='\n') as f:
        f.write('key\tstage\tresult\treason\n')
        for k, st, why in res: f.write(f'{k}\t{stage_of[k]}\t{st}\t{why}\n')
        for k, why in skipped: f.write(f'{k}\tgs\t跳过\t{why}\n')
    c = collections.Counter(st for _, st, _ in res)
    print(f'出库 {len(res)}：' + '，'.join(f'{k} {v}' for k, v in c.most_common()) + f'；跳过（几何）{len(skipped)}；库目录 {lib}')
    for st in c:
        if st == 'ok': continue
        for e, n in collections.Counter(re.sub(r'[0-9A-Fa-f]{6,}', '#', w) for _, s, w in res if s == st).most_common(6): print(f'  [{st}] {n}  {e}')

if __name__ == '__main__':
    main()
