#!/usr/bin/env python3
"""Rebuild and verify the reviewed reader without using any device interface."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

OUT = Path(__file__).resolve().parent
commands = [
    ['clang', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
     '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
     'reader_core.c', 'test_reader.c', '-o', 'test_reader'],
    ['clang', '--target=aarch64-linux-gnu', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
     '-ffreestanding', '-nostdlib', '-static', '-fno-stack-protector', '-fno-pic',
     '-Wl,-e,_start', '-Wl,--build-id=none', '-fuse-ld=lld',
     'reader_core.c', 'reader_start.c', '-o', 'regmap-reader.aarch64'],
    [str(OUT / 'test_reader')],
    ['python3', 'test-reader-elf.py'],
]
manifest = {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in [OUT / n for n in ('reader_core.h', 'reader_core.c', 'reader_start.c',
                                        'test_reader.c', 'test-reader-elf.py', 'replay-checks.py')]}
record = dict(status='RUNNING', inputs=manifest, commands=[])
(OUT / 'replay-result.json').write_text(json.dumps(record, indent=2) + '\n')
for i, cmd in enumerate(commands):
    result = subprocess.run(cmd, cwd=OUT, text=True, capture_output=True)
    log = result.stdout + result.stderr
    (OUT / f'replay-{i}.log').write_text(log)
    record['commands'].append(dict(argv=cmd, rc=result.returncode))
    assert result.returncode == 0, (cmd, result.returncode, log[-3000:])
    if i == 2:
        assert not re.search(r'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:', log)
        record['host_summary'] = log.splitlines()[-1]
    if i == 3:
        record['elf_summary'] = log.strip()
verify = subprocess.run(['readelf', '-hlWs', 'regmap-reader.aarch64'], cwd=OUT,
                        capture_output=True, text=True, check=True).stdout
dynamic = subprocess.run(['readelf', '-dW', 'regmap-reader.aarch64'], cwd=OUT,
                         capture_output=True, text=True, check=True).stdout
assert 'AArch64' in verify and 'EXEC' in verify and 'INTERP' not in verify
assert 'There is no dynamic section' in dynamic
for line in verify.splitlines():
    if re.search(r'\bUND\b', line):
        assert line.split()[0] == '0:', line  # Only the mandatory ELF null symbol.
(OUT / 'reviewed-elf-readelf.txt').write_text(verify + dynamic)
assert manifest == {name: hashlib.sha256((OUT / name).read_bytes()).hexdigest() for name in manifest}
record.update(status='PASS', reader_bytes=(OUT / 'regmap-reader.aarch64').stat().st_size,
              reader_sha256=hashlib.sha256((OUT / 'regmap-reader.aarch64').read_bytes()).hexdigest())
(OUT / 'replay-result.json').write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps({k: record[k] for k in ('status', 'host_summary', 'elf_summary',
                                        'reader_bytes', 'reader_sha256')}, indent=2))
