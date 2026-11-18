#!/usr/bin/env python3
"""Trial723 HLOG parser.

Fixed to this trial, not to Trial722's values: start LBA 2, two blocks at LBA 2 and 3, CRCs
0x0d024e0c and 0x58f10429, banner budget=1. A Trial722 log must not pass here.

Reported separately and never merged:
  * kernel_final  — the kernel's own `final ... failed=0`
  * loader_gate   — the loader's `return-gate ... trial=723 state=2 allowed=1`
    UNUSED (state=0 allowed=1) is refused: only a restored lease counts.

A synthetic fixture can only ever be FORMAT_PASS_ONLY, never a hardware PASS.
"""
import argparse, hashlib, json, re, struct, sys
from pathlib import Path

WINDOW = 4198400
BLOCK_BYTES = 4096
PREFIX = 'mt6895-ufs-twoblock: '
SYNTHETIC = 'SYNTHETIC TRIAL723 HOST FORMAT FIXTURE'
TRIAL = 723
START_LBA = 2
BLOCKS = 2
REF = {2: 0x0d024e0c, 3: 0x58f10429}
RESPONSE_VALID = 0x1fff
STAMP = re.compile(r'^\[ *[0-9]+\.[0-9]{6}\]: (?=' + re.escape(PREFIX) + r')', re.MULTILINE)
# A driver line begins at column 0, optionally behind the one timestamp form seen on hardware.
# Anything else in front of the prefix (quotes, other prefixes) must not match.
DRIVER = re.compile(r'^(?:\[ *[0-9]+\.[0-9]{6}\]: )?' + re.escape(PREFIX) + r'(.*)$')
LAUNCHD = re.compile(r'^com\.apple\.xpc\.launchd\|[^\n]*?<Notice>: boot-args = (.*)$', re.MULTILINE)
BARE = re.compile(r'^\s*boot-args = (.*)$', re.MULTILINE)

ORDER = [
    ('banner', re.compile(r'^bounded two-block READ10 lun2 lba(\d+) blocks(\d+) bytes(\d+) '
                          r'budget1; guarded handoff required$')),
    ('guard',  re.compile(r'^guard magic=0x554c5345 version=1 trial=723 state=0$')),
    ('opened', re.compile(r'^opened control=0x2 shared=1$')),
    ('request', re.compile(r'^request=1 lba=(\d+) blocks=(\d+) bytes=(\d+) admitted=(\d+) '
                           r'readback_ok=(\d+) issued=(\d+) rings=(\d+) response_valid=(\S+) '
                           r'failures=(\S+) ocs=(\S+) data_words=(\d+)/(\d+) restored=(\d+) '
                           r'copied=(\d+) control=(\S+) shared=(\d+)$')),
    ('block1', re.compile(r'^block=1 lba=(\d+) reference_present=1 crc=(\S+) reference_crc=(\S+) match=1$')),
    ('block2', re.compile(r'^block=2 lba=(\d+) reference_present=1 crc=(\S+) reference_crc=(\S+) match=1$')),
    ('final',  re.compile(r'^final control=(\S+) shared=(\d+) failed=(\d+)$')),
    ('gate',   re.compile(r'^return-gate magic=0x554c5345 version=1 trial=723 state=(\d+) allowed=(\d+)$')),
]

class Reject(Exception):
    pass

def need(c, m):
    if not c:
        raise Reject(m)

def num(t):
    return int(t, 16) if t.lower().startswith('0x') else int(t)

def driver_lines(text):
    """Return (lines, stray) where stray counts prefix-bearing lines that did not anchor."""
    out, stray = [], 0
    for line in text.splitlines():
        m = DRIVER.match(line)
        if m:
            out.append(m.group(1))
        elif PREFIX in line:
            stray += 1
    return out, stray

def check_sequence(lines):
    need(len(lines) == len(ORDER),
         f'driver lines: expected exactly {len(ORDER)} in order, found {len(lines)}')
    got = {}
    for idx, (name, rx) in enumerate(ORDER):
        m = rx.match(lines[idx])
        need(m is not None, f'line {idx + 1}: expected {name}, got {lines[idx][:70]!r}')
        got[name] = m
    return got

def check_values(g):
    b = g['banner']
    need(int(b.group(1)) == START_LBA, f'banner lba must be {START_LBA}')
    need(int(b.group(2)) == BLOCKS, 'banner blocks must be 2')
    need(int(b.group(3)) == BLOCKS * BLOCK_BYTES, 'banner bytes must be 8192')

    r = g['request']
    need(int(r.group(1)) == START_LBA, f'request start lba must be {START_LBA}')
    need(int(r.group(2)) == BLOCKS, 'request blocks must be 2')
    need(int(r.group(3)) == BLOCKS * BLOCK_BYTES, 'request bytes must be 8192')
    for grp, name in ((4, 'admitted'), (5, 'readback_ok'), (6, 'issued'), (7, 'rings'),
                      (13, 'restored'), (14, 'copied')):
        need(int(r.group(grp)) == 1, f'{name} must be 1')
    need(int(r.group(11)) == 2048 and int(r.group(12)) == 2048, 'data_words must be 2048/2048')
    need(num(r.group(8)) == RESPONSE_VALID, f'response_valid must be {RESPONSE_VALID:#x}')
    need(num(r.group(9)) == 0, 'failures must be 0')
    need(num(r.group(10)) == 0, 'ocs must be 0')
    need(num(r.group(15)) == 0x4, 'request control must be 0x4 (DRAINED sample)')
    need(int(r.group(16)) == 1, 'request shared must be 1')

    for name, lba, crc in (('block1', START_LBA, REF[2]), ('block2', START_LBA + 1, REF[3])):
        m = g[name]
        need(int(m.group(1)) == lba, f'{name} lba must be {lba}')
        need(num(m.group(2)) == crc, f'{name} crc {num(m.group(2)):#x} != {crc:#x}')
        need(num(m.group(3)) == crc, f'{name} reference_crc {num(m.group(3)):#x} != {crc:#x}')

    f = g['final']
    need(num(f.group(1)) == 0x2, 'final control must be 0x2')
    need(int(f.group(2)) == 1, 'final shared must be 1')
    need(int(f.group(3)) == 0, 'final failed must be 0')

    gate = g['gate']
    state, allowed = int(gate.group(1)), int(gate.group(2))
    need(state == 2, f'gate state must be 2 (restored), got {state}')
    need(allowed == 1, 'gate allowed must be 1')
    return {'start_lba': START_LBA, 'blocks': BLOCKS, 'bytes': BLOCKS * BLOCK_BYTES,
            'block_crcs': {'2': hex(REF[2]), '3': hex(REF[3])},
            'response_valid': hex(RESPONSE_VALID),
            'kernel_final': {'failed': 0, 'control': '0x2', 'shared': 1},
            'loader_gate': {'trial': TRIAL, 'state': state, 'allowed': allowed}}

def check_boot_args(text, full_args):
    hits = LAUNCHD.findall(text) + BARE.findall(text)
    need(len(hits) == 1,
         f'boot-args: expected exactly 1 line (launchd or bare), found {len(hits)}')
    # Compare the value exactly. Stripping would let a trailing space or newline through, and the
    # frozen argument line must match byte for byte.
    need(hits[0] == full_args,
         'boot-args value does not equal arguments723.json full_arguments')
    return True

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('raw'); ap.add_argument('--body', required=True)
    ap.add_argument('--pre', required=True); ap.add_argument('--arguments', required=True)
    ap.add_argument('--output', required=True)
    a = ap.parse_args()
    out = Path(a.output)
    if out.exists():
        print(json.dumps({'rc': 8, 'error': 'output exists; refusing to overwrite'})); return 8
    rep = {'parser': 'trial723', 'verdicts': {}}
    try:
        raw = Path(a.raw).read_bytes(); body = Path(a.body).read_bytes()
        pre = Path(a.pre).read_bytes()
        # Pin the arguments file to the frozen digest before trusting a single byte of it.
        ARGS_SHA = 'c7ac172058a80333d5553a1ef4bea7ab912e923c22da4211bea5fd69ab27c54d'
        args_bytes = Path(a.arguments).read_bytes()
        got_args_sha = hashlib.sha256(args_bytes).hexdigest()
        need(got_args_sha == ARGS_SHA,
             f'arguments file is not the frozen one: {got_args_sha[:16]} != {ARGS_SHA[:16]}')
        rep['arguments_sha256'] = got_args_sha
        full = json.loads(args_bytes)['full_arguments']

        need(len(raw) == WINDOW, f'raw must be {WINDOW} bytes, got {len(raw)}')
        need(len(pre) >= WINDOW, f'pre must hold {WINDOW} bytes, got {len(pre)}')
        magic, ver = struct.unpack_from('<II', raw, 0)
        need(magic == 0x474f4c48 and ver == 1, 'HLOG identity')
        ln, reason, code = struct.unpack_from('<3Q', raw, 8)
        need(0 < ln <= 4194304, 'body length bound')
        need(ln == len(body), f'body length {len(body)} != header {ln}')
        need(raw[4096:4096 + ln] == body, 'body does not match the raw capture')
        total = struct.unpack_from('<Q', raw, 8 + 7 * 8)[0]
        need(sum(body) & 0xFFFFFFFFFFFFFFFF == total, 'full additive checksum')
        need(reason == 0 and code == 0, 'abnormal reason/code')

        pre_win = pre[:WINDOW]
        pre_sha = hashlib.sha256(pre_win).hexdigest()
        post_sha = hashlib.sha256(raw).hexdigest()

        # A whole-window hash can differ from trailing padding alone. Compare the record the
        # header actually declares — header block plus body — so padding cannot make a stale
        # capture look new.
        pre_magic, pre_ver = struct.unpack_from('<II', pre_win, 0)
        need(pre_magic == 0x474f4c48 and pre_ver == 1,
             'pre window is not an HLOG record; cannot establish a baseline')
        pre_len = struct.unpack_from('<Q', pre_win, 8)[0]
        need(0 < pre_len <= 4194304, 'pre body length bound')
        # struct logbook_hdr is 72 bytes; the rest of the 4096-byte block is reserved and must
        # not count as record content, or a reserved-byte change would look like a new capture.
        HDR = 72
        pre_sum = struct.unpack_from('<Q', pre_win, 64)[0]
        pre_body = pre_win[4096:4096 + pre_len]
        need(sum(pre_body) & 0xFFFFFFFFFFFFFFFF == pre_sum,
             'pre body does not match its own header checksum')
        pre_rec = pre_win[:HDR] + pre_body
        post_rec = raw[:HDR] + body
        pre_rec_sha = hashlib.sha256(pre_rec).hexdigest()
        post_rec_sha = hashlib.sha256(post_rec).hexdigest()
        rep['pre_post'] = {'pre_sha256': pre_sha, 'post_sha256': post_sha,
                           'pre_record_bytes': len(pre_rec),
                           'post_record_bytes': len(post_rec),
                           'pre_record_sha256': pre_rec_sha,
                           'post_record_sha256': post_rec_sha,
                           'window_hash_changed': pre_sha != post_sha,
                           'record_changed': pre_rec_sha != post_rec_sha}
        need(pre_rec_sha != post_rec_sha,
             'pre and post HLOG records are identical; only padding differs, not a new capture')
        need(pre_sha != post_sha, 'pre and post windows are identical; not a new capture')

        text = body.decode('utf-8', 'replace')
        # This tool never certifies hardware. Any successful parse reports FORMAT_PASS_ONLY; the
        # reviewer decides hardware PASS from the raw A2/A13 captures. That removes the whole
        # class of "did the fixture carry the marker" ambiguity.
        synthetic = True
        check_boot_args(text, full)
        rep['boot_args_match'] = True

        lines, stray = driver_lines(text)
        rep['stray_prefixed_lines'] = stray
        need(stray == 0, f'{stray} line(s) carry the driver prefix but do not start at column 0')
        # Record what each side actually shows before judging either one, so a rejection still
        # carries both observations instead of an empty verdicts object.
        fin = next((l for l in lines if l.startswith('final control=')), None)
        gte = next((l for l in lines if l.startswith('return-gate ')), None)
        rep['observed'] = {'kernel_final_line': fin, 'loader_gate_line': gte}
        if fin is not None:
            fm = re.match(r'^final control=(\S+) shared=(\d+) failed=(\d+)$', fin)
            if fm:
                rep['verdicts']['kernel_final'] = {
                    'control': fm.group(1), 'shared': int(fm.group(2)),
                    'failed': int(fm.group(3)),
                    'ok': num(fm.group(1)) == 0x2 and int(fm.group(2)) == 1 and int(fm.group(3)) == 0}
        if gte is not None:
            gm = re.match(r'^return-gate magic=0x554c5345 version=1 trial=723 state=(\d+) allowed=(\d+)$', gte)
            if gm:
                st, al = int(gm.group(1)), int(gm.group(2))
                rep['verdicts']['loader_gate'] = {
                    'trial': TRIAL, 'state': st, 'allowed': al,
                    'ok': st == 2 and al == 1}
        need(rep['verdicts'].get('kernel_final', {}).get('ok') is True,
             'kernel_final did not show control=0x2 shared=1 failed=0')
        need(rep['verdicts'].get('loader_gate', {}).get('ok') is True,
             'loader_gate is not trial=723 state=2 allowed=1')
        got = check_sequence(lines)
        rep['validated'] = check_values(got)
        rep['raw'] = {'bytes': len(raw), 'sha256': post_sha}
        rep['body'] = {'bytes': len(body), 'sha256': hashlib.sha256(body).hexdigest()}
        rep['status'] = 'FORMAT_PASS_ONLY'
        rep['NORMAL_RETURN'] = 'NOT_HARDWARE_EVIDENCE'
        rep['evidence_kind'] = 'HOST_PARSER_DOES_NOT_CERTIFY_HARDWARE'
        rep['hardware_verdict'] = 'decided by the reviewer from the raw captures, not by this tool'
        rc = 0
    except Reject as e:
        rep['status'] = 'INCOMPLETE_OR_REJECTED'; rep['error'] = str(e); rc = 1
    except (ValueError, struct.error, OSError, KeyError) as e:
        rep['status'] = 'INCOMPLETE_OR_REJECTED'; rep['error'] = f'{type(e).__name__}: {e}'; rc = 2
    try:
        with open(out, 'x') as f:
            f.write(json.dumps(rep, indent=2) + '\n')
    except FileExistsError:
        print(json.dumps({'rc': 8, 'error': 'output exists'})); return 8
    print(json.dumps({'rc': rc, 'status': rep['status'], 'error': rep.get('error')}, indent=2))
    return rc

if __name__ == '__main__':
    sys.exit(main())
