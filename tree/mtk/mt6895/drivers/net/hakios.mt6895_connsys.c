/*
 * The Wi-Fi base of the Redmi Note 11T Pro (xaga): the MT6895's CONNSYS, mapped and
 * probed, never driven yet. Only with boot-arg wifi=2.
 *
 * WHAT THIS IS, AND WHAT IT IS NOT
 *
 * xaga has no separate Wi-Fi chip. The MAC, the PHY and the firmware run on CONNSYS,
 * a subsystem inside the MT6895 die with an MCU of its own, reached at 0x180xxxxx and
 * 0x184xxxxx through seven windows (wf-top / wf-cfg / wf-mcu / wf-misc on 0x184x, plus
 * the 0x180x CSR set). What sits *outside* the die is only the radio front end: Qorvo
 * FEMs (wlan7207h / wlan7207c / wlan7205c / qm42195 / qm45197) selected through the
 * connfem node. So the driver to port is MediaTek's own wlan_drv_gen4m, not a
 * Qualcomm WCN stack -- adding a second radio chip to this port would be inventing
 * hardware the phone does not have.
 *
 * This file deliberately stops before "start the radio". Three things must be true
 * before firmware is even loaded, and each of them fails silently when it is wrong:
 *
 *   1. every window must map without an External Abort. A wrong address does not
 *      return an error on this platform: it aborts the kernel on the first read.
 *   2. CONNSYS's EMI region must be the factory physical address, byte for byte.
 *      The EMI MPU is programmed by the boot loader for that one address; a region
 *      allocated elsewhere is not "slower", it is invisible to the conn MCU, and the
 *      symptom is a driver that loads, reports success, and never sees an AP.
 *   3. the calibration and the MAC address have to come out of nvdata, read-only.
 *
 * nvdata is the phone's own calibration store, in the APRDEB section of the nvdata
 * partition. It is read here and never written -- not because this port lacks the
 * write path (it has one, through the loader's UFS page code), but because a wrong
 * write to nvdata costs the phone its radio calibration permanently, while a wrong
 * write to anything else costs a reflash.
 *
 * WHERE THE ADDRESSES COME FROM
 *
 * Not from another phone's driver. Two independent sources agree:
 *   - the phone's own device tree: mt6895.dts mint6895.dts, wifi@18000000
 *     ("mediatek,wifi", 0x700000, GIC SPI 522 and 537, emi-size 0x1400000,
 *     flavor_bin "b") -- and the clock table that puts wifi and conn_mcu in the
 *     same XO_BBCK2 domain.
 *   - Cyber-Mobile/xaga-qemu's MT6895 machine model, which lists each window it
 *     models and names the drivers that probe them (wlan_drv_gen4m_6895.ko,
 *     bt_drv_6895.ko).
 * hakios_mt6895_wireless_addrs.h is generated from the second and checked against
 * the first; tools/gen_xaga_wireless_addrs.py rebuilds it from a qemu checkout.
 *
 * The one address that cannot be generated is the EMI physical base: it is a
 * boot-loader decision, recorded in Android's /reserved-memory. Pass it in with
 * consys_emi_pa=0x... after reading it off the phone; there is no default, because
 * a guessed value would pass every check in this file except the one that matters.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "hakios_mt6895_wireless_addrs.h"
#include "hakios_mt6895_connsys_nvdata.h"

/*
 * XNU supplies the three below; the host-side syntax check (make -C tools
 * check-wireless) supplies them too, so the same source can be compiled
 * without a kernel tree.
 */
#if defined(HAKIOS_HOST_TEST)
typedef unsigned long vm_offset_t;
typedef unsigned long vm_size_t;
extern int printf(const char *fmt, ...);
extern int PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_len);
extern void IODelay(unsigned microseconds);
#endif
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

/* Boot arguments, as the loader passes them (mtkloader sets the ones it needs). */
#define WIFI_ARG        "wifi"
#define EMI_PA_ARG      "consys_emi_pa"
#define EMI_SIZE_ARG    "consys_emi_size"

/* CONNSYS EMI is 20 MiB in the device tree (emi-size 0x1400000). The check in
 * connsys_nvdata.h wants at least 2 MiB; anything under the real size still fails
 * the MPU, so refuse it here as well rather than accept a smaller region. */
#define CONNSYS_EMI_MIN (2u * 1024u * 1024u)

struct hakios_connsys_state {
	bool        mapped;
	vm_offset_t wf_top;
	vm_offset_t wf_cfg;
	vm_offset_t wf_mcu;
	vm_offset_t wf_misc;
	struct mtk_connsys_emi_region emi;
	struct mtk_nvdata_wireless_ids ids;
};

static struct hakios_connsys_state gConnsys;

/*
 * The four Wi-Fi windows as descriptors, so the map calls and the probe
 * callback agree on one set of numbers -- both read the generated header.
 */
static const struct mtk_mmio_window win_wf_top  = {
	"wf-top",  XAGA_WIN_WF_TOP_BASE,  XAGA_WIN_WF_TOP_SIZE,  0 };
static const struct mtk_mmio_window win_wf_cfg  = {
	"wf-cfg",  XAGA_WIN_WF_CFG_BASE,  XAGA_WIN_WF_CFG_SIZE,  0 };
static const struct mtk_mmio_window win_wf_mcu  = {
	"wf-mcu",  XAGA_WIN_WF_MCU_BASE,  XAGA_WIN_WF_MCU_SIZE,  0 };
static const struct mtk_mmio_window win_wf_misc = {
	"wf-misc", XAGA_WIN_WF_MISC_BASE, XAGA_WIN_WF_MISC_SIZE, 0 };

/* ---------------------------------------------------------------------------
 * 1. Mapping. One window at a time, and stop at the first failure: a fault on
 *    this platform is fatal, so there is no "try the rest and see" loop.
 * ------------------------------------------------------------------------- */

static vm_offset_t
connsys_map_window(const struct mtk_mmio_window *w)
{
	vm_offset_t kva = ml_io_map((vm_offset_t)w->phys_base, (vm_size_t)w->size);

	if (kva == 0) {
		printf("connsys: %s at 0x%llx did not map\n",
		       w->name, (unsigned long long)w->phys_base);
		return 0;
	}
	return kva;
}

/*
 * The probe callback the generic walker in connsys_nvdata.h calls. It reads the
 * first 32 bits of each window through the mapped address rather than through
 * the physical one: ml_io_map has already established the translation, so a
 * window that survived this read is genuinely reachable from this kernel.
 */
static uint32_t
connsys_probe_read32(void *ctx, uint64_t pa)
{
	struct hakios_connsys_state *st = ctx;
	vm_offset_t kva = 0;
	uint32_t size = 0;

	if (pa == XAGA_WIN_WF_TOP_BASE) {
		kva = st->wf_top; size = XAGA_WIN_WF_TOP_SIZE;
	} else if (pa == XAGA_WIN_WF_CFG_BASE) {
		kva = st->wf_cfg; size = XAGA_WIN_WF_CFG_SIZE;
	} else if (pa == XAGA_WIN_WF_MCU_BASE) {
		kva = st->wf_mcu; size = XAGA_WIN_WF_MCU_SIZE;
	} else if (pa == XAGA_WIN_WF_MISC_BASE) {
		kva = st->wf_misc; size = XAGA_WIN_WF_MISC_SIZE;
	} else {
		/*
		 * The 0x180x CSR windows are not mapped here yet: they need the conn
		 * power domain on first, and turning that on is the next step, not this
		 * one. Report them as "not probed" instead of reading an unmapped page.
		 */
		return 0xFFFFFFFFu;
	}

	if (kva == 0 || size < 4u) {
		return 0xFFFFFFFFu;
	}
	return *(volatile uint32_t *)kva;
}

/* ---------------------------------------------------------------------------
 * 2. EMI. The single check that decides whether the conn MCU can see its own
 *    memory. Fails closed.
 * ------------------------------------------------------------------------- */

static int
connsys_lock_emi(void)
{
	uint64_t expect_pa = 0, size = 0;
	struct hakios_connsys_state *st = &gConnsys;
	int rc;

	if (!PE_parse_boot_argn(EMI_PA_ARG, &expect_pa, sizeof(expect_pa)) ||
	    expect_pa == 0) {
		printf("connsys: no %s= boot argument. The EMI physical base is a "
		       "boot-loader decision and cannot be guessed: read it from "
		       "Android's /reserved-memory and pass it in. Refusing to start.\n",
		       EMI_PA_ARG);
		return -1;
	}

	if (!PE_parse_boot_argn(EMI_SIZE_ARG, &size, sizeof(size)) ||
	    size < CONNSYS_EMI_MIN) {
		size = 20u * 1024u * 1024u;
	}

	st->emi.expected_factory_pa = expect_pa;
	st->emi.mapped_pa = expect_pa;   /* this port does not relocate it */
	st->emi.size = size;

	rc = mtk_connsys_verify_emi_lock(&st->emi);
	if (rc != 0) {
		/*
		 * -13 here is the interesting case: the region is 4 KiB aligned and
		 * big enough, and still not the factory address. That is the failure
		 * that looks like success everywhere else -- firmware loads, the
		 * driver reports OK, and no AP is ever seen.
		 */
		printf("connsys: EMI lock refused (rc=%d): expected_pa=0x%llx "
		       "mapped_pa=0x%llx size=0x%llx. A mismatched base is invisible "
		       "to the conn MCU, not merely slower.\n",
		       rc, (unsigned long long)st->emi.expected_factory_pa,
		       (unsigned long long)st->emi.mapped_pa,
		       (unsigned long long)st->emi.size);
		return rc;
	}
	printf("connsys: EMI locked at 0x%llx (0x%llx bytes), matches factory\n",
	       (unsigned long long)expect_pa, (unsigned long long)size);
	return 0;
}

/* ---------------------------------------------------------------------------
 * 3. The power sequence, checked before anything is mapped.
 *
 *    Two controllers decide whether the 0x184x windows answer at all, and
 *    neither is in the generated window table because neither belongs to
 *    CONNSYS: the MTCMOS controller and the modem's own sequencer.
 *
 *      power-controller@1c001000  compatible "mediatek,mt6895-scpsys", syscon
 *                                 (xaga-live.dts:7338; the tree also lists it
 *                                 as /soc/power-controller@1c001000 at 13843).
 *                                 Not 0x10006000 -- that is the MT6768's SPM,
 *                                 and the MT6895 has an scp_infra block there.
 *      the mddriver node         reg 0x0d124000, 0x2000, md_id 0,
 *                                 md_generation 0x189a (xaga-live.dts:2878).
 *
 *    The register offsets inside them come from the vendor's power-flow code
 *    and are named as it names them here, so they can be checked against it:
 *    SPM_PDN_REQ/ACK at 0x0c34/0x0c3c inside SCPSYS, the modem's own CTRL/STA
 *    at 0x0204/0x0310 inside the mddriver page.
 *
 *    Reads only, unless boot-arg connsys_power=1. A port that has not yet
 *    verified these offsets should not be writing them, and the read still
 *    answers the question that matters: is the block powered, or is the window
 *    about to abort?
 * ------------------------------------------------------------------------- */

#define SCPSYS_PHYS        0x1c001000ULL
#define SCPSYS_SIZE        0x0FB0u
#define MD1_PHYS           0x0D124000ULL
#define MD1_SIZE           0x2000u

#define SCPSYS_SPM_PDN_REQ (0x0C34u)
#define SCPSYS_SPM_PDN_ACK (0x0C3Cu)
#define SCPSYS_SPM_PDN_BITS (0x00028000u)   /* bit 15 | bit 17: conn and md */

#define MD1_CTRL           (0x0204u)
#define MD1_STA            (0x0310u)
#define MD1_CTRL_ENABLED   0x04040040u
#define MD1_CTRL_DISABLED  0x01010001u

#define POWER_ARG          "connsys_power"

struct hakios_connsys_power {
	vm_offset_t scpsys;
	vm_offset_t md1;
	bool        scpsys_on;
	bool        md1_on;
};

static int
connsys_power_check(struct hakios_connsys_power *p)
{
	uint32_t req, ack, ctrl, sta, want_write = 0;
	bool do_write;

	p->scpsys = ml_io_map((vm_offset_t)SCPSYS_PHYS, (vm_size_t)SCPSYS_SIZE);
	p->md1 = ml_io_map((vm_offset_t)MD1_PHYS, (vm_size_t)MD1_SIZE);
	if (p->scpsys == 0 || p->md1 == 0) {
		printf("connsys: power controllers did not map (scpsys=%d md1=%d)\n",
		       p->scpsys != 0, p->md1 != 0);
		return -1;
	}

	do_write = PE_parse_boot_argn(POWER_ARG, &want_write, sizeof(want_write)) &&
		   want_write == 1;

	req = *(volatile uint32_t *)(p->scpsys + SCPSYS_SPM_PDN_REQ);
	ack = *(volatile uint32_t *)(p->scpsys + SCPSYS_SPM_PDN_ACK);
	p->scpsys_on = (req & SCPSYS_SPM_PDN_BITS) == SCPSYS_SPM_PDN_BITS &&
		       (ack & SCPSYS_SPM_PDN_BITS) == SCPSYS_SPM_PDN_BITS;
	printf("connsys: SCPSYS req=0x%08x ack=0x%08x -> conn/md %s\n",
	       req, ack, p->scpsys_on ? "powered" : "NOT powered");

	ctrl = *(volatile uint32_t *)(p->md1 + MD1_CTRL);
	sta  = *(volatile uint32_t *)(p->md1 + MD1_STA);
	p->md1_on = (ctrl == MD1_CTRL_ENABLED) || (ctrl != MD1_CTRL_DISABLED && sta != 0);
	printf("connsys: MD1 ctrl=0x%08x sta=0x%08x -> sequencer %s\n",
	       ctrl, sta, p->md1_on ? "enabled" : "disabled or unknown");

	if (!p->scpsys_on && do_write) {
		/*
		 * The order matters and is the vendor's: request the domain in the
		 * MTCMOS controller and wait for its acknowledgement before touching
		 * the modem's page. Writing MD1 first leaves the sequencer enabled
		 * with no power behind it, which reads back as "enabled" and behaves
		 * as "dead".
		 */
		printf("connsys: raising conn/md power (connsys_power=1)\n");
		*(volatile uint32_t *)(p->scpsys + SCPSYS_SPM_PDN_REQ) =
			req | SCPSYS_SPM_PDN_BITS;
		for (int i = 0; i < 1000; i++) {
			ack = *(volatile uint32_t *)(p->scpsys + SCPSYS_SPM_PDN_ACK);
			if ((ack & SCPSYS_SPM_PDN_BITS) == SCPSYS_SPM_PDN_BITS) {
				p->scpsys_on = true;
				break;
			}
			IODelay(100);
		}
		printf("connsys: SCPSYS ack now 0x%08x -> %s\n", ack,
		       p->scpsys_on ? "up" : "still down after 10 ms");
	}

	if (!p->scpsys_on) {
		/*
		 * Not fatal: LK may have left the domain on and the window may answer
		 * anyway. But it is reported first, because "the window faulted" and
		 * "the domain was never on" look identical from a console otherwise.
		 */
		printf("connsys: conn/md domain is down. Mapping the 0x184x windows "
		       "now would fault on first read; pass connsys_power=1 to raise it.\n");
	}
	return p->scpsys_on ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * 4. nvdata. Supplied by the caller: this file does not read flash.
 *
 *    The buffer has to come from the phone's own nvdata partition, copied into
 *    RAM once, read-only. On this port the read goes through the loader's UFS
 *    page code (the same path /private/var uses); on the bench it can come from
 *    a dump. Either way it arrives here as a flat array and leaves untouched.
 * ------------------------------------------------------------------------- */

int
hakios_mt6895_connsys_set_nvdata(const uint8_t *wifi_nv, size_t wifi_len,
                                const uint8_t *bt_nv, size_t bt_len)
{
	struct hakios_connsys_state *st = &gConnsys;
	int rc;

	rc = mtk_nvdata_parse_wireless_ids(wifi_nv, wifi_len, bt_nv, bt_len, &st->ids);
	if (rc != 0) {
		printf("connsys: nvdata parse failed (rc=%d)\n", rc);
		return rc;
	}
	if (st->ids.wifi_mac_valid) {
		printf("connsys: Wi-Fi MAC %02x:%02x:%02x:%02x:%02x:%02x from nvdata\n",
		       st->ids.wifi_mac[0], st->ids.wifi_mac[1], st->ids.wifi_mac[2],
		       st->ids.wifi_mac[3], st->ids.wifi_mac[4], st->ids.wifi_mac[5]);
	} else {
		/*
		 * Not fatal for the probe, fatal for association: a zero or multicast
		 * MAC is refused by every AP. Say so rather than carry on quietly.
		 */
		printf("connsys: nvdata holds no valid unicast Wi-Fi MAC\n");
	}
	return 0;
}

const struct mtk_nvdata_wireless_ids *
hakios_mt6895_connsys_ids(void)
{
	return &gConnsys.ids;
}

/* ---------------------------------------------------------------------------
 * 5. Entry. Called from the kernel's driver start path when CONFIG_HAKIOS_
 *    MT6895_CONNSYS is built and boot-arg wifi=2 is present.
 * ------------------------------------------------------------------------- */

int
hakios_mt6895_connsys_start(void)
{
	struct hakios_connsys_state *st = &gConnsys;
	struct mtk_connsys_probe_ops ops;
	uint32_t which = 0;
	int rc;

	if (!PE_parse_boot_argn(WIFI_ARG, &which, sizeof(which)) || which != 2) {
		return 0;   /* not asked for */
	}

	memset(st, 0, sizeof(*st));

	rc = connsys_lock_emi();
	if (rc != 0) {
		return rc;
	}

	/*
	 * Power before mapping: the 0x184x windows are inside the conn domain, and
	 * a read of a gated window aborts rather than returning zero.
	 */
	{
		struct hakios_connsys_power pw;

		memset(&pw, 0, sizeof(pw));
		(void)connsys_power_check(&pw);   /* reported, not fatal */
	}

	/*
	 * The four Wi-Fi windows, by name. The 0x180x CSR set is deliberately not
	 * mapped: those registers only answer after the conn power domain is up,
	 * and bringing that up is a separate step with its own ordering (SPM ->
	 * conninfra -> MCU release). Mapping them first would fault.
	 */
	st->wf_top  = connsys_map_window(&win_wf_top);
	st->wf_cfg  = connsys_map_window(&win_wf_cfg);
	st->wf_mcu  = connsys_map_window(&win_wf_mcu);
	st->wf_misc = connsys_map_window(&win_wf_misc);

	if (!st->wf_top || !st->wf_cfg || !st->wf_mcu || !st->wf_misc) {
		return -1;
	}

	/*
	 * The probe table is generated from the header, one entry per window, so a
	 * rebuilt header cannot silently disagree with a hand-written list.
	 * XAGA_MMIO_WINDOWS is an X-macro taking (name, base, size); the
	 * designated initialisers keep C's field order out of the X-macro.
	 */
	{
		static const struct mtk_mmio_window wins[] = {
#define X(addr, wbase, wsize) \
			{ .name = #addr, .phys_base = (wbase), \
			  .size = (uint32_t)(wsize), .probe_ofs = 0 },
			XAGA_MMIO_WINDOWS(X)
#undef X
		};
		const size_t nwins = sizeof(wins) / sizeof(wins[0]);

		ops.ctx = st;
		ops.read32 = connsys_probe_read32;
		ops.log = 0;   /* the walker prints through printf when no logger is set */

		rc = mtk_connsys_probe_windows(&ops, wins, nwins);
		printf("connsys: probed %zu windows, rc=%d\n", nwins, rc);
	}

	st->mapped = true;
	printf("connsys: base up. The radio is NOT started: firmware, the conn "
	       "power domain and wlan_drv_gen4m are the next steps, in that order.\n");
	return rc;
}
