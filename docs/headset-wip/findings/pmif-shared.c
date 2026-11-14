/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pmif-shared.c -- the one PMIF/SPMI transaction path on SWINF_2 (see pmif-shared.h).
 *
 * DRAFT. Lives beside the header under docs/headset-wip/ until the fixes land.
 *
 * Serialisation: hakios_spmi_lock() / hakios_spmi_unlock(), the lock this port already has
 * for "one transfer at a time through the PMIC arbiter for this system, whoever asks"
 * (drivers/core/hakios.platform.c:27-44), taken once around one transaction. Reusing it is
 * the whole point: a second lock of our own would not be exclusive with the buttons, the
 * PMIC survey, the torch and the camera, which already take that one around their arbiter
 * transfers, and two locks that do not exclude each other are no lock at all.
 *
 * Locked region: one ACC write and up to three STA polls. It never sleeps. delay() and
 * IOSleep() are absent from this file by construction -- both block when the preemption
 * level is zero (osfmk/kern/clock.c:1585-1601) and a blocking call under a spin lock taken
 * with lck_spin_lock, which raises the preemption level, would either spin the core or
 * deadlock. The wait is a deadline read from mach_absolute_time plus a hard spin ceiling,
 * whichever comes first; the worst case for one transaction is PMIF_POLL_BUDGET_US (100 ms)
 * and the lock's own panic timeout is 524 ms (osfmk/arm/locks_arm.c:106), so a transaction
 * that overruns is caught by the timeout rather than by the lock.
 */
#include <dev/hakios.pmif.h>

#include <kern/clock.h>
#include <libkern/libkern.h>
#include <sys/hakios.input.h>           /* hakios_spmi_lock() / hakios_spmi_unlock() */
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

/*
 * The barrier between a store to the channel and the status read that follows it. On arm64
 * -- the only place this file is built for the kernel -- that is `dsb sy`, the same
 * instruction all three drivers used and stronger than readl()/writel(). The host build
 * (pmif-fix-check.py --selftest, and a plain gcc -c) has no such instruction, so the
 * portable fence stands in: the host build is for type checking and the state machine, not
 * for measuring ordering.
 */
#if defined(__aarch64__) || defined(__arm64__)
#define PMIF_BARRIER()  __asm__ volatile ("dsb sy" ::: "memory")
#else
#define PMIF_BARRIER()  __atomic_thread_fence(__ATOMIC_SEQ_CST)
#endif

/* ==================================================================== state */

static vm_offset_t pmif_base;
static uint32_t pmif_sta_before;
static uint32_t pmif_sta_after;

/*
 * The mapping is made once. Two workers reach pmif_init() on the default boot path -- the
 * keys worker, and the GPU's 2 s later -- so the check and the store are both under the
 * lock rather than under an atomic of their own: it costs nothing (the mapping happens
 * once) and it is one fewer thing to reason about.
 */
pmif_status_t
pmif_init(void)
{
	pmif_status_t status = PMIF_OK;

	hakios_spmi_lock();
	if (pmif_base == 0) {
		pmif_base = ml_io_map(PMIF_BASE, PMIF_SIZE_MAPPED);
		if (pmif_base == 0) {
			status = PMIF_E_DEAD;
		}
	}
	hakios_spmi_unlock();
	return status;
}

/* ==================================================================== MMIO */

/*
 * One dword of the channel, or 0xffffffff for an offset that is not the channel's. The
 * off-spec offsets are a programming error, not a runtime condition, and returning a value
 * that fails every comparison is a cheaper way to notice than a store to some other block.
 *
 * dsb sy on both sides: the ACC store has to be out at the device before the STA poll that
 * follows can mean anything (SPEC §4.5), and the load needs its result before the next
 * decision. This is what all three drivers already did.
 */
/*
 * A seam for the host build. PMIF_MMIO_TEST is defined only by pmif-fix-check.py's
 * --selftest build and by nothing in the kernel, where the two volatile accesses below are
 * what actually happens. With it defined, the whole state machine -- the idle recovery, the
 * ack, the four failure codes -- runs against a model of the device on the development
 * machine, which is the only way to test any of it without a phone.
 */
#ifndef PMIF_MMIO_TEST
#define PMIF_MMIO_TEST 0
#endif

#if PMIF_MMIO_TEST
extern uint32_t pmif_test_rd(uint32_t at);
extern void pmif_test_wr(uint32_t at, uint32_t value);

static inline uint32_t
pmif_load(uint32_t at)
{
	return pmif_test_rd(at);
}

static inline void
pmif_store(uint32_t at, uint32_t value)
{
	pmif_test_wr(at, value);
}
#else
static inline uint32_t
pmif_load(uint32_t at)
{
	uint32_t value = *(volatile uint32_t *)(pmif_base + at);

	PMIF_BARRIER();
	return value;
}

static inline void
pmif_store(uint32_t at, uint32_t value)
{
	PMIF_BARRIER();
	*(volatile uint32_t *)(pmif_base + at) = value;
	PMIF_BARRIER();
}
#endif

static inline uint32_t
pmif_rd(uint32_t at)
{
	/* An offset outside the window is a programming error, not a runtime condition. */
	return pmif_offset_ok(at) ? pmif_load(at) : 0xffffffffu;
}

static inline void
pmif_wr(uint32_t at, uint32_t value)
{
	if (pmif_offset_ok(at)) {
		pmif_store(at, value);
	}
}

/* ==================================================================== polling */

/*
 * Wait until the channel's FSM reads `state`.
 *
 * Two bounds, both real, whichever expires first:
 *   - PMIF_SPIN_HARD_CAP reads. This is the bare-metal fallback: the clock is not the thing
 *     being tested here, and a machine whose counter does not advance must still not hang.
 *   - PMIF_POLL_BUDGET_US of wall time from mach_absolute_time.
 *
 * The interval is not slept. PMIF_POLL_INTERVAL_US names the vendor's cadence and is kept
 * for the log, but sleeping between polls would mean sleeping inside the lock, and the
 * hardware's own synchronisation point is the FSM field, not a delay (SPEC §4.3).
 *
 * On failure the FSM value that was last read is left in pmif_sta_after, which is what the
 * caller's error code is derived from.
 */
static bool
pmif_wait(uint32_t state)
{
	uint64_t deadline, now;
	unsigned spins;
	uint32_t sta;

	clock_interval_to_deadline(PMIF_POLL_BUDGET_US, NSEC_PER_USEC, &deadline);
	for (spins = 0; spins < PMIF_SPIN_HARD_CAP; spins++) {
		sta = pmif_rd(PMIF_SWINF_2_STA);
		pmif_sta_after = sta;
		if (PMIF_STA_FSM(sta) == state) {
			return true;
		}
		/*
		 * ERR is checked here rather than after the wait: a NAK parks the FSM at WFDLE
		 * (4) and sets bit 18, and waiting the whole budget for a state that will never
		 * come is 100 ms of a 20 ms poll period spent on a decision already made.
		 */
		if ((sta & PMIF_STA_ERR) != 0u) {
			return false;
		}
		now = mach_absolute_time();
		if (now >= deadline) {
			return false;
		}
	}
	return false;
}

/* Why a wait failed, from the FSM and the error bit that were last read. */
static pmif_status_t
pmif_wait_failure(void)
{
	uint32_t sta = pmif_sta_after;
	uint32_t fsm = PMIF_STA_FSM(sta);

	if ((sta & PMIF_STA_ERR) != 0u || fsm == PMIF_FSM_WFDLE) {
		return PMIF_E_NAK;
	}
	if (fsm == PMIF_FSM_WFVLDCLR) {
		/* Someone (possibly this very transaction) left a valid word waiting for its ACK. */
		return PMIF_E_STUCK;
	}
	return PMIF_E_TIMEOUT;
}

/*
 * Acknowledge a waiting valid word, if there is one, and wait for IDLE.
 *
 * This is the recovery the SPEC calls the only safe one (§5.3): it touches VLD_CLR and
 * nothing else. There is no SW_RST in this block, and ARB_EN / CMDISSUE_EN / INF_EN /
 * TIMER_CTRL are global to every channel and every consumer.
 */
static pmif_status_t
pmif_ack_locked(void)
{
	uint32_t sta = pmif_rd(PMIF_SWINF_2_STA);

	pmif_sta_after = sta;
	if (PMIF_STA_FSM(sta) == PMIF_FSM_IDLE) {
		return PMIF_OK;
	}
	if (PMIF_STA_FSM(sta) != PMIF_FSM_WFVLDCLR) {
		/*
		 * Not ours to touch: another transaction is in flight on another core, or the
		 * slave has parked the channel at WFDLE. Acking either would be lying about it.
		 */
		return PMIF_E_BUSY;
	}
	pmif_wr(PMIF_SWINF_2_VLD_CLR, PMIF_VLDCLR_ACK);
	if (!pmif_wait(PMIF_FSM_IDLE)) {
		return PMIF_E_STUCK;
	}
	return PMIF_OK;
}

/* ==================================================================== accessors */

/*
 * The idle wait every transaction starts with, including the recovery the SPEC asks for:
 * if the FSM is at WFVLDCLR, ack it and try once more, because the read that left it there
 * is over -- its data is gone and nobody is going to collect it (SPEC §4.4, [U]:379-386).
 */
static pmif_status_t
pmif_wait_idle_locked(void)
{
	uint32_t sta;

	sta = pmif_rd(PMIF_SWINF_2_STA);
	pmif_sta_before = sta;
	pmif_sta_after = sta;
	if (PMIF_STA_FSM(sta) == PMIF_FSM_IDLE) {
		return PMIF_OK;
	}
	if (PMIF_STA_FSM(sta) == PMIF_FSM_WFVLDCLR) {
		pmif_wr(PMIF_SWINF_2_VLD_CLR, PMIF_VLDCLR_ACK);
		if (pmif_wait(PMIF_FSM_IDLE)) {
			return PMIF_OK;
		}
		return PMIF_E_STUCK;
	}
	if (!pmif_wait(PMIF_FSM_IDLE)) {
		return pmif_wait_failure();
	}
	return PMIF_OK;
}

pmif_status_t
pmif_read8(uint8_t sid, uint16_t addr, uint8_t *out)
{
	pmif_status_t status;
	uint32_t sta;
	uint8_t byte;

	if (out == NULL) {
		return PMIF_E_ARG;
	}
	status = pmif_hdr_admit(sid, addr, false);
	if (status != PMIF_OK) {
		return status;
	}
	hakios_spmi_lock();
	if (pmif_base == 0) {
		hakios_spmi_unlock();
		return PMIF_E_DEAD;
	}
	status = pmif_wait_idle_locked();
	if (status != PMIF_OK) {
		hakios_spmi_unlock();
		return status;
	}
	pmif_wr(PMIF_SWINF_2_ACC, PMIF_CMD_READ8(sid, addr));
	if (!pmif_wait(PMIF_FSM_WFVLDCLR)) {
		status = pmif_wait_failure();
		(void)pmif_ack_locked();        /* a late valid word must not outlive the failure */
		hakios_spmi_unlock();
		return status;
	}
	/*
	 * RDATA first, ACK second: the ACK retires the valid word and with it the data. Reading
	 * after the ack is the mistake that returns the previous byte.
	 */
	byte = (uint8_t)(pmif_rd(PMIF_SWINF_2_RDATA) & 0xffu);
	pmif_wr(PMIF_SWINF_2_VLD_CLR, PMIF_VLDCLR_ACK);
	if (!pmif_wait(PMIF_FSM_IDLE)) {
		sta = pmif_sta_after;
		status = (PMIF_STA_FSM(sta) == PMIF_FSM_WFVLDCLR) ? PMIF_E_STUCK : PMIF_E_ACK;
		(void)pmif_ack_locked();
		hakios_spmi_unlock();
		return status;
	}
	pmif_sta_after = pmif_rd(PMIF_SWINF_2_STA);
	hakios_spmi_unlock();
	*out = byte;                            /* staged: a failure never writes through out */
	return PMIF_OK;
}

pmif_status_t
pmif_write8(uint8_t sid, uint16_t addr, uint8_t val)
{
	pmif_status_t status;

	status = pmif_hdr_admit(sid, addr, true);
	if (status != PMIF_OK) {
		return status;
	}
	hakios_spmi_lock();
	if (pmif_base == 0) {
		hakios_spmi_unlock();
		return PMIF_E_DEAD;
	}
	status = pmif_wait_idle_locked();
	if (status != PMIF_OK) {
		hakios_spmi_unlock();
		return status;
	}
	pmif_wr(PMIF_SWINF_2_WDATA, (uint32_t)val);
	pmif_wr(PMIF_SWINF_2_ACC, PMIF_CMD_WRITE8(sid, addr));
	/*
	 * A write completes at IDLE and produces no valid read data, so there is no WFVLDCLR to
	 * wait for and no ACK to send ([U]:464-471). Waiting for WFVLDCLR here is the defect
	 * that made every write to 0x241 report a timeout after a full budget while the write
	 * itself had landed.
	 */
	if (!pmif_wait(PMIF_FSM_IDLE)) {
		status = pmif_wait_failure();
		(void)pmif_ack_locked();
		hakios_spmi_unlock();
		return status;
	}
	pmif_sta_after = pmif_rd(PMIF_SWINF_2_STA);
	hakios_spmi_unlock();
	return PMIF_OK;
}

/* ==================================================================== recovery */

pmif_status_t
pmif_recover(void)
{
	pmif_status_t status;

	hakios_spmi_lock();
	status = (pmif_base == 0) ? PMIF_E_DEAD : pmif_ack_locked();
	hakios_spmi_unlock();
	return status;
}

/*
 * The quiesce path: no lock, no wait, one write at most.
 *
 * loader_return() runs this after psci_smp_stop_others(), which returns with interrupts
 * off on this core (osfmk/arm64/psci_smp.c:297, DAIFSet DAIFSC_ALL). Two things follow:
 *
 *   - the shared lock must not be taken. It may be held by a core that has just been
 *     switched off, and lck_spin_lock spins until lock-panic, 524 ms
 *     (osfmk/arm/locks_arm.c:106) then panics; there is no "give up" return from it.
 *   - the wait must be short and must not depend on anything another core owns. One STA
 *     read and, if it says WFVLDCLR, one VLD_CLR write: a store to a device register is
 *     this core's business alone.
 *
 * What is lost: if the FSM is mid-transaction on a stopped core the ack does not help, and
 * the configuration restore that follows does nothing. That is a headset that does not
 * work in the next boot's log, against a phone that does not return to its loader.
 */
void
pmif_recover_besteffort(void)
{
	if (pmif_base == 0) {
		return;
	}
	if (PMIF_STA_FSM(pmif_rd(PMIF_SWINF_2_STA)) == PMIF_FSM_WFVLDCLR) {
		pmif_wr(PMIF_SWINF_2_VLD_CLR, PMIF_VLDCLR_ACK);
	}
}

/* ==================================================================== diagnostics */

void
pmif_last_sta(uint32_t *before, uint32_t *after)
{
	hakios_spmi_lock();
	if (before != NULL) {
		*before = pmif_sta_before;
	}
	if (after != NULL) {
		*after = pmif_sta_after;
	}
	hakios_spmi_unlock();
}

const char *
pmif_status_name(pmif_status_t status)
{
	switch (status) {
	case PMIF_OK:              return "ok";
	case PMIF_E_SID:           return "SID not on the bus";
	case PMIF_E_OFFSET:        return "PMIF offset outside 0x000..0x8fe";
	case PMIF_E_REG:           return "PMIC register is write-forbidden";
	case PMIF_E_CMD:           return "command word is not a single-byte extended transfer";
	case PMIF_E_ARG:           return "bad argument";
	case PMIF_E_BUSY:          return "channel busy, and not with a valid word to ack";
	case PMIF_E_NAK:           return "the slave did not answer (NAK)";
	case PMIF_E_TIMEOUT:       return "no WFVLDCLR inside the budget";
	case PMIF_E_STUCK:         return "parked at WFVLDCLR and the ack did not retire it";
	case PMIF_E_ACK:           return "acked, but the channel did not return to IDLE";
	case PMIF_E_DEAD:          return "the PMIF window is not mapped";
	}
	return "unknown PMIF status";
}
