# 把 Windows 上出的卡比着色器库（kirby/tools/kirby_shlib.py 的产物）编成 iOS 版 metallib。
# Windows 上没有苹果的着色器编译器，那边的 <键>_b.metallib 里装的其实是 MSL 源码（见 kirby_shlib.py 头注）；卡比的 App 不走 D2R 壳的
# 「设备上现编」（那条路在 DM_SHELL 下，连带改了装载器），所以要在 Mac 上先编好。编译参数只用 dm/ios/tools/ios_metal.py 那一份（D2R 的唯一口径）。
#   输入  <shlib>/b-<V>/<键>_b.{metal,json}、<shlib>/shader_map.tsv
#   输出  <out>/b-<V>/<键>_b.{metallib,json}、<out>/shader_map.tsv、<out>/b-<V>/failed.tsv（编不过的，每行：键 \t 首条报错）
# 重跑：目标比源新就跳过。用法：python3 -I shlib_ios.py <shlib 目录> <输出目录> [-j 并发数]
# Compiles the Kirby shader library produced on Windows (output of kirby/tools/kirby_shlib.py) into iOS metallibs.
# Windows has no Apple shader compiler, so the <key>_b.metallib files from there actually hold MSL source (see the kirby_shlib.py header); Kirby's App does not use
# the D2R shell's "compile on device" path (that lives under DM_SHELL, which also changes the loader), so they are compiled on the Mac first. Compile flags come only
# from dm/ios/tools/ios_metal.py (D2R's single standard).
#   in   <shlib>/b-<V>/<key>_b.{metal,json}, <shlib>/shader_map.tsv
#   out  <out>/b-<V>/<key>_b.{metallib,json}, <out>/shader_map.tsv, <out>/b-<V>/failed.tsv (failures, one per line: key \t first error)
# Re-runs skip targets newer than their source. Usage: python3 -I shlib_ios.py <shlib dir> <out dir> [-j jobs]
import argparse, concurrent.futures as cf, json, os, re, shutil, subprocess, sys, tempfile, time
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.path.join(REPO, 'dm', 'ios', 'tools'))
import ios_metal   # noqa: E402  METAL / METALLIB / RECORD

ap = argparse.ArgumentParser()
ap.add_argument('shlib'); ap.add_argument('out'); ap.add_argument('-j', type=int, default=os.cpu_count() or 4)
a = ap.parse_args()
chain = open(os.path.join(REPO, 'dm/gfx/nvn/dmsh_chain.h'), encoding='utf-8').read().split('#define DMSH_CHAIN "', 1)[1].split('"', 1)[0]
src_dir, dst_dir = os.path.join(a.shlib, f'b-{chain}'), os.path.join(a.out, f'b-{chain}')
if not os.path.isdir(src_dir):
    have = [d for d in os.listdir(a.shlib) if d.startswith('b-')] if os.path.isdir(a.shlib) else []
    sys.exit(f'🔴 没有 {src_dir}（宿主的翻译链版本是 {chain}；这里有的：{have or "无"}）。版本不符就在 Windows 上按 kirby/docs/shaders.md 重新出库')
os.makedirs(dst_dir, exist_ok=True)
shutil.copy2(os.path.join(a.shlib, 'shader_map.tsv'), os.path.join(a.out, 'shader_map.tsv'))
keys = sorted(f[:-len('_b.metal')] for f in os.listdir(src_dir) if f.endswith('_b.metal'))
if not keys: sys.exit(f'🔴 {src_dir} 里没有 *_b.metal')

# 顶点着色器补齐插值输出（FIX 变了就重编所有顶点着色器）。真机（2026-10-14）：2640 次建管线失败，全是
# 「Fragment input(s) user(locn3) ... not written by vertex shader」——NVN 允许片元读顶点没写的插值（读到未定义值），Metal 不许。
# 翻译出来的插值一律是 float4、顶点里 `main0_out out = {};` 清零，所以给每个顶点着色器补上缺的 user(locnK)（K 到 max(14, 本身最大)），值为 0。
# 片元不读的顶点输出 Metal 会忽略；15 个 float4 + 位置远在 Metal 的 124 分量上限内。
# Pad vertex-shader varyings (when FIX changes, every vertex shader is recompiled). On device (2026-10-14): 2640 pipeline failures, all
# "Fragment input(s) user(locn3) ... not written by vertex shader" -- NVN lets a fragment read a varying the vertex did not write (undefined value), Metal does not.
# Translated varyings are all float4 and vertex shaders zero them with `main0_out out = {};`, so each vertex shader gets the missing user(locnK) (K up to max(14, its own max)) as 0.
# Metal ignores vertex outputs the fragment does not read; 15 float4 + position is far under Metal's 124-component limit.
FIX = 'pad-locn-v1'
PAD_MAX = 14
def fix_vs(src):
    m = re.search(r'struct main0_out\n\{\n(.*?)\n\};', src, re.S)
    if not m: return src, 0
    have = {int(x) for x in re.findall(r'\[\[user\(locn(\d+)\)\]\]', m.group(1))}
    top = max([PAD_MAX] + list(have))
    add = [f'    float4 m_kpad{k} [[user(locn{k})]];' for k in range(top + 1) if k not in have]
    if not add: return src, 0
    return src[:m.end(1)] + '\n' + '\n'.join(add) + src[m.end(1):], len(add)
tmp = tempfile.mkdtemp(prefix='kirby_air_')

def one(k):
    src, lib = os.path.join(src_dir, f'{k}_b.metal'), os.path.join(dst_dir, f'{k}_b.metallib')
    jin, jout = os.path.join(src_dir, f'{k}_b.json'), os.path.join(dst_dir, f'{k}_b.json')
    j = json.load(open(jin, encoding='utf-8'))
    vs = j.get('msl_entry_kind') == 'vertex' or j.get('stage') == 'vs'
    if os.path.exists(lib) and os.path.exists(jout) and os.path.getmtime(lib) >= os.path.getmtime(src):
        if not vs or json.load(open(jout, encoding='utf-8')).get('kirby_ios_fix') == FIX: return k, 'skip', ''
    air, msl = os.path.join(tmp, f'{k}.air'), src
    if vs:
        text, npad = fix_vs(open(src, encoding='utf-8').read())
        if npad: msl = os.path.join(tmp, f'{k}_b.metal'); open(msl, 'w', encoding='utf-8').write(text)
        j['kirby_ios_fix'] = FIX
    r = subprocess.run([*ios_metal.METAL, msl, '-o', air], capture_output=True, text=True)
    if msl != src: os.remove(msl)
    if r.returncode == 0: r = subprocess.run([*ios_metal.METALLIB, air, '-o', lib], capture_output=True, text=True)
    if os.path.exists(air): os.remove(air)
    if r.returncode:
        err = next((l for l in r.stderr.splitlines() if 'error' in l), r.stderr.strip()[:300])
        if os.path.exists(lib): os.remove(lib)
        return k, 'fail', err.replace('\t', ' ')
    j['chain_metal'] = ios_metal.RECORD   # 记录 == 实际编译参数 | record == the actual compile flags
    json.dump(j, open(jout, 'w', encoding='utf-8'), ensure_ascii=False)
    return k, 'ok', ''

t0, cnt, fails = time.time(), {'ok': 0, 'skip': 0, 'fail': 0}, []
with cf.ThreadPoolExecutor(a.j) as ex:
    for i, (k, st, err) in enumerate(ex.map(one, keys), 1):
        cnt[st] += 1
        if st == 'fail': fails.append((k, err))
        if i % 500 == 0 or i == len(keys): print(f'[{time.time() - t0:6.0f}s] {i}/{len(keys)}  新编 {cnt["ok"]} 跳过 {cnt["skip"]} 失败 {cnt["fail"]}', flush=True)
shutil.rmtree(tmp, ignore_errors=True)
with open(os.path.join(dst_dir, 'failed.tsv'), 'w', encoding='utf-8') as f:
    for k, e in fails: f.write(f'{k}\t{e}\n')
print(f'完成：{len(keys)} 个，新编 {cnt["ok"]}，跳过 {cnt["skip"]}，失败 {cnt["fail"]}（{dst_dir}/failed.tsv）')
sys.exit(1 if fails else 0)
