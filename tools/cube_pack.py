#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Convert an sRGB -> sRGB .cube to a checked 17/33 lattice. Standard library only.
Does NOT convert between colour spaces. Rejects 1D/shaper LUTs and unknown directives.
"""
import argparse, math, struct, shlex
from pathlib import Path

def load(path):
    n=None;lo=[0.,0.,0.];hi=[1.,1.,1.];values=[]
    for line in Path(path).read_text().splitlines():
        tok=shlex.split(line,comments=True)
        if not tok:continue
        if tok[0]=='TITLE':continue
        if tok[0]=='LUT_3D_SIZE':
            if n is not None:raise ValueError('duplicate size')
            n=int(tok[1]);continue
        if tok[0]=='DOMAIN_MIN':lo=list(map(float,tok[1:]));continue
        if tok[0]=='DOMAIN_MAX':hi=list(map(float,tok[1:]));continue
        if len(tok)!=3:raise ValueError('unsupported directive or malformed row')
        values.append(tuple(map(float,tok)))
    if n is None or not 2<=n<=65 or len(values)!=n**3:raise ValueError('invalid size or row count')
    if len(lo)!=3 or len(hi)!=3 or any(not math.isfinite(v) for row in [lo,hi]+values for v in row):raise ValueError('invalid numeric value')
    if any(b<=a for a,b in zip(lo,hi)):raise ValueError('invalid domain')
    return n,lo,hi,values

def sample(cube,rgb):
    n,lo,hi,v=cube;coords=[max(0,min(n-1,(x-a)/(b-a)*(n-1))) for x,a,b in zip(rgb,lo,hi)]
    ix=[min(n-2,int(x)) for x in coords];f=[x-i for x,i in zip(coords,ix)]
    order=sorted(range(3),key=lambda c:f[c],reverse=True);steps=[1,n,n*n]
    idx=ix[0]+n*(ix[1]+n*ix[2]);indices=[idx]
    for c in order:indices.append(indices[-1]+steps[c])
    w=[1-f[order[0]],f[order[0]]-f[order[1]],f[order[1]]-f[order[2]],f[order[2]]]
    return [sum(v[i][c]*a for i,a in zip(indices,w)) for c in range(3)]

def pack(cube,n):
    payload=bytearray()
    for b in range(n):
        for g in range(n):
            for r in range(n):
                rgb=sample(cube,[r/(n-1),g/(n-1),b/(n-1)])
                payload+=struct.pack('<3H',*(round(max(0,min(1,x))*65535) for x in rgb))
    h=2166136261
    for x in payload:h=((h^x)*16777619)&0xffffffff
    return b'G12LUT1\0'+struct.pack('<HHI',n,1,h)+payload

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('input');p.add_argument('output');p.add_argument('--size',type=int,choices=[17,33],default=17)
    a=p.parse_args();Path(a.output).write_bytes(pack(load(a.input),a.size))
