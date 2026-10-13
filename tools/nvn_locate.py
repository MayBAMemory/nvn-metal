"""Locate NVN loader functions from name references and NSO unwind entries.
Usage: python -I nvn_locate.py <main.img> <main NSO> [--name nvnDeviceInitialize]
"""
import argparse
import bisect
import struct
import sys
from collections import Counter
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import verify_nsoimg as nsoimg

PROBE_NAMES = ('nvnDeviceInitialize', 'nvnCommandBufferInitialize', 'nvnQueueSubmitCommands')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('image', type=Path); ap.add_argument('nso', type=Path)
    ap.add_argument('--name', action='append')
    args = ap.parse_args()
    data = args.image.read_bytes()
    with args.nso.open('rb') as stream: header = stream.read(0x100)
    if header[:4] != b'NSO0': raise ValueError('Expected NSO0')
    text_end = struct.unpack_from('<I', header, 0x18)[0]
    starts = nsoimg.func_starts(data, text_end)
    targets = {data.index(name.encode('ascii') + b'\0'): name for name in (args.name or PROBE_NAMES)}
    words = struct.unpack_from('<%dI' % (text_end // 4), data)
    print('BUILD_ID:', header[0x40:0x50].hex(), 'TEXT_END:', hex(text_end))
    found = Counter()
    for i, word in enumerate(words):
        if word & 0x9f000000 != 0x90000000: continue
        reg = word & 31
        imm = ((word >> 29) & 3) | (((word >> 5) & 0x7ffff) << 2)
        if imm & (1 << 20): imm -= 1 << 21
        page = (i * 4 & ~0xfff) + (imm << 12)
        for j in range(i + 1, min(i + 7, len(words))):
            other = words[j]
            if other & 0xffc00000 == 0x91000000 and (other >> 5) & 31 == reg:
                address = page + ((other >> 10) & 0xfff)
                if address in targets:
                    function = starts[bisect.bisect_right(starts, i * 4) - 1]
                    print(targets[address], 'string=', hex(address), 'reference=', hex(i * 4),
                          'function=', hex(function))
                    found[function] += 1
            if other & 31 == reg and other & 0xffc00000 != 0x91000000: break
    print('Candidate functions:', [(hex(function), count) for function, count in found.most_common()])


if __name__ == '__main__':
    main()
