"""Compare the shipped audit function to an independent interval sweep and GUID histogram."""
import argparse
import collections
import ctypes
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reference-window', required=True, type=Path)
parser.add_argument('--libmini', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent
ref = args.reference_window.read_bytes()
assert hashlib.sha256(ref).hexdigest() == '03628d70602e4f21e36ace76f5c4e27ed1b10e50ae690704035799ea34e4e82b'
def expected(array, count, stride, first, last):
    ids, spans, events = [], [], []
    for off in range(0,count*stride,stride):
        if not any(array[off:off+16]): continue
        ids.append(array[off+16:off+32])
        lo,hi = struct.unpack_from('<QQ',array,off+32)
        spans.append((lo,hi))
        if lo <= hi: events.extend(((lo,0),(hi,1)))
    active = intersections = 0
    for _,kind in sorted(events):
        if kind == 0: intersections += active; active += 1
        else: active -= 1
    assert active == 0
    duplicate_pairs = sum(n*(n-1)//2 for key,n in collections.Counter(ids).items() if any(key))
    issues=[sum(a>b for a,b in spans),sum(a<first for a,b in spans),sum(b>last for a,b in spans),
            intersections,ids.count(bytes(16)),duplicate_pairs]
    return [len(spans),count-len(spans),*issues,int(not any(issues))]

with tempfile.TemporaryDirectory() as tmp:
    t=Path(tmp)
    (t/'audit.c').write_text("""
#define main trial_device_main
#define printf trial_printf
#define errno trial_errno
#include "ufs_range_check.c"
int trial_errno;
static unsigned *output;
int trial_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (memcmp(fmt, "ufs-range-user: entries ", 24) || va_arg(args,int) != 42) __builtin_trap();
    for (unsigned i=0; i<9; ++i) output[i]=va_arg(args,unsigned);
    va_end(args);
    return 0;
}
void run(const uint8_t *p,uint64_t first,uint64_t last,uint32_t count,uint32_t stride,unsigned *out) {
    struct gpt_layout layout = {first,last,count,stride,0};
    output=out;
    audit_entries(p,&layout,42);
}
""")
    subprocess.run(['clang','-Wno-ignored-attributes','-shared','-fPIC','-fno-builtin','-ffreestanding','-O2','-I',str(root),
                    '-I',str(args.libmini.resolve()),str(t/'audit.c'),'-o',str(t/'audit.so')],check=True)
    run=ctypes.CDLL(str(t/'audit.so')).run
    run.argtypes=[ctypes.c_char_p,ctypes.c_uint64,ctypes.c_uint64,ctypes.c_uint32,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32)]
    run.restype=None
    def check(data,count,stride,lo,hi):
        oracle=expected(data,count,stride,lo,hi)
        out=(ctypes.c_uint32*9)()
        run(data,lo,hi,count,stride,out)
        assert list(out)==oracle,(list(out),oracle)
        return oracle
    h=ref[4096:8192]
    start,count,stride=struct.unpack_from('<QII',h,72)
    first,last=struct.unpack_from('<QQ',h,40)
    observed=check(ref[start*4096:start*4096+count*stride],count,stride,first,last)
    assert observed==[88,40,0,1,0,0,0,0,0]
    rng=random.Random(732)
    for case in range(240):
        stride=rng.choice([128,256,512,1024,2048]);count=16384//stride
        b=bytearray(16384)
        choices=[0,1,33,34,35,100,2**33-2,2**33,2**33+1,2**64-1]
        for off in range(0,len(b),stride):
            b[off]=rng.randrange(3)
            b[off+16]=rng.randrange(11)
            struct.pack_into('<QQ',b,off+32,rng.choice(choices),rng.choice(choices))
        check(bytes(b),count,stride,34,2**33)
print('Independent sweep/histogram matched real reference and 240 seeded cases:',observed)
