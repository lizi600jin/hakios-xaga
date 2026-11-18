#!/usr/bin/env python3
"""Independent Trial723 parser checks built from reviewed source log formats.

All positive inputs are visibly synthetic. Output directories must be new.
This script never invokes adb/fastboot and never reads any device.
"""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WINDOW = 4096 + 4194304
PREFIX = 'mt6895-ufs-twoblock: '
MARKER = 'SYNTHETIC TRIAL723 HOST FORMAT FIXTURE'
LINES = [
    'bounded two-block READ10 lun2 lba2 blocks2 bytes8192 budget1; guarded handoff required',
    'guard magic=0x554c5345 version=1 trial=723 state=0',
    'opened control=0x2 shared=1',
    'request=1 lba=2 blocks=2 bytes=8192 admitted=1 readback_ok=1 issued=1 rings=1 '
    'response_valid=0x1fff failures=0 ocs=0 data_words=2048/2048 restored=1 copied=1 '
    'control=0x4 shared=1',
    'block=1 lba=2 reference_present=1 crc=0xd024e0c reference_crc=0xd024e0c match=1',
    'block=2 lba=3 reference_present=1 crc=0x58f10429 reference_crc=0x58f10429 match=1',
    'final control=0x2 shared=1 failed=0',
    'return-gate magic=0x554c5345 version=1 trial=723 state=2 allowed=1',
]


def raw_window(body):
    header = bytearray(4096)
    struct.pack_into('<II', header, 0, 0x474f4c48, 1)
    struct.pack_into('<3Q', header, 8, len(body), 0, 0)
    struct.pack_into('<Q', header, 64, sum(body))
    return bytes(header) + body + b'\0' * (4194304 - len(body))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--parser', required=True, type=Path)
    ap.add_argument('--out-dir', required=True, type=Path)
    a = ap.parse_args()
    a.out_dir.mkdir(parents=True, exist_ok=False)
    argfile = ROOT / 'arguments723.json'
    full_args = json.loads(argfile.read_text())['full_arguments']
    baseline = (MARKER + '\nboot-args = ' + full_args + '\n' +
                '\n'.join(PREFIX + line for line in LINES) + '\n')
    cases = [('synthetic-bare', baseline, True, None),
             ('synthetic-launchd', baseline.replace('boot-args = ',
               'com.apple.xpc.launchd|1970-01-01 08:00:19.803803 <Notice>: boot-args = '), True, None),
             ('synthetic-timestamps', baseline.replace(PREFIX, '[    1.000001]: ' + PREFIX), True, None)]
    mutations = [
        ('old-trial', 'trial=723', 'trial=722'),
        ('banner-wrong-lba', 'lun2 lba2', 'lun2 lba1'),
        ('banner-wrong-budget', 'budget1;', 'budget2;'),
        ('request-wrong-lba', 'request=1 lba=2', 'request=1 lba=1'),
        ('request-two-rings', 'rings=1', 'rings=2'),
        ('request-not-issued', 'issued=1', 'issued=0'),
        ('request-not-restored', 'restored=1', 'restored=0'),
        ('request-not-copied', 'copied=1', 'copied=0'),
        ('request-wrong-response', 'response_valid=0x1fff', 'response_valid=0x1ffe'),
        ('request-failure', 'failures=0', 'failures=1'),
        ('request-wrong-ocs', 'ocs=0', 'ocs=1'),
        ('request-short-data', 'data_words=2048/2048', 'data_words=2047/2048'),
        ('request-wrong-control', 'control=0x4 shared=1', 'control=0x2 shared=1'),
        ('block2-wrong-lba', 'block=2 lba=3', 'block=2 lba=2'),
        ('block1-wrong-crc', 'crc=0xd024e0c reference_crc=', 'crc=0x58f10429 reference_crc='),
        ('block2-wrong-reference', 'reference_crc=0x58f10429', 'reference_crc=0xd024e0c'),
        ('block2-match-false', 'reference_crc=0x58f10429 match=1', 'reference_crc=0x58f10429 match=0'),
        ('final-failed', 'failed=0', 'failed=1'),
        ('final-wrong-state', 'final control=0x2 shared=1', 'final control=0x304 shared=3'),
        ('gate-unused', 'trial=723 state=2 allowed=1', 'trial=723 state=0 allowed=1'),
        ('gate-claimed', 'trial=723 state=2 allowed=1', 'trial=723 state=1 allowed=1'),
        ('gate-denied', 'state=2 allowed=1', 'state=2 allowed=0'),
        ('boot-args-wrong-start', 'mtk_ufs_start_lba=2', 'mtk_ufs_start_lba=1'),
    ]
    for name, old, new in mutations:
        assert old in baseline, name
        cases.append((name, baseline.replace(old, new), False, None))
    cases.extend([
        ('missing-gate', baseline.replace(PREFIX + LINES[7] + '\n', ''), False, None),
        ('missing-block2', baseline.replace(PREFIX + LINES[5] + '\n', ''), False, None),
        ('duplicate-request', baseline + PREFIX + LINES[3] + '\n', False, None),
        ('out-of-order', baseline.replace(PREFIX + LINES[4] + '\n' + PREFIX + LINES[5],
                                          PREFIX + LINES[5] + '\n' + PREFIX + LINES[4]), False, None),
        ('duplicate-boot-args', baseline + 'boot-args = ' + full_args + '\n', False, None),
        ('no-boot-args', baseline.replace('boot-args = ' + full_args + '\n', ''), False, None),
    ])
    for name in ['same-window', 'short-pre', 'bad-magic', 'bad-version', 'bad-checksum',
                 'bad-reason', 'bad-code', 'raw-body-mismatch', 'short-raw', 'length-zero',
                 'length-overflow', 'missing-pre', 'missing-arguments']:
        cases.append((name, baseline, False, name))

    cases.extend([
        ('no-marker', baseline.replace(MARKER + '\n', ''), True, None),
        ('quoted-prefix', baseline.replace(PREFIX, 'quoted ' + PREFIX), False, None),
        ('trailing-space', baseline.replace(full_args + '\n', full_args + ' \n'), False, None),
        ('header-padding-only', baseline, False, 'header-padding-only'),
        ('tail-padding-only', baseline, False, 'tail-padding-only'),
        ('invalid-pre-checksum', baseline, False, 'invalid-pre-checksum'),
        ('wrong-file-and-log', baseline.replace('mtk_ufs_start_lba=2', 'mtk_ufs_start_lba=1'),
         False, 'wrong-file-and-log'),
    ])
    results = []
    for name, text, positive, corrupt in cases:
        dest = a.out_dir / name
        dest.mkdir()
        body = text.encode()
        raw = bytearray(raw_window(body))
        pre = raw_window(b'SYNTHETIC prior HLOG\n')
        expected_args = argfile
        if corrupt == 'wrong-file-and-log':
            expected_args = dest / 'alternate-args.json'
            expected_args.write_text(argfile.read_text().replace('mtk_ufs_start_lba=2', 'mtk_ufs_start_lba=1'))
        if corrupt in ('header-padding-only', 'tail-padding-only', 'invalid-pre-checksum'):
            pre = bytearray(raw)
            offset = {'header-padding-only': 72, 'tail-padding-only': -1, 'invalid-pre-checksum': 64}
            pre[offset[corrupt]] ^= 1
        if corrupt == 'same-window': pre = bytes(raw)
        elif corrupt == 'short-pre': pre = pre[:4096]
        elif corrupt == 'bad-magic': struct.pack_into('<I', raw, 0, 0)
        elif corrupt == 'bad-version': struct.pack_into('<I', raw, 4, 2)
        elif corrupt == 'bad-checksum': struct.pack_into('<Q', raw, 64, sum(body) + 1)
        elif corrupt == 'bad-reason': struct.pack_into('<Q', raw, 16, 1)
        elif corrupt == 'bad-code': struct.pack_into('<Q', raw, 24, 1)
        elif corrupt == 'raw-body-mismatch': raw[4096] ^= 1
        elif corrupt == 'short-raw': raw = raw[:-1]
        elif corrupt == 'length-zero': struct.pack_into('<Q', raw, 8, 0)
        elif corrupt == 'length-overflow': struct.pack_into('<Q', raw, 8, 4194305)
        (dest / 'raw.bin').write_bytes(raw)
        (dest / 'body.bin').write_bytes(body)
        (dest / 'pre.bin').write_bytes(pre)
        cmd = [sys.executable, str(a.parser), str(dest / 'raw.bin'), '--body', str(dest / 'body.bin')]
        if corrupt != 'missing-pre': cmd.extend(['--pre', str(dest / 'pre.bin')])
        if corrupt != 'missing-arguments': cmd.extend(['--arguments', str(expected_args)])
        cmd.extend(['--output', str(dest / 'report.json')])
        done = subprocess.run(cmd, text=True, capture_output=True)
        (dest / 'stdout.txt').write_text(done.stdout)
        (dest / 'stderr.txt').write_text(done.stderr)
        (dest / 'rc.txt').write_text(str(done.returncode) + '\n')
        report = json.loads((dest / 'report.json').read_text()) if (dest / 'report.json').exists() else {}
        status = report.get('status')
        if positive:
            passed = done.returncode == 0 and status == 'FORMAT_PASS_ONLY'
            passed &= report.get('verdicts', {}).get('loader_gate') == {'trial': 723, 'state': 2, 'allowed': 1, 'ok': True}
        else:
            passed = done.returncode != 0 and status not in ['PASS', 'FORMAT_PASS_ONLY']
        expected_errors = {
            'trailing-space': 'boot-args value does not equal',
            'wrong-file-and-log': 'arguments file is not the frozen one',
            'header-padding-only': 'pre and post HLOG records are identical',
            'tail-padding-only': 'pre and post HLOG records are identical',
            'invalid-pre-checksum': 'pre body does not match its own header checksum',
            'quoted-prefix': 'line(s) carry the driver prefix',
        }
        if name in expected_errors:
            passed &= done.returncode == 1 and expected_errors[name] in report.get('error', '')
        results.append({'case': name, 'check_passed': bool(passed), 'rc': done.returncode,
                        'status': status, 'error': report.get('error')})
    summary = {'parser': str(a.parser), 'parser_sha256': hashlib.sha256(a.parser.read_bytes()).hexdigest(),
               'synthetic_only': True, 'device_touched': False, 'cases': results,
               'case_count': len(results), 'all_checks_passed': all(r['check_passed'] for r in results)}
    (a.out_dir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({'case_count': len(results), 'all_checks_passed': summary['all_checks_passed'],
                      'failed_cases': [r for r in results if not r['check_passed']],
                      'report': str(a.out_dir / 'summary.json')}, indent=2))
    return 0 if summary['all_checks_passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
