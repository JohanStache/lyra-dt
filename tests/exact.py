#!/usr/bin/env python3
"""Checks a LYRA host build against 0.4's sample for sample: tests/exact.py <host> [<reference host>]: the reference
(default ./lyra_host_04) is host_render.c built with 0.4's lyra.c."""
import subprocess, numpy as np, sys
REF = sys.argv[2] if len(sys.argv) > 2 else './lyra_host_04'
rng=np.random.default_rng(7); worst=0; n=0; diffs=[]
cases=[[]]
for _ in range(120):
    kw=['%d=%d'%(k,int(rng.integers(0,128))) for k in range(1,15)]
    if rng.random()<0.3: kw.append('97=%d'%int(rng.integers(58,72)))
    if rng.random()<0.2: kw.append('96=1')
    cases.append(kw)
for kw in cases:
    note=str(int(rng.integers(20,80)))
    a=subprocess.run([REF,note,'600']+kw,capture_output=True).stdout
    b=subprocess.run([sys.argv[1],note,'600']+kw,capture_output=True).stdout
    n+=1
    if a!=b:
        x=np.frombuffer(a,dtype=np.int32); y=np.frombuffer(b,dtype=np.int32)
        k=np.argmax(x!=y); diffs.append((kw,k))
print("identical in %d of %d settings" % (n-len(diffs), n))
for d in diffs[:5]: print(" first difference at sample", d[1], d[0])
