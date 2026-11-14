/*
 * MT6895 SWINF_2, shared by keys, accdet and VBUCK2. Each call takes the existing
 * hakios_spmi_lock() around one transaction. Normal thread context only, without
 * a spin lock held; never call after psci_smp_stop_others(). Consumer RMW helpers
 * are not atomic across calls: their PMIC register sets must remain disjoint.
 */
#ifndef _SYS_HAKIOS_PMIF_H_
#define _SYS_HAKIOS_PMIF_H_

#include <stdbool.h>
#include <stdint.h>

#define PMIF_BASE                0x1c804000u
#define PMIF_SIZE_DECLARED       0x08ffu
#define PMIF_SWINF_2_ACC         0x880u
#define PMIF_SWINF_2_WDATA       0x884u
#define PMIF_SWINF_2_RDATA       0x894u
#define PMIF_SWINF_2_VLD_CLR     0x8a4u
#define PMIF_SWINF_2_STA         0x8a8u
#define PMIF_CMD_WRITE           (1u << 29)
#define PMIF_CMD_ADDR_MASK       0xffffu
#define PMIF_CMD_READ8(sid, addr) (0xc0000000u | ((uint32_t)(sid) << 24) | (uint32_t)(addr))
#define PMIF_CMD_WRITE8(sid, addr) (PMIF_CMD_READ8((sid), (addr)) | PMIF_CMD_WRITE)
#define PMIF_STA_FSM(sta)        (((uint32_t)(sta) >> 1) & 7u)
#define PMIF_STA_FSM_MASK        0x0eu
#define PMIF_STA_FSM_SHIFT       1u
#define PMIF_STA_ERR             (1u << 18)
#define PMIF_FSM_IDLE            0u
#define PMIF_FSM_REQ             2u
#define PMIF_FSM_WFDLE           4u
#define PMIF_FSM_WFVLDCLR        6u
#define PMIF_VLDCLR_ACK          1u
#define PMIF_SID_MT6363          4u
#define PMIF_SID_MT6368          5u

/* Vendor cadence; one budget shared by every phase and recovery. */
#define PMIF_POLL_INTERVAL_US    10u
#define PMIF_POLL_BUDGET_US      100000u
#define PMIF_SPIN_HARD_CAP       10000u

typedef enum {
	PMIF_OK = 0,
	PMIF_E_SID = -1,
	PMIF_E_OFFSET = -2,
	PMIF_E_REG = -3,
	PMIF_E_CMD = -4,
	PMIF_E_ARG = -5,
	PMIF_E_BUSY = -6,
	PMIF_E_NAK = -7,
	PMIF_E_TIMEOUT = -8,
	PMIF_E_STUCK = -9,
	PMIF_E_ACK = -10,
	PMIF_E_DEAD = -11,
} pmif_status_t;

static inline bool
pmif_pmic_reg_writable(uint32_t addr)
{
	return addr <= 0xffffu && addr != 0x014bu && addr != 0x0240u;
}

/* Validate full-width arguments before encoding, never silently truncate. */
static inline pmif_status_t
pmif_hdr_admit(uint32_t sid, uint32_t addr, bool write)
{
	if (sid != PMIF_SID_MT6363 && sid != PMIF_SID_MT6368) {
		return PMIF_E_SID;
	}
	if (addr > 0xffffu) {
		return PMIF_E_ARG;
	}
	return write && !pmif_pmic_reg_writable(addr) ? PMIF_E_REG : PMIF_OK;
}

/* Mapping/allocation happens outside the spin lock. Repeated calls are safe. */
pmif_status_t pmif_init(void);
/* On failure *out stays unchanged. Values wider than one byte are rejected. */
pmif_status_t pmif_read8(uint32_t sid, uint32_t addr, uint8_t *out);
pmif_status_t pmif_write8(uint32_t sid, uint32_t addr, uint32_t value);
/* Only ACK FSM=6; no reset/gate/global-controller writes. Thread context only. */
pmif_status_t pmif_recover(void);
/* Last transaction snapshot, not a fresh hardware observation. */
void pmif_last_sta(uint32_t *before, uint32_t *after);
const char *pmif_status_name(pmif_status_t status);

#endif /* _SYS_HAKIOS_PMIF_H_ */
