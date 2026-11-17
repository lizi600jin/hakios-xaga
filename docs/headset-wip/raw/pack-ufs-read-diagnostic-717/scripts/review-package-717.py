#!/usr/bin/env python3
"""Independently validate a trial717 image; never opens a device or changes inputs."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def xnup(data):
    require(len(data) >= 32, 'short XNUP')
    magic, size = struct.unpack_from('<II', data)
    require(magic == 0x50554e58 and size == len(data) - 8, 'XNUP header/size')
    off, entries, terminated = 8, {}, False
    while off + 24 <= len(data):
        header = data[off:off + 24]
        name, length, reserved = struct.unpack('<16sII', header)
        off += 24
        if name == bytes(16):
            require(header == bytes(24), 'nonzero terminator')
            terminated = True
            break
        name = name.split(b'\0', 1)[0].decode('ascii')
        require(name not in entries and reserved == 0, 'duplicate/reserved entry')
        end = off + length
        padded = off + ((length + 15) & ~15)
        require(padded <= len(data), 'entry outside payload')
        require(data[end:padded] == bytes(padded - end), 'nonzero entry padding')
        entries[name] = data[off:end]
        off = padded
    require(terminated, 'missing 24-byte XNUP terminator')
    require(off == len(data), 'trailing payload bytes')
    require(list(entries) == ['kernel', 'ramdisk', 'kernel.ver'], 'unexpected entries/order')
    return entries


def boot(data):
    require(data[:8] == b'ANDROID!', 'not Android boot image')
    version = struct.unpack_from('<I', data, 40)[0]
    require(version == 4, 'expected stock v4 header')
    require(struct.unpack_from('<I', data, 20)[0] == 1584, 'v4 header size')
    kernel_size, ramdisk_size = struct.unpack_from('<II', data, 8)
    sig_size = struct.unpack_from('<I', data, 1580)[0]
    result, off = {'header': data[:4096]}, 4096
    for name, size in [('kernel', kernel_size), ('ramdisk', ramdisk_size), ('sig', sig_size)]:
        require(off + size <= len(data), 'truncated boot ' + name)
        result[name] = data[off:off + size]
        off += (size + 4095) & ~4095
    require(off <= len(data), 'truncated boot padding')
    result['end'] = off
    return result


def review(root, trial, revision):
    expected_kernel = json.loads((Path(__file__).resolve().parent / 'independent-kernel-review-717.json').read_text())['kernel_sha256']
    kernel = (root / 'logs/ufs-read-diag-20261117/kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837').read_bytes()
    require(sha(kernel) == expected_kernel, 'host kernel hash')
    reference = (root / 'logs/resume-sol-20261114-231152/trial-709-mt6685-read/payload.bin').read_bytes()
    require(sha(reference) == '5ac894d6b6debbd608ac10750477ab0395e301af9819a8f7b54f32793f978539', 'reference payload hash')
    previous = xnup(reference)
    payload = (revision / 'payload.bin').read_bytes()
    entries = xnup(payload)
    require(entries['kernel'] == kernel, 'nested kernel differs')
    for name in ('ramdisk', 'kernel.ver'):
        require(entries[name] == previous[name], name + ' differs from reference')
    canonical_size = 8 + 24 + sum(24 + ((len(data) + 15) & ~15) for data in entries.values())
    require(len(payload) == canonical_size, 'canonical payload size')

    stock_data = (root / 'backup/boot_a_stock_real.img').read_bytes()
    require(sha(stock_data) == 'c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc', 'stock image hash')
    image_data = (revision / 'boot.img').read_bytes()
    require(len(image_data) < 64 << 20, 'image exceeds boot partition')
    stock, image = boot(stock_data), boot(image_data)
    expected_header = bytearray(stock['header'])
    struct.pack_into('<I', expected_header, 8, len(image['kernel']))
    require(image['header'] == expected_header, 'unexpected boot header mutation')
    for name in ('ramdisk', 'sig'):
        require(image[name] == stock[name], 'stock ' + name + ' mutated')
    require(image['end'] == len(image_data), 'unexpected image trailer')
    linux = gzip.decompress(stock['kernel'])
    require(sha(linux) == '12d255cdd09fb5b9fb5cf9608e6b060a5b2dabc109fd4344b071a3dbdc0cf391', 'Android Image hash')
    blob = gzip.decompress(image['kernel'])
    loader = (trial / 'loader.bin').read_bytes()
    require(loader[0x38:0x3c] == b'ARM\x64', 'loader architecture')
    require(len(loader) <= 0x40000, 'loader exceeds chain slot')
    payload_off = 0x40000 + ((len(linux) + 15) & ~15)
    require(blob[0x40000:0x40000 + len(linux)] == linux, 'Android Image changed')
    require(blob[payload_off:] == payload, 'nested payload differs')
    require(blob[len(loader):0x40000] == bytes(0x40000 - len(loader)), 'loader padding')
    require(blob[0x40000 + len(linux):payload_off] == bytes(payload_off - 0x40000 - len(linux)), 'Android padding')
    patched = bytearray(loader)
    total = max(0x40000 + struct.unpack_from('<Q', linux, 0x10)[0], payload_off + len(payload))
    struct.pack_into('<Q', patched, 0x10, (total + 0xfffff) & ~0xfffff)
    struct.pack_into('<Q', patched, 0x28, len(linux))
    struct.pack_into('<Q', patched, 0x30, payload_off)
    require(blob[:len(loader)] == patched, 'loader changed beyond documented header patches')

    parameters = json.loads((Path(__file__).resolve().parent / 'arguments717.json').read_text())
    base_args = parameters['base_arguments']
    require(len(base_args.encode()) == 168, 'base argument length')
    require(base_args.encode() + b'\0' in loader, 'exact terminated base arguments absent')
    require('mali_trial=717' in base_args and 'mtk_ufs_read=1' in base_args and 'headset=0' in base_args, 'trial behavior')
    keys_and_values = dict(x.split('=', 1) for x in base_args.split() if '=' in x)
    require(not any(k.startswith('hp_') for k in keys_and_values), 'headset output arguments present')
    require(keys_and_values.get('mali') == '0', 'GPU worker enabled')
    require({k for k in keys_and_values if k.startswith('mali')} == {'mali', 'mali_trial'}, 'unexpected GPU experiment controls')
    require(keys_and_values.get('cpus') == '4' and keys_and_values.get('loader_return_after') == '150', 'CPU/return baseline changed')
    require(parameters['device_execution_authorized'] is False, 'host-only hold missing')
    require(not any(k in keys_and_values for k in ['cpufreq', 'ddr', 'gpueb', 'mtk_cpu_probe', 'mtk_ufs_probe']), 'unexpected controller arguments')
    version = entries['kernel.ver']
    require(version.endswith(b'\0') and len(version.split(b'\0')) == 2, 'kernel.ver termination')
    version = re.split(b'[ \0]', version, maxsplit=1)[0]
    full_args = base_args.encode() + b' hakios_kver=' + version
    require(len(full_args) < 608 and len(full_args) + 1 <= 608, 'full boot args overflow')
    require(full_args.decode() == parameters['full_arguments'], 'runtime argument metadata mismatch')
    require(len(full_args) == 228 and len(full_args) + 1 == 229, 'runtime argument lengths')
    require(loader.count(base_args.encode() + b'\0') == 1, 'base argument body is not unique')
    keys = [arg.split('=', 1)[0] for arg in full_args.decode().split() if '=' in arg]
    require(len(keys) == len(set(keys)), 'duplicate behavior argument')

    return {
        'status': 'PASS', 'image': str(revision / 'boot.img'),
        'image_bytes': len(image_data), 'image_sha256': sha(image_data),
        'image_md5': hashlib.md5(image_data).hexdigest(),
        'loader_bytes': len(loader), 'loader_sha256': sha(loader),
        'payload_bytes': len(payload), 'payload_sha256': sha(payload),
        'kernel_sha256': expected_kernel,
        'entries': {name: {'bytes': len(data), 'sha256': sha(data)} for name, data in entries.items()},
        'base_args_bytes': len(base_args), 'full_args_bytes': len(full_args),
        'full_arguments': full_args.decode(), 'chain_blob_bytes': len(blob),
        'limitations': ['Host structural/provenance checks only; no hardware or electrical result']
    }


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('revision', type=Path)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[2]
    trial = Path(__file__).resolve().parent / 'trial717'
    try:
        result = review(root, trial, args.revision.resolve())
    except (OSError, ValueError, KeyError, struct.error) as error:
        result = {'status': 'REJECTED', 'revision': str(args.revision), 'error': str(error)}
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(result, ensure_ascii=False))
    raise SystemExit(0 if result['status'] == 'PASS' else 1)
