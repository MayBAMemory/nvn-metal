"""Apple arm64 syntax / type check of the Kirby host's C sources on Windows (no macOS SDK needed).

System headers come from kirby/tools/applestub/ (minimal stand-ins: declarations only, written for this check) plus clang's
own freestanding headers (stdint, stdatomic, stddef, stdarg). Repo headers (dm/host, dm/gfx) are the real ones.
It catches syntax errors, undeclared identifiers, wrong argument counts / types, printf format mismatches and the
compile-time HLE / NVN signature checks. It is NOT a full Mac build: Objective-C (.m) and assembly (.S) are not checked,
nothing is linked, and a stub that declares something differently from the real SDK can hide or invent a problem.
When a check fails only because a stub lacks a declaration, add the declaration to the stub (never edit the source to suit the stub).

Status 2026-10-14: 33 of the 39 Kirby-build C files pass. The 6 that do not (dm/host/crash.c, hle_fdstat.c, hle_lifecycle.c,
hle_table.c, hle_thread.c, plus a harmless unused-variable warning in hle.c) are untouched D2R files that use Mach thread state,
signals, GCD sources and ctype; their failures are missing stub declarations, not source errors. Extend the stubs if one of them changes.

usage: python -I syntax_check.py [--clang <clang.exe>] [file.c ...]
       (no files = every .c the Kirby host build compiles, per kirby/host/Makefile: dm/host generic files + dm/gfx + kirby/host)
"""
import argparse, shutil, subprocess, sys
from pathlib import Path

repo = Path(__file__).resolve().parents[2]
stub = Path(__file__).resolve().parent / 'applestub'
HOST_EXCL = {'main.c', 'diag.c', 'hle_tact.c', 'hle_granny.c', 'hle_charname.c', 'hle_target.c', 'hle_private_aim.c',
             'hle_hid.c', 'hle_movie.c', 'touch_mac.c', 'romfs_validate.c'}   # same as kirby/host/Makefile HOST_EXCL

def kirby_build_sources():
    host = [repo / 'dm/host' / n for n in ('loader.c', 'hle.c', 'crash.c', 'thread.c')]
    host += [p for p in sorted((repo / 'dm/host').glob('hle_*.c')) if p.name not in HOST_EXCL]
    gfx = sorted((repo / 'dm/gfx').glob('*.c')) + sorted((repo / 'dm/gfx/nvn').glob('*.c')) + sorted((repo / 'dm/gfx/tex').glob('*.c'))
    return host + gfx + sorted((repo / 'kirby/host').glob('*.c'))

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--clang', default=shutil.which('clang') or str(Path.home() / 'scoop/apps/llvm/current/bin/clang.exe'))
    ap.add_argument('files', nargs='*')
    a = ap.parse_args()
    files = [Path(f).resolve() for f in a.files] or kirby_build_sources()
    nbad = nwarn = 0
    for f in files:
        cmd = [a.clang, '--target=arm64-apple-macos13', '-std=gnu11', '-fblocks', '-ffreestanding', '-fsyntax-only', '-Wall',
               '-Wno-unused-function', '-ferror-limit=0', '-isystem', str(stub), '-I', str(f.parent), '-I', str(repo / 'dm/host'),
               '-I', str(repo / 'dm/gfx/nvn'), '-I', str(repo / 'dm/gfx/nvn/include'), str(f)]
        r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
        warn = 'warning:' in r.stderr
        status = 'FAIL' if r.returncode else ('WARN' if warn else 'OK  ')
        nbad += r.returncode != 0; nwarn += warn and r.returncode == 0
        print(f'{status} {f.relative_to(repo) if f.is_relative_to(repo) else f}')
        if r.stderr.strip(): print(r.stderr.strip())
    print(f'--- {len(files)} files: {len(files) - nbad - nwarn} OK, {nwarn} with warnings, {nbad} failed')
    sys.exit(1 if nbad else 0)

if __name__ == '__main__':
    main()
