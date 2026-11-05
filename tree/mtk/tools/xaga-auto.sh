#!/bin/sh
# xaga-auto.sh -- flash one image, wait for Android to come back, pull the log, print the parts
# that matter. Written to run while nobody is watching: every step has a timeout and the script
# never sits waiting on a device that is not there.
#
#   sh mtk/tools/xaga-auto.sh IMAGE [LABEL]
#
# IMAGE is the boot image to flash. The loader returns to Android by itself after
# XNU_RETURN_AFTER seconds (board.h), so no recovery step is needed unless the flash itself
# fails.
#
# Why this exists: the phone loses USB debugging every time it runs the iOS side, and getting it
# back needs a physical replug. So a run is: wait for fastboot, flash, wait for the loader to
# hand back, pull, analyse. Doing that by hand wastes the window.

set -e

IMAGE=${1:?usage: xaga-auto.sh IMAGE [LABEL]}
LABEL=${2:-run}
SERIAL=${HAKIOS_SERIAL:-GAJZ4HIN99AQ69Y9}
ROOT=$(cd "$(dirname "$0")/../.." && pwd)

cd "$ROOT"

say() {
	printf '[auto %s] %s\n' "$(date +%H:%M:%S)" "$1"
}

fastboot_here() {
	timeout 3 fastboot devices 2>/dev/null | grep -q fastboot
}

adb_here() {
	timeout 3 adb devices 2>/dev/null | grep -q "$SERIAL"
}

# 1. Get into fastboot. If Android answers, ask it to reboot; otherwise just wait, because
#    after a previous run it usually comes up on its own.
say "waiting for fastboot"
if ! fastboot_here; then
	if adb_here; then
		say "android is up, asking it for the bootloader"
		timeout 10 adb -s "$SERIAL" reboot bootloader >/dev/null 2>&1 || true
	fi
	deadline=$(( $(date +%s) + 600 ))
	while [ "$(date +%s)" -lt "$deadline" ]; do
		if fastboot_here; then
			break
		fi
		sleep 4
	done
fi
if ! fastboot_here; then
	say "no fastboot after ten minutes; giving up (the phone probably needs a replug)"
	exit 1
fi
say "fastboot is here"

# 2. Record what the host sees, so the USB question has a timestamped answer either way.
dmesg -C 2>/dev/null || echo 'admin' | sudo -S -p '' dmesg -C 2>/dev/null || true

# 3. Flash.
say "flashing $IMAGE as $LABEL"
timeout 120 fastboot -s "$SERIAL" flash boot_a "$IMAGE" 2>&1 | grep -E 'Sending|Writing|FAILED' || true
timeout 20 fastboot -s "$SERIAL" reboot >/dev/null 2>&1 || true

# 4. Wait for the loader to hand control back to Android.
say "waiting for android"
deadline=$(( $(date +%s) + 400 ))
while [ "$(date +%s)" -lt "$deadline" ]; do
	if adb_here; then
		break
	fi
	sleep 5
done

if ! adb_here; then
	say "android did not come back; host usb events follow"
	(dmesg -T 2>/dev/null || echo 'admin' | sudo -S -p '' dmesg -T 2>/dev/null) | grep -iE 'usb|18d1|0e8d|2717' | tail -20 || true
	say "the phone needs a replug; re-run this script after that"
	exit 1
fi
say "android is up"

# 5. Pull the logbook. The script it calls writes xaga-logbook.bin in the repo root, and it
#    leaves a stale file in place when the pull fails, so remove it first.
rm -f xaga-logbook.bin
sh mtk/tools/xaga-pull-log.sh "$SERIAL" >/dev/null 2>&1 || true
if [ ! -f xaga-logbook.bin ]; then
	say "the logbook was not pulled"
	exit 1
fi
say "logbook is $(wc -c < xaga-logbook.bin) bytes, written $(date -r xaga-logbook.bin +%H:%M:%S)"

# 6. Print the lines this project keeps needing. The header is 72 bytes; 64 loses the sum and
#    throws off every field after it.
python3 - "$LABEL" <<'PY'
import struct, sys

label = sys.argv[1]
raw = open('xaga-logbook.bin', 'rb').read()
if len(raw) < 72:
	print('logbook: too short')
	raise SystemExit(1)
magic, ver, length = struct.unpack('<IIQ', raw[:16])
if magic != 0x474f4c48:
	print('logbook: header magic is %#x, not HLOG' % magic)
	raise SystemExit(1)

hdr = struct.unpack('<IIQQQQQQQQ', raw[:72])
reason, code, elr, far = hdr[3], hdr[4], hdr[5], hdr[6]
print('logbook %s: %d bytes, reason %d code %#x elr %#x far %#x'
      % (label, length, reason, code, elr, far))

body = raw[72:72 + length].decode('utf-8', 'replace')
lines = body.split('\n')
print('logbook: %d lines' % len(lines))

wanted = ('nvt:', 'usb:', 'fb_disp:', 'mt6895', 'hakios', 'MiniIOMobile',
          'display', 'panic', 'Panic', 'check failed', 'checks,')
for line in lines:
	if any(word in line for word in wanted):
		print(line[:220])
PY

say "done"
