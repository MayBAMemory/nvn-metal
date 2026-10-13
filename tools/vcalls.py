"""List virtual calls (blr through a vtable slot) in an address range of a decompressed NSO image.
For each `blr xT`, walk back a few instructions to find `ldr xT, [xV, #slot]` and `ldr xV, [xO, #0]` (vtable load),
then report where the object register xO came from (`ldr xO, [xB, #field]` or `mov`). Heuristic, linear backward scan.

usage: python -I vcalls.py <module.img> <start_hex> <end_hex>
"""
import struct, sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from capstone.arm64 import ARM64_OP_REG, ARM64_OP_MEM

img = open(sys.argv[1], 'rb').read()
start, end = int(sys.argv[2], 16), int(sys.argv[3], 16)
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM); md.detail = True
ins = list(md.disasm(img[start:end], start))
def find_back(i, pred, limit=24):
    for j in range(i - 1, max(-1, i - limit), -1):
        if pred(ins[j]): return j
        if ins[j].mnemonic in ('ret', 'b') and j != i - 1: return None
    return None
def writes(x, reg):
    return x.operands and x.operands[0].type == ARM64_OP_REG and x.operands[0].reg == reg and x.mnemonic not in ('str', 'stp', 'stur', 'cmp', 'cbz', 'cbnz', 'tbz', 'tbnz')
for i, x in enumerate(ins):
    if x.mnemonic != 'blr': continue
    t = x.operands[0].reg
    j = find_back(i, lambda y: writes(y, t))
    if j is None or ins[j].mnemonic != 'ldr' or ins[j].operands[1].type != ARM64_OP_MEM:
        print(f'{x.address:#x}: blr {x.reg_name(t)}  (target not from vtable slot)'); continue
    slot = ins[j].operands[1].mem.disp; vreg = ins[j].operands[1].mem.base
    k = find_back(j, lambda y: writes(y, vreg))
    obj = '?'
    if k is not None and ins[k].mnemonic == 'ldr' and ins[k].operands[1].type == ARM64_OP_MEM and ins[k].operands[1].mem.disp == 0:
        oreg = ins[k].operands[1].mem.base
        m = find_back(k, lambda y: writes(y, oreg))
        if m is not None:
            y = ins[m]
            obj = f'{y.mnemonic} {y.op_str}'
    print(f'{x.address:#x}: slot {slot // 8:2d} (+{slot:#x})  obj <- {obj}')
