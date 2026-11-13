import struct, json
d=open('mali_csffw.bin','rb').read()
info=json.load(open('fw-sections.json')); secs=info['secs']
def find(word):
    pat=struct.pack('<I',word); out=[]; i=d.find(pat)
    while i>=0:
        out.append(i); i=d.find(pat,i+1)
    return out
for name,w in [('0xff5aa5f0 version sentinel',0xff5aa5f0),('0x0302bc78',0x302bc78),('0x40022000',0x40022000),('0x40003034',0x40003034),('0x0304 size',0x3040000)]:
    o=find(w)
    print('%-28s %d occurrences: %s' % (name, len(o), ' '.join('%#x'%x for x in o[:12])))
