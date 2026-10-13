"""Decompress NSOs and print module info, middleware tags, NVN usage.
usage: python -I nso_info.py <exefs dir> <outdir>
"""
import os, re, struct, sys, hashlib

def lz4_block(src, out_size):
    dst = bytearray()
    i = 0
    n = len(src)
    while i < n:
        tok = src[i]; i += 1
        lit = tok >> 4
        if lit == 15:
            while True:
                b = src[i]; i += 1; lit += b
                if b != 255: break
        dst += src[i:i + lit]; i += lit
        if i >= n: break
        off = src[i] | (src[i + 1] << 8); i += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = src[i]; i += 1; ml += b
                if b != 255: break
        ml += 4
        start = len(dst) - off
        if off >= ml:
            dst += dst[start:start + ml]
        else:
            for k in range(ml):
                dst.append(dst[start + k])
    assert len(dst) == out_size, (len(dst), out_size)
    return bytes(dst)

def load_nso(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'NSO0'
    flags = struct.unpack_from('<I', d, 0xC)[0]
    segs = []
    for i in range(3):
        foff, moff, dsz = struct.unpack_from('<III', d, 0x10 + i * 0x10)
        csz = struct.unpack_from('<I', d, 0x60 + i * 4)[0]
        raw = d[foff:foff + csz]
        data = lz4_block(raw, dsz) if flags & (1 << i) else raw
        segs.append((moff, data))
    bss = struct.unpack_from('<I', d, 0x3C)[0]
    build_id = d[0x40:0x60]
    size = max(m + len(x) for m, x in segs)
    img = bytearray(size)
    for m, x in segs:
        img[m:m + len(x)] = x
    return bytes(img), segs, bss, build_id

def main():
    exefs, outdir = sys.argv[1:3]
    os.makedirs(outdir, exist_ok=True)
    for name in ('main', 'sdk', 'subsdk0', 'rtld'):
        p = os.path.join(exefs, name)
        if not os.path.exists(p):
            continue
        img, segs, bss, bid = load_nso(p)
        open(os.path.join(outdir, name + '.img'), 'wb').write(img)
        print('== %s  build_id=%s  text=%d ro=%d data=%d bss=%d' % (
            name, bid[:20].hex(), len(segs[0][1]), len(segs[1][1]), len(segs[2][1]), bss))
        # module name in rodata
        tags = sorted(set(re.findall(rb'SDK MW\+[ -~]{3,80}', img)))
        for t in tags:
            print('   ', t.decode())
        mods = sorted(set(re.findall(rb'[A-Za-z0-9_:\\/.-]{2,}\.(?:cpp|c|cc)\b', img)))
        print('    src paths:', len(mods), [m.decode() for m in mods[:15]])
        nvn = sorted(set(re.findall(rb'\bnvn[A-Z][A-Za-z0-9]{3,}\b', img)))
        print('    nvn names:', len(nvn))
        open(os.path.join(outdir, name + '.nvn.txt'), 'w').write('\n'.join(x.decode() for x in nvn))
        vk = sorted(set(re.findall(rb'\bvk[A-Z][A-Za-z0-9]{3,}\b', img)))
        print('    vulkan names:', len(vk))
        for kw in (rb'Havok', rb'CRI', rb'criAtom', rb'criMana', rb'Wwise', rb'FMOD', rb'PhysX',
                   rb'Unreal', rb'Unity', rb'il2cpp', rb'mono_', rb'Lua', rb'lua_', rb'Bink',
                   rb'Pia', rb'nn::pia', rb'nn::atk', rb'nn::audio', rb'nn::ldn', rb'nn::hid',
                   rb'nn::fs', rb'nn::mii', rb'nn::nifm', rb'nn::album', rb'nn::web', rb'nn::swkbd',
                   rb'nn::movie', rb'nn::mm', rb'nn::nfp', rb'amiibo', rb'Amiibo', rb'Effekseer',
                   rb'Vibration', rb'SixAxis', rb'ptcl', rb'eft2', rb'nn::g3d', rb'nn::ui2d',
                   rb'nn::font', rb'nn::gfx', rb'agl::', rb'sead::', rb'Hal', rb'HAL'):
            c = img.count(kw)
            if c:
                print('    %-12s %d' % (kw.decode(), c))

main()
