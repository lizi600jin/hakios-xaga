#!/usr/bin/env python3
"""Review DSH's recorded recovery evidence without querying any device."""
import argparse
import hashlib
import json
from pathlib import Path
import re

BASE = Path(__file__).resolve().parent
STOCK_MD5 = 'a63b2e35a7c2522cb24a6a10332b9bc8'

def require(value, message):
    if not value:
        raise ValueError(message)

def field(text, name, pattern):
    matches = re.findall(r'^\s*'+re.escape(name)+r'\s+('+pattern+r')(?=\s|$)[^\r\n]*$', text, re.M | re.I)
    require(len(matches) == 1, 'missing/repeated recovery field '+name)
    return matches[0]

def review(directory, mode):
    names = ['A-preflight.txt', 'B-flash.txt', 'D-recovery.txt', 'D-verify.txt']
    evidence = {name: (directory/name).read_bytes() for name in names}
    texts = {name: data.decode() for name, data in evidence.items()}
    verify = texts['D-verify.txt']
    boot_id = field(verify, 'boot_id', '[0-9a-f-]{36}')
    previous = re.findall(r'boot_id\s+([0-9a-f-]{36})', texts['A-preflight.txt'])
    require(len(previous) == 1 and previous[0] != boot_id, 'stock recovery lacks fresh boot ID')
    checks = dict(slot_a=field(verify, 'slot', '_[a-z]') == '_a',
                  boot_completed=field(verify, 'boot_completed', '[0-9]+') == '1',
                  selinux=field(verify, 'SELinux', '[A-Za-z]+') == 'Enforcing',
                  partition_bytes=int(field(verify, 'boot_a size', '[0-9]+')) == 67108864,
                  stock_md5=field(verify, 'boot_a MD5', '[0-9a-f]{32}') == STOCK_MD5,
                  surfaceflinger=bool(re.search(r'SurfaceFlinger.*(?:rc=0|正常|Build configuration:)', verify)))
    require(all(checks.values()), 'stock recovery field mismatch')
    recover = texts['D-recovery.txt']
    commands = re.findall(r'\brc=(\d+) timed_out=(False|True)', recover)
    require(len(commands) >= (3 if mode == 'automatic' else 2), 'missing native recovery command records')
    require(all(rc == '0' and timeout == 'False' for rc, timeout in commands[-2:]),
            'stock flash/reboot was not a native success')
    if mode == 'automatic':
        require(all(rc == '0' and timeout == 'False' for rc, timeout in commands), 'automatic recovery had failed command')
        require('timed_out=True' not in recover, 'automatic recovery contains timeout')
    require(re.search(r"Sending 'boot_a'.*OKAY", recover) and
            re.search(r"Writing 'boot_a'.*OKAY", recover), 'missing stock flash acknowledgement')
    return dict(status='PASS', checks=checks, boot_id=boot_id, previous_boot_id=previous[0],
                stock_md5=STOCK_MD5, partition_bytes=67108864, recovery_mode=mode,
                native_recovery_rcs=[int(rc) for rc, _ in commands],
                evidence_sha256={name: hashlib.sha256(data).hexdigest() for name, data in evidence.items()},
                limits=['Independent host review of recorded DSH observations; no direct Codex device query',
                        'Automatic/manual classification must match the recorded recovery path'])

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', type=Path)
    ap.add_argument('--mode', choices=['automatic', 'manual'], required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    require(not args.output.exists(), 'review output exists; choose a fresh path')
    report = review(args.directory, args.mode)
    args.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
