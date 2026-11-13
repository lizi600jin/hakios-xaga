/*
 * MT6368 accdet on xaga (MT6895), SPMI slave 5.
 *
 * headset=1 dumps the two PMIC register blocks without changing PMIC state.
 * Only SWINF_2 at PMIF offsets 0x0880..0x08a8 is accessed. The ACC and
 * VLDCLR writes below submit and acknowledge READ transactions; there is no
 * PMIC write command or WDATA access, including no write to shutdown 0x014b.
 *
 * Run synchronously before the keys driver starts: it uses the same channel
 * without a shared lock. Do not move this dump to a concurrent worker.
 */
#include <stdbool.h>
#include <stdint.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.audio.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

/* Copied from hakios.mt6895_keys.c; do not probe any other PMIF channel. */
#define PMIF_BASE           0x1c804000
#define PMIF_SIZE           0x1000
#define PMIF_SWINF_ACC      0x0880
#define PMIF_SWINF_RDATA    0x0894
#define PMIF_SWINF_VLDCLR   0x08a4
#define PMIF_SWINF_STA      0x08a8
#define PMIF_CMD_EXT_REG_LONG   3u
#define SWINF_IDLE          0x00u
#define SWINF_WFVLDCLR      0x06u
#define PMIF_SPIN_LIMIT     200000u

#define MT6368_SID          5u
#define ACCDET_RG_LO        0x2688
#define ACCDET_RG_HI        0x26d3
#define AUDDEC_ANA_LO       0x2508
#define AUDDEC_ANA_HI       0x2537

static vm_offset_t pmif;

static inline uint32_t
pmif_rd(uint32_t at)
{
	uint32_t value = *(volatile uint32_t *)(pmif + at);

	__asm__ volatile ("dsb sy" ::: "memory");
	return value;
}

static inline void
pmif_wr(uint32_t at, uint32_t value)
{
	__asm__ volatile ("dsb sy" ::: "memory");
	*(volatile uint32_t *)(pmif + at) = value;
	__asm__ volatile ("dsb sy" ::: "memory");
}

/* Same transaction as keys' pmic_read_on(), with SID 5 and a 16-bit address. */
static bool
mt6368_read(uint16_t addr, uint8_t *value)
{
	uint32_t cmd;
	uint32_t spins;

	for (spins = 0; spins < PMIF_SPIN_LIMIT; spins++) {
		if (((pmif_rd(PMIF_SWINF_STA) >> 1) & 0x7u) == SWINF_IDLE) {
			break;
		}
	}
	if (spins == PMIF_SPIN_LIMIT) {
		return false;
	}
	cmd = (PMIF_CMD_EXT_REG_LONG << 30) | ((uint32_t)MT6368_SID << 24) | (0u << 16) | addr;
	pmif_wr(PMIF_SWINF_ACC, cmd);
	for (spins = 0; spins < PMIF_SPIN_LIMIT; spins++) {
		if (((pmif_rd(PMIF_SWINF_STA) >> 1) & 0x7u) == SWINF_WFVLDCLR) {
			break;
		}
	}
	if (spins == PMIF_SPIN_LIMIT) {
		return false;
	}
	*value = (uint8_t)(pmif_rd(PMIF_SWINF_RDATA) & 0xffu);
	pmif_wr(PMIF_SWINF_VLDCLR, 1u);
	return true;
}

static bool
dump_block(uint16_t lo, uint16_t hi)
{
	for (uint16_t addr = lo; addr <= hi; addr++) {
		uint8_t value;

		if (!mt6368_read(addr, &value)) {
			printf("headset: read timeout at 0x%04x; dump stopped\n", addr);
			return false;
		}
		printf("headset: 0x%04x = 0x%02x\n", addr, value);
	}
	return true;
}

void
hakios_mt6895_headset_init(void)
{
	uint32_t mode = 0;

	if (!PE_parse_boot_argn("headset", &mode, sizeof(mode)) || mode == 0) {
		return;
	}
	if (mode == 2) {
		/* TODO: complete accdet initialisation, then poll 0x26b6 bits 7:6 -- not
		 * 0x26a9 bit 0, which is an EINT pending flag. The insertion state is cur_AB,
		 * bits 7:6 of 0x26b6 in work context (0x26b8 in interrupt context), and 3
		 * means nothing is plugged in: the Android baseline reads 0xff in both.
		 * A future poller must serialize PMIF transactions with the keys driver.
		 */
		printf("headset: headset=2 is not implemented\n");
		return;
	}
	if (mode != 1) {
		printf("headset: unsupported mode %u\n", mode);
		return;
	}
	pmif = ml_io_map(PMIF_BASE, PMIF_SIZE);
	if (pmif == 0) {
		printf("headset: cannot map PMIF\n");
		return;
	}
	if (!dump_block(ACCDET_RG_LO, ACCDET_RG_HI)) {
		return;
	}
	(void)dump_block(AUDDEC_ANA_LO, AUDDEC_ANA_HI);
}
