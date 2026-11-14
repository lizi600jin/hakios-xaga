#!/usr/bin/env python3
"""Build controlled images offline. This script has no device/flash operations."""
from pathlib import Path
import argparse, subprocess, struct, hashlib, json, gzip
HERE = Path(__file__).resolve().parent
ROOT = next(path for path in HERE.parents if (path / 'mtk/loader/mtkloader.c').is_file())
BASE = ROOT / 'logs/headset-resume-20261114-220244/nopdc'

def digest(data):
    return hashlib.sha256(data).hexdigest()

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('kind', choices=['logging-reference', 'current-off', 'headset1'])
    ap.add_argument('--kernel', type=Path)
    ap.add_argument('--out', type=Path)
    args = ap.parse_args()
    out = args.out or HERE / args.kind
    out.mkdir(parents=True, exist_ok=True)
    base = json.loads((BASE / 'build.json').read_text())
    old = (BASE / 'payload.bin').read_bytes()
    assert digest(old) == base['payload_sha256']
    payload = old
    entries = []
    if args.kind != 'logging-reference':
        assert args.kernel, 'give the verified newly built kernel explicitly'
        kernel = args.kernel.read_bytes()
        normal = ROOT / 'xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837'
        assert kernel == normal.read_bytes(), 'the packaged kernel is not the latest linked/stripped build'
        assert b'headset: mode 2 initialised' in kernel
        assert b'_hakios_mt6895_headset_quiesce' in kernel
        pos = 8
        while old[pos:pos+16].rstrip(b'\0'):
            name = old[pos:pos+16].split(b'\0')[0].decode()
            size = struct.unpack_from('<I', old, pos+16)[0]
            end = pos + 24 + (size + 15) // 16 * 16
            raw = old[pos:end]
            if name == 'kernel':
                raw = struct.pack('<16sII', b'kernel', len(kernel), 0) + kernel + b'\0' * (-len(kernel) % 16)
            entries.append((name, raw))
            pos = end
        body = b''.join(raw for _, raw in entries) + old[pos:]
        payload = struct.pack('<II', 0x50554e58, len(body)) + body
        kernel_sha = digest(kernel)
    else:
        kernel_sha = base['kernel_sha256']
    (out / 'payload.bin').write_bytes(payload)
    extra = base['base_args'] + ' mali_nopdc=1'
    if args.kind == 'headset1':
        extra += ' headset=1'
    actual_args = 'rd=md0 -v ' + extra + ' loader_return_after=150'
    assert len(actual_args.encode()) < 608
    assert 'mali_gpueb=1' not in actual_args and 'headset=2' not in actual_args
    flags = [flag for flag in base['loader_flags'] if not flag.startswith('-DXNU_EXTRA_ARGS=')]
    flags.append('-DXNU_EXTRA_ARGS="' + extra + '"')
    commands = []
    for source, obj in [('start.S', 'start.o'), ('mtkloader.c', 'mtkloader.o')]:
        command = ['clang', *flags, '-c', str(ROOT/'mtk/loader'/source), '-o', str(out/obj)]
        commands.append(command)
        run = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        (out/(obj+'.build.log')).write_text(run.stdout)
        assert run.returncode == 0, run.stdout
    command = ['ld.lld', '-T', str(ROOT/'mtk/loader/link.ld'), '--oformat', 'binary', str(out/'start.o'), str(out/'mtkloader.o'), '-o', str(out/'loader.bin')]
    commands.append(command)
    subprocess.run(command, check=True, timeout=30)
    loader = (out/'loader.bin').read_bytes()
    assert actual_args.encode()+b'\0' in loader
    command = ['python3', str(ROOT/'mtk/tools/chainboot.py'), str(ROOT/'backup/boot_a_stock_real.img'), str(out/'loader.bin'), str(out/'boot.img'), str(out/'payload.bin'), '--partition-size', '64M']
    commands.append(command)
    run = subprocess.run(command, cwd=ROOT, check=True, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    (out/'chainboot.log').write_text(run.stdout)
    image = (out/'boot.img').read_bytes()
    size = struct.unpack_from('<I', image, 8)[0]
    packed = gzip.decompress(image[4096:4096+size])
    offset = struct.unpack_from('<Q', packed, 48)[0]
    assert packed[offset:offset+len(payload)] == payload
    sources = ['mtk/loader/mtkloader.c', 'mtk/mt6895/drivers/input/hakios.mt6895_keys.c', 'mtk/mt6895/drivers/audio/hakios.mt6895_headset.c', 'mtk/mt6895/drivers/gpu/hakios.mali610.c', 'xnu/files/osfmk/arm64/loader_return.c', 'mtk/mt6895/drivers/core/hakios.mt6895_pmif.c', 'xnu/files/bsd/sys/hakios.pmif.h', 'mtk/mt6895/drivers/core/conf', 'mtk/mt6895/drivers/input/conf', 'mtk/mt6895/drivers/audio/conf', 'mtk/mt6895/drivers/gpu/conf', 'mtk/mt6895/configs/mt6895.config', '.config']
    result = {'kind':args.kind, 'actual_loader_args':actual_args, 'argument_bytes':len(actual_args.encode()), 'loader_flags':flags, 'commands':commands, 'payload_sha256':digest(payload), 'kernel_sha256':kernel_sha, 'loader_sha256':digest(loader), 'loader_bytes':len(loader), 'image_md5':hashlib.md5(image).hexdigest(), 'image_sha256':digest(image), 'image_bytes':len(image), 'flashed':False, 'source_sha256':{name:digest((ROOT/name).read_bytes()) for name in sources}}
    result['generated_config_args'] = subprocess.check_output(['python3', str(ROOT/'tools/hakios-config.py'), 'args'], cwd=ROOT, text=True).strip()
    if args.kind == 'headset1':
        assert 'headset=1' in result['generated_config_args'].split()
        result['headset_dump_count'] = 129
        result['interpretation'] = 'Read-only SID5 baseline; VAUD18 is observed, not enabled. AB alone is not evidence of detector function if 0x1cb2 bit0 is clear. No headphones available; insertion and sound are untested.'
    if args.kind == 'logging-reference':
        result['use'] = 'offline reference with unchanged NOPDC payload; its old kernel retains legacy PMIF probing'
    (out/'build.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({key:value for key,value in result.items() if key not in ['loader_flags','commands','source_sha256']}, indent=2))
if __name__ == '__main__':
    main()
