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

S=${1:-GAJZ4HIN99AQ69Y9}
SKIP=653560          # bytes 8 MiB below the end of xnusys, in 4096-byte blocks
COUNT=1025           # 1 header block + 4 MiB of log
TMP=/data/local/tmp/hakios-logbook.bin
OUT=${OUT:-/home/lizi/IOS/hakios-mtk/xaga-logbook.bin}

echo "pulling the logbook from $S"
adb -s "$S" shell "su -c 'dd if=/dev/block/by-name/xnusys of=$TMP bs=4096 skip=$SKIP count=$COUNT'" 2>&1 | tail -1
adb -s "$S" pull "$TMP" "$OUT" 2>&1 | tail -1

python3 - "$OUT" <<'PY'
import struct, sys
path = sys.argv[1]
d = open(path, 'rb').read()
magic, version, ln, reason, code, elr, far, ticks, where, summ = struct.unpack_from('<IIQQQQQQQQ', d, 0)
if magic != 0x474f4c48:
    print('no HLOG header: magic 0x%08x -- the loader never wrote one' % magic)
    for name, val in (('first 64 bytes', d[:64]),):
        print(name, val.hex())
    sys.exit(1)
why = {0: 'end (the kernel handed back)', 1: 'early exception', 2: 'checkpoint',
       0xfeedface: 'boot note (written before XNU was entered)'}.get(reason, 'unknown')
print('magic    HLOG v%d' % version)
print('len      %d bytes' % ln)
print('reason   0x%x  %s' % (reason, why))
print('code     0x%x' % code)
print('elr      0x%x  far 0x%x' % (elr, far))
print('ticks    %d  (where 0x%x)' % (ticks, where))
print('sum      0x%x' % summ)
body = d[4096:4096 + ln]
print('checksum %s' % ('ok' if sum(body) == summ else 'MISMATCH'))
print('---- log ----')
sys.stdout.write(body.decode('utf-8', 'replace'))
print('---- end ----')
PY
