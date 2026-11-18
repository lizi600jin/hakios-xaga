#!/usr/bin/env python3
"""Rebuild the tested Trial726 driver from the 725 archive and replay its tests.

The 725 tree is reconstructed the same way its own checker does it: start from the frozen 723
source, apply the recorded patches in order, then apply this trial's incremental patch. Nothing
is taken on trust — every source and test file is checked against source-sha256.json first.
"""
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
RAW = HERE.parent
PREV = RAW / 'ufs-mixed-length-725'
OLDER = RAW / 'ufs-read-length-724'
BASE = RAW / 'final-ufs-varstart-source-723'


def apply(work, patch):
    subprocess.run(['git', 'apply', str(patch)], cwd=work, check=True)


def build_725(work):
    """Reproduce the 725 tree exactly as ufs-mixed-length-725/check-platform.py does."""
    for folder in ('source', 'tests'):
        shutil.copytree(BASE / folder, work / folder)
    subprocess.run(['git', 'init', '-q'], cwd=work, check=True)
    apply(work, OLDER / '723-to-724.patch')
    apply(work, OLDER / '724-platform.patch')
    apply(work, PREV / '724-to-725-platform.patch')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference-window', type=Path,
                    help='64 KiB LUN2 window; byte-range positives compare against it')
    args = ap.parse_args()

    with tempfile.TemporaryDirectory(prefix='ufs-726-') as tmp:
        work = Path(tmp)
        build_725(work)
        apply(work, HERE / '725-to-726-platform.patch')

        frozen = json.loads((HERE / 'source-sha256.json').read_bytes())
        for rel, want in frozen.items():
            got = hashlib.sha256((work / rel).read_bytes()).hexdigest()
            if got != want:
                raise SystemExit(f'Unexpected source hash for {rel}: {got}')

        print('source hashes match source-sha256.json')

        # The platform test needs the reference window; without it the suite cannot assert the
        # byte-for-byte slice contents and is skipped rather than silently weakened.
        if args.reference_window is None:
            print('SKIP platform byte-range suite: needs --reference-window '
                  '(64 KiB LUN2 window, the same file Trial721 used)')
        else:
            if hashlib.sha256(args.reference_window.read_bytes()).hexdigest() != '03628d70602e4f21e36ace76f5c4e27ed1b10e50ae690704035799ea34e4e82b':
                raise ValueError('Expected the frozen LUN2 reference window')
            for flags in (['-O2'],
                          ['-O1', '-g', '-fsanitize=address,undefined',
                           '-fno-sanitize-recover=all']):
                exe = work / 'platform'
                subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                '-Wno-unused-function', '-DUS_HOST_TEST=1',
                                *flags, '-I', 'tests', '-I', 'source/ufs_twoblock',
                                '-I', 'source/ufs_twoblock/codec',
                                '-I', 'source/ufs_twoblock/engine',
                                '-I', 'source/ufs_twoblock/support',
                                '-I', 'source/ufs_varread',
                                'tests/test-ufs-twoblock-platform.c', '-o', str(exe)],
                               cwd=work, check=True)
                subprocess.run([str(exe), str(args.reference_window.resolve())], cwd=work, check=True)

        # Standalone suites: no reference window needed, all CRCs synthetic and computed in-test.
        for name, extra in (('codec', ['tests/test-byte-range-codec.c']),
                            ('sweep', ['tests/test-byte-range-sweep.c']),
                            ('engine', ['tests/test-ufs-two-block-engine.c'])):
            exe = work / name
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-function', '-DUS_HOST_TEST=1',
                            '-O1', '-g', '-fsanitize=address,undefined',
                            '-fno-sanitize-recover=all',
                            '-I', 'tests', '-I', 'source/ufs_twoblock',
                            '-I', 'source/ufs_twoblock/codec',
                            '-I', 'source/ufs_twoblock/engine',
                            '-I', 'source/ufs_twoblock/support',
                            '-I', 'source/ufs_varread', *extra, '-o', str(exe)],
                           cwd=work, check=True)
            subprocess.run([str(exe)], cwd=work, check=True)

        print('all suites passed')


if __name__ == '__main__':
    main()
