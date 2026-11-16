#!/usr/bin/env python3
"""Execute stock-kernel debugfs instructions against explicit external models.

No device access. Models: allocation, metadata predicates, locks, formatting,
user copy, and _regmap_read. This does not execute a PMIF/SPMI controller or
establish the live map's identity/configuration.
"""
from pathlib import Path
import hashlib
import json
import struct

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import (
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3,
    UC_ARM64_REG_X4, UC_ARM64_REG_X5, UC_ARM64_REG_X18, UC_ARM64_REG_X30,
    UC_ARM64_REG_PC, UC_ARM64_REG_SP, UC_ARM64_REG_SP_EL0, UC_ARM64_REG_PSTATE)

OUT = Path(__file__).resolve().parent
(OUT / 'read-window-emulation.json').unlink(missing_ok=True)
IMAGE_SHA = '12d255cdd09fb5b9fb5cf9608e6b060a5b2dabc109fd4344b071a3dbdc0cf391'
data = (OUT / 'stock-Image').read_bytes()
assert hashlib.sha256(data).hexdigest() == IMAGE_SHA
symbols = {}
ambiguous = set()
symbol_bytes = (OUT / 'stock-kallsyms.tsv').read_bytes()
assert hashlib.sha256(symbol_bytes).hexdigest() == '4b49954e0c7be2d71166e7c8652f2be6ef48f68af2f9e5b0fc011e580eadebda'
for line in symbol_bytes.decode().splitlines():
    address, kind, name = line.split('\t')
    if name in symbols and symbols[name] != int(address, 16):
        ambiguous.add(name)
    symbols[name] = int(address, 16)
for name in ambiguous:
    del symbols[name]  # Never choose silently between duplicate names at distinct addresses.

MASK = (1 << 64) - 1
BASE = symbols['_text']
MAP, POS, USER, TASK, LOCK = [0x20000000 + n * 0x1000 for n in range(1, 6)]
HEAP, STACK, SHADOW, STOP = 0x21000000, 0x22000000, 0x22100000, 0x22200000
FILE = 0x20006000
ARGS = [UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2,
        UC_ARM64_REG_X3, UC_ARM64_REG_X4, UC_ARM64_REG_X5]


class WindowModel:
    def __init__(self):
        self.uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        self.uc.mem_map(BASE, 0x4000000)
        self.uc.mem_write(BASE, data)
        self.uc.mem_map(0x20000000, 0x10000)
        self.uc.mem_map(HEAP, 0x100000)
        self.uc.mem_map(STACK, 0x100000)
        self.uc.mem_map(SHADOW, 0x10000)
        self.uc.mem_map(STOP, 0x1000)
        self.uc.reg_write(UC_ARM64_REG_PSTATE, 0x3c5)
        self.uc.reg_write(UC_ARM64_REG_SP_EL0, TASK)
        self.heap = HEAP
        self.reads, self.allocations, self.freed = [], {}, []
        self.locked = set()
        self.failed_registers = set()
        self.copy_fault = False
        self.values = {0x258e: 0xa5, 0x258f: 0x5a}  # Synthetic, test-only.
        self.metadata_calls = 0
        self.alloc_fail = False
        self.p32(MAP + 120, 1)    # format.val_bytes
        self.p32(MAP + 360, 65535)  # max_register
        self.p32(MAP + 516, 1)    # reg_stride
        self.p64(MAP + 48, symbols['regmap_lock_mutex.cfi_jt'])
        self.p64(MAP + 56, symbols['regmap_unlock_mutex.cfi_jt'])
        self.p64(MAP + 64, LOCK)
        self.p64(MAP + 296, MAP + 296)  # debugfs_off_cache list head
        self.p64(MAP + 304, MAP + 296)
        self.p64(TASK + 8, 0x7fffffffff)  # Model user address limit.
        self.p64(FILE + 216, MAP)  # This kernel's file.private_data.
        names = ['__kmalloc', 'kmem_cache_alloc_trace', 'kfree', 'snprintf',
                 'memset', 'memcpy', 'mutex_lock', 'mutex_unlock', '__list_add_valid',
                 '__list_del_entry_valid', '__check_object_size', '__arch_copy_to_user',
                 'regmap_lock_mutex.cfi_jt', 'regmap_unlock_mutex.cfi_jt',
                 'regmap_precious', 'regmap_readable', 'regmap_cached', '_regmap_read',
                 '__cfi_slowpath', '__stack_chk_fail']
        for name in names:
            self.uc.hook_add(UC_HOOK_CODE, self.hook, name,
                             begin=symbols[name], end=symbols[name])

    def p32(self, address, value):
        self.uc.mem_write(address, struct.pack('<I', value))

    def p64(self, address, value):
        self.uc.mem_write(address, struct.pack('<Q', value))

    def u64(self, address):
        return struct.unpack('<Q', self.uc.mem_read(address, 8))[0]

    def ret(self, value=0):
        self.uc.reg_write(UC_ARM64_REG_X0, value & MASK)
        self.uc.reg_write(UC_ARM64_REG_PC, self.uc.reg_read(UC_ARM64_REG_X30))

    def hook(self, uc, address, size, name):
        a = [uc.reg_read(r) for r in ARGS]
        if name in ('__kmalloc', 'kmem_cache_alloc_trace'):
            count = a[0] if name == '__kmalloc' else a[2]
            assert 0 < count <= 0x400000, count
            if self.alloc_fail:
                self.ret(0)
                return
            start = self.heap
            self.heap += (count + 15) & ~15
            assert self.heap < HEAP + 0x100000
            self.allocations[start] = count
            uc.mem_write(start, bytes(count))
            self.ret(start)
        elif name == 'kfree':
            assert a[0] in self.allocations, hex(a[0])
            self.freed.append(a[0])
            self.ret()
        elif name == 'snprintf':
            fmt = bytes(uc.mem_read(a[2], 64)).split(b'\0')[0]
            if fmt == b'%x':
                rendered = f'{a[3]:x}'.encode()
            elif fmt in (b'%.*x: ', b'%.*x'):
                rendered = f'{a[4]:0{a[3]}x}'.encode()
                if fmt.endswith(b': '):
                    rendered += b': '
            elif fmt == b'%x-%x\n':
                rendered = f'{a[3]:x}-{a[4]:x}\n'.encode()
            else:
                raise AssertionError(('unmodeled format', fmt))
            if a[1]:
                payload = rendered[:a[1] - 1] + b'\0'
                uc.mem_write(a[0], payload)
            self.ret(len(rendered))
        elif name == 'memset':
            uc.mem_write(a[0], bytes([a[1] & 255]) * a[2])
            self.ret(a[0])
        elif name == 'memcpy':
            uc.mem_write(a[0], bytes(uc.mem_read(a[1], a[2])))
            self.ret(a[0])
        elif name in ('mutex_lock', 'regmap_lock_mutex.cfi_jt'):
            assert a[0] not in self.locked
            self.locked.add(a[0])
            self.ret()
        elif name in ('mutex_unlock', 'regmap_unlock_mutex.cfi_jt'):
            assert a[0] in self.locked
            self.locked.remove(a[0])
            self.ret()
        elif name == '__list_add_valid':
            assert self.u64(a[1]) == a[2] and self.u64(a[2] + 8) == a[1]
            self.ret(1)
        elif name == '__list_del_entry_valid':
            self.ret(1)
        elif name in ('regmap_precious', 'regmap_readable', 'regmap_cached'):
            assert a[0] == MAP
            self.metadata_calls += 1
            self.ret(name == 'regmap_readable' and a[1] <= 65535)
        elif name == '_regmap_read':
            assert a[0] == MAP and LOCK in self.locked
            self.reads.append(a[1])
            if a[1] in self.failed_registers:
                self.ret(-5)
            else:
                self.p32(a[2], self.values.get(a[1], a[1] & 255))
                self.ret()
        elif name == '__check_object_size':
            assert a[0] in self.allocations and a[1] <= self.allocations[a[0]]
            self.ret()
        elif name == '__arch_copy_to_user':
            assert a[0] == USER and a[2] <= 64
            if not self.copy_fault:
                uc.mem_write(a[0], bytes(uc.mem_read(a[1], a[2])))
            self.ret(a[2] if self.copy_fault else 0)
        else:
            raise AssertionError(('unmodeled kernel boundary', name, hex(address)))

    def call(self, entry, offset, arguments):
        self.p64(POS, offset & MASK)
        self.uc.mem_write(USER, bytes(64))
        self.uc.reg_write(UC_ARM64_REG_SP, STACK + 0xff000)
        self.uc.reg_write(UC_ARM64_REG_X18, SHADOW + 0x1000)
        self.uc.reg_write(UC_ARM64_REG_X30, STOP)
        for register, value in zip(ARGS, arguments):
            self.uc.reg_write(register, value)
        self.reads = []
        self.uc.emu_start(symbols[entry], STOP, count=6000000)
        assert self.uc.reg_read(UC_ARM64_REG_PC) == STOP, 'instruction budget exhausted'
        assert not self.locked
        result = self.uc.reg_read(UC_ARM64_REG_X0)
        result = result - (1 << 64) if result >> 63 else result
        payload = bytes(self.uc.mem_read(USER, max(0, result)))
        return result, payload, self.u64(POS), list(self.reads)

    def read(self, offset, count):
        return self.call('regmap_read_debugfs', offset, (MAP, 0, 65535, USER, count, POS))

    def ranges(self):
        return self.call('regmap_reg_ranges_read_file', 0, (FILE, USER, 64, POS, 0, 0))


def main():
    model = WindowModel()
    cases = []

    def check(name, offset, count, expected, expected_reads, next_offset=None):
        result, payload, advanced, reads = model.read(offset, count)
        assert reads == expected_reads, (name, reads)
        if isinstance(expected, int):
            assert result == expected and not payload, (name, result, payload)
        else:
            assert payload == expected and result == len(expected), (name, result, payload)
        if next_offset is not None:
            assert advanced == next_offset, (name, advanced)
        cases.append(dict(name=name,offset=offset,count=count,returned=result,
                          payload=payload.decode(),next_offset=advanced,read_registers=reads))
        print('PASS', name, flush=True)

    offset = 0x258e * 9
    check('cold exact two-byte window', offset, 18, b'258e: a5\n258f: 5a\n', [0x258e, 0x258f], offset + 18)
    check('warm exact window', offset, 18, b'258e: a5\n258f: 5a\n', [0x258e, 0x258f], offset + 18)
    check('one complete line', offset, 9, b'258e: a5\n', [0x258e], offset + 9)
    check('less than one line', offset, 8, b'', [], offset)
    check('not enough for two lines', offset, 17, b'258e: a5\n', [0x258e], offset + 9)
    check('zero count', offset, 0, -22, [], offset)
    check('negative offset', -1, 18, -22, [], MASK)
    model.failed_registers = {0x258e}
    check('left read error', offset, 18, b'258e: XX\n258f: 5a\n', [0x258e, 0x258f], offset + 18)
    model.failed_registers = {0x258f}
    check('right read error', offset, 18, b'258e: a5\n258f: XX\n', [0x258e, 0x258f], offset + 18)
    model.failed_registers = {0x258e, 0x258f}
    check('both read errors', offset, 18, b'258e: XX\n258f: XX\n', [0x258e, 0x258f], offset + 18)
    model.failed_registers.clear()
    model.copy_fault = True
    check('user copy fault retains offset', offset, 18, -14, [0x258e, 0x258f], offset)
    model.copy_fault = False
    model.alloc_fail = True
    check('allocation failure', offset, 18, -12, [], offset)
    model.alloc_fail = False
    model.values = {0x258e: 0, 0x258f: 255}
    check('zero and ff are valid bytes', offset, 18, b'258e: 00\n258f: ff\n', [0x258e, 0x258f], offset + 18)
    check('misaligned seek skips partial line', offset + 1, 18, b'258f: ff\n2590: 90\n', [0x258f, 0x2590], offset + 19)
    meta = WindowModel()
    n, payload, advanced, reads = meta.ranges()
    assert (n, payload, advanced, reads) == (7, b'0-ffff\n', 7, [])
    cases.append(dict(name='cold range metadata does not read registers', returned=n,
                      payload=payload.decode(), next_offset=advanced, read_registers=reads))
    print('PASS', cases[-1]['name'], flush=True)
    report = dict(status='PASS', image_sha256=IMAGE_SHA,
                  symbols_sha256=hashlib.sha256(symbol_bytes).hexdigest(),
                  runner_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  function='regmap_read_debugfs',address=hex(symbols['regmap_read_debugfs']),
                  cases=cases,metadata_predicate_calls=model.metadata_calls,
                  assumptions=['map: max_register=ffff, stride=1, val_bytes=1, full printable range',
                               'Metadata predicates, allocator, locks, formatting, copy and _regmap_read modeled',
                               'Actual backup kernel instructions execute; no controller or device execution',
                               'Results do not establish the live map configuration or calibration quality'])
    (OUT / 'read-window-emulation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS', len(cases), 'stock-kernel instruction cases', flush=True)


if __name__ == '__main__':
    main()
