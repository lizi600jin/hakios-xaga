"""Check Trial735's fresh HLOG and correlate userspace output with the UIO caller."""
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

def expected(pid, write_error):
    user = [
        APP + f'trial=735 pid={pid} write_error={write_error}',
        APP + f'block pid={pid} bytes=4096 errno=0 guards=1',
        APP + f'header pid={pid} size=92 crc=0xba3342fe self=31240191 alternate=1 first=34 last=31240183',
        APP + f'layout pid={pid} entries_lba=31240184 count=128 stride=128 blocks=4',
        APP + f'array pid={pid} bytes=16384 crc=0xce39daa8 stored=0xce39daa8 guards=1',
        APP + f'entries pid={pid} used=88 unused=40 reversed=0 before=1 after=0 overlaps=0 zero_ids=0 duplicates=0 consistent=0',
        APP + f'partition pid={pid} bytes=4096 errno=0 guards=1',
        APP + f'nxsb pid={pid} type=1 block_size=4096 blocks=608448 checksum_ok=1 valid=1',
        APP + f'metadata pid={pid} bytes=20480 guards=1',
        APP + f'volume pid={pid} checkpoint=1 omap=2394 oid=1028 xid=1 index=0 magic=APSB checksum_ok=1',
        APP + f'eof pid={pid} bytes=0 unchanged=1',
        APP + f'final pid={pid} passed=1 close=0',
    ]
    driver = [
        DEV + 'bounded READ10 lun2 lba31240191 blocks1 bytes4096 budget8; guarded handoff required',
        DEV + 'guard magic=0x554c5345 version=1 trial=735 state=0',
        DEV + 'opened control=0x2 shared=1',
        DEV + 'snapshot registered path=/dev/mt6895-ufs-range bytes=45056 budget=8',
    ]
    requests = [
        (1, 31240191, [0x4b13d623], 0x4b13d623),
        (2, 31240184, [0x0d024e0c, 0x58f10429], 0xc6984388),
        (3, 31240186, [0xa0d23c1c, 0xc71c0011], 0xfbe507f5),
        (4, 30580480, [0x787ea9ea], 0x787ea9ea),
        (5, 30580481, [0x14f752f0, 0x787ea9ea], 0xe5e8dadb),
        (6, 30582874, [0x3cb0c204], 0x3cb0c204),
        (7, 30582875, [0xb708a29a], 0xb708a29a),
        (8, 30582876, [0xd85f81f9], 0xd85f81f9),
    ]
    for ordinal, lba, checksums, crc in requests:
        if ordinal == 4:
            driver.append(DEV + 'partition xnusys first=30580480 last=31236087 resolved=1')
        n = len(checksums)
        driver.append(DEV + f'request={ordinal} lba={lba} blocks={n} bytes={4096*n} admitted=1 readback_ok=1 issued=1 rings=1 response_valid=0x1fff failures=0 ocs=0 data_words={1024*n}/{1024*n} restored=1 copied=1 control=0x4 shared=1')
        for i, block in enumerate(checksums):
            driver.append(DEV + f'block={i+1} lba={lba+i} reference_present=1 crc={block:#x} reference_crc={block:#x} match=1')
        driver.append(DEV + f'slice offset={4096*lba} blocks={n} bytes={4096*n} first=4096 second={4096*(n-1)} cap={4096*n} crc={crc:#x} delivered=1')
    for offset, size, residual in [(0,4096,0),(4096,16384,0),(20480,4096,0),(24576,20480,0),(45056,1,1)]:
        driver.append(DEV + f'userspace read pid={pid} offset={offset} requested={size} error=0 residual={residual} rings=8')
    driver.append(DEV + 'return-gate magic=0x554c5345 version=1 trial=735 state=2 allowed=1')
    return user + driver


def verify(pre, post):
    CHECK(len(pre) >= TOOLS['WINDOW'] and len(post) == TOOLS['WINDOW'], 'capture lengths')
    pin = json.loads((ROOT/'baseline-735.json').read_bytes())
    oldbody, old, old_reason, old_code = TOOLS['record'](pre)
    CHECK(len(pre) == pin['capture_bytes'] and SHA(pre) == pin['capture_sha256'] and
          SHA(pre[:TOOLS['WINDOW']]) == pin['window_sha256'] and old == pin['record_sha256']
          and (len(oldbody), old_reason, old_code) == (pin['body_bytes'], pin['reason'], pin['code']),
          'reviewed preflash baseline')
    body, new, reason, code = TOOLS['record'](post)
    CHECK(new != old and oldbody != body and reason == code == 0, 'fresh record and normal return')
    argfile = (ROOT/'arguments735.json').read_bytes()
    CHECK(SHA(argfile) == json.loads((ROOT/'package-735.json').read_bytes())['arguments_sha256'], 'argument file')
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
    hits = [m for s in user if (m := re.fullmatch(r'ufs-range-user: trial=735 pid=([0-9]+) write_error=(13|30)', s))]
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
