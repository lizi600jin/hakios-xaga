/*
 * The Bluetooth base of the Redmi Note 11T Pro (xaga): the MT6895's BTIF and the WMT
 * protocol on top of it. Only with boot-arg bt=2.
 *
 * THE SHAPE OF THIS SUBSYSTEM, WHICH DECIDES WHAT A PORT CAN DO
 *
 * Bluetooth on xaga is not a chip either. The controller is a core inside CONNSYS,
 * the same subsystem the Wi-Fi MAC lives in: it shares the conn MCU, the conn power
 * domain and the XO_BBCK2 clock. What the AP sees is BTIF -- a byte-oriented
 * interface at 0x1100C000 (plus the two 0x11300Bxx aliases the qemu model and the
 * device tree both list), which MediaTek wires to a UART in their own kernels:
 * Linux drives this phone's BT through serdev/btmtkuart, and btmtkuart.c contains
 * no readl/writel at all -- it never touches BTIF registers, it hands bytes to a
 * serial port. That is the single most useful fact about this port: the transport
 * is "a UART that happens to be inside the SoC", and the protocol above it is
 * WMT, which is plain HCI vendor commands (0xFC6F) carrying a five-byte header.
 *
 * So this file is split at exactly that seam:
 *
 *   - the protocol half (opcode framing, the ROM-patch container's layout, the
 *     section-by-section download order, the enable-then-set-BD_ADDR sequence)
 *     is in hakios_mt6895_bt_wmt.h and needs no hardware at all.
 *   - the transport half is one callback: hci_cmd_sync(). Nothing in this file
 *     invents BTIF's register offsets, because they are not in any public source
 *     for this SoC -- btmtkuart.c does not contain them, and the offsets a
 *     Qualcomm or an older MediaTek BTIF uses are not the same block. Until the
 *     offsets are read off the phone (or off its LK, which programs BTIF before
 *     the kernel ever runs), the callback stays NULL and this driver probes and
 *     stops. It says so on the console instead of pretending to have brought the
 *     radio up.
 *
 * WHY PROBE FIRST, EVEN THOUGH NOTHING IS DRIVEN YET
 *
 * A wrong address on this platform is not an error code, it is an External Abort
 * that takes the kernel down -- and the three BTIF windows are at addresses that
 * look plausible in three different ways (0x1100C000 sits in the peripheral
 * cluster, 0x11300B00 in the second one, 0x188xxxxx would be the CONNSYS alias).
 * Reading each window once and printing what came back turns "the kernel died
 * during boot" into "window 2 read 0x00000000", which is the difference between
 * an afternoon and a week. mtk_connsys_probe_windows() does that walk.
 *
 * THE ROM PATCH
 *
 * The controller boots from ROM with no Bluetooth stack of its own; the patch that
 * gives it one arrives over WMT, in sections, from /vendor/firmware on the phone.
 * Three details cost people days:
 *   - the container's header is 28 bytes but padded to 32 in the file, so the
 *     section table does not start where the header ends (MTK_BT_FW_PATCH_HDR_SIZE
 *     vs MTK_BT_FW_GD_SIZE in the header spell this out).
 *   - each section must be acknowledged before the next one is sent; sending them
 *     back to back overruns the controller's buffer and the failure looks like a
 *     corrupted patch rather than a pacing bug.
 *   - BD_ADDR (0xFC1A) is only accepted after the enable command (WMT op 0,
 *     flag 0, param 1). Setting it first returns success and does nothing.
 *
 * None of that is hardware-dependent, so all of it is implemented and tested on
 * the host (see tools/check-wireless.sh).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "hakios_mt6895_wireless_addrs.h"
#include "hakios_mt6895_bt_wmt.h"
#include "hakios_mt6895_connsys_nvdata.h"

#if defined(HAKIOS_HOST_TEST)
typedef unsigned long vm_offset_t;
typedef unsigned long vm_size_t;
extern int printf(const char *fmt, ...);
extern int PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_len);
#endif
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

/* Wired by the kernel once the BTIF transport exists; NULL on a fresh port. */
extern const struct mtk_bt_hci_ops *hakios_mt6895_bt_transport(void);

#define BT_ARG      "bt"
#define BT_FW_ARG   "bt_fw"

struct hakios_bt_state {
	bool        mapped;
	vm_offset_t btif;
	vm_offset_t btif2;
	vm_offset_t btif3;
	uint8_t     bdaddr[6];
	bool        bdaddr_valid;
	uint32_t    sections;
};

static struct hakios_bt_state gBt;

/*
 * Reading the first word of each BTIF window is a liveness test, not a
 * configuration step: no write, no side effect, nothing that can leave the
 * controller in a state a later driver has to undo. 0xFFFFFFFF is what an
 * unmapped or gated window reads as on this SoC, so it is reported as "not
 * answering" rather than as a value.
 */
static void
bt_probe_window(const char *name, vm_offset_t kva)
{
	uint32_t v;

	if (kva == 0) {
		printf("bt: %s did not map\n", name);
		return;
	}
	v = *(volatile uint32_t *)kva;
	if (v == 0xFFFFFFFFu) {
		printf("bt: %s reads all ones -- mapped, but not answering "
		       "(power domain or clock still off)\n", name);
	} else {
		printf("bt: %s reads 0x%08x\n", name, v);
	}
}

/*
 * The ROM patch arrives as a file. Where it comes from is the loader's business
 * (the same UFS page path nvdata and /private/var use), so this takes the bytes
 * and validates the container before a single one is sent. A patch whose section
 * map runs past the end of the buffer is refused here, not after ten seconds of
 * HCI traffic.
 */
int
hakios_mt6895_bt_load_fw(const uint8_t *fw, size_t len)
{
	struct hakios_bt_state *st = &gBt;
	uint32_t sections = 0;
	int rc;

	rc = mtk_bt_fw_validate_layout(fw, len, &sections);
	if (rc != 0) {
		printf("bt: ROM patch container rejected (rc=%d, %zu bytes)\n", rc, len);
		return rc;
	}
	st->sections = sections;
	printf("bt: ROM patch container OK, %u sections, %zu bytes\n", sections, len);
	return 0;
}

int
hakios_mt6895_bt_set_bdaddr(const uint8_t *nv, size_t nv_len)
{
	struct mtk_nvdata_wireless_ids ids;
	int rc;

	rc = mtk_nvdata_parse_wireless_ids(NULL, 0, nv, nv_len, &ids);
	if (rc != 0 || !ids.bt_bdaddr_valid) {
		printf("bt: nvdata holds no valid BD_ADDR\n");
		return rc != 0 ? rc : -1;
	}
	memcpy(gBt.bdaddr, ids.bt_bdaddr, 6);
	gBt.bdaddr_valid = true;
	printf("bt: BD_ADDR %02x:%02x:%02x:%02x:%02x:%02x from nvdata\n",
	       gBt.bdaddr[0], gBt.bdaddr[1], gBt.bdaddr[2],
	       gBt.bdaddr[3], gBt.bdaddr[4], gBt.bdaddr[5]);
	return 0;
}

/* ---------------------------------------------------------------------------
 * bluetoothd's first read
 *
 * iOS 16.5's bluetoothd opens the DeviceTree's /product node and reads its
 * product-id before it touches a controller. Handed a tree without that
 * property -- which is what a kernel rebuilt for a phone its bootloader never
 * described looks like -- bluetoothd does not merely fail to start Bluetooth:
 * it aborts, and launchd restarts it forever. The check below is the one the
 * OneiOS port (akshaynexus/OneiOS, docs/a56.md) found sufficient: a non-empty
 * property is enough, and its value does not have to mean anything yet.
 *
 * Returns 1 when a property had to be synthesised (and fills the buffer), 0 when
 * the tree already has one, negative when the buffer is too small.
 * ------------------------------------------------------------------------- */

#define PRODUCT_ID_FALLBACK "xaga-mt6895"

int
hakios_mt6895_bt_product_id(const uint8_t *prop, size_t prop_len,
                            uint8_t *out, size_t cap, size_t *out_len)
{
	static const char fallback[] = PRODUCT_ID_FALLBACK;
	const size_t flen = sizeof(fallback) - 1;

	if (out_len != NULL) {
		*out_len = 0;
	}
	if (hakios_dt_has_bluetoothd_product_id(prop, prop_len)) {
		return 0;   /* the tree has one; leave it alone */
	}
	if (out == NULL || cap < flen) {
		return -1;
	}
	memcpy(out, fallback, flen);
	if (out_len != NULL) {
		*out_len = flen;
	}
	printf("bt: /product has no product-id; synthesised \"%s\" for bluetoothd. "
	       "Without it bluetoothd aborts on start and launchd loops.\n",
	       PRODUCT_ID_FALLBACK);
	return 1;
}

/*
 * Start the controller: patch first, then enable, then the address. Each step
 * exists because the controller refuses the one after it otherwise, and each
 * returns the WMT status the controller actually reported -- not the fact that
 * the bytes went out, which is the mistake that produces "paired, but silent".
 */
int
hakios_mt6895_bt_bringup(const uint8_t *fw, size_t fw_len)
{
	struct hakios_bt_state *st = &gBt;
	const struct mtk_bt_hci_ops *ops = hakios_mt6895_bt_transport();
	int rc;

	if (ops == NULL) {
		printf("bt: transport not wired. BTIF is a serial port on this SoC and "
		       "its register offsets are not in btmtkuart.c; read them off the "
		       "phone's LK before this driver sends a byte. Probe only.\n");
		return -1;
	}
	if (fw == NULL || fw_len == 0) {
		printf("bt: no ROM patch loaded; the controller stays in ROM\n");
		return -1;
	}

	rc = mtk_bt_download_patch_sectioned(ops, fw, fw_len, 0);
	if (rc != 0) {
		printf("bt: ROM patch download failed (rc=%d)\n", rc);
		return rc;
	}

	rc = mtk_bt_enable_and_set_addr(ops, st->bdaddr_valid ? st->bdaddr : NULL);
	if (rc != 0) {
		printf("bt: enable/set address failed (rc=%d)\n", rc);
		return rc;
	}

	printf("bt: controller enabled (WMT FUNC_CTRL acknowledged, BD_ADDR %s)\n",
	       st->bdaddr_valid ? "set" : "not set -- nvdata had none");
	return 0;
}

int
hakios_mt6895_bt_start(void)
{
	struct hakios_bt_state *st = &gBt;
	uint32_t which = 0;

	if (!PE_parse_boot_argn(BT_ARG, &which, sizeof(which)) || which != 2) {
		return 0;   /* not asked for */
	}

	memset(st, 0, sizeof(*st));

	st->btif  = ml_io_map((vm_offset_t)XAGA_WIN_BTIF_BASE,  XAGA_WIN_BTIF_SIZE);
	st->btif2 = ml_io_map((vm_offset_t)XAGA_WIN_BTIF_2_BASE, XAGA_WIN_BTIF_2_SIZE);
	st->btif3 = ml_io_map((vm_offset_t)XAGA_WIN_BTIF_3_BASE, XAGA_WIN_BTIF_3_SIZE);

	if (!st->btif || !st->btif2 || !st->btif3) {
		printf("bt: BTIF windows did not all map; stopping before any read\n");
		return -1;
	}

	bt_probe_window("btif",   st->btif);
	bt_probe_window("btif-2", st->btif2);
	bt_probe_window("btif-3", st->btif3);

	st->mapped = true;
	printf("bt: BTIF mapped. Controller NOT started: transport and ROM patch "
	       "are the two pieces left, in that order.\n");
	return 0;
}
