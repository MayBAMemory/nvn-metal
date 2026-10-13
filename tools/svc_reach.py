"""Heuristic: does an exported SDK function reach a system service?
Walks direct calls (bl/b to named functions, through PLT stubs) from each given symbol up to a depth and reports
whether it hits a service-ish callee: nn::sf / hipc / cmif proxies, nn::sm, svc wrappers (nn::svc::*), or indirect blr
calls on objects fetched from a "Get*Service" / "*Proxy" / "*Object" getter. Static and approximate: confirm at runtime.

usage: python -I svc_reach.py <sdk.img> <depth> <symbol>...
"""
import re, struct, sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

img = open(sys.argv[1], 'rb').read()
depth = int(sys.argv[2])
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
defs, addr, size = {}, {}, {}
for k in range(1, nsym):
    _, info, _, shndx, value, sz = struct.unpack_from('<IBBHQQ', img, symtab + k * 24)
    if shndx and value and (info & 0xf) == 2:
        n = sname(k); defs.setdefault(value, n); addr[n] = value; size[n] = sz
got = {}
for off_tag, size_tag in ((7, 8), (23, 2)):
    if off_tag in tags:
        for r in range(0, tags[size_tag], 24):
            o, info, _ = struct.unpack_from('<QQq', img, tags[off_tag] + r)
            if info >> 32: got[o] = sname(info >> 32)
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM); md.detail = True
def resolve(t):
    if t in defs: return defs[t]
    ins = list(md.disasm(img[t:t + 16], t))
    if len(ins) >= 3 and ins[0].mnemonic == 'adrp' and ins[1].mnemonic == 'ldr':
        return got.get(ins[0].operands[1].imm + ins[1].operands[1].mem.disp)
    return None
SERVICE = re.compile(r'(2sf|4hipc|4cmif|2sm\d|3svc|Proxy|GetService|ServiceObject|ByHipc|SendSyncRequest|Ipc)')
def callees(name):
    a, sz = addr.get(name), size.get(name) or 0x400
    if a is None: return [], 0
    out, blr = [], 0
    for x in md.disasm(img[a:a + sz], a):
        if x.mnemonic in ('bl', 'b') and x.operands and x.operands[0].type == 2:
            n = resolve(x.operands[0].imm)
            if n and n != name: out.append(n)
        elif x.mnemonic == 'blr': blr += 1
    return out, blr
for root in sys.argv[3:]:
    seen, frontier, hits, blrs = {root}, [root], [], 0
    for _ in range(depth):
        nxt = []
        for f in frontier:
            cs, b = callees(f); blrs += b
            for c in cs:
                if SERVICE.search(c): hits.append(c)
                if c not in seen: seen.add(c); nxt.append(c)
        frontier = nxt
    verdict = 'SERVICE' if hits else ('maybe (indirect calls)' if blrs else 'local')
    print(f'{verdict:22s} {root}' + (f'   <- {hits[0][:90]}' if hits else ''))
