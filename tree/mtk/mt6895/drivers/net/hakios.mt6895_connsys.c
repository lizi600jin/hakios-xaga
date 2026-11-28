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

#if !defined(HAKIOS_HOST_TEST)
#define HAKIOS_MT6895_CONNSYS_NVDATA_IMPLEMENTATION
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.pmif.h>
#include <sys/systm.h>
extern kern_return_t thread_terminate(thread_t thread);
#endif

#include "hakios_mt6895_wireless_addrs.h"
#include "hakios_mt6895_connsys_nvdata.h"

/*
 * XNU supplies the primitives below; the host-side check (tools/check-wireless.sh)
 * supplies stubs so the same source compiles without a kernel tree.
 */
#include <stdarg.h>

#if defined(HAKIOS_HOST_TEST)
typedef unsigned long vm_offset_t;
typedef unsigned long vm_size_t;
typedef int kern_return_t;
typedef int wait_result_t;
typedef void *thread_t;
typedef void (*thread_continue_t)(void *param, wait_result_t);
typedef int pmif_status_t;
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#ifndef KERN_SUCCESS
#define KERN_SUCCESS 0
#endif
#ifndef NSEC_PER_MSEC
#define NSEC_PER_MSEC 1000000u
#endif
#ifndef PMIF_OK
#define PMIF_OK 0
#endif
#ifndef PMIF_SID_MT6363
#define PMIF_SID_MT6363 4u
#endif
#ifndef PMIF_SID_MT6368
#define PMIF_SID_MT6368 5u
#endif
#ifndef PMIF_SID_MT6685
#define PMIF_SID_MT6685 9u
#endif
extern int printf(const char *fmt, ...);
extern int PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_len);
extern void delay(int microseconds);
extern void delay_for_interval(uint32_t interval, uint32_t scale_factor);
extern kern_return_t kernel_thread_start(thread_continue_t continuation,
                                         void *parameter, thread_t *new_thread);
extern void thread_deallocate(thread_t thread);
extern thread_t current_thread(void);
extern kern_return_t thread_terminate(thread_t thread);
extern pmif_status_t pmif_init(void);
extern pmif_status_t pmif_read8(uint32_t sid, uint32_t addr, uint8_t *out);
extern pmif_status_t pmif_write8(uint32_t sid, uint32_t addr, uint32_t value);
#endif
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
/* the walker hands its lines back through this, so the window values reach the log */
extern int vprintf(const char *fmt, va_list ap);

/* Boot arguments, as the loader passes them (mtkloader sets the ones it needs). */
#define WIFI_ARG        "wifi"
#define EMI_PA_ARG      "consys_emi_pa"
#define EMI_SIZE_ARG    "consys_emi_size"

/* CONNSYS EMI is 20 MiB in the device tree (emi-size 0x1400000). The check in
 * connsys_nvdata.h wants at least 2 MiB; anything under the real size still fails
 * the MPU, so refuse it here as well rather than accept a smaller region. */
#define CONNSYS_EMI_MIN (2u * 1024u * 1024u)

/*
 * Factory /reserved-memory physical addresses verified from xaga-live.dts:
 *   - mblock-46-shared-dma-pool_wifi-reserve-memory_dma: 0xBD000000 (20 MiB, wifi@18000000)
 *   - mblock-45-consys_emi_reserved:                     0x8E500000 ( 8 MiB, conninfra/BT)
 * Pass consys_emi_pa=0xbd000000 (or override per-device) at boot.
 */
#define XAGA_LIVE_WIFI_EMI_PA    0xBD000000ULL
#define XAGA_LIVE_WIFI_EMI_SIZE  0x01400000ULL
#define XAGA_LIVE_CONSYS_EMI_PA  0x8E500000ULL
#define XAGA_LIVE_CONSYS_EMI_SIZE 0x00800000ULL

struct hakios_connsys_state {
	bool        mapped;
	bool        conn_powered;
	vm_offset_t wf_top;
	vm_offset_t wf_cfg;
	vm_offset_t wf_mcu;
	vm_offset_t wf_misc;
	vm_offset_t conn_cfg;
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
	} else if (pa == XAGA_WIN_CONSYS_6_BASE && st->conn_cfg != 0) {
		kva = st->conn_cfg; size = XAGA_WIN_CONSYS_6_SIZE;
	} else {
		/*
		 * The remaining 0x180x/0x188x/0x18Cx CSR windows belong to gated
		 * sub-domains (BGF/GPS/WFSYS/MD) and are not mapped here yet:
		 * reading an unpowered sub-domain causes a fatal External Abort.
		 * Report them as "not probed" instead of reading an unmapped page.
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
 * 3. The MT6895 CONN power sequence (verified against vendor k-xaga-16.2):
 *
 *      SPM (power-controller@1c001000)
 *        mtk-scpsys-mt6895.c:30-61 & mt6895_pos_gen.c:234-361:
 *        - POWERON_CONFIG_EN @ +0x0000: write 0x0B160001 (mask 0xFFFF0001)
 *        - MD1_PWR_CON       @ +0x0E00 (read-only telemetry here)
 *        - CONN_PWR_CON      @ +0x0E04 (MTK_SCPD_IS_PWR_CON_ON: bits[31:30]
 *          are PWR_ACK / PWR_ACK_2ND; no sram_pdn_bits on CONN)
 *        - PWR_STATUS / 2ND  @ +0x0F34 / +0x0F38 (read-only telemetry)
 *
 *      INFRACFG_AO (syscon@10001000)
 *        scpsys.h:286-289 & mt6895_pos_gen.c:311-361:
 *        - MCU_CONNSYS_PROTECT_EN/CLR/RDY_0 @ +0x0C90/+0x0C98/+0x0C9C:
 *          bit0 = AP2CONN AHB RX, bit1 = CONN2AP AXI TX
 *        - INFRASYS_PROTECT_EN/CLR/RDY_1    @ +0x0C50/+0x0C58/+0x0C5C:
 *          bit12 = AP2CONN AHB TX
 *        - INFRASYS_PROTECT_EN/CLR/RDY_0    @ +0x0C40/+0x0C48/+0x0C4C:
 *          bit8 = CONN2AP AXI RX
 *
 *      TOPCKGEN (syscon@10000000)
 *        mt6895_pos.c:84-85 & mt6895_pos_gen.c:475-501:
 *        - CLK_CFG_20_CLR (+0x158)=0xF, SET (+0x154)=0x1, UPDATE2 (+0x00C)=0x40000
 *
 *      CONN_CFG (0x18011000, only read after CONN ACK + bus_prot clear)
 *        mt6895_pos_gen.c:503-542:
 *        - IP_VERSION @ +0x0000: expects 0x02050100 (CONSYS_GEN_CONN_HW_VER)
 *
 *      MD1 sequencer (0x1C803000, read-only telemetry)
 *        md_sys1_platform.c:414-505 (0x0D124000 was md_l2sram_base, not ctrl):
 *        - CTRL @ +0x0204, STA @ +0x0310 (0x04040040=enabled, 0x01010001=disabled)
 *
 *    Reads only unless boot-arg connsys_power=1.
 * ------------------------------------------------------------------------- */

#define SCPSYS_PHYS                  0x1C001000ULL
#define SCPSYS_SIZE                  0x1000u
#define INFRACFG_AO_PHYS             0x10001000ULL
#define INFRACFG_AO_SIZE             0x1000u
#define TOPCKGEN_PHYS                0x10000000ULL
#define TOPCKGEN_SIZE                0x1000u
#define CONN_CFG_PHYS                0x18011000ULL
#define CONN_CFG_SIZE                0x1000u
#define MD1_SEQ_PHYS                 0x1C803000ULL
#define MD1_SEQ_SIZE                 0x1000u

/* SPM registers (base 0x1C001000) */
#define SPM_POWERON_CONFIG_EN        0x0000u
#define SPM_POWERON_CONFIG_MASK      0xFFFF0001u
#define SPM_POWERON_CONFIG_UNLOCK    0x0B160001u
#define SPM_MD1_PWR_CON              0x0E00u
#define SPM_CONN_PWR_CON             0x0E04u
#define SPM_PWR_STATUS               0x0F34u
#define SPM_PWR_STATUS_2ND           0x0F38u

#define SPM_PWR_RST_B_BIT            (1u << 0)
#define SPM_PWR_ISO_BIT              (1u << 1)
#define SPM_PWR_ON_BIT               (1u << 2)
#define SPM_PWR_ON_2ND_BIT           (1u << 3)
#define SPM_PWR_CLK_DIS_BIT          (1u << 4)
#define SPM_PWR_ACK_BIT              (1u << 30)
#define SPM_PWR_ACK_2ND_BIT          (1u << 31)
#define SPM_PWR_ACK_BOTH             (SPM_PWR_ACK_BIT | SPM_PWR_ACK_2ND_BIT)

/* INFRACFG_AO bus protection registers (base 0x10001000) */
#define INFRA_PROT_EN_0              0x0C40u
#define INFRA_PROT_CLR_0             0x0C48u
#define INFRA_PROT_RDY_0             0x0C4Cu
#define INFRA_PROT_EN_1              0x0C50u
#define INFRA_PROT_CLR_1             0x0C58u
#define INFRA_PROT_RDY_1             0x0C5Cu
#define MCU_CONNSYS_PROT_EN_0        0x0C90u
#define MCU_CONNSYS_PROT_CLR_0       0x0C98u
#define MCU_CONNSYS_PROT_RDY_0       0x0C9Cu

#define INFRA_PROT_CONN2AP_AXI_RX    (1u << 8)   /* INFRASYS_PROTECT_0 */
#define INFRA_PROT_AP2CONN_AHB_TX    (1u << 12)  /* INFRASYS_PROTECT_1 */
#define INFRA_PROT_AP2CONN_AHB_RX    (1u << 0)   /* MCU_CONNSYS_PROTECT_0 */
#define INFRA_PROT_CONN2AP_AXI_TX    (1u << 1)   /* MCU_CONNSYS_PROTECT_0 */

/* TOPCKGEN AP2CONN HCLK registers (base 0x10000000) */
#define TOPCKGEN_CLK_CFG_UPDATE2     0x000Cu
#define TOPCKGEN_CLK_CFG_20          0x0150u
#define TOPCKGEN_CLK_CFG_20_SET      0x0154u
#define TOPCKGEN_CLK_CFG_20_CLR      0x0158u
#define TOPCKGEN_AP2CONN_HCLK_CLR    0x0000000Fu
#define TOPCKGEN_AP2CONN_HCLK_SET    0x00000001u
#define TOPCKGEN_AP2CONN_HCLK_UPD    0x00040000u

/* GPIO diagnostic snapshot registers (base 0x10005000, read-only) */
#define GPIO_DIAG_PHYS               0x10005000ULL
#define GPIO_DIAG_SIZE               0x1000u
#define GPIO_G3_DIR                  0x0030u
#define GPIO_G3_DOUT                 0x0130u
#define GPIO_G3_DIN                  0x0230u
#define GPIO_G4_DIR                  0x0040u
#define GPIO_G4_DOUT                 0x0140u
#define GPIO_G4_DIN                  0x0240u
#define GPIO_MODE13                  0x03D0u
#define GPIO_MODE14                  0x03E0u
#define GPIO_MODE16                  0x0400u
#define GPIO_MODE20                  0x0440u
#define GPIO_MODE21                  0x0450u

/* CONN_CFG D-die IP version (base 0x18011000) */
#define CONN_CFG_IP_VERSION          0x0000u
#define CONN_CFG_HW_VER_EXPECT       0x02050100u

/* MD1 sequencer telemetry (base 0x1C803000, read-only) */
#define MD1_SEQ_CTRL                 0x0204u
#define MD1_SEQ_STA                  0x0310u
#define MD1_SEQ_STA_ENABLED          0x04040040u
#define MD1_SEQ_STA_DISABLED         0x01010001u

#define POWER_ARG                    "connsys_power"
#define READID_ARG                   "connsys_readid"
#define DELAY_S_ARG                  "connsys_delay_s"
#define POS3_ARG                     "connsys_pos3"
#define ADIE_ARG                     "connsys_adie"

/*
 * CONNINFRA POS Part 3 D-die & Step 2.3A-2 A-die register windows (verified
 * against k-xaga-16.2 mt6895_pos.c, mt6895_pos_gen.{h,c}, conn_semaphore.h).
 */
#define CONN_RGU_ON_PHYS             0x18000000ULL
#define CONN_RGU_ON_SIZE             0x1000u
#define CONN_CFG_ON_PHYS             0x18001000ULL
#define CONN_CFG_ON_SIZE             0x1000u
#define CONN_WT_SLP_CTL_PHYS         0x18003000ULL
#define CONN_WT_SLP_CTL_SIZE         0x1000u
#define CONN_BUS_CR_ON_PHYS          0x1800E000ULL
#define CONN_BUS_CR_ON_SIZE          0x1000u
#define CONN_CLKGEN_TOP_PHYS         0x18012000ULL
#define CONN_CLKGEN_TOP_SIZE         0x1000u
#define CONN_AFE_CTL_PHYS            0x18041000ULL
#define CONN_AFE_CTL_SIZE            0x1000u
#define CONN_RF_SPI_MST_PHYS         0x18042000ULL
#define CONN_RF_SPI_MST_SIZE         0x1000u
#define CONN_BUS_CR_PHYS             0x1804B000ULL
#define CONN_BUS_CR_SIZE             0x1000u
#define CONN_OFF_DBG_CTRL_AO_PHYS    0x1804D000ULL
#define CONN_OFF_DBG_CTRL_AO_SIZE    0x1000u
#define CONN_INFRA_SYSRAM_PHYS       0x18050000ULL
#define CONN_INFRA_SYSRAM_SIZE       0x5000u   /* 20 KiB: 0x18050000..0x18054FFF */
#define CONN_INFRA_SW_CR_ADIE_ID_OFS 0x3800u   /* 0x18053800 */
#define CONN_INFRA_SW_CR_EFUSE_OFS   0x3820u   /* 0x18053820 */
#define CONN_HOST_CSR_TOP_PHYS       0x18060000ULL
#define CONN_HOST_CSR_TOP_SIZE       0x1000u
#define CONN_SEMAPHORE_PHYS          0x18070000ULL
#define CONN_SEMAPHORE_SIZE          0x3000u   /* covers M2_OWN_STA/REL @ +0x2000/+0x2200 */
#define CONN_SEMA_RFSPI_INDEX        12u
#define CONN_SEMA_M2_OWN_STA(idx)    (0x2000u + ((idx) * 4u))
#define CONN_SEMA_M2_OWN_REL(idx)    (0x2200u + ((idx) * 4u))

/* D-die <-> A-die IOCFG pads (mt6895_pos_gen.h:106-124) */
#define IOCFG_RT_PHYS                0x11C30000ULL
#define IOCFG_RT_SIZE                0x1000u
#define IOCFG_RTT_PHYS               0x11EA0000ULL
#define IOCFG_RTT_SIZE               0x1000u
#define IOCFG_TR_PHYS                0x11EB0000ULL
#define IOCFG_TR_SIZE                0x1000u
#define GPIO_G5_DIR                  0x0050u
#define GPIO_G5_DOUT                 0x0150u

/* MT6363 (SID=4) / MT6368 (SID=5) / MT6685 (SID=9) PMIC registers
 * Verified directly from xaga shipped mt6363-regulator.ko (mt6363_regulators[]),
 * k-xaga-16.2 mt6895_pmic.h, mtk_power_gs_6363_array.c, mt6895_consys_reg_offset.h:
 *   VRFIO18: vsel=0x1F2C[3:0] (ldo_volt_table3[11]=1.70V -> 0x0B), vocal=0x1F2B[3:0],
 *            con0=0x1BCD (bit0=SW_EN, bit1=SW_LP), mon=0x1BD1
 *   VCN13  : vsel=0x1B3F[3:0] (ldo_volt_table1[4]=1.30V -> 0x04), vocal=0x1F88[3:0] (+50mV -> 0x05 = 1.35V),
 *            con0=0x1D07 (bit0=SW_EN, bit1=SW_LP), mon=0x1D0B
 */
#define PMIC_HWCID_LO                0x0008u
#define PMIC_HWCID_HI                0x0009u
#define MT6363_LDO_VRFIO18_CON0      0x1BCDu
#define MT6363_LDO_VRFIO18_MON       0x1BD1u
#define MT6363_LDO_VRFIO18_VOSEL     0x1F2Cu
#define MT6363_LDO_VRFIO18_VOCAL     0x1F2Bu
#define MT6363_LDO_VCN13_CON0        0x1D07u
#define MT6363_LDO_VCN13_MON         0x1D0Bu
#define MT6363_LDO_VCN13_VOSEL       0x1B3Fu
#define MT6363_LDO_VCN13_VOCAL       0x1F88u
#define MT6368_LDO_VCN33_1_MON       0x1CC4u
#define MT6368_LDO_VCN33_2_MON       0x1CD2u
#define MT6368_LDO_VANT18_MON        0x1C27u
#define MT6685_DCXO_DIGCLK_ELR       0x07F4u

static inline uint32_t
connsys_rd32(vm_offset_t base, uint32_t ofs)
{
	return *(volatile uint32_t *)(base + (vm_offset_t)ofs);
}

static inline void
connsys_wr32(vm_offset_t base, uint32_t ofs, uint32_t val)
{
	*(volatile uint32_t *)(base + (vm_offset_t)ofs) = val;
}

static inline void
connsys_rmw32(vm_offset_t base, uint32_t ofs, uint32_t mask, uint32_t val)
{
	uint32_t v = connsys_rd32(base, ofs);
	connsys_wr32(base, ofs, (v & ~mask) | (val & mask));
}

static inline void
connsys_set32(vm_offset_t base, uint32_t ofs, uint32_t bits)
{
	connsys_wr32(base, ofs, connsys_rd32(base, ofs) | bits);
}

static inline void
connsys_clr32(vm_offset_t base, uint32_t ofs, uint32_t bits)
{
	connsys_wr32(base, ofs, connsys_rd32(base, ofs) & ~bits);
}

static bool
connsys_poll_bit(vm_offset_t base, uint32_t ofs, uint32_t mask, uint32_t expect,
                 int tries, int step_us, uint32_t *last_out)
{
	uint32_t v = 0;

	for (int i = 0; i < tries; i++) {
		v = connsys_rd32(base, ofs);
		if ((v & mask) == expect) {
			if (last_out != NULL) {
				*last_out = v;
			}
			return true;
		}
		delay(step_us);
	}
	if (last_out != NULL) {
		*last_out = v;
	}
	return false;
}

static void
connsys_dump_diag(vm_offset_t gpio, vm_offset_t topckgen, const char *tag)
{
	if (gpio != 0) {
		uint32_t g4_dir  = connsys_rd32(gpio, GPIO_G4_DIR);
		uint32_t g4_dout = connsys_rd32(gpio, GPIO_G4_DOUT);
		uint32_t g4_din  = connsys_rd32(gpio, GPIO_G4_DIN);
		uint32_t m16     = connsys_rd32(gpio, GPIO_MODE16);
		uint32_t g3_dir  = connsys_rd32(gpio, GPIO_G3_DIR);
		uint32_t g3_dout = connsys_rd32(gpio, GPIO_G3_DOUT);
		uint32_t g3_din  = connsys_rd32(gpio, GPIO_G3_DIN);
		uint32_t m13     = connsys_rd32(gpio, GPIO_MODE13);
		uint32_t m14     = connsys_rd32(gpio, GPIO_MODE14);
		uint32_t m20     = connsys_rd32(gpio, GPIO_MODE20);
		uint32_t m21     = connsys_rd32(gpio, GPIO_MODE21);

		printf("connsys: GPIO135 %-6s: DIR=0x%08x DOUT=0x%08x DIN=0x%08x MODE16=0x%08x (mode=%u din=%u)\n",
		       tag, g4_dir, g4_dout, g4_din, m16, (m16 >> 28) & 0xFu, (g4_din >> 7) & 1u);
		printf("connsys: GPIO109-112 %-6s: DIR=0x%08x DOUT=0x%08x DIN=0x%08x MODE13=0x%08x MODE14=0x%08x\n",
		       tag, g3_dir, g3_dout, g3_din, m13, m14);
		printf("connsys: GPIO_CONN %-6s: MODE20(0x440)=0x%08x MODE21(0x450)=0x%08x\n",
		       tag, m20, m21);
	}
	if (topckgen != 0) {
		printf("connsys: TOPCKGEN %-6s: 0x150=0x%08x 0x154=0x%08x 0x158=0x%08x 0x00C=0x%08x\n",
		       tag,
		       connsys_rd32(topckgen, TOPCKGEN_CLK_CFG_20),
		       connsys_rd32(topckgen, TOPCKGEN_CLK_CFG_20_SET),
		       connsys_rd32(topckgen, TOPCKGEN_CLK_CFG_20_CLR),
		       connsys_rd32(topckgen, TOPCKGEN_CLK_CFG_UPDATE2));
	}
}

struct hakios_connsys_power {
	vm_offset_t scpsys;
	vm_offset_t infracfg;
	vm_offset_t topckgen;
	vm_offset_t gpio;
	vm_offset_t md1_seq;
	vm_offset_t conn_cfg;
	bool        scpsys_on;
	bool        bus_prot_clear;
	bool        chipid_ok;
	bool        md1_on;
	uint32_t    chipid;
};

static int
connsys_power_check(struct hakios_connsys_power *p, uint32_t power_level,
                    uint32_t readid)
{
	uint32_t conn_con, md1_con, pwr_sta, pwr_sta2;
	uint32_t mcu_en0, mcu_rdy0, ifr_en1, ifr_rdy1, ifr_en0, ifr_rdy0;
	uint32_t md1_ctrl, md1_sta;

	p->scpsys   = ml_io_map((vm_offset_t)SCPSYS_PHYS,      (vm_size_t)SCPSYS_SIZE);
	p->infracfg = ml_io_map((vm_offset_t)INFRACFG_AO_PHYS, (vm_size_t)INFRACFG_AO_SIZE);
	p->topckgen = ml_io_map((vm_offset_t)TOPCKGEN_PHYS,    (vm_size_t)TOPCKGEN_SIZE);
	p->gpio     = ml_io_map((vm_offset_t)GPIO_DIAG_PHYS,   (vm_size_t)GPIO_DIAG_SIZE);
	p->md1_seq  = ml_io_map((vm_offset_t)MD1_SEQ_PHYS,     (vm_size_t)MD1_SEQ_SIZE);
	if (p->scpsys == 0 || p->infracfg == 0 || p->topckgen == 0 || p->md1_seq == 0) {
		printf("connsys: power controllers did not map "
		       "(spm=%d infra=%d topck=%d gpio=%d md1_seq=%d)\n",
		       p->scpsys != 0, p->infracfg != 0, p->topckgen != 0,
		       p->gpio != 0, p->md1_seq != 0);
		return -1;
	}

	/* 1. Read-only baseline telemetry (Run A & pre-power-on bisect) */
	conn_con = connsys_rd32(p->scpsys, SPM_CONN_PWR_CON);
	md1_con  = connsys_rd32(p->scpsys, SPM_MD1_PWR_CON);
	pwr_sta  = connsys_rd32(p->scpsys, SPM_PWR_STATUS);
	pwr_sta2 = connsys_rd32(p->scpsys, SPM_PWR_STATUS_2ND);

	mcu_en0  = connsys_rd32(p->infracfg, MCU_CONNSYS_PROT_EN_0);
	mcu_rdy0 = connsys_rd32(p->infracfg, MCU_CONNSYS_PROT_RDY_0);
	ifr_en1  = connsys_rd32(p->infracfg, INFRA_PROT_EN_1);
	ifr_rdy1 = connsys_rd32(p->infracfg, INFRA_PROT_RDY_1);
	ifr_en0  = connsys_rd32(p->infracfg, INFRA_PROT_EN_0);
	ifr_rdy0 = connsys_rd32(p->infracfg, INFRA_PROT_RDY_0);

	p->scpsys_on = (conn_con & SPM_PWR_ACK_BOTH) == SPM_PWR_ACK_BOTH;
	p->bus_prot_clear =
		((mcu_rdy0 & INFRA_PROT_AP2CONN_AHB_RX) == 0u) &&
		((ifr_rdy1 & INFRA_PROT_AP2CONN_AHB_TX) == 0u) &&
		((ifr_en0  & INFRA_PROT_CONN2AP_AXI_RX) == 0u) &&
		((mcu_en0  & INFRA_PROT_CONN2AP_AXI_TX) == 0u);

	printf("connsys: SPM CONN_PWR_CON(0x0e04)=0x%08x MD1_PWR_CON(0x0e00)=0x%08x "
	       "PWR_STA=0x%08x/0x%08x -> CONN %s\n",
	       conn_con, md1_con, pwr_sta, pwr_sta2,
	       p->scpsys_on ? "powered" : "NOT powered");
	printf("connsys: INFRACFG prot mcu0 en/rdy=0x%08x/0x%08x "
	       "ifr1 en/rdy=0x%08x/0x%08x ifr0 en/rdy=0x%08x/0x%08x -> bus_prot %s\n",
	       mcu_en0, mcu_rdy0, ifr_en1, ifr_rdy1, ifr_en0, ifr_rdy0,
	       p->bus_prot_clear ? "CLEAR" : "ACTIVE");

	md1_ctrl = connsys_rd32(p->md1_seq, MD1_SEQ_CTRL);
	md1_sta  = connsys_rd32(p->md1_seq, MD1_SEQ_STA);
	p->md1_on = (md1_sta == MD1_SEQ_STA_ENABLED);
	printf("connsys: MD1_SEQ(0x1c803000) ctrl=0x%08x sta=0x%08x -> %s\n",
	       md1_ctrl, md1_sta,
	       p->md1_on ? "enabled" :
	       (md1_sta == MD1_SEQ_STA_DISABLED ? "disabled" : "unknown"));

	connsys_dump_diag(p->gpio, p->topckgen, "before");

	/*
	 * 2. MT6895 CONN power-on sequence (mt6895_pos_gen.c:248-362 + 503-542):
	 *    connsys_power=0:  read-only baseline
	 *    connsys_power>=1: SPM CONN MTCMOS + INFRACFG_AO 4 bus_prot clear
	 *                      (TOPCKGEN untouched; LK leaves ap2conn_host_sel=1).
	 *    connsys_readid=0: (default) safe power-on; skips 0x18011000 & 39-window probe.
	 *    connsys_readid=1: also reads CONN_CFG (0x18011000) IP_VERSION (0x02050100)
	 *                      and runs the 39-window MMIO probe.
	 */
	if (power_level >= 1u) {
		uint32_t v;
		bool ack1_ok = true, ack2_ok = true;
		bool rx_ok, tx_ok;

		printf("connsys: raising CONN domain (connsys_power=%u, connsys_readid=%u, no TOPCKGEN write)\n",
		       power_level, readid);

		/* Step 1: SPM CONN MTCMOS */
		if (!p->scpsys_on) {
			/* Unlock SPM register writes (project code 0x0B16 | BCLK_CG_EN) */
			v = connsys_rd32(p->scpsys, SPM_POWERON_CONFIG_EN);
			connsys_wr32(p->scpsys, SPM_POWERON_CONFIG_EN,
				     (v & ~SPM_POWERON_CONFIG_MASK) | SPM_POWERON_CONFIG_UNLOCK);

			/* Assert primary PWR_ON (bit2) and wait for PWR_ACK (bit30) */
			v = connsys_rd32(p->scpsys, SPM_CONN_PWR_CON) | SPM_PWR_ON_BIT;
			connsys_wr32(p->scpsys, SPM_CONN_PWR_CON, v);
			ack1_ok = connsys_poll_bit(p->scpsys, SPM_CONN_PWR_CON,
						   SPM_PWR_ACK_BIT, SPM_PWR_ACK_BIT,
						   10, 500, &conn_con);

			/* Assert secondary PWR_ON_2ND (bit3) and wait for PWR_ACK_2ND (bit31) */
			v = connsys_rd32(p->scpsys, SPM_CONN_PWR_CON) | SPM_PWR_ON_2ND_BIT;
			connsys_wr32(p->scpsys, SPM_CONN_PWR_CON, v);
			ack2_ok = connsys_poll_bit(p->scpsys, SPM_CONN_PWR_CON,
						   SPM_PWR_ACK_2ND_BIT, SPM_PWR_ACK_2ND_BIT,
						   10, 500, &conn_con);

			if (ack1_ok && ack2_ok) {
				/* CLK_DIS=0 -> 1us -> ISO=0 -> RST_B=1 -> 500us */
				v = connsys_rd32(p->scpsys, SPM_CONN_PWR_CON) & ~SPM_PWR_CLK_DIS_BIT;
				connsys_wr32(p->scpsys, SPM_CONN_PWR_CON, v);
				delay(1);

				v &= ~SPM_PWR_ISO_BIT;
				connsys_wr32(p->scpsys, SPM_CONN_PWR_CON, v);

				v |= SPM_PWR_RST_B_BIT;
				connsys_wr32(p->scpsys, SPM_CONN_PWR_CON, v);
				delay(500);

				conn_con = connsys_rd32(p->scpsys, SPM_CONN_PWR_CON);
				p->scpsys_on = (conn_con & SPM_PWR_ACK_BOTH) == SPM_PWR_ACK_BOTH;
			}
			printf("connsys: SPM CONN_PWR_CON now 0x%08x (ack1=%d ack2=%d) -> %s\n",
			       conn_con, ack1_ok, ack2_ok,
			       p->scpsys_on ? "UP" : "FAILED");
		}

		/* Step 2: INFRACFG_AO bus_prot clear */
		if (p->scpsys_on) {
			connsys_wr32(p->infracfg, MCU_CONNSYS_PROT_CLR_0, INFRA_PROT_AP2CONN_AHB_RX);
			rx_ok = connsys_poll_bit(p->infracfg, MCU_CONNSYS_PROT_RDY_0,
						 INFRA_PROT_AP2CONN_AHB_RX, 0u,
						 100, 500, &mcu_rdy0);

			connsys_wr32(p->infracfg, INFRA_PROT_CLR_1, INFRA_PROT_AP2CONN_AHB_TX);
			tx_ok = connsys_poll_bit(p->infracfg, INFRA_PROT_RDY_1,
						 INFRA_PROT_AP2CONN_AHB_TX, 0u,
						 100, 500, &ifr_rdy1);

			connsys_wr32(p->infracfg, INFRA_PROT_CLR_0, INFRA_PROT_CONN2AP_AXI_RX);
			connsys_wr32(p->infracfg, MCU_CONNSYS_PROT_CLR_0, INFRA_PROT_CONN2AP_AXI_TX);
			delay(5000);

			mcu_en0  = connsys_rd32(p->infracfg, MCU_CONNSYS_PROT_EN_0);
			mcu_rdy0 = connsys_rd32(p->infracfg, MCU_CONNSYS_PROT_RDY_0);
			ifr_en1  = connsys_rd32(p->infracfg, INFRA_PROT_EN_1);
			ifr_rdy1 = connsys_rd32(p->infracfg, INFRA_PROT_RDY_1);
			ifr_en0  = connsys_rd32(p->infracfg, INFRA_PROT_EN_0);
			ifr_rdy0 = connsys_rd32(p->infracfg, INFRA_PROT_RDY_0);

			p->bus_prot_clear = rx_ok && tx_ok;
			printf("connsys: INFRACFG prot after clear rx_ok=%d tx_ok=%d "
			       "mcu0=0x%08x/0x%08x ifr1=0x%08x/0x%08x ifr0=0x%08x/0x%08x\n",
			       rx_ok, tx_ok, mcu_en0, mcu_rdy0,
			       ifr_en1, ifr_rdy1, ifr_en0, ifr_rdy0);
		}

		/* Step 3: Optional CONN_CFG (0x18011000) IP_VERSION read (gated by connsys_readid=1) */
		if (p->scpsys_on && p->bus_prot_clear) {
			if (readid != 0u) {
				p->conn_cfg = ml_io_map((vm_offset_t)CONN_CFG_PHYS,
							(vm_size_t)CONN_CFG_SIZE);
				if (p->conn_cfg == 0) {
					printf("connsys: CONN_CFG (0x18011000) did not map\n");
				} else {
					uint32_t ver = 0;
					int tries = 0;

					for (int i = 0; i < 11; i++) {
						tries = i + 1;
						ver = connsys_rd32(p->conn_cfg, CONN_CFG_IP_VERSION);
						if (ver == CONN_CFG_HW_VER_EXPECT) {
							p->chipid_ok = true;
							break;
						}
						delay(1000);
					}
					p->chipid = ver;
					printf("connsys: CONN_CFG IP_VERSION(0x18011000)=0x%08x "
					       "(expect 0x%08x, tries=%d) -> %s\n",
					       ver, CONN_CFG_HW_VER_EXPECT, tries,
					       p->chipid_ok ? "MATCH" : "MISMATCH");
				}
			} else {
				printf("connsys: CONN_CFG (0x18011000) read skipped (connsys_readid=0)\n");
			}
		}

		connsys_dump_diag(p->gpio, p->topckgen, "after");
	}

	if (!p->scpsys_on) {
		printf("connsys: CONN domain is down (0x18011000 NOT touched; "
		       "pass connsys_power=1 to raise it).\n");
	} else if (!p->bus_prot_clear) {
		printf("connsys: CONN domain is up but INFRACFG bus_prot still active "
		       "(0x18011000 NOT touched).\n");
	}
	return (p->scpsys_on && p->bus_prot_clear) ? 0 : -1;
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

/*
 * Said for each window the walker reads. The walker gives the format and the values and says
 * nothing itself when no logger is set, which is why the four 0x184x read-backs were missing
 * from every log until now: the whole point of the read is what each window answers.
 */
static void
connsys_probe_log(void *ctx, const char *fmt, ...)
{
	va_list ap;

	(void)ctx;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

/*
 * MT6895 CONN_INFRA CMDBT restore microcode (1024 x 32-bit = 4096 bytes),
 * extracted verbatim from vendor k-xaga-16.2 mt6895_pos_gen.c:35-128.
 * Indices [509..1023] are zero-initialized by C.
 */
static const uint32_t g_cmdbt_dwn_value_ary_mt6895[1024] = {
	0x16001040u, 0x16011801u, 0x16100001u, 0x16110000u, 0x31000100u, 0x16000400u, 0x16011805u, 0x16100A00u,
	0x16111805u, 0x1620006Fu, 0x16210000u, 0xCCCCCCCCu, 0x16001044u, 0x16011801u, 0x1610FFFEu, 0x1611FFFFu,
	0x31000100u, 0x16002060u, 0x16011801u, 0x1610FFFFu, 0x1611FFFFu, 0x31000100u, 0x16002070u, 0x16011801u,
	0x1610FFFFu, 0x16110000u, 0x31000100u, 0x1600C000u, 0x16011801u, 0x16100000u, 0x16110000u, 0x31000100u,
	0x1600C008u, 0x16011801u, 0x16100000u, 0x16110000u, 0x31000100u, 0x1600D00Cu, 0x16011801u, 0x161000BFu,
	0x16110000u, 0x31000100u, 0x16000740u, 0x16011805u, 0x16100D40u, 0x16111805u, 0x1620002Du, 0x16210000u,
	0xCCCCCCCCu, 0x16001040u, 0x16011801u, 0x32100000u, 0x16200002u, 0x16210000u, 0x19300102u, 0x31000300u,
	0x1600071Cu, 0x16011805u, 0x16100D1Cu, 0x16111805u, 0x16200009u, 0x16210000u, 0xCCCCCCCCu, 0x16001044u,
	0x16011801u, 0x32100000u, 0x1620FFFDu, 0x1621FFFFu, 0x17300102u, 0x31000300u, 0x160005C4u, 0x16011805u,
	0x16100BC4u, 0x16111805u, 0x1620001Bu, 0x16210000u, 0xCCCCCCCCu, 0x1600C0D0u, 0x16011801u, 0x16100001u,
	0x16110000u, 0x31000100u, 0x16000630u, 0x16011805u, 0x16100C30u, 0x16111805u, 0x1620003Bu, 0x16210000u,
	0xCCCCCCCCu, 0x160005BCu, 0x16011805u, 0x16100BBCu, 0x16111805u, 0x16200002u, 0x16210000u, 0xCCCCCCCCu,
	0x1600D000u, 0x16011804u, 0x16100200u, 0x16110000u, 0x31000100u, 0x1600D000u, 0x16011804u, 0x16100200u,
	0x161107F4u, 0x31000100u, 0x1600D000u, 0x16011804u, 0x16100204u, 0x161107F4u, 0x31000100u, 0x1600D000u,
	0x16011804u, 0x1610020Cu, 0x161107F4u, 0x31000100u, 0x1600D000u, 0x16011804u, 0x1610001Cu, 0x161107F4u,
	0x31000100u, 0x06000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
	0x16000400u, 0x16011805u, 0x16100A00u, 0x16111805u, 0x16200071u, 0x16210000u, 0xBBBBBBBBu, 0x16002060u,
	0x16011801u, 0x1610FFFFu, 0x1611FFFFu, 0x31000100u, 0x16002070u, 0x16011801u, 0x1610FFFFu, 0x16110000u,
	0x31000100u, 0x160005C4u, 0x16011805u, 0x16100BC4u, 0x16111805u, 0x16200056u, 0x16210000u, 0xBBBBBBBBu,
	0x1600C000u, 0x16011801u, 0x16100000u, 0x16110000u, 0x31000100u, 0x1600C008u, 0x16011801u, 0x16100000u,
	0x16110000u, 0x31000100u, 0x1600071Cu, 0x16011805u, 0x16100D1Cu, 0x16111805u, 0x16200009u, 0x16210000u,
	0xBBBBBBBBu, 0x1600D00Cu, 0x16011801u, 0x161000BFu, 0x16110000u, 0x31000100u, 0x16000740u, 0x16011805u,
	0x16100D40u, 0x16111805u, 0x1620002Du, 0x16210000u, 0xBBBBBBBBu, 0x06000000u, 0x00000000u, 0x00000000u,
	0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
	0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
	0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
	0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
	0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
	0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
	0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
	0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
	0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
	0x18041014u, 0x180410F4u, 0x1804100Cu, 0x18041040u, 0x18041044u, 0x18041100u, 0x18011050u, 0x18011100u,
	0x18011104u, 0x18012000u, 0x18012004u, 0x18012008u, 0x18012020u, 0x18012050u, 0x1804B000u, 0x1804B004u,
	0x1804B008u, 0x1804B010u, 0x1804B024u, 0x1804B070u, 0x1804B074u, 0x1804B350u, 0x1804B354u, 0x1804B358u,
	0x1804B35Cu, 0x1804B360u, 0x1804B364u, 0x1804B368u, 0x1804B36Cu, 0x1804B370u, 0x1804B374u, 0x1804B378u,
	0x1804B37Cu, 0x1804B380u, 0x1804B384u, 0x1804B388u, 0x1804B38Cu, 0x1804B390u, 0x1804B394u, 0x1804B398u,
	0x1804B39Cu, 0x1804B3A0u, 0x1804B3A4u, 0x1804B3A8u, 0x1804B3ACu, 0x1804B3B0u, 0x1804B3B4u, 0x1804B3B8u,
	0x1804B3BCu, 0x1804B3C0u, 0x1804B3C4u, 0x1804B3C8u, 0x1804B3CCu, 0x1804B3D0u, 0x1804B3D4u, 0x1804B3D8u,
	0x1804B3DCu, 0x1804B3E0u, 0x1804B3E4u, 0x1804B3E8u, 0x1804B3ECu, 0x18040004u, 0x18040008u, 0x1804000Cu,
	0x18040010u, 0x18040014u, 0x18040018u, 0x18040000u, 0x1804001Cu, 0x18040020u, 0x18040024u, 0x18040028u,
	0x18042008u, 0x18012030u, 0x18012038u, 0x18016308u, 0x1801630Cu, 0x18016310u, 0x18016314u, 0x18016318u,
	0x1801631Cu, 0x18016320u, 0x18016324u, 0x18016328u, 0x1801632Cu, 0x18016330u, 0x18016334u, 0x18016338u,
	0x1801633Cu, 0x18016340u, 0x18016344u, 0x18016348u, 0x1801634Cu, 0x18016350u, 0x18016354u, 0x18016358u,
	0x1801635Cu, 0x18016360u, 0x18016364u, 0x180030A8u, 0x18003120u, 0x18003124u, 0x18003128u, 0x1800312Cu,
	0x18003130u, 0x18003134u, 0x18041024u, 0x18041028u, 0x1804102Cu, 0x18041030u, 0x18041034u, 0x18012060u,
	0x18012070u, 0x1801C000u, 0x1801C004u, 0x1801C008u, 0x1801C0C0u, 0x1801C0C8u, 0x1801C0D4u, 0x1801C0D8u,
	0x1801C0DCu, 0x1801C0E0u, 0x1801C0E4u, 0x1801C0E8u, 0x1801C0ECu, 0x1801C0F0u, 0x1801C0F4u, 0x1801C0F8u,
	0x1801C0FCu, 0x1801C100u, 0x1801C104u, 0x1801C108u, 0x1801C10Cu, 0x1801C110u, 0x1801C114u, 0x1801C118u,
	0x1801C11Cu, 0x1801C120u, 0x1801C124u, 0x1801C128u, 0x1801C0D0u, 0x1801C200u, 0x1801C204u, 0x1801C208u,
	0x1801C214u, 0x1801C230u, 0x1801C238u, 0x1801C23Cu, 0x1801C2C0u, 0x1801C2C4u, 0x1801C2C8u, 0x1801C2CCu,
	0x1801C2D0u, 0x1801C2D4u, 0x1801C2D8u, 0x1801C2DCu, 0x1801C400u, 0x1801C404u, 0x1801C408u, 0x1801C464u,
	0x1801C468u, 0x1801C46Cu, 0x1801C4D0u, 0x1801C530u, 0x1801C534u, 0x1801C58Cu, 0x1801C590u, 0x1801C594u,
	0x1801C598u, 0x1801C59Cu, 0x1801C5A0u, 0x1801C5A4u, 0x1801C5A8u, 0x1801C5ACu, 0x1801C5B0u, 0x1801C5B4u,
	0x1801C5B8u, 0x1801C5BCu, 0x1801C5C0u, 0x1801C5C4u, 0x1801C5C8u, 0x1801C5CCu, 0x1801C5D0u, 0x1801C5D4u,
	0x1801C5D8u, 0x1801C5E0u, 0x1801C5E4u, 0x1801C654u, 0x1801C754u, 0x1801C7B0u, 0x1801C7B4u, 0x1801C7C0u,
	0x1801C7E0u, 0x1801C800u, 0x1801C830u, 0x1801C254u, 0x1801C258u, 0x1801C294u, 0x1801C298u, 0x1801D00Cu,
	0x1801D008u, 0x1801D024u, 0x1801D028u, 0x1801D02Cu, 0x1801D03Cu, 0x1801D04Cu, 0x1801D054u, 0x1801D058u,
	0x1801D000u, 0x1801D004u, 0x1801B04Cu, 0x1801B0BCu, 0x1801B070u, 0x1801B074u, 0x1801B500u, 0x1801A000u,
	0x1801A004u, 0x1801A008u, 0x1801A00Cu, 0x1801A018u, 0x1801A024u, 0x1801A028u, 0x1801A02Cu, 0x1801A110u,
	0x1801A200u, 0x1801A204u, 0x1801A208u, 0x1801A20Cu, 0x1801A210u, 0x1801A214u, 0x1801A218u, 0x1801A21Cu,
	0x1801A228u, 0x1801A22Cu, 0x1801A230u, 0x1801A234u, 0x1801A238u, 0x1801A23Cu, 0x1801A240u, 0x1801A244u,
	0x1801A248u, 0x1801A304u, 0x1801A30Cu, 0x1801A310u, 0x1801A314u, 0x1801A604u, 0x1801A608u, 0x1801A610u,
	0x1801A620u, 0x1801A630u, 0x1801A640u, 0x1801A650u, 0x1801A660u,
};

/*
 * Step 2.3A-1: CONNINFRA POS Part 3 D-die foundation (zero PMIC writes, zero
 * GPIO pinmux writes, and does NOT enter dsleep yet so Step 2.3A-2 A-die SPI
 * and Step 2.4 BGF bringup can access 0x180x directly).
 *
 * Staged cumulatively via connsys_pos3=1..5:
 *   1: EMI Remap (0x1804B354 + 4 peri bases)
 *   2: + SYSRAM 20KiB zero + EMI path + D-die Efuse + RGU sleep ctl
 *   3: + WT_SLP 11-reg A-die routing table (0x18003070..0x98)
 *   4: + Subsys PLL 26M initial (0x180410F4/04/0C)
 *   5: + Bus DCM/timeout + clocks + CMDBT 4KiB download
 */
static int
connsys_pos3_d_die_init(struct hakios_connsys_state *st, uint32_t max_stage)
{
	vm_offset_t rgu_on, wt_slp, bus_cr_on, conn_cfg, clkgen;
	vm_offset_t afe_ctl, bus_cr, off_dbg, sysram, host_csr;
	uint32_t mcu_remap, wf_peri, bt_peri, gps_peri, scp_sram;
	uint32_t d_die_efuse, efuse_rb, emi_s, emi_e;
	uint32_t wt_wf_ck, wt_gps_l5;
	uint32_t pll_stb, dig_en02, dig_top01;
	uint32_t cmdbt_addr, cmdbt_w0, cmdbt_w128, cmdbt_w508, ckgen_bus;
	const uint32_t emi_remap_val = (uint32_t)(XAGA_LIVE_CONSYS_EMI_PA >> 16);

	if (!st->conn_powered) {
		printf("connsys: pos3 skipped (CONN domain not powered)\n");
		return -1;
	}

	rgu_on    = ml_io_map((vm_offset_t)CONN_RGU_ON_PHYS,          (vm_size_t)CONN_RGU_ON_SIZE);
	wt_slp    = ml_io_map((vm_offset_t)CONN_WT_SLP_CTL_PHYS,      (vm_size_t)CONN_WT_SLP_CTL_SIZE);
	bus_cr_on = ml_io_map((vm_offset_t)CONN_BUS_CR_ON_PHYS,       (vm_size_t)CONN_BUS_CR_ON_SIZE);
	conn_cfg  = (st->conn_cfg != 0)
		    ? st->conn_cfg
		    : ml_io_map((vm_offset_t)CONN_CFG_PHYS,           (vm_size_t)CONN_CFG_SIZE);
	clkgen    = ml_io_map((vm_offset_t)CONN_CLKGEN_TOP_PHYS,      (vm_size_t)CONN_CLKGEN_TOP_SIZE);
	afe_ctl   = ml_io_map((vm_offset_t)CONN_AFE_CTL_PHYS,         (vm_size_t)CONN_AFE_CTL_SIZE);
	bus_cr    = ml_io_map((vm_offset_t)CONN_BUS_CR_PHYS,          (vm_size_t)CONN_BUS_CR_SIZE);
	off_dbg   = ml_io_map((vm_offset_t)CONN_OFF_DBG_CTRL_AO_PHYS, (vm_size_t)CONN_OFF_DBG_CTRL_AO_SIZE);
	sysram    = ml_io_map((vm_offset_t)CONN_INFRA_SYSRAM_PHYS,    (vm_size_t)CONN_INFRA_SYSRAM_SIZE);
	host_csr  = ml_io_map((vm_offset_t)CONN_HOST_CSR_TOP_PHYS,    (vm_size_t)CONN_HOST_CSR_TOP_SIZE);

	if (!rgu_on || !wt_slp || !bus_cr_on || !conn_cfg || !clkgen ||
	    !afe_ctl || !bus_cr || !off_dbg || !sysram || !host_csr) {
		printf("connsys: pos3 map failed (rgu=%d wt=%d bco=%d cfg=%d clk=%d "
		       "afe=%d bc=%d dbg=%d sram=%d hcsr=%d)\n",
		       rgu_on != 0, wt_slp != 0, bus_cr_on != 0, conn_cfg != 0,
		       clkgen != 0, afe_ctl != 0, bus_cr != 0, off_dbg != 0,
		       sysram != 0, host_csr != 0);
		return -1;
	}
	st->conn_cfg = conn_cfg;

	printf("connsys: pos3 D-die init starting (max_stage=%u, consys_emi=0x%llx -> remap=0x%05x)\n",
	       max_stage, (unsigned long long)XAGA_LIVE_CONSYS_EMI_PA, emi_remap_val);

	/* 1. EMI Remap (mt6895_pos_gen.c:544-599) */
	connsys_rmw32(bus_cr, 0x354u, 0x000FFFFFu, emi_remap_val);
	connsys_rmw32(bus_cr, 0x364u, 0x000FFFFFu, 0x00001000u);
	connsys_rmw32(bus_cr, 0x368u, 0x000FFFFFu, 0x00001E00u);
	connsys_rmw32(bus_cr, 0x36Cu, 0x000FFFFFu, 0x00001000u);
	connsys_rmw32(bus_cr, 0x370u, 0x000FFFFFu, 0x00001C60u);

	mcu_remap = connsys_rd32(bus_cr, 0x354u);
	wf_peri   = connsys_rd32(bus_cr, 0x364u);
	bt_peri   = connsys_rd32(bus_cr, 0x368u);
	gps_peri  = connsys_rd32(bus_cr, 0x36Cu);
	scp_sram  = connsys_rd32(bus_cr, 0x370u);
	printf("connsys: pos3[1] EMI remap 0x354=0x%08x (%s) wf=0x%08x bt=0x%08x gps=0x%08x scp=0x%08x\n",
	       mcu_remap,
	       ((mcu_remap & 0x000FFFFFu) == emi_remap_val) ? "MATCH" : "MISMATCH",
	       wf_peri, bt_peri, gps_peri, scp_sram);

	if (max_stage < 2u) {
		printf("connsys: pos3 stopped after stage 1 (connsys_pos3=%u)\n", max_stage);
		return ((mcu_remap & 0x000FFFFFu) == emi_remap_val) ? 0 : -1;
	}

	/* 2. SYSRAM zero (20 KiB) + EMI path window + D-die Efuse + RGU sleep ctl */
	for (uint32_t ofs = 0; ofs < CONN_INFRA_SYSRAM_SIZE; ofs += 4u) {
		connsys_wr32(sysram, ofs, 0u);
	}
	connsys_wr32(bus_cr, 0x070u, 0x04000000u);
	connsys_wr32(bus_cr, 0x074u, 0x043FFFFFu);

	d_die_efuse = connsys_rd32(conn_cfg, 0x020u);
	connsys_wr32(sysram, CONN_INFRA_SW_CR_EFUSE_OFS, d_die_efuse);
	efuse_rb = connsys_rd32(sysram, CONN_INFRA_SW_CR_EFUSE_OFS);

	connsys_wr32(rgu_on, 0x050u, 0u);
	connsys_wr32(rgu_on, 0x054u, 3u);
	connsys_wr32(rgu_on, 0x070u, 0u);
	connsys_wr32(rgu_on, 0x074u, 1u);

	emi_s = connsys_rd32(bus_cr, 0x070u);
	emi_e = connsys_rd32(bus_cr, 0x074u);
	printf("connsys: pos3[2] SYSRAM(20KiB) zeroed, emi_path=0x%08x..0x%08x, "
	       "efuse=0x%08x->0x%08x (%s), rgu=0x%x/0x%x/0x%x/0x%x\n",
	       emi_s, emi_e, d_die_efuse, efuse_rb,
	       (efuse_rb == d_die_efuse) ? "OK" : "MISMATCH",
	       connsys_rd32(rgu_on, 0x050u), connsys_rd32(rgu_on, 0x054u),
	       connsys_rd32(rgu_on, 0x070u), connsys_rd32(rgu_on, 0x074u));

	if (max_stage < 3u) {
		printf("connsys: pos3 stopped after stage 2 (connsys_pos3=%u)\n", max_stage);
		return ((mcu_remap & 0x000FFFFFu) == emi_remap_val) ? 0 : -1;
	}

	/* 3. WT_SLP / SPI Master A-die routing table (11 regs: 0x18003070..0x98) */
	connsys_rmw32(wt_slp, 0x070u, 0x0FFF0FFFu, 0x0AF40A04u);
	connsys_rmw32(wt_slp, 0x074u, 0x0FFF0FFFu, 0x00A00090u);
	connsys_rmw32(wt_slp, 0x078u, 0x0FFF0FFFu, 0x009C008Cu);
	connsys_rmw32(wt_slp, 0x07Cu, 0x00000FFFu, 0x00000A08u);
	connsys_rmw32(wt_slp, 0x080u, 0x00000FFFu, 0x00000094u);
	connsys_rmw32(wt_slp, 0x084u, 0x00000FFFu, 0x00000A00u);
	connsys_rmw32(wt_slp, 0x088u, 0x0FFF0FFFu, 0x0AFC0A0Cu);
	connsys_rmw32(wt_slp, 0x08Cu, 0x00000FFFu, 0x000000F0u);
	connsys_rmw32(wt_slp, 0x090u, 0x00000FFFu, 0x000000F4u);
	connsys_rmw32(wt_slp, 0x094u, 0x00000FFFu, 0x000000FCu);
	connsys_rmw32(wt_slp, 0x098u, 0x00000FFFu, 0x000000F8u);

	wt_wf_ck  = connsys_rd32(wt_slp, 0x070u);
	wt_gps_l5 = connsys_rd32(wt_slp, 0x098u);
	printf("connsys: pos3[3] WT_SLP table set (0x70=0x%08x 0x98=0x%08x)\n",
	       wt_wf_ck, wt_gps_l5);

	if (max_stage < 4u) {
		printf("connsys: pos3 stopped after stage 3 (connsys_pos3=%u)\n", max_stage);
		return ((mcu_remap & 0x000FFFFFu) == emi_remap_val) ? 0 : -1;
	}

	/* 4. Subsys PLL initial (XTAL 26M, mt6895_pos_gen.c:1703-1719) */
	connsys_rmw32(afe_ctl, 0x0F4u, 0x7FFF7FFFu, 0x03140521u);
	connsys_rmw32(afe_ctl, 0x004u, 0x000300CFu, 0x00030047u);
	connsys_rmw32(afe_ctl, 0x00Cu, 0x00078000u, 0x00068000u);

	pll_stb   = connsys_rd32(afe_ctl, 0x0F4u);
	dig_en02  = connsys_rd32(afe_ctl, 0x004u);
	dig_top01 = connsys_rd32(afe_ctl, 0x00Cu);
	printf("connsys: pos3[4] Subsys PLL 0xF4=0x%08x 0x04=0x%08x 0x0C=0x%08x\n",
	       pll_stb, dig_en02, dig_top01);

	if (max_stage < 5u) {
		printf("connsys: pos3 stopped after stage 4 (connsys_pos3=%u)\n", max_stage);
		return ((mcu_remap & 0x000FFFFFu) == emi_remap_val) ? 0 : -1;
	}

	/* 5. Bus timeouts/DCM + CONN_INFRA clocks + CMDBT download (NO dsleep bit0 yet) */
	connsys_rmw32(rgu_on, 0x020u, 0xFFFF0040u, 0x42540000u);
	connsys_rmw32(rgu_on, 0x010u, 0xFFFF0040u, 0x57460000u);

	connsys_set32(conn_cfg, 0x100u, (1u << 18));
	connsys_rmw32(conn_cfg, 0x100u, 0x00007FF0u, 0x00003FF0u);
	connsys_set32(conn_cfg, 0x100u, (1u << 15));

	connsys_set32(bus_cr_on, 0x110u, (1u << 14));
	connsys_set32(bus_cr_on, 0x110u, (1u << 16));
	connsys_set32(bus_cr_on, 0x104u, (1u << 14));
	connsys_set32(bus_cr_on, 0x104u, (1u << 16));

	connsys_rmw32(bus_cr, 0x374u, 0x00000C63u, 0x00000C63u);

	connsys_rmw32(bus_cr, 0x024u, 0x000007F8u, 0x00000110u);
	connsys_set32(bus_cr, 0x024u, (1u << 0));
	connsys_rmw32(bus_cr_on, 0x038u, 0x000007F8u, 0x00000188u);
	connsys_set32(bus_cr_on, 0x038u, (1u << 0));
	connsys_rmw32(bus_cr_on, 0x024u, 0x000007F8u, 0x00000498u);
	connsys_set32(bus_cr_on, 0x024u, (1u << 0));

	connsys_set32(off_dbg, 0x000u, (1u << 9));
	connsys_rmw32(off_dbg, 0x000u, 0xFFFF0000u, 0x07F40000u);
	connsys_set32(off_dbg, 0x000u, (1u << 2));
	connsys_set32(off_dbg, 0x000u, (1u << 3));
	connsys_set32(off_dbg, 0x000u, (1u << 4));
	connsys_clr32(off_dbg, 0x000u, (1u << 9));

	connsys_set32(clkgen, 0x000u, (1u << 0));
	connsys_set32(clkgen, 0x004u, (1u << 0));
	connsys_set32(clkgen, 0x008u, (1u << 0));
	connsys_set32(clkgen, 0x00Cu, (1u << 0));

	connsys_clr32(clkgen, 0x038u, (1u << 7));
	connsys_set32(clkgen, 0x038u, (1u << 6));
	connsys_set32(clkgen, 0x038u, (1u << 4));

	connsys_set32(host_csr, 0x380u, (1u << 31));
	connsys_set32(host_csr, 0x380u, (1u << 0));

	connsys_wr32(clkgen, 0x060u, 0xFFFFFFFFu);
	connsys_rmw32(clkgen, 0x070u, 0x0000FFFFu, 0x0000FFFFu);
	connsys_clr32(clkgen, 0x050u, (1u << 0));

	/* CMDBT_FETCH_START_ADDR0 (0x18011050) is 20-bit [19:0] (CODA/conn_cfg.h:268) */
	connsys_wr32(conn_cfg, 0x050u, 0x18050200u);
	for (uint32_t i = 0; i < 1024u; i++) {
		connsys_wr32(sysram, i * 4u, g_cmdbt_dwn_value_ary_mt6895[i]);
	}

	cmdbt_addr = connsys_rd32(conn_cfg, 0x050u);
	cmdbt_w0   = connsys_rd32(sysram, 0x000u);
	cmdbt_w128 = connsys_rd32(sysram, 128u * 4u);
	cmdbt_w508 = connsys_rd32(sysram, 508u * 4u);
	ckgen_bus  = connsys_rd32(clkgen, 0x050u);
	printf("connsys: pos3[5] clocks+CMDBT done: fetch=0x%08x w0=0x%08x w128=0x%08x "
	       "w508=0x%08x ckgen_bus=0x%08x (%s, dsleep NOT entered)\n",
	       cmdbt_addr, cmdbt_w0, cmdbt_w128, cmdbt_w508, ckgen_bus,
	       ((cmdbt_addr & 0x000FFFFFu) == 0x00050200u &&
		cmdbt_w0   == g_cmdbt_dwn_value_ary_mt6895[0] &&
		cmdbt_w128 == g_cmdbt_dwn_value_ary_mt6895[128] &&
		cmdbt_w508 == g_cmdbt_dwn_value_ary_mt6895[508]) ? "VERIFY OK" : "MISMATCH");

	return ((mcu_remap & 0x000FFFFFu) == emi_remap_val) ? 0 : -1;
}

/*
 * Step 2.3A-2 preflight: read-only MT6363 / MT6368 HWCID & wireless LDO
 * telemetry through the shared PMIF SWINF_2 driver (zero PMIC writes).
 */
static void
connsys_pmic_dump_status(void)
{
	pmif_status_t st;
	uint8_t id63_lo = 0, id63_hi = 0, id68_lo = 0, id68_hi = 0;
	uint8_t vrf18_con0 = 0, vrf18_mon = 0;
	uint8_t vcn13_con0 = 0, vcn13_mon = 0;
	uint8_t vcn33_1_mon = 0, vcn33_2_mon = 0, vant18_mon = 0;
	pmif_status_t r1, r2, r3, r4;
	pmif_status_t s1, s2, s3, s4, s5, s6, s7;

	st = pmif_init();
	if (st != PMIF_OK) {
		printf("connsys: PMIC preflight skipped (pmif_init=%d)\n", (int)st);
		return;
	}

	r1 = pmif_read8(PMIF_SID_MT6363, PMIC_HWCID_LO, &id63_lo);
	r2 = pmif_read8(PMIF_SID_MT6363, PMIC_HWCID_HI, &id63_hi);
	r3 = pmif_read8(PMIF_SID_MT6368, PMIC_HWCID_LO, &id68_lo);
	r4 = pmif_read8(PMIF_SID_MT6368, PMIC_HWCID_HI, &id68_hi);

	s1 = pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VRFIO18_CON0, &vrf18_con0);
	s2 = pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VRFIO18_MON,  &vrf18_mon);
	s3 = pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VCN13_CON0,   &vcn13_con0);
	s4 = pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VCN13_MON,    &vcn13_mon);
	s5 = pmif_read8(PMIF_SID_MT6368, MT6368_LDO_VCN33_1_MON,  &vcn33_1_mon);
	s6 = pmif_read8(PMIF_SID_MT6368, MT6368_LDO_VCN33_2_MON,  &vcn33_2_mon);
	s7 = pmif_read8(PMIF_SID_MT6368, MT6368_LDO_VANT18_MON,   &vant18_mon);

	printf("connsys: PMIC[RO] HWCID MT6363(SID4 0x08/09)=0x%02x/0x%02x(rc=%d/%d) "
	       "MT6368(SID5 0x08/09)=0x%02x/0x%02x(rc=%d/%d)\n",
	       id63_lo, id63_hi, (int)r1, (int)r2,
	       id68_lo, id68_hi, (int)r3, (int)r4);
	printf("connsys: PMIC[RO] MT6363 VRFIO18 con0(0x1bcd)=0x%02x(rc=%d) mon(0x1bd1)=0x%02x(rc=%d) "
	       "VCN13 con0(0x1d07)=0x%02x(rc=%d) mon(0x1d0b)=0x%02x(rc=%d)\n",
	       vrf18_con0, (int)s1, vrf18_mon, (int)s2,
	       vcn13_con0, (int)s3, vcn13_mon, (int)s4);
	printf("connsys: PMIC[RO] MT6368 VCN33_1_MON(0x1cc4)=0x%02x(rc=%d) "
	       "VCN33_2_MON(0x1cd2)=0x%02x(rc=%d) VANT18_MON(0x1c27)=0x%02x(rc=%d)\n",
	       vcn33_1_mon, (int)s5, vcn33_2_mon, (int)s6, vant18_mon, (int)s7);
}

/*
 * Safe bounded PMIC read-modify-write with pre-snapshot & post-readback verify.
 * Fails closed on any SPMI error or readback mismatch.
 */
static bool
connsys_pmic_rmw8(uint32_t sid, uint32_t addr, uint8_t mask, uint8_t val,
                  const char *name)
{
	uint8_t before = 0, after = 0, target;
	pmif_status_t rc;

	rc = pmif_read8(sid, addr, &before);
	if (rc != PMIF_OK) {
		printf("connsys: adie[PMIC] %s(SID%u 0x%04x) pre-read FAILED rc=%d\n",
		       name, sid, addr, (int)rc);
		return false;
	}
	target = (uint8_t)((before & (uint8_t)(~mask)) | (val & mask));
	rc = pmif_write8(sid, addr, (uint32_t)target);
	if (rc != PMIF_OK) {
		printf("connsys: adie[PMIC] %s(SID%u 0x%04x) write 0x%02x->0x%02x FAILED rc=%d\n",
		       name, sid, addr, before, target, (int)rc);
		return false;
	}
	rc = pmif_read8(sid, addr, &after);
	if (rc != PMIF_OK || (after & mask) != (val & mask)) {
		printf("connsys: adie[PMIC] %s(SID%u 0x%04x) verify FAILED "
		       "(0x%02x->0x%02x rb=0x%02x mask=0x%02x rc=%d)\n",
		       name, sid, addr, before, target, after, mask, (int)rc);
		return false;
	}
	printf("connsys: adie[PMIC] %s(SID%u 0x%04x): 0x%02x -> 0x%02x (rb=0x%02x OK)\n",
	       name, sid, addr, before, target, after);
	return true;
}

/*
 * Step 2.3A-2: Enable MT6363 VRFIO18 (1.70V) + VCN13 (1.35V), configure
 * D-die <-> A-die Aux1 pinmux + pads, enable A-die dig_top_ck, deassert A-die
 * reset, acquire RFSPI semaphore 12, and read MT6637 CHIP_ID (0x66378A00/01).
 */
static int
connsys_adie_probe_mt6637(struct hakios_connsys_state *st)
{
	pmif_status_t pst;
	uint8_t vrf18_vsel = 0, vrf18_vcal = 0, vrf18_con0 = 0, vrf18_mon = 0;
	uint8_t vcn13_vsel = 0, vcn13_vcal = 0, vcn13_con0 = 0, vcn13_mon = 0;
	uint8_t dcxo_elr = 0;
	pmif_status_t elr_rc;
	bool is_52m = false;
	vm_offset_t gpio, iocfg_rt, iocfg_rtt, iocfg_tr;
	vm_offset_t cfg_on, wt_slp, rfspi, sema, sysram;
	uint32_t m20_before, m21_before, m20_after, m21_after;
	uint32_t ck_sta = 0, adie_ctl = 0, spi_sta = 0, adie_id = 0;
	bool ck_ok, sema_ok, spi1_ok = false, spi2_ok = false;

	if (!st->conn_powered) {
		printf("connsys: adie skipped (CONN domain not powered)\n");
		return -1;
	}

	pst = pmif_init();
	if (pst != PMIF_OK) {
		printf("connsys: adie aborted (pmif_init=%d)\n", (int)pst);
		return -1;
	}

	/* 1. Read-only PMIC pre-snapshot (VRFIO18, VCN13, MT6685 DCXO_DIGCLK_ELR) */
	(void)pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VRFIO18_VOSEL, &vrf18_vsel);
	(void)pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VRFIO18_VOCAL, &vrf18_vcal);
	(void)pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VRFIO18_CON0,  &vrf18_con0);
	(void)pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VRFIO18_MON,   &vrf18_mon);
	(void)pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VCN13_VOSEL,   &vcn13_vsel);
	(void)pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VCN13_VOCAL,   &vcn13_vcal);
	(void)pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VCN13_CON0,    &vcn13_con0);
	(void)pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VCN13_MON,     &vcn13_mon);
	elr_rc = pmif_read8(PMIF_SID_MT6685, MT6685_DCXO_DIGCLK_ELR, &dcxo_elr);
	if (elr_rc == PMIF_OK && (dcxo_elr & 0x01u) != 0u) {
		is_52m = true;
	}

	printf("connsys: adie[1] PMIC before: VRFIO18 vsel=0x%02x vcal=0x%02x con0=0x%02x mon=0x%02x | "
	       "VCN13 vsel=0x%02x vcal=0x%02x con0=0x%02x mon=0x%02x | "
	       "MT6685 ELR(0x7f4)=0x%02x(rc=%d -> %s)\n",
	       vrf18_vsel, vrf18_vcal, vrf18_con0, vrf18_mon,
	       vcn13_vsel, vcn13_vcal, vcn13_con0, vcn13_mon,
	       dcxo_elr, (int)elr_rc, is_52m ? "52M" : "26M");

	/* Enable VRFIO18: set voltage 1.70V (index 0x0B) first, then SW_EN=1 SW_LP=0 */
	if (!connsys_pmic_rmw8(PMIF_SID_MT6363, MT6363_LDO_VRFIO18_VOSEL,
			       0x0Fu, 0x0Bu, "VRFIO18_VOSEL(1.70V)") ||
	    !connsys_pmic_rmw8(PMIF_SID_MT6363, MT6363_LDO_VRFIO18_CON0,
			       0x03u, 0x01u, "VRFIO18_CON0(EN=1,LP=0)")) {
		printf("connsys: adie aborted on VRFIO18 write failure\n");
		return -1;
	}
	delay(500);
	pst = pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VRFIO18_MON, &vrf18_mon);
	printf("connsys: adie[1] VRFIO18_MON(0x1bd1)=0x%02x (rc=%d -> %s)\n",
	       vrf18_mon, (int)pst, (pst == PMIF_OK && vrf18_mon != 0u) ? "UP" : "STILL OFF");
	if (pst != PMIF_OK || vrf18_mon == 0u) {
		printf("connsys: adie STOPPED: VRFIO18_MON did not assert\n");
		return -1;
	}

	/* Enable VCN13: set voltage 1.30V (0x04) + 50mV trim (0x05) = 1.35V, then SW_EN=1 SW_LP=0 */
	if (!connsys_pmic_rmw8(PMIF_SID_MT6363, MT6363_LDO_VCN13_VOSEL,
			       0x0Fu, 0x04u, "VCN13_VOSEL(1.30V)") ||
	    !connsys_pmic_rmw8(PMIF_SID_MT6363, MT6363_LDO_VCN13_VOCAL,
			       0x0Fu, 0x05u, "VCN13_VOCAL(+50mV)") ||
	    !connsys_pmic_rmw8(PMIF_SID_MT6363, MT6363_LDO_VCN13_CON0,
			       0x03u, 0x01u, "VCN13_CON0(EN=1,LP=0)")) {
		printf("connsys: adie aborted on VCN13 write failure\n");
		return -1;
	}
	delay(500);
	pst = pmif_read8(PMIF_SID_MT6363, MT6363_LDO_VCN13_MON, &vcn13_mon);
	printf("connsys: adie[1] VCN13_MON(0x1d0b)=0x%02x (rc=%d -> %s)\n",
	       vcn13_mon, (int)pst, (pst == PMIF_OK && vcn13_mon != 0u) ? "UP" : "STILL OFF");
	if (pst != PMIF_OK || vcn13_mon == 0u) {
		printf("connsys: adie STOPPED: VCN13_MON did not assert\n");
		return -1;
	}
	delay(1000);

	/* 2. Map D-die <-> A-die pad/control/SPI/semaphore windows */
	gpio      = ml_io_map((vm_offset_t)GPIO_DIAG_PHYS,        (vm_size_t)GPIO_DIAG_SIZE);
	iocfg_rt  = ml_io_map((vm_offset_t)IOCFG_RT_PHYS,         (vm_size_t)IOCFG_RT_SIZE);
	iocfg_rtt = ml_io_map((vm_offset_t)IOCFG_RTT_PHYS,        (vm_size_t)IOCFG_RTT_SIZE);
	iocfg_tr  = ml_io_map((vm_offset_t)IOCFG_TR_PHYS,         (vm_size_t)IOCFG_TR_SIZE);
	cfg_on    = ml_io_map((vm_offset_t)CONN_CFG_ON_PHYS,      (vm_size_t)CONN_CFG_ON_SIZE);
	wt_slp    = ml_io_map((vm_offset_t)CONN_WT_SLP_CTL_PHYS,  (vm_size_t)CONN_WT_SLP_CTL_SIZE);
	rfspi     = ml_io_map((vm_offset_t)CONN_RF_SPI_MST_PHYS,  (vm_size_t)CONN_RF_SPI_MST_SIZE);
	sema      = ml_io_map((vm_offset_t)CONN_SEMAPHORE_PHYS,   (vm_size_t)CONN_SEMAPHORE_SIZE);
	sysram    = ml_io_map((vm_offset_t)CONN_INFRA_SYSRAM_PHYS,(vm_size_t)CONN_INFRA_SYSRAM_SIZE);

	if (!gpio || !iocfg_rt || !iocfg_rtt || !iocfg_tr ||
	    !cfg_on || !wt_slp || !rfspi || !sema || !sysram) {
		printf("connsys: adie map failed (gpio=%d rt=%d rtt=%d tr=%d "
		       "cfgon=%d wt=%d spi=%d sema=%d sram=%d)\n",
		       gpio != 0, iocfg_rt != 0, iocfg_rtt != 0, iocfg_tr != 0,
		       cfg_on != 0, wt_slp != 0, rfspi != 0, sema != 0, sysram != 0);
		return -1;
	}

	/* Pinmux Aux1 + driving + PUPD (mt6895_pos_gen.c:158-185) */
	m20_before = connsys_rd32(gpio, GPIO_MODE20);
	m21_before = connsys_rd32(gpio, GPIO_MODE21);
	connsys_rmw32(gpio, GPIO_MODE20, 0x77700000u, 0x11100000u);
	connsys_rmw32(gpio, GPIO_MODE21, 0x77777777u, 0x11111111u);
	connsys_rmw32(iocfg_rt,  0x020u, 0x00E00000u, 0x00200000u);
	connsys_rmw32(iocfg_rtt, 0x000u, 0x00000038u, 0x00000008u);
	connsys_rmw32(iocfg_tr,  0x010u, 0x00000E00u, 0x00000200u);
	connsys_rmw32(iocfg_tr,  0x000u, 0x0003FFC0u, 0x00009240u);
	connsys_set32(iocfg_rtt, 0x050u, (1u << 2));
	connsys_set32(iocfg_rt,  0x0A0u, (1u << 16));
	m20_after = connsys_rd32(gpio, GPIO_MODE20);
	m21_after = connsys_rd32(gpio, GPIO_MODE21);
	printf("connsys: adie[2] pinmux MODE20 0x%08x->0x%08x MODE21 0x%08x->0x%08x\n",
	       m20_before, m20_after, m21_before, m21_after);

	/* 3. CONN_WF_CTRL2 clock-strap GPIO + A-die top_ck_en + deassert reset
	 *    (mt6895_pos.c:274-276, mt6895_pos_gen.c:710-820) */
	connsys_rmw32(gpio, GPIO_MODE21, 0x00700000u, 0x00000000u);
	if (is_52m) {
		connsys_set32(gpio, GPIO_G5_DOUT, (1u << 13));
	} else {
		connsys_clr32(gpio, GPIO_G5_DOUT, (1u << 13));
	}
	connsys_set32(gpio, GPIO_G5_DIR, (1u << 13));

	/* Enable A-die dig_top_ck (WB_SLP_TOP_CK_0 bit0=1, poll bit1==0) */
	connsys_set32(wt_slp, 0x120u, (1u << 0));
	ck_ok = connsys_poll_bit(wt_slp, 0x120u, (1u << 1), 0u, 100, 5, &ck_sta);

	/* Deassert A-die reset (CONN_CFG_ON + 0x010 bit0=1) */
	connsys_set32(cfg_on, 0x010u, (1u << 0));
	delay(1000);
	adie_ctl = connsys_rd32(cfg_on, 0x010u);
	printf("connsys: adie[3] strap=%s WB_SLP_TOP_CK_0=0x%08x(ok=%d) ADIE_CTL=0x%08x\n",
	       is_52m ? "52M(H)" : "26M(L)", ck_sta, ck_ok ? 1 : 0, adie_ctl);

	/* 4. Acquire RFSPI semaphore 12 + SYS_SPI_TOP read 0x02C (MT6637 CHIP_ID) */
	sema_ok = connsys_poll_bit(sema, CONN_SEMA_M2_OWN_STA(CONN_SEMA_RFSPI_INDEX),
				   1u, 1u, 1000, 10, NULL);
	if (sema_ok) {
		spi1_ok = connsys_poll_bit(rfspi, 0x000u, (1u << 5), 0u, 100, 50, &spi_sta);
		if (spi1_ok) {
			connsys_wr32(rfspi, 0x050u, 0x0000B000u | 0x02Cu);
			connsys_wr32(rfspi, 0x054u, 0u);
			delay(1);
			spi2_ok = connsys_poll_bit(rfspi, 0x000u, (1u << 5), 0u, 100, 50, &spi_sta);
			if (spi2_ok) {
				adie_id = connsys_rd32(rfspi, 0x058u);
			}
		}
		/* Always release RFSPI semaphore 12 */
		connsys_wr32(sema, CONN_SEMA_M2_OWN_REL(CONN_SEMA_RFSPI_INDEX), 1u);
		delay(2);
	}

	/* Always switch CONN_WF_CTRL2 back to CONN mode (mt6895_pos_gen.c:1345) */
	connsys_rmw32(gpio, GPIO_MODE21, 0x00700000u, 0x00100000u);

	if ((adie_id & 0xFFFF0000u) == 0x66370000u) {
		connsys_wr32(sysram, CONN_INFRA_SW_CR_ADIE_ID_OFS, adie_id);
		printf("connsys: adie[4] MT6637 CHIP_ID=0x%08x (%s) -> MATCH! "
		       "(sema=%d spi1=%d spi2=%d)\n",
		       adie_id,
		       (adie_id == 0x66378A00u) ? "MT6637E1" :
		       (adie_id == 0x66378A01u) ? "MT6637E2" : "MT6637",
		       sema_ok ? 1 : 0, spi1_ok ? 1 : 0, spi2_ok ? 1 : 0);
		return 0;
	}

	printf("connsys: adie[4] MT6637 CHIP_ID READ FAILED: id=0x%08x "
	       "(sema=%d spi1=%d spi2=%d spi_sta=0x%08x ck_sta=0x%08x adie_ctl=0x%08x)\n",
	       adie_id, sema_ok ? 1 : 0, spi1_ok ? 1 : 0, spi2_ok ? 1 : 0,
	       spi_sta, ck_sta, adie_ctl);
	return -1;
}

static uint32_t gDeferredPowerLevel;
static uint32_t gDeferredReadId;
static uint32_t gDeferredDelayS;
static uint32_t gDeferredPos3;
static uint32_t gDeferredAdie;

static int
connsys_run_power_and_probe(struct hakios_connsys_state *st,
                            uint32_t power_level, uint32_t readid,
                            uint32_t pos3_level, uint32_t adie_level)
{
	struct mtk_connsys_probe_ops ops;
	int rc = 0;

	/*
	 * Power before mapping: the 0x184x windows are inside the conn domain, and
	 * a read of a gated window aborts rather than returning zero.
	 */
	{
		struct hakios_connsys_power pw;

		memset(&pw, 0, sizeof(pw));
		(void)connsys_power_check(&pw, power_level, readid); /* reported, not fatal */
		st->conn_powered = pw.scpsys_on && pw.bus_prot_clear;
		st->conn_cfg = pw.conn_cfg;
	}

	if (pos3_level >= 1u) {
		if (st->conn_powered) {
			(void)connsys_pos3_d_die_init(st, pos3_level);
		} else {
			printf("connsys: pos3 skipped (CONN domain not powered)\n");
		}
		connsys_pmic_dump_status();
	}

	if (adie_level >= 1u) {
		if (st->conn_powered) {
			(void)connsys_adie_probe_mt6637(st);
		} else {
			printf("connsys: adie skipped (CONN domain not powered)\n");
		}
	}

	if (readid == 0u) {
		printf("connsys: 39-window MMIO probe skipped (connsys_readid=0, safe power-on mode)\n");
		st->mapped = true;
		return 0;
	}

	/*
	 * The four Wi-Fi windows, by name. Note that when only CONNINFRA (0x180x)
	 * is up and WFSYS (0x184x) is still powered down, the MT6895 bus bridge
	 * returns 0xdead0001 (host bus timeout sentinel, see kal_pdma.c:153).
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
		/*
		 * The walker prints each window's read-back only when a logger is set -- with none it
		 * counts and says nothing -- so leaving this NULL was why no line of the four 0x184x
		 * values ever reached the log. It is the whole point of the read: what each window
		 * answers is how the base, the power domain and the EMI guard are judged.
		 */
		ops.log = connsys_probe_log;

		rc = mtk_connsys_probe_windows(&ops, wins, nwins);
		/*
		 * The walker answers how many windows read back something (0 to 4), not whether it
		 * succeeded: a zero there means the base is mapped and answering nothing, which is
		 * what a powered-down conn domain looks like. Only a negative value is a failure.
		 * Returning the count instead made this function answer 1..4 on a healthy probe, and
		 * the caller takes any non-zero as "stop the chain" -- so BTIF and en1 were never
		 * started at all (boot-1020).
		 */
		printf("connsys: probed %zu windows, %d of them answering\n", nwins, rc);
	}

	st->mapped = true;
	return rc < 0 ? rc : 0;
}

static void
connsys_deferred_thread(__unused void *parameter, __unused wait_result_t result)
{
	int rc;

	for (uint32_t s = 0; s < gDeferredDelayS; s++) {
		delay_for_interval(1000, NSEC_PER_MSEC);
	}

	printf("connsys: deferred bringup firing after %us "
	       "(connsys_power=%u connsys_readid=%u connsys_pos3=%u connsys_adie=%u)\n",
	       gDeferredDelayS, gDeferredPowerLevel, gDeferredReadId,
	       gDeferredPos3, gDeferredAdie);
	rc = connsys_run_power_and_probe(&gConnsys, gDeferredPowerLevel,
					 gDeferredReadId, gDeferredPos3, gDeferredAdie);
	printf("connsys: deferred bringup complete (rc=%d conn_powered=%d)\n",
	       rc, gConnsys.conn_powered ? 1 : 0);
	thread_terminate(current_thread());
}

int
hakios_mt6895_connsys_start(void)
{
	struct hakios_connsys_state *st = &gConnsys;
	uint32_t which = 0;
	uint32_t power_level = 0;
	uint32_t readid = 0;
	uint32_t delay_s = 0;
	uint32_t pos3_level = 0;
	uint32_t adie_level = 0;
	int rc;

	if (!PE_parse_boot_argn(WIFI_ARG, &which, sizeof(which)) || which != 2) {
		return 0;   /* not asked for */
	}

	memset(st, 0, sizeof(*st));

	rc = connsys_lock_emi();
	if (rc != 0) {
		return rc;
	}

	(void)PE_parse_boot_argn(POWER_ARG, &power_level, sizeof(power_level));
	(void)PE_parse_boot_argn(READID_ARG, &readid, sizeof(readid));
	(void)PE_parse_boot_argn(DELAY_S_ARG, &delay_s, sizeof(delay_s));
	(void)PE_parse_boot_argn(POS3_ARG, &pos3_level, sizeof(pos3_level));
	(void)PE_parse_boot_argn(ADIE_ARG, &adie_level, sizeof(adie_level));

	printf("connsys: config power=%u readid=%u delay_s=%u pos3=%u adie=%u\n",
	       power_level, readid, delay_s, pos3_level, adie_level);

	if (delay_s > 0u) {
		thread_t thread;

		gDeferredPowerLevel = power_level;
		gDeferredReadId     = readid;
		gDeferredDelayS     = delay_s;
		gDeferredPos3       = pos3_level;
		gDeferredAdie       = adie_level;
		if (kernel_thread_start(connsys_deferred_thread, NULL, &thread) == KERN_SUCCESS) {
			thread_deallocate(thread);
			printf("connsys: deferring CONN power/pos3/adie/probe by %us on kernel thread\n",
			       delay_s);
			return 0;
		}
		printf("connsys: failed to start deferred thread; running synchronously\n");
	}

	rc = connsys_run_power_and_probe(st, power_level, readid, pos3_level, adie_level);
	printf("connsys: base up. The radio is NOT started: firmware, the conn "
	       "power domain and wlan_drv_gen4m are the next steps, in that order.\n");
	return rc < 0 ? rc : 0;
}
