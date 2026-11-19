"""Check Trial730's fresh HLOG and correlate userspace output with the UIO caller."""
from pathlib import Path
import json
import re
import runpy
import sys

ROOT = Path(__file__).resolve().parent
TOOLS = runpy.run_path(str(ROOT.parent/'ufs-mixed-length-725/parse-hlog-725.py'))
CHECK, SHA = TOOLS['require'], TOOLS['sha']
DEV = TOOLS['PREFIX']
APP = 'ufs-range-user: '
STAMP = re.compile(r'^(?:\[ *[0-9]+\.[0-9]{6}\]: )?')
PRE = '1bba34d6e79ae16bed65c9205bd56b85e28e02036bc8b2ed6dd15eec2e93b3f5'

def expected(pid, write_error):
    user = [
        APP + f'trial=730 pid={pid} write_error={write_error}',
        APP + f'block pid={pid} bytes=4096 errno=0 guards=1',
        APP + f'header pid={pid} size=92 crc=0x7fdb9634 self=1 alternate=31240191 first=34 last=31240183',
        APP + f'layout pid={pid} entries_lba=2 count=128 stride=128 blocks=4',
        APP + f'eof pid={pid} bytes=0 unchanged=1',
        APP + f'final pid={pid} passed=1 close=0',
    ]
    driver = [
        DEV + 'bounded READ10 lun2 lba1 blocks1 bytes4096 budget1; guarded handoff required',
        DEV + 'guard magic=0x554c5345 version=1 trial=730 state=0',
        DEV + 'opened control=0x2 shared=1',
        DEV + 'snapshot registered path=/dev/mt6895-ufs-range bytes=4096 budget=1',
        DEV + 'request=1 lba=1 blocks=1 bytes=4096 admitted=1 readback_ok=1 issued=1 rings=1 response_valid=0x1fff failures=0 ocs=0 data_words=1024/1024 restored=1 copied=1 control=0x4 shared=1',
        DEV + 'block=1 lba=1 reference_present=1 crc=0x28a2836c reference_crc=0x28a2836c match=1',
        DEV + 'slice offset=4096 blocks=1 bytes=4096 first=4096 second=0 cap=4096 crc=0x28a2836c delivered=1',
        DEV + f'userspace read pid={pid} offset=0 requested=4096 error=0 residual=0 rings=1',
        DEV + f'userspace read pid={pid} offset=4096 requested=1 error=0 residual=1 rings=1',
        DEV + 'return-gate magic=0x554c5345 version=1 trial=730 state=2 allowed=1',
    ]
    return user + driver


def verify(pre, post):
    CHECK(len(pre) >= TOOLS['WINDOW'] and len(post) == TOOLS['WINDOW'], 'capture lengths')
    oldbody, old, _, _ = TOOLS['record'](pre)
    body, new, reason, code = TOOLS['record'](post)
    CHECK(old == PRE and new != old and oldbody != body and reason == code == 0, 'fresh record and normal return')
    argfile = (ROOT/'arguments730.json').read_bytes()
    CHECK(SHA(argfile) == json.loads((ROOT/'package-730.json').read_bytes())['arguments_sha256'], 'argument file')
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
    hits = [m for s in user if (m := re.fullmatch(r'ufs-range-user: trial=730 pid=([0-9]+) write_error=(13|30)', s))]
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
