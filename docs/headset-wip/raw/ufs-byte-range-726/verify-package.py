#!/usr/bin/env python3
"""Unpack the released Android v4 image and check the manifest and the full boot arguments."""
import argparse
import gzip
import json
import runpy
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOLS = runpy.run_path(str(HERE.parent / 'ufs-mixed-length-725/verify-package.py'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('boot_img', type=Path)
    parser.add_argument('--loader', type=Path)
    opts = parser.parse_args()
    report = json.loads((HERE / 'package-726.json').read_bytes())
    package = report['package']
    check, sha = TOOLS['check'], TOOLS['sha']
    boot = opts.boot_img.read_bytes()
    kernel_section, _, end = TOOLS['boot_parts'](boot)
    check(end == len(boot) == package['boot_bytes'] and sha(boot) == package['boot_sha256'], 'boot image identity')
    image = gzip.decompress(kernel_section)
    offset = struct.unpack_from('<Q', image, 0x30)[0]
    payload = image[offset:]
    check(sha(payload) == package['payload_sha256'], 'embedded payload')
    items = TOOLS['payload_entries'](payload)
    check(sha(items['kernel']) == package['kernel_sha256'] and len(items['kernel']) == package['kernel_bytes'], 'embedded kernel')
    check(sha(items['ramdisk']) == TOOLS['RAMDISK_SHA'], 'embedded ramdisk')
    base, = [s for s in image[:0x40000].split(b'\0') if s.startswith(b'rd=md0 ')]
    version = items['kernel.ver']
    check(version.endswith(b'\0') and version.count(b'\0') == 1, 'kernel version')
    full = base + b' hakios_kver=' + version[:-1]
    raw_args = (HERE / 'arguments726.json').read_bytes()
    args = json.loads(raw_args)
    check(sha(raw_args) == report['arguments']['sha256'] and
          base == args['base_arguments'].encode() and full == args['full_arguments'].encode() and
          len(full)+1 == args['nul_inclusive_bytes'], 'exact full arguments')
    if opts.loader:
        plain = opts.loader.read_bytes()
        check(len(plain) == package['loader_bytes'] and sha(plain) == package['loader_sha256'], 'loader identity')
        diff = [pos for pos, byte in enumerate(plain) if byte != image[pos]]
        check(diff == package['loader_backfill_offsets'], 'loader backfills')
    print('Trial726 package PASS', sha(boot))


if __name__ == '__main__':
    main()
