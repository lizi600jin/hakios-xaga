/*
 * MT6895 UFS host-controller observation, mtk_ufs_probe=1. No controller writes.
 *
 * xaga live DT: ufshci@112b0000, compatible "mediatek,mt8183-ufshci", size 0x2300.
 * Register layout: JEDEC UFSHCI (JESD223), the offsets the K20 port's shim/ufs.c
 * reads and the ones drivers/ufs/host/ufs-mediatek.c uses.
 *
 * This answers one question only: after the loader handed over, is the controller
 * still enabled and is the link still up? A read-only snapshot cannot establish
 * link speed, gear, lane count, that any read of the storage would succeed, or that
 * a filesystem could be mounted. It never writes a register, never rings a doorbell
 * and never issues a SCSI command.
 *
 * The PHY and the controller's power were configured before this kernel ran: the
 * mainline xaga port declares no phys and no resets for this node, and its driver
 * tolerates both being absent. This probe assumes nothing beyond that and reports
 * the raw words when the answer is no.
 */
#ifndef HAKIOS_UFS_HOST_TEST
#include <stdint.h>
#include <stdbool.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <vm/vm_kern.h>
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
#endif

#define UFS_PHYS        0x112b0000u
#define UFS_SIZE        0x2300u

/* JEDEC UFSHCI register offsets. Only these are ever read. */
#define UFS_CAP         0x00u   /* controller capabilities */
#define UFS_VER         0x08u   /* UFSHCI version */
#define UFS_IS          0x20u   /* interrupt status */
#define UFS_HCS         0x30u   /* host controller status */
#define UFS_HCE         0x34u   /* host controller enable */
#define UFS_UTRLBA      0x50u   /* task request list base, low */
#define UFS_UTRLBAU     0x54u   /* task request list base, high */
#define UFS_UTRLDBR     0x58u   /* task request door bell */
#define UFS_UTRLRSR     0x60u   /* task request list run stop */

/* HCS bits, JESD223. */
#define HCS_DP          (1u << 0)   /* device present */
#define HCS_UTRLRDY     (1u << 1)   /* task request list ready */
#define HCS_UCRDY       (1u << 3)   /* UIC command ready */

#define UFS_WORDS 9u

struct ufs_snapshot {
	uint32_t word[UFS_WORDS];
};

enum ufs_result {
	UFS_OBSERVED,       /* both snapshots agree and the controller looks live */
	UFS_READ_FAILED,    /* a register the whitelist allows could not be read */
	UFS_UNSTABLE,       /* the two snapshots differ */
	UFS_NOT_ENABLED,    /* HCE bit0 clear: the controller is not enabled */
	UFS_NO_DEVICE,      /* HCS DP clear: nothing on the link */
	UFS_NOT_READY       /* HCS UTRLRDY clear: the request list is not ready */
};

/* The register table, in the order the results are printed. */
static const uint16_t ufs_offset[UFS_WORDS] = {
	UFS_CAP, UFS_VER, UFS_IS, UFS_HCS, UFS_HCE,
	UFS_UTRLBA, UFS_UTRLBAU, UFS_UTRLDBR, UFS_UTRLRSR
};

/* A read callback has no write operation and is bounded to the DT resource. */
struct ufs_reader {
	bool (*read)(void *, uint16_t, uint32_t *);
	void *context;
};

static bool
ufs_read_snapshot(const struct ufs_reader *io, struct ufs_snapshot *s)
{
	for (uint32_t i = 0; i < UFS_WORDS; i++) {
		if (!io->read(io->context, ufs_offset[i], &s->word[i])) {
			return false;
		}
	}
	return true;
}

static enum ufs_result
ufs_observe(const struct ufs_reader *io, struct ufs_snapshot *first,
	    struct ufs_snapshot *second, uint32_t *cap_nutrs)
{
	if (!ufs_read_snapshot(io, first) || !ufs_read_snapshot(io, second)) {
		return UFS_READ_FAILED;
	}
	for (uint32_t i = 0; i < UFS_WORDS; i++) {
		if (first->word[i] != second->word[i]) {
			return UFS_UNSTABLE;
		}
	}
	/* HCE bit0 is the host's own enable; the vendor drivers require it set. */
	if ((second->word[4] & 1u) == 0) {
		return UFS_NOT_ENABLED;
	}
	/* HCS DP: a device answers on the link. */
	if ((second->word[3] & HCS_DP) == 0) {
		return UFS_NO_DEVICE;
	}
	/* HCS UTRLRDY: the transfer request list can take commands. */
	if ((second->word[3] & HCS_UTRLRDY) == 0) {
		return UFS_NOT_READY;
	}
	/* CAP[4:0] is NUTRS - 1, the number of task request slots. Reported, not used. */
	*cap_nutrs = (second->word[0] & 0x1fu) + 1u;
	return UFS_OBSERVED;
}

static bool
ufs_mmio_read(void *context, uint16_t offset, uint32_t *value)
{
	vm_offset_t base = (vm_offset_t)context;
	bool allowed = false;

	for (uint32_t i = 0; i < UFS_WORDS; i++) {
		if (offset == ufs_offset[i]) {
			allowed = true;
			break;
		}
	}
	if (!allowed || offset + sizeof(uint32_t) > UFS_SIZE) {
		return false;
	}
	*value = *(const volatile uint32_t *)(base + offset);
	return true;
}

void hakios_mt6895_ufs_init(void);

void
hakios_mt6895_ufs_init(void)
{
	uint32_t mode = 0;
	vm_offset_t base;
	struct ufs_reader io;
	struct ufs_snapshot first, second;
	uint32_t nutrs = 0;
	enum ufs_result result;

	if (!PE_parse_boot_argn("mtk_ufs_probe", &mode, sizeof(mode)) || mode == 0) {
		return;
	}
	if (mode != 1u) {
		printf("mt6895-ufs: mtk_ufs_probe must be 0 or 1; no access\n");
		return;
	}
	printf("mt6895-ufs: read-only probe; a snapshot is not a mounted disk\n");
	base = ml_io_map(UFS_PHYS, UFS_SIZE);
	if (base == 0) {
		printf("mt6895-ufs: map failed base=%#x size=%#x\n", UFS_PHYS, UFS_SIZE);
		return;
	}
	io.read = ufs_mmio_read;
	io.context = (void *)base;
	result = ufs_observe(&io, &first, &second, &nutrs);
	printf("mt6895-ufs: base=%#x cap=%#x ver=%#x is=%#x hcs=%#x hce=%#x "
	    "utrlba=%#x utrlbau=%#x utrldbr=%#x utrlrsr=%#x result=%u nutrs=%u\n",
	    UFS_PHYS, second.word[0], second.word[1], second.word[2], second.word[3],
	    second.word[4], second.word[5], second.word[6], second.word[7],
	    second.word[8], result, nutrs);
	printf("mt6895-ufs: dp=%u utrlrdy=%u ucrdy=%u enabled=%u\n",
	    (second.word[3] & HCS_DP) ? 1u : 0u,
	    (second.word[3] & HCS_UTRLRDY) ? 1u : 0u,
	    (second.word[3] & HCS_UCRDY) ? 1u : 0u,
	    second.word[4] & 1u);
	if (result != UFS_OBSERVED) {
		for (uint32_t i = 0; i < UFS_WORDS; i++) {
			printf("mt6895-ufs: raw %#04x=%#x/%#x\n", ufs_offset[i],
			    first.word[i], second.word[i]);
		}
	}
}
