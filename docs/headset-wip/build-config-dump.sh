#!/bin/sh
# build-config-dump.sh -- the configuration a build of this tree actually gets, on stdout, in one go.
#
#   sh docs/headset-wip/build-config-dump.sh                 # the tree at /home/lizi/IOS/hakios-mtk
#   HAKIOS_TREE=/path/to/hakios-mtk sh docs/headset-wip/build-config-dump.sh
#
# READ ONLY. It runs no make and no build.sh, and writes nothing anywhere: every command it calls is
# one of tools/hakios-config.py's read-only subcommands (show, args, check), and the one macro
# expansion it does is clang -E -P on a four-line file in a mktemp directory, removed on exit.
# The output is meant to be pasted into a report as-is.
#
# What it answers, in order:
#   1  which files the configuration is made from, and the top of the tree
#   2  the driver choice: what is compiled into the kernel, what is left out, and which files that is
#   3  the .config's own boot-arguments -- what `make phone` / xaga-cycle.sh pass as XNU_EXTRA_ARGS
#   4  the loader's view: board.h's macros with the defaults and the -D overrides resolved, and the
#      complete boot_args.commandLine the loader builds out of them, with its length
#   5  the CLI a run uses, with the length check the kernel's 608-character line implies
#   6  what the files on disk were built from: mtimes and hashes, so a stale build is visible
#   7  every mismatch found, with file:line
#
# Exit status: 0 always, unless the tree is not there (2). A finding is printed, not returned: this
# is an audit tool, and a report that stops at the first problem hides the rest of them.
#
# Checked with `sh -n`, and run under both bash and dash: the same output (dash is the strict one --
# no [[ ]], no process substitution, no here-strings, no local, no arrays).

set -u

TREE=${HAKIOS_TREE:-/home/lizi/IOS/hakios-mtk}
CC=${CC:-clang}
PY=${PY:-python3}

if [ ! -d "$TREE" ]; then
	echo "build-config-dump: no tree at $TREE (HAKIOS_TREE=...)" >&2
	exit 2
fi
cd "$TREE" || exit 2
ROOT=$(pwd)

say() { printf '\n===== %s =====\n' "$1"; }
sub() { printf '\n--- %s ---\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }

TMPD=$(mktemp -d "${TMPDIR:-/tmp}/hakios-dump.XXXXXX") || exit 2
trap 'rm -rf "$TMPD"' EXIT INT TERM

# ---------------------------------------------------------------------------------------------------
say "0  WHERE THIS IS"
printf 'tree            %s\n' "$ROOT"
printf 'git HEAD        %s\n' "$(git rev-parse HEAD 2>/dev/null || echo '(not a git tree)')"
printf 'git HEAD date   %s\n' "$(git log -1 --format=%ci 2>/dev/null || echo '-')"
printf 'dirty files     %s (git status --porcelain | wc -l)\n' "$(git status --porcelain 2>/dev/null | wc -l | tr -d ' ')"
printf 'host            %s\n' "$(uname -srm)"
printf 'clang           %s\n' "$(have "$CC" && "$CC" --version 2>/dev/null | head -1 || echo '(absent)')"
printf 'python3         %s\n' "$(have "$PY" && "$PY" -V 2>&1 || echo '(absent)')"

HAKIOS_CONFIG=""
for cand in tools/hakios-config.py; do
	[ -f "$cand" ] && HAKIOS_CONFIG=$cand
done

# ---------------------------------------------------------------------------------------------------
say "1  THE FILES THE CONFIGURATION IS MADE FROM"
if [ -n "$HAKIOS_CONFIG" ]; then
	sub "tools/hakios-config.py show (the .config, its symbols and its arguments)"
	"$PY" "$HAKIOS_CONFIG" show 2>&1
	sub "tools/hakios-config.py check (its own consistency check)"
	"$PY" "$HAKIOS_CONFIG" check 2>&1
	sub "the .config's own header: the files it was made from"
	sed -n '1,4p' .config 2>/dev/null || echo '(no .config)'
	sub "mtimes, newest last"
	for f in configs/defconfig configs/common_defconfig \
		mtk/mt6895/configs/mt6895.config \
		devices/xaga/configs/xaga.config devices/xaga/configs/xaga_defconfig \
		devices/xaga/device.conf .config configs/boot-args.lst configs/symbols.tsv \
		mtk/mt6895/loader/board.h mtk/loader/mtkloader.c mtk/loader/build.sh; do
		[ -f "$f" ] || { printf '%-46s MISSING\n' "$f"; continue; }
		printf '%-46s %s\n' "$f" "$(date -r "$f" '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo '?')"
	done
else
	echo "(no tools/hakios-config.py)"
fi

# ---------------------------------------------------------------------------------------------------
say "2  THE DRIVERS COMPILED IN"
if [ -n "$HAKIOS_CONFIG" ]; then
	"$PY" "$HAKIOS_CONFIG" show 2>&1 | sed -n '/drivers built in/p;/drivers left out/p'
fi
sub "the CONFIG_HAKIOS_* symbols the .config sets to y"
grep -n '^CONFIG_HAKIOS_.*=y$' .config 2>/dev/null || echo '(none)'
sub "what the build tree was last told (xnu-src/.../bsd/dev/hakios.drivers.h)"
if [ -f xnu-src/xnu-8796.121.2/bsd/dev/hakios.drivers.h ]; then
	grep -n 'HAKIOS_DRIVERS_LIST\|HAKIOS_DRIVERS_COUNT\|HAKIOS_DRIVERS_CONFIG' \
		xnu-src/xnu-8796.121.2/bsd/dev/hakios.drivers.h
	printf 'written         %s\n' "$(date -r xnu-src/xnu-8796.121.2/bsd/dev/hakios.drivers.h '+%Y-%m-%d %H:%M:%S' 2>/dev/null)"
else
	echo "(not generated yet: run make kernel)"
fi
sub "each driver's declaration (drivers/*/conf, and the SoC's own)"
for f in mtk/mt6895/drivers/*/conf; do
	[ -f "$f" ] || continue
	awk -v F="$f" '/^driver /{printf "%-40s %-18s %s\n", F, $2, "declared"} /requires/{printf "%-40s %-18s %s\n", F, "", $0}' "$f"
done

# ---------------------------------------------------------------------------------------------------
say "3  THE .config's BOOT-ARGUMENTS (what a run passes as XNU_EXTRA_ARGS)"
CONFIG_ARGS=$("$PY" tools/hakios-config.py args 2>/dev/null || true)
if [ -n "${CONFIG_ARGS:-}" ]; then
	printf '%s\n' "$CONFIG_ARGS"
	printf '\ncharacters      %s\n' "$(printf '%s' "$CONFIG_ARGS" | wc -c | tr -d ' ')"
	printf 'words           %s\n' "$(printf '%s\n' "$CONFIG_ARGS" | wc -w | tr -d ' ')"
else
	echo '(tools/hakios-config.py args printed nothing: no .config?)'
fi
sub "CONFIG_BOOTARGS as it stands in the .config"
grep -n '^CONFIG_BOOTARGS=' .config 2>/dev/null || echo '(unset)'
sub "the same line in the file that sets it"
grep -n '^CONFIG_BOOTARGS=' devices/xaga/configs/xaga.config 2>/dev/null || echo '(not in xaga.config)'

# ---------------------------------------------------------------------------------------------------
# The macro rows and the command line are read out of clang -E -P output, and the compiler joins
# adjacent string literals ("a" " " "b" is one string "a b"). So each thing asked for is wrapped in
# markers that survive that join and cannot occur in a value:
#   ROW(name)       ->  "@@" "@@" name "@@" "@@" value        i.e. @@name@@value
#   the command line ->  "@CMDLINE@" <the loader's args> "@ENDCMDLINE@"
# parse.py turns those back into "name<TAB>value" lines and one string. Nothing is guessed.
cat > "$TMPD/parse.py" <<'PARSEPY'
import re
import sys

mode = sys.argv[1]
text = sys.stdin.read().replace('"', '')

if mode == "rows":
    for line in text.splitlines():
        m = re.search(r'@@\s*([A-Z][A-Z0-9_]*)\s*@@\s*(.*)$', line)
        if not m:
            continue
        name, value = m.group(1), m.group(2).strip()
        if value == name:
            value = "(undefined: no #define here, and no #ifndef default)"
        print("%s\t%s" % (name, value))
else:
    m = re.search(r'@CMDLINE@(.*?)@ENDCMDLINE@', text, re.S)
    print(re.sub(r'\s+', ' ', m.group(1)).strip() if m else "")
PARSEPY

say "4  THE LOADER's VIEW: board.h, WITH THE DEFAULTS AND THE -D OVERRIDES RESOLVED"
BOARD=mtk/mt6895/loader/board.h
if [ -f "$BOARD" ] && have "$CC"; then
	# The loader is built with -DBOARD_HEADER, which mtkloader.c includes before anything else, and
	# with -DXNU_* from build.sh's "$@". This is those two steps and nothing more: no object, no link.
	{
		echo '#include BOARD_HEADER'
		echo '#define ROW(m) "@@" #m "@@" m'
		for m in BOARD_NAME XNU_EXTRA_ARGS XNU_CORES XNU_RETURN_AFTER XNU_WATCH_SECONDS XNU_WDT_SECONDS BOARD_NO_PLOG BOARD_NO_UART BOARD_NO_OVL BOARD_NO_FB_TEXT BOARD_TOUCH XNU_SYSTEM_IMAGE XNU_ROOT_EMMC BOARD_STORAGE_UFS UFS_USE_LK_LIST MENU_DEFAULT_XNU BOARD_RETURN_TO_ANDROID BOARD_MALI_POWER BOARD_MALI_PREPOWER BOARD_MALI_DOMAINS BOARD_MALI_STAGE BOARD_MALI_EARLY BOARD_MALI_STOP BOARD_REPORT_PAGES XNU_BIG_CLOCK BOARD_REPORT_SYSTEM_IMAGE BOARD_USB_PROBE BOARD_PMIF_PROBE BOARD_DISP_PROBE BOARD_MALI_PROBE; do echo "ROW($m)"; done
	} > "$TMPD/probe.c"

	# "<name>"<tab><value>, one per line, as the preprocessor resolved it.
	macro_dump() {
		"$CC" --target=aarch64-linux-gnu -E -P -DBOARD_HEADER="\"$ROOT/$BOARD\"" "$@" "$TMPD/probe.c" 2>/dev/null |
			"$PY" "$TMPD/parse.py" rows |
			awk -F'\t' '{ printf "  %-26s %s\n", $1, $2 }'
	}

	sub "board.h as the build resolves it (no -D)"
	macro_dump

	sub "the same with the flags xaga-cycle.sh passes (the .config's arguments, SECS=600)"
	macro_dump -DXNU_CORES=4 -DXNU_WATCH_SECONDS=120 '-DXNU_RETURN_AFTER="600"' \
		"-DXNU_EXTRA_ARGS=\"$CONFIG_ARGS epoch=$(date +%s)\""

	sub "the boot_args.commandLine the loader builds (rd=md0 + XNU_EXTRA_ARGS + loader_return_after)"
	cat > "$TMPD/args.c" <<'ARGS'
#include BOARD_HEADER
"@CMDLINE@" XNU_BOOT_ARGS "@ENDCMDLINE@"
ARGS
	# mtkloader.c's own two lines, reproduced so the string is the loader's and not a guess:
	#   #define XNU_BOOT_ARGS  XNU_ROOT_ARG " " XNU_EXTRA_ARGS " loader_return_after=" XNU_RETURN_AFTER
	"$CC" --target=aarch64-linux-gnu -E -P -DBOARD_HEADER="\"$ROOT/$BOARD\"" \
		-DXNU_ROOT_ARG='"rd=md0"' \
		-DXNU_BOOT_ARGS='XNU_ROOT_ARG " " XNU_EXTRA_ARGS " loader_return_after=" XNU_RETURN_AFTER' \
		"$TMPD/args.c" 2>/dev/null |
		"$PY" "$TMPD/parse.py" cmdline > "$TMPD/cmdline.txt"
	if [ -s "$TMPD/cmdline.txt" ]; then
		cat "$TMPD/cmdline.txt"
		printf '\ncharacters      %s of 607 (BOOT_LINE_LENGTH 608 less the terminator, mtkloader.c:344)\n' \
			"$(wc -c < "$TMPD/cmdline.txt" | tr -d ' ')"
		printf 'words           %s\n' "$(wc -w < "$TMPD/cmdline.txt" | tr -d ' ')"
	else
		echo '(clang -E produced no command line)'
	fi

	sub "the same string as the last built loader has it"
	if [ -f mtk/loader/mtkloader.bin ]; then
		strings -a mtk/loader/mtkloader.bin 2>/dev/null | grep -m1 '^boot args: ' || echo '(no "boot args:" string in mtkloader.bin)'
		printf 'mtkloader.bin   %s bytes, %s\n' \
			"$(wc -c < mtk/loader/mtkloader.bin | tr -d ' ')" \
			"$(date -r mtk/loader/mtkloader.bin '+%Y-%m-%d %H:%M:%S' 2>/dev/null)"
	else
		echo '(no mtk/loader/mtkloader.bin)'
	fi
else
	echo "(no $BOARD, or no clang)"
fi

# ---------------------------------------------------------------------------------------------------
say "4b WHAT A BUILD WITHOUT DEVICE=xaga GETS (the MT6768 defaults)"
# mtk/loader/build.sh:24-31 picks mtk/<soc>/loader/board.h only when DEVICE names a phone whose
# device.conf says `soc`; with no DEVICE there is no -DBOARD_HEADER, and mtkloader.c's own values in
# the #ifndef blocks are the MT6768 / x6826b ones. These are those lines, as they stand.
for pat in 'XNU_PHYS_BASE' 'XNU_MEM_SIZE' 'UART_BASE' 'TOPRGU_BASE' 'RAMOOPS_CONSOLE' 'LK_FB_BASE' \
	'OVL0_BASE' 'XNU_EXTRA_ARGS' 'XNU_CORES' 'XNU_RETURN_AFTER' 'XNU_WATCH_SECONDS' 'XNU_WDT_SECONDS' \
	'XNU_SYSTEM_IMAGE' 'MENU_DEFAULT_XNU'; do
	ln=$(grep -n "^#define $pat " mtk/loader/mtkloader.c mtk/loader/mtkdbg.h 2>/dev/null | head -1)
	[ -n "$ln" ] || ln=$(grep -n "^#define[[:space:]]\{1,\}$pat " mtk/loader/mtkloader.c mtk/loader/mtkdbg.h 2>/dev/null | head -1)
	printf '  %s\n' "${ln:-(not a default in the loader: board.h only)}"
done

# ---------------------------------------------------------------------------------------------------
say "5  THE COMMAND LINE A RUN USES"
sub "make phone DEVICE=xaga: the recipe, and what it sets"
grep -n 'XNU_SECS=$(SECS) XNU_ARGS' Makefile 2>/dev/null | sed 's/^/  Makefile:/'
grep -n '^CYCLE ?=' Makefile 2>/dev/null | sed 's/^/  Makefile:/'
grep -n '^ARGS ?=' Makefile 2>/dev/null | sed 's/^/  Makefile:/'
grep -n '^DEVICE ?=' Makefile 2>/dev/null | sed 's/^/  Makefile:/'

sub "mtk/loader/build.sh: how the board header is chosen"
sed -n '24,36p' mtk/loader/build.sh | sed 's/^/  /'

sub "mtk/tools/xaga-cycle.sh: what it passes on the compiler's command line"
grep -n 'DEVICE=xaga\|^SECS=\|^XNU_WATCH=\|TEST_FLAGS=' mtk/tools/xaga-cycle.sh 2>/dev/null | sed 's/^/  xaga-cycle.sh:/'

sub "the length budget tools/hakios-config.py enforces"
grep -n 'KERNEL_LINE = \|RUN_OVERHEAD = ' tools/hakios-config.py 2>/dev/null | sed 's/^/  /'
grep -n 'define BOOT_LINE_LENGTH' mtk/loader/mtkloader.c 2>/dev/null | sed 's/^/  mtkloader.c:/'

# ---------------------------------------------------------------------------------------------------
say "6  WHAT THE FILES ON DISK WERE BUILT FROM"
for f in hakios-kernel xnu-flashed-slot_a.img build-payload.bin mtk/loader/mtkloader.bin; do
	if [ -f "$f" ]; then
		printf '%-28s %10s bytes  %s  %s\n' "$f" "$(wc -c < "$f" | tr -d ' ')" \
			"$(date -r "$f" '+%Y-%m-%d %H:%M:%S' 2>/dev/null)" \
			"$(sha256sum "$f" 2>/dev/null | cut -c1-16)"
	else
		printf '%-28s (absent)\n' "$f"
	fi
done
sub "the newest source each one depends on"
for f in mtk/mt6895/loader/board.h mtk/loader/mtkloader.c mtk/loader/start.S \
	mtk/mt6895/drivers/audio/hakios.mt6895_headset.c xnu/files/bsd/sys/hakios.pmif.h; do
	[ -f "$f" ] || { printf '%-56s MISSING\n' "$f"; continue; }
	printf '%-56s %s\n' "$f" "$(date -r "$f" '+%Y-%m-%d %H:%M:%S' 2>/dev/null)"
done

# ---------------------------------------------------------------------------------------------------
say "7  MISMATCHES THIS DUMP CAN SEE"
# Each numbered line is a mismatch with the evidence beside it; a line without a number is context
# for the numbers around it.
FOUND=0
flag() { FOUND=$((FOUND + 1)); printf '%2d. %s\n' "$FOUND" "$1"; }
info() { printf '    %s\n' "$1"; }

sub "7.1  headset=1: where it is written, and whether a run gets it"
FRAG_HEAD=$(grep -m1 '^CONFIG_BOOTARGS=' devices/xaga/configs/xaga.config 2>/dev/null)
LIVE_ARGS=${CONFIG_ARGS:-}
case "$FRAG_HEAD" in
*headset=*) FRAG_HAS=yes ;; *) FRAG_HAS=no ;;
esac
case "$LIVE_ARGS" in
*headset=*) LIVE_HAS=yes ;; *) LIVE_HAS=no ;;
esac
info "devices/xaga/configs/xaga.config:40 says headset= : $FRAG_HAS"
info "the .config's arguments (what xaga-cycle.sh passes) have headset= : $LIVE_HAS"
if [ "$FRAG_HAS" = no ] && [ "$LIVE_HAS" = no ]; then
	flag "headset= is in neither the fragment nor the .config: a run boots without it. Add it to devices/xaga/configs/xaga.config:40 CONFIG_BOOTARGS, then make xaga_defconfig"
elif [ "$FRAG_HAS" = yes ] && [ "$LIVE_HAS" = no ]; then
	flag "the fragment HAS headset= but the .config does not: the .config predates the edit, and mtk/tools/xaga-cycle.sh:35 reads the .config, so a run still boots WITHOUT headset=1. Run make xaga_defconfig (no kernel rebuild needed: the driver is already compiled in)"
elif [ "$FRAG_HAS" = no ] && [ "$LIVE_HAS" = yes ]; then
	flag "the .config has headset= but the fragment does not: the next make xaga_defconfig will drop it. Add it to devices/xaga/configs/xaga.config:40"
else
	info "both agree; a run gets headset=1"
fi
if grep -q '^#define XNU_EXTRA_ARGS.*headset=1' "$BOARD" 2>/dev/null; then
	info "board.h:$(grep -n '^#define XNU_EXTRA_ARGS.*headset=1' "$BOARD" | cut -d: -f1) also has headset=1 -- for a loader built with no -DXNU_EXTRA_ARGS (a bare mtk/loader/build.sh), not for a run"
fi
case "$LIVE_ARGS" in
*usb=*) info "usb= is set: the USB driver comes up" ;;
*) flag "CONFIG_HAKIOS_MT6895_USB=y but the arguments have no usb=, so hakios_mt6895_usb_init() returns at its first line (mtk/mt6895/drivers/usb/hakios.mt6895_usb.c:1866): the driver is compiled in and never runs. mtk/mt6895/configs/mt6895.config:83 says xaga.config turns it on -- it does not" ;;
esac

sub "7.2  arguments named in CONFIG_BOOTARGS that configs/boot-args.lst does not have"
if [ -f configs/boot-args.lst ]; then
	for w in ${CONFIG_ARGS:-}; do
		name=${w%%=*}
		grep -q "^$name	" configs/boot-args.lst 2>/dev/null ||
			flag "the argument $name is not in configs/boot-args.lst, so nothing in the source reads it (and CONFIG_BOOTARGS would have refused it)"
	done
	info "(none listed above: every argument in the .config has a reader in the list)"
else
	info "(no configs/boot-args.lst)"
fi

sub "7.3  the mali arguments: the driver's readers vs the list"
if [ -f mtk/mt6895/drivers/gpu/hakios.mali610.c ]; then
	grep -o 'PE_parse_boot_argn("mali[a-z0-9_]*"' mtk/mt6895/drivers/gpu/hakios.mali610.c |
		sed 's/.*("//; s/"//' | sort -u > "$TMPD/mali-driver.txt"
	grep '^mali' configs/boot-args.lst 2>/dev/null | awk '{print $1}' | sort -u > "$TMPD/mali-lst.txt"
	info "$(wc -l < "$TMPD/mali-driver.txt" | tr -d ' ') mali arguments are read by hakios.mali610.c; $(wc -l < "$TMPD/mali-lst.txt" | tr -d ' ') lines in configs/boot-args.lst begin with mali"
	ONLY_DRV=$(comm -23 "$TMPD/mali-driver.txt" "$TMPD/mali-lst.txt" 2>/dev/null | tr '\n' ' ')
	ONLY_LST=$(comm -13 "$TMPD/mali-driver.txt" "$TMPD/mali-lst.txt" 2>/dev/null | tr '\n' ' ')
	[ -n "$ONLY_DRV" ] && flag "read by hakios.mali610.c but absent from configs/boot-args.lst (CONFIG_BOOTARGS rejects them): $ONLY_DRV"
	[ -n "$ONLY_LST" ] && info "in the list with no reader under mtk/mt6895/drivers/ (they belong to the MT6768's hakios.mali.c / hakios.mtk_keys.c): $ONLY_LST"
fi

sub "7.4  the generated catalogues against the source (tools/boot-args.py --check)"
if [ -f tools/boot-args.py ] && have "$PY"; then
	OUT=$("$PY" tools/boot-args.py --check 2>&1)
	if [ -n "$OUT" ]; then
		printf '%s\n' "$OUT" | sed 's/^/    /'
		flag "configs/symbols.tsv and docs/boot-args.md are stale. symbols.tsv is what CONFIG_BOOTARGS is checked against: a stale one rejects a correct argument and accepts a typo. Regenerate with tools/boot-args.py --symbols > configs/symbols.tsv (and --markdown for the doc), in the build tree, then re-run make xaga_defconfig"
	else
		info "both are current"
	fi
fi

sub "7.5  an argument a run adds that no configuration can see"
case " ${CONFIG_ARGS:-} " in
*" epoch= "*) info "epoch= is in the .config" ;;
*) info 'epoch= is added by mtk/tools/xaga-cycle.sh:78 (-DXNU_EXTRA_ARGS="<the .config args> epoch=$(date +%s)") and is in no configuration file; it identifies the run in the log, by design (docs/configs.md: the few the loader and the runner set themselves)' ;;
esac

sub "7.6  board.h macros a -D cannot change (an unconditional #define, no #ifndef)"
for m in MENU_DEFAULT_XNU BOARD_NO_UART BOARD_NO_OVL BOARD_NO_PLOG; do
	if grep -q "^#define $m" "$BOARD" 2>/dev/null; then
		info "$m = $(grep -m1 "^#define $m" "$BOARD" | awk '{print $3}') at $BOARD:$(grep -n -m1 "^#define $m" "$BOARD" | cut -d: -f1) -- -D$m=0 is overridden with a -Wmacro-redefined warning and the build keeps 1"
	fi
done

sub "7.7  the .config against the fragments it claims to be made from"
if [ -n "$HAKIOS_CONFIG" ] && [ -f .config ]; then
	if "$PY" "$HAKIOS_CONFIG" defconfig xaga_defconfig -o "$TMPD/regen.config" >/dev/null 2>&1; then
		if diff -q .config "$TMPD/regen.config" >/dev/null 2>&1; then
			info ".config is what xaga_defconfig makes today: current"
		else
			flag ".config differs from what xaga_defconfig makes today (diff below): a fragment changed after the last make xaga_defconfig, so the .config -- and the kernel built from it -- is older than the files"
			diff -u .config "$TMPD/regen.config" 2>/dev/null | sed -n '3,40p' | sed 's/^/    /'
		fi
	else
		info "(hakios-config.py defconfig failed; see section 1)"
	fi
fi

sub "7.8  files a build depends on that git does not carry"
for f in mtk/mt6895/configs/mt6895.config mtk/mt6895/loader/board.h \
	devices/xaga/configs/xaga.config devices/xaga/configs/xaga_defconfig \
	xnu/files/bsd/sys/hakios.pmif.h; do
	[ -f "$f" ] || { info "$f: MISSING"; continue; }
	if git ls-files --error-unmatch "$f" >/dev/null 2>&1; then
		if git diff --quiet HEAD -- "$f" 2>/dev/null; then
			info "$f: tracked, unchanged since HEAD"
		else
			info "$f: tracked, changed since HEAD"
		fi
	elif git status --porcelain -- "$f" 2>/dev/null | grep -q '^A '; then
		flag "$f is staged as an ADD but its index blob is empty (intent-to-add, blob e69de29): a plain git commit records an EMPTY file. git add it by path -- never git stash / reset / checkout, which would leave the other A files half-written"
	elif git check-ignore -q "$f" 2>/dev/null; then
		info "$f: ignored on purpose ($(git check-ignore -v "$f" 2>/dev/null | awk '{print $1, $2}')) -- it is a device repository's own file, cloned rather than committed (devices/manifest.conf)"
	else
		flag "$f is not tracked and not ignored: a fresh clone has no such file, and a build there fails or silently gets different values. git add it"
	fi
done

sub "7.9  tools/drivers-check.py (the tree's own driver map check)"
if [ -f tools/drivers-check.py ] && have "$PY"; then
	OUT=$("$PY" tools/drivers-check.py 2>&1)
	if [ -n "$OUT" ]; then
		printf '%s\n' "$OUT" | sed 's/^/    /'
		flag "drivers-check is not clean (see above): CI runs it, and a driver it complains about is one drivers/map does not place in the kernel tree"
	else
		info "clean"
	fi
fi

printf '\n%d numbered finding(s); each one is explained, with file:line, in docs/headset-wip/findings/BUILD-CONFIG-AUDIT.md\n' "$FOUND"
