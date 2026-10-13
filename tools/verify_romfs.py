"""Verify extracted RomFS against its manifest and known resource signatures.
Usage: python -I verify_romfs.py <RomFS directory> <romfs_list.txt>
"""
import argparse
from pathlib import Path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root', type=Path)
    ap.add_argument('manifest', type=Path)
    args = ap.parse_args()
    expected = {}
    for line in args.manifest.read_text(encoding='utf-8').splitlines():
        size, name = line.split('\t', 1)
        expected[name.lstrip('/')] = int(size)
    actual = {p.relative_to(args.root).as_posix(): p.stat().st_size
              for p in args.root.rglob('*') if p.is_file()}
    assert actual == expected, 'Extracted paths or sizes differ from manifest'
    checks = [('shader', '.bfsha', b'FSHA'), ('mov', '.webm', bytes.fromhex('1a45dfa3')),
              ('msg', '.msbt', b'MsgStdBn')]
    for folder, ext, magic in checks:
        paths = list((args.root / folder).rglob('*' + ext))
        assert paths, 'No files for signature check'
        for path in paths:
            with path.open('rb') as stream:
                assert stream.read(len(magic)) == magic, path.relative_to(args.root)
        print(folder, ext, 'magic OK:', len(paths))
    print('RomFS OK:', len(actual), 'files,', sum(actual.values()), 'bytes')


if __name__ == '__main__':
    main()
