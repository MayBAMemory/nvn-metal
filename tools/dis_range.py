"""Disassemble an address range of a decompressed NSO image, naming bl/b targets through the image's PLT/GOT.
usage: python -I dis_range.py <module.img> <start_hex> <end_hex> [--calls-only]
"""
import struct, sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

img = open(sys.argv[1], 'rb').read()
start, end = int(sys.argv[2], 16), int(sys.argv[3], 16)
calls_only = '--calls-only' in sys.argv
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
    if shndx and value: defs.setdefault(value, sname(k))
got = {}
for off_tag, size_tag in ((7, 8), (23, 2)):
    if off_tag in tags:
        for r in range(0, tags[size_tag], 24):
            o, info, _ = struct.unpack_from('<QQq', img, tags[off_tag] + r)
            if info >> 32: got[o] = sname(info >> 32)
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM); md.detail = True
def name_of(a):
    if a in defs: return defs[a]
    ins = list(md.disasm(img[a:a + 16], a))
    if len(ins) >= 3 and ins[0].mnemonic == 'adrp' and ins[1].mnemonic == 'ldr':
        return got.get(ins[0].operands[1].imm + ins[1].operands[1].mem.disp)
    return None
for ins in md.disasm(img[start:end], start):
    tag = ''
    if ins.mnemonic in ('bl', 'b') and ins.operands and ins.operands[0].type == 2:
        n = name_of(ins.operands[0].imm)
        if n: tag = '    ; ' + n
    if calls_only and not (ins.mnemonic in ('bl', 'blr') or tag): continue
    print(f'{ins.address:#x}: {ins.mnemonic} {ins.op_str}{tag}')
