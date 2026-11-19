#!/usr/bin/env python3
"""Reconstruct Trial733 from the published Trial732 code and replay frozen inputs."""
import argparse
import hashlib
import json
import runpy
import shutil
import subprocess
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
RAW = HERE.parent
PREV732 = RAW / 'ufs-gpt-entries-732'

REFERENCE_SHA = '03628d70602e4f21e36ace76f5c4e27ed1b10e50ae690704035799ea34e4e82b'


def apply(work, patch):
    subprocess.run(['patch', '--batch', '--fuzz=0', '-p1', '-i', str(patch)], cwd=work,
                   check=True, stdout=subprocess.DEVNULL)


def build_platform(work):
    prev = runpy.run_path(str(PREV732 / 'check-platform.py'))
    prev['build_platform'](work)
    apply(work, HERE / '732-to-733-platform.patch')


def build_loader_tree(work):
    previous = runpy.run_path(str(PREV732 / 'check-platform.py'))
    previous['build_loader_tree'](work)
    apply(work, HERE / '732-to-733-loader.patch')


def verify(work, frozen, prefixes):
    bad = []
    for rel, want in frozen.items():
        if not rel.startswith(prefixes):
            continue
        p = work / rel
        got = hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else 'missing'
        if got != want:
            bad.append((rel, got[:16]))
    return bad


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference-window', type=Path)
    ap.add_argument('--libmini', type=Path, help='directory holding mini.h')
    ap.add_argument('--loader-base', type=Path, help='matching HakiOS loader headers')
    ap.add_argument('--backup-header', type=Path)
    ap.add_argument('--backup-array', type=Path)
    args = ap.parse_args()
    if any((args.reference_window,args.backup_header,args.backup_array)) and not all((args.reference_window,args.backup_header,args.backup_array)):
        ap.error('supply all three reference files together')
    if args.backup_header:
        args.backup_header = args.backup_header.resolve()
        args.backup_array = args.backup_array.resolve()
        for path, want in [(args.backup_header,'97b33c62b39b4806b03a3334982451e4b65110a5fe60a47a58d4e1bb71080f72'),(args.backup_array,'3313d2b916fa562d56a9ac4463dc10be8acdf273ed9e3b63bdb76bfdfbe237f7')]:
            if hashlib.sha256(path.read_bytes()).hexdigest() != want:
                raise ValueError('unexpected backup reference file')
    if args.reference_window is not None:
        args.reference_window = args.reference_window.resolve()
        got = hashlib.sha256(args.reference_window.read_bytes()).hexdigest()
        if got != REFERENCE_SHA:
            raise ValueError('expected the frozen LUN2 reference window')

    client = HERE / 'ufs_range_check.c'
    rel = json.loads((HERE / 'package-733.json').read_bytes())
    if hashlib.sha256((HERE / 'arguments733.json').read_bytes()).hexdigest() != rel['arguments_sha256']:
        raise ValueError('wrong arguments file')

    frozen = json.loads((HERE / 'source-sha256.json').read_bytes())

    with tempfile.TemporaryDirectory(prefix='ufs-733-') as tmp:
        work = Path(tmp)
        build_platform(work)
        bad = verify(work, frozen, ('source/', 'tests/', 'ufs_range_check.c'))
        if bad:
            raise SystemExit(f'platform mismatch: {bad}')
        if client.read_bytes() != (work / 'ufs_range_check.c').read_bytes():
            raise SystemExit('client source differs')
        print('platform sources match source-sha256.json')

        if args.reference_window is None:
            print('SKIP platform suite: needs --reference-window (64 KiB LUN2 window)')
        else:
            snapshot = work/'backup-window.bin'
            snapshot.write_bytes(args.backup_header.read_bytes()+args.backup_array.read_bytes())
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
                subprocess.run([str(exe), str(args.reference_window), str(snapshot)], cwd=work, check=True)

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
            # Trial733 adds a read(2) path on the same description, so its stub is redirected too.
            redirect = ['-Wno-ignored-attributes', '-Dprintf=range_printf', '-Dmain=range_client', '-Dopen=range_open', '-Dread=range_read',
                        '-Dpread=range_pread', '-Dclose=range_close', '-Dgetpid=range_pid',
                        '-Derrno=range_errno']
            subprocess.run(['clang', *base, *redirect, '-c', str(work / 'ufs_range_check.c'),
                            '-o', 'rc.o'], cwd=work, check=True)
            subprocess.run(['clang', *base, '-c', 'tests/test-user-range-client.c',
                            '-o', 'tc.o'], cwd=work, check=True)
            subprocess.run(['clang', '-fsanitize=address,undefined', 'rc.o', 'tc.o', '-lz',
                            '-o', 'client'], cwd=work, check=True)
            subprocess.run([str(work / 'client'), str(args.backup_header), str(args.backup_array)], cwd=work,
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

        lwork = Path(tmp) / 'loader'
        lwork.mkdir()
        build_loader_tree(lwork)
        if args.loader_base is not None:
            for header in (args.loader_base / 'mtk/loader').glob('*.h'):
                target = lwork / 'mtk/loader' / header.name
                if not target.exists():
                    shutil.copyfile(header, target)
            (lwork / 'xnuloader').mkdir(exist_ok=True)
            shutil.copyfile(args.loader_base / 'xnuloader/identity.h',
                            lwork / 'xnuloader/identity.h')
            (lwork / 'out').mkdir(exist_ok=True)
            for cmd in json.loads((HERE / 'loader-build.json').read_bytes()):
                subprocess.run(cmd, cwd=lwork, check=True)
            digest = hashlib.sha256((lwork / 'out/loader.bin').read_bytes()).hexdigest()
            if digest != json.loads((HERE / 'package-733.json').read_bytes())['loader_sha256']:
                raise ValueError('loader binary differs')
            issue = verify(lwork, frozen, ('mtk/', 'xnuloader/'))
            if issue:
                raise ValueError(f'loader sources differ: {issue}')
            print('Trial733 loader binary and sources match')
        else:
            for rel_name in ('mtk/loader/mtkloader.c', 'mtk/loader/ufs_read_contract.h'):
                got = hashlib.sha256((lwork / rel_name).read_bytes()).hexdigest()
                if got != frozen.get(rel_name):
                    raise ValueError(f'loader source differs: {rel_name}')
            print('Reconstructed loader delta. Full binary replay requires --loader-base.')
        print('Requested checks passed')


if __name__ == '__main__':
    main()
