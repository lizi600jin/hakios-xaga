import struct, json
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json')); secs=info['secs']
def va2off(va):
    for s in secs:
        vs,ve=s['va']; ds,de=s['data']
        if vs<=va<ve and de>ds and (va-vs)<(de-ds): return ds+(va-vs)
    return None
def r32(va):
    o=va2off(va); return None if o is None else struct.unpack_from('<I',d,o)[0]
print('boot-mode word @0x202c7a0 =', hex(r32(0x202c7a0)) if r32(0x202c7a0) is not None else 'UNMAPPED')
o=va2off(0x1002b64)
print('tbb table bytes @0x1002b64:', d[o:o+8].hex(), '-> halfwords', [struct.unpack_from('<H',d,o+2*i)[0] for i in range(4)])
print('bytes at 0x202c7a0 area:', d[va2off(0x202c7a0)-8:va2off(0x202c7a0)+16].hex() if va2off(0x202c7a0) else '-')
# what is at 0x202c944 / 0x202c210
for a in (0x202c210,0x202c7a0,0x202c944):
    o2=va2off(a); print('0x%x -> fileoff %s' % (a, hex(o2) if o2 is not None else '-'))
