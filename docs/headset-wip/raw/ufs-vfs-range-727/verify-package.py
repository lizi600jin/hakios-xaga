#!/usr/bin/env python3
"""Verify the released image, the three XNUP entries and the frozen loader arguments."""
import argparse
import gzip
import json
from pathlib import Path
import runpy
import struct

HERE = Path(__file__).resolve().parent
tool = runpy.run_path(str(HERE.parent / 'ufs-mixed-length-725/verify-package.py'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    args = parser.parse_args()
    manifest = json.loads((HERE / 'package-727.json').read_bytes())
    check, sha = tool['check'], tool['sha']
    boot = args.image.read_bytes()
    kernel, _, end = tool['boot_parts'](boot)
    check(len(boot) == end == manifest['boot_bytes'] and sha(boot) == manifest['boot_sha256'], 'boot identity')
    image = gzip.decompress(kernel)
    offset = struct.unpack_from('<Q', image, 0x30)[0]
    payload = image[offset:]
    check(sha(payload) == manifest['payload_sha256'], 'payload')
    entries = tool['payload_entries'](payload)
    for key in ('kernel', 'ramdisk'):
        check(sha(entries[key]) == manifest[key + '_sha256'], key)
    base, = [s for s in image[:0x40000].split(b'\0') if s.startswith(b'rd=md0 ')]
    version = entries['kernel.ver']
    check(version.count(b'\0') == 1 and version.endswith(b'\0'), 'kernel version')
    args_file = (HERE / 'arguments727.json').read_bytes()
    check(sha(args_file) == manifest['arguments_sha256'], 'arguments file')
    frozen = json.loads(args_file)
    full = base + b' hakios_kver=' + version[:-1]
    check(base == frozen['base_arguments'].encode() and full == frozen['full_arguments'].encode() and
          len(full)+1 == frozen['nul_inclusive_bytes'] <= 608, 'packaged boot arguments')
    print('Trial727 package PASS', sha(boot))


if __name__ == '__main__':
    main()
