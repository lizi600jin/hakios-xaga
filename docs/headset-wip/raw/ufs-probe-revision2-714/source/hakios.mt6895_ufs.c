/*
 * MT6895 UFS host-controller observation, mtk_ufs_probe=1. No controller writes.
 *
 * xaga live DT: ufshci@112b0000, compatible "mediatek,mt8183-ufshci", size 0x2300.
 * Register layout: JEDEC UFSHCI (JESD223), the offsets the K20 port's shim/ufs.c
 * reads and the ones drivers/ufs/host/ufs-mediatek.c uses.
 *
 * This captures controller words and the HCE/DP/UTRLRDY flags after handover.
 * A flag sample is not proof that the link or an inherited queue can execute IO.
 * A read-only snapshot cannot establish
 * link speed, gear, lane count, that any read of the storage would succeed, or that
 * a filesystem could be mounted. It never writes a register, never rings a doorbell
 * and never issues a SCSI command.
 *
 * The existing loader contains its own controller takeover and flash read path.
 * Its state at kernel entry still needs observation. Optional PHY/reset handles
 * in a Linux port do not establish that firmware or other init is unnecessary.
 * Real MMIO data aborts are not converted into false by the callback below.
 */
#ifndef HAKIOS_UFS_HOST_TEST
#include <stdint.h>
#include <stdbool.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <libkern/libkern.h>
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
#define UFS_UTRLBA      0x50u   /* transfer request list base, low */
#define UFS_UTRLBAU     0x54u   /* transfer request list base, high */
#define UFS_UTRLDBR     0x58u   /* transfer request doorbell */
#define UFS_UTRLRSR     0x60u   /* transfer request list run/stop */

/* HCS bits, JESD223. */
#define HCS_DP          (1u << 0)   /* device present */
#define HCS_UTRLRDY     (1u << 1)   /* transfer request list ready flag */
#define HCS_UCRDY       (1u << 3)   /* UIC command ready */

#define UFS_WORDS 9u

struct ufs_snapshot {
	uint32_t word[UFS_WORDS];
	uint32_t attempted, valid;
};

enum ufs_result {
	UFS_FLAGS_OBSERVED, /* stable words with the three flags set; not permission for IO */
	UFS_READ_FAILED,    /* a register the whitelist allows could not be read */
	UFS_UNSTABLE,       /* non-IS words changed, not a hardware-failure diagnosis */
	UFS_NOT_ENABLED,    /* HCE bit0 clear: the controller is not enabled */
	UFS_NO_DEVICE,      /* HCS DP clear: nothing on the link */
	UFS_NOT_READY,      /* HCS UTRLRDY flag clear */
	UFS_UNTRUSTED_RESPONSE /* obvious invalid identity/status response; cause unknown */
};

/* The register table, in the order the results are printed. */
static const uint16_t ufs_offset[UFS_WORDS] = {
	UFS_CAP, UFS_VER, UFS_IS, UFS_HCS, UFS_HCE,
	UFS_UTRLBA, UFS_UTRLBAU, UFS_UTRLDBR, UFS_UTRLRSR
};

/* This interface exposes only reads; the implementation must be audited too. */
struct ufs_reader {
	bool (*read)(void *, uint16_t, uint32_t *);
	void *context;
};

static bool
ufs_read_snapshot(const struct ufs_reader *io, struct ufs_snapshot *s)
{
	for (uint32_t i = 0; i < UFS_WORDS; i++) {
		s->attempted |= 1u << i;
		if (!io->read(io->context, ufs_offset[i], &s->word[i])) {
			return false;
		}
		s->valid |= 1u << i;
	}
	return true;
}

static enum ufs_result
ufs_observe(const struct ufs_reader *io, struct ufs_snapshot *first,
	    struct ufs_snapshot *second, uint32_t *cap_nutrs)
{
	*first = (struct ufs_snapshot){ 0 };
	*second = (struct ufs_snapshot){ 0 };
	*cap_nutrs = 0;
	if (!ufs_read_snapshot(io, first) || !ufs_read_snapshot(io, second)) {
		return UFS_READ_FAILED;
	}
	for (uint32_t i = 0; i < UFS_WORDS; i++) {
		/* Interrupt status changes autonomously; report both samples. */
		if (ufs_offset[i] != UFS_IS && first->word[i] != second->word[i]) {
			return UFS_UNSTABLE;
		}
	}
	/* These responses cannot support the flag interpretation below. Unknown
	 * nonzero versions are retained, without claiming version support. */
	if (second->word[0] == UINT32_MAX || second->word[1] == 0 ||
	    second->word[1] == UINT32_MAX || second->word[3] == UINT32_MAX ||
	    second->word[4] == UINT32_MAX) {
		return UFS_UNTRUSTED_RESPONSE;
	}
	/* HCE bit0 is the host's own enable; the vendor drivers require it set. */
	if ((second->word[4] & 1u) == 0) {
		return UFS_NOT_ENABLED;
	}
	/* Report the device-present flag, not a successful command response. */
	if ((second->word[3] & HCS_DP) == 0) {
		return UFS_NO_DEVICE;
	}
	/* A ready flag does not establish queue ownership or DMA validity. */
	if ((second->word[3] & HCS_UTRLRDY) == 0) {
		return UFS_NOT_READY;
	}
	/* CAP[4:0]+1 is the legacy single-doorbell transfer-slot count. */
	*cap_nutrs = (second->word[0] & 0x1fu) + 1u;
	return UFS_FLAGS_OBSERVED;
}

static void
ufs_format_word(const struct ufs_snapshot *s, uint32_t row, char *text, size_t size)
{
	if (s->valid & (1u << row)) {
		snprintf(text, size, "%#x", s->word[row]);
	} else {
		snprintf(text, size, "%s", (s->attempted & (1u << row)) ?
		    "FAILED" : "NOT_COLLECTED");
	}
}

static void
ufs_report(const struct ufs_snapshot *first, const struct ufs_snapshot *second,
	    enum ufs_result result, uint32_t nutrs)
{
	const char *name;

	switch (result) {
	case UFS_FLAGS_OBSERVED: name = "FLAGS_OBSERVED"; break;
	case UFS_READ_FAILED: name = "READ_FAILED"; break;
	case UFS_UNSTABLE: name = "CHANGING_WORDS"; break;
	case UFS_NOT_ENABLED: name = "HCE_CLEAR"; break;
	case UFS_NO_DEVICE: name = "DP_CLEAR"; break;
	case UFS_NOT_READY: name = "UTRLRDY_CLEAR"; break;
	case UFS_UNTRUSTED_RESPONSE: name = "UNTRUSTED_RESPONSE"; break;
	default: name = "UNKNOWN_RESULT"; break;
	}
	printf("mt6895-ufs: base=%#x result=%s(%u) valid=%#x/%#x attempted=%#x/%#x sdb_slots=%u\n",
	    UFS_PHYS, name, result, first->valid, second->valid,
	    first->attempted, second->attempted, nutrs);
	for (uint32_t i = 0; i < UFS_WORDS; i++) {
		char a[24], b[24];

		ufs_format_word(first, i, a, sizeof(a));
		ufs_format_word(second, i, b, sizeof(b));
		printf("mt6895-ufs: raw %#04x=%s/%s\n", ufs_offset[i], a, b);
	}
	if (result == UFS_UNTRUSTED_RESPONSE) {
		printf("mt6895-ufs: flags=UNTRUSTED_RESPONSE\n");
	} else if (result == UFS_READ_FAILED) {
		printf("mt6895-ufs: flags=INCOMPLETE\n");
	} else if ((second->valid & ((1u << 3) | (1u << 4))) == ((1u << 3) | (1u << 4))) {
		printf("mt6895-ufs: flags-only dp=%u utrlrdy=%u ucrdy=%u enabled=%u\n",
		    (second->word[3] & HCS_DP) ? 1u : 0u,
		    (second->word[3] & HCS_UTRLRDY) ? 1u : 0u,
		    (second->word[3] & HCS_UCRDY) ? 1u : 0u, second->word[4] & 1u);
	} else {
		printf("mt6895-ufs: flags=NOT_COLLECTED\n");
	}
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
	struct ufs_snapshot first = { 0 }, second = { 0 };
	uint32_t nutrs = 0;
	enum ufs_result result;

	if (!PE_parse_boot_argn("mtk_ufs_probe", &mode, sizeof(mode)) || mode == 0) {
		return;
	}
	if (mode != 1u) {
		printf("mt6895-ufs: mtk_ufs_probe must be 0 or 1; no access\n");
		return;
	}
	printf("mt6895-ufs: read-only probe; flag samples do not establish IO readiness\n");
	base = ml_io_map(UFS_PHYS, UFS_SIZE);
	if (base == 0) {
		printf("mt6895-ufs: map failed base=%#x size=%#x\n", UFS_PHYS, UFS_SIZE);
		return;
	}
	io.read = ufs_mmio_read;
	io.context = (void *)base;
	result = ufs_observe(&io, &first, &second, &nutrs);
	ufs_report(&first, &second, result, nutrs);
}
