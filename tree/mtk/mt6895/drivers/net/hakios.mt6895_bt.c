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

#if !defined(HAKIOS_HOST_TEST)
#define HAKIOS_MT6895_BT_WMT_IMPLEMENTATION
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#endif

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

/*
 * BTIF PIO transport. The register map and the two write rules it enforces
 * (read-back after every write, read-modify-write on the shared 0x08 window)
 * come from MT6895-Mainline/linux btif_plat.c / btif_priv.h; the base
 * 0x1100C000 is corroborated by xaga-qemu hw/arm/mt6895.c and xaga-live.dts.
 */
#define XAGA_BTIF_IMPLEMENTATION
#include "hakios_mt6895_bt_pio.h"

/* 睡眠原语：XNU 与主机 stub 都以 delay(int usec) 为准，不用 IODelay（仅 XNU 有）。 */
extern void delay(int microseconds);

struct hakios_bt_state {
	bool               mapped;
	vm_offset_t        btif;
	vm_offset_t        btif2;
	vm_offset_t        btif3;
	uint8_t            bdaddr[6];
	bool               bdaddr_valid;
	uint32_t           sections;
	struct xaga_btif   pio;
};

static struct hakios_bt_state gBt;

#define XAGA_BTIF_SPINS 200000u
#define XAGA_BTIF_EVT_MAX 128u

static uint32_t
bt_mmio_rd32(void *ctx, uint32_t off)
{
	vm_offset_t base = (vm_offset_t)ctx;

	return *(volatile uint32_t *)(base + (vm_offset_t)off);
}

static void
bt_mmio_wr32(void *ctx, uint32_t off, uint32_t val)
{
	vm_offset_t base = (vm_offset_t)ctx;

	*(volatile uint32_t *)(base + (vm_offset_t)off) = val;
}

static void
bt_mmio_msleep(void *ctx, uint32_t ms)
{
	(void)ctx;
	while (ms-- != 0) {
		delay(1000);
	}
}

/*
 * The ops contract, read off mtk_bt_wmt_sync(): tx_data is already a WMT
 * payload without an H4 header, and rx_buf must receive something that starts
 * at struct mtk_wmt_hdr. So we add the H4 command header on the way out and
 * strip the H4 event header on the way in.
 *
 * Two event shapes exist on this port:
 *   0xFF vendor event  -> payload after [04 FF LEN] is the WMT header
 *   0x0E cmd complete  -> payload is [ncmd][oplo][ophi][status] then the WMT
 *                         header, so we skip four more bytes
 * The 0x0E offset is the one thing here that could not be checked against a
 * source tree; it is written so that a wrong guess fails loudly (the parser
 * rejects it) rather than silently returning a bogus status.
 */
/* 以上 mmio 在主机侧同样成立：stub 的 ml_io_map 返回真实可读写内存，
   所以这段与真机共用同一份代码，不为测试另写实现。 */

/*
 * hci_cmd_sync 与平台无关：它只经 struct xaga_btif_bus 说话，因此主机侧单测
 * 能把同一段代码跑在模拟寄存器组上，而不是另写一份测试专用实现。
 */
static int
bt_hci_cmd_sync(void *ctx, uint16_t opcode, const uint8_t *tx_data,
                uint16_t tx_len, uint8_t *rx_buf, uint16_t rx_cap)
{
	struct hakios_bt_state *st = (struct hakios_bt_state *)ctx;
	uint8_t frame[XAGA_BTIF_EVT_MAX];
	int rc;
	int attempt;
	size_t payload_off = 0;
	size_t payload_len;

	if (st == NULL || rx_buf == NULL || rx_cap < 6) {
		return -22;
	}

	/* Wake the BB side, then drop anything stale before the command goes out. */
	xaga_btif_wake(&st->pio);
	(void)xaga_btif_clear_fifo(&st->pio, 0, 1);

	rc = xaga_btif_h4_send_cmd(&st->pio, opcode, tx_data, tx_len,
	                           XAGA_BTIF_SPINS);
	if (rc < 0) {
		printf("bt: H4 command 0x%04x did not go out (rc=%d)\n",
		       (unsigned)opcode, rc);
		return rc;
	}

	for (attempt = 0; attempt < 3; attempt++) {
		rc = xaga_btif_h4_recv_evt(&st->pio, frame, sizeof(frame),
		                           XAGA_BTIF_SPINS);
		if (rc < 3) {
			continue;
		}

		if (frame[1] == 0xFFu) {
			payload_off = 3;
		} else if (frame[1] == 0x0Eu) {
			payload_off = 7;
		} else {
			/* Not a response to us; keep reading. */
			continue;
		}

		if ((size_t)rc <= payload_off) {
			continue;
		}
		payload_len = (size_t)rc - payload_off;
		if (payload_len > (size_t)rx_cap) {
			return -7;
		}

		memcpy(rx_buf, frame + payload_off, payload_len);
		return (int)payload_len;
	}

	return -11;   /* the controller answered with nothing we recognise */
}

/*
 * 骨架要的五个函数。power_on 认为 BTIF 已映射就是"芯片起来了"—— 真正的
 * 上电、ROM patch 与 WMT 脚本属于 connsys，Wi-Fi 通之后这一步就是等它。
 */
#include "hakios_mt6895_bt_plat.h"

static int
bt_plat_power_on(void *ctx)
{
	struct hakios_bt_state *st = (struct hakios_bt_state *)ctx;

	if (st == NULL || !st->mapped) {
		return -1;
	}
	return 0;
}

/* WMT_FUNC_CTRL，type 0 (BT)，param 1 = on / 0 = off。上游文档的命令形状：
 * 01 06 02 00 00 01 -> 02 06 01 00 00
 */
static int
bt_plat_function(void *ctx, uint8_t on)
{
	struct hakios_bt_state *st = (struct hakios_bt_state *)ctx;
	struct mtk_bt_hci_ops ops;
	uint8_t param = on ? 1u : 0u;
	uint8_t sts = 0xFF;
	int rc;

	if (st == NULL) {
		return -22;
	}
	ops.ctx          = st;
	ops.hci_cmd_sync = bt_hci_cmd_sync;
	ops.msleep       = NULL;

	rc = mtk_bt_wmt_sync(&ops, MTK_WMT_FUNC_CTRL, 0, &param, 1, &sts);
	if (rc < 0) {
		return rc;
	}
	return (sts == MTK_WMT_ON_DONE || sts == MTK_WMT_ON_PROGRESS) ? 0 : -5;
}

static int
bt_plat_function_on(void *ctx)
{
	return bt_plat_function(ctx, 1);
}

static int
bt_plat_function_off(void *ctx)
{
	return bt_plat_function(ctx, 0);
}

static int
bt_plat_send(void *ctx, const uint8_t *h4, size_t len)
{
	struct hakios_bt_state *st = (struct hakios_bt_state *)ctx;
	int rc;

	if (st == NULL || h4 == NULL || len == 0 || len > 0xFFFFu) {
		return -22;
	}
	xaga_btif_wake(&st->pio);
	rc = xaga_btif_tx(&st->pio, h4, len, XAGA_BTIF_SPINS);
	return (rc == (int)len) ? (int)len : ((rc < 0) ? rc : -6);
}

static int
bt_plat_receive(void *ctx, uint8_t *h4, size_t cap)
{
	struct hakios_bt_state *st = (struct hakios_bt_state *)ctx;

	if (st == NULL || h4 == NULL || cap < 3 || cap > 0xFFFFu) {
		return -22;
	}
	return xaga_btif_h4_recv_evt(&st->pio, h4, (uint16_t)cap, XAGA_BTIF_SPINS);
}

const struct xaga_bt_platform *
hakios_mt6895_bt_platform(void)
{
	static const struct xaga_bt_platform plat = {
		.power_on     = bt_plat_power_on,
		.function_on  = bt_plat_function_on,
		.function_off = bt_plat_function_off,
		.send         = bt_plat_send,
		.receive      = bt_plat_receive,
		.ctx          = &gBt,
		/* 蓝牙地址 = Wi-Fi 地址 +1，交给 Wi-Fi 驱动学到后补；NULL 时骨架
		 * 会用 RedwoodBluetoothAddress()。
		 */
		.wifi_address = NULL,
	};

	return &plat;
}

#if !defined(HAKIOS_HOST_TEST)

static void
bt_msleep(void *ctx, uint32_t ms)
{
	bt_mmio_msleep(ctx, ms);
}

static struct mtk_bt_hci_ops gBtOps = {
	.ctx           = NULL,
	.hci_cmd_sync  = bt_hci_cmd_sync,
	.msleep        = bt_msleep,
};

const struct mtk_bt_hci_ops *
hakios_mt6895_bt_transport(void)
{
	struct hakios_bt_state *st = &gBt;

	if (!st->mapped) {
		return NULL;
	}
	gBtOps.ctx = st;
	return &gBtOps;
}
#else
extern const struct mtk_bt_hci_ops *hakios_mt6895_bt_transport(void);
#endif

#define BT_ARG      "bt"
#define BT_FW_ARG   "bt_fw"

/* struct hakios_bt_state 与 gBt 已上移到 transport 实现之前： */
/* hakios_mt6895_bt_transport() 需要读 gBt.mapped 并回填 gBt.pio 的 ctx。 */

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

	/* Liveness probe said the window answers, so it is safe to program it. */
	st->pio.bus.ctx    = (void *)st->btif;
	st->pio.bus.rd32   = bt_mmio_rd32;
	st->pio.bus.wr32   = bt_mmio_wr32;
	st->pio.bus.msleep = bt_mmio_msleep;

	if (xaga_btif_init(&st->pio) != 0) {
		printf("bt: BTIF init rejected its bus; controller left untouched\n");
		return -1;
	}

	st->mapped = true;
	printf("bt: BTIF programmed (FAKELCR normal, FIFOs flushed, TRI_LVL "
	       "TX%d/RX%d, handshake on). Transport is live; ROM patch is the "
	       "piece still missing.\n",
	       XAGA_BTIF_TX_FIFO_THRE, XAGA_BTIF_RX_FIFO_THRE);
	return 0;
}
