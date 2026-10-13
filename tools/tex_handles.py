"""Histogram of texture / image handle references in translated Kirby shaders (dmsh_xlate *.res.json from kirby_xlate.py).
For bound (non-bindless) textures Ryujinx reports cbuf_slot -1 and handle = word offset into the texture constant buffer (c[2] on NVN);
byte offset = handle * 4. D2R's separate-handle tables sit at c[2]+0x168 (textures) / 0x568 (samplers) / 0x120 (images);
this shows where Kirby's (combined, nn::gfx BindTexture) handles live.

usage: python -I tex_handles.py <spv dir with *.res.json>
"""
import collections, json, sys
from pathlib import Path

tex = collections.Counter(); img = collections.Counter(); cb = collections.Counter(); sep = collections.Counter(); n = 0
for p in Path(sys.argv[1]).glob('*.res.json'):
    r = json.loads(p.read_text(encoding='utf-8')); n += 1
    for t in r.get('textures', []):
        tex[t['handle'] * 4] += 1; cb[t['cbuf_slot']] += 1; sep[t['separate']] += 1
    for t in r.get('images', []):
        img[t['handle'] * 4] += 1
print(f'{n} shaders; texture refs {sum(tex.values())}, image refs {sum(img.values())}; cbuf_slot {dict(cb)}; separate {dict(sep)}')
for name, c in (('texture', tex), ('image', img)):
    if not c: continue
    offs = sorted(c)
    print(f'{name} byte offsets: min {offs[0]:#x} max {offs[-1]:#x}; all multiples of 8 from min: {all((o - offs[0]) % 8 == 0 for o in offs)}')
    print('  ' + ', '.join(f'{o:#x}:{c[o]}' for o in offs[:48]))
