#!/usr/bin/env python3
"""Execute the final ARM64 ELF against modeled Linux syscalls; no real /sys."""
from pathlib import Path
import argparse
import hashlib
import json
import struct

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_INTR
from unicorn.arm64_const import (
    UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3,
    UC_ARM64_REG_X8, UC_ARM64_REG_SP)

OUT = Path(__file__).resolve().parent
ROOT = '/sys/kernel/debug/regmap/0-05/'
LEAVES = ['name', 'range', 'cache_only', 'cache_bypass', 'cache_dirty', 'registers']
VALID = b'258e: 00\n258f: ff\n'
MASK = (1 << 64) - 1


class Model:
    def __init__(self, data, case):
        self.case, self.events = case, []
        self.fds, self.opened, self.reads = {}, [], []
        self.output, self.stderr = bytearray(), bytearray()
        self.exit = None
        self.preads = 0
        self.write_results = list(case.get('stdout_results', []))
        header = struct.unpack_from('<16sHHIQQQIHHHHHH', data)
        assert header[1:4] == (2, 183, 1)
        self.entry = header[4]
        self.uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        pages = set()
        loads = []
        for i in range(header[10]):
            p = struct.unpack_from('<II6Q', data, header[5] + i * header[9])
            assert p[0] not in (2, 3), 'No DYNAMIC or INTERP permitted'
            if p[0] != 1:
                continue
            loads.append(p)
            pages.update(range(p[3] & ~4095, (p[3] + p[6] + 4095) & ~4095, 4096))
        for page in sorted(pages):
            self.uc.mem_map(page, 4096)
        for p in loads:
            self.uc.mem_write(p[3], data[p[2]:p[2] + p[5]])
        self.uc.mem_map(0x70000000, 0x100000)
        self.uc.reg_write(UC_ARM64_REG_SP, 0x700ff000)
        self.uc.hook_add(UC_HOOK_INTR, self.syscall)

    def string(self, at):
        value = bytearray()
        for _ in range(256):
            c = self.uc.mem_read(at, 1)[0]
            if not c:
                return value.decode()
            value.append(c)
            at += 1
        raise AssertionError('unbounded path')

    def syscall(self, uc, number, unused):
        assert number == 2, ('unexpected trap', number)
        nr = uc.reg_read(UC_ARM64_REG_X8)
        a = [uc.reg_read(r) for r in (UC_ARM64_REG_X0, UC_ARM64_REG_X1,
                                      UC_ARM64_REG_X2, UC_ARM64_REG_X3)]
        event = dict(syscall=nr)
        if nr == 56:
            path = self.string(a[1])
            assert a[0] == ((-100) & MASK) and a[2] == 0xa0000 and a[3] == 0
            assert path in [ROOT + n for n in LEAVES], path
            leaf = path[len(ROOT):]
            assert leaf == LEAVES[len(self.opened)], ('open order/retry', self.opened, leaf)
            self.opened.append(leaf)
            event.update(path=path, flags=a[2])
            forced = self.case.get('open_errors', {}).get(leaf)
            if forced is not None:
                result = forced
            elif leaf.startswith('cache_') and self.case.get('present') != leaf:
                result = -2
            else:
                result = 3  # Deliberately reuse the same descriptor after every close.
                assert result not in self.fds
                self.fds[result] = leaf
        elif nr == 57:
            assert a[0] in self.fds
            leaf = self.fds.pop(a[0])
            event.update(fd=a[0], leaf=leaf)
            result = -5 if self.case.get('close_error') == leaf else 0
        elif nr == 63:
            leaf = self.fds[a[0]]
            assert leaf in ('name', 'range') and a[2] == 64
            assert leaf not in self.reads, 'No metadata retry or EOF probe'
            self.reads.append(leaf)
            event.update(fd=a[0], leaf=leaf, count=a[2])
            payload = self.case.get('metadata', {}).get(leaf, b'mtk-spmi-pmic\n' if leaf == 'name' else b'0-ffff\n')
            result = self.case.get('read_results', {}).get(leaf, min(len(payload), 64))
            if result > 0:
                assert result <= len(payload) and result <= 64
                uc.mem_write(a[1], payload[:result])
        elif nr == 67:
            assert self.fds[a[0]] == 'registers'
            assert a[2:] == [18, 86526]
            self.preads += 1
            assert self.preads == 1, 'No pread retry or topping up a short read'
            event.update(fd=a[0], leaf='registers', count=a[2], offset=a[3])
            result = self.case.get('pread_result', 18)
            if result > 0:
                payload = self.case.get('payload', VALID)
                assert result <= 18 and len(payload) == 18
                uc.mem_write(a[1], payload[:result])
        elif nr == 64:
            assert a[0] in (1, 2) and 0 < a[2] < 256
            payload = bytes(uc.mem_read(a[1], a[2]))
            event.update(fd=a[0], count=a[2], payload=payload.decode())
            result = self.write_results.pop(0) if a[0] == 1 and self.write_results else a[2]
            assert result <= a[2]
            if result > 0:
                (self.output if a[0] == 1 else self.stderr).extend(payload[:result])
        elif nr == 93:
            self.exit = a[0]
            event.update(code=a[0])
            result = 0
            uc.emu_stop()
        else:
            raise AssertionError(('forbidden syscall', nr, a))
        event['return'] = result
        self.events.append(event)
        uc.reg_write(UC_ARM64_REG_X0, result & MASK)

    def run(self):
        self.uc.emu_start(self.entry, 0, count=100000)
        assert self.exit is not None, 'No SYS_exit or instruction budget exhausted'
        assert not self.fds, ('unclosed descriptor', self.fds)
        want_ok = self.case.get('ok', False)
        assert (self.exit == 0) == want_ok, ('exit', self.exit, want_ok)
        if self.case.get('metadata_failure'):
            assert 'registers' not in self.opened, 'Metadata failure reached registers'
        if want_ok:
            assert self.preads == 1 and bytes(self.output) == self.case.get('payload', VALID)
            assert not self.stderr
        if self.output:
            assert bytes(self.output) == self.case.get('payload', VALID)[:len(self.output)]
        assert len(self.output) <= 18


def cases():
    rows = [dict(name='valid 00/ff', ok=True),
            dict(name='valid ff/00', ok=True, payload=b'258e: ff\n258f: 00\n'),
            dict(name='valid 1a/2b', ok=True, payload=b'258e: 1a\n258f: 2b\n')]
    for leaf in ('name', 'range'):
        for error in (-2, -13, -4):
            rows.append(dict(name=f'{leaf} open {error}', open_errors={leaf: error}, metadata_failure=True))
        for result in (-5, -4, 0, 1):
            rows.append(dict(name=f'{leaf} read {result}', read_results={leaf: result}, metadata_failure=True))
        rows.append(dict(name=f'{leaf} close EIO', close_error=leaf, metadata_failure=True))
        content = b'mtk-spmi-pmic\n' if leaf == 'name' else b'0-ffff\n'
        for suffix, value in [('wrong', b'wrong\n'), ('no newline', content[:-1]), ('extra', content+b'x')]:
            rows.append(dict(name=f'{leaf} {suffix}', metadata_failure=True, metadata={leaf: value}))
    for leaf in ('cache_only', 'cache_bypass', 'cache_dirty'):
        rows.append(dict(name=f'{leaf} present', present=leaf, metadata_failure=True))
        for error in (-13, -20, -4):
            rows.append(dict(name=f'{leaf} open {error}', open_errors={leaf: error}, metadata_failure=True))
    for error in (-2, -13, -4):
        rows.append(dict(name=f'registers open {error}', open_errors={'registers': error}))
    rows.append(dict(name='registers close EIO', close_error='registers'))
    for result in (-5, -4, 0, 1, 17):
        rows.append(dict(name=f'pread {result}', pread_result=result))
    for name, payload in [('left XX', b'258e: XX\n258f: ff\n'),
                          ('right XX', b'258e: 00\n258f: XX\n'),
                          ('both XX', b'258e: XX\n258f: XX\n'),
                          ('left address', b'258d: 00\n258f: ff\n'),
                          ('right address', b'258e: 00\n2590: ff\n'),
                          ('separator', b'258e= 00\n258f: ff\n'),
                          ('line ending', b'258e: 00\n258f: ffX'),
                          ('upper hex', b'258e: 0A\n258f: ff\n')]:
        rows.append(dict(name=name, payload=payload))
    for name, results, ok in [('stdout EIO', [-5], False), ('stdout EINTR', [-4], False),
                               ('stdout zero', [0], False), ('stdout short then zero', [17, 0], False),
                               ('stdout partial then error', [5, -5], False),
                               ('stdout split complete', [3, 15], True)]:
        rows.append(dict(name=name, stdout_results=results, ok=ok))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reader', type=Path, default=OUT / 'regmap-reader.aarch64')
    parser.add_argument('--output', type=Path, default=OUT / 'elf-syscall-model.json')
    args = parser.parse_args()
    args.output.unlink(missing_ok=True)
    data, records = args.reader.read_bytes(), []
    for case in cases():
        model = Model(data, case)
        try:
            model.run()
            error = None
        except Exception as failure:
            error = str(failure)
        records.append(dict(name=case['name'], status='FAIL' if error else 'PASS', error=error,
                            exit=model.exit, stdout=model.output.decode(), stderr=model.stderr.decode(),
                            syscalls=model.events))
    failures = [r for r in records if r['status'] != 'PASS']
    report = dict(status='FAIL' if failures else 'PASS', reader=str(args.reader),
                  reader_sha256=hashlib.sha256(data).hexdigest(),
                  model_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  cases=len(records), failed=len(failures), records=records,
                  limitations=['Actual ARM64 ELF executes with modeled Linux syscalls',
                               'One descriptor is deliberately reused for distinct file types',
                               'No host /sys or device access; no calibration or live identity established'])
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(report['status'], len(records), 'ARM64 ELF syscall cases;', len(failures), 'failed')
    for row in failures:
        print(row['name'], row['error'])
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
