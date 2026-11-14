/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pmif-shared.h -- the one PMIF/SPMI transaction path the three drivers on SWINF_2 share.
 *
 * DRAFT, NOT YET INTEGRATED. Lives under docs/headset-wip/ until the fixes in
 * SPMI-FIX-DESIGN.md land. Nothing includes it yet.
 *
 * What it replaces, one for one (line numbers as of the frozen audit snapshot, whose md5
 * is 71c8190c for keys, 41e2107f for headset and 0608664f for mali):
 *
 *   hakios.mt6895_keys.c:244-286   pmic_read_on()          read only, SID 4 hard-coded
 *   hakios.mt6895_keys.c:218-238   pmic_recover_on()       ack-on-timeout, no error code
 *   hakios.mt6895_headset.c:113-176 pmif_wait/recover + mt6368_read + mt6368_write
 *   hakios.mali610.c:3046-3107      mali_pmif_wait/recover + mali_spmi
 *
 * Three copies, no mutual exclusion between them, and the concurrency between the keys
 * worker (hakios.mt6895_keys.c:549, `for (unsigned tick = 1;; tick++)`, 20 ms period) and
 * the mali worker (hakios.mali610.c:5248 kernel_thread_start, :5088 delay(2000*1000)
 * before the first PMIF touch) is real on the default boot path. See SPMI-FIX-DESIGN.md
 * for the serialisation choice and the reasoning behind it.
 *
 * This header is dependency-free on purpose: C11, no XNU headers, no Linux headers, so
 * it compiles under `gcc -std=c11 -Wall -Wextra -Werror` on the host as well as in the
 * kernel. The kernel-side implementation (pmif-shared.c) is the only file that knows
 * about lck_mtx_t, ml_io_map and mach_absolute_time.
 *
 * Evidence tags are the SPEC's:
 *   [U] drivers/spmi/spmi-mtk-pmif.c        MT6895-Mainline/linux @ 7.2-mt6895-xiaomi-xaga
 *   [V] drivers/spmi/spmi-mtk-pmif-core.c   XagaForge/android_kernel_xiaomi_mt6895 @ 17.0
 *   [O] observed on this board, recorded in hakios-mtk
 */
#ifndef _HAKIOS_PMIF_SHARED_H_
#define _HAKIOS_PMIF_SHARED_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==================================================================== block layout */

#define PMIF_BASE               0x1c804000u     /* [O] xaga_live.dts:1119 */
#define PMIF_SIZE_DECLARED      0x08ffu         /* [O] xaga_live.dts:1119 reg = <... 0x8ff> */
/*
 * The mapping is one page because io_map() rounds up anyway (osfmk/arm/io_map.c:94-96:
 * `alloc_size = round_page(size + start_offset)`), and because a page is what pmap maps.
 * The mapping is NOT an access licence: every accessor below checks the offset first.
 */
#define PMIF_SIZE_MAPPED        0x1000u
#define PMIF_WINDOW_LO          0x000u
#define PMIF_WINDOW_HI          0x8feu          /* [O] last byte the tree declares */
#define PMIF_MPU_LO             0x900u          /* [O] iomem.txt:264 spmi_pmif_mpu@1c804900 */

/* SWINF_2, the only software channel inside the window. [U]:213-217 (mt8195 table) */
#define PMIF_SWINF_2_ACC        0x880u
#define PMIF_SWINF_2_WDATA      0x884u
#define PMIF_SWINF_2_RDATA      0x894u
#define PMIF_SWINF_2_VLD_CLR    0x8a4u
#define PMIF_SWINF_2_STA        0x8a8u

/* ==================================================================== ACC: command word */

/*
 *  31 30 | 29 | 28 | 27..24 | 23..16       | 15..0
 *  opc   | WR | 0  |  SID   | BC = len - 1 | ADDR
 * [U]:389 (read) :467 (write)   [V]:422 (read) :501 (write)
 */
#define PMIF_CMD_OPC_SHIFT      30
#define PMIF_CMD_OPC_MASK       0xc0000000u
#define PMIF_CMD_WRITE          (1u << 29)
#define PMIF_CMD_SID_SHIFT      24
#define PMIF_CMD_SID_MASK       0x0f000000u
#define PMIF_CMD_BC_SHIFT       16
#define PMIF_CMD_BC_MASK        0x00ff0000u
#define PMIF_CMD_ADDR_SHIFT     0
#define PMIF_CMD_ADDR_MASK      0x0000ffffu

#define PMIF_OPC_EXT_REG_LONG   3u              /* [U]:25-28, 16-bit register space */

/*
 * The only command shape this header can express: one byte, extended register. `len` is
 * not a parameter anywhere below — a caller cannot ask for a two-byte transfer, which is
 * what makes the BC field check in pmif_hdr_admit() total rather than advisory.
 */
#define PMIF_CMD_MAKE(write, sid, addr)                                       \
	((((uint32_t)PMIF_OPC_EXT_REG_LONG) << PMIF_CMD_OPC_SHIFT) |          \
	 ((write) ? PMIF_CMD_WRITE : 0u) |                                    \
	 ((((uint32_t)(sid)) << PMIF_CMD_SID_SHIFT) & PMIF_CMD_SID_MASK) |    \
	 (((uint32_t)(addr)) & PMIF_CMD_ADDR_MASK))

#define PMIF_CMD_READ8(sid, addr)   PMIF_CMD_MAKE(0, (sid), (addr))
#define PMIF_CMD_WRITE8(sid, addr)  PMIF_CMD_MAKE(1, (sid), (addr))
/* MT6368 accdet read of 0x26b6 -> 0xc50026b6. MT6368 VBUCK2 set-alias write of 0x241 -> 0xe5000241. */

/* ==================================================================== STA: channel state */

#define PMIF_STA_FSM_MASK       0x0000000eu
#define PMIF_STA_FSM_SHIFT      1
#define PMIF_STA_REQ            (1u << 4)
#define PMIF_STA_QFULL          (1u << 5)
#define PMIF_STA_QEMPTY         (1u << 6)
#define PMIF_STA_DONE           (1u << 15)
#define PMIF_STA_SBUSY          (1u << 17)
#define PMIF_STA_ERR            (1u << 18)

#define PMIF_FSM_IDLE           0u
#define PMIF_FSM_REQ            2u
#define PMIF_FSM_WFDLE          4u              /* parked here when the slave does not answer */
#define PMIF_FSM_WFVLDCLR       6u

#define PMIF_STA_FSM(sta)       ((((uint32_t)(sta)) & PMIF_STA_FSM_MASK) >> PMIF_STA_FSM_SHIFT)

/* ==================================================================== VLDCLR */

/*
 * The transaction ack: a W1C that retires WFVLDCLR. Not readable back — it self-clears.
 * A read that skips it leaves the FSM at 6 and every later IDLE wait times out.
 * [U]:379-386 says exactly what to do on an IDLE timeout: ack if the FSM is 6, then fail.
 */
#define PMIF_VLDCLR_ACK         1u

/* ==================================================================== timeouts */

/*
 * Polling has two bounds and both are enforced below:
 *   - a wall-clock budget, from mach_absolute_time (the real one);
 *   - a spin ceiling, for the case where the clock is not usable yet or the MMIO read
 *     itself is the thing that is slow. 200000 is what all three drivers used.
 *
 * PMIF_POLL_BUDGET_US is the vendor's 100 ms ([V]:36), not upstream's 10 ms ([U]:31):
 * on this board the failure mode with 10 ms was a report of a timeout where the previous
 * transaction was merely slow, and 100 ms is still far below the keys worker's 20 ms
 * period × any plausible backlog. [O] unverified which of the two the board needs.
 */
#define PMIF_POLL_INTERVAL_US   2u              /* [U]:30 PMIF_DELAY_US */
#define PMIF_POLL_BUDGET_US     100000u         /* [V]:36 PMIF_TIMEOUT */
#define PMIF_SPIN_HARD_CAP      200000u         /* [O] what the three drivers used */

/* ==================================================================== error codes */

/*
 * Negative on purpose: every function below returns 0 or one of these, so
 * `if (pmif_read8(...) < 0)` is the whole error contract and a caller cannot mistake a
 * data byte for a status.
 */
typedef enum {
	PMIF_OK = 0,
	PMIF_E_SID = -1,        /* SID not in the whitelist */
	PMIF_E_OFFSET = -2,     /* PMIF register offset outside 0x000..0x8fe */
	PMIF_E_REG = -3,        /* PMIC register on the forbidden list */
	PMIF_E_CMD = -4,        /* the word would not be an EXT_REG_LONG single-byte transfer */
	PMIF_E_ARG = -5,        /* null pointer, or a value wider than a byte */
	PMIF_E_BUSY = -6,       /* channel never reached IDLE inside the budget */
	PMIF_E_NAK = -7,        /* STA.ERR set, or the FSM parked at WFDLE (4) */
	PMIF_E_TIMEOUT = -8,    /* never reached WFVLDCLR inside the budget, FSM elsewhere */
	PMIF_E_STUCK = -9,      /* FSM at WFVLDCLR and the ack did not retire it */
	PMIF_E_ACK = -10,       /* ack written, channel did not return to IDLE */
	PMIF_E_DEAD = -11,      /* a bus abort was recorded; the machine is gone */
} pmif_status_t;

/* The name of a status, for a log line. Never NULL. */
const char *pmif_status_name(pmif_status_t status);

/* ==================================================================== whitelists */

#define PMIF_SID_MT6363         4u              /* [O] xaga_live.dts:1134 pmic@4 */
#define PMIF_SID_MT6368         5u              /* [O] xaga_live.dts:1796 second_pmic@5 */

static inline bool
pmif_sid_ok(uint32_t sid)
{
	/* Four bits on the wire; this board populates exactly two of the sixteen. */
	return sid == PMIF_SID_MT6363 || sid == PMIF_SID_MT6368;
}

/* PMIC registers that must never be written by this project. [O] incident log, SPEC §5 */
#define PMIC_REG_SHUTDOWN       0x014bu          /* global shutdown: writing it ends the boot */
#define PMIC_REG_VBUCK2_STATUS  0x0240u          /* read-only here; RMW is forbidden */

static inline bool
pmif_pmic_reg_writable(uint32_t addr)
{
	return addr != PMIC_REG_SHUTDOWN && addr != PMIC_REG_VBUCK2_STATUS;
}

static inline bool
pmif_offset_ok(uint32_t off)
{
	/* Unsigned, and the low bound is zero: only the upper bound can ever bind. */
	return off <= PMIF_WINDOW_HI;
}

/*
 * The offset check has to happen before the load or store is formed: STA cannot help,
 * because bits 3:1 and bit 18 describe an SPMI transaction on a channel that is already
 * being addressed, while an offset outside the window faults on the APB access itself.
 */
#define PMIF_REQUIRE_OFFSET(off)                                              \
	do {                                                                  \
		if (!pmif_offset_ok((uint32_t)(off))) {                       \
			return PMIF_E_OFFSET;                                 \
		}                                                             \
	} while (0)

/*
 * The four checks every access passes, in this order, before a single store happens.
 * Split out from the accessors so the host-side selftest can exercise it with no MMIO.
 */
static inline pmif_status_t
pmif_hdr_admit(uint32_t sid, uint32_t addr, bool write)
{
	uint32_t cmd;

	if (!pmif_sid_ok(sid)) {
		return PMIF_E_SID;
	}
	if (write && !pmif_pmic_reg_writable(addr)) {
		return PMIF_E_REG;
	}
	cmd = write ? PMIF_CMD_WRITE8(sid, addr) : PMIF_CMD_READ8(sid, addr);
	/* A single-byte extended transfer, and nothing else: opc 3, BC field 0, SID intact. */
	if (((cmd & PMIF_CMD_OPC_MASK) >> PMIF_CMD_OPC_SHIFT) != PMIF_OPC_EXT_REG_LONG ||
	    ((cmd & PMIF_CMD_BC_MASK) >> PMIF_CMD_BC_SHIFT) != 0u ||
	    ((cmd & PMIF_CMD_SID_MASK) >> PMIF_CMD_SID_SHIFT) != sid) {
		return PMIF_E_CMD;
	}
	return PMIF_OK;
}

/* ==================================================================== the shared API */

/*
 * Five functions. Two accessors, one recovery, one initialisation, one diagnostic.
 *
 * Concurrency contract: every one of them takes the same lock, so a caller may interleave
 * them with any other caller on any CPU and each transaction is atomic against the rest.
 * A caller must NOT hold the lock across two of them — there is no way to, the lock is
 * static to the implementation — and must not call them from an interrupt context, from
 * a context where preemption is disabled, or from a spinlock. Reason: the implementation
 * takes lck_mtx_t, which sleeps when contended. See SPMI-FIX-DESIGN.md §2 for why that is
 * the right choice here and what would have to change to make it an interrupt-safe path.
 *
 * Everything below is safe to call before the channel is initialised: it returns
 * PMIF_E_DEAD. pmif_init() is idempotent and may be called from any thread; the first
 * caller maps the block, later callers see the mapping it made.
 */
pmif_status_t pmif_init(void);

/*
 * One PMIC byte.
 *
 *   sid    PMIF_SID_MT6363 (4) or PMIF_SID_MT6368 (5). Anything else: PMIF_E_SID.
 *   addr   the PMIC's 16-bit register. A write to PMIC_REG_SHUTDOWN or
 *          PMIC_REG_VBUCK2_STATUS: PMIF_E_REG, before any store.
 *   out    where the byte goes on success. NULL: PMIF_E_ARG. On failure *out is untouched
 *          (the byte is staged in a local and stored last) — the opposite of what
 *          hakios.mt6895_keys.c:276-285 does today.
 *   val    the byte to write. Only the low eight bits are used; a value above 0xff is
 *          PMIF_E_ARG rather than a silent truncation, because every caller here writes
 *          either a register image or a mask and a wide value means a caller bug.
 *
 * Returns PMIF_OK, or a negative pmif_status_t. Every failure path has already done what
 * can be done to the channel: an IDLE timeout with the FSM at WFVLDCLR acks it
 * (PMIF_E_BUSY), a read that reached WFVLDCLR and then failed to retire acks it
 * (PMIF_E_STUCK / PMIF_E_ACK). What it never does is write ARB_EN, CMDISSUE_EN, INF_EN,
 * TIMER_CTRL or SPMI_OP_ST_CTRL: there is no SW_RST in this block (SPEC §5.2) and the
 * other three are global to every channel.
 */
pmif_status_t pmif_read8(uint8_t sid, uint16_t addr, uint8_t *out);
pmif_status_t pmif_write8(uint8_t sid, uint16_t addr, uint8_t val);

/*
 * Acknowledge whatever the channel is holding, without starting a transaction.
 *
 * This is the only recovery there is (SPEC §5.3): if the FSM is at WFVLDCLR, write 1 to
 * VLD_CLR and wait for IDLE; if it is anywhere else, change nothing and say so. It exists
 * for two callers that must not go through the accessors: headset's restore_configuration()
 * (hakios.mt6895_headset.c:307-332), which runs after loader_return() has stopped the other
 * cores, and a diagnostic path that has already decided the channel is unusable.
 *
 * Returns PMIF_E_DEAD if the block was never mapped, PMIF_E_BUSY if the FSM is neither
 * IDLE nor WFVLDCLR (someone else's transaction — do not touch it), PMIF_E_STUCK if the
 * ack did not retire it, PMIF_OK otherwise including the "was already IDLE" case.
 */
pmif_status_t pmif_recover(void);

/*
 * The same ack, without the lock and without any wait, for the one caller that runs after
 * psci_smp_stop_others() has left this core with interrupts off (hakios_mt6895_headset_quiesce).
 * One STA read and, if it says WFVLDCLR, one VLD_CLR write. It cannot deadlock, and it cannot
 * block; what it cannot do is wait for a channel that a stopped core left mid-transaction.
 * Anywhere else pmif_recover() is the right call.
 */
void pmif_recover_besteffort(void);

/*
 * The last transaction's STA, for a log line. Written only by the accessors, and only
 * while they hold the lock, so the pair is coherent even if another CPU transacts after.
 * Either pointer may be NULL. Values are meaningless before the first accessor call
 * (both read 0); pmif_init() does not touch the channel.
 */
void pmif_last_sta(uint32_t *before, uint32_t *after);

/* ==================================================================== self-check */

/*
 * Compile-time sanity of this header, usable from a -fsyntax-only harness or a host test.
 * Not called by anything in the kernel; pmif-fix-check.py's --selftest asserts the same
 * identities from the outside.
 */
#define PMIF_HDR_ASSERT(cond)   ((void)sizeof(char[(cond) ? 1 : -1]))

static inline void
pmif_hdr_selfcheck(void)
{
	PMIF_HDR_ASSERT(PMIF_CMD_READ8(5, 0x26b6) == 0xc50026b6u);
	PMIF_HDR_ASSERT(PMIF_CMD_WRITE8(5, 0x0241) == 0xe5000241u);
	PMIF_HDR_ASSERT(PMIF_CMD_READ8(4, 0x001e) == 0xc400001eu);
	PMIF_HDR_ASSERT(PMIF_WINDOW_HI < PMIF_MPU_LO);
	PMIF_HDR_ASSERT(PMIF_SWINF_2_STA <= PMIF_WINDOW_HI);
	PMIF_HDR_ASSERT((PMIF_SWINF_2_ACC & 0x3u) == 0u);
	PMIF_HDR_ASSERT(PMIF_E_SID < 0 && PMIF_OK == 0);
}

#ifdef __cplusplus
}
#endif

#endif /* _HAKIOS_PMIF_SHARED_H_ */
