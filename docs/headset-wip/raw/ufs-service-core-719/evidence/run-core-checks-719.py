#!/usr/bin/env python3
"""New service core/lease checks, isolated output; no kernel/device activity."""
from pathlib import Path
import hashlib
import json
import subprocess

BASE=Path(__file__).resolve().parent
OUT=BASE/'host-checks/core-v1';OUT.mkdir(exist_ok=False)
records=[]
def run(stage,argv):
    p=subprocess.run(argv,capture_output=True,text=True,cwd=BASE)
    records.append(dict(stage=stage,argv=argv,rc=p.returncode,stdout=p.stdout,stderr=p.stderr))
    (OUT/'progress.json').write_text(json.dumps(records,indent=2)+'\n')
    print(stage,'rc='+str(p.returncode),p.stdout.strip(),flush=True)
    assert p.returncode==0,p.stderr
    assert not p.stderr,p.stderr
for name,source in [('service','test-ufs-service.c'),('race','test-ufs-service-race.c')]:
    for sanitizer,flags in [('asan',['-fsanitize=address,undefined']),
                            ('msan',['-fsanitize=memory','-fPIE','-pie'])]:
        out=OUT/(name+'-'+sanitizer)
        argv=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
              '-fno-omit-frame-pointer',*flags]
        if name=='race':argv+=['-pthread']
        run(name+' '+sanitizer+' build',argv+[str(BASE/'candidate'/source),'-o',str(out)])
        run(name+' '+sanitizer+' run',[str(out)])
run('freestanding ARM64',['clang','--target=aarch64-linux-gnu','-std=c11','-ffreestanding',
    '-fno-builtin','-mno-outline-atomics','-mgeneral-regs-only','-mstrict-align','-O2',
    '-Wall','-Wextra','-Werror','-c',str(BASE/'candidate/service-arm64.c'),
    '-o',str(OUT/'service-arm64.o')])
files={p.name:dict(bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
       for p in sorted((BASE/'candidate').iterdir()) if p.is_file()}
(OUT/'result.json').write_text(json.dumps(dict(status='PASS',records=records,files=files,
 limits=['Host models and finite race schedules, not a proof of all concurrent executions',
         'No platform mappings, live device or persistent block service established']),indent=2)+'\n')
