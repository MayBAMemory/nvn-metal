"""Minimal XCI -> secure HFS0 -> NCA -> ExeFS/RomFS extractor (pure Python + pycryptodomex).

usage: python -I xci_extract.py <xci> <prod.keys> <outdir> [--romfs]
"""
import os, struct, sys
from Cryptodome.Cipher import AES
from Cryptodome.Util import Counter

def load_keys(path):
    keys = {}
    for line in open(path, encoding='utf-8', errors='ignore'):
        if '=' in line:
            k, v = line.split('=', 1)
            try:
                keys[k.strip()] = bytes.fromhex(v.strip())
            except ValueError:
                pass
    return keys

def gf_mul_alpha(t):
    n = int.from_bytes(t, 'little') << 1
    if n >> 128:
        n = (n & ((1 << 128) - 1)) ^ 0x87
    return n.to_bytes(16, 'little')

def xts_decrypt(key, data, sector_size=0x200, first_sector=0):
    k1, k2 = AES.new(key[:16], AES.MODE_ECB), AES.new(key[16:], AES.MODE_ECB)
    out = bytearray()
    for s in range(len(data) // sector_size):
        t = k2.encrypt((first_sector + s).to_bytes(16, 'big'))
        blk = data[s * sector_size:(s + 1) * sector_size]
        for i in range(0, sector_size, 16):
            x = bytes(a ^ b for a, b in zip(blk[i:i + 16], t))
            out += bytes(a ^ b for a, b in zip(k1.decrypt(x), t))
            t = gf_mul_alpha(t)
    return bytes(out)

def parse_hfs0(f, base):
    f.seek(base)
    magic, n, strsz, _ = struct.unpack('<4sIII', f.read(16))
    assert magic in (b'HFS0', b'PFS0'), magic
    esz = 0x40 if magic == b'HFS0' else 0x18
    ents = [f.read(esz) for _ in range(n)]
    strs = f.read(strsz)
    data_base = base + 16 + n * esz + strsz
    files = {}
    for e in ents:
        off, size, noff = struct.unpack_from('<QQI', e)
        name = strs[noff:strs.index(b'\0', noff)].decode()
        files[name] = (data_base + off, size)
    return files

class NcaSection:
    def __init__(self, f, nca_off, start, end, key, ctr_hi):
        self.f, self.base, self.size = f, nca_off + start, end - start
        self.start, self.key, self.ctr_hi = start, key, ctr_hi

    def read(self, off, size):
        a0 = off & ~0xF
        a1 = (off + size + 0xF) & ~0xF
        self.f.seek(self.base + a0)
        raw = self.f.read(a1 - a0)
        ctr = Counter.new(64, prefix=self.ctr_hi, initial_value=(self.start + a0) >> 4)
        dec = AES.new(self.key, AES.MODE_CTR, counter=ctr).decrypt(raw)
        return dec[off - a0: off - a0 + size]

def open_nca(f, off, keys):
    f.seek(off)
    hdr = xts_decrypt(keys['header_key'], f.read(0xC00))
    assert hdr[0x200:0x204] == b'NCA3', hdr[0x200:0x204]
    ctype, kg_old, kaek_idx = hdr[0x205], hdr[0x206], hdr[0x207]
    title_id = struct.unpack_from('<Q', hdr, 0x210)[0]
    sdk_ver = struct.unpack_from('<I', hdr, 0x21C)[0]
    kg = max(kg_old, hdr[0x220])
    kidx = kg - 1 if kg > 0 else 0
    assert kaek_idx == 0, 'only application key area supported'
    kak = AES.new(keys['key_area_key_application_%02x' % kidx], AES.MODE_ECB)
    key_ctr = kak.decrypt(hdr[0x300 + 0x20:0x300 + 0x30])
    sections = []
    for i in range(4):
        s, e = struct.unpack_from('<II', hdr, 0x240 + i * 0x10)
        if e == 0:
            sections.append(None)
            continue
        fsh = hdr[0x400 + i * 0x200:0x600 + i * 0x200]
        ctr_hi = fsh[0x140:0x148][::-1]
        sections.append((fsh, NcaSection(f, off, s * 0x200, e * 0x200, key_ctr, ctr_hi)))
    return dict(type=ctype, title_id=title_id, sdk=sdk_ver, kg=kg, sections=sections)

def pfs0_in_section(fsh, sec):
    # hash data: master hash(0x20) block size(4) layer count(4) then regions
    pfs_off, pfs_size = struct.unpack_from('<QQ', fsh, 0x8 + 0x38)
    hdr = sec.read(pfs_off, 0x10)
    assert hdr[:4] == b'PFS0', hdr[:4]
    n, strsz = struct.unpack_from('<II', hdr, 4)
    body = sec.read(pfs_off, 0x10 + n * 0x18 + strsz)
    strs = body[0x10 + n * 0x18:]
    data_base = pfs_off + 0x10 + n * 0x18 + strsz
    files = {}
    for i in range(n):
        o, sz, no = struct.unpack_from('<QQI', body, 0x10 + i * 0x18)
        files[strs[no:strs.index(b'\0', no)].decode()] = (data_base + o, sz)
    return files

def romfs_list(fsh, sec):
    # IVFC: level 6 (data) region at hash-data offset 0x8 + 0x10 + 5*0x18
    lvl_off, lvl_size = struct.unpack_from('<QQ', fsh, 0x8 + 0x10 + 5 * 0x18)
    h = sec.read(lvl_off, 0x50)
    vals = struct.unpack_from('<10Q', h)
    _, dh_off, dh_sz, dm_off, dm_sz, fh_off, fh_sz, fm_off, fm_sz, data_off = vals
    dmeta = sec.read(lvl_off + dm_off, dm_sz)
    fmeta = sec.read(lvl_off + fm_off, fm_sz)
    out = []
    def walk_dir(doff, path):
        parent, sib, child, ffile, nxt, nlen = struct.unpack_from('<6I', dmeta, doff)
        name = dmeta[doff + 0x18:doff + 0x18 + nlen].decode('utf-8', 'replace')
        p = path + '/' + name if doff else ''
        fo = ffile
        while fo != 0xFFFFFFFF:
            fpar, fsib, fdoff, fsz, fhn, fnlen = struct.unpack_from('<IIQQII', fmeta, fo)
            fname = fmeta[fo + 0x20:fo + 0x20 + fnlen].decode('utf-8', 'replace')
            out.append((p + '/' + fname, fsz))
            fo = fsib
        c = child
        while c != 0xFFFFFFFF:
            walk_dir(c, p)
            c = struct.unpack_from('<I', dmeta, c + 4)[0]
    walk_dir(0, '')
    return out

def main():
    xci, keyfile, outdir = sys.argv[1:4]
    do_romfs = '--romfs' in sys.argv
    keys = load_keys(keyfile)
    os.makedirs(outdir, exist_ok=True)
    with open(xci, 'rb') as f:
        f.seek(0x100)
        assert f.read(4) == b'HEAD'
        f.seek(0x130)
        root_off = struct.unpack('<Q', f.read(8))[0]
        root = parse_hfs0(f, root_off)
        print('root partitions:', list(root))
        secure = parse_hfs0(f, root['secure'][0])
        for name, (off, size) in secure.items():
            if not name.endswith('.nca'):
                print('  ', name, size)
                continue
            nca = open_nca(f, off, keys)
            sdk = nca['sdk']
            print('  %s type=%d tid=%016x kg=%d sdk=%d.%d.%d.%d size=%d' % (
                name, nca['type'], nca['title_id'], nca['kg'],
                sdk >> 24, (sdk >> 16) & 0xFF, (sdk >> 8) & 0xFF, sdk & 0xFF, size))
            if nca['type'] != 0:
                continue
            fsh, sec = nca['sections'][0]
            exefs = pfs0_in_section(fsh, sec)
            ed = os.path.join(outdir, 'exefs')
            os.makedirs(ed, exist_ok=True)
            for fn, (o, sz) in exefs.items():
                print('     exefs/%s %d' % (fn, sz))
                with open(os.path.join(ed, fn), 'wb') as w:
                    w.write(sec.read(o, sz))
            if nca['sections'][1]:
                fsh1, sec1 = nca['sections'][1]
                lst = romfs_list(fsh1, sec1)
                with open(os.path.join(outdir, 'romfs_list.txt'), 'w', encoding='utf-8') as w:
                    for p, sz in lst:
                        w.write('%d\t%s\n' % (sz, p))
                print('     romfs files:', len(lst), 'total', sum(s for _, s in lst))

main()
