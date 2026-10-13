"""Find direct callers (bl / b) of an exported function, across one or more decompressed NSO images.
Inside the defining image, direct bl/b to the address are matched; in other images, calls through PLT stubs
whose GOT slot is bound to the symbol are matched. Callers are named by the nearest preceding defined symbol.

usage: python -I xref_calls.py <symbol> <defining.img> [other.img ...]
"""
import bisect, struct, sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

def load(path):
    img = open(path, 'rb').read()
    mod0 = struct.unpack_from('<I', img, 4)[0]
    dyn = mod0 + struct.unpack_from('<i', img, mod0 + 4)[0]
    tags = {}; i = dyn
    while True:
        t, v = struct.unpack_from('<qQ', img, i); i += 16
        if t == 0: break
        tags.setdefault(t, v)
    symtab, strtab = tags[6], tags[5]
    nsym = struct.unpack_from('<I', img, tags[4] + 4)[0] if 4 in tags else (strtab - symtab) // 24
    def sname(k):
        no = struct.unpack_from('<I', img, symtab + k * 24)[0]
        return img[strtab + no:img.index(b'\0', strtab + no)].decode()
    defs = {}
    for k in range(1, nsym):
        _, info, _, shndx, value, size = struct.unpack_from('<IBBHQQ', img, symtab + k * 24)
        if shndx and value and (info & 0xf) == 2:
            defs.setdefault(value, sname(k))
    got = {}
    for off_tag, size_tag in ((7, 8), (23, 2)):
        if off_tag in tags:
            for r in range(0, tags[size_tag], 24):
                o, info, _ = struct.unpack_from('<QQq', img, tags[off_tag] + r)
                if info >> 32: got[o] = sname(info >> 32)
    text_end = struct.unpack_from('<I', open(path.replace('.img', ''), 'rb').read(0x40) if False else img[:0], 0)[0] if False else None
    return img, defs, got

def calls_to(img, targets, defs, label):
    addrs = sorted(defs)
    n = len(img) // 4
    w = struct.unpack_from('<%dI' % n, img, 0)
    for i in range(n):
        x = w[i]
        if x >> 26 in (0x25, 0x05):
            off = (x & 0x3ffffff); off = off - (1 << 26) if off & (1 << 25) else off
            t = i * 4 + off * 4
            if t in targets:
                j = bisect.bisect_right(addrs, i * 4) - 1
                caller = defs[addrs[j]] if j >= 0 else '?'
                print(f'{label}+{i*4:#x}  {"bl" if x >> 26 == 0x25 else "b "} {targets[t]}  in {caller}+{i*4 - (addrs[j] if j >= 0 else 0):#x}')

def plt_stubs(img, got, want):
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM); md.detail = True
    out = {}
    n = len(img) // 4
    for i in range(n):
        x = struct.unpack_from('<I', img, i * 4)[0]
        if x & 0x9F00001F == 0x90000010:   # adrp x16
            ins = list(md.disasm(img[i * 4:i * 4 + 16], i * 4))
            if len(ins) == 4 and ins[1].mnemonic == 'ldr' and ins[3].mnemonic == 'br':
                slot = ins[0].operands[1].imm + ins[1].operands[1].mem.disp
                if got.get(slot) == want: out[i * 4] = want + '@plt'
    return out

sym = sys.argv[1]
img, defs, got = load(sys.argv[2])
addr = [a for a, n in defs.items() if n == sym]
if not addr: sys.exit('symbol not defined in ' + sys.argv[2])
print(f'{sym} defined at {sys.argv[2]}+{addr[0]:#x}')
calls_to(img, {addr[0]: sym}, defs, 'def')
stubs = plt_stubs(img, got, sym)
if stubs: calls_to(img, stubs, defs, 'def')
for p in sys.argv[3:]:
    oimg, odefs, ogot = load(p)
    stubs = plt_stubs(oimg, ogot, sym)
    if stubs: calls_to(oimg, stubs, odefs, p.split('\\')[-1].split('/')[-1])
