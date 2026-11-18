#!/usr/bin/env python3
"""Check the ten Trial726 driver records against the frozen expectations.

Every check is fixed to this trial: start LBA 2, two blocks, LBA 2/3 CRCs, byte offset 12031,
length 770 split 257+513, capacity 770, and the caller-side observation. A Trial725 or 723 log
cannot pass.

Success here means FORMAT_PASS_ONLY. This tool never certifies hardware; the review side decides
that from the raw captures.
"""
import re
import argparse
import json
import runpy
import sys
from pathlib import Path

PREFIX = 'mt6895-ufs-twoblock: '
EXPECT = [
    r'^bounded READ10 lun2 lba2 blocks2 bytes8192 budget1; guarded handoff required$',
    r'^guard magic=0x554c5345 version=1 trial=726 state=0$',
    r'^opened control=0x2 shared=1$',
    r'^request=1 lba=2 blocks=2 bytes=8192 admitted=1 readback_ok=1 issued=1 rings=1 '
    r'response_valid=0x1fff failures=0 ocs=0 data_words=2048/2048 restored=1 copied=1 '
    r'control=0x4 shared=1$',
    r'^block=1 lba=2 reference_present=1 crc=0xd024e0c reference_crc=0xd024e0c match=1$',
    r'^block=2 lba=3 reference_present=1 crc=0x58f10429 reference_crc=0x58f10429 match=1$',
    r'^slice offset=12031 blocks=2 bytes=770 first=257 second=513 cap=770 '
    r'crc=0x8f350378 delivered=1$',
    r'^caller bytes=770 crc=0x8f350378$',
    r'^final control=0x2 shared=1 failed=0 delivered=770$',
    r'^return-gate magic=0x554c5345 version=1 trial=726 state=2 allowed=1$',
]


def records(text):
    out = []
    for line in text.splitlines():
        if line.startswith('#'):
            continue
        i = line.find(PREFIX)
        out.append(line[i + len(PREFIX):] if i >= 0 else line.strip())
    return [r for r in out if r]


def check(lines):
    if len(lines) != len(EXPECT):
        return [f'expected {len(EXPECT)} records, found {len(lines)}']
    return [f'record {i + 1} does not match: {lines[i][:60]!r}'
            for i, pat in enumerate(EXPECT) if not re.match(pat, lines[i])]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('lines', type=Path)
    parser.add_argument('--pre', type=Path)
    parser.add_argument('--post', type=Path)
    opts = parser.parse_args()
    if bool(opts.pre) != bool(opts.post):
        parser.error('--pre and --post must be supplied together')
    lines = records(opts.lines.read_text())
    problems = check(lines)
    if problems:
        raise ValueError(problems)
    for idx, before, after in ((6, 'bytes=770', 'bytes=769'), (7, '0x8f350378', '0x4192ff1d'),
                               (9, 'trial=726', 'trial=725')):
        changed = list(lines)
        changed[idx] = changed[idx].replace(before, after)
        if not check(changed):
            raise ValueError('corrupt fixture passed')
    if not check(lines[:-1]):
        raise ValueError('short fixture passed')
    if opts.pre:
        here = Path(__file__).resolve().parent
        tools = runpy.run_path(str(here.parent / 'ufs-mixed-length-725/parse-hlog-725.py'))
        require = tools['require']
        exp = json.loads((here / 'package-726.json').read_bytes())['hlog']
        pre, post = opts.pre.read_bytes(), opts.post.read_bytes()
        old_body, old, _, _ = tools['record'](pre)
        body, curr, reason, code = tools['record'](post)
        require(old == exp['pre_record_sha256'] and curr == exp['post_record_sha256'] and
                body != old_body and reason == code == 0, 'unexpected record')
        require(len(post) == tools['WINDOW'] and tools['sha'](post) == exp['post_window_sha256'], 'post window')
        require(tools['sha'](body) == exp['body_sha256'], 'body checksum')
        raw_lines = [tools['STAMP'].fullmatch(line.decode('ascii')) for line in body.splitlines()
                     if PREFIX.encode() in line]
        require(all(raw_lines) and [m[1] for m in raw_lines] == lines, 'raw driver records')
        args = json.loads((here / 'arguments726.json').read_bytes())['full_arguments'].encode()
        matches = re.findall(rb'^(?:com\.apple\.xpc\.launchd\|[^\n]*?<Notice>: |[ \t]*)boot-args = ([^\r\n]*)\r?$', body, re.M)
        require(matches == [args], 'exact boot arguments')
    print(f'FORMAT_PASS_ONLY: {len(lines)} records match the Trial726 expectations')
    return 0


if __name__ == '__main__':
    sys.exit(main())
