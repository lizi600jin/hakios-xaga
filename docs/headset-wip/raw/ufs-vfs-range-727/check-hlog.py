"""Validate the frozen Trial727 sequence and its transition from the verified Trial726 log."""
from pathlib import Path
import json
import re
import runpy
import sys

ROOT = Path(__file__).resolve().parent
ARCH = ROOT.parent
HLOG = runpy.run_path(str(ARCH/"ufs-mixed-length-725/parse-hlog-725.py"))
EXPECTED = [
    "bounded READ10 lun2 lba2 blocks2 bytes8192 budget1; guarded handoff required",
    "guard magic=0x554c5345 version=1 trial=727 state=0", "opened control=0x2 shared=1",
    "vfs registered path=/dev/mt6895-ufs-range budget=1", "vfs lookup error=0",
    "vfs write-open error=30 rings_before=0 rings_after=0",
    "request=1 lba=2 blocks=2 bytes=8192 admitted=1 readback_ok=1 issued=1 rings=1 response_valid=0x1fff failures=0 ocs=0 data_words=2048/2048 restored=1 copied=1 control=0x4 shared=1",
    "block=1 lba=2 reference_present=1 crc=0xd024e0c reference_crc=0xd024e0c match=1",
    "block=2 lba=3 reference_present=1 crc=0x58f10429 reference_crc=0x58f10429 match=1",
    "slice offset=12031 blocks=2 bytes=770 first=257 second=513 cap=770 crc=0x8f350378 delivered=1",
    "vfs read offset=12031 bytes=770 error=0 residual=0 crc=0x8f350378 guards=1 rings=1",
    "vfs eof error=0 residual=4 unchanged=1 rings_before=1 rings_after=1",
    "vfs final passed=1 rings=1 control=0x2 shared=1",
    "return-gate magic=0x554c5345 version=1 trial=727 state=2 allowed=1",
]
def verify(pre, post):
    require = HLOG["require"]
    require(len(pre) >= HLOG["WINDOW"] and len(post) == HLOG["WINDOW"], "capture lengths")
    old_body, old, _, _ = HLOG["record"](pre)
    body, new, reason, code = HLOG["record"](post)
    require(old == "4c0806b6864fd9f46562895ce923644fb753e61d4d6837325bf31b98391a3dae" and new != old and body != old_body, "726 baseline and fresh 727")
    require(reason == code == 0, "normal return")
    arguments = (ROOT/"arguments727.json").read_bytes()
    expected = json.loads((ROOT/"package-727.json").read_bytes())["arguments_sha256"]
    require(HLOG["sha"](arguments) == expected, "frozen arguments file")
    full = json.loads(arguments)["full_arguments"].encode()
    args = re.findall(rb'^(?:com\.apple\.xpc\.launchd\|[^\n]*?<Notice>: |[ \t]*)boot-args = ([^\r\n]*)\r?$',body,re.M)
    require(args == [full], "runtime boot arguments")
    lines = []
    for line in body.splitlines():
        if HLOG["PREFIX"].encode() in line:
            m = HLOG["STAMP"].fullmatch(line.decode("ascii"))
            require(m is not None, "malformed driver line")
            lines.append(m[1])
    require(lines == EXPECTED, "driver records")
    return {"status":"FORMAT_PASS_ONLY","pre_record_sha256":old,"post_record_sha256":new,
            "post_window_sha256":HLOG["sha"](post),"body_bytes":len(body),
            "post_body_sha256":HLOG["sha"](body),"driver_lines":lines}
if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1]).read_bytes(),Path(sys.argv[2]).read_bytes()),indent=2))
