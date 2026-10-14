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
# v2：卡比用 NVN 缺省的 GL 约定（从不调 nvnDeviceSetWindowOriginMode / SetDepthMode）：窗口原点左下、裁剪 z ∈ [-w, w]。翻译链（dmsh_xlate）按 D2R 的左上 / 0..1 写死，
# 所以在顶点输出处补：y 取反（渲染出的内存第 0 行 = 图像底部，同 GL；视口 / 剪裁 / FragCoord 数值都不用动），z ← (z + w) / 2。回放层对应地对调绕序、出图上下翻转
# （nvn_mtl.m g_nvn_gl_origin）。真机：不补时文字上下颠倒、3D 后处理链每过一次渲染到贴图就翻一次
# v2: Kirby uses NVN's default GL conventions (it never calls nvnDeviceSetWindowOriginMode / SetDepthMode): lower-left window origin, clip z in [-w, w]. The translation chain
# (dmsh_xlate) hard-codes D2R's upper-left / 0..1, so the vertex output is patched: y negated (memory row 0 of the rendering = image bottom, as in GL; viewport / scissor /
# FragCoord values need no change), z <- (z + w) / 2. The replay layer swaps the winding and flips at present (nvn_mtl.m g_nvn_gl_origin). On device without it, text
# was upside down and the 3D post-processing chain flipped at every render-to-texture
FIX = 'pad-locn-v1+gl-origin-v1'
def gl_clip(src):
    if not re.search(r'struct main0_out\n\{\n[^}]*\bgl_Position\b', src): return src
    return re.sub(r'\n(\s*)return out;', r'\n\1out.gl_Position.y = -out.gl_Position.y;\n\1out.gl_Position.z = (out.gl_Position.z + out.gl_Position.w) * 0.5;\n\1return out;', src)
PAD_MAX = 14
def fix_vs(src):
    m = re.search(r'struct main0_out\n\{\n(.*?)\n\};', src, re.S)
    if not m: return src, 0
    have = {int(x) for x in re.findall(r'\[\[user\(locn(\d+)\)\]\]', m.group(1))}
    top = max([PAD_MAX] + list(have))
    add = [f'    float4 m_kpad{k} [[user(locn{k})]];' for k in range(top + 1) if k not in have]
    if not add: return src, 0
    return src[:m.end(1)] + '\n' + '\n'.join(add) + src[m.end(1):], len(add)

# 片元着色器的「无符号整数输出」变体 <键>_b_u.metallib（UFIX 变了就重编）。真机（2026-10-14）：进第一关后 52 万次建管线失败，全是
# 「output of type float4 is not compatible with a MTLPixelFormatR8Uint color attachment」——NVN 的整数渲染目标存的是输出寄存器的原始位，
# 翻译出来的输出一律 float4。变体把颜色输出改成 uint4、每个赋值按位重解释（as_type），回放层遇到整数附件时换用它（nvn_mtl.m pso_desc）。
# Unsigned-integer-output variant of each fragment shader, <key>_b_u.metallib (recompiled when UFIX changes). On device (2026-10-14): 520k pipeline failures in level 1, all
# "output of type float4 is not compatible with a MTLPixelFormatR8Uint color attachment" -- NVN integer render targets store the raw bits of the output registers, while the
# translated outputs are all float4. The variant makes colour outputs uint4 with every assignment bit-reinterpreted (as_type); the replay layer uses it for integer attachments.
UFIX = 'uint-out-v1'
def make_uint(src):
    m = re.search(r'struct main0_out\n\{\n(.*?)\n\};', src, re.S)
    if not m: return None
    names = re.findall(r'float4 (m_\d+) \[\[color\(\d\)\]\]', m.group(1))
    if not names: return None
    body = re.sub(r'float4 (m_\d+) (\[\[color\(\d\)\]\])', r'uint4 \1 \2', m.group(1))
    out = src[:m.start(1)] + body + src[m.end(1):]
    alt = '|'.join(names)
    out = re.sub(r'out\.(' + alt + r')\.([xyzw]) = (.*);', r'out.\1.\2 = as_type<uint>(float(\3));', out)
    out = re.sub(r'out\.(' + alt + r') = (.*);', r'out.\1 = as_type<uint4>(float4(\2));', out)
    return out
tmp = tempfile.mkdtemp(prefix='kirby_air_')

def one(k):
    src, lib = os.path.join(src_dir, f'{k}_b.metal'), os.path.join(dst_dir, f'{k}_b.metallib')
    jin, jout = os.path.join(src_dir, f'{k}_b.json'), os.path.join(dst_dir, f'{k}_b.json')
    j = json.load(open(jin, encoding='utf-8'))
    vs = j.get('msl_entry_kind') == 'vertex' or j.get('stage') == 'vs'
    if os.path.exists(lib) and os.path.exists(jout) and os.path.getmtime(lib) >= os.path.getmtime(src):
        jo = json.load(open(jout, encoding='utf-8'))
        fsx = j.get('msl_entry_kind') == 'fragment' or j.get('stage') == 'fs'
        if (not vs or jo.get('kirby_ios_fix') == FIX) and (not fsx or jo.get('kirby_ios_ufix') in (UFIX, 'failed')): return k, 'skip', ''
    air, msl = os.path.join(tmp, f'{k}.air'), src
    if vs:
        text, npad = fix_vs(open(src, encoding='utf-8').read())
        text = gl_clip(text)
        msl = os.path.join(tmp, f'{k}_b.metal'); open(msl, 'w', encoding='utf-8').write(text)
        j['kirby_ios_fix'] = FIX
    r = subprocess.run([*ios_metal.METAL, msl, '-o', air], capture_output=True, text=True)
    if msl != src: os.remove(msl)
    if r.returncode == 0: r = subprocess.run([*ios_metal.METALLIB, air, '-o', lib], capture_output=True, text=True)
    if os.path.exists(air): os.remove(air)
    if r.returncode:
        err = next((l for l in r.stderr.splitlines() if 'error' in l), r.stderr.strip()[:300])
        if os.path.exists(lib): os.remove(lib)
        return k, 'fail', err.replace('\t', ' ')
    j['chain_metal'] = ios_metal.RECORD
    if j.get('msl_entry_kind') == 'fragment' or j.get('stage') == 'fs':   # 整数输出变体，编不过只记一笔（主库照用） | integer-output variant; a failure is only recorded (the main library is still used)
        u = make_uint(open(src, encoding='utf-8').read())
        if u:
            um, ua, ul = os.path.join(tmp, f'{k}_bu.metal'), os.path.join(tmp, f'{k}_bu.air'), os.path.join(dst_dir, f'{k}_b_u.metallib')
            open(um, 'w', encoding='utf-8').write(u)
            ru = subprocess.run([*ios_metal.METAL, um, '-o', ua], capture_output=True, text=True)
            if ru.returncode == 0: ru = subprocess.run([*ios_metal.METALLIB, ua, '-o', ul], capture_output=True, text=True)
            for x in (um, ua):
                if os.path.exists(x): os.remove(x)
            j['kirby_ios_ufix'] = UFIX if ru.returncode == 0 else 'failed'
            if ru.returncode: print(f'  整数输出变体编不过 {k}：' + next((l for l in ru.stderr.splitlines() if 'error' in l), '')[:160], flush=True)   # 记录 == 实际编译参数 | record == the actual compile flags
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
