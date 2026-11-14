#include <stdio.h>
#include <assert.h>
#include "spmi-pmif.h"

int main(void)
{
	struct pmif_xfer x;

	/* Command words against the values recorded from this board and from the SPEC. */
	assert(PMIF_CMD_READ8(5, 0x26b6)  == 0xc50026b6u);
	assert(PMIF_CMD_WRITE8(5, 0x0241) == 0xe5000241u);
	assert(PMIF_CMD_READ8(4, 0x001e)  == 0xc400001eu);
	assert(PMIF_CMD_WRITE8(4, 0x001e) == 0xe400001eu);

	/* ACC field isolation. */
	assert((PMIF_CMD_READ8(5, 0x26b6) & PMIF_CMD_SID_MASK) >> PMIF_CMD_SID_SHIFT == 5);
	assert((PMIF_CMD_READ8(5, 0x26b6) & PMIF_CMD_WRITE) == 0);
	assert(PMIF_CMD_WRITE8(5, 0x0241) & PMIF_CMD_WRITE);

	/* STA, against the idle value this board reads. */
	assert(PMIF_STA_FSM(0x00008441u) == PMIF_FSM_IDLE);
	assert((0x00008441u & PMIF_STA_DONE) != 0);
	assert((0x00008441u & PMIF_STA_QEMPTY) != 0);
	assert((0x00008441u & PMIF_STA_ERR) == 0);
	assert((0x00008441u & PMIF_STA_SBUSY) == 0);
	assert(((0x00008441u & PMIF_STA_QFREECNT_MASK) >> PMIF_STA_QFREECNT_SHIFT) == 8);
	assert(((0x00008441u & PMIF_STA_QFILLCNT_MASK) >> PMIF_STA_QFILLCNT_SHIFT) == 0);
	assert(PMIF_STA_FSM(0x0000000cu) == PMIF_FSM_WFVLDCLR);
	assert(PMIF_STA_FSM(0x00000008u) == PMIF_FSM_WFDLE);

	/* Window. */
	assert(pmif_offset_ok(0x880) && pmif_offset_ok(0x8fe) && !pmif_offset_ok(0x8ff) && !pmif_offset_ok(0x900));
	assert(PMIF_MPU_LO == 0x900u);

	/* Whitelists. */
	assert(pmif_sid_ok(4) && pmif_sid_ok(5));
	assert(!pmif_sid_ok(0) && !pmif_sid_ok(6) && !pmif_sid_ok(0xf));
	assert(!pmif_pmic_reg_writable(0x014b));
	assert(!pmif_pmic_reg_writable(0x0240));
	assert(pmif_pmic_reg_writable(0x0241));

	/* Admit path. */
	x = (struct pmif_xfer){ .sid = 5, .addr = 0x26b6, .write = false };
	assert(pmif_xfer_admit(&x) == PMIF_OK);
	x.sid = 0;
	assert(pmif_xfer_admit(&x) == PMIF_E_SID);
	x.sid = 5; x.write = true; x.addr = 0x014b;
	assert(pmif_xfer_admit(&x) == PMIF_E_REG);
	x.addr = 0x0240;
	assert(pmif_xfer_admit(&x) == PMIF_E_REG);

	/* Byte-ext shape check rejects the short and the multi-byte forms. */
	assert(pmif_cmd_is_byte_ext(PMIF_CMD_READ8(5, 0x1e)));
	assert(!pmif_cmd_is_byte_ext((PMIF_OPC_REG << PMIF_CMD_OPC_SHIFT) | (4u << 24) | 0x1e));
	assert(!pmif_cmd_is_byte_ext(PMIF_CMD_MAKE(0, 5, 0x100, 2)));

	/* Timeout budget. */
	assert(PMIF_POLL_MAX == 5000u);
	assert(PMIF_SPIN_HARD_CAP >= PMIF_POLL_MAX);

	printf("header ok: read8(5,0x26b6)=0x%08x write8(5,0x241)=0x%08x poll_max=%u\n",
	       PMIF_CMD_READ8(5, 0x26b6), PMIF_CMD_WRITE8(5, 0x0241), PMIF_POLL_MAX);
	return 0;
}
