#!/usr/bin/env python3
"""Pure host engine checks in fresh output; no kernel build, loader or device operations."""
from pathlib import Path
import hashlib
import json
import subprocess

BASE=Path(__file__).resolve().parent
OUT=BASE/'host-checks/engine-v1'
OUT.mkdir(parents=True,exist_ok=False)
records=[]
def run(stage,argv):
    p=subprocess.run(argv,cwd=BASE,capture_output=True,text=True)
    records.append(dict(stage=stage,argv=argv,rc=p.returncode,stdout=p.stdout,stderr=p.stderr))
    (OUT/'progress.json').write_text(json.dumps(records,indent=2)+'\n')
    print(stage,'rc='+str(p.returncode),p.stdout.strip(),flush=True)
    assert p.returncode==0 and not p.stderr,(p.returncode,p.stderr)
for sanitizer,flags in [('asan',['-fsanitize=address,undefined']),
                        ('msan',['-fsanitize=memory','-fPIE','-pie'])]:
    binary=OUT/('engine-'+sanitizer)
    run(sanitizer+' build',['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
        '-fno-omit-frame-pointer',*flags,str(BASE/'candidate/test-ufs-capacity-engine.c'),'-o',str(binary)])
    run(sanitizer+' run',[str(binary)])
run('freestanding ARM64',['clang','--target=aarch64-linux-gnu','-std=c11','-ffreestanding',
    '-fno-builtin','-mno-outline-atomics','-mgeneral-regs-only','-mstrict-align','-O2',
    '-Wall','-Wextra','-Werror','-c',str(BASE/'candidate/capacity-engine-arm64.c'),
    '-o',str(OUT/'capacity-engine-arm64.o')])
files={str(p.relative_to(BASE)):dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
       for p in sorted((BASE/'candidate').iterdir()) if p.is_file()}
report=dict(status='PASS',records=records,files=files,
            limits=['Synthetic transaction/fault model only; no real MMIO/DMA/provider/VFS evidence',
                    'Freestanding ARM64 compilation is not a real XNU-header or kernel build'])
(OUT/'result.json').write_text(json.dumps(report,indent=2)+'\n')
