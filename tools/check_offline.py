"""Check K08 coverage, shared-registration exclusions and real HLE macro syntax.
Usage: python -I check_offline.py --clang <clang.exe> --refs <reloc_refs.txt>
The temporary shim supplies libc declarations only; HLE/HLE_FORCE are read verbatim
from dm/host/hle.h. This is an Apple arm64 syntax check, not a complete Mac build.
"""
import argparse
import csv
import re
import subprocess
import sys
import os
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]                                       # 本仓库根 | this repository
DMROOT = Path(os.environ.get('D2R_MOBILE', ROOT.parent / 'd2r-mobile')).resolve()   # 上游 d2r-mobile 检出 | upstream checkout
sys.path.insert(0,str(Path(__file__).resolve().parent))
from imports_compare import strip_comments
from offline_inventory import NAMESPACES, SHARED


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--clang',required=True);ap.add_argument('--refs',type=Path,required=True)
    args=ap.parse_args();repo=DMROOT
    imports=list(csv.DictReader((ROOT/'analysis/imports_main.tsv').open(encoding='utf-8'),delimiter='\t'))
    target={r['mangled'] for r in imports if r['ns'] in NAMESPACES or r['mangled'].startswith('glslc')}
    policy=list(csv.DictReader((ROOT/'docs/offline_stubs.tsv').open(encoding='utf-8'),delimiter='\t'))
    assert len(policy)==len(target)==103 and {r['mangled'] for r in policy}==target
    code=(ROOT/'host/hle_offline.c').read_text(encoding='utf-8')
    registered=re.findall(r'\bHLE(?:_FORCE)?\("([^"]+)"',strip_comments(code))
    assert len(registered)==len(set(registered))==57
    shared=set()
    for name in SHARED:
        text=strip_comments((DMROOT/'dm/host'/name).read_text(encoding='utf-8'))
        shared.update(re.findall(r'"((?:_Z|nnssl|nnsocket)[^"\n]+)"',text))
    assert not set(registered)&shared
    references=set(args.refs.read_text(encoding='utf-8').splitlines());assert set(registered)<=references
    header=(DMROOT/'dm/host/hle.h').read_text(encoding='utf-8').splitlines()
    macros=[]
    for name in ['HLE','HLE_FORCE']:
        i=next(i for i,line in enumerate(header) if line.startswith('#define '+name+'('))
        while True:
            line=header[i];macros.append(line);i+=1
            if not line.endswith('\\'):break
    preamble='typedef unsigned int uint32_t;\ntypedef struct { const char *name; void *fn; } hle_ent;\ntypedef struct { const char *name; void *fn; const char *why; } hle_force_ent;\ntypedef uint32_t Result;\n#define RES_OK 0u\n#define RES_FAIL 0x2A2u\n#define NULL ((void*)0)\nvoid *memset(void*,int,__SIZE_TYPE__);\nvoid hle_bind_system_event(void*,int);\n_Static_assert(sizeof(unsigned long)==8,"Switch/Apple ulong ABI");\n'
    source=preamble+'\n'.join(macros)+'\n'+code.replace('#include "hle.h"','')
    result=subprocess.run([args.clang,'--target=arm64-apple-macos11','-std=c11','-Werror','-fsyntax-only','-x','c','-'],input=source,encoding='utf-8',capture_output=True)
    if result.returncode:print(result.stderr);raise SystemExit(result.returncode)
    print('K08 coverage/relocations/no-duplicate checks OK; 57 functions passed real-macro Apple arm64 syntax check')


if __name__=='__main__':main()
