"""Import an already-extracted ExeFS / RomFS dump into the Kirby work directory.

The repository ships no decryption code and reads no keys. Extract the program's ExeFS and RomFS from your own
copy with an external tool first (for example nxdumptool on your own Switch), then run:

usage: python -I import_dump.py --exefs <ExeFS dir> --romfs <RomFS dir> <outdir> [--romfs-out <directory>]

Writes <outdir>/exefs/ (main, sdk, subsdk0, ...), <outdir>/romfs_list.txt (size<TAB>/path, the format
verify_romfs.py reads) and, with --romfs-out, a copy of the RomFS tree.
"""
import argparse, os, shutil
from pathlib import Path

CHUNK_SIZE = 8 * 1024 * 1024
NSO_MODULES = ('main', 'sdk', 'subsdk0')
# Kirby and the Forgotten Land v1.0.0, measured in K01
EXPECTED_ROMFS = (9373, 6140261775)


def check_exefs(src):
    for name in NSO_MODULES:
        path = src / name
        if not path.is_file():
            raise FileNotFoundError('ExeFS is missing %s' % name)
        with path.open('rb') as f:
            if f.read(4) != b'NSO0':
                raise ValueError('%s is not an NSO (still encrypted, or not an ExeFS directory?)' % name)


def romfs_entries(root):
    root = Path(root).resolve()
    out = []
    for p in sorted(root.rglob('*')):
        if p.is_symlink():
            raise ValueError('Symlink in RomFS: %s' % p.relative_to(root))
        if p.is_file():
            out.append(('/' + p.relative_to(root).as_posix(), p.stat().st_size))
    return out


def copy_tree(src, entries, outdir):
    src, root = Path(src).resolve(), Path(outdir).resolve()
    root.mkdir(parents=True, exist_ok=True)
    written = skipped = total = 0
    for index, (name, size) in enumerate(entries, 1):
        parts = name.lstrip('/').split('/')
        if any(part in ('', '.', '..') or ':' in part or '\\' in part for part in parts):
            raise ValueError('Invalid RomFS path')
        target = root.joinpath(*parts)
        if not target.resolve().is_relative_to(root):
            raise ValueError('RomFS path escapes output directory')
        if target.is_file() and target.stat().st_size == size:
            skipped += 1
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            # Publish only complete files; interrupted copies cannot qualify for skipping.
            partial = target.with_name(target.name + '.kirby-partial')
            with src.joinpath(*parts).open('rb') as inp, partial.open('wb') as output:
                done = 0
                while done < size:
                    chunk = inp.read(min(CHUNK_SIZE, size - done))
                    if not chunk:
                        raise EOFError('Short RomFS read')
                    output.write(chunk)
                    done += len(chunk)
            os.replace(partial, target)
            written += 1
        total += size
        if index % 100 == 0 or index == len(entries):
            print('     RomFS %d/%d bytes=%d written=%d skipped=%d' %
                  (index, len(entries), total, written, skipped), flush=True)
    return written, skipped, total


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--exefs', type=Path, required=True)
    parser.add_argument('--romfs', type=Path, required=True)
    parser.add_argument('outdir', type=Path)
    parser.add_argument('--romfs-out', type=Path, help='copy the RomFS tree to this directory')
    args = parser.parse_args()
    check_exefs(args.exefs)
    ed = args.outdir / 'exefs'
    ed.mkdir(parents=True, exist_ok=True)
    for p in sorted(args.exefs.iterdir()):
        if p.is_file():
            print('     exefs/%s %d' % (p.name, p.stat().st_size))
            shutil.copyfile(p, ed / p.name)
    entries = romfs_entries(args.romfs)
    with open(args.outdir / 'romfs_list.txt', 'w', encoding='utf-8') as w:
        for p, sz in entries:
            w.write('%d\t%s\n' % (sz, p))
    count, size = len(entries), sum(s for _, s in entries)
    print('     romfs files:', count, 'total', size)
    if (count, size) != EXPECTED_ROMFS:
        print('     注意：v1.0.0 应是 %d 个文件、%d 字节；版本不同或导出不完整 | expected v1.0.0 numbers differ' % EXPECTED_ROMFS)
    if args.romfs_out and args.romfs_out.resolve() != args.romfs.resolve():
        copy_tree(args.romfs, entries, args.romfs_out)


if __name__ == '__main__':
    main()
