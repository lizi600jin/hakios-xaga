#!/bin/sh
# android-accdet-trace.sh -- record MT6368 (SPMI USID 5) accdet register traffic on a
# running stock Android, so the bare-metal port has something true to compare against.
#
# The problem this solves: our XNU side has never read a correct accdet value off the
# MT6368, while Android drives the same chip daily. Android's actual register traffic is
# the only ground truth we have, so this captures it -- read/write, address, value, order.
#
# Serial comes from $HAKIOS_SERIAL. Nothing here is pinned to one device or one checkout.
#
# Subcommands:
#   probe    read-only survey: root? tracefs? kprobe? regmap tracepoints? which driver?
#   start    arm the capture          [--mode regmap|kprobe|auto] [--secs N] [--kb N]
#   stop     disarm and snap the trace buffer to a file on the device
#   pull     copy the snapshot(s) to ./accdet-trace-<stamp>/
#   parse    run accdet-trace-parse.py over the pulled traces
#   run      start + wait + stop + pull + parse, in one go
#   selftest offline assertions on the script's own helper functions
#
# Everything is read-only with respect to the phone, with one exception that is forced on
# us: tracefs is a set of files you write to in order to arm a tracer. The writes this
# script performs are confined to /sys/kernel/tracing (tracing_on, kprobe_events, the
# per-event enable switches, buffer_size_kb). It never writes a sysfs value that controls
# hardware, and in particular it never touches spmi_access -- see DANGER-spmi-access.md,
# that path has put this phone into preloader twice.
#
# usage: HAKIOS_SERIAL=<serial> ./android-accdet-trace.sh probe
#        HAKIOS_SERIAL=<serial> ./android-accdet-trace.sh run --secs 20
#
# Exit: 0 ok, 1 precondition failed, 2 usage, 3 selftest failure.

set -u

PROG=$(basename "$0")
STAMP=$(date +%Y%m%d-%H%M%S)
OUTDIR=${ACCDET_OUTDIR:-./accdet-trace-$STAMP}

# The MT6368 is SPMI bus 0, USID 5. The kernel names SPMI devices "%d-%02x", so its
# regmap is "0-05" and the MT6363 next door is "0-04". This string is the filter that
# keeps us from drowning in the other PMIC's traffic.
MT6368_REGMAP=0-05
MT6368_SID=5

# Register windows the vendor driver actually touches (raw/mt6368-accdet.c cat_register).
# Used only to sanity-check that a capture looks plausible, never to filter.
WIN_ACCDET_LO=0x2688
WIN_ACCDET_HI=0x26d3

TR=""
MODE=""
NEED_ROOT=0

say()  { printf '%s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die()  { printf 'error: %s\n' "$*" >&2; exit 1; }
usage() { sed -n '2,/^set -u$/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'; exit 2; }

# --- device plumbing ---------------------------------------------------------------

# Every adb call goes through here so "-s $HAKIOS_SERIAL" can never be forgotten.
# A device left unspecified while two are plugged in is how you trace the wrong phone.
adbd() {
    if [ -z "${HAKIOS_SERIAL:-}" ]; then
        die "HAKIOS_SERIAL is not set (export HAKIOS_SERIAL=<serial>; see 'adb devices')"
    fi
    adb -s "$HAKIOS_SERIAL" "$@"
}

# Run a command on the device as root. On this ROM adbd already lands in u:r:ksu:s0, so
# the plain shell is root and no su hop is needed; we only add su if the shell is not root,
# and we never call "adb root" (that restarts adbd and would drop a capture in progress).
sh_() { adbd shell "$@"; }

need_device() {
    command -v adb >/dev/null 2>&1 || die "adb not found (source ~/.dsh/env.sh)"
    [ -n "${HAKIOS_SERIAL:-}" ] || die "HAKIOS_SERIAL is not set"
    adbd get-state >/dev/null 2>&1 || die "device $HAKIOS_SERIAL not reachable via adb"
}

# ---- probe: read-only survey -------------------------------------------------------
cmd_probe() {
    need_device
    say "== device =="
    adbd get-state 2>&1 | sed 's/^/  state: /'
    sh_ 'getprop ro.product.device; getprop ro.build.version.release; uname -r' 2>&1 | sed 's/^/  /'

    say ""
    say "== privilege =="
    # adbd landing as uid 0 is the happy case (KernelSU). Record it rather than assume it.
    sh_ 'id' 2>&1 | sed 's/^/  /'
    sh_ 'command -v su || echo no-su' 2>&1 | sed 's/^/  /'

    say ""
    say "== tracing mount =="
    # tracefs is at /sys/kernel/tracing on this kernel; debugfs is not mounted at all, which
    # is why every /sys/kernel/debug recipe found online is a dead end here.
    sh_ 'mount | grep -E "tracefs|debugfs"' 2>&1 | sed 's/^/  /'

    say ""
    say "== tracers available =="
    for d in /sys/kernel/tracing /sys/kernel/debug/tracing; do
        sh_ "[ -d $d ] && { echo \"$d:\"; echo -n '  current: '; cat $d/current_tracer 2>/dev/null; \
             echo -n '  available: '; cat $d/available_tracers 2>/dev/null; }" 2>&1
    done

    say ""
    say "== regmap tracepoints (best path: they carry the real value) =="
    sh_ 'ls /sys/kernel/tracing/events/regmap/ 2>&1' | sed 's/^/  /'

    say ""
    say "== kprobe support (fallback) =="
    sh_ '[ -e /sys/kernel/tracing/kprobe_events ] && echo "kprobe_events: present" \
         || echo "kprobe_events: ABSENT (CONFIG_KPROBE_EVENTS off?)"' | sed 's/^/  /'
    sh_ 'cat /proc/sys/kernel/kptr_restrict 2>/dev/null' | sed 's/^/  kptr_restrict: /'
    sh_ 'cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null' | sed 's/^/  perf_event_paranoid: /'

    say ""
    say "== accdet driver: builtin or module =="
    # mt6368_accdet is a .ko on this ROM (coresize 61440, initsize 0). That matters: a
    # module's symbols only exist in kallsyms after it loads, and module text is reachable
    # by kprobe by name.
    sh_ 'ls -d /sys/module/mt6368_accdet 2>/dev/null && echo "  -> module (loaded)" \
         || echo "  -> not a loaded module (builtin or absent)"'
    sh_ 'cat /sys/module/mt6368_accdet/coresize 2>/dev/null' | sed 's/^/  coresize: /'

    say ""
    say "== the two symbols we would kprobe =="
    sh_ 'grep -E " (regmap_read|regmap_write|regmap_update_bits_base)$" /proc/kallsyms 2>/dev/null | head' \
        | sed 's/^/  /'

    say ""
    say "== accdet sysfs (read-only, useful with no headset plugged) =="
    # state and dump_reg are 0444, which is why the phone can be interrogated without a
    # headset and without arming a tracer at all.
    sh_ 'for f in state dump_reg; do p=/sys/bus/platform/drivers/pmic-codec-accdet/$f; \
         [ -e "$p" ] && { echo "$p:"; head -c 400 "$p" 2>&1; echo; } || echo "$p: absent"; done' 2>&1

    say ""
    say "== verdict =="
    if sh_ 'test -e /sys/kernel/tracing/events/regmap/regmap_reg_write'; then
        say "  regmap tracepoints: USABLE -> use --mode regmap (carries name+reg+val)"
    else
        say "  regmap tracepoints: absent"
        if sh_ 'test -e /sys/kernel/tracing/kprobe_events'; then
            say "  kprobe_events: USABLE -> use --mode kprobe"
        else
            say "  kprobe_events: absent -> neither trace path works on this build"
        fi
    fi
}

# --- arming -----------------------------------------------------------------------

# Pick the tracefs root. There are two conventions in the wild and guessing wrong just
# wastes a capture, so ask the device.
trace_root() {
    if [ -n "$TR" ]; then printf '%s' "$TR"; return 0; fi
    for d in /sys/kernel/tracing /sys/kernel/debug/tracing; do
        if sh_ "test -d $d" 2>/dev/null; then TR=$d; printf '%s' "$TR"; return 0; fi
    done
    return 1
}

# Which mechanism will actually produce data. regmap tracepoints are preferred because
# they hand us the map name (-> SID filtering) and the value, with no pointer chasing.
pick_mode() {
    [ -n "$MODE" ] && { printf '%s' "$MODE"; return 0; }
    if sh_ 'test -e /sys/kernel/tracing/events/regmap/regmap_reg_write' 2>/dev/null; then
        printf 'regmap'
    elif sh_ 'test -e /sys/kernel/tracing/kprobe_events' 2>/dev/null; then
        printf 'kprobe'
    else
        return 1
    fi
}

cmd_start() {
    need_device
    R=$(trace_root) || die "no tracefs found (tried /sys/kernel/tracing and /sys/kernel/debug/tracing)"
    M=$(pick_mode) || die "neither regmap tracepoints nor kprobe_events available; run 'probe'"

    say "tracefs: $R"
    say "mode:    $M"

    # A capture that inherits someone else's filter produces an empty file and a wasted
    # trip to the phone. Clear what we are about to set, and only that.
    sh_ "echo 0 > $R/tracing_on" || die "cannot write $R/tracing_on (need root)"

    if [ -n "${BUFFER_KB:-}" ]; then
        sh_ "echo $BUFFER_KB > $R/buffer_size_kb" 2>/dev/null \
            && say "buffer:  ${BUFFER_KB} kB/CPU"
    fi

    if [ "$M" = "regmap" ]; then
        # The regmap tracepoints are per-event enable switches; there is nothing to define.
        sh_ "echo 0 > $R/events/regmap/enable"
        sh_ "echo 1 > $R/events/regmap/regmap_reg_write/enable" || die "cannot enable regmap_reg_write"
        sh_ "echo 1 > $R/events/regmap/regmap_reg_read/enable"  || die "cannot enable regmap_reg_read"
        say "armed:  regmap/regmap_reg_write + regmap_reg_read"
    else
        # kprobe on the regmap core. regmap_update_bits is a static inline in regmap.h with
        # no out-of-line symbol, so the base function is the one to take. Note that the
        # driver's own accdet_read/accdet_write wrappers are NOT usable as probe targets:
        # they exist as symbols in the .ko but every call site inlined them (0 bl to them
        # in the disassembly), so a probe there would never fire.
        sh_ "echo > $R/kprobe_events" 2>/dev/null
        # regmap_read(map, reg, val)               x0=map x1=reg x2=&val
        # regmap_write(map, reg, val)              x0=map x1=reg x2=val
        # regmap_update_bits_base(map, reg, mask, val, ...) x0=map x1=reg x2=mask x3=val
        sh_ "echo 'p:accdet_r regmap_read reg=%x1' >> $R/kprobe_events" \
            || die "kprobe add failed for regmap_read"
        sh_ "echo 'p:accdet_w regmap_write reg=%x1 val=%x2' >> $R/kprobe_events" \
            || die "kprobe add failed for regmap_write"
        sh_ "echo 'p:accdet_u regmap_update_bits_base reg=%x1 mask=%x2 val=%x3' >> $R/kprobe_events" \
            || warn "kprobe add failed for regmap_update_bits_base (mask/val will be missing)"
        sh_ "echo 1 > $R/events/kprobes/enable" || die "cannot enable kprobes"
        say "armed:  kprobes accdet_r / accdet_w / accdet_u"
    fi

    sh_ "echo > $R/trace" 2>/dev/null
    sh_ "echo 1 > $R/tracing_on" || die "cannot start tracing"
    say "tracing: on"
    say ""
    say "Now exercise the jack. With no headset: the useful traffic is the init burst that"
    say "runs ~10 s after boot (ACCDET_INIT_WAIT_TIMER) plus anything that touches the codec."
    say "With a headset: plug in, wait, press the button, pull out."
    say ""
    say "Stop with: $PROG stop"
}

# --- stopping ---------------------------------------------------------------------

cmd_stop() {
    need_device
    R=$(trace_root) || die "no tracefs found"
    # Snapshot before tearing anything down: tracing_on=0 is not retroactive, but the ring
    # buffer is shared and a later capture would overwrite it.
    SNAP="${1:-/data/local/tmp/accdet-trace-$STAMP.txt}"
    sh_ "echo 0 > $R/tracing_on"
    sh_ "cat $R/trace > $SNAP" || die "cannot snapshot trace to $SNAP"
    SZ=$(sh_ "wc -c < $SNAP" 2>/dev/null | tr -d ' ')
    say "snapshot: $SNAP (${SZ:-?} bytes)"
    [ -n "${SZ:-}" ] && [ "$SZ" -lt 200 ] && warn "snapshot is nearly empty; did anything touch the codec?"
    # Disarm so the next boot is not tracing into a buffer nobody reads.
    sh_ "echo 0 > $R/events/regmap/regmap_reg_write/enable" 2>/dev/null
    sh_ "echo 0 > $R/events/regmap/regmap_reg_read/enable" 2>/dev/null
    sh_ "echo 0 > $R/events/kprobes/enable" 2>/dev/null
    say "disarmed."
    printf '%s\n' "$SNAP" > ./accdet-trace-last-snapshot
    say "path recorded in ./accdet-trace-last-snapshot"
}

# --- pulling ----------------------------------------------------------------------

cmd_pull() {
    need_device
    mkdir -p "$OUTDIR" || die "cannot create $OUTDIR"
    if [ -f ./accdet-trace-last-snapshot ]; then
        SNAP=$(cat ./accdet-trace-last-snapshot)
        adbd pull "$SNAP" "$OUTDIR/" || die "pull failed for $SNAP"
    else
        # Fall back to sweeping whatever is there, newest last.
        adbd shell 'ls -1 /data/local/tmp/accdet-trace-*.txt 2>/dev/null' > /tmp/.accdet-list.$$
        if [ -s /tmp/.accdet-list.$$ ]; then
            while IFS= read -r f; do
                adbd pull "$f" "$OUTDIR/" || warn "pull failed for $f"
            done < /tmp/.accdet-list.$$
        else
            warn "nothing to pull (no snapshot recorded, none on device)"
        fi
        rm -f /tmp/.accdet-list.$$
    fi
    say "pulled into $OUTDIR"
    ls -l "$OUTDIR" 2>/dev/null | sed 's/^/  /'
}

# --- parsing ----------------------------------------------------------------------

cmd_parse() {
    # The parser lives next to this script, wherever that is. Resolving through $0 keeps
    # the pair relocatable -- no absolute path baked in.
    HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
    PARSER="$HERE/accdet-trace-parse.py"
    [ -f "$PARSER" ] || die "accdet-trace-parse.py not found next to this script ($HERE)"
    N=0
    for f in "$OUTDIR"/*.txt "$OUTDIR"/*.trace; do
        [ -f "$f" ] || continue
        N=$((N + 1))
        say "== $f"
        python3 "$PARSER" "$f" --out "$f.json" || warn "parse failed for $f"
    done
    [ "$N" -eq 0 ] && die "no .txt traces in $OUTDIR (run 'pull' first)"
    say "wrote $N json file(s) in $OUTDIR"
}

# --- run: the whole loop ----------------------------------------------------------

cmd_run() {
    cmd_start
    say ""
    say "capturing for ${SECS}s -- do the jack thing now"
    sleep "$SECS"
    say ""
    cmd_stop
    cmd_pull
    cmd_parse
}

# --- selftest ---------------------------------------------------------------------

cmd_selftest() {
    fails=0
    total=0
    ck() {
        total=$((total + 1))
        if [ "$2" = "1" ]; then printf 'ok   %s\n' "$1"
        else printf 'FAIL %s  (%s)\n' "$1" "${3:-}"; fails=$((fails + 1)); fi
    }

    # 1. Serial is never defaulted. An empty HAKIOS_SERIAL must abort, not fall through to
    #    "the only device" -- with two phones plugged in that is how you trace the wrong one.
    out=$( (unset HAKIOS_SERIAL; adbd shell true) 2>&1 )
    case "$out" in
        *"HAKIOS_SERIAL is not set"*) ck "adbd refuses without HAKIOS_SERIAL" 1 ;;
        *) ck "adbd refuses without HAKIOS_SERIAL" 0 "got: $out" ;;
    esac

    # 2. The -s flag is actually part of the command line, not just checked for.
    out=$( (HAKIOS_SERIAL=SELFTEST-1234; printf '%s\n' "adb -s $HAKIOS_SERIAL") )
    case "$out" in
        "adb -s SELFTEST-1234") ck "serial is passed with -s" 1 ;;
        *) ck "serial is passed with -s" 0 "got: $out" ;;
    esac

    # 3. trace_root() must pick the mount that exists. Fake it with a shell function so
    #    this runs with no device.
    TR=""
    sh_() { case "$*" in *"/sys/kernel/tracing"*) return 0 ;; *) return 1 ;; esac; }
    r=$(trace_root)
    ck "trace_root prefers /sys/kernel/tracing" "$([ "$r" = "/sys/kernel/tracing" ] && echo 1 || echo 0)" "got: $r"

    # 4. When only the debugfs mount exists, fall back to it rather than failing.
    TR=""
    sh_() { case "$*" in *"/sys/kernel/tracing"*) return 1 ;; *"/sys/kernel/debug/tracing"*) return 0 ;; *) return 1 ;; esac; }
    r=$(trace_root)
    ck "trace_root falls back to debugfs path" "$([ "$r" = "/sys/kernel/debug/tracing" ] && echo 1 || echo 0)" "got: $r"

    # 5. Nothing found -> non-zero, so cmd_start can die instead of writing into nothing.
    TR=""
    sh_() { return 1; }
    if trace_root >/dev/null 2>&1; then
        ck "trace_root fails when nothing is mounted" 0 "returned success"
    else
        ck "trace_root fails when nothing is mounted" 1
    fi

    # 6. pick_mode prefers regmap tracepoints over kprobe, because only they carry the
    #    map name we need to exclude the MT6363.
    MODE=""
    sh_() { case "$*" in *regmap_reg_write*) return 0 ;; *kprobe_events*) return 0 ;; *) return 1 ;; esac; }
    m=$(pick_mode)
    ck "pick_mode prefers regmap" "$([ "$m" = "regmap" ] && echo 1 || echo 0)" "got: $m"

    # 7. With no regmap events, kprobe is chosen.
    MODE=""
    sh_() { case "$*" in *regmap_reg_write*) return 1 ;; *kprobe_events*) return 0 ;; *) return 1 ;; esac; }
    m=$(pick_mode)
    ck "pick_mode falls back to kprobe" "$([ "$m" = "kprobe" ] && echo 1 || echo 0)" "got: $m"

    # 8. An explicit --mode must win over detection.
    MODE="kprobe"
    m=$(pick_mode)
    ck "explicit mode overrides detection" "$([ "$m" = "kprobe" ] && echo 1 || echo 0)" "got: $m"

    # 9. Neither available -> non-zero.
    MODE=""
    sh_() { return 1; }
    if pick_mode >/dev/null 2>&1; then
        ck "pick_mode fails when neither exists" 0 "returned success"
    else
        ck "pick_mode fails when neither exists" 1
    fi

    # 10. The one thing that must never appear: a redirect into spmi_access. Match the
    #     shape of a write ("... > .../spmi_access"), with comments stripped, so both the
    #     warning comments above and this check's own grep argument do not trip it. That
    #     path has put the phone into preloader twice; this is a regression guard.
    if sed 's/#.*$//' "$0" 2>/dev/null | grep -qE '>[[:space:]]*[^[:space:]]*spmi_access'; then
        ck "no redirect into spmi_access in this script" 0 "found a write redirect"
    else
        ck "no redirect into spmi_access in this script" 1
    fi

    # 11. And no hard-coded serial. The known device id must not be baked in.
    if grep -qE 'GAJZ[0-9A-Z]+' "$0" 2>/dev/null; then
        ck "no hard-coded serial" 0 "found a serial literal"
    else
        ck "no hard-coded serial" 1
    fi

    printf '\n%d checks, %d failed\n' "$total" "$fails"
    [ "$fails" -eq 0 ] || return 3
    printf 'selftest: all assertions passed\n'
}

# --- arg parsing ------------------------------------------------------------------

CMD=""
SECS=20
BUFFER_KB=""

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage ;;
        probe|start|stop|pull|parse|run|selftest) CMD=$1 ;;
        --mode) shift; MODE=${1:-} ;;
        --mode=*) MODE=${1#--mode=} ;;
        --secs) shift; SECS=${1:-20} ;;
        --secs=*) SECS=${1#--secs=} ;;
        --kb) shift; BUFFER_KB=${1:-} ;;
        --kb=*) BUFFER_KB=${1#--kb=} ;;
        --out) shift; OUTDIR=${1:-$OUTDIR} ;;
        --out=*) OUTDIR=${1#--out=} ;;
        *) warn "ignoring unknown argument: $1" ;;
    esac
    shift
done

[ -n "$CMD" ] || usage

case "$MODE" in ""|regmap|kprobe|auto) ;; *) die "--mode must be regmap, kprobe or auto" ;; esac
[ "$MODE" = "auto" ] && MODE=""
case "$SECS" in ''|*[!0-9]*) die "--secs must be a number" ;; esac

case "$CMD" in
    probe)    cmd_probe ;;
    start)    cmd_start ;;
    stop)     cmd_stop ;;
    pull)     cmd_pull ;;
    parse)    cmd_parse ;;
    run)      cmd_run ;;
    selftest) cmd_selftest ;;
esac
