"""Synthetic regressions for dump import (streaming copy, resume), shader scanning, NVN slots and CMP codecs.
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


dump = load('import_dump')
cmp = load('cmp_unpack')
formats = load('romfs_formats')
nvn = load('nvn_slots_kirby')


class ExtractionTests(unittest.TestCase):
    def test_list_format_and_exefs_check(self):
        with tempfile.TemporaryDirectory() as td:
            exefs, romfs = Path(td, 'exefs'), Path(td, 'romfs')
            (romfs / 'shader').mkdir(parents=True)
            (romfs / 'shader' / 'a.bfsha').write_bytes(b'FSHA')
            (romfs / 'empty').write_bytes(b'')
            self.assertEqual(dump.romfs_entries(romfs), [('/empty', 0), ('/shader/a.bfsha', 4)])
            exefs.mkdir()
            for name in dump.NSO_MODULES: (exefs / name).write_bytes(b'NSO0' + bytes(12))
            dump.check_exefs(exefs)
            (exefs / 'sdk').write_bytes(b'\x8f' * 16)
            with self.assertRaises(ValueError): dump.check_exefs(exefs)
            (exefs / 'sdk').unlink()
            with self.assertRaises(FileNotFoundError): dump.check_exefs(exefs)

    def test_chunks_resume_and_partial_replacement(self):
        payload = bytes(range(256)) * (dump.CHUNK_SIZE // 256 + 1)
        with tempfile.TemporaryDirectory() as td:
            src, out = Path(td, 'src'), Path(td, 'out')
            (src / 'nested').mkdir(parents=True)
            (src / 'nested' / 'file').write_bytes(payload)
            (src / 'empty').write_bytes(b'')
            entries = dump.romfs_entries(src)
            target = out / 'nested' / 'file'
            target.parent.mkdir(parents=True)
            target.write_bytes(b'partial')
            self.assertEqual(dump.copy_tree(src, entries, out), (2, 0, len(payload)))
            self.assertEqual(target.read_bytes(), payload)
            self.assertEqual(dump.copy_tree(src, entries, out), (0, 2, len(payload)))

    def test_short_read_never_publishes_and_paths_cannot_escape(self):
        with tempfile.TemporaryDirectory() as td:
            src, out = Path(td, 'src'), Path(td, 'out')
            src.mkdir()
            (src / 'file').write_bytes(b'x')
            with self.assertRaises(EOFError): dump.copy_tree(src, [('/file', 2)], out)
            self.assertFalse((out / 'file').exists())
            for name in ['/../escape', '/C:/escape', '/a\\escape']:
                with self.assertRaises(ValueError): dump.copy_tree(src, [(name, 1)], out)
            (src / 'link').symlink_to(src / 'file')
            with self.assertRaises(ValueError): dump.romfs_entries(src)

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
