"""Extract deduplicated NVN control/code pairs from Kirby BFSHA/BNSH containers.
Usage: python -I bnsh_extract.py <RomFS> <shader-out> <index.tsv> --inventory <K03 TSV>
Original implementation. Format references (no implementation copied):
https://github.com/KillzXGaming/ShaderLibrary/tree/790dd2e501926e536a9193907d1256d446104162
D2R key convention: SHA-256 of the complete 0x880-byte NVN control block, first 16 hex digits.
"""
import argparse
import os
from itertools import combinations
import csv
import hashlib
import json
import struct
import sys
from collections import Counter
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from cmp_unpack import cmp_reader

CTRL_SIZE = 0x880
CTRL_MAGIC = 0x98761234
BNSH_MAGIC = b'BNSH' + bytes(4)
FSHA_MAGIC = b'FSHA    '
STAGES = ('vs', 'tcs', 'tes', 'gs', 'fs', 'cs')
# ResShaderFile 2.1.x: grsc header at 0x60; 0x40-byte variation entries;
# six stage descriptors at program+8; pointers are relative to the BNSH start.
LAYOUT = {'grsc': 0x60, 'variation_size': 0x40, 'program_stages': 8,
          'stage_control': 8, 'stage_code': 16, 'stage_sizes': 24}


def checked(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError('BNSH pointer/size outside file')
    return data[offset:offset + size]


def header_size(data, base, magic):
    if bytes(checked(data, base, 8)) != magic: raise ValueError('Wrong container signature')
    if bytes(checked(data, base + 12, 2)) != b'\xff\xfe': raise ValueError('Unsupported byte order')
    size = struct.unpack_from('<I', checked(data, base + 28, 4))[0]
    checked(data, base, size)
    if size < 32: raise ValueError('Container shorter than header')
    return size


def parse_bnsh(data):
    data = memoryview(data)
    size = header_size(data, 0, BNSH_MAGIC)
    data = data[:size]
    major = struct.unpack_from('<H', data, 10)[0]
    if major != 2: raise ValueError('Unsupported BNSH major version')
    block = LAYOUT['grsc']
    if bytes(checked(data, block, 4)) != b'grsc': raise ValueError('Missing grsc')
    count = struct.unpack_from('<I', checked(data, block + 28, 4))[0]
    variations = struct.unpack_from('<Q', checked(data, block + 32, 8))[0]
    checked(data, variations, count * LAYOUT['variation_size'])
    for variation in range(count):
        entry = variations + variation * LAYOUT['variation_size']
        program = struct.unpack_from('<Q', data, entry + 16)[0]
        if not program: continue
        stage_offsets = struct.unpack_from('<6Q', checked(data, program + 8, 48))
        for stage, descriptor in zip(STAGES, stage_offsets):
            if not descriptor: continue
            ctrl_off, code_off, code_size, ctrl_size = struct.unpack_from('<QQII', checked(data, descriptor + 8, 24))
            if ctrl_size != CTRL_SIZE: raise ValueError('Unexpected control block size')
            ctrl = checked(data, ctrl_off, ctrl_size)
            code = checked(data, code_off, code_size)
            if struct.unpack_from('<I', ctrl)[0] != CTRL_MAGIC: raise ValueError('Wrong NVN control magic')
            if not code_size: raise ValueError('Empty binary shader')
            yield stage, variation, ctrl_off, code_off, ctrl, code


def extract(root, output, index, inventory, audit_only=False):
    output.mkdir(parents=True, exist_ok=True)
    containers = [(path, None, None) for path in sorted((root / 'shader').rglob('*.bfsha'))]
    with inventory.open(encoding='utf-8') as input_stream:
        inventory_rows = list(csv.DictReader(input_stream, delimiter='\t'))
    for row in inventory_rows:
        containers.append((root / row['source'], [int(x, 0) for x in row['bnsh_offsets'].split(';') if x],
                           [int(x, 0) for x in row['fsha_offsets'].split(';') if x]))
    controls, pairs, cache = {}, {}, {}
    stage_counts = Counter()
    bnsh_count = largest = 0
    # Stage files by the full control/code identity until all conflicts are known.
    # No candidate is selected as canonical for an ambiguous control-only key.
    def save_checked(target, payload):
        if target.exists():
            if hashlib.sha256(target.read_bytes()).digest() != hashlib.sha256(payload).digest():
                raise ValueError('Different existing shader output: ' + target.name)
        else:
            partial = target.with_name(target.name + '.partial')
            partial.write_bytes(payload); os.replace(partial, target)

    for source_id, (path, offsets, archives) in enumerate(containers, 1):
        if offsets is None:
            raw = path.read_bytes(); header_size(memoryview(raw), 0, FSHA_MAGIC)
            offsets, start = [], 0
            while (start := raw.find(BNSH_MAGIC, start)) >= 0:
                header_size(memoryview(raw), start, BNSH_MAGIC); offsets.append(start); start += 8
        else:
            with cmp_reader(path) as (expected, reader): raw = reader.read(expected + 1)
            if len(raw) != expected: raise ValueError('CMP decoded size mismatch')
            for base in archives: header_size(memoryview(raw), base, FSHA_MAGIC)
        largest = max(largest, len(raw)); view = memoryview(raw)
        for bnsh_number, base in enumerate(offsets):
            size = header_size(view, base, BNSH_MAGIC); blob = view[base:base + size]
            digest = hashlib.sha256(blob).hexdigest(); records = cache.get(digest)
            if records is None:
                records = []
                for stage, variant, ctrl_off, code_off, ctrl, code in parse_bnsh(blob):
                    ctrl_sha = hashlib.sha256(ctrl).hexdigest(); key = ctrl_sha[:16]
                    code_sha = hashlib.sha256(code).hexdigest(); code_hash = code_sha[:16]
                    identity = key, code_hash
                    if key in controls and controls[key]['sha'] != ctrl_sha: raise ValueError('Control SHA truncation collision')
                    controls.setdefault(key, {'sha': ctrl_sha, 'codes': set(), 'occurrences': 0, 'stage': stage})
                    controls[key]['codes'].add(code_hash)
                    if identity in pairs and pairs[identity]['code_sha'] != code_sha: raise ValueError('Code SHA truncation collision')
                    if identity not in pairs:
                        c1_start, c1_end = struct.unpack_from('<2I', ctrl, 0x700)
                        if not 0 <= c1_start <= c1_end <= len(code): raise ValueError('Invalid control c1 bounds')
                        graphics = bytes(code[:4]) == bytes.fromhex('78563412')
                        instruction_start = 0x80 if graphics else 0
                        pairs[identity] = {'stage': stage, 'source': source_id, 'bnsh_offset': hex(base),
                                           'variant': str(bnsh_number) + '/' + str(variant), 'code_size': len(code),
                                           'code_sha': code_sha, 'occurrences': 0, 'c1_start': c1_start, 'c1_end': c1_end,
                                           'instruction_start': instruction_start}
                        if not audit_only:
                            save_checked(output / (key + '.ctrl'), ctrl)
                            save_checked(output / (key + '.' + code_hash + '.code'), code)
                    records.append((identity, stage))
                cache[digest] = records
            for identity, stage in records:
                pairs[identity]['occurrences'] += 1; controls[identity[0]]['occurrences'] += 1; stage_counts[stage] += 1
            bnsh_count += 1
        del view, raw
        if source_id % 100 == 0 or source_id == len(containers):
            print('Shader containers', source_id, '/', len(containers), 'BNSH', bnsh_count,
                  'control keys', len(controls), 'code pairs', len(pairs), flush=True)

    # User's 2026-10-14 decision: only conflicting keys keep a code-hash suffix.
    # An old strict-mode partial output may exist for an ambiguous key; remove it
    # only if it is byte-identical to a candidate from this same extraction.
    if not audit_only:
        for key, control in controls.items():
            plain = output / (key + '.code')
            if len(control['codes']) == 1:
                suffix = next(iter(control['codes'])); named = output / (key + '.' + suffix + '.code')
                if plain.exists() and plain.read_bytes() != named.read_bytes(): raise ValueError('Different existing canonical shader')
                if plain.exists(): named.unlink()
                else: os.replace(named, plain)
            elif plain.exists():
                sha = hashlib.sha256(plain.read_bytes()).hexdigest()
                if sha not in {pairs[key, suffix]['code_sha'] for suffix in control['codes']}:
                    raise ValueError('Refusing to remove unrelated shader output')
                plain.unlink()

    index.parent.mkdir(parents=True, exist_ok=True)
    # All 27239 identities form one logical table. Split physical TSV parts at
    # 900 KiB so each stays within README's small-analysis-file bound.
    columns = ['key', 'code_hash', 'filename', 'stage', 'source', 'variant', 'bnsh_offset',
               'code_size', 'occurrences', 'conflict']
    parts, number, stream = [], 0, None
    try:
        for (key, suffix), item in sorted(pairs.items()):
            if stream is None or stream.tell() >= 900 * 1024:
                if stream: stream.close()
                number += 1
                part = index if number == 1 else index.with_name(index.stem + '.part%02d.tsv' % number)
                stream = part.open('w', encoding='utf-8', newline=''); parts.append(part.name)
                writer = csv.writer(stream, delimiter='\t', lineterminator='\n'); writer.writerow(columns)
            conflict = len(controls[key]['codes']) > 1
            filename = key + ('.' + suffix if conflict else '') + '.code'
            writer.writerow([key, suffix, filename, item['stage'], item['source'], item['variant'], item['bnsh_offset'],
                             item['code_size'], item['occurrences'], int(conflict)])
    finally:
        if stream: stream.close()
    index.with_name('shader_index_parts.txt').write_text('\n'.join(parts) + '\n', encoding='utf-8')
    with index.with_name('shader_sources.tsv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n'); writer.writerow(['id', 'source'])
        writer.writerows((n, path.relative_to(root).as_posix()) for n, (path, _, _) in enumerate(containers, 1))
    with index.with_name('shader_control_collisions.tsv').open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n'); writer.writerow(['key', 'code_variants', 'code_hashes'])
        for key, control in sorted(controls.items()):
            if len(control['codes']) > 1: writer.writerow([key, len(control['codes']), ';'.join(sorted(control['codes']))])
    summary = {'containers': len(containers), 'bnsh_occurrences': bnsh_count, 'unique_bnsh': len(cache),
               'shader_occurrences': sum(stage_counts.values()), 'stages': dict(stage_counts),
               'unique_keys': len(controls), 'unique_stages': dict(Counter(c['stage'] for c in controls.values())),
               'unique_control_code_pairs': len(pairs), 'pair_stages': dict(Counter(p['stage'] for p in pairs.values())),
               'ambiguous_control_keys': sum(len(c['codes']) > 1 for c in controls.values()),
               'largest_decoded_container': largest, 'index_parts': parts, 'audit_only': audit_only}
    (output / 'extraction_summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    if not audit_only:
        analyze_code_differences(output, index, controls, pairs)
    print(json.dumps(summary, ensure_ascii=False))
    return summary


def analyze_code_differences(output, index, controls, pairs):
    rows = []
    for key, control in sorted(controls.items()):
        if len(control['codes']) < 2: continue
        variants = sorted(control['codes'])
        blobs = {suffix: (output / (key + '.' + suffix + '.code')).read_bytes() for suffix in variants}
        for reference, candidate in combinations(variants, 2):
            old, new = blobs[reference], blobs[candidate]
            item = pairs[key, reference]
            begin, c1_begin, c1_end = item['instruction_start'], item['c1_start'], item['c1_end']
            different = [offset for offset in range(max(len(old), len(new)))
                         if offset >= min(len(old), len(new)) or old[offset] != new[offset]]
            locations = Counter('header' if off < begin else 'instructions' if off < c1_begin else
                                'c1' if off < c1_end else 'padding' for off in different)
            rows.append([key, reference, candidate, item['stage'], len(old), len(new), begin, c1_begin, c1_end,
                         locations['header'], locations['instructions'], locations['c1'], locations['padding'],
                         hex(min(different)), hex(max(different))])
    path = index.with_name('shader_code_differences.tsv')
    with path.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
        writer.writerow(['key', 'reference_code', 'candidate_code', 'stage', 'reference_size', 'candidate_size',
                         'instruction_start', 'c1_start', 'c1_end', 'header_diff', 'instruction_diff', 'c1_diff',
                         'padding_diff', 'first_difference', 'last_difference'])
        writer.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root', type=Path); ap.add_argument('output', type=Path); ap.add_argument('index', type=Path)
    ap.add_argument('--inventory', type=Path, required=True)
    ap.add_argument('--audit-only', action='store_true', help='scan every shader and record conflicting keys without writing binaries')
    args = ap.parse_args()
    extract(args.root, args.output, args.index, args.inventory, args.audit_only)


if __name__ == '__main__':
    main()
