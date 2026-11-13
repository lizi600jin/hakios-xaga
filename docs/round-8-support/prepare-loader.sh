#!/bin/bash
# Build/verify a loader and image, with no device access. Run from the source tree.
set -euo pipefail
trial=${1:?trial number required}
args=${2:?Mali arguments required}
evidence=/home/lizi/IOS/work-xaga/codex/out/round-8-support
full="cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 $args mali_trial=$trial"
source ./xnu-env.sh
DEVICE=xaga ./mtk/loader/build.sh \
  -DBOARD_NO_UART -DBOARD_NO_OVL -DBOARD_SKIP_MENU -DBOARD_NO_CONNSYS \
  -DXNU_CORES=4 -DXNU_VERBOSE=1 -DXNU_DISPLAY=0 -DXNU_WATCH_SECONDS=0 \
  -DXNU_RETURN_AFTER='"150"' \
  -DBOARD_MALI_POWER=1 -DBOARD_MALI_PREPOWER=0 \
  -DXNU_EXTRA_ARGS="\"$full\"" > "$evidence/trial-$trial-loader-build.log" 2>&1
strings -a mtk/loader/mtkloader.bin > "$evidence/trial-$trial-loader-strings.txt"
for arg in $args "mali_trial=$trial"; do
  n=$(grep -F -c -- "$arg" "$evidence/trial-$trial-loader-strings.txt" || true)
  printf '%s: %s occurrences (required 3)\n' "$arg" "$n"
  test "$n" -eq 3
done
python3 mtk/loader/build_flashed.py > "$evidence/trial-$trial-pack.log" 2>&1
python3 - "$trial" "$full" "$evidence" <<'PY'
import hashlib, json, pathlib, sys
trial, args, out = sys.argv[1:]
paths=['mtk/mt6895/drivers/gpu/hakios.mali610.c','hakios-kernel',
       'build-payload.bin','mtk/loader/mtkloader.bin','xnu-flashed-slot_a.img']
meta={'trial':int(trial),'verified_args':args,'arg_occurrences':3,'files':[]}
for name in paths:
 p=pathlib.Path(name); b=p.read_bytes()
 meta['files'].append({'path':name,'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),
                       'md5':hashlib.md5(b).hexdigest()})
pathlib.Path(out,f'trial-{trial}-build.json').write_text(json.dumps(meta,indent=2)+'\n')
print(json.dumps(meta,indent=2))
PY
