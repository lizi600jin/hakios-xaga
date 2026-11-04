#!/bin/bash
# One XNU run on the Redmi Note 11T Pro (xaga, MT6895), where tools/xnu-cycle.sh does not apply.
#
# The difference is what the phone can do: lk_a registers no `boot:` command (checked against both
# the phone's own lk_a.img and the lk.img of the stock ROM in work-xaga/rom), so there is no way to
# try a kernel out of RAM -- every run is a write to boot_a. And a bad boot_a is a phone that only
# comes back through fastboot, since there is no recovery partition either. So this script does
# three things xnu-cycle.sh does not have to:
#
#  1. Backs boot_a up before the first write and never overwrites that backup (BACKUP below).
#  2. Refuses to run unless the backup is there and the phone answers fastboot.
#  3. Leaves the restore command on screen when anything fails, so a phone that will not boot is
#     one command from Android again.
#
# The rest is xnu-cycle.sh's: build the loader, pack it with the rooted Android boot image, flash
# boot_a, wait for Android, ask it for one XNU boot through the pmsg zone, wait for the run to end,
# and pull the kernel's log out of pstore.
#
# usage: xaga-cycle.sh [extra flags for mtk/loader/build.sh]
#   XNU_SECS=45        how long XNU runs before loader_return_after hands back
#   XNU_ARGS="cpus=4"  the kernel's own command line (must agree with XNU_CORES)
#   XNU_WATCH=120      the watcher's reset-if-nothing-happens timer
#   XAGA_SKIP_FLASH=1  do not write boot_a; just run the one XNU boot the phone already has
set -e

S=${HAKIOS_SERIAL:?set HAKIOS_SERIAL to the serial of your phone, as adb devices shows it}
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BACKUP="$ROOT/backup/boot_a_magisk.img"
OUT=${XNU_LOG_OUT:-$ROOT/logs/xnu-xaga.log}
SECS=${XNU_SECS:-45}
# The kernel's arguments come out of .config (CONFIG_CPUS, CONFIG_FBSCALE, CONFIG_FBFIT, ...), the
# same way `make phone` gets them. They are not optional decoration: fbfit and fbscale are what make
# MiniIOMobileFramebuffer scale the 750x1624 picture onto this 1080x2460 panel, and without them a
# kernel that boots draws nothing. `make xaga_defconfig` has to have been run.
XNU_ARGS=${XNU_ARGS:-$(cd "$ROOT" && tools/hakios-config.py args 2>/dev/null)}
[ -n "$XNU_ARGS" ] || { echo "no .config: run 'make xaga_defconfig' first" >&2; exit 1; }
XNU_WATCH=${XNU_WATCH:-120}

restore_hint() {
	echo
	echo "  the phone is not running Android. To put it back:"
	echo "    fastboot -s $S flash boot_a $BACKUP"
	echo "    fastboot -s $S reboot"
	echo
}

# ---- the backup -------------------------------------------------------------------
#
# boot_a is where Magisk lives on this phone (it is rooted), so the backup has to be the phone's
# own boot image, not the stock one: restoring stock would lose root, and with it the `su -c` that
# reads the kernel's log out of pstore at the end of every run.
if [ ! -s "$BACKUP" ]; then
	echo "no backup at $BACKUP" >&2
	echo "take it off the phone first, before anything writes boot_a:" >&2
	echo "  adb -s $S shell 'su -c \"dd if=/dev/block/by-name/boot_a\"' > $BACKUP" >&2
	echo "(or copy backup/boot_a_magisk.img into place if you already have it)" >&2
	exit 1
fi
echo "backup: $BACKUP ($(wc -c < "$BACKUP") bytes)"

if ! fastboot devices | grep -q "$S"; then
	echo "the phone is not in fastboot; run: adb -s $S reboot bootloader" >&2
	exit 1
fi
SLOT=$(fastboot -s "$S" getvar current-slot 2>&1 | sed -n 's/^current-slot: //p')
[ "$SLOT" = a ] || { echo "current slot is '$SLOT', not a: this writes boot_a only" >&2; exit 1; }

# ---- build ------------------------------------------------------------------------
#
# DEVICE=xaga is what makes mtk/loader/build.sh pick up mtk/mt6895/loader/board.h; without it the
# loader is built with the MT6768 defaults and would boot the wrong SoC's addresses.
#
# The three settings go as -D flags on the command line, which build.sh passes to clang: it reads
# no environment of its own (see the "$@" at the end of mtk/loader/build.sh).
CORES=$(expr "$XNU_ARGS" : '.*cpus=\([0-9]*\)' || true)
[ -n "$CORES" ] || CORES=4
TEST_FLAGS=(-DXNU_CORES="$CORES" -DXNU_WATCH_SECONDS="$XNU_WATCH"
	"-DXNU_RETURN_AFTER=\"$SECS\"" "-DXNU_EXTRA_ARGS=\"$XNU_ARGS epoch=$(date +%s)\"")

echo "building the loader for xaga ($CORES cores, ${SECS}s, ${XNU_WATCH}s watcher)..."
DEVICE=xaga "$ROOT/mtk/loader/build.sh" "${TEST_FLAGS[@]}" "$@" >/dev/null
python3 "$ROOT/mtk/loader/build_flashed.py" >/dev/null
IMG="$ROOT/xnu-flashed-slot_a.img"
echo "image: $IMG ($(wc -c < "$IMG") bytes, boot_a holds $((64 * 1024 * 1024)))"

# ---- flash and run ----------------------------------------------------------------
if [ "${XAGA_SKIP_FLASH:-0}" != 1 ]; then
	echo "flashing boot_a (restore with: fastboot -s $S flash boot_a $BACKUP)"
	fastboot -s "$S" flash boot_a "$IMG" || { restore_hint; exit 1; }
	fastboot -s "$S" reboot || { restore_hint; exit 1; }
	echo "waiting for Android..."
	for _ in $(seq 1 100); do
		[ "$(adb -s "$S" get-state 2>/dev/null)" = device ] && break
		sleep 3
	done
	[ "$(adb -s "$S" get-state 2>/dev/null)" = device ] || { restore_hint; exit 1; }
	sleep 4
fi

# Android is up: leave the request in the pmsg zone and reboot. mtkloader finds it, boots XNU, and
# the boot after that is Android again -- loader_return_after and the watcher see to that.
"$ROOT/mtk/tools/boot-xnu.sh"
echo "XNU is running; Android comes back by itself after ${SECS}s"

# ---- collect ----------------------------------------------------------------------
for _ in $(seq 1 $(( (SECS + 300) / 3 ))); do
	[ "$(adb -s "$S" get-state 2>/dev/null)" = device ] && break
	sleep 3
done
if [ "$(adb -s "$S" get-state 2>/dev/null)" != device ]; then
	echo "Android did not come back within $(( SECS + 300 ))s" >&2
	restore_hint
	exit 1
fi
sleep 3

mkdir -p "$(dirname "$OUT")"
for _ in $(seq 1 10); do
	adb -s "$S" shell 'su -c "cat /sys/fs/pstore/console-ramoops-0"' > "$OUT" 2>/dev/null || true
	[ -s "$OUT" ] && break
	sleep 2
done

# what the kernel kept for the host (kern.hakios.host_keep), which mtkloader put into the pmsg
# zone: appended to the log when it is this run's and not Android's own
adb -s "$S" shell 'su -c "cat /sys/fs/pstore/pmsg-ramoops-0"' > "$OUT.keep" 2>/dev/null || true
if head -c 64 "$OUT.keep" 2>/dev/null | grep -q 'kept for the host by the kernel'; then
	{ echo; cat "$OUT.keep"; } >> "$OUT"
	echo "kept for the host: $(head -1 "$OUT.keep" | sed 's/^=== kept for the host by the kernel: //; s/ ===$//')"
fi
rm -f "$OUT.keep"

echo "log: $OUT ($(wc -c < "$OUT") bytes)"
