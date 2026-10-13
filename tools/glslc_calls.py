"""Locate GLSLC GOT getters and their caller chain in Kirby main.
Usage: python -I glslc_calls.py <main.img> <main NSO> <out.tsv>
All version-sensitive addresses are discovered from relocations and unwind data.
"""
import argparse
import bisect
import csv
import struct
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from module_symbols import symbols
from verify_nsoimg import func_starts


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('image',type=Path);ap.add_argument('nso',type=Path);ap.add_argument('out',type=Path)
    ap.add_argument('--interface-getter',type=lambda v:int(v,0),help='compiler-interface pointer getter, discovered by disassembling the initializer caller')
    args=ap.parse_args();data=args.image.read_bytes()
    with args.nso.open('rb') as f:header=f.read(0x100)
    end=struct.unpack_from('<I',header,0x18)[0];words=struct.unpack_from('<%dI'%(end//4),data)
    starts=func_starts(data,end);_,rel=symbols(data)
    got={r['offset']:r['name'] for r in rel if r['name'].startswith('glslc')}
    getters={};calls={}
    for i,x in enumerate(words):
        if x&0x9f000000!=0x90000000:continue
        reg=x&31;imm=((x>>29)&3)|(((x>>5)&0x7ffff)<<2)
        if imm&(1<<20):imm-=1<<21
        page=(i*4&~0xfff)+(imm<<12)
        for j in range(i+1,min(i+9,len(words))):
            y=words[j]
            if y&0xffc00000==0xf9400000 and (y>>5)&31==reg:
                address=page+((y>>10)&0xfff)*8
                if address in got:
                    fn=starts[bisect.bisect_right(starts,j*4)-1]
                    getters[fn]=(got[address],address,j*4)
            if y&31==reg:break
    for i,x in enumerate(words):
        if x&0x7c000000!=0x14000000:continue
        imm=x&0x3ffffff
        if imm&0x2000000:imm-=0x4000000
        target=i*4+imm*4
        calls.setdefault(target,[]).append(i*4)
    args.out.parent.mkdir(parents=True,exist_ok=True)
    with args.out.open('w',encoding='utf-8',newline='') as f:
        w=csv.writer(f,delimiter='\t',lineterminator='\n')
        w.writerow(['name','got','got_reference','getter','getter_call','initializer','initializer_call','shader_function'])
        for getter,(name,address,reference) in sorted(getters.items()):
            for pc in calls.get(getter,[]):
                parent=starts[bisect.bisect_right(starts,pc)-1]
                for upper in calls.get(parent,[]):
                    upper_fn=starts[bisect.bisect_right(starts,upper)-1]
                    w.writerow([name,hex(address),hex(reference),hex(getter),hex(pc),hex(parent),hex(upper),hex(upper_fn)])
    if args.interface_getter is not None:
        offsets={}
        for getter,(name,_,_) in getters.items():
            for call in calls.get(getter,[]):
                store=words[call//4+1]
                if store&0xffc0001f==0xf9000000:
                    offsets[((store>>10)&0xfff)*8]=name
        callback_rows=[]
        for i,x in enumerate(words):
            if x&0xfc000000!=0x94000000:continue
            imm=x&0x3ffffff
            if imm&0x2000000:imm-=0x4000000
            if i*4+imm*4!=args.interface_getter:continue
            for j in range(i+1,min(i+6,len(words))):
                load=words[j]
                if load&0xffc00000==0xf9400000 and (load>>5)&31==0:
                    offset=((load>>10)&0xfff)*8;register=load&31
                    if offset in offsets:
                        for k in range(j+1,min(j+22,len(words))):
                            branch=words[k]
                            if branch&0xfffffc1f==0xd63f0000 and (branch>>5)&31==register:
                                callback_rows.append([offsets[offset],hex(k*4),hex(starts[bisect.bisect_right(starts,k*4)-1]),hex(j*4),hex(offset)]);break
                if load&0xfc000000==0x94000000:break
        with args.out.with_name('glslc_indirect_calls.tsv').open('w',encoding='utf-8',newline='') as f:
            writer=csv.writer(f,delimiter='\t',lineterminator='\n')
            writer.writerow(['name','call','function','pointer_load','interface_offset']);writer.writerows(callback_rows)
        print('Compiler interface callbacks',len(callback_rows))
    print('GLSLC GOT names',len(got),'getters',len(getters),'chain table written')


if __name__=='__main__':main()
