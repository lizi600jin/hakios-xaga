#!/usr/bin/env python3
"""Rebuild the tested Trial725 driver and replay the single- and mixed-length tests."""
import argparse
import hashlib
import json
import runpy
import shutil
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PREVIOUS = HERE.parent / 'ufs-read-length-724'
BASE = HERE.parent / 'final-ufs-varstart-source-723'


def apply(work, patch):
    subprocess.run(['git', 'apply', str(patch)], cwd=work, check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference-window', type=Path)
    args = ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='ufs-725-') as tmp:
        work = Path(tmp)
        for folder in ('source', 'tests'):
            shutil.copytree(BASE/folder, work/folder)
        subprocess.run(['git', 'init', '-q'], cwd=work, check=True)
        apply(work, PREVIOUS/'723-to-724.patch')
        apply(work, PREVIOUS/'724-platform.patch')
        apply(work, HERE/'724-to-725-platform.patch')
        for rel, frozen in json.loads((HERE/'source-sha256.json').read_bytes()).items():
            if rel.startswith(('source/', 'tests/')) and hashlib.sha256((work/rel).read_bytes()).hexdigest() != frozen:
                raise RuntimeError(f'Unexpected source hash for {rel}')
        prepare_window = runpy.run_path(str(PREVIOUS/'check-platform.py'))['prepare_window']
        window = prepare_window(work, args.reference_window)
        for flags in (['-O2'], ['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']):
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function',
                            '-Wconversion','-Wshadow','-pedantic',*flags,'-I','tests','-I','source/ufs_twoblock',
                            'tests/test-ufs-twoblock-platform.c','-o','platform'], cwd=work, check=True)
            subprocess.run([work/'platform', window], cwd=work, check=True)


if __name__ == '__main__':
    main()
