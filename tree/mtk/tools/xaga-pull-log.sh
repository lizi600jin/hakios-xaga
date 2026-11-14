#!/bin/sh
# Pull the logbook out of the xnusys partition and print it.
#
# mtkloader writes it there because this board has no UART, no ramoops and no pstore: the screen
# holds one page, and a photograph of a scrolling console is worthless. The partition is ours and
# Android never mounts it, so the record survives a return to Android untouched.
#
# Layout: 8 MiB reserved at the end of the partition.
#   +0        struct logbook_hdr (4096 bytes, magic "HLOG")
#   +4096     the log, hdr.len bytes
#
# Usage: mtk/tools/xaga-pull-log.sh [serial]
set -e

S=${1:-${HAKIOS_SERIAL:?set HAKIOS_SERIAL to the serial of your phone, as adb devices shows it}}
SKIP=653560          # bytes 8 MiB below the end of xnusys, in 4096-byte blocks
COUNT=1025           # 1 header block + 4 MiB of log
TMP=/data/local/tmp/hakios-logbook.bin
OUT=${OUT:-$PWD/xaga-logbook.bin}

echo "pulling the logbook from $S"
adb -s "$S" shell "su -c 'dd if=/dev/block/by-name/xnusys of=$TMP bs=4096 skip=$SKIP count=$COUNT'" 2>&1 | tail -1
adb -s "$S" pull "$TMP" "$OUT" 2>&1 | tail -1

python3 "$(dirname "$0")/xaga-logbook.py" "$OUT"
