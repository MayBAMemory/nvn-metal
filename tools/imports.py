"""List undefined (imported) and defined dynamic symbols of a decompressed NSO image.
usage: python -I imports.py <img> <out_prefix>
"""
import struct, sys

def dynsyms(img):
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
    symtab, strtab, strsz = tags[6], tags[5], tags[10]
    if 4 in tags:
        nsym = struct.unpack_from('<I', img, tags[4] + 4)[0]
    else:
        nsym = (strtab - symtab) // 24
    out = []
    for k in range(1, nsym):
        name_off, info, other, shndx, value, size = struct.unpack_from('<IBBHQQ', img, symtab + k * 24)
        s = img[strtab + name_off:img.index(b'\0', strtab + name_off)].decode()
        out.append((s, shndx != 0, info >> 4))
    return out

img = open(sys.argv[1], 'rb').read()
syms = dynsyms(img)
und = sorted(s for s, d, b in syms if not d and s)
dfn = sorted(s for s, d, b in syms if d and s)
open(sys.argv[2] + '.imports.txt', 'w').write('\n'.join(und))
open(sys.argv[2] + '.exports.txt', 'w').write('\n'.join(dfn))
print('imports', len(und), 'exports', len(dfn))
