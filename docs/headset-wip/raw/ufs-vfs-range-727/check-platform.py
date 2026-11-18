#!/usr/bin/env python3
"""Reconstruct the Trial727 platform and loader deltas and exercise the driver."""
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
RAW = HERE.parent
PREV726 = RAW / 'ufs-byte-range-726'
PREV725 = RAW / 'ufs-mixed-length-725'
OLDER724 = RAW / 'ufs-read-length-724'
BASE723 = RAW / 'final-ufs-varstart-source-723'


def apply(work, patch):
    subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(patch)], cwd=work, check=True,
                   stdout=subprocess.DEVNULL)


def build_platform(work):
    """723 base, then every recorded increment in order, ending at 727."""
    for folder in ('source', 'tests'):
        shutil.copytree(BASE723 / folder, work / folder)
    subprocess.run(['git', 'init', '-q'], cwd=work, check=True)
    for p in (OLDER724 / '723-to-724.patch', OLDER724 / '724-platform.patch',
              PREV725 / '724-to-725-platform.patch', PREV726 / '725-to-726-platform.patch',
              HERE / '726-to-727-platform.patch'):
        apply(work, p)


def build_loader(work, project):
    seed = RAW / 'final-ufs-varread-source-721/source/loader'
    shutil.copytree(seed, work / 'mtk/loader')
    subprocess.run(['git', 'init', '-q'], cwd=work, check=True)
    subprocess.run(['git', 'apply', '--directory=mtk', '--include=mtk/loader/*',
                    str(BASE723 / '721-to-723-integration.patch')], cwd=work, check=True)
    for patch in (OLDER724 / '724-loader.patch', PREV725 / '724-to-725-loader.patch',
                  PREV726 / '725-to-726-loader.patch', HERE / '726-to-727-loader.patch'):
        apply(work, patch)
    if project is None:
        print('Full loader binary replay requires --loader-base with the matching HakiOS board headers and xnuloader/identity.h')
        return
    for header in (project / 'mtk/loader').glob('*.h'):
        target = work / 'mtk/loader' / header.name
        if not target.exists():
            shutil.copyfile(header, target)
    (work / 'xnuloader').mkdir()
    shutil.copyfile(project / 'xnuloader/identity.h', work / 'xnuloader/identity.h')
    (work / 'out').mkdir()
    for args in json.loads((HERE / 'loader-build.json').read_bytes()):
        subprocess.run(args, cwd=work, check=True)
    result = hashlib.sha256((work / 'out/loader.bin').read_bytes()).hexdigest()
    expected = json.loads((HERE / 'package-727.json').read_bytes())['loader_sha256']
    if result != expected:
        raise ValueError(f'unexpected loader digest {result}')
    print('reconstructed loader matches the released binary')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference-window', type=Path,
                    help='64 KiB LUN2 window; the byte-range positives compare against it')
    ap.add_argument('--loader-base', type=Path,
                    help='project containing the matching mtk/loader headers and xnuloader/identity.h')
    args = ap.parse_args()

    frozen = json.loads((HERE / 'source-sha256.json').read_bytes())

    with tempfile.TemporaryDirectory(prefix='ufs-727-') as tmp:
        work = Path(tmp)
        build_platform(work)
        for rel, want in frozen.items():
            if not rel.startswith(('source/', 'tests/')):
                continue
            p = work / rel
            got = hashlib.sha256(p.read_bytes()).hexdigest()
            if got != want:
                raise SystemExit(f'Unexpected source hash for {rel}: {got}')
        print('platform sources match source-sha256.json')

        if args.reference_window is None:
            print('SKIP platform suite: needs --reference-window (64 KiB LUN2 window)')
        else:
            if hashlib.sha256(args.reference_window.read_bytes()).hexdigest() != '03628d70602e4f21e36ace76f5c4e27ed1b10e50ae690704035799ea34e4e82b':
                raise ValueError('expected the frozen LUN2 window')
            for flags in (['-O2'],
                          ['-O1', '-g', '-fsanitize=address,undefined',
                           '-fno-sanitize-recover=all']):
                exe = work / 'platform'
                subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                '-Wconversion', '-Wshadow', '-pedantic',
                                '-Wno-unused-function', '-DUS_HOST_TEST=1', *flags,
                                '-I', 'tests', '-I', 'source/ufs_twoblock',
                                '-I', 'source/ufs_twoblock/codec',
                                '-I', 'source/ufs_twoblock/engine',
                                '-I', 'source/ufs_twoblock/support',
                                '-I', 'source/ufs_varread',
                                'tests/test-ufs-twoblock-platform.c', '-o', str(exe)],
                               cwd=work, check=True)
                subprocess.run([str(exe), str(args.reference_window.resolve())], cwd=work, check=True)

        # Synthetic-data suites need no window.
        for name, src in (('codec', 'tests/test-byte-range-codec.c'),
                          ('sweep', 'tests/test-byte-range-sweep.c'),
                          ('engine', 'tests/test-ufs-two-block-engine.c')):
            exe = work / name
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-function', '-DUS_HOST_TEST=1',
                            '-O1', '-g', '-fsanitize=address,undefined',
                            '-fno-sanitize-recover=all',
                            '-I', 'tests', '-I', 'source/ufs_twoblock',
                            '-I', 'source/ufs_twoblock/codec',
                            '-I', 'source/ufs_twoblock/engine',
                            '-I', 'source/ufs_twoblock/support',
                            '-I', 'source/ufs_varread', src, '-o', str(exe)],
                           cwd=work, check=True)
            subprocess.run([str(exe)], cwd=work, check=True)

        # Loader increment, in its own subdirectory so it cannot disturb the platform tree.
        lwork = Path(tmp) / 'loader'
        lwork.mkdir()
        build_loader(lwork, args.loader_base)
        for rel, want in frozen.items():
            if not rel.startswith('mtk/'):
                continue
            got = hashlib.sha256((lwork / rel).read_bytes()).hexdigest()
            if got != want:
                raise SystemExit(f'Unexpected loader hash for {rel}: {got}')
        print('loader sources match source-sha256.json')
        print('all suites passed')


if __name__ == '__main__':
    main()
