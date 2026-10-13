"""K07: dry-run translation of every Kirby shader (K06 output) through the D2R chain: dmsh_xlate (Ryujinx) -> SPIR-V -> spirv-cross -> MSL.
Windows has no Apple Metal compiler, so the chain stops at MSL (same as dm/gfx/tools/nometal_run.py's route).

Input per shader follows dm/gfx/tools/dmsh_build.py job_for(): the .code file is the BNSH bytecode starting at the NVN header;
graphics stages drop the first 0x30 bytes (NVN header) so the translator sees SPH + instructions; c1 = code[ctrl+0x700 : ctrl+0x704].
No answer table (shader_facts.tsv has no Kirby keys): the translator falls back to defaults and records each fallback as a note
(texture type -> 2D, image format -> R8G8B8A8Unorm, attributes / outputs -> float, compute local size -> 1). Those notes are the
list of questions an answer table must cover (H3).

usage: python -I kirby_xlate.py --shaders D:/kirby-work/shaders --out D:/kirby-work/xlate
         [--xlate D:/kirby-work/tools/dmsh_xlate/dmsh_xlate.exe] [--spirv-cross <exe>] [--jobs 12] [--batch 400] [--limit N]
outputs: <out>/dryrun.tsv (one row per control+code combination), <out>/summary.txt, <out>/spv/, <out>/msl/
"""
import argparse, collections, concurrent.futures as cf, csv, json, os, re, struct, subprocess, sys
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
XC = ['--msl', '--msl-version', '20300', '--msl-argument-buffers', '--msl-texture-buffer-native', '--msl-pad-fragment-output']   # = dmsh_build.py XC

def load_index():
    parts = (repo / 'kirby/analysis/shader_index_parts.txt').read_text(encoding='utf-8').split()
    rows = []
    for p in parts:
        with open(repo / 'kirby/analysis' / p, encoding='utf-8') as f:
            rows += list(csv.DictReader(f, delimiter='\t'))
    return rows

def note_kind(n):
    if '贴图句柄' in n: return 'tex_type'
    if '图像句柄' in n: return 'img_format'
    if 'c1 读越界' in n: return 'c1_oob'
    return 'other'

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--shaders', required=True, type=Path); ap.add_argument('--out', required=True, type=Path)
    ap.add_argument('--xlate', default='D:/kirby-work/tools/dmsh_xlate/dmsh_xlate.exe')
    ap.add_argument('--spirv-cross', default='D:/kirby-work/tools/SPIRV-Cross/build/spirv-cross.exe')
    ap.add_argument('--jobs', type=int, default=12); ap.add_argument('--batch', type=int, default=400); ap.add_argument('--limit', type=int, default=0)
    a = ap.parse_args()
    rows = load_index()
    if a.limit: rows = rows[:a.limit]
    work, spv_dir, msl_dir = a.out / 'work', a.out / 'spv', a.out / 'msl'
    for d in (work, spv_dir, msl_dir): d.mkdir(parents=True, exist_ok=True)

    # 1) 作业：键 = 控制段键.机器码哈希（冲突键的多份机器码各自一项） | jobs: key = control key.code hash (each code of a conflicting key is its own job)
    jobs = []
    for r in rows:
        jk = f"{r['key']}.{r['code_hash']}"
        code = (a.shaders / r['filename']).read_bytes(); ctrl = (a.shaders / f"{r['key']}.ctrl").read_bytes()
        c1o, c1t = struct.unpack_from('<2I', ctrl, 0x700)
        c1 = code[c1o:c1t] if c1t > c1o else b''
        body = code if r['stage'] == 'cs' else code[0x30:]
        cp = work / f'{jk}.bin'
        if not cp.exists(): cp.write_bytes(body)
        jobs.append({'key': jk, 'stage': 'ps' if r['stage'] == 'fs' else r['stage'], 'code': str(cp), 'tex': {}, 'img': {}, 'attr': {}, 'fout': {},
                     'c1': c1.hex(), 'debug': False, 'reduced': True})
    print(f'{len(jobs)} 个作业', flush=True)

    # 2) 翻译成 SPIR-V：分批，每批一个 dmsh_xlate 进程 | translate to SPIR-V: in batches, one dmsh_xlate process per batch
    batches = [jobs[i:i + a.batch] for i in range(0, len(jobs), a.batch)]
    xerr = {}
    def run_batch(i):
        jf = work / f'batch{i:04d}.json'; jf.write_text(json.dumps(batches[i]), encoding='utf-8')
        r = subprocess.run([a.xlate, str(jf), str(spv_dir)], capture_output=True, text=True, encoding='utf-8', errors='replace')
        errs = {}
        for ln in r.stderr.splitlines():
            m = re.match(r'【红】([0-9a-f]{16}\.[0-9a-f]{16})：(.*)', ln)
            if m: errs[m.group(1)] = m.group(2)[:200]
        return errs
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        for k, errs in enumerate(ex.map(run_batch, range(len(batches)))):
            xerr.update(errs)
            if k % 10 == 0: print(f'  翻译批次 {k + 1}/{len(batches)}', flush=True)

    # 3) SPIR-V → MSL
    def to_msl(j):
        k = j['key']; spv = spv_dir / f'{k}.spv'
        if k in xerr or not spv.exists(): return k, 'spirv', xerr.get(k, '没有产出 .spv')
        r = subprocess.run([a.spirv_cross, *XC, str(spv), '--output', str(msl_dir / f'{k}.metal')], capture_output=True, text=True, encoding='utf-8', errors='replace')
        if r.returncode: return k, 'msl', (r.stderr.strip().splitlines() or ['?'])[0][:200]
        return k, 'ok', ''
    with cf.ThreadPoolExecutor(a.jobs) as ex:
        res = list(ex.map(to_msl, jobs))

    # 4) 汇总 | summary
    stage_of = {j['key']: j['stage'] for j in jobs}
    notes = collections.Counter(); keys_with = collections.Counter()
    with open(a.out / 'dryrun.tsv', 'w', encoding='utf-8', newline='') as f:
        w = csv.writer(f, delimiter='\t'); w.writerow(['key', 'code_hash', 'stage', 'result', 'reason', 'n_tex_default', 'n_img_default', 'n_c1_oob'])
        for k, st, why in res:
            kinds = collections.Counter()
            rj = spv_dir / f'{k}.res.json'
            if rj.exists():
                for n in json.loads(rj.read_text(encoding='utf-8')).get('notes', []): kinds[note_kind(n)] += 1
            for kd in kinds: keys_with[kd] += 1
            notes.update(kinds)
            key, ch = k.split('.')
            w.writerow([key, ch, stage_of[k], st, why, kinds['tex_type'], kinds['img_format'], kinds['c1_oob']])
    st = collections.Counter(r[1] for r in res); by_stage = collections.Counter((stage_of[r[0]], r[1]) for r in res)
    lines = [f'作业 {len(res)}：MSL 成功 {st["ok"]}（{st["ok"] / max(1, len(res)):.1%}），SPIR-V 失败 {st["spirv"]}，MSL 失败 {st["msl"]}']
    for sg in ('vs', 'ps', 'cs', 'gs'):
        n = sum(v for (s, _), v in by_stage.items() if s == sg)
        if n: lines.append(f'  {sg}: {n} 个，成功 {by_stage[(sg, "ok")]}，SPIR-V 失败 {by_stage[(sg, "spirv")]}，MSL 失败 {by_stage[(sg, "msl")]}')
    for lvl in ('spirv', 'msl'):
        c = collections.Counter(re.sub(r'[0-9A-Fa-f]{6,}', '#', r[2]) for r in res if r[1] == lvl)
        for e, n in c.most_common(15): lines.append(f'  [{lvl}] {n:6d}  {e}')
    lines.append(f'缺省作答（作答表要回答的问题）：贴图类型 {notes["tex_type"]} 处 / {keys_with["tex_type"]} 个着色器；图像格式 {notes["img_format"]} 处 / {keys_with["img_format"]} 个；c1 读越界 {notes["c1_oob"]} 处 / {keys_with["c1_oob"]} 个')
    (a.out / 'summary.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    print('\n'.join(lines))

if __name__ == '__main__':
    main()
