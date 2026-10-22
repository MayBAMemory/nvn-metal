"""Read dynamic symbols and relocations from an NSO memory image.
Based on tools/imports.py and the ELF64 layout; returns addresses as well.
"""
import struct


def symbols(data):
    mod = struct.unpack_from('<I', data, 4)[0]
    if data[mod:mod + 4] != b'MOD0': raise ValueError('Expected MOD0')
    pos = mod + struct.unpack_from('<i', data, mod + 4)[0]
    tags = {}
    while True:
        tag, value = struct.unpack_from('<qQ', data, pos); pos += 16
        if tag == 0: break
        tags.setdefault(tag, value)
    symtab, strings = tags[6], tags[5]
    n = struct.unpack_from('<I', data, tags[4] + 4)[0] if 4 in tags else (strings - symtab) // 24
    rows = []
    for i in range(n):
        offset, info, other, section, address, size = struct.unpack_from('<IBBHQQ', data, symtab + i * 24)
        end = data.index(b'\0', strings + offset)
        name = data[strings + offset:end].decode('utf-8')
        rows.append({'name': name, 'address': address, 'size': size, 'defined': bool(section), 'binding': info >> 4})
    relocs = []
    for address, size in [(tags.get(7, 0), tags.get(8, 0)), (tags.get(23, 0), tags.get(2, 0))]:
        for i in range(size // 24):
            offset, info, addend = struct.unpack_from('<QQq', data, address + i * 24)
            index = info >> 32
            relocs.append({'offset': offset, 'type': info & 0xffffffff, 'name': rows[index]['name'] if index else '', 'addend': addend})
    return rows, relocs
