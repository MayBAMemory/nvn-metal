"""Disassemble one exported function of a decompressed NSO image and name its direct call targets.
usage: python -I sdis_sym.py <module.img> <symbol> [max_insns]
Call targets are named by the image's own symbol table, or by the import a PLT stub jumps through."""
import struct, sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

img = open(sys.argv[1], 'rb').read()
want = sys.argv[2]
maxn = int(sys.argv[3]) if len(sys.argv) > 3 else 400
mod0 = struct.unpack_from('<I', img, 4)[0]
dyn = mod0 + struct.unpack_from('<i', img, mod0 + 4)[0]
tags = {}
i = dyn
while True:
    t, v = struct.unpack_from('<qQ', img, i); i += 16
    if t == 0: break
    tags.setdefault(t, v)
symtab, strtab = tags[6], tags[5]
nsym = struct.unpack_from('<I', img, tags[4] + 4)[0] if 4 in tags else (strtab - symtab) // 24
def sname(k):
    no = struct.unpack_from('<I', img, symtab + k * 24)[0]
    return img[strtab + no:img.index(b'\0', strtab + no)].decode()
by_addr, addr_of = {}, {}
for k in range(1, nsym):
    _, info, _, shndx, value, size = struct.unpack_from('<IBBHQQ', img, symtab + k * 24)
    if shndx and value:
        n = sname(k); by_addr.setdefault(value, n); addr_of[n] = (value, size)
got = {}
for off_tag, size_tag in ((7, 8), (23, 2)):
    if off_tag in tags:
        for r in range(0, tags[size_tag], 24):
            o, info, _ = struct.unpack_from('<QQq', img, tags[off_tag] + r)
            if info >> 32: got[o] = sname(info >> 32)
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM); md.detail = True
def plt_name(a):
    ins = list(md.disasm(img[a:a + 16], a))
    if len(ins) >= 3 and ins[0].mnemonic == 'adrp' and ins[1].mnemonic == 'ldr':
        slot = ins[0].operands[1].imm + ins[1].operands[1].mem.disp
        return got.get(slot)
    return None
a, size = addr_of[want]
print(f'{want} @ {a:#x} size {size:#x}')
end = a + (size or maxn * 4)
for ins in md.disasm(img[a:end], a):
    line = f'{ins.address:#x}: {ins.mnemonic} {ins.op_str}'
    if ins.mnemonic in ('bl', 'b') and ins.operands and ins.operands[0].type == 2:
        t = ins.operands[0].imm
        n = by_addr.get(t) or plt_name(t)
        if n: line += f'    ; {n}'
    print(line)
