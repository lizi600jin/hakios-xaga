#!/bin/sh
# lock-probe.sh -- re-verify, from the sources, when XNU's locking primitives become
# usable, and which of them the project already uses.
#
# Read only. Prints to stdout. Exit 0 when every probe ran, 1 when a required
# path is missing or a grep of a required fact came back empty.
#
# Usage:
#   sh docs/headset-wip/lock-probe.sh                 # default tree locations
#   XNU_SRC=/path/to/xnu sh docs/headset-wip/lock-probe.sh
#   PROJECT=/path/to/hakios-mtk sh docs/headset-wip/lock-probe.sh
#
# The paths below are the ones this project uses; override with the environment.

set -u

PROJECT=${PROJECT:-/home/lizi/IOS/hakios-mtk}
XNU_SRC=${XNU_SRC:-$PROJECT/xnu-src/xnu-8796.121.2}
PATCHED=${PATCHED:-$PROJECT/xnu/files}
DRIVERS=${DRIVERS:-$PROJECT/mtk/mt6895/drivers}

fail=0
missing=0

say() { printf '%s\n' "$*"; }
head2() { printf '\n== %s ==\n' "$*"; }

need_dir() {
	if [ ! -d "$1" ]; then
		say "MISSING DIRECTORY: $1"
		missing=$((missing + 1))
		return 1
	fi
	return 0
}

need_file() {
	if [ ! -f "$1" ]; then
		say "MISSING FILE: $1"
		missing=$((missing + 1))
		return 1
	fi
	return 0
}

# grep -n with a bound line count; prints a note when nothing matches.
show() {
	desc=$1
	pattern=$2
	shift 2
	printf -- '-- %s\n' "$desc"
	out=$(grep -n -- "$pattern" "$@" 2>/dev/null | head -24)
	if [ -z "$out" ]; then
		say '   (no match)'
		fail=$((fail + 1))
	else
		printf '%s\n' "$out" | sed 's/^/   /'
	fi
}

# A grep that is allowed to come back empty (a negative result is the finding).
show_allow_empty() {
	desc=$1
	pattern=$2
	shift 2
	printf -- '-- %s\n' "$desc"
	out=$(grep -n -- "$pattern" "$@" 2>/dev/null | head -24)
	if [ -z "$out" ]; then
		say '   (no match -- this is the expected result and is the evidence)'
	else
		printf '%s\n' "$out" | sed 's/^/   /'
	fi
}

say "lock-probe: XNU lock availability, read only"
say "date:    $(date '+%Y-%m-%dT%H:%M:%S%z')"
say "project: $PROJECT"
say "xnu:     $XNU_SRC"
say "patched: $PATCHED"
say "drivers: $DRIVERS"

need_dir "$PROJECT" || true
need_dir "$XNU_SRC" || true
need_dir "$PATCHED" || true
need_dir "$DRIVERS" || true

# ---------------------------------------------------------------------------
head2 "1. where the three drivers are called from, and in what order"
# ---------------------------------------------------------------------------

INPUT_C=
for c in "$PROJECT/drivers/input/hakios.input.c" \
         "$PATCHED/bsd/dev/hakios.input.c" \
         "$XNU_SRC/bsd/dev/hakios.input.c"; do
	if [ -f "$c" ]; then
		INPUT_C=$c
		break
	fi
done
printf -- '-- hakios.input.c found at: %s\n' "${INPUT_C:-(none)}"
if need_file "${INPUT_C:-/nonexistent}"; then
	show "hakios_input_init definition and the drivers it starts" \
		'hakios_input_init\|hakios_mt6895_keys_init\|hakios_mali610_init\|hakios_mt6895_headset_init' "$INPUT_C"
fi

BSD_INIT=$XNU_SRC/bsd/kern/bsd_init.c
if need_file "$BSD_INIT"; then
	show "bsd_init: the call sites of the hakios initialisers (the boot stage)" \
		'hakios_input_init\|devfs_kernel_mount' "$BSD_INIT"
fi

STARTUP_C=$XNU_SRC/osfmk/kern/startup.c
if need_file "$STARTUP_C"; then
	show "kernel_bootstrap -> kernel_bootstrap_thread -> bsd_init" \
		'bsd_init();\|STARTUP_SUB_LOCKDOWN\|spllo' "$STARTUP_C"
fi

# ---------------------------------------------------------------------------
head2 "2. the startup phase order (LOCKS before ZALLOC: this is the whole answer)"
# ---------------------------------------------------------------------------

STARTUP_H=$XNU_SRC/osfmk/kern/startup.h
if need_file "$STARTUP_H"; then
	show "startup_subsystem_id_t, in order" \
		'STARTUP_SUB_' "$STARTUP_H"
fi

# ---------------------------------------------------------------------------
head2 "3. how a statically declared lock initialises itself (no zalloc)"
# ---------------------------------------------------------------------------

LOCK_GRP_H=$XNU_SRC/osfmk/kern/lock_group.h
if need_file "$LOCK_GRP_H"; then
	show "LCK_GRP_DECLARE -> STARTUP(LOCKS, RANK_SECOND, lck_grp_startup_init)" \
		'LCK_GRP_DECLARE_ATTR\|lck_grp_startup_init\|LCK_GRP_USE_ARG' "$LOCK_GRP_H"
fi

LOCK_MTX_H=$XNU_SRC/osfmk/kern/lock_mtx.h
if need_file "$LOCK_MTX_H"; then
	show "LCK_MTX_DECLARE -> STARTUP(LOCKS, RANK_FOURTH, lck_mtx_startup_init)" \
		'LCK_MTX_DECLARE_ATTR\|lck_mtx_startup_init' "$LOCK_MTX_H"
fi

LOCKS_H=$XNU_SRC/osfmk/kern/locks.h
if need_file "$LOCKS_H"; then
	show "LCK_SPIN_DECLARE -> STARTUP(LOCKS, RANK_FOURTH, lck_spin_startup_init)" \
		'LCK_SPIN_DECLARE_ATTR\|lck_spin_startup_init\|LCK_ATTR_DECLARE' "$LOCKS_H"
fi

LOCKS_C=$XNU_SRC/osfmk/kern/locks.c
if need_file "$LOCKS_C"; then
	show "the startup trampolines that call lck_*_init" \
		'lck_spin_startup_init\|lck_mtx_startup_init' "$LOCKS_C"
fi

# ---------------------------------------------------------------------------
head2 "4. dynamic init: what lck_*_alloc_init needs, and whether grp may be NULL"
# ---------------------------------------------------------------------------

LOCK_MTX_C=$XNU_SRC/osfmk/kern/lock_mtx.c
if need_file "$LOCK_MTX_C"; then
	show "lck_mtx_alloc_init uses zalloc(KT_LCK_MTX)" \
		'lck_mtx_alloc_init\|zalloc(KT_LCK_MTX)\|KALLOC_TYPE_DEFINE(KT_LCK_MTX' "$LOCK_MTX_C"
	printf -- '-- the body of lck_mtx_init: does it test grp for NULL?\n'
	sed -n '/^lck_mtx_init(/,/^}/p' "$LOCK_MTX_C" | sed 's/^/   /'
fi

LOCK_GRP_C=$XNU_SRC/osfmk/kern/lock_group.c
if need_file "$LOCK_GRP_C"; then
	show "lck_grp_alloc_init uses zalloc(KT_LCK_GRP); lck_group_init is STARTUP(LOCKS, FIRST)" \
		'lck_grp_alloc_init\|zalloc(KT_LCK_GRP)\|lck_group_init\|STARTUP(LOCKS' "$LOCK_GRP_C"
fi

LOCKS_ARM_C=$XNU_SRC/osfmk/arm/locks_arm.c
if need_file "$LOCKS_ARM_C"; then
	show "lck_spin_init: grp is guarded by if (grp); KT_LCK_SPIN for the dynamic form" \
		'lck_spin_alloc_init\|zalloc(KT_LCK_SPIN)\|^lck_spin_init\|if (grp)' "$LOCKS_ARM_C"
	show "arm64 lck_spin_lock is a ticket lock" \
		'^lck_spin_lock\|lck_ticket_lock\|LCK_SPIN_IS_TICKET_LOCK' "$LOCKS_ARM_C"
fi

LOCK_TICKET_C=$XNU_SRC/osfmk/kern/lock_ticket.c
if need_file "$LOCK_TICKET_C"; then
	show "lck_ticket_init: grp guarded; lck_ticket_lock disables preemption itself" \
		'^lck_ticket_init\|if (grp)\|^lck_ticket_lock\|lock_disable_preemption_for_thread' "$LOCK_TICKET_C"
fi

# ---------------------------------------------------------------------------
head2 "5. the runtime invariants: mutex in IRQ, mutex with preemption off, delay()"
# ---------------------------------------------------------------------------

if need_file "$LOCK_MTX_C"; then
	show "mutex panics in IRQ context and with preemption disabled (DEVELOPMENT||DEBUG)" \
		'lck_mtx_check_irq\|__lck_mtx_at_irq_panic\|lck_mtx_check_preemption\|startup_phase < STARTUP_SUB_EARLY_BOOT' "$LOCK_MTX_C"
fi

CLOCK_C=$XNU_SRC/osfmk/kern/clock.c
if need_file "$CLOCK_C"; then
	show "delay() spins or blocks, and under which conditions" \
		'^delay(\|machine_delay_until\|thread_block(THREAD_CONTINUE_NULL)\|get_preemption_level() != 0' "$CLOCK_C"
fi

MROUT_C=$XNU_SRC/osfmk/arm/machine_routines_common.c
if need_file "$MROUT_C"; then
	show "ml_set_interrupts_enabled: DAIF only, no initialisation dependency" \
		'^ml_set_interrupts_enabled\|builtin_arm_rsr("DAIF")\|DAIFClr\|DAIFSet' "$MROUT_C"
fi

MROUT_ARM64=$XNU_SRC/osfmk/arm64/machine_routines.c
if need_file "$MROUT_ARM64"; then
	show "how long a ticket spin may be held before it panics (TLockTimeOut)" \
		'TLockTimeOut' "$MROUT_ARM64"
fi

LOCKS_ARM_C=$XNU_SRC/osfmk/arm/locks_arm.c
if need_file "$LOCKS_ARM_C"; then
	show "the mutex interlock panic budget (lock-panic, ~524ms)" \
		'lock_panic_timeout' "$LOCKS_ARM_C"
fi

LOCKS_C=$XNU_SRC/osfmk/kern/locks.c
if need_file "$LOCKS_C"; then
	show "spinlock_timeout_panic defaults to TRUE; -no_slto_panic clears it" \
		'spinlock_timeout_panic' "$LOCKS_C" "$STARTUP_C"
fi

# ---------------------------------------------------------------------------
head2 "6. what the project already uses"
# ---------------------------------------------------------------------------

for d in "$DRIVERS" "$PROJECT/drivers" "$PROJECT/xnu/files"; do
	[ -d "$d" ] || continue
	printf -- '-- lck_* / hw_lock / IOCatalogue locks under %s (per-file counts)\n' "$d"
	out=$(grep -rn 'lck_mtx\|lck_spin\|IOLock\|IOSimpleLock\|hw_lock\|LCK_MTX_DECLARE\|LCK_SPIN_DECLARE\|LCK_GRP_DECLARE' \
		"$d" --include='*.c' --include='*.h' --include='*.cpp' 2>/dev/null |
		sed 's/:.*//' | sort | uniq -c | sort -rn | head -20)
	if [ -z "$out" ]; then
		say '   (no match)'
	else
		printf '%s\n' "$out" | sed 's/^/   /'
	fi
done

PLATFORM_C=$PROJECT/drivers/core/hakios.platform.c
[ -f "$PLATFORM_C" ] || PLATFORM_C=$PATCHED/bsd/dev/hakios.platform.c
if need_file "$PLATFORM_C"; then
	show "the shared PMIC-arbiter lock the project already has" \
		'LCK_GRP_DECLARE\|LCK_SPIN_DECLARE\|hakios_spmi_lock\|hakios_spmi_unlock' "$PLATFORM_C"
fi

# ---------------------------------------------------------------------------
head2 "7. the three drivers that share SWINF_2: do any of them take a lock?"
# ---------------------------------------------------------------------------

for f in "$DRIVERS/input/hakios.mt6895_keys.c" \
         "$DRIVERS/audio/hakios.mt6895_headset.c" \
         "$DRIVERS/gpu/hakios.mali610.c"; do
	base=$(basename "$f")
	if [ ! -f "$f" ]; then
		say "   MISSING FILE: $f"
		missing=$((missing + 1))
		continue
	fi
	n=$(grep -c 'lck_mtx\|lck_spin\|hw_lock\|hakios_spmi_lock\|IOSimpleLock' "$f" 2>/dev/null)
	printf -- '-- %s: %s lock-primitive occurrences\n' "$base" "${n:-0}"
	grep -n 'SWINF_ACC\|PMIF_SWINF_ACC\|MALI_PMIF_ACC\|PMIF_BASE' "$f" 2>/dev/null |
		head -6 | sed 's/^/   /'
	grep -n 'kernel_thread_start\|delay(2000\|for (;;)' "$f" 2>/dev/null |
		head -6 | sed 's/^/   /'
done

# ---------------------------------------------------------------------------
head2 "8. the header both the drivers and platform.c see"
# ---------------------------------------------------------------------------

HDR=$PATCHED/bsd/sys/hakios.input.h
if need_file "$HDR"; then
	show "hakios_spmi_lock is declared for the drivers" \
		'hakios_spmi_lock\|hakios_spmi_unlock' "$HDR"
fi

# ---------------------------------------------------------------------------
printf '\n== summary ==\n'
say "probes with no match (unexpected): $fail"
say "missing paths:                    $missing"
if [ "$fail" -eq 0 ] && [ "$missing" -eq 0 ]; then
	say "result: every probe ran and every required fact was found"
	exit 0
fi
say "result: something moved; read the sections above"
exit 1
