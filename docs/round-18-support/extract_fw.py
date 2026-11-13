import re, hashlib, struct
src='/home/lizi/IOS/hakios-mtk/drivers/gpu/hakios.mali610_fw.h'
txt=open(src).read()
body=txt.split('kMaliFirmware[MALI_FW_LENGTH] = {',1)[1].split('};',1)[0]
data=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})', body))
n=258048
assert len(data)==n, (len(data), n)
open('mali_csffw.bin','wb').write(data)
print('bytes', len(data), 'sha256', hashlib.sha256(data).hexdigest())
print('magic %08x hdr %d.%d table_end %d' % (struct.unpack_from('<I',data,0)[0], data[5], data[4], struct.unpack_from('<I',data,16)[0]))
