/*
 * MT6895 PMIF/SWINF_2. All consumers use the existing platform SPMI lock.
 * One-byte transfers to SID 4/5 and the five SWINF_2 offsets only.
 * No allocation, logging or sleeping while holding the transaction spin lock.
 */
#include <sys/hakios.pmif.h>
#include <sys/hakios.input.h>
#include <kern/clock.h>
#include <mach/mach_types.h>
#include <mach/vm_param.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern void ml_io_unmap(vm_offset_t address, vm_size_t size);

#if defined(__aarch64__) || defined(__arm64__)
#define PMIF_BARRIER() __asm__ volatile ("dsb sy" ::: "memory")
#define PMIF_RELAX() __asm__ volatile ("yield" ::: "memory")
#else
#define PMIF_BARRIER() __atomic_thread_fence(__ATOMIC_SEQ_CST)
#define PMIF_RELAX() __atomic_signal_fence(__ATOMIC_SEQ_CST)
#endif

static vm_offset_t pmif_base;
static uint32_t pmif_sta_before, pmif_sta_after;

pmif_status_t
pmif_init(void)
{
	vm_offset_t candidate;
	bool ready;

	hakios_spmi_lock();
	ready = pmif_base != 0;
	hakios_spmi_unlock();
	if (ready) {
		return PMIF_OK;
	}
	/* Mapping can allocate/sleep. A concurrent first mapper is handled below. */
	candidate = ml_io_map(PMIF_BASE, PMIF_SIZE_DECLARED);
	hakios_spmi_lock();
	if (pmif_base == 0) {
		pmif_base = candidate;
		candidate = 0;
	}
	ready = pmif_base != 0;
	hakios_spmi_unlock();
	if (candidate != 0) {
		/* Free a losing mapping outside the lock, with the rounded VM size. */
		ml_io_unmap(candidate, round_page((vm_size_t)PMIF_SIZE_DECLARED));
	}
	return ready ? PMIF_OK : PMIF_E_DEAD;
}

/* The model replaces raw loads/stores; admission, locks and the FSM are real. */
#if defined(PMIF_MMIO_TEST) && PMIF_MMIO_TEST
extern uint32_t pmif_test_rd(uint32_t at);
extern void pmif_test_wr(uint32_t at, uint32_t value);
#endif

static uint32_t
pmif_rd(uint32_t at)
{
	uint32_t value;

	if (at != PMIF_SWINF_2_STA && at != PMIF_SWINF_2_RDATA) {
		return 0xffffffffu;
	}
	PMIF_BARRIER();
#if defined(PMIF_MMIO_TEST) && PMIF_MMIO_TEST
	value = pmif_test_rd(at);
#else
	value = *(volatile uint32_t *)(pmif_base + at);
#endif
	PMIF_BARRIER();
	return value;
}

static void
pmif_wr(uint32_t at, uint32_t value)
{
	if (at != PMIF_SWINF_2_ACC && at != PMIF_SWINF_2_WDATA &&
	    at != PMIF_SWINF_2_VLD_CLR) {
		return;
	}
	PMIF_BARRIER();
#if defined(PMIF_MMIO_TEST) && PMIF_MMIO_TEST
	pmif_test_wr(at, value);
#else
	*(volatile uint32_t *)(pmif_base + at) = value;
#endif
	PMIF_BARRIER();
}

struct pmif_budget {
	uint64_t deadline, interval;
	unsigned polls;
};

static struct pmif_budget
pmif_budget_begin(void)
{
	struct pmif_budget budget;

	clock_interval_to_deadline(PMIF_POLL_BUDGET_US, NSEC_PER_USEC, &budget.deadline);
	nanoseconds_to_absolutetime(PMIF_POLL_INTERVAL_US * NSEC_PER_USEC, &budget.interval);
	budget.polls = 0;
	return budget;
}

/*
 * Busy cadence, never delay()/sleep under this spin lock. The clock-only pause
 * is also finite: a frozen counter cannot defeat the outer bound. The 100ms
 * budget covers polling; a finite STA/ACK cleanup can follow its expiration.
 * No software deadline can bound an MMIO access that itself never returns.
 */
static bool
pmif_poll_pause(const struct pmif_budget *budget)
{
	uint64_t next = mach_absolute_time() + budget->interval;

	for (unsigned n = 0; n < PMIF_SPIN_HARD_CAP; n++) {
		uint64_t now = mach_absolute_time();

		if (now >= budget->deadline) {
			return false;
		}
		if (now >= next) {
			return true;
		}
		PMIF_RELAX();
	}
	return false;
}

static bool
pmif_sample(struct pmif_budget *budget)
{
	if (budget->polls >= PMIF_SPIN_HARD_CAP || mach_absolute_time() >= budget->deadline) {
		return false;
	}
	budget->polls++;
	pmif_sta_after = pmif_rd(PMIF_SWINF_2_STA);
	return true;
}

static bool
pmif_wait(struct pmif_budget *budget, uint32_t state)
{
	while (pmif_sample(budget)) {
		if (pmif_sta_after & PMIF_STA_ERR) {
			return false;
		}
		if (PMIF_STA_FSM(pmif_sta_after) == state) {
			return true;
		}
		if (budget->polls >= PMIF_SPIN_HARD_CAP || !pmif_poll_pause(budget)) {
			return false;
		}
	}
	return false;
}

static pmif_status_t
pmif_wait_failure(void)
{
	if (pmif_sta_after & PMIF_STA_ERR) {
		return PMIF_E_NAK;
	}
	return PMIF_STA_FSM(pmif_sta_after) == PMIF_FSM_WFVLDCLR ?
	       PMIF_E_STUCK : PMIF_E_TIMEOUT;
}

static pmif_status_t
pmif_ack_locked(struct pmif_budget *budget)
{
	/* One final sample is allowed even when the poll budget is exhausted. */
	pmif_sta_after = pmif_rd(PMIF_SWINF_2_STA);
	if (PMIF_STA_FSM(pmif_sta_after) == PMIF_FSM_IDLE) {
		return (pmif_sta_after & PMIF_STA_ERR) ? PMIF_E_NAK : PMIF_OK;
	}
	if (PMIF_STA_FSM(pmif_sta_after) != PMIF_FSM_WFVLDCLR) {
		return PMIF_E_BUSY;
	}
	pmif_wr(PMIF_SWINF_2_VLD_CLR, PMIF_VLDCLR_ACK);
	return pmif_wait(budget, PMIF_FSM_IDLE) ? PMIF_OK : PMIF_E_STUCK;
}

static pmif_status_t
pmif_wait_idle_locked(struct pmif_budget *budget)
{
	if (!pmif_sample(budget)) {
		return PMIF_E_TIMEOUT;
	}
	pmif_sta_before = pmif_sta_after;
	if (pmif_sta_after & PMIF_STA_ERR) {
		return PMIF_E_NAK;
	}
	if (PMIF_STA_FSM(pmif_sta_after) == PMIF_FSM_IDLE) {
		return PMIF_OK;
	}
	if (PMIF_STA_FSM(pmif_sta_after) == PMIF_FSM_WFVLDCLR) {
		pmif_wr(PMIF_SWINF_2_VLD_CLR, PMIF_VLDCLR_ACK);
	}
	return pmif_wait(budget, PMIF_FSM_IDLE) ? PMIF_OK : pmif_wait_failure();
}

pmif_status_t
pmif_read8(uint32_t sid, uint32_t addr, uint8_t *out)
{
	pmif_status_t status;
	struct pmif_budget budget;
	uint8_t byte;

	if (out == 0) {
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
	budget = pmif_budget_begin();
	status = pmif_wait_idle_locked(&budget);
	if (status != PMIF_OK) {
		goto failed;
	}
	pmif_wr(PMIF_SWINF_2_ACC, PMIF_CMD_READ8(sid, addr));
	if (!pmif_wait(&budget, PMIF_FSM_WFVLDCLR)) {
		status = pmif_wait_failure();
		goto failed;
	}
	byte = (uint8_t)(pmif_rd(PMIF_SWINF_2_RDATA) & 0xffu);
	pmif_wr(PMIF_SWINF_2_VLD_CLR, PMIF_VLDCLR_ACK);
	if (!pmif_wait(&budget, PMIF_FSM_IDLE)) {
		status = PMIF_STA_FSM(pmif_sta_after) == PMIF_FSM_WFVLDCLR ?
		         PMIF_E_STUCK : PMIF_E_ACK;
		goto failed;
	}
	hakios_spmi_unlock();
	*out = byte;
	return PMIF_OK;
failed:
	/* Includes FSM=6 reached while the initial IDLE wait was expiring. */
	(void)pmif_ack_locked(&budget);
	hakios_spmi_unlock();
	return status;
}

pmif_status_t
pmif_write8(uint32_t sid, uint32_t addr, uint32_t value)
{
	pmif_status_t status = pmif_hdr_admit(sid, addr, true);
	struct pmif_budget budget;

	if (status != PMIF_OK) {
		return status;
	}
	if (value > 0xffu) {
		return PMIF_E_ARG;
	}
	hakios_spmi_lock();
	if (pmif_base == 0) {
		hakios_spmi_unlock();
		return PMIF_E_DEAD;
	}
	budget = pmif_budget_begin();
	status = pmif_wait_idle_locked(&budget);
	if (status != PMIF_OK) {
		goto failed;
	}
	pmif_wr(PMIF_SWINF_2_WDATA, value);
	/* Bit 29 is essential: without it this is a read and WDATA is ignored. */
	pmif_wr(PMIF_SWINF_2_ACC, PMIF_CMD_WRITE8(sid, addr));
	/* Writes finish at IDLE, producing neither RDATA nor a WFVLDCLR ACK. */
	if (!pmif_wait(&budget, PMIF_FSM_IDLE)) {
		status = pmif_wait_failure();
		goto failed;
	}
	hakios_spmi_unlock();
	return PMIF_OK;
failed:
	(void)pmif_ack_locked(&budget);
	hakios_spmi_unlock();
	return status;
}

pmif_status_t
pmif_recover(void)
{
	pmif_status_t status = PMIF_E_DEAD;

	hakios_spmi_lock();
	if (pmif_base != 0) {
		struct pmif_budget budget = pmif_budget_begin();

		status = pmif_ack_locked(&budget);
	}
	hakios_spmi_unlock();
	return status;
}

void
pmif_last_sta(uint32_t *before, uint32_t *after)
{
	hakios_spmi_lock();
	if (before != 0) {
		*before = pmif_sta_before;
	}
	if (after != 0) {
		*after = pmif_sta_after;
	}
	hakios_spmi_unlock();
}

const char *
pmif_status_name(pmif_status_t status)
{
	switch (status) {
	case PMIF_OK: return "ok";
	case PMIF_E_SID: return "SID not allowed";
	case PMIF_E_OFFSET: return "PMIF offset not allowed";
	case PMIF_E_REG: return "PMIC write forbidden";
	case PMIF_E_CMD: return "invalid command";
	case PMIF_E_ARG: return "invalid argument";
	case PMIF_E_BUSY: return "channel busy";
	case PMIF_E_NAK: return "PMIF error/NAK";
	case PMIF_E_TIMEOUT: return "PMIF poll timeout";
	case PMIF_E_STUCK: return "valid word ACK did not retire";
	case PMIF_E_ACK: return "channel not idle after ACK";
	case PMIF_E_DEAD: return "PMIF not mapped";
	}
	return "unknown PMIF status";
}
