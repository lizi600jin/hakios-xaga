#!/bin/sh
# xaga-qemu smoke test: does this machine's emulator still know the xaga machine?
#
# The interesting part of a qemu fork for this port is one file, hw/arm/mt6895.c,
# and the fact that the binary lists `mt6895` in -M help is the cheapest proof
# that a rebuild did not drop it. Everything past that needs the phone's own dumps
# (fw/LUA_1, fw/gpt_512.bin, data/lu2.img), which are not in the repository -- so
# when they are absent this test says SKIPPED and exits 0, rather than pretending
# to have booted something.
#
# Usage: tools/qemu-smoke.sh [qemu-build-dir]
#   or  QEMU_DIR=/path/to/xaga-qemu sh tools/qemu-smoke.sh

set -eu

OUT=${SMOKE_OUT:-/tmp/hakios-qemu-smoke}
mkdir -p "$OUT"

QEMU_DIR=${QEMU_DIR:-${1:-}}
if [ -z "$QEMU_DIR" ]; then
	# The common layouts: a checkout beside this repository, or the one this
	# repository documents. Neither is a hard-coded home directory.
	for cand in \
		"$(dirname "$0")/../../xaga-qemu" \
		"$(dirname "$0")/../../xaga-work/xaga-qemu"; do
		if [ -d "$cand" ]; then
			QEMU_DIR=$(cd "$cand" && pwd)
			break
		fi
	done
fi

if [ -z "$QEMU_DIR" ] || [ ! -d "$QEMU_DIR" ]; then
	echo "SKIPPED: no xaga-qemu checkout found (set QEMU_DIR= to point at one)"
	exit 0
fi

QEMU=$QEMU_DIR/build/qemu-system-aarch64
if [ ! -x "$QEMU" ]; then
	echo "SKIPPED: $QEMU_DIR has no built qemu-system-aarch64"
	echo "         build it with: cd $QEMU_DIR/build && ../configure \\"
	echo "           --target-list=aarch64-softmmu --disable-werror && ninja"
	exit 0
fi

echo "== machine model =="
if "$QEMU" -M help 2>&1 | tee "$OUT/machines.txt" | grep -q "mt6895"; then
	echo "   ok  mt6895 is in -M help"
else
	echo "   FAIL: mt6895 is not listed; this build is not the xaga fork"
	exit 1
fi

echo "== peripherals the wireless work depends on =="
# Each of these is a device the CONNSYS/BT/en1 drivers will reach through, so a
# build that lost one of them builds a machine the drivers cannot run on.
miss=0
for dev in mt6895-consys mt6895-dpmaif mt6895-btif; do
	if "$QEMU" -device help 2>&1 | grep -q "$dev"; then
		echo "   ok  $dev"
	else
		echo "   --  $dev not modelled"
		miss=$((miss + 1))
	fi
done
echo "   ($miss peripheral(s) not modelled; that is a modelling gap, not a build failure)"

echo "== boot =="
if [ -f "$QEMU_DIR/fw/LUA_1" ] && [ -f "$QEMU_DIR/data/lu2.img" ]; then
	echo "   dumps present, booting 15 s with UART captured"
	# -s is deliberately absent: this smoke test opens no gdb port.
	( cd "$QEMU_DIR" && timeout 15 ./build/qemu-system-aarch64 \
		-M mt6895,preloader=fw/LUA_1,gpt=fw/gpt_512.bin,data=data/lu2.img,entry-bytes=3072 \
		-m 6G -smp 8 -display none -monitor none \
		-accel tcg,thread=multi,tb-size=4096 -serial file:"$OUT/uart.log" ) || true
	echo "   uart log: $OUT/uart.log ($(wc -l < "$OUT/uart.log" 2>/dev/null || echo 0) lines)"
else
	echo "   SKIPPED: fw/LUA_1 or data/lu2.img missing; nothing to boot"
fi

echo
echo "qemu smoke done"
