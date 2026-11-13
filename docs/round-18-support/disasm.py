import struct, json
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json'))
secs=info['secs']
# build VA->file offset map for sections with data
def va2off(va):
    for s in secs:
        vs,ve=s['va']; ds,de=s['data']
        if vs<=va<ve and de>ds:
            return ds+(va-vs)
    return None
print('reset vector PC 0x10018ec -> file off', hex(va2off(0x10018ec)))
print('sec1 file off', hex(va2off(0x1000000)), 'len', 63560)
# dump first bytes at entry
off=va2off(0x10018ec)
print('entry bytes', d[off:off+32].hex())
