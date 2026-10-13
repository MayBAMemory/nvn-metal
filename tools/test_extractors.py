"""Synthetic regressions for streaming, RomFS offsets, resume and CMP codecs.
Usage: python -I test_extractors.py
No user keys or game data are read.
"""
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path
import lz4.frame
import zstandard as zstd


def load(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


xci = load('xci_extract')
cmp = load('cmp_unpack')
formats = load('romfs_formats')
nvn = load('nvn_slots_kirby')


class Section:
    def __init__(self, blob):
        self.blob = blob
        self.calls = []

    def read(self, offset, size):
        self.calls.append((offset, size))
        return self.blob[offset:offset + size]


class ExtractionTests(unittest.TestCase):
    def test_ivfc_and_data_offsets(self):
        fsh = bytearray(0x200)
        level = 0x1300
        data_off = 0x240
        payload = b'offset check'
        blob = bytearray(0x2000)
        struct.pack_into('<QQ', fsh, 0x8 + 0x10 + 5 * 0x18, level, 0x500)
        struct.pack_into('<10Q', blob, level, 0x50, 0, 0, 0x50, 24, 0, 0, 0x80, 36, data_off)
        struct.pack_into('<6I', blob, level + 0x50, 0xffffffff, 0xffffffff, 0xffffffff, 0, 0xffffffff, 0)
        struct.pack_into('<IIQQII', blob, level + 0x80, 0, 0xffffffff, 7, len(payload), 0xffffffff, 4)
        blob[level + 0xa0:level + 0xa4] = b'file'
        blob[level + data_off + 7:level + data_off + 7 + len(payload)] = payload
        sec = Section(blob)
        entries = xci.romfs_entries(fsh, sec)
        self.assertEqual(entries, [('/file', len(payload), level + data_off + 7)])
        with tempfile.TemporaryDirectory() as td:
            xci.extract_romfs(sec, entries, td)
            self.assertEqual(Path(td, 'file').read_bytes(), payload)

    def test_chunks_resume_and_partial_replacement(self):
        payload = bytes(range(256)) * (xci.CHUNK_SIZE // 256 + 1)
        sec = Section(b'prefix' + payload)
        entries = [('/nested/file', len(payload), 6), ('/empty', 0, 0)]
        with tempfile.TemporaryDirectory() as td:
            target = Path(td, 'nested', 'file')
            target.parent.mkdir()
            target.write_bytes(b'partial')
            self.assertEqual(xci.extract_romfs(sec, entries, td), (2, 0, len(payload)))
            self.assertEqual(target.read_bytes(), payload)
            self.assertEqual([n for _, n in sec.calls], [xci.CHUNK_SIZE, 256])
            sec.calls.clear()
            self.assertEqual(xci.extract_romfs(sec, entries, td), (0, 2, len(payload)))
            self.assertFalse(sec.calls)

    def test_short_read_never_publishes_and_paths_cannot_escape(self):
        with tempfile.TemporaryDirectory() as td:
            with self.assertRaises(EOFError): xci.extract_romfs(Section(b'x'), [('/file', 2, 0)], td)
            self.assertFalse(Path(td, 'file').exists())
            for name in ['/../escape', '/C:/escape', '/a\\escape']:
                with self.assertRaises(ValueError): xci.extract_romfs(Section(b'x'), [(name, 1, 0)], td)

    def test_shader_signatures_chunk_boundary_and_bounds(self):
        import io
        header = bytearray(32)
        header[:8] = b'FSHA    '
        header[12:14] = bytes.fromhex('fffe')
        struct.pack_into('<I', header, 28, 32)
        second = bytearray(header)
        second[:8] = b'BNSH' + bytes(4)
        blob = b'x' * (formats.READ_SIZE - 3) + header + b'x' + second
        size, found = formats.shader_headers(io.BytesIO(blob), len(blob))
        self.assertEqual(size, len(blob))
        self.assertEqual(found, [('BNSH', formats.READ_SIZE + 30), ('FSHA', formats.READ_SIZE - 3)])
        self.assertEqual(formats.shader_headers(io.BytesIO(blob), 1)[1], [])

    def test_nvn_branch_join_and_call_clobber(self):
        # adrp x8,0x4000; cbz w0,16; ldr x8,[x8]; b 20;
        # ldr x8,[x8,8]; ldr x8,[x8]; blr x8; blr x8; ret.
        words = [0x90000028, 0x34000060, 0xf9400108, 0x14000002,
                 0xf9400508, 0xf9400108, 0xd63f0100, 0xd63f0100, 0xd65f03c0]
        data = struct.pack('<%dI' % len(words), *words)
        result = nvn.static_calls(data, words, {0x5000: 'nvnA', 0x5008: 'nvnB'},
                                  {0x4000: 0x5000, 0x4008: 0x5008}, [0])
        self.assertEqual(result, {0x5000: [24], 0x5008: [24]})

    def test_cmp_codecs_and_size_checks(self):
        payload = b'FRES    ' + bytes(range(256)) * 9000
        with tempfile.TemporaryDirectory() as td:
            source, target = Path(td, 'in.cmp'), Path(td, 'out')
            for encode in [zstd.ZstdCompressor().compress, lz4.frame.compress]:
                source.write_bytes(struct.pack('<I', len(payload)) + encode(payload))
                self.assertEqual(cmp.unpack(source, target), len(payload))
                self.assertEqual(target.read_bytes(), payload)
                source.write_bytes(struct.pack('<I', len(payload) + 1) + encode(payload))
                with self.assertRaises(ValueError): cmp.unpack(source, target)
            source.write_bytes(struct.pack('<I', len(payload)) + zstd.ZstdCompressor().compress(payload)[:-5])
            with self.assertRaises((ValueError, zstd.ZstdError)): cmp.unpack(source, target)


if __name__ == '__main__':
    unittest.main()
