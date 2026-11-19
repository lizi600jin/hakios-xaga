#!/usr/bin/env python3
"""Rebuild the tested Trial728 sources and replay every host test.

The driver and loader are reconstructed from the published 727 sources and the two 728 patches.
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
PREV727 = RAW / 'ufs-vfs-range-727'


def apply(work, patch):
    subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(patch)], cwd=work, check=True,
                   stdout=subprocess.DEVNULL)


def build_platform(work):
    """Run the 727 archive's own rebuild, then apply this trial's increment."""
    import runpy
    prev = runpy.run_path(str(PREV727 / 'check-platform.py'))
    prev['build_platform'](work)
    apply(work, HERE / '727-to-728-platform.patch')


def verify(work, frozen, prefixes):
    bad = []
    for rel, want in frozen.items():
        if not rel.startswith(prefixes):
            continue
        p = work / rel
        if not p.exists():
            bad.append((rel, 'missing'))
            continue
        got = hashlib.sha256(p.read_bytes()).hexdigest()
        if got != want:
            bad.append((rel, got[:16]))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference-window', type=Path)
    ap.add_argument('--libmini', type=Path,
                    help='directory holding mini.h for the userspace client build')
    ap.add_argument('--loader-base', type=Path, help='matching HakiOS project headers')
    args = ap.parse_args()
    if args.reference_window is not None:
        args.reference_window = args.reference_window.resolve()
        if hashlib.sha256(args.reference_window.read_bytes()).hexdigest() != '03628d70602e4f21e36ace76f5c4e27ed1b10e50ae690704035799ea34e4e82b':
            raise ValueError('expected the frozen LUN2 reference window')
    meta = json.loads((HERE / 'package-728.json').read_bytes())
    client = HERE / 'ufs_range_check.c'
    if hashlib.sha256(client.read_bytes()).hexdigest() != meta['client_source_sha256']:
        raise ValueError('wrong client source')

    frozen = json.loads((HERE / 'source-sha256.json').read_bytes())

    with tempfile.TemporaryDirectory(prefix='ufs-728-') as tmp:
        work = Path(tmp)
        build_platform(work)
        bad = verify(work, frozen, ('source/', 'tests/'))
        if bad:
            raise SystemExit(f'platform mismatch: {bad}')
        print('platform sources match source-sha256.json')

        if args.reference_window is None:
            print('SKIP platform suite: needs --reference-window (64 KiB LUN2 window)')
        else:
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
                subprocess.run([str(exe), str(args.reference_window)], cwd=work, check=True)

        # The userspace client's fault tests. The device program is compiled once as range_client
        # with its mini syscalls redirected to the test's stubs; the test file is compiled without
        # those redirects, because its own main must stay main. It also needs libmini's header,
        # which lives outside this archive.
        libmini = args.libmini.resolve() if args.libmini else None
        if libmini is None or args.reference_window is None:
            print('SKIP userspace client suite: requires --reference-window and --libmini')
        else:
            base = ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-incompatible-library-redeclaration', '-DUS_HOST_TEST=1',
                    '-O1', '-g', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all',
                    '-I', 'tests', '-I', str(libmini), '-I', 'source/ufs_twoblock',
                    '-I', 'source/ufs_varread']
            redirect = ['-Dmain=range_client', '-Dopen=range_open', '-Dpread=range_pread',
                        '-Dclose=range_close', '-Dgetpid=range_pid', '-Derrno=range_errno']
            subprocess.run(['clang', *base, *redirect, '-c',
                            str(client), '-o', 'rc.o'],
                           cwd=work, check=True)
            subprocess.run(['clang', *base, '-c', 'tests/test-user-range-client.c',
                            '-o', 'tc.o'], cwd=work, check=True)
            subprocess.run(['clang', '-fsanitize=address,undefined', 'rc.o', 'tc.o',
                            '-o', 'client'], cwd=work, check=True)
            subprocess.run([str(work / 'client'), str(args.reference_window)], cwd=work,
                           check=True)
            print('userspace client fault tests passed')

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

        import runpy
        lwork = work / 'loader'
        lwork.mkdir()
        previous = runpy.run_path(str(PREV727 / 'check-platform.py'))
        previous['build_loader'](lwork, None)
        apply(lwork, HERE / '727-to-728-loader.patch')
        if args.loader_base is not None:
            for header in (args.loader_base / 'mtk/loader').glob('*.h'):
                target = lwork / 'mtk/loader' / header.name
                if not target.exists():
                    shutil.copyfile(header, target)
            (lwork / 'xnuloader').mkdir()
            shutil.copyfile(args.loader_base / 'xnuloader/identity.h', lwork / 'xnuloader/identity.h')
            (lwork / 'out').mkdir()
            for cmd in json.loads((HERE / 'loader-build.json').read_bytes()):
                subprocess.run(cmd, cwd=lwork, check=True)
            if hashlib.sha256((lwork / 'out/loader.bin').read_bytes()).hexdigest() != meta['loader_sha256']:
                raise ValueError('loader binary differs')
            issue = verify(lwork, frozen, ('mtk/', 'xnuloader/'))
            if issue:
                raise ValueError(f'loader sources differ: {issue}')
            print('Trial728 loader binary and source hashes match')
        else:
            for rel in ['mtk/loader/mtkloader.c', 'mtk/loader/ufs_read_contract.h']:
                if hashlib.sha256((lwork / rel).read_bytes()).hexdigest() != frozen[rel]:
                    raise ValueError(rel)
            print('Reconstructed loader delta. Full binary replay requires --loader-base.')
        print('Requested checks passed')


if __name__ == '__main__':
    main()
