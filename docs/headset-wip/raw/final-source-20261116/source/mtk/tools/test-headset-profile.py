#!/usr/bin/env python3
"""Run headset=4 against a PMIF fault model and a frozen vendor instruction oracle.

This compiles the actual driver and shared PMIF core with ASan/UBSan. It performs
no device access. Oracle trim 3/7 and fine 1/2 are synthetic test inputs only.
"""

from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / 'fixtures'


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def model_parameters(fixture):
    defined = {int(k, 16): v for k, v in fixture['defined_masks'].items()}
    initial = {int(k, 16): v for k, v in fixture['initial_mt6368'].items()}
    lines = ['static uint8_t model_defined(uint32_t sid, uint16_t reg) {',
             '    if (sid == 9) return 255;', '    switch (reg) {']
    lines += [f'    case 0x{a:04x}: return 0x{m:02x};' for a, m in defined.items()]
    lines += ['    default: return 255;', '    }', '}',
              'static bool model_profile_reg(uint32_t sid, uint16_t reg) {',
              '    if (sid == 9) return reg == 0x079e;', '    switch (reg) {']
    lines += [f'    case 0x{a:04x}:' for a in defined]
    lines += ['        return true;', '    default: return false;', '    }', '}',
              'static void model_seed(void) {']
    lines += [f'    model_regs[0][0x{a:04x}] = 0x{v:02x};' for a, v in initial.items()]
    lines += [f'    model_regs[1][0x{int(a, 16):04x}] = 0x{v:02x};'
              for a, v in fixture['initial_mt6685'].items()]
    return '\n'.join(lines + ['}', ''])


def vendor_events(fixture):
    """Normalize frozen vendor calls, without consulting the driver's step arrays."""
    defined = {int(k, 16): v for k, v in fixture['defined_masks'].items()}
    regs = {(5, int(k, 16)): v for k, v in fixture['initial_mt6368'].items()}
    regs.update({(9, int(k, 16)): v for k, v in fixture['initial_mt6685'].items()})
    expected = {'up': [], 'down': []}

    def update(phase, sid, reg, mask, value, explicit=False):
        valid = 255 if sid == 9 else defined[reg]
        old = regs.get((sid, reg), 0) & valid
        mask &= valid
        new = ((old & ~mask) | (value & mask)) & valid
        if explicit or new != old:
            expected[phase].append(('W', sid, reg, new))
        regs[sid, reg] = new

    # DAC source control is a separate precondition, outside the callback trace.
    update('up', 5, 0x238e, 1, 1)
    for call in fixture['calls']:
        phase, op = call['phase'], call['op']
        if op == 'update':
            update(phase, 5, call['reg'], call['mask'], call['value'])
        elif op == 'write':
            update(phase, 5, call['reg'], 255, call['value'], explicit=True)
        elif op == 'delay':
            expected[phase].append(('D', call['min_us']))
        elif op == 'external_dcxo':
            update(phase, 9, 0x079e, 4, 4 if call['enable'] else 0)
            if call['enable']:
                expected[phase].append(('D', 400))
        elif op != 'external_regulator':
            raise ValueError(call)
    return expected


def driver_events(stdout):
    actual = {'up': [], 'down': []}
    for line in stdout.splitlines():
        if not line.startswith('E '):
            continue
        words = line.split()
        phase = int(words[1])
        if phase == 4:
            continue  # The C model verifies snapshot restoration separately.
        assert phase in (1, 2, 3), line
        dest = actual['up' if phase == 1 else 'down']
        if words[2] == 'W':
            dest.append(('W', int(words[3]), int(words[4], 16), int(words[5], 16)))
        else:
            assert words[2] == 'D', line
            dest.append(('D', int(words[3])))
    return actual


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path,
                        default=ROOT / 'mtk/mt6895/drivers/audio/hakios.mt6895_headset.c')
    parser.add_argument('--header', type=Path, default=ROOT / 'xnu/files/bsd/sys/hakios.pmif.h')
    parser.add_argument('--shared-source', type=Path,
                        default=ROOT / 'mtk/mt6895/drivers/core/hakios.mt6895_pmif.c')
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--log-file', type=Path, help='Save compiler and complete model output')
    parser.add_argument('--result-file', type=Path, help='Write JSON verdict and tested input hashes')
    args = parser.parse_args()
    fixture_path = FIXTURES / 'mt6368-profile4-vendor.json'
    fixture = json.loads(fixture_path.read_text())
    inputs = dict(headset=sha256(args.source), pmif_header=sha256(args.header),
                  pmif_core=sha256(args.shared_source), oracle=sha256(fixture_path),
                  model_prefix=sha256(FIXTURES / 'mt6368-profile4-prefix.c'),
                  model_suffix=sha256(FIXTURES / 'mt6368-profile4-suffix.c'),
                  runner=sha256(Path(__file__)))
    # Replace an old verdict before compiling, so a failed run cannot leave an old PASS.
    if args.result_file:
        args.result_file.write_text(json.dumps(dict(status='RUNNING', inputs=inputs), indent=2) + '\n')

    def strip_includes(text):
        return re.sub(r'^#include .*\n', '', text, flags=re.M)

    parts = [(FIXTURES / 'mt6368-profile4-prefix.c').read_text(), model_parameters(fixture),
             args.header.read_text(), strip_includes(args.shared_source.read_text()),
             strip_includes(args.source.read_text()),
             (FIXTURES / 'mt6368-profile4-suffix.c').read_text()]
    summary = dict(status='FAIL', inputs=inputs)
    log = ''
    try:
        with tempfile.TemporaryDirectory(prefix='hakios-profile4-test-') as name:
            temp = Path(name)
            (temp / 'model.c').write_text('\n'.join(parts))
            command = [args.cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                       str(temp / 'model.c'), '-o', str(temp / 'model')]
            compiled = subprocess.run(command, text=True, capture_output=True)
            log += compiled.stdout + compiled.stderr
            if compiled.returncode:
                raise RuntimeError(f'host compiler exit {compiled.returncode}')
            result = subprocess.run([str(temp / 'model')], text=True, capture_output=True)
            log += result.stdout + result.stderr
            if result.returncode:
                raise RuntimeError(f'PMIF model exit {result.returncode}')
            expected, actual = vendor_events(fixture), driver_events(result.stdout)
            for phase in ('up', 'down'):
                if actual[phase] != expected[phase]:
                    for i, (a, b) in enumerate(zip(actual[phase], expected[phase])):
                        if a != b:
                            raise AssertionError((phase, i, 'driver', a, 'vendor oracle', b))
                    raise AssertionError((phase, 'length', len(actual[phase]), len(expected[phase])))
            summary.update(status='PASS', trace_events={p: len(v) for p, v in actual.items()},
                           model_summary=result.stdout.splitlines()[-1],
                           limitations=fixture['limitations'])
    except (RuntimeError, AssertionError, OSError, ValueError) as error:
        summary['error'] = str(error)
        print(log[-5000:])
    finally:
        if args.log_file:
            args.log_file.write_text(log)
        if args.result_file:
            args.result_file.write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))
    return 0 if summary['status'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
