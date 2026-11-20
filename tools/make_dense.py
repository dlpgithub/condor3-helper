#!/usr/bin/env python3
"""Stress-test landscape: copy a landscape and multiply its autogen density.

    make_dense.py <Landscapes/Src> <Landscapes/Dst> <copies>

Every Autogen/oXXYY.c3d gets <copies> copies of all its objects, each shifted a few metres (copy k: +k*9 m west, +k*6 m north),
so the per-tile data is <copies> times larger. Everything else is cloned (APFS copy-on-write), files are renamed to the new name
and the .oha MD5 values are recomputed, so the unmodified game accepts the landscape. Visually meaningless; it exists to measure memory.
"""
import hashlib, os, re, shutil, struct, subprocess, sys
import numpy as np

src, dst, k = sys.argv[1].rstrip('/'), sys.argv[2].rstrip('/'), int(sys.argv[3])
sname, dname = os.path.basename(src), os.path.basename(dst)
if os.path.exists(dst):
    sys.exit(f'{dst} exists')
subprocess.check_call(['cp', '-c', '-R', src, dst])
for f in os.listdir(dst):
    if f.startswith(sname + '.'):
        os.rename(os.path.join(dst, f), os.path.join(dst, dname + f[len(sname):]))

def pstr(d, o):
    n = d[o]; return d[o + 1:o + 1 + n], o + 1 + n

oha = os.path.join(dst, dname + '.oha')
lines = open(oha, 'rb').read().decode('latin1').splitlines()
out = []
for ln in lines:
    m = re.match(r'(\d+)\s+(\S+)', ln)
    if not m:
        out.append(ln); continue
    key = int(m.group(1)); tx, ty = key // 1000, key % 1000
    p = None
    for nm in (f'o{tx:03d}{ty:03d}.c3d', f'o{tx:02d}{ty:02d}.c3d'):
        if os.path.exists(os.path.join(dst, 'Autogen', nm)):
            p = os.path.join(dst, 'Autogen', nm); break
    d = open(p, 'rb').read()
    o = 3
    ng, = struct.unpack_from('<I', d, o); o += 4 + 8 * ng
    nobj, = struct.unpack_from('<I', d, o); o += 4
    objs = []
    for _ in range(nobj):
        name, o = pstr(d, o)
        fv, nv, fi, ni = struct.unpack_from('<IIII', d, o); o += 16
        tex, o = pstr(d, o)
        rest = d[o:o + 28]; o += 28
        objs.append((name, fv, nv, fi, ni, tex, rest))
    nverts, = struct.unpack_from('<I', d, o); o += 4
    v = np.frombuffer(d, dtype='<f4', count=nverts * 8, offset=o).reshape(nverts, 8); o += nverts * 32
    nidx, = struct.unpack_from('<I', d, o); o += 4
    idx = np.frombuffer(d, dtype='<u4', count=nidx, offset=o)
    b = bytearray(b'C3D') + struct.pack('<III', 1, 0, nobj * k) + struct.pack('<I', nobj * k)
    for c in range(k):
        for (name, fv, nv, fi, ni, tex, rest) in objs:
            nm = (name + b'_%d' % c)[:255]
            b += bytes([len(nm)]) + nm + struct.pack('<IIII', fv + c * nverts, nv, fi + c * nidx, ni) + bytes([len(tex)]) + tex + rest
    vv = np.concatenate([v + np.array([c * 9.0, c * 6.0, 0, 0, 0, 0, 0, 0], dtype='<f4') for c in range(k)]).astype('<f4')
    ii = np.concatenate([idx + np.uint32(c * nverts) for c in range(k)]).astype('<u4')
    b += struct.pack('<I', nverts * k) + vv.tobytes() + struct.pack('<I', nidx * k) + ii.tobytes()
    os.remove(p)                      # break the clone before writing
    open(p, 'wb').write(b)
    out.append(f'{key:06d} {hashlib.md5(b).hexdigest()}')
    print(os.path.basename(p), len(d) >> 20, '->', len(b) >> 20, 'MB')
open(oha, 'wb').write(('\r\n'.join(out) + '\r\n').encode('latin1'))
print('done')
