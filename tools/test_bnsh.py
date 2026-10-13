"""Synthetic pointer-boundary and conflicting-control extraction regressions.
Usage: python -I test_bnsh.py
No user keys or game files are used.
"""
import csv
import hashlib
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

spec=importlib.util.spec_from_file_location('bnsh_extract',Path(__file__).with_name('bnsh_extract.py'))
extractor=importlib.util.module_from_spec(spec);spec.loader.exec_module(extractor)


def fixture():
    blob=bytearray(0x1200)
    blob[:8]=extractor.BNSH_MAGIC
    struct.pack_into('<H',blob,10,2);blob[12:14]=bytes.fromhex('fffe')
    struct.pack_into('<I',blob,28,len(blob));blob[0x60:0x64]=b'grsc'
    struct.pack_into('<I',blob,0x7c,2);struct.pack_into('<Q',blob,0x80,0x100)
    for i in range(2):
        program,descriptor,code=0x200+i*0x100,0x400+i*0x40,0x1000+i*0x100
        struct.pack_into('<Q',blob,0x100+i*0x40+16,program)
        struct.pack_into('<Q',blob,program+8,descriptor)
        struct.pack_into('<QQII',blob,descriptor+8,0x600,code,0x100,0x880)
        struct.pack_into('<I',blob,code,0x12345678)
        blob[code+0xa0]=i+1
    struct.pack_into('<I',blob,0x600,0x98761234)
    struct.pack_into('<2I',blob,0x600+0x700,0x100,0x100)
    return blob


class BnshTests(unittest.TestCase):
    def test_bounds_and_variants(self):
        blob=fixture();records=list(extractor.parse_bnsh(blob))
        self.assertEqual([r[1] for r in records],[0,1])
        self.assertEqual(bytes(records[0][4]),bytes(records[1][4]))
        self.assertNotEqual(bytes(records[0][5]),bytes(records[1][5]))
        struct.pack_into('<Q',blob,0x408,len(blob)+1)
        with self.assertRaises(ValueError):list(extractor.parse_bnsh(blob))

    def test_collision_names_index_and_rerun(self):
        with tempfile.TemporaryDirectory() as td:
            root=Path(td,'RomFS');(root/'shader').mkdir(parents=True)
            data=fixture();archive=bytearray(0x100)+data
            archive[:8]=extractor.FSHA_MAGIC;archive[12:14]=bytes.fromhex('fffe');struct.pack_into('<I',archive,28,len(archive))
            (root/'shader/One.bfsha').write_bytes(archive)
            inventory=Path(td,'inventory.tsv');inventory.write_text('source\tfsha_offsets\tbnsh_offsets\n')
            output=Path(td,'shaders');index=Path(td,'analysis/shaders.tsv')
            summary=extractor.extract(root,output,index,inventory)
            self.assertEqual((summary['unique_keys'],summary['unique_control_code_pairs'],summary['ambiguous_control_keys']),(1,2,1))
            with index.open() as stream:
                rows=list(csv.DictReader(stream,delimiter='\t'))
            self.assertEqual(len(rows),2)
            for row in rows:
                self.assertEqual(row['conflict'],'1')
                self.assertEqual(row['filename'],row['key']+'.'+row['code_hash']+'.code')
                self.assertEqual(hashlib.sha256((output/row['filename']).read_bytes()).hexdigest()[:16],row['code_hash'])
                self.assertFalse((output/(row['key']+'.code')).exists())
            again=extractor.extract(root,output,index,inventory)
            self.assertEqual(summary,again)


if __name__=='__main__':unittest.main()
