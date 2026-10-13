"""Kirby NVN loaders and static indirect calls, with ELF GOT relocations and control-flow joins.
Source: dm/gfx/tools/nvn_slots.py (adrp decoder and the loader/slot analysis scheme).
The loader parser and call resolver below support Kirby's GOT-indirect globals.
Version-dependent offsets are CLI parameters; no game address is built in.
Usage: python -I nvn_slots_kirby.py <main.img> <out.tsv> --loader <hex> --text-end <hex>
       [--d2r-root <repo>] [--needed-out <tsv>]
"""
import argparse
import bisect
import csv
import re
import struct
import sys
from collections import deque
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import verify_nsoimg as nsoimg

RELATIVE = 1027


def adrp(x, pc):
    imm = ((x >> 29) & 3) | (((x >> 5) & 0x7ffff) << 2)
    if imm & (1 << 20): imm -= 1 << 21
    return (pc & ~0xfff) + (imm << 12)

def analyze(data, loader, text_end):
    words = struct.unpack_from('<%dI' % (text_end // 4), data)
    rel = {off: addend for off, typ, name, val, addend in nsoimg.relocs(data)
           if typ == RELATIVE}
    starts = nsoimg.func_starts(data, text_end)
    if loader not in starts:
        raise ValueError('Loader is not an unwind function start')
    registers, current, slots, got_slots = {}, None, {}, {}
    loader_end = None
    for i in range(loader // 4, len(words)):
        x, pc = words[i], i * 4
        rd, rn = x & 31, (x >> 5) & 31
        if x == 0xd65f03c0:
            loader_end = pc + 4
            break
        if x & 0x9f000000 == 0x90000000:
            registers[rd] = adrp(x, pc)
        elif x & 0xffc00000 == 0x91000000:
            value = registers.get(rn)
            if value is None: registers.pop(rd, None)
            else:
                registers[rd] = value + ((x >> 10) & 0xfff)
                if rd == 1:
                    a = registers[rd]
                    current = data[a:data.index(b'\0', a)].decode('ascii')
                    if not re.fullmatch(r'nvn[A-Za-z0-9_]+', current):
                        raise ValueError('Loader contains a non-NVN lookup')
        elif x & 0xffc00000 == 0xf9400000:
            base = registers.get(rn)
            address = None if base is None else base + ((x >> 10) & 0xfff) * 8
            if address in rel:
                registers[rd] = rel[address]
                got_slots[address] = rel[address]
            else: registers.pop(rd, None)
        elif x & 0xffc00000 == 0xf9000000 and rd == 0:
            if rn not in registers or current is None:
                raise ValueError('Unresolved loader store')
            address = registers[rn] + ((x >> 10) & 0xfff) * 8
            if address in slots: raise ValueError('Duplicate NVN slot')
            slots[address] = current
            current = None
        elif x & 0xfffffc1f == 0xd63f0000:
            registers = {r: v for r, v in registers.items() if r >= 19}
    if loader_end is None or not slots: raise ValueError('Incomplete loader')
    got_slots = {a: slot for a, slot in got_slots.items() if slot in slots}
    uses = static_calls(data, words, slots, rel, starts)
    return slots, uses, got_slots, loader_end


def static_calls(data, words, slots, relocations, functions):
    """Basic-block fixed point; union pointer alternatives at branch joins.
    Track only the finite domain of GOT cells, NVN globals, and their pages.
    Capstone supplies exact register writes for instructions outside the recognized
    address/load/move patterns; no lexical backward guess crosses a branch join.
    """
    count = len(words)
    got = {a: v for a, v in relocations.items() if v in slots}
    domain = set(got) | set(slots)
    pages = {v & ~0xfff for v in domain}
    domain |= pages
    leaders = {0, count} | {a // 4 for a in functions}
    branch_targets = {}
    terminal = set()
    for i, x in enumerate(words):
        target = None
        if x & 0xfc000000 == 0x14000000:
            imm = x & 0x3ffffff
            if imm & 0x2000000: imm -= 0x4000000
            target = i + imm
            terminal.add(i)
        elif x & 0xff000010 == 0x54000000 or x & 0x7e000000 == 0x34000000:
            imm = (x >> 5) & 0x7ffff
            if imm & 0x40000: imm -= 0x80000
            target = i + imm
        elif x & 0x7e000000 == 0x36000000:
            imm = (x >> 5) & 0x3fff
            if imm & 0x2000: imm -= 0x4000
            target = i + imm
        elif x & 0xfffffc1f in (0xd61f0000, 0xd65f0000):
            terminal.add(i)
        if target is not None:
            branch_targets[i] = target
            if 0 <= target < count: leaders.add(target)
        if target is not None or i in terminal: leaders.add(i + 1)
    boundaries = sorted(leaders)
    block_ids = {start: k for k, start in enumerate(boundaries[:-1])}
    successors = []
    for k, (start, end) in enumerate(zip(boundaries, boundaries[1:])):
        tail = end - 1
        edges = []
        target = branch_targets.get(tail)
        if target in block_ids: edges.append(block_ids[target])
        if tail not in terminal and k + 1 < len(boundaries) - 1: edges.append(k + 1)
        successors.append(edges)
    states = {}
    queue = deque(range(len(successors)))
    queued = set(queue)
    calls = {}
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM); md.detail = True
    write_cache = {}

    def assign(state, reg, values):
        values = frozenset(values)
        if values: state[reg] = values
        else: state.pop(reg, None)

    def dereference(addresses):
        result = set()
        for address in addresses:
            if address in slots: result.add(-address - 1)
            elif address in got: result.add(got[address])
        return result

    while queue:
        block = queue.popleft(); queued.remove(block)
        state = dict(states.get(block, {}))
        for i in range(boundaries[block], boundaries[block + 1]):
            x, rd, rn = words[i], words[i] & 31, (words[i] >> 5) & 31
            if x & 0x9f000000 == 0x90000000:
                page = adrp(x, i * 4)
                assign(state, rd, [page] if page in pages else [])
            elif x & 0x9f000000 == 0x10000000:
                imm = ((x >> 29) & 3) | (((x >> 5) & 0x7ffff) << 2)
                if imm & (1 << 20): imm -= 1 << 21
                address = i * 4 + imm
                assign(state, rd, [address] if address in domain else [])
            elif x & 0xff800000 == 0x91000000:
                add = ((x >> 10) & 0xfff) << (12 if x & (1 << 22) else 0)
                assign(state, rd, [v + add for v in state.get(rn, ()) if v >= 0 and v + add in domain])
            elif x & 0xffc00000 == 0xf9400000:
                offset = ((x >> 10) & 0xfff) * 8
                assign(state, rd, dereference(v + offset for v in state.get(rn, ()) if v >= 0))
            elif x & 0xffe0ffe0 == 0xaa0003e0:
                assign(state, rd, state.get((x >> 16) & 31, ()))
            elif x & 0xffe00c00 == 0x9a800000:
                assign(state, rd, set(state.get(rn, ())) | set(state.get((x >> 16) & 31, ())))
            elif x & 0xfffffc1f in (0xd63f0000, 0xd61f0000):
                values = {-v - 1 for v in state.get(rn, ()) if v < 0}
                if values: calls.setdefault(i * 4, set()).update(values)
                if x & 0xfffffc1f == 0xd63f0000:
                    state = {r: v for r, v in state.items() if 19 <= r < 30}
            elif x & 0xfc000000 == 0x94000000:
                state = {r: v for r, v in state.items() if 19 <= r < 30}
            elif state:
                if x not in write_cache:
                    ins = next(md.disasm(data[i * 4:i * 4 + 4], i * 4), None)
                    registers = [] if ins is None else ins.regs_access()[1]
                    names = [md.reg_name(reg) for reg in registers]
                    write_cache[x] = {int(name[1:]) for name in names
                                      if name.startswith(('x', 'w')) and name[1:].isdigit()}
                for reg in write_cache[x]: state.pop(reg, None)
        for following in successors[block]:
            old = states.get(following, {})
            merged = dict(old)
            for reg, values in state.items(): merged[reg] = frozenset(set(old.get(reg, ())) | set(values))
            if merged != old:
                states[following] = merged
                if following not in queued: queue.append(following); queued.add(following)
    uses = {slot: [] for slot in slots}
    for pc, targets in sorted(calls.items()):
        for slot in targets: uses[slot].append(pc)
    return uses


def d2r_implementation(root):
    implemented = set()
    paths = list((root / 'dm/gfx').rglob('*.c'))
    for path in paths:
        text = path.read_text(encoding='utf-8')
        # Strip comments while preserving string literals.
        text = re.sub(r'"(?:\\.|[^"\\])*"|/\*.*?\*/|//[^\n]*',
                      lambda m: ' ' if m[0].startswith(('/', '/*')) else m[0], text, flags=re.S)
        implemented.update(re.findall(r'\bNVN(?:_NOPROTO)?\s*\(\s*(nvn\w+)\s*,', text))
    host = (root / 'dm/gfx/nvn/hle_nvn.c').read_text(encoding='utf-8')
    block = re.search(r'g_nops\[\]\s*=\s*\{(.*?)\};', host, re.S).group(1)
    nops = set(re.findall(r'"(nvn\w+)"', block))
    return implemented, nops


def object_group(name):
    for group in ['CommandBuffer', 'MemoryPool', 'Texture', 'Sampler', 'Program', 'Queue',
                  'Device', 'Sync', 'Window', 'Buffer', 'Event', 'ColorState', 'BlendState',
                  'DepthStencilState', 'MultisampleState', 'PolygonState', 'VertexAttribState',
                  'VertexStreamState']:
        if name.startswith('nvn' + group): return group
    return 'Other'


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('image', type=Path); ap.add_argument('out', type=Path)
    ap.add_argument('--loader', type=lambda x: int(x, 0), required=True)
    ap.add_argument('--text-end', type=lambda x: int(x, 0), required=True)
    ap.add_argument('--d2r-root', type=Path)
    ap.add_argument('--needed-out', type=Path)
    args = ap.parse_args()
    slots, uses, got, end = analyze(args.image.read_bytes(), args.loader, args.text_end)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('w', encoding='utf-8', newline='') as out:
        writer = csv.writer(out, delimiter='\t', lineterminator='\n')
        writer.writerow(['name', 'slot', 'nuses', 'first_use'])
        for slot in sorted(slots):
            calls = uses[slot]
            writer.writerow([slots[slot], hex(slot), len(calls), hex(calls[0]) if calls else '-'])
    used = {slots[slot] for slot in slots if uses[slot]}
    print('Loader:', hex(args.loader), 'to', hex(end), 'slots:', len(slots), 'GOT:', len(got))
    print('Used functions:', len(used), 'function/call-site references:', sum(map(len, uses.values())),
          'distinct call sites:', len({pc for positions in uses.values() for pc in positions}))
    if args.d2r_root and args.needed_out:
        implemented, nops = d2r_implementation(args.d2r_root)
        needed = []
        for slot, name in slots.items():
            if uses[slot] and (name not in implemented or name in nops):
                needed.append([name, object_group(name), len(uses[slot]), 'nop' if name in nops else 'missing'])
        needed.sort(key=lambda row: (-row[2], row[1], row[0]))
        with args.needed_out.open('w', encoding='utf-8', newline='') as out:
            writer = csv.writer(out, delimiter='\t', lineterminator='\n')
            writer.writerow(['name', 'object', 'nuses', 'd2r'])
            writer.writerows(needed)
        print('D2R semantic registrations:', len(implemented), 'nops:', len(nops))
        print('Kirby required:', len(used), 'already implemented:', len(used & (implemented - nops)), 'new:', len(needed))


if __name__ == '__main__':
    main()
