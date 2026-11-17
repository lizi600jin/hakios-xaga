#!/usr/bin/env python3
"""Strict package verification for revision2/trial721. Host only.

Paths point at the NEW outputs. The H1 script had hardcoded old paths and is not reused.
Beyond the original 46 checks this adds: the loader binary carries the varread return-gate
prefix exactly once and the capacity prefix zero times.
"""
import gzip, hashlib, json, struct, sys
from pathlib import Path

R21 = Path('/home/lizi/IOS/hakios-mtk/logs/ufs-variable-read-20261117')
R2  = R21 / 'revision2'
O   = R2 / 'trial721'
KC  = R21 / 'kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837'
LBS = R2 / 'build/r2/loader.bin'
KV  = Path('/home/lizi/IOS/hakios-mtk/logs/profile4-device-20261116/trial710/revision2/ref-kernel.ver.bin')
ARGS= R2 / 'arguments721-r2.json'

fails = []
def req(ok, what):
    if not ok: fails.append(what)
    print(('  PASS  ' if ok else '  FAIL  ') + what)
def sha(b): return hashlib.sha256(b).hexdigest()

boot = (O/'boot.img').read_bytes(); payload = (O/'payload.bin').read_bytes()
loader = (O/'loader.bin').read_bytes(); kernel = KC.read_bytes()
params = json.loads(ARGS.read_text())

print('== frozen kernel identity ==')
req(len(kernel) == 15339856, 'kernel size 15339856')
req(sha(kernel) == '784500d791bf2a623d24ad81ed460b4c0cdf25143f8beade8a2d4729eed342cb', 'kernel sha256')

print('== XNUP payload ==')
magic, size = struct.unpack_from('<II', payload)
req(magic == 0x50554e58, 'payload magic')
req(size == len(payload) - 8, 'payload size field')
off, entries, terminated = 8, {}, False
while off + 24 <= len(payload):
    hdr = payload[off:off+24]
    name, length, reserved = struct.unpack('<16sII', hdr)
    off += 24
    if name == bytes(16):
        req(hdr == bytes(24), 'nonzero terminator'); terminated = True; break
    name = name.split(b'\0',1)[0].decode('ascii')
    req(name not in entries and reserved == 0, f'entry {name} unique/reserved0')
    end = off + length; padded = off + ((length+15)&~15)
    req(padded <= len(payload), f'entry {name} inside payload')
    req(payload[end:padded] == bytes(padded-end), f'entry {name} padding')
    entries[name] = payload[off:end]; off = padded
req(terminated, 'XNUP terminator present')
req(off == len(payload), 'no trailing payload bytes')
req(list(entries) == ['kernel','ramdisk','kernel.ver'], 'entries and order')
req(entries.get('kernel') == kernel, 'nested kernel == build output')
req(entries.get('kernel.ver') == KV.read_bytes(), 'kernel.ver == reference')

print('== boot.img layers ==')
CHAIN, IMAGE, page = 0x40000, 42201364, 4096
req(boot[:8] == b'ANDROID!', 'Android boot magic')
ks = struct.unpack_from('<I', boot, 8)[0]
kern = boot[page:page+ks]
req(kern[:3] == b'\x1f\x8b\x08', 'kernel segment is gzip')
blob = gzip.decompress(kern)
plo = CHAIN + ((IMAGE+15) & ~15)
req(blob[plo:] == payload, 'nested payload == payload.bin')
req(len(blob) == plo + len(payload), 'blob length')
req(blob[len(loader):CHAIN] == bytes(CHAIN-len(loader)), 'loader slot zero-padded')
req(blob[CHAIN:CHAIN+2] == b'MZ', 'Android Image at CHAIN_OFFSET')
seg = bytearray(blob[:len(loader)]); expect = bytearray(loader)
struct.pack_into('<Q', expect, 0x10, (plo+len(payload)+0xfffff) & ~0xfffff)
struct.pack_into('<Q', expect, 0x28, IMAGE)
struct.pack_into('<Q', expect, 0x30, plo)
req(bytes(seg) == bytes(expect), 'loader equals three-field patched original')
raw_diff = [i for i in range(len(loader)) if loader[i] != bytes(seg)[i]]
req(raw_diff == [18,19,40,41,42,43,48,49,50,51],
    f'raw loader differs only in ten bytes ({len(raw_diff)})')

print('== loader return-gate prefix (new, added in r2) ==')
req(loader.count(b'mt6895-ufs-varread: return-gate') == 1, 'varread return-gate prefix exactly once')
req(loader.count(b'mt6895-ufs-capacity: return-gate') == 0, 'capacity return-gate prefix zero times')

print('== loader arguments ==')
argstr = params['base_arguments'].encode()
req(loader.count(argstr + b'\0') == 1, 'base argument body unique')
for must in (b'mtk_ufs_varread=1', b'mtk_ufs_capacity=0', b'mtk_ufs_probe=0',
             b'mtk_ufs_read=0', b'mtk_ufs_service=0', b'mali=0', b'headset=0',
             b'cpus=4', b'loader_return_after=150', b'mali_trial=721'):
    req(must in loader, f'loader carries {must.decode()}')
req(b'fresh trial721 lease' in loader, 'loader return guard says trial721')

print('== runtime argument string ==')
full = params['full_arguments'].encode()
req(len(full) == params['full_bytes'], 'full_bytes')
req(len(full)+1 == params['nul_inclusive_bytes'], 'nul-inclusive length')
req(len(full)+1 <= 608, 'within BOOT_LINE_LENGTH 608')
keys = [a.split(b'=',1)[0] for a in full.split() if b'=' in a]
req(len(keys) == len(set(keys)), 'no duplicate keys')
req(params['device_execution_authorized'] is False, 'host-only hold')

print('== identities ==')
for n, b in (('boot.img',boot),('loader.bin',loader),('payload.bin',payload)):
    print(f'  {n:<12} {len(b):>10} B  {sha(b)[:16]}')
    req(len(b) > 0, f'{n} non-empty')
req(loader == LBS.read_bytes(), 'packaged loader == build/r2/loader.bin')

print()
if fails:
    print(f'FAILED {len(fails)}: ' + '; '.join(fails)); sys.exit(1)
print('PASS all strict package checks (r2)')
