"""Verify every K06 control/code output and every index identity.
Usage: python -I verify_k06.py <analysis-directory> <shader-directory>
"""
import argparse
import csv
import hashlib
import json
from collections import Counter, defaultdict
from pathlib import Path


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('analysis',type=Path);ap.add_argument('shaders',type=Path)
    args=ap.parse_args();rows=[]
    for name in (args.analysis/'shader_index_parts.txt').read_text().splitlines():
        part=args.analysis/name
        assert part.stat().st_size<=1024*1024
        rows.extend(csv.DictReader(part.open(encoding='utf-8'),delimiter='\t'))
    identities={(r['key'],r['code_hash']) for r in rows}
    assert len(rows)==len(identities)==27239
    keys={r['key'] for r in rows};assert len(keys)==26267
    variants=defaultdict(set)
    for row in rows:variants[row['key']].add(row['code_hash'])
    assert sum(len(v)>1 for v in variants.values())==766
    for key in keys:
        data=(args.shaders/(key+'.ctrl')).read_bytes()
        assert len(data)==0x880 and hashlib.sha256(data).hexdigest()[:16]==key
    for row in rows:
        data=(args.shaders/row['filename']).read_bytes()
        assert len(data)==int(row['code_size'])
        assert hashlib.sha256(data).hexdigest()[:16]==row['code_hash']
        conflict=len(variants[row['key']])>1
        assert int(row['conflict'])==int(conflict)
        expected=row['key']+('.'+row['code_hash'] if conflict else '')+'.code'
        assert row['filename']==expected
        if conflict:assert not (args.shaders/(row['key']+'.code')).exists()
    expected={r['filename'] for r in rows}|{key+'.ctrl' for key in keys}|{'extraction_summary.json'}
    assert {p.name for p in args.shaders.iterdir() if p.is_file()}==expected
    counts=Counter()
    for row in rows:counts[row['stage']]+=int(row['occurrences'])
    assert dict(counts)=={'vs':111486,'fs':111485,'cs':458,'gs':50}
    assert sum(counts.values())==223479
    print('K06 verified: 27239 pairs, 26267 controls, 766 conflict keys; every hash/size/name matched; total occurrences 223479')


if __name__=='__main__':main()
