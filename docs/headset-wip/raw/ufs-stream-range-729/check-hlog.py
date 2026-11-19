"""Check Trial729's fresh HLOG and correlate userspace output with the UIO caller."""
from pathlib import Path
import json
import re
import runpy
import sys

HERE = Path(__file__).resolve().parent
# In the archive the arguments file sits beside this script.
ROOT = HERE if (HERE / 'arguments729.json').exists() else HERE.parent
TOOLS = runpy.run_path(str(HERE.parent/'ufs-mixed-length-725/parse-hlog-725.py'))
CHECK, SHA = TOOLS['require'], TOOLS['sha']
DEV = TOOLS['PREFIX']
APP = 'ufs-range-user: '
STAMP = re.compile(r'^(?:\[ *[0-9]+\.[0-9]{6}\]: )?')
PRE = '7a9c569b915744c96066cdc2478997f9e228ef7dfd702368277e298c74e138e2'

def expected(pid, write_error):
    driver = [
        DEV + 'bounded READ10 lun2 lba2 blocks2 bytes8192 budget1; guarded handoff required',
        DEV + 'guard magic=0x554c5345 version=1 trial=729 state=0',
        DEV + 'opened control=0x2 shared=1',
        DEV + 'snapshot registered path=/dev/mt6895-ufs-range bytes=770 budget=1',
        DEV + f'userspace read pid={pid} offset=770 requested=1 error=0 residual=1 rings=0',
        DEV + 'request=1 lba=2 blocks=2 bytes=8192 admitted=1 readback_ok=1 issued=1 rings=1 response_valid=0x1fff failures=0 ocs=0 data_words=2048/2048 restored=1 copied=1 control=0x4 shared=1',
        DEV + 'block=1 lba=2 reference_present=1 crc=0xd024e0c reference_crc=0xd024e0c match=1',
        DEV + 'block=2 lba=3 reference_present=1 crc=0x58f10429 reference_crc=0x58f10429 match=1',
        DEV + 'slice offset=12031 blocks=2 bytes=770 first=257 second=513 cap=770 crc=0x8f350378 delivered=1',
    ]
    for offset, asked, residual in [(0,256,0),(250,19,0),(256,259,0),(515,512,257),(770,1,1)]:
        driver.append(DEV + f'userspace read pid={pid} offset={offset} requested={asked} error=0 residual={residual} rings=1')
    driver.append(DEV + 'return-gate magic=0x554c5345 version=1 trial=729 state=2 allowed=1')
    user = [APP + f'trial=729 pid={pid} write_error={write_error}']
    for name, count, crc in [('cold-eof',0,0),('head',256,0x9de8fce7),('overlap',19,0x678d8cf7),
                             ('middle',259,0xa6a8c96b),('tail',255,0xc275a37c),('eof',0,0)]:
        user.append(APP + f'{name} pid={pid} bytes={count} errno=0 crc=0x{crc:x} guards=1')
    return user + [APP + f'final pid={pid} passed=1 close=0'] + driver

def verify(pre, post):
    CHECK(len(pre) >= TOOLS['WINDOW'] and len(post) == TOOLS['WINDOW'], 'capture lengths')
    oldbody, old, _, _ = TOOLS['record'](pre)
    body, new, reason, code = TOOLS['record'](post)
    CHECK(old == PRE and new != old and oldbody != body and reason == code == 0, 'fresh record and normal return')
    argfile = (ROOT/'arguments729.json').read_bytes()
    CHECK(SHA(argfile) == json.loads((ROOT/'arguments-729.sha256.json').read_bytes())['sha256'], 'argument file')
    CHECK(SHA(argfile) == json.loads((ROOT/'package-729.json').read_bytes())['arguments_sha256'], 'frozen package arguments')
    full = json.loads(argfile)['full_arguments'].encode()
    matched = re.findall(rb'^(?:com\.apple\.xpc\.launchd\|[^\n]*?<Notice>: |[ \t]*)boot-args = ([^\r\n]*)\r?$', body, re.M)
    CHECK(matched == [full], 'runtime arguments')
    separator = b'\n--- kernel message buffer ---\n'
    CHECK(body.count(separator) == 1, 'unique kernel message buffer boundary')
    console, msgbuf = body.split(separator)
    def tagged(section):
        return [line.decode('ascii') for line in section.splitlines()
                if DEV.encode() in line or APP.encode() in line]
    user = tagged(console)
    hits = [m for s in user if (m := re.fullmatch(r'ufs-range-user: trial=729 pid=([0-9]+) write_error=(13|30)', s))]
    CHECK(len(hits) == 1, 'unique userspace invocation and denied write-open')
    pid, write_error = map(int, hits[0].groups())
    spec = expected(pid, write_error)
    driver = [s for s in spec if s.startswith(DEV)]
    CHECK(pid > 1 and user == [s for s in spec if s.startswith(APP)], 'console sequence and PID')
    kernel = tagged(msgbuf)
    CHECK(len(kernel) == len(driver) and all(re.fullmatch(r'\[ *[0-9]+\.[0-9]{6}\]: ' + re.escape(want), got)
          for want, got in zip(driver[:-1], kernel[:-1])), 'timestamped kernel sequence and PID')
    CHECK(kernel[-1] == driver[-1] and msgbuf.endswith(b'\n' + driver[-1].encode() + b'\n'), 'final loader return gate')
    lines = user + driver
    return {'status':'FORMAT_PASS_ONLY','pre_record_sha256':old,'post_record_sha256':new,'body_bytes':len(body),
            'body_sha256':SHA(body),'post_window_sha256':SHA(post),'lines':lines}

if __name__ == '__main__':
    print(json.dumps(verify(Path(sys.argv[1]).read_bytes(), Path(sys.argv[2]).read_bytes()), indent=2))
