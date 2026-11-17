#!/usr/bin/env python3
"""Package the reviewed kernel+loader into a NEW trial718 directory. Host only."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess

ROOT = Path('/home/lizi/IOS/hakios-mtk')
BASE = Path(__file__).resolve().parent
OLD = ROOT / 'logs/ufs-probe-20261117'
OUT = BASE / 'trial718'
KERNEL = BASE / 'kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837'
assert not OUT.exists(), OUT
review = json.loads((BASE / 'independent-kernel-review-718.json').read_text())
assert review['status'] == 'PASS' and hashlib.sha256(KERNEL.read_bytes()).hexdigest() == review['kernel_sha256']
loader = BASE / 'loader-candidate/loader.bin'
assert hashlib.sha256(loader.read_bytes()).hexdigest() == 'dc1d273481eeab99086726e90762c4dca4a30e7d631fddab31990349ce6e4b25'
assert json.loads((BASE / 'independent-loader-review-718.json').read_text())['status'] == 'PASS'
mk = json.loads((OLD / 'trial715/mkpayload-cmd.json').read_text())['argv']
assert mk.count('-o') == 1
mk[mk.index('-o') + 1] = str(OUT / 'payload.bin')
keys = [i for i, a in enumerate(mk) if a.startswith('kernel=')]
assert len(keys) == 1
mk[keys[0]] = 'kernel=' + str(KERNEL)
chain = json.loads((OLD / 'trial715/chainboot-cmd.json').read_text())['argv']
assert len(chain) == 8 and chain[2] == str(ROOT / 'backup/boot_a_stock_real.img')
chain[3:6] = [str(OUT / 'loader.bin'), str(OUT / 'boot.img'), str(OUT / 'payload.bin')]
assert Path(mk[mk.index('-o') + 1]).parent == OUT and Path(chain[4]).parent == OUT
OUT.mkdir()
(OUT / 'planned-commands.json').write_text(json.dumps([mk, chain], indent=2) + '\n')
shutil.copy2(loader, OUT / 'loader.bin')
for name, argv in [('mkpayload-cmd.json', mk), ('chainboot-cmd.json', chain)]:
    result = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
    record = dict(argv=argv, rc=result.returncode, stdout=result.stdout, stderr=result.stderr)
    (OUT / name).write_text(json.dumps(record, indent=2) + '\n')
    assert result.returncode == 0, record
artifacts = {p.name: dict(bytes=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest())
             for p in [OUT / 'boot.img', OUT / 'loader.bin', OUT / 'payload.bin']}
(OUT / 'artifacts.json').write_text(json.dumps(artifacts, indent=2) + '\n')
print(json.dumps(artifacts, indent=2))
