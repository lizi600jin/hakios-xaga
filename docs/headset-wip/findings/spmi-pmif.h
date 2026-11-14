/* SPDX-License-Identifier: GPL-2.0 */
/*
 * spmi-pmif.h -- MT6895 PMIF / SPMI constants, masks and guards.
 *
 * DRAFT, NOT YET INTEGRATED. Lives under docs/headset-wip/ on purpose: it is the
 * reviewed form of what hakios.mt6895_keys.c, hakios.mt6895_headset.c and
 * hakios.mali610.c each open-code today. Nothing includes it yet.
 *
 * Every constant carries its evidence, in the same tags the SPEC uses:
 *   [U] drivers/spmi/spmi-mtk-pmif.c  MT6895-Mainline/linux @ 7.2-mt6895-xiaomi-xaga
 *   [V] drivers/spmi/spmi-mtk-pmif-core.c  XagaForge/android_kernel_xiaomi_mt6895 @ 17.0
 *       (register tables cross-checked against the .rodata of this phone's
 *        spmi-mtk-pmif.ko)
 *   [O] observed on this board, recorded in hakios-mtk
 *
 * Deliberately dependency-free: no XNU headers, no Linux headers. Include it from a
 * driver, a host-side test, or the simulator's counterpart without dragging anything in.
 */
#ifndef _HAKIOS_SPMI_PMIF_H_
#define _HAKIOS_SPMI_PMIF_H_

#include <stdbool.h>
#include <stdint.h>

/* ------------------------------------------------------------------ block layout */

#define PMIF_BASE               0x1c804000u     /* [O] xaga_live.dts:1119 */
#define PMIF_SIZE_DECLARED      0x8ffu          /* [O] xaga_live.dts:1119 reg = <... 0x8ff> */
#define PMIF_SIZE_MAPPED        0x1000u         /* page granularity; NOT an access licence */
#define SPMIMST_BASE            0x1c801000u     /* [O] xaga_live.dts:1119 second reg */
#define SPMIMST_SIZE            0x100u

/*
 * The window, as a pair of bounds any accessor must check before it forms an address.
 *
 * The tree declares size 0x8ff, so the last legally declared byte is offset 0x8fe, and
 * iomem.txt:263 agrees: the resource is 1c804000-1c8048fe.
 *
 * 0x900 is not "the next channel". It is a different block: iomem.txt:264 shows
 * 1c804900-1c804dff owned by `spmi_pmif_mpu`, and xaga_live.dts:2150-2154 declares it
 * as spmi_pmif_mpu@1c804900, size 0x500. Reading it is an access violation on a device
 * this kernel never enables. [O]
 */
#define PMIF_WINDOW_LO          0x000u
#define PMIF_WINDOW_HI          0x8feu
#define PMIF_MPU_LO             0x900u          /* [O] first offset that is not PMIF */

/* SWINF_2, the only channel inside the window. [U]:213-217 (mt8195 table) */
#define PMIF_SWINF_2_ACC        0x880u
#define PMIF_SWINF_2_WDATA      0x884u
#define PMIF_SWINF_2_RDATA      0x894u
#define PMIF_SWINF_2_VLD_CLR    0x8a4u
#define PMIF_SWINF_2_STA        0x8a8u

/*
 * Derived, and worth writing down because the arithmetic is easy to get wrong:
 * the driver indexes its register table with an enum, not with an address.
 *   ch_sta  = PMIF_SWINF_0_STA     + PMIF_CHAN_OFFSET * soc_chan = 25 + 10 = SWINF_2_STA  -> 0x8a8
 *   ch_send = PMIF_SWINF_0_ACC     + ...                         = 28 + 10 = SWINF_2_ACC  -> 0x880
 *   ch_rdy  = PMIF_SWINF_0_VLD_CLR + ...                         = 29 + 10 = SWINF_2_VLD_CLR -> 0x8a4
 * [U]:838-843 with the enum at :81-127 and soc_chan = 2 at :666. Adding the offset to the
 * *addresses* instead gives 0x80a, which is not a register; that mistake is recorded at
 * hakios.mt6895_keys.c:103-104.
 */
#define PMIF_CHAN_STRIDE        0x40u           /* [U] table stride between channels */
#define PMIF_CHAN_INDEX         2u              /* [O][V] ap_swinf_no = 2, soc_chan = 2 */

/* Arbiter gate/enable registers. Read-only for us: LK leaves them set. */
#define PMIF_INIT_DONE          0x000u          /* [U]:130 */
#define PMIF_INF_EN             0x024u          /* [U]:131 */
#define PMIF_ARB_EN             0x150u          /* [U]:132 */
#define PMIF_CMDISSUE_EN        0x3b8u          /* [U]:181 mt8195 table; [O] reads 1 */
#define PMIF_TIMER_CTRL         0x3e4u          /* [U]:182 */
#define PMIF_SPI_MODE_CTRL      0x408u          /* [U]:183; [O] reads 0x0000225d */

/* SPMI master half, at SPMIMST_BASE. Only the FSM status byte concerns us. */
#define SPMI_OP_ST_CTRL         0x000u          /* [U]:250 — slave RESET/SLEEP/SHUTDOWN: DO NOT WRITE */
#define SPMI_OP_ST_STA          0x008u          /* [U]:252 */
#define SPMI_OP_ST_BUSY         (1u << 0)       /* [U]:39 */

/* ------------------------------------------------------------------ ACC: command word */

/*
 * One dword, written to PMIF_SWINF_2_ACC, is the whole command. [U]:389 (read) :467 (write)
 *                                                              [V]:422 (read) :501 (write)
 *
 *  31 30 | 29 | 28 | 27..24 | 23..16        | 15..0
 *  opc   | WR | 0  |  SID   | BC = len - 1  | ADDR
 *
 * There is no USID, no PID, no BCUSID and no address-length flag in this word. USID/PID
 * belong to the SPMI frame the arbiter builds for the opcode it was given, and broadcast
 * (BCUSID) cannot be expressed: both drivers reject any SID outside four bits
 * ([U]:356, :425  [V]:387, :459), so only four-bit unicast SIDs reach the wire.
 * Address length is implied by the opcode: opc 3 is the 16-bit extended-register form.
 */
#define PMIF_CMD_OPC_SHIFT      30
#define PMIF_CMD_OPC_MASK       0xc0000000u
#define PMIF_CMD_WRITE          (1u << 29)      /* 1 = write, 0 = read */
#define PMIF_CMD_SID_SHIFT      24
#define PMIF_CMD_SID_MASK       0x0f000000u     /* bit 28 stays zero */
#define PMIF_CMD_BC_SHIFT       16
#define PMIF_CMD_BC_MASK        0x00ff0000u     /* byte count minus one */
#define PMIF_CMD_ADDR_SHIFT     0
#define PMIF_CMD_ADDR_MASK      0x0000ffffu

/* The arbiter's own opcodes, not SPMI's. [U]:25-28 */
#define PMIF_OPC_REG_0          0u
#define PMIF_OPC_REG            1u              /* SPMI 0x40..0x5f / 0x60..0x7f, 8-bit space */
#define PMIF_OPC_EXT_REG        2u
#define PMIF_OPC_EXT_REG_LONG   3u              /* SPMI 0x20..0x2f / 0x38..0x3f, 16-bit space */

#define PMIF_CMD_MAKE(write, sid, addr, len)                                  \
	((((uint32_t)PMIF_OPC_EXT_REG_LONG) << PMIF_CMD_OPC_SHIFT) |          \
	 ((write) ? PMIF_CMD_WRITE : 0u) |                                    \
	 ((((uint32_t)(sid)) << PMIF_CMD_SID_SHIFT) & PMIF_CMD_SID_MASK) |    \
	 (((((uint32_t)(len)) - 1u) << PMIF_CMD_BC_SHIFT) & PMIF_CMD_BC_MASK) | \
	 (((uint32_t)(addr)) & PMIF_CMD_ADDR_MASK))

/* The two shapes, spelled out, so a reader can compare with the raw dword in a log. */
#define PMIF_CMD_READ8(sid, addr)   PMIF_CMD_MAKE(0, (sid), (addr), 1)
#define PMIF_CMD_WRITE8(sid, addr)  PMIF_CMD_MAKE(1, (sid), (addr), 1)
/* MT6368 accdet read of 0x26b6 -> 0xc50026b6. MT6368 VBUCK2 set-alias write of 0x241 -> 0xe5000241. */

/* ------------------------------------------------------------------ STA: channel state */

/*
 * [U]:20-23 — bits 3:1 are the channel FSM; 0 is idle, 6 is "valid, clear it".
 * The remaining fields are the vendor's own decomposition, recovered from
 * get_pmif_swinf_reg_dump() in spmi-mtk-pmif.ko: the compiler emits ubfx from one register
 * at #18 #17 #15 #11(w4) #7(w4) #6 #5 #4, in the argument order of the format string
 * "[err:%d, sbusy:%d, done:%d, qfillcnt:%d qfreecnt:%d, qempty:%d, qfull:%d, req:%d".
 *
 * Cross-checked against this board's idle value 0x00008441 [O]: done=1, qempty=1,
 * qfreecnt=8, qfillcnt=0, err=0, sbusy=0, req=0 — an idle channel with an empty queue.
 */
#define PMIF_STA_FSM_MASK       0x0000000eu
#define PMIF_STA_FSM_SHIFT      1
#define PMIF_STA_REQ            (1u << 4)
#define PMIF_STA_QFULL          (1u << 5)
#define PMIF_STA_QEMPTY         (1u << 6)
#define PMIF_STA_QFREECNT_SHIFT 7
#define PMIF_STA_QFREECNT_MASK  (0x0fu << 7)
#define PMIF_STA_QFILLCNT_SHIFT 11
#define PMIF_STA_QFILLCNT_MASK  (0x0fu << 11)
#define PMIF_STA_DONE           (1u << 15)
#define PMIF_STA_SBUSY          (1u << 17)      /* software busy */
#define PMIF_STA_ERR            (1u << 18)      /* the error/NAK report */
#define PMIF_STA_BIT0           (1u << 0)       /* reads 1 here; meaning unverified */

/* FSM states. [U]:20-21 for the two the drivers use; the middle values are the legacy
 * wrapper's own enum, mtk-pmic-wrap.c:41-44, which uses the same three-bit field. */
#define PMIF_FSM_IDLE           0u
#define PMIF_FSM_REQ            2u
#define PMIF_FSM_WFDLE          4u              /* parked here when the slave does not answer */
#define PMIF_FSM_WFVLDCLR       6u

#define PMIF_STA_FSM(sta)       ((((uint32_t)(sta)) & PMIF_STA_FSM_MASK) >> PMIF_STA_FSM_SHIFT)

/* ------------------------------------------------------------------ VLDCLR */

/*
 * PMIF_SWINF_2_VLD_CLR is the transaction ack: a W1C that retires WFVLDCLR. It carries no
 * data, so "write one, read it back, require equality" is meaningless — the bit is
 * self-clearing and reads 0 afterwards.
 *
 * Skipping it leaves the FSM in WFVLDCLR and the *next* transaction's IDLE wait can never
 * succeed. Both upstream and the legacy wrapper say so in as many words:
 *   [U]:379-386  on IDLE timeout: if the FSM is WFVLDCLR, write 1 to ch_rdy, then fail
 *   mtk-pmic-wrap.c:1420-1431  "It made state machine still on FSM_VLDCLR and timeout next time"
 */
#define PMIF_VLDCLR_ACK         1u

/* ------------------------------------------------------------------ timeouts */

/*
 * No manual gives a minimum inter-step delay: the synchronisation point is the FSM field,
 * not time. These are the poll cadences the two working drivers use, kept as the budget so
 * a stall is bounded by wall time and not by an arbitrary spin count.
 */
#define PMIF_POLL_INTERVAL_US   2u              /* [U]:30 PMIF_DELAY_US */
#define PMIF_TIMEOUT_US         10000u          /* [U]:31 PMIF_TIMEOUT_US */
#define PMIF_TIMEOUT_VENDOR_US  100000u         /* [V]:36 PMIF_TIMEOUT */
#define PMIF_POLL_MAX           (PMIF_TIMEOUT_US / PMIF_POLL_INTERVAL_US)   /* 5000 */

/*
 * A hard ceiling, for the bare-metal case where delay() may not be honoured: never spin
 * more than this many times, whatever the clock says. 200000 is what the three drivers use
 * today; it is kept only so the value is in one place.
 */
#define PMIF_SPIN_HARD_CAP      200000u

/* ------------------------------------------------------------------ error codes */

typedef enum {
	PMIF_OK = 0,
	PMIF_E_SID,             /* SID not in the whitelist */
	PMIF_E_OFFSET,          /* PMIF register offset outside 0x000..0x8ff */
	PMIF_E_REG,             /* PMIC register on the forbidden list */
	PMIF_E_CMD,             /* command word would not be an EXT_REG_LONG byte transfer */
	PMIF_E_BUSY,            /* channel never reached IDLE */
	PMIF_E_STUCK_VLDCLR,    /* parked in WFVLDCLR and the ack did not retire it */
	PMIF_E_NAK,             /* STA.ERR set, or the FSM parked at WFDLE */
	PMIF_E_TIMEOUT,         /* never reached WFVLDCLR inside the budget */
	PMIF_E_ACK,             /* VLDCLR written but the channel did not return to IDLE */
	PMIF_E_DEAD,            /* a bus abort already happened; the machine is gone */
} pmif_status_t;

/* ------------------------------------------------------------------ whitelists */

/* This board's SPMI populated addresses. [O] xaga_live.dts:1134 pmic@4, :1796 second_pmic@5 */
#define PMIF_SID_MT6363         4u
#define PMIF_SID_MT6368         5u

static inline bool
pmif_sid_ok(uint32_t sid)
{
	/* Four bits on the wire; this board populates exactly two of the sixteen. */
	return sid == PMIF_SID_MT6363 || sid == PMIF_SID_MT6368;
}

/* PMIC registers that must never be written by this project. [O] incident log, SPEC section 5 */
#define PMIC_REG_SHUTDOWN       0x014bu          /* global shutdown: writing it ends the boot */
#define PMIC_REG_VBUCK2_STATUS  0x0240u          /* read-only here; RMW is forbidden */
#define PMIC_REG_VBUCK2_SET     0x0241u          /* the set alias, the only legal way to enable */

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
 * The offset check has to happen before the load or store is formed.
 *
 * STA cannot help here, and believing otherwise is the trap: bits 3:1 and bit 18 describe
 * the *SPMI transaction* on a channel that is already being addressed. An offset outside
 * the window faults on the AXI/APB access to the block itself — one step earlier — and on
 * this SoC, with no abort vectors, the core stops. The only defence is this predicate.
 */
#define PMIF_REQUIRE_OFFSET(off)                                              \
	do {                                                                  \
		if (!pmif_offset_ok((uint32_t)(off))) {                       \
			return PMIF_E_OFFSET;                                 \
		}                                                             \
	} while (0)

/* ------------------------------------------------------------------ transaction shapes */

/* One PMIC register access, in the form a caller fills in. */
struct pmif_xfer {
	uint8_t  sid;           /* PMIF_SID_MT6363 or PMIF_SID_MT6368 */
	uint16_t addr;          /* PMIC register, 16 bits */
	bool     write;
	uint8_t  data;          /* in for a write, out for a read */
	uint32_t cmd;           /* the dword actually written to ACC, kept for the log */
	uint32_t sta_before;
	uint32_t sta_after;
};

/* What a correctly-formed transfer of one byte looks like, for assertions and for tests. */
static inline uint32_t
pmif_cmd_for(const struct pmif_xfer *x)
{
	return x->write ? PMIF_CMD_WRITE8(x->sid, x->addr) : PMIF_CMD_READ8(x->sid, x->addr);
}

static inline bool
pmif_cmd_is_byte_ext(const uint32_t cmd)
{
	return ((cmd & PMIF_CMD_OPC_MASK) >> PMIF_CMD_OPC_SHIFT) == PMIF_OPC_EXT_REG_LONG &&
	       ((cmd & PMIF_CMD_BC_MASK) >> PMIF_CMD_BC_SHIFT) == 0u;
}

/* The four checks every access must pass, in this order, before a single store happens. */
static inline pmif_status_t
pmif_xfer_admit(const struct pmif_xfer *x)
{
	uint32_t cmd;

	if (!pmif_sid_ok(x->sid)) {
		return PMIF_E_SID;
	}
	if (x->write && !pmif_pmic_reg_writable(x->addr)) {
		return PMIF_E_REG;
	}
	cmd = pmif_cmd_for(x);
	if (!pmif_cmd_is_byte_ext(cmd)) {
		return PMIF_E_CMD;
	}
	return PMIF_OK;
}

#endif /* _HAKIOS_SPMI_PMIF_H_ */
