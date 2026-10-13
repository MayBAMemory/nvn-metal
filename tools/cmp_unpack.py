"""Unpack Kirby CMP/CMPBIN: little-endian uint32 size + Zstandard or LZ4 frame.
Usage: python -I cmp_unpack.py <input> <output>
The implementation is original; format identified from local file headers.
"""
import argparse
import os
import struct
from contextlib import contextmanager
from pathlib import Path
import zstandard as zstd
import lz4.frame

CHUNK_SIZE = 1024 * 1024
ZSTD_MAGIC = bytes.fromhex('28b52ffd')
LZ4_MAGIC = bytes.fromhex('04224d18')


def cmp_header(stream):
    head = stream.read(22)
    if len(head) < 8:
        raise ValueError('CMP header is truncated')
    expected = struct.unpack_from('<I', head)[0]
    if head[4:8] == ZSTD_MAGIC:
        codec = 'zstd'
        frame_size = zstd.frame_content_size(head[4:])
        if frame_size not in (zstd.CONTENTSIZE_UNKNOWN, expected):
            raise ValueError('CMP size and Zstandard content size disagree')
    elif head[4:8] == LZ4_MAGIC:
        codec = 'lz4'
        frame_size = lz4.frame.get_frame_info(head[4:])['content_size']
        if frame_size not in (0, expected):
            raise ValueError('CMP size and LZ4 content size disagree')
    else:
        raise ValueError('Expected CMP size followed by Zstandard or LZ4 frame')
    stream.seek(4)
    return expected, codec


@contextmanager
def cmp_reader(path):
    with Path(path).open('rb') as source:
        expected, codec = cmp_header(source)
        decoder = (zstd.ZstdDecompressor().stream_reader(source) if codec == 'zstd'
                   else lz4.frame.open(source, mode='rb'))
        with decoder as reader:
            yield expected, reader


def unpack(source, target):
    target = Path(target)
    if Path(source).resolve() == target.resolve():
        raise ValueError('Input and output must differ')
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + '.kirby-partial')
    with cmp_reader(source) as (expected, reader), partial.open('wb') as output:
        total = 0
        while chunk := reader.read(CHUNK_SIZE):
            total += len(chunk)
            if total > expected:
                raise ValueError('Decompressed CMP exceeds declared size')
            output.write(chunk)
        if total != expected:
            raise ValueError('Decompressed CMP size differs from declared size')
    os.replace(partial, target)
    return total


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source')
    parser.add_argument('target')
    args = parser.parse_args()
    print('Unpacked bytes:', unpack(args.source, args.target))


if __name__ == '__main__':
    main()
