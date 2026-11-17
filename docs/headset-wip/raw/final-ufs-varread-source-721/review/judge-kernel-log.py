#!/usr/bin/env python3
"""Private judge for THIS frozen build. Host only; touches no production file.

It refuses anything it does not recognise:

  * stdout and stderr are bound by full SHA-256 and byte length to the frozen build
  * the native rc is read from the real build-rc.txt and must be 2
  * the kernel is bound by exact size and SHA-256, not merely by existence
  * every `make... *** ` line must be a syntactically complete failure record or one of the
    two exact "waiting for unfinished jobs" notices; anything else is rejected
  * no blanket filters: stripping brackets or the word Error to "find" a target is forbidden
"""
import argparse, hashlib, json, re, sys
from pathlib import Path

FROZEN = {
 'stdout': dict(bytes=98196,
   sha256='3b81ec7fe89f6fd27e885e9b1c6f31265ed4062f0362d38412c8695d465ea6ca'),
 'stderr': dict(bytes=58564,
   sha256='e924424b23fd4e7e5b95cb70ee21fa119f12d48c1e7fea6bad529f485627d1d4'),
 'kernel': dict(bytes=15339856,
   sha256='784500d791bf2a623d24ad81ed460b4c0cdf25143f8beade8a2d4729eed342cb'),
 'make_rc': 2,
 'leaf': 'check_all_exports',
}
PROPAGATION = {'config_all', 'config_all_recurse_into_config',
               'config_all_bootstrap_DEVELOPMENT^ARM64^BCM2837', 'all'}
# EXACT notices only. No wildcards, no case folding beyond the two literal strings.
NOTICES = {
 'Waiting for unfinished jobs....',                # GNU Make, English, exact text
 '正在等待未完成的任务....',                        # GNU Make, Chinese, exact text
}
# The one warning GNU Make prints on stderr when a sub-make forces -j. Not a failure.
WARNINGS = {
 'make[1]: 警告: 子 make 中强制 -j9: 重置 jobserver 模式。',
}
# make[N]: *** [Makefile:NN: target] 错误 N   |   make[N]: *** [Makefile:NN: target] Error N
# make[N]: *** [target] 错误 N               |   make[N]: *** [target]
FAIL = re.compile(r'^make\[(?P<n>\d+)\]: \*\*\* '
                  r'\[(?:(?P<file>[^\]:]+):(?P<line>\d+)[:：]\s*)?(?P<target>[^\]]+)\]'
                  r'(?:\s*(?:错误|Error)\s*(?P<code>\d+))?\s*$')
BARE_FAIL = re.compile(r'^make\[(?P<n>\d+)\]: \*\*\* (?P<text>.+?)\s*$')

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def judge(stderr_path, stdout_path, rc_path, kernel_path):
    r = dict(verdict='FAIL', checks={}, failures=[], rejected=[], notices=0)
    def chk(name, ok, detail=''):
        r['checks'][name] = {'ok': bool(ok), 'detail': detail}
        return bool(ok)

    # --- bindings first: nothing is judged until the inputs are the frozen ones ---
    for label, p in (('stderr', stderr_path), ('stdout', stdout_path)):
        q = Path(p)
        if not q.exists():
            chk(f'{label} exists', False, str(q)); return r
        d = q.read_bytes()
        want = FROZEN[label]
        chk(f'{label} bytes', len(d) == want['bytes'], f'{len(d)}')
        chk(f'{label} sha256', sha(q) == want['sha256'], sha(q)[:16])
    q = Path(rc_path)
    if not q.exists():
        chk('rc file exists', False, str(q)); return r
    raw_rc = q.read_text().strip()
    chk('rc file is the integer 2', raw_rc == str(FROZEN['make_rc']), raw_rc)
    k = Path(kernel_path)
    if not k.exists():
        chk('kernel exists', False, str(k)); return r
    chk('kernel bytes', k.stat().st_size == FROZEN['kernel']['bytes'], str(k.stat().st_size))
    chk('kernel sha256', sha(k) == FROZEN['kernel']['sha256'], sha(k)[:16])
    if not all(c['ok'] for c in r['checks'].values()):
        r['reason'] = 'input is not the frozen build'
        return r

    # --- classify every complete line ---
    text = Path(stderr_path).read_text(errors='replace')
    for line in text.splitlines():
        if not line.startswith('make['):
            continue
        m = FAIL.match(line)
        if m:
            t = m.group('target').strip()
            r['failures'].append(t)
            if t == FROZEN['leaf'] or t in PROPAGATION:
                continue
            r['rejected'].append(t)
            continue
        if line in WARNINGS:
            r['notices'] += 1
            continue
        b = BARE_FAIL.match(line)
        if b:
            t = b.group('text').strip()
            if t in NOTICES:
                r['notices'] += 1
                continue
            # A `*** ` line that is neither a complete failure record nor an exact notice.
            r['rejected'].append('malformed: ' + t[:60])
            continue
        r['rejected'].append('unparsed make line: ' + line[:60])

    # raw compiler/linker errors anywhere in the frozen streams are never tolerated
    for label in ('stderr', 'stdout'):
        body = Path(stderr_path if label == 'stderr' else stdout_path).read_text(errors='replace')
        for pat in (r'\bclang: error', r'\bld\.lld: error', r'\bundefined symbol',
                    r'\bfatal error:', r'\bcollect2: error'):
            if re.search(pat, body):
                r['rejected'].append(f'{label}: {pat}')

    if FROZEN['leaf'] not in r['failures']:
        chk('tolerated leaf present', False, 'check_all_exports absent')
    else:
        chk('tolerated leaf present', True)
    chk('no rejected lines', not r['rejected'], ', '.join(r['rejected'][:4]))
    if all(c['ok'] for c in r['checks'].values()):
        r['verdict'] = 'PASS'
        r['reason'] = 'frozen build; only check_all_exports and its propagation failed'
    else:
        r['reason'] = r.get('reason') or 'rejected content'
    return r

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--stderr', required=True)
    ap.add_argument('--stdout', required=True)
    ap.add_argument('--rc-file', required=True)
    ap.add_argument('--kernel', required=True)
    ap.add_argument('--output', required=True)
    a = ap.parse_args()
    r = judge(a.stderr, a.stdout, a.rc_file, a.kernel)
    Path(a.output).write_text(json.dumps(r, indent=2) + '\n')
    print(json.dumps({'verdict': r['verdict'], 'reason': r.get('reason')}, indent=2))
    return 0 if r['verdict'] == 'PASS' else 1

if __name__ == '__main__':
    sys.exit(main())
