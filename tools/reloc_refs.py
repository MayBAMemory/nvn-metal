"""List every symbol name referenced by a relocation (DT_RELA + DT_JMPREL, sym != 0) in the given NSO images.
The host's HLE_FORCE entries are matched while resolving these relocations (dm/host/loader.c force_init / stubs):
an entry whose name never shows up here is reported "强制接管条目无任何引用" and the host aborts at load.

usage: python -I reloc_refs.py <out.txt> <module.img>...   (images from tools/nso_info.py)
"""
import struct, sys

def refs(img):
    mod0 = struct.unpack_from('<I', img, 4)[0]
    assert img[mod0:mod0 + 4] == b'MOD0'
    dyn = mod0 + struct.unpack_from('<i', img, mod0 + 4)[0]
    tags = {}
    i = dyn
    while True:
        t, v = struct.unpack_from('<qQ', img, i)
        i += 16
        if t == 0:
            break
        tags.setdefault(t, v)
    symtab, strtab = tags[6], tags[5]
    names = set()
    for off_tag, size_tag in ((7, 8), (23, 2)):          # DT_RELA/DT_RELASZ, DT_JMPREL/DT_PLTRELSZ
        if off_tag not in tags:
            continue
        base, size = tags[off_tag], tags[size_tag]
        for k in range(0, size, 24):
            _, info, _ = struct.unpack_from('<QQq', img, base + k)
            si = info >> 32
            if si:
                no = struct.unpack_from('<I', img, symtab + si * 24)[0]
                names.add(img[strtab + no:img.index(b'\0', strtab + no)].decode())
    return names

out = sys.argv[1]
allnames = set()
for p in sys.argv[2:]:
    n = refs(open(p, 'rb').read())
    print(p, len(n))
    allnames |= n
open(out, 'w').write('\n'.join(sorted(allnames)) + '\n')
print('total distinct', len(allnames))
