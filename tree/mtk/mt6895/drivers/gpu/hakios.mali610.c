/*
 * The GPU of the Redmi Note 11T Pro (xaga), a Mali-G610 MC6: a Valhall part with a Command Stream
 * Front end, which means it runs firmware the host has to load before it will do anything at all.
 * Only with boot-arg mali=1. Two milestones so far:
 *
 *   mali=1  M0  power, clocks, and what the block says it is
 *   mali=2  M1  the CSF firmware in the MCU's address space, the MCU started, and the global
 *               interface handshake that says the firmware is running and talking
 *
 * Where every number comes from
 * -----------------------------
 * The register map of the GPU itself is Panthor's (drivers/gpu/drm/panthor, "GPL-2.0 or MIT"):
 * panthor_regs.h for the block, panthor_mmu.c for the address spaces, panthor_fw.c for the
 * firmware container and the interface blocks. Those registers are the architecture's, not a
 * board's, and this GPU answers at the same offsets.
 *
 * The power and clock side is this phone's, and was the hard part: MT6895 has no entry in mainline
 * (mtk-mfg-pmdomain.c knows only mt8196) and the vendor's kbase gets its power from the kernel's
 * clock and genpd frameworks, so there is no bare-metal sequence to copy. Xiaomi published the
 * board: MiCode/Xiaomi_Kernel_OpenSource, branch xaga-s-oss, and the three files that matter are
 *
 *   drivers/soc/mediatek/mtk-scpsys-mt6895.c    the twelve MFG domains and their offsets
 *   drivers/soc/mediatek/mtk-scpsys.c           what the bits in a domain's word mean
 *   drivers/gpu/mediatek/gpufreq/v2/gpufreq_mt6895.c   PDC, the shader-core masks, the PLL
 *   drivers/clk/mediatek/clk-mt6895.c, clk-mt6895-mfgcfg.c   the two muxes and the gate
 *
 * The clock had a surprise worth writing down. With all twelve domains powered and the block
 * ungated, GPU_ID read 0 -- the register block was not answering at all. The reason was that
 * mfg_sel_0, the mux that picks the GPU's clock, was still pointing at mfg_ref_ck, whose own
 * parent select had never been set, so the GPU had no clock. Pointing mfg_ref_sel at the 26 MHz
 * crystal (which runs for the whole SoC and cannot fail) turned GPU_ID into 0xa8670004 at once,
 * and only then was it worth starting MFGPLL. That order is kept below: the crystal first, so the
 * block always has some clock, and the PLL brought up beside it before the mux is moved.
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <vm/vm_kern.h>

#include "hakios.mali610_fw.h"

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
extern uint64_t kvtophys(vm_offset_t va);
extern vm_offset_t phystokv(uint64_t pa);
/* osfmk/arm/machine_routines.h is not on this subtree's include path either, so the three-argument
 * form is restated the same way. Its flags argument is the WIMG word from
 * osfmk/arm/memory_types.h: VM_WIMG_WCOMB is 0x6 there (not cacheable, coherent) and it is the
 * one ml_io_map() uses for every device buffer on this platform. */
extern vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, uint32_t flags);
#define MALI_WIMG_WCOMB 0x6u
/* osfmk/arm/caches_internal.h is not on this subtree's include path (bsd/dev sees osfmk's public
 * headers only), so the one cache routine this file needs is restated here. Only the PoC flush
 * exists -- the header has no invalidate-by-range for the data cache -- and on a platform whose
 * caches are not snooped it is a clean and an invalidate in one, which is exactly the pair a
 * handover needs at both ends. */
extern void FlushPoC_DcacheRegion(vm_offset_t va, size_t length);

/* ------------------------------------------------------------------ registers we touch */

/* The SPM, this SoC's MTCMOS controller. Not the MT6768's 0x10006000. */
#define MALI_SPM_PHYS           0x1c001000ULL
#define MALI_SPM_SIZE           0x1000u
/* One word per MFG domain, in the order they have to come up (mtk-scpsys-mt6895.c). */
#define MALI_MFG1               0x0ebcu         /* MPU, the domain the vendor powers by hand */
#define MALI_MFG2               0x0ec0u         /* L2 cache */
#define MALI_MFG3               0x0ec4u         /* ST0 */
#define MALI_MFG4               0x0ec8u         /* ST1 */
#define MALI_MFG5               0x0eccu         /* ST4 */
#define MALI_MFG6               0x0ed0u         /* ST5 */
#define MALI_MFG7               0x0ed4u         /* ST0T0 */
#define MALI_MFG8               0x0ed8u         /* ST1T0 */
#define MALI_MFG9               0x0edcu         /* ST0T1 */
#define MALI_MFG10              0x0ee0u         /* ST4T0 */
#define MALI_MFG11              0x0ee4u         /* ST5T0 */
#define MALI_MFG12              0x0ee8u         /* ST4T1 */
/* PWR_STATUS, which is the register that says whether the rail is really live.
 *
 * There are two copies and they are not interchangeable. scpsys on this SoC reads 0xF34 and
 * 0xF38 (mtk-scpsys-mt6895.c:604-605); gpufreq reads 0xF3C and 0xF40, eight bytes further on.
 * Both are printed below so the next run says which one answers for these domains rather than
 * assuming. MFG_1_12_PWR_MASK is the vendor's own mask, bits 2..13, one per domain
 * (gpufreq_mt6895.h:72-76) -- the same register whose bad value it logs as "incorrect MFG1-12
 * power off status" (gpufreq_mt6895.c:3586). */
/*
 * The AOC handshake, and the SRAM isolation it releases.
 *
 * Both were done by the loader until now, which is the reason this driver has never run the
 * vendor's first step. Android does it here, from a block that has never been powered, and that
 * is the only path known to end with an MCU that executes. See mali_aoc_up() below.
 */
#define MALI_SPM_SEMA_M4        0x06acu         /* SPM_SEMA_M4, bit 0 is the hardware semaphore */
#define MALI_SPM_BUCK_ISO       0x0f30u         /* SOC_BUCK_ISO_CON */
#define MALI_AOC_VGPU_SRAM_ISO  (1u << 9)       /* AOC_VGPU_SRAM_ISO_DIN */
#define MALI_AOC_VGPU_LATCH     (1u << 10)      /* AOC_VGPU_SRAM_LATCH_ENB */
#define MALI_SPM_PWR_STATUS     0x0f34u
#define MALI_SPM_PWR_STATUS2    0x0f38u
#define MALI_SPM_PWR_STATUS_GF  0x0f3cu
#define MALI_MFG_PWR_MASK       0x3ffcu
/* The word's bits (mtk-scpsys.c, the only place they are written down). */
#define SPM_PWR_RST_B           (1u << 0)
#define SPM_PWR_ISO             (1u << 1)
#define SPM_PWR_ON              (1u << 2)
#define SPM_PWR_ON_2ND          (1u << 3)
#define SPM_PWR_CLK_DIS         (1u << 4)
#define SPM_SRAM_PDN            (1u << 8)
#define SPM_SRAM_ACK            (1u << 12)
/* The domains' own status is bits 31:30 of the control word, not the global status registers:
 * every MT6895 domain carries MTK_SCPD_IS_PWR_CON_ON, and that is the test scpsys uses. */
#define SPM_STA_MASK            0xc0000000u

/* topckgen: the two muxes in front of the GPU's clock, and the crystal. */
#define MALI_TOPCKGEN_PHYS      0x10000000ULL
#define MALI_TOPCKGEN_SIZE      0x1000u
#define MALI_CLK_CFG_4          0x0050u         /* mfg_ref_sel in bits 25:24 */
#define MALI_CLK_CFG_4_SET      0x0054u
#define MALI_CLK_CFG_4_CLR      0x0058u
#define MALI_CLK_CFG_5          0x0060u         /* mfg_sel_1_sel in bits 1:0 */
#define MALI_CLK_CFG_5_SET      0x0064u
#define MALI_CLK_CFG_5_CLR      0x0068u
#define MALI_MFG_SEL_1_MASK     0x00000003u     /* 2 = mainpll_d5_d2, what the phone uses */
#define MALI_CLK_CFG_30         0x01f0u         /* mfg_sel_0_sel in bit 16 */
#define MALI_CLK_CFG_30_SET     0x01f4u
#define MALI_CLK_CFG_30_CLR     0x01f8u
#define MALI_CLK_UPDATE         0x0004u         /* one bit per mux, latched by writing it */
#define MALI_MFG_REF_MASK       0x03000000u
#define MALI_MFG_REF_26M        (0u << 24)      /* tck_26m_mx9_ck: the always-on crystal */
#define MALI_MFG_REF_SHIFT      19              /* CLK_UPDATE bit for mfg_ref_sel */
#define MALI_MFGSC_REF_SHIFT    20              /* CLK_UPDATE bit for mfgsc_ref_sel */
#define MALI_MFG_SEL_0_BIT      (1u << 16)      /* 0 = mfg_ref_ck, 1 = mfgpll_ck */

/*
 * The AXI protections mfg1 owns.
 *
 * MT6895_POWER_DOMAIN_MFG1 is the only MFG domain in mtk-scpsys-mt6895.c with a bp_table, and
 * these are its four entries. Infracfg and mfgrpc are the two blocks the table names; the set
 * and clear registers sit next to each other and the clear one takes the mask directly, which is
 * what clear_bus_protection() in mtk-scpsys.c:402 does.
 *
 * Why it matters here: everything in this driver before the MCU start reads and writes the GPU's
 * config port, and that port answers whether or not the block's AXI master is allowed out. The
 * MCU fetches its instructions over AXI. With these still asserted the core cannot make its first
 * fetch, which is exactly what the witness stub showed -- our own thirty-two bytes in the code
 * section, a legal page table, and not one store reached memory. Four register write-and-forgets
 * are a cheap way to find out whether that is the wall.
 */
#define MALI_INFRACFG_PHYS      0x10001000ULL
#define MALI_INFRACFG_SIZE      0x1000u
/*
 * The mfg_rpc bus: two device tree nodes, one address.
 *
 * xaga_live.dts declares them separately. g3d_config@13f90000 (line 4905) is a hundred kilobytes
 * covering the whole subsystem; syscon@13f91000 (line 4910) is our own four kilobytes inside it,
 * named "mediatek,mt6895-gpu_eb_rpc_bus". The vendor's gpufreq driver takes the outer window and
 * addresses 0x13F91034 as 0x13f90000 + 0x1034; we take the inner one, so the same register is
 * 0x13f91000 + 0x34.
 *
 * Both spell the same address, and only one of them is mapped here. Widening this to the outer
 * window was tried and cost a boot loop: 0x13f90000-0x13f90fff is not in the phone's own iomem
 * at all -- nothing on Android ever maps it -- and on this part touching an undriven MFG window
 * does not fault, it hangs the bus until the watchdog takes the board.
 *
 * So the base stays, and the offset is what was wrong. It used to be 0x1034 here, copied from the
 * vendor's numbering without adjusting for the different base: 0x13f91000 + 0x1034 is 0x13f92034,
 * past the end of this window. That write never landed anywhere, which is why the interconnect
 * gates below always read back exactly what they already were.
 */
#define MALI_MFGRPC_PHYS        0x13f91000ULL
#define MALI_MFGRPC_SIZE        0x1000u

/* g3d_mpu: the GPU's own protection unit, and g3d_brisket next to it. No driver claims either. */
#define MALI_G3D_MPU_PHYS       0x13bff000ULL
#define MALI_G3D_MPU_SIZE       0x1000u
#define MALI_G3D_BRISKET_PHYS   0x13bfee00ULL
#define MALI_G3D_BRISKET_SIZE   0x200u

/* mfg_cpe_control: the second GPU coprocessor block, at the vendor's 0x13FB9C00. */
#define MALI_MFG_CPE_PHYS       0x13fb9c00ULL
#define MALI_MFG_CPE_SIZE       0x100u

#define MALI_BP_MD0_MFG1        0x10001ca8u        /* clr_ofs 0x0ca8 in infracfg */
#define MALI_BP_MD0_MFG1_MASK   0x0000000fu
#define MALI_BP_MFGRPC_MFG1     0x13f91044u        /* clr_ofs 0x0044 in mfgrpc */
#define MALI_BP_MFGRPC_MFG1_MASK 0x000f0000u
#define MALI_BP_EMISYS0_MFG1    0x10001c68u        /* clr_ofs 0x0c68 in infracfg */
#define MALI_BP_EMISYS0_MFG1_MASK 0x000c0000u
#define MALI_BP_EMISYS1_MFG1    0x10001c78u        /* clr_ofs 0x0c78 in infracfg */
#define MALI_BP_EMISYS1_MFG1_MASK 0x000c0000u

/* mtk-scpsys.c:8-26 _BUS_PROT(type, set_ofs, clr_ofs, en_ofs, sta_ofs, mask, ignore_clr_ack) --
 * every entry is a five-tuple, and the four CLR words the driver already writes are only one
 * quarter of it. mtk-scpsys-mt6983.c:438 gives the mfgrpc tuple as (0x1040, 0x1044, 0x103C,
 * 0x1048); the infracfg tuples follow the same shape at the offsets scpsys.h:364-369 uses.
 * BUS_PROT_IGN sets ignore_clr_ack, so clear_bus_protection() returns before it ever polls the
 * status word -- which is why the question "did the protection really drop" has never been
 * answered on this board. Read the status word; it is the only honest answer. */
#define MALI_BP_MD0_MFG1_EN_OFF     0x0ca0u         /* en_ofs  in infracfg_ao */
#define MALI_BP_MD0_MFG1_STA_OFF    0x0cacu         /* sta_ofs in infracfg_ao */
#define MALI_BP_EMISYS0_EN_OFF      0x0c60u
#define MALI_BP_EMISYS0_STA_OFF     0x0c6cu
#define MALI_BP_EMISYS1_EN_OFF      0x0c70u
#define MALI_BP_EMISYS1_STA_OFF     0x0c7cu
#define MALI_BP_MFGRPC_EN_OFF       0x003cu         /* relative to 0x13F91000 */
#define MALI_BP_MFGRPC_STA_OFF      0x0048u

/* mfgcfg: the block's own gate. mfgcfg_bg3d, the only gate in it (clk-mt6895-mfgcfg.c). */
#define MALI_MFGCFG_PHYS        0x13fbf000ULL
#define MALI_MFGCFG_SIZE        0x1000u
#define MALI_MFGCFG_STA         0x0000u
#define MALI_MFGCFG_CLR         0x0008u
#define MALI_MFGCFG_BG3D        (1u << 0)
/* The block's own bus-idle readback, and its timestamp counter. */
#define MALI_MFG_QCHANNEL_CON   0x00b4u
#define MALI_MFG_ACTIVE_SEL     (1u << 0)
#define MALI_MFG_TIMESTAMP      0x0130u
#define MALI_MFG_TIMESTAMP_ON   0x00000003u
#define MALI_MFG_DEBUG_SEL      0x0170u
#define MALI_MFG_DEBUG_TOP_SEL  0x00000003u
#define MALI_MFG_DEBUG_TOP      0x0178u
#define MALI_MFG_DEBUG_TOP_BUSY (1u << 0)
#define MALI_MFG_DEBUG_TOP_IDLE (1u << 2)       /* v1 drivers poll this: 1 = idle, 0 = non-idle */
/* MFG_AXCOHERENCE: bit 3 is M3_coherence_enable, the port the M7 fetches through. */
#define MALI_MFG_AXCOHERENCE    0x0168u

/*
 * MFG_I2M_PROTECTOR: the initiator-to-memory protector, which sits between every master in the
 * MFG block and DRAM. This is step nine of the vendor's power-on and it is not optional: the
 * vendor sets g_gpm_enable = true before it ever reaches power control (gpufreq_mt6895.c:5196,
 * "default enable GPM 1.0, before power control") and only then guards the call with that flag
 * (gpufreq_mt6895.c:814). The block is named for di/dt shaping, which is what the vendor's
 * comment calls it, but a protector that is never programmed is a gate with an undefined answer,
 * and the one thing every failed run here agrees on is that the GPU's own reads never complete.
 *
 * The four values and the ordering are copied from __gpufreq_gpm_control()
 * (gpufreq_mt6895.c:3347-3367) verbatim: three configuration words, the fourth register, a one
 * microsecond wait, and then the first word again with bit 0 set, which is what arms it. Reading
 * them back afterwards matters because nothing in this block has ever been read here.
 */
#define MALI_MFG_I2M_CFG_00     0x0f60u         /* 0x13FBFF60 */
#define MALI_MFG_I2M_CFG_01     0x0f64u         /* 0x13FBFF64 */
#define MALI_MFG_I2M_CFG_02     0x0f68u         /* 0x13FBFF68 */
#define MALI_MFG_I2M_CFG_03     0x0fa8u         /* 0x13FBFFA8 */
/* Unnamed in every source read so far; sits in the same page and is only ever read here. */
#define MALI_MFG_I2M_OSTD       0x0f40u         /* 0x13FBFF40 */

/*
 * GPUEB: a separate Cortex-M4-ish coprocessor at 0x13C40000 that the vendor firmware leaves in
 * charge of the GPU's own power and clock sequencing on this SoC (the DTS says
 * `gpueb-support = <1>`, and gpufreq_mt6895.c:5166 jumps over its entire power_control path
 * because of that flag).
 *
 * Its watchdog kick register is the cheapest liveness probe there is: the firmware rewrites it
 * continuously, so two reads a millisecond apart that differ mean the coprocessor is still
 * running -- in Android or in here. What that implies for a core that will not fetch is the
 * question this whole probe exists to answer: if GPUEB is alive while XNU owns the GPU, then it
 * may be the one holding the MFG bus, and no amount of registering from the AP side would show
 * up as anything but "busy".
 */
#define MALI_GPUEB_PHYS         0x13c40000ULL
#define MALI_GPUEB_SIZE         0x24000u
#define MALI_GPUEB_WDT_KICK     0x2061cu
#define MALI_GPUEB_WDT_CON      0x20618u
#define MALI_GPUEB_IRQ_RAW      0x2033cu
#define MALI_GPUEB_MBOX_RECV    0x22078u
#define MALI_GPUEB_GPR          0x1fd1cu        /* gpr_offset from the allocator's node */
/*
 * The MFG interconnect's own clock gates.
 *
 * MFG_ASYNC_CON has four of them and the vendor driver for this part only turns on the two slave
 * ones. The older driver in the same tree (gpufreq/v1/mt6873/mtk_gpufreq_plat.c:829-877,
 * mt_gpufreq_external_cg_control) turns on all four and two more besides, and its comments say
 * what the missing pair is for: bit 22 MEM0_MST_CG_ENABLE and bit 24 MEM1_MST_CG_ENABLE are the
 * *master* side, while the pair mt6895's driver writes (bits 23 and 25) is the slave side.
 *
 * That matters here because the MCU is a master: it fetches its instructions out over this
 * interconnect, and a master-side clock gate left shut is a fetch that never returns. The
 * signature matches what this board shows better than anything else has -- the channel reports
 * non-idle the instant the control word is written, stays that way with the core disabled, and
 * the core never reaches its first instruction and never faults, because a request stuck on a
 * gated clock has nothing to fault on.
 *
 * The rest of the same function is here too: QCHANNEL_ENABLE in MFG_QCHANNEL_CON bit 4, and
 * FAXI_CK_SOC_IN_EN in MFG_ASYNC_CON_1 bit 0, which is the SOC-side input of the same bridge.
 */
#define MALI_MFG_ASYNC_CON      0x0020u
#define MALI_MFG_MEM0_MST_CG    (1u << 22)
#define MALI_MFG_MEM0_SLV_CG    (1u << 23)
#define MALI_MFG_MEM1_MST_CG    (1u << 24)
#define MALI_MFG_MEM1_SLV_CG    (1u << 25)
#define MALI_MFG_ASYNC_CON_1    0x0024u
#define MALI_MFG_FAXI_SOC_IN_EN (1u << 0)
#define MALI_MFG_QCHANNEL_EN    (1u << 4)
#define MALI_MFG_DCM_CON_0      0x0010u
#define MALI_MFG_GLOBAL_CON     0x00b0u
#define MALI_MFG_RPC_AO_CLK_CFG 0x0034u     /* 0x13F91034 from this base, not the vendor's 0x13f90000 */

/*
 * PDC: the shader cores' own power controller, in the same mfgcfg block.
 *
 * The vendor turns this on as step seven of its nine-step power-on (gpufreq_mt6895.c:1488,
 * __gpufreq_pdc_control) and nothing else in the driver does it. On this SoC the twelve MFG
 * MTCMOS domains are not meant to be raised one at a time by software at all: every mfg1..mfg12
 * descriptor carries MTK_SCPD_BYPASS_INIT_ON (mtk-scpsys-mt6895.c:449-556), which tells scpsys
 * to leave them alone at init, and the vendor's own __gpufreq_mtcmos_control() only ever does
 * pm_runtime_get_sync(mfg1) when GPUFREQ_PDCv2_ENABLE is set -- PDC raises mfg2..mfg12 when the
 * GPU itself asks for them.
 *
 * That is the shape of the failure this fixes. Raising the domains by hand gets mfg1..mfg4 and
 * then stops: mfg5 through mfg12 each answer "request taken, power pending" (0x111e) forever,
 * because the thing they are waiting on is not software at all. And the sign of it is SHADER_READY
 * reading zero -- Android has 0x00130013 there, which is the whole shader-present mask, so its
 * cores are up before it ever starts the MCU. Ours were not, and an MCU whose shader cores never
 * came out of retention has nothing to run on.
 *
 * The bit is the same in all twenty-six registers: bit 0 of MFG_ACTIVE_POWER_CON_nn is
 * rg_*_active_pwrctl_en. The ten 0x400-step registers are one per shader-core cluster
 * (rg_sc0..rg_sc9), and the 0xC0/0x98/0x100/0x118/0x120/0x140/0x1C0 set is the core groups
 * (cg, st0, st1, st2, st4, st5, st6). Each even register has a twin four bytes up carrying
 * rg_*_active_pwrctl_rsv in bit 31, which the vendor also sets; this driver sets both, because
 * the vendor does and the cost of being wrong is a core that stays off.
 */
#define MALI_PDC_CORES          10u    /* 0x400 + n*0x18: rg_sc0..rg_sc9, and no further */
#define MALI_PDC_SC_STEP        0x18u
#define MALI_PDC_SC_BASE        0x400u
#define MALI_PDC_SC_RSV         0x04u
#define MALI_PDC_SC_EN          (1u << 0)
#define MALI_PDC_SC_RSV_BIT     (1u << 31)
static const uint16_t kPdcGroups[] = { 0xc0, 0x98, 0x100, 0x118, 0x120, 0x140, 0x1c0 };
#define MALI_PDC_GROUP_EN       (1u << 0)

/* MFGPLL. CON0 holds PLL_EN, CON1 the divider and the feedback word, CON3 power and isolation.
 * 26 MHz * 0x1713b1 / 2^14 / 8 = 300.1 MHz, so VCO 2400 MHz, inside the PLL's 1.5..3.8 GHz. */
#define MALI_MFGPLL_PHYS        0x13fa0000ULL
#define MALI_MFGPLL_SIZE        0x1000u
#define MALI_MFGPLL_CON0        0x0008u
#define MALI_MFGPLL_CON1        0x000cu
#define MALI_MFGPLL_CON3        0x0014u
#define MALI_PLL_PWR_ON         (1u << 0)
#define MALI_PLL_ISO_EN         (1u << 1)
#define MALI_PLL_EN             (1u << 0)
#define MALI_PLL_PCW_CHG        (1u << 31)
#define MALI_PLL_POSDIV_8       (3u << 24)
#define MALI_PLL_PCW_300MHZ     0x1713b1u

/* The GPU block. Panthor's register map; the names are its names. */
#define MALI_GPU_PHYS           0x13000000ULL
#define MALI_GPU_SIZE           0x480000u
/* What GPU_ID answers once the block is powered and clocked: this board is a G610, and the
 * Android side reports the same 0xA867. A read of zero or of all ones means nothing is answering,
 * which is the only state in which the power and clock blocks are worth touching at all. */
#define MALI_GPU_ID             0xa8670004u
enum : uint32_t {
	kGpuId = 0x000, kL2Features = 0x004, kCoreFeatures = 0x008, kTilerFeatures = 0x00c,
	kMemFeatures = 0x010, kMmuFeatures = 0x014, kAsPresent = 0x018, kCsfId = 0x01c,
	kIntRaw = 0x020, kIntClear = 0x024, kIntMask = 0x028, kIntStatus = 0x02c,
	kJobIntRaw = 0x1000, kJobIntClear = 0x1004, kJobIntMask = 0x1008, kJobIntStatus = 0x100c,
	kMmuIntRaw = 0x2000, kMmuIntClear = 0x2004, kMmuIntMask = 0x2008,
	kThreadMax = 0x0a0, kThreadFeatures = 0x0ac,
	kShaderPresent = 0x100, kTilerPresent = 0x110, kL2Present = 0x120,
	kShaderReady = 0x140, kTilerReady = 0x150, kL2Ready = 0x160,
	kL2PwrOn = 0x1a0, kL2PwrOff = 0x1e0, kL2PwrTrans = 0x220, kL2PwrActive = 0x260,
	/* The core power requests kbase derives by subtracting the presence register:
	 * SHADER_PWRON 0x180 / SHADER_PWROFF 0x1c0 against SHADER_PRESENT 0x100, and the tiler
	 * pair 0x190 / 0x1d0 against TILER_PRESENT 0x110. High words are +4. */
	kShaderPwrOn = 0x180, kShaderPwrOnHi = 0x184, kShaderPwrTrans = 0x200,
	kTilerPwrOn = 0x190, kTilerPwrOnHi = 0x194, kTilerPwrTrans = 0x210,
	kGpuCmd = 0x030, kGpuStatus = 0x034,
	kGpuCycleCount = 0x090, kGpuTimestamp = 0x098,
	kGpuFaultStatus = 0x03c, kGpuFaultAddress = 0x040,
	kGpuFaultAddressHi = 0x044,
	kCoherencyFeatures = 0x300, kCoherencyProtocol = 0x304,
	kShaderConfig = 0x0f04, kTilerConfig = 0x0f08, kL2MmuConfig = 0x0f0c, kCsfConfig = 0x0f00,
	kPwrKey = 0x050, kMcuControl = 0x700, kMcuStatus = 0x704,
	kStackPresent = 0xe00, kDoorbell0 = 0x80000,
	kL2Config = 0x048, kAsnHash0 = 0x2c0, kAsnHashStep = 0x4, kAsnHashCount = 3,
	/*
	 * The IPA block, which the vendor driver arms before every firmware start. Offsets are
	 * taken from the phone's own cold-start trace (records 69 to 90); nothing in mainline
	 * panthor or in the headers this file includes names them.
	 */
	kIpaCmd = 0x40000, kIpaStatus = 0x40004, kIpaClear = 0x40008,
	kIpaCfg = 0x40010, kIpaCfgEnd = 0x4002c, kIpaCounter0 = 0x40100,
};
#define kJobGlobalIf            (1u << 31)
#define kIpaCfgValue            0x20181004u     /* what the vendor leaves in 0x40010 */
#define kAsTranscfgUnmapped     0x42000001ull   /* ADRMODE_UNMAPPED, PTW_MEMATTR write-back */
#define kMcuControlDisable      0u              /* MCU_CONTROL_DISABLE */
#define kMcuControlEnable       1u              /* MCU_CONTROL_ENABLE */
#define kMcuControlAuto         2u              /* MCU_CONTROL_AUTO: start it */
/* MCU_CONTROL bit 31 in the 2025 DDK is DOORBELL_DISABLE. Nothing in r32p1 ever writes it, but
 * this driver has walked every neighbouring offset at some point, and a stray 0xffffffff here
 * would leave the core running and permanently deaf. Read it back; it costs one load. */
#define kMcuControlDoorbellDisable (1u << 31)
/* GPU_IRQ_STATUS bit 19, from mali_kbase_gpu_regmap_csf.h:314. The macro there is named
 * MCU_STATUS_GPU_IRQ, which reads as if it belonged to 0x704; it does not. 0x704 is a three-value
 * enum and this bit lives in the GPU interrupt block. */
#define kGpuIrqMcuAttention     (1u << 19)
#define kMcuStatusEnabled       1u              /* MCU_STATUS_ENABLED, not proof of firmware boot */
#define kGpuCmdSoftReset        0x101u          /* GPU_CMD_DEF(1, 1), panthor_gpu_regs.h */
#define kGpuCmdFlushCaches      0x3304u         /* GPU_CMD_DEF(4, 0x33): clean+inv l2 and lsc */
#define kGpuIrqResetCompleted   (1u << 8)       /* GPU_IRQ_RESET_COMPLETED */
#define kGpuIrqCleanCaches      (1u << 17)      /* GPU_IRQ_CLEAN_CACHES_COMPLETED */
/*
 * The two power-change bits, and the difference between them is the whole point.
 *
 * POWER_CHANGED_SINGLE is one domain finishing a transition; POWER_CHANGED_ALL is every domain in
 * the block having finished. The phone raises ALL forty times in one boot and SINGLE never; this
 * board has raised SINGLE and never ALL on every run since the driver was written, which is what a
 * transition that begins and does not complete looks like from the outside.
 */
#define kGpuIrqPowerChangedSingle (1u << 9)     /* GPU_IRQ_POWER_CHANGED_SINGLE */
#define kGpuIrqPowerChangedAll  (1u << 10)      /* GPU_IRQ_POWER_CHANGED_ALL */
/*
 * What the vendor driver leaves in GPU_IRQ_MASK: fault, protected fault, reset completed,
 * power changed (single and all). Taken from the phone itself -- see the note on
 * start_mcu() -- rather than from a header, because it is a value and not a name.
 */
#define kGpuIrqMaskVendor       0x00080503u
#define kCoherencyNone          31u             /* GPU_COHERENCY_NONE */
#define kPwrKeyUnlock           0x2968a819u     /* GPU_PWR_KEY_UNLOCK, panfrost_regs.h:55 */
/*
 * GPU_L2_CONFIG bit 24, the switch that makes the L2 use the three ASN hash words instead of
 * the hash the hardware was wired with. Both this bit and the words themselves are new in the
 * mt8196 patch "drm/panthor: add custom ASN_HASH support for mt8196" (LKML, reviewed by Boris
 * Brezillon), whose commit message says in as many words: "Without custom ASN_HASH, FW fails
 * to boot". That board reported "Unhandled Page fault in AS0 at VA 0x0" and then status=fatal,
 * which is a louder failure than ours, but the mechanism is the one thing that can stop the
 * very first fetch while leaving every register readable and every power domain reported up.
 */
#define kL2ConfigAsnHashEnable  (1u << 24)
static const uint32_t kAsnHashMt8196[kAsnHashCount] = { 0xbu, 0xeu, 0x0u };
#define kL2PresentFirst         1u              /* only the first L2 core group is ever used */
/*
 * A version word no firmware build would ever publish, used to find out whether the MCU ran at
 * all. The top byte of a real CSF interface version is the major number (four for this
 * generation), so a byte of 0xff cannot be mistaken for one.
 */
#define kSentinelVersion        0xff5aa5f0u

/* One address space's registers, 0x40 apart, the first at 0x2400 (panthor_mmu.c). */
#define asReg(as, off)          (0x2400u + ((as) << 6) + (off))
enum : uint32_t {
	kAsTranstab = 0x00, kAsMemattr = 0x08, kAsLockaddr = 0x10, kAsCommand = 0x18,
	kAsFaultStatus = 0x1c, kAsFaultAddress = 0x20, kAsStatus = 0x28, kAsTranscfg = 0x30,
};
/*
 * UPDATE is one, and the phone's own register trace settles it for this address space: the very
 * first thing kbase does on a cold start is program AS0 -- transcfig, transtab, memattr -- and
 * then write one to AS_COMMAND at 0x2418 (record 7 of the trace). The three that appears at
 * 0x4636c in the module is a different path, for the address spaces the scheduler hands to
 * contexts, and copying it here would have been a change away from the one sequence that is
 * known to work on this part.
 */
enum : uint32_t { kAsCmdUpdate = 1, kAsCmdLock = 2, kAsCmdFlushMem = 5, kAsCmdUpdateLock = 3 };

/* ------------------------------------------------------------------ state */

static vm_offset_t gSpm, gTopckgen, gMfgcfg, gMfgpll, gGpu, gInfracfg, gMfgrpc, gGpueb;
static unsigned gMaliProgress;                  /* how far bring-up got; 1 and 2 are the milestones */
static unsigned gMaliLevel = 2;                 /* the boot-arg's number: 1 power, 2 all, 3..5 stop earlier */
static unsigned gMaliAsnHash = 1;               /* mali_asn=0 turns off the custom ASN hash */
static unsigned gMaliWitness;                   /* mali_witness=1 runs 32 bytes of our own code instead */
static unsigned gMaliGpueb;                     /* mali_gpueb=1 maps a block whose clock is off */
static unsigned gMaliRpc;                       /* mali_rpc=1 touches mfg_rpc, 2 reads it only */
/*
 * mali_as0=1 parks address space 0 the way the other seven are parked, instead of giving it the
 * firmware's table. Nothing else changes: same clear, same three writes, same UPDATE.
 *
 * This is the one comparison that separates the two halves of a question every run so far has
 * answered ambiguously. AS0 is handed an AArch64 table and its UPDATE raises MMU_BUS_ERROR(0);
 * AS2 through AS7 are handed a zero table with ADRMODE_UNMAPPED and their UPDATEs raise nothing,
 * on every single boot, seven times over. Two readings, two outcomes, four candidate causes: the
 * granule, the table contents, the root address, or address space zero itself.
 *
 * Parking AS0 collapses it. If a parked AS0 also raises the bus error, the fault is in the slot
 * or in how it is being addressed and no amount of table work will move it. If a parked AS0 is
 * silent -- as its seven neighbours are -- then the slot is fine and the error comes from the
 * walk over what was written into TRANSTAB, which leaves the table as the only thing to fix.
 */
static unsigned gMaliAs0;                       /* mali_as0=1 parks AS0 as a control */
static unsigned gMaliPreAs0;                    /* mali_preas0=1 updates AS0 before the L2 comes up */
static unsigned gMaliNoApClear;                 /* mali_noapclear=1 skips 13 diagnostic MMU clears */
static unsigned gMaliNoPwrClear;                /* mali_nopwrclear=1 skips two L2 power IRQ clears */
static unsigned gMaliAs0First;                  /* mali_as0first=1 moves the pre-AS0 block before AS2..7 */
static unsigned gMaliDomainWait;                /* mali_domainwait=1 polls SPM at 10us/1s like scpsys */
static unsigned gMaliFwNc;                      /* mali_fwnc=1 maps every firmware section non-cacheable */
static unsigned gMaliFwX;                       /* mali_fwx=1 makes every firmware section executable */
static unsigned gMaliPtVendor;                  /* mali_ptvendor=1 matches noncoherent kbase SH/NG bits */
static unsigned gMaliBpVerify;                  /* mali_bpverify=1 releases bus protection and confirms STA */
static unsigned gMaliScClk;                     /* mali_scclk=1 un-gates CLK_CFG_5 bit7 (mfgsc_ref_ck) */
static unsigned gMaliScUpd;                     /* mali_scupd=1 enables and commits the MFGSC mux */
static unsigned gMaliSc26;                      /* with scupd, select the known-live 26 MHz parent */
static unsigned gMaliMcuWin;                    /* mali_mcuwin=1 prints the whole 0x3000..0x3100 block */
static unsigned gMaliMcuSeq;                    /* mali_mcuseq=1 writes ENABLE before AUTO */
static unsigned gMaliEarlyAs0;                  /* set once the early path has already done it */
/*
 * mali_cold=1 walks the path the phone's own driver walks when the machine boots, and nothing else.
 *
 * There are two register traces off this device and they are not the same path. The five hundred
 * records taken after its reset node was written start with GPU_CMD = 0x3304 and 0x0101, load four
 * configuration registers with zero, and set the coherency protocol to 0x1f. The twenty-four
 * hundred records of an ordinary boot do none of that: across the entire cold start there is not
 * one write to 0x030, 0x050, 0x048, 0x0f00, 0x0f04, 0x0f08, 0x0f0c or 0x304.
 *
 * This driver had been following the reset trace, on the reasoning that a block handed over by a
 * bootloader is closer to a reset than to a warm start. That reasoning produced a path no working
 * machine has ever run: ours resets the block, zeroes registers the vendor leaves alone, and
 * writes a power key the vendor never writes, before it ever asks the MCU to start. The cold
 * trace is the one that ends with a running firmware, so it is the one to copy.
 *
 * Switching it off puts the old sequence back, so the two can be compared in one line.
 */
static unsigned gMaliCold = 1;                  /* mali_cold=0 restores the reset-path writes */
/*
 * mali_gpm=0 skips the initiator-to-memory protector step.
 *
 * On by default because the vendor has it on by default: gpufreq_mt6895.c:5196 sets
 * g_gpm_enable = true unconditionally before power control can ever run, so no phone reaches a
 * powered GPU without those five writes. This is the one step of the vendor's nine that this
 * driver has never performed, and the only one whose absence leaves a master gate unprogrammed.
 */
static unsigned gMaliGpm = 1;                   /* mali_gpm=0 skips the I2M protector */
/*
 * mali_ostd=1 reads 0x13FBFF40 twice, in the same page as the protector words, described second
 * hand as MFG_MAX_OSTD_R_CON_0. Off by default: nothing in the vendor tree names it, and an
 * undecoded word in this block hangs the bus rather than returning.
 */
static unsigned gMaliOstd;                      /* mali_ostd=1 samples the outstanding counter */
/*
 * mali_sema=1 puts the write half of the AOC semaphore handshake back.
 *
 * Off by default now. The write is the vendor's own idiom (gpufreq_mt6895.c:3244-3252 does
 * read, or-in bit 0, write, then test), and it is also the only hardware *write* this driver
 * added between the last image that booted Android and the one that went into a preloader
 * loop. Every other change in that window was a read or a longer wait. One variable at a time:
 * the reads alone are what the booting image did, so that is where it starts.
 */
static unsigned gMaliSema;                      /* mali_sema=1 writes the semaphore request */
static unsigned gMaliPdcAuto;                    /* mali_pdcauto=1 powers only mfg1 and lets PDCv2 take the rest */
/* Round comparisons keep these independent and opt in. */
static unsigned gMaliFreeRun;                   /* mali_freerun=1 sets four GLOBAL_CON free-run bits */
static unsigned gMaliParkAttrs;                 /* mali_parkattrs=1 matches cold AS2/AS3 MEMATTR */
static unsigned gMaliTrace;                     /* mali_trace=1 records actual GPU stores */
static unsigned gMaliTrial;                     /* numeric experiment identifier */
static unsigned gMaliLowMem = 1;
static bool gMaliDone;

#define mrd(at)                 (*(volatile uint32_t *)(gGpu + (at)))
/* Record completed stores without issuing extra MMIO or printing between stores. */
static struct {
	uint32_t at, value;
} gMaliWrites[128];
static unsigned gMaliWriteCount;
static bool gMaliWritesFrozen;
static bool gMaliWritesPrinted;

static void
mali_gpu_write(uint32_t at, uint32_t value)
{
	*(volatile uint32_t *)(gGpu + at) = value;
	__asm__ volatile ("dsb sy" ::: "memory");
	if (gMaliTrace && !gMaliWritesFrozen) {
		unsigned n = gMaliWriteCount++;

		if (n < sizeof(gMaliWrites) / sizeof(gMaliWrites[0])) {
			gMaliWrites[n].at = at;
			gMaliWrites[n].value = value;
		}
	}
}

#define mwr(at, v)              mali_gpu_write((at), (v))

static void
mali_write_trace(const char *when)
{
	unsigned i;

	printf("mali: trial %u %s: level %u cold %u low %u preas0 %u freerun %u parkattrs %u trace %u as0first %u noapclear %u nopwrclear %u domainwait %u fwnc %u fwx %u\n",
	    gMaliTrial, when, gMaliLevel, gMaliCold, gMaliLowMem, gMaliPreAs0,
	    gMaliFreeRun, gMaliParkAttrs, gMaliTrace, gMaliAs0First,
	    gMaliNoApClear, gMaliNoPwrClear, gMaliDomainWait, gMaliFwNc, gMaliFwX);
	if (!gMaliTrace || gMaliWritesPrinted) {
		return;
	}
	gMaliWritesFrozen = true;
	gMaliWritesPrinted = true;
	printf("mali: write trace: %u completed GPU stores (capacity %u)\n", gMaliWriteCount,
	    (unsigned)(sizeof(gMaliWrites) / sizeof(gMaliWrites[0])));
	for (i = 0; i < gMaliWriteCount && i < sizeof(gMaliWrites) / sizeof(gMaliWrites[0]); i++) {
		printf("mali: write[%u] %#x=%#x\n", i, gMaliWrites[i].at, gMaliWrites[i].value);
	}
}

static uint32_t
reg_rd(vm_offset_t base, uint32_t at)
{
	uint32_t v = *(volatile uint32_t *)(base + at);

	__asm__ volatile ("dsb sy" ::: "memory");
	return v;
}

static void
reg_wr(vm_offset_t base, uint32_t at, uint32_t v)
{
	__asm__ volatile ("dsb sy" ::: "memory");
	*(volatile uint32_t *)(base + at) = v;
	__asm__ volatile ("dsb sy" ::: "memory");
}

/* ------------------------------------------------------------------ GPU-visible memory */

/*
 * Memory the GPU reads and writes. The GPU is not cache coherent with the CPU here
 * (COHERENCY_FEATURES reads 0x1 and the vendor's own kbase runs with coherency off), so every
 * page handed to it is marked uncached in the CPU's own tables as well; otherwise a write the
 * CPU still has in a cache line would arrive at the GPU late or not at all.
 *
 * Pages are taken one at a time and mapped one at a time, so nothing here needs physically
 * contiguous memory. That is deliberate: the CSF layout wants six regions totalling 1.4 MB, and
 * a single physically contiguous request of that size fails after the machine has been up for a
 * while (the RK3588 port hit exactly this and had to grow a chunk list).
 */
struct mali_mem {
	vm_offset_t kva;
	uint64_t pa;            /* only meaningful when uncached: the fixed address behind kva */
	size_t size;
	uint32_t pages;
	bool uncached;
};

/*
 * Where the MCU's memory comes from, and why there is a choice.
 *
 * Everything this driver builds -- the four page table levels, the seven firmware sections and the
 * shared block the MCU writes its answers into -- is read by the GPU through its own MMU, not by
 * the CPU. kmem_alloc() hands out pages from wherever the kernel's free list happens to be, and on
 * this phone that is above four gigabytes (XNU's own window starts at 0x100000000). Observed root
 * page table addresses across runs: 0x102c2c000, 0x1ed84c000, 0x1ffb3c000 -- 4.0, 7.7 and 8.5 GiB.
 *
 * The last of those is where the interesting reading came from. Driving AS0 one write at a time
 * showed all three configuration registers writing clean, and MMU_BUS_ERROR(0) appearing only when
 * AS_COMMAND=UPDATE made the walker read the root table. A walk that cannot read its own root
 * reports exactly that: a bus error with AS_FAULTSTATUS and AS_FAULTADDRESS both zero, because no
 * translation was ever attempted.
 *
 * So the driver can now place all of it below four gigabytes instead. 0x50000000 is inside the
 * 0x48402000-0x6fffffff System RAM block that Android's own iomem dump shows as free, above
 * anything the boot chain loads (chain_stub sits at 0x60000000, the UFS bounce area at
 * 0x60100000, the kernel at 0x40080000) and clear of ramoops at 0x48090000-0x4816ffff. The bump
 * pointer stops 192 MiB later, well short of chain_stub.
 *
 * These pages are mapped uncached: ml_io_map_unmappable() with VM_WIMG_WCOMB, which is the same
 * attribute ml_io_map() gives every device buffer here. That removes the cache maintenance
 * question entirely -- the CPU's writes are in memory when the store retires -- which is why
 * mem_flush() below returns immediately for them.
 */
static bool mem_alloc_high(struct mali_mem *m, size_t size);

#define MALI_LOW_BASE   0x50000000ULL
#define MALI_LOW_END    0x5c000000ULL
static uint64_t gLowNext = MALI_LOW_BASE;

static bool
mem_alloc_low(struct mali_mem *m, size_t size)
{
	uint64_t pa = (gLowNext + PAGE_MASK) & ~(uint64_t)PAGE_MASK;
	vm_offset_t kva;

	size = (size + PAGE_MASK) & ~(size_t)PAGE_MASK;
	if (size == 0) {
		return false;
	}
	if (pa + size > MALI_LOW_END) {
		printf("mali: the low window is full (%#llx + %zu past %#llx)\n",
		    pa, size, MALI_LOW_END);
		return false;
	}
	kva = ml_io_map_unmappable((vm_offset_t)pa, (vm_size_t)size, MALI_WIMG_WCOMB);
	if (kva == 0) {
		printf("mali: could not map %zu bytes at %#llx\n", size, pa);
		return false;
	}
	memset((void *)kva, 0, size);
	m->kva = kva;
	m->pa = pa;
	m->size = size;
	m->pages = (uint32_t)(size >> PAGE_SHIFT);
	m->uncached = true;
	gLowNext = pa + size;
	return true;
}

/* The one entry point the rest of the file uses. mali_low=0 puts everything back on kmem_alloc()
 * for an A/B run without a rebuild. */
static bool
mem_alloc(struct mali_mem *m, size_t size)
{
	if (gMaliLowMem) {
		if (mem_alloc_low(m, size)) {
			return true;
		}
		printf("mali: falling back to the kernel's own pages\n");
	}
	return mem_alloc_high(m, size);
}

static bool
mem_alloc_high(struct mali_mem *m, size_t size)
{
	vm_offset_t kva = 0;
	uint32_t i;

	size = (size + PAGE_MASK) & ~(size_t)PAGE_MASK;
	if (size == 0) {
		return false;
	}
	if (kmem_alloc(kernel_map, &kva, size, KMA_KOBJECT | KMA_ZERO | KMA_DATA,
	    VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		return false;
	}
	m->kva = kva;
	m->pa = 0;
	m->size = size;
	m->pages = (uint32_t)(size >> PAGE_SHIFT);
	m->uncached = false;
	/*
	 * The pages stay cacheable in the CPU's own tables on purpose.
	 *
	 * The first version of this marked every page uncached with pmap_set_cache_attributes() so
	 * that the GPU and the CPU would agree they were looking at memory with no cache in between.
	 * That is not what that call means here: the page came from kmem_alloc() and is already
	 * mapped cacheable in kernel_map, so changing the pmap entry underneath a live mapping leaves
	 * one physical page with two descriptions -- the CPU still writes through its caches while
	 * the hardware is told there are none -- and the machine died three resets in a row without
	 * ever reaching the point where it could say why.
	 *
	 * The GPU is coherent with the CPU on this SoC for the sharable, inner-shareable leaves the
	 * firmware's own sections ask for, and the leaves that ask for device memory are handled in
	 * the page tables this file builds (IDX_NC). What is left is the ordering a non-snooping
	 * reader needs, and that is what mem_flush() below does, at the points where ownership of a
	 * buffer actually changes hands.
	 */
	for (i = 0; i < m->pages; i++) {
		uint64_t pa = kvtophys(kva + ((vm_offset_t)i << PAGE_SHIFT));

		if (pa == 0) {
			printf("mali: page %u of %u has no physical address\n", i, m->pages);
			return false;
		}
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	return true;
}

/* Hand a buffer to the GPU: everything the CPU wrote must be in memory before the doorbell rings.
 * Hand it back: anything the GPU wrote must not be shadowed by a line the CPU still holds. One
 * routine covers both directions because FlushPoC_DcacheRegion is a clean followed by an
 * invalidate; calling it before and after a handover is the whole protocol.
 *
 * Buffers in the low window are mapped uncached, so there is no line to clean or invalidate and a
 * dsb is the whole of the handover. Keeping the call in the same shape for both kinds means the
 * call sites do not have to know which they have. */
static void
mem_flush(const struct mali_mem *m)
{
	if (!m->uncached) {
		FlushPoC_DcacheRegion(m->kva, m->size);
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

static uint64_t
mem_pa(const struct mali_mem *m, size_t offset)
{
	/* The low window is mapped one-to-one, so the address the CPU writes through is also the
	 * address the GPU has to be given. kvtophys() would answer the same thing, but it is a walk
	 * of the CPU's tables for a value already known. */
	if (m->uncached) {
		return m->pa + offset;
	}
	return kvtophys(m->kva + offset);
}

/* ------------------------------------------------------------------ the MCU's page tables */

/*
 * AArch64, 4 KiB granule, 48-bit, four levels: exactly what the firmware's own translations expect
 * and what io-pgtable-arm builds for Panthor. The tables have to be readable by the GPU, so they
 * live in uncached pages like everything else, and every edit is flushed out of the CPU's caches
 * before the GPU can walk it.
 */
/*
 * Descriptor type, low two bits. A table pointer at levels 0..2 is 0b11, and a level 3 page is
 * also 0b11 -- kbase spells them ENTRY_IS_ATE_L02 (1) for a block, ENTRY_IS_ATE_L3 (3) for a page
 * and ENTRY_IS_PTE (3) for the next-level pointer, all in
 * mmu/mali_kbase_mmu_mode_aarch64.c:33-36, and the AArch64 manual puts table descriptors at
 * 0b11 as well. What matters here is that a leaf without them is 0b00, which the walker reads
 * as an invalid descriptor: it produces no translation and, on this walker, no fault either.
 */
#define PTE_TABLE       3ull
#define PTE_PAGE        3ull
/*
 * Access permission is AP[2:1] in bits 7:6, and the three valued encoding means the "read only"
 * form is not a single bit.
 *
 *	0b00  privileged read/write, unprivileged no access
 *	0b01  privileged read/write, unprivileged read only
 *	0b10  reserved -- the encoding this file used to write, silently
 *	0b11  read only for both
 *
 * kbase says the same thing as ENTRY_ACCESS_RO = 3 << 6 (mmu/mali_kbase_mmu_mode_aarch64.c:33-42),
 * next to ENTRY_ACCESS_RW = 0 and ENTRY_ACCESS_USER_ABLE = 2 << 6. The old PTE_RDONLY here was
 * 1 << 7, which sets AP[2] and clears AP[1]: that is 0b10, the reserved one. Nothing has noticed
 * because no fetch has happened yet; the reset vector 0x010018ed sits in section 1, which is the
 * read-only one, so it would have mattered at exactly the moment the MCU finally started.
 */
#define PTE_UNPRIV      (1ull << 6)     /* AP = 0b01: privileged RW, unprivileged RO */
#define PTE_RDONLY      (3ull << 6)     /* AP = 0b11: read only everywhere */
#define PTE_SH_OS       (2ull << 8)
#define PTE_SH_IS       (3ull << 8)
#define PTE_AF          (1ull << 10)
#define PTE_NG          (1ull << 11)
/*
 * Execute-never is bit 54, and only bit 54.
 *
 * io-pgtable-arm.c:77 writes 3 << 53 because it is aiming at a CPU page table, where bit 53 is the
 * privileged XN bit and bit 54 the unprivileged one. The GPU's walker is kbase's, and kbase has one
 * bit: ENTRY_NX_BIT (1ULL << 54) in mmu/mali_kbase_mmu_mode_aarch64.c:43. Both spellings have been
 * used here and both survive a walk, so this is not what the MCU was stuck on -- but the value
 * should be the one the vendor driver writes, not a CPU encoding that happens to overlap.
 */
#define PTE_XN          (1ull << 54)
#define PTE_ADDRESS     0x0000fffffffff000ull
/*
 * Indices into AS_MEMATTR, not MAIR slots.
 *
 * A leaf page table entry carries a three bit index in bits 4:2 and the address space's AS_MEMATTR
 * register says what each index means. kbase fills that register from a fixed table
 * (mali_kbase_mmu_get_as_setup(), mmu/mali_kbase_mmu_csf.c:41) and then picks an index per region
 * in firmware_mem_flags() (csf/mali_kbase_csf_firmware.c:352):
 *
 *   cache mode none                      -> NON_CACHEABLE        (index 5, attribute 0x4c)
 *   cache mode cached                    -> IMPL_DEF_CACHE_POLICY (index 0, attribute 0x88)
 *   uncached coherent / cached coherent  -> SHARED                (index 6, attribute 0x08)
 *
 * Ours used to be 0 and 1, which are the indices io-pgtable-arm hands to a CPU page table and mean
 * nothing here: index 1 in kbase's table is FORCE_TO_CACHE_ALL, not write-back.
 */
#define IDX_CACHE       0               /* AS_MEMATTR_INDEX_IMPL_DEF_CACHE_POLICY */
/*
 * The granule pt_map() actually indexes with: table[(va >> 12) & 0x1ff].
 *
 * Deliberately not PAGE_SIZE. On this kernel PAGE_SIZE is 16K (see the note at the firmware
 * mapping loop), while the level-3 table this code builds is walked at 4K by whatever the MCU
 * does with AS_TRANSTAB -- and pt_map agrees with the MCU, not with PAGE_SIZE.
 */
#define PT_LEAF_GRANULE  0x1000ull

#define IDX_NC          5               /* AS_MEMATTR_INDEX_NON_CACHEABLE */
#define IDX_SHARED      6               /* AS_MEMATTR_INDEX_SHARED */
/*
 * The attribute table the vendor driver installs, read back off the phone.
 *
 * kbase builds this value from seven named constants rather than deriving it from a MAIR
 * (mmu/mali_kbase_mmu_csf.c:41-64): index 0 IMPL_DEF_CACHE_POLICY 0x88, 1 FORCE_TO_CACHE_ALL 0x8f,
 * 2 WRITE_ALLOC 0x8d, 3 AARCH64_OUTER_IMPL_DEF 0x88, 4 AARCH64_OUTER_WA 0x8d, 5 NON_CACHEABLE 0x4c,
 * 6 SHARED 0x08. Packed little endian that is 88 8f 8d 88 8d 4c 08 00, and that is exactly what
 * the register held: AS_MEMATTR_LO 888d8f88, AS_MEMATTR_HI 00084c8d.
 *
 * This replaces panthor's mair_to_memattr() result. That function is correct for panthor, whose
 * pages the CPU also maps, but MediaTek's tree does not use it and the values differ in almost
 * every index -- index 5 here is 0x4c where panthor put 0x4c too, but index 0 is 0x88 where
 * panthor had 0x4c. A page marked cached in a PTE would have been non-cacheable to the GPU.
 */
#define MALI_MEMATTR    0x00084c8d888d8f88ull

/* Every table page ever handed out, so a physical address can be turned back into something the
 * CPU can read. A linear scan is fine: a whole address space is a few dozen tables. */
struct mali_pt {
	uint64_t pa;
	uint64_t *kva;
};
#define MALI_PT_MAX     512
static struct mali_pt gPt[MALI_PT_MAX];
static unsigned gPtCount;
/* Whether the tables came from the low window. They all come from the same place -- pt_new() is
 * the only caller -- so one flag covers them, and pt_map() uses it to skip the cache flush that
 * only makes sense for cacheable pages. */
static bool gPtUncached;

static uint64_t *
pt_kva(uint64_t pa)
{
	unsigned i;

	for (i = 0; i < gPtCount; i++) {
		if (gPt[i].pa == pa) {
			return gPt[i].kva;
		}
	}
	return NULL;
}

static uint64_t *
pt_new(uint64_t *pa)
{
	struct mali_mem m;

	if (gPtCount == MALI_PT_MAX) {
		printf("mali: out of page table slots\n");
		return NULL;
	}
	if (!mem_alloc(&m, PAGE_SIZE)) {
		return NULL;
	}
	gPtUncached = m.uncached;
	gPt[gPtCount].pa = mem_pa(&m, 0);
	gPt[gPtCount].kva = (uint64_t *)m.kva;
	*pa = gPt[gPtCount].pa;
	return gPt[gPtCount++].kva;
}

/* One 4 KiB page: GPU address `va` gets physical `pa`, with `leaf` as the attribute bits. */
static bool
pt_map(uint64_t *root, uint64_t va, uint64_t pa, uint64_t leaf)
{
	uint64_t *table = root;
	int level;

	for (level = 0; level < 3; level++) {
		unsigned index = (unsigned)((va >> (39 - 9 * level)) & 0x1ffu);

		if (!(table[index] & 1)) {
			uint64_t child_pa = 0;
			uint64_t *child = pt_new(&child_pa);

			if (child == NULL) {
				return false;
			}
			/*
			 * The access flag belongs on table descriptors too.
			 *
			 * kbase has one builder for both kinds of entry, and it sets bit 10 in
			 * each of them: entry_set_pte() writes (phy & PAGE_MASK) |
			 * ENTRY_ACCESS_BIT | ENTRY_IS_PTE and entry_set_ate() writes the same
			 * bit alongside its own type. This driver had been putting the flag on
			 * the leaf only, which leaves every table the walker reads without it.
			 * A walk that stops at a descriptor with the flag clear latches an MMU
			 * interrupt instead of a page fault, which is the reading that finally
			 * turned up: mmu irq 0x10000, MMU_BUS_ERROR(0), with AS0 faultstatus
			 * still zero.
			 */
			table[index] = child_pa | PTE_TABLE | PTE_AF;
			if (!gPtUncached) {
				FlushPoC_DcacheRegion((vm_offset_t)&table[index],
				    sizeof(table[index]));
			}
		}
		table = pt_kva(table[index] & PTE_ADDRESS);
		if (table == NULL) {
			return false;
		}
	}
	table[(va >> 12) & 0x1ffu] = (pa & PTE_ADDRESS) | leaf;
	if (!gPtUncached) {
		FlushPoC_DcacheRegion((vm_offset_t)&table[(va >> 12) & 0x1ffu],
		    sizeof(uint64_t));
	}
	return true;
}

/* ------------------------------------------------------------------ the firmware container */

static void *sect_at(uint64_t va);

/*
 * The CSF image is a container, not a raw blob (panthor_fw.c): a twenty-byte header and then a run
 * of entries. An interface entry describes one region of the firmware's address space: where it
 * lives (va_start..va_end), which bytes of the file fill it (data_start..data_end, the rest
 * zeroes), and how the MCU may use it. Only these entries matter for loading; the others are
 * metadata for tools.
 */
#define FW_MAGIC        0xc3f13a6eu
#define FW_ENTRY_IFACE  0u
#define SEC_RD          (1u << 0)
#define SEC_WR          (1u << 1)
#define SEC_EX          (1u << 2)
#define SEC_CACHE_MASK  (3u << 3)
#define SEC_CACHED      (1u << 3)
#define SEC_PROT        (1u << 5)
#define SEC_SHARED      (1u << 30)
#define SEC_ZERO        (1u << 31)

#define MALI_SHARED_VA  0x04000000ull
#define MALI_MAX_SECT   16

struct mali_section {
	uint32_t flags;
	uint64_t va_start, va_end;
	struct mali_mem mem;
};

static struct mali_section gSect[MALI_MAX_SECT];
static unsigned gSectCount;
static struct mali_section *gShared;

static uint32_t
le32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t *gMcuRoot;
static uint64_t gMcuRootPa;

static bool
load_firmware(void)
{
	uint32_t offset;
	uint32_t size;

	if (MALI_FW_LENGTH < 20 || le32(kMaliFirmware) != FW_MAGIC) {
		printf("mali: the firmware image does not start with the CSF magic\n");
		return false;
	}
	size = le32(kMaliFirmware + 16);
	printf("mali: firmware %u bytes, header %u.%u, entry table ends at %u\n",
	    MALI_FW_LENGTH, kMaliFirmware[5], kMaliFirmware[4], size);
	if (kMaliFirmware[5] != 0 || size > MALI_FW_LENGTH) {
		printf("mali: the firmware header is not one this loader knows\n");
		return false;
	}
	if ((gMcuRoot = pt_new(&gMcuRootPa)) == NULL) {
		printf("mali: no memory for the MCU's page tables\n");
		return false;
	}
	offset = 20;
	while (offset + 4 <= size) {
		uint32_t ehdr = le32(kMaliFirmware + offset);
		uint32_t type = ehdr & 0xffu;
		uint32_t esize = (ehdr >> 8) & 0xffu;

		if (esize < 4 || (esize & 3) || offset + esize > size) {
			printf("mali: bad firmware entry at %u (%#x)\n", offset, ehdr);
			return false;
		}
		if (type == FW_ENTRY_IFACE) {
			const unsigned char *e = kMaliFirmware + offset + 4;
			uint32_t flags = le32(e);
			uint64_t va_start = le32(e + 4);
			uint64_t va_end = le32(e + 8);
			uint32_t data_start = le32(e + 12);
			uint32_t data_end = le32(e + 16);

			if (esize < 24 || va_end < va_start || data_end < data_start ||
			    data_end > MALI_FW_LENGTH) {
				printf("mali: corrupt firmware section at %u\n", offset);
				return false;
			}
			/*
			 * The three PROT entries are the firmware's .bss: 256KB covering VA
			 * 0x3000000-0x3040000 with no data in the image at all. kbase does not
			 * skip them -- it allocates them through the protected memory allocator
			 * (csf/mali_kbase_csf_firmware.c:537 kbase_csf_protected_memory_alloc)
			 * and maps them, and only gives up if the allocator is missing. There is
			 * no allocator here, so they get ordinary pages: the MCU only needs the
			 * memory to exist, and leaving them unmapped means the first store to a
			 * global variable lands on nothing. That is a plausible reason for a core
			 * that reports ENABLED, never writes its version word and never faults.
			 */
			if (flags & SEC_PROT) {
				printf("mali: section %#llx-%#llx is protected-mode, "
				    "mapped with ordinary memory\n", va_start, va_end);
			}
			if (va_end > va_start) {
				struct mali_section *s;
				bool cached;
				unsigned index;
				uint64_t leaf, va;

				if (gSectCount == MALI_MAX_SECT) {
					printf("mali: too many firmware sections\n");
					return false;
				}
				s = &gSect[gSectCount];
				s->flags = flags;
				s->va_start = va_start;
				s->va_end = va_end;
				if (!mem_alloc(&s->mem, (size_t)(va_end - va_start))) {
					printf("mali: no memory for section %#llx-%#llx\n",
					    va_start, va_end);
					return false;
				}
				if (data_end > data_start) {
					memcpy((void *)s->mem.kva, kMaliFirmware + data_start,
					    data_end - data_start);
				}
				/* The MCU walks these pages once it starts, and it does not snoop. */
				mem_flush(&s->mem);
				cached = (flags & SEC_CACHE_MASK) == SEC_CACHED;
				/*
				 * The shared section is the one the MCU writes back into, so its
				 * index decides whether we can see the firmware's answer at all.
				 *
				 * Its cache mode field reads 3, "cached coherent", and kbase does not
				 * map that straight to SHARED: firmware_mem_flags()
				 * (csf/mali_kbase_csf_firmware.c:337-355) downgrades it first. A
				 * platform without ACE loses cached coherent to uncached coherent,
				 * and a platform without ACE-Lite loses that to plain none. This one
				 * reports COHERENCY_NONE, so the index that ends up in the PTE is
				 * NON_CACHEABLE(5) -- the picture the vendor's AS_MEMATTR table shows
				 * too, where indices 5 and 6 both carry 0x4c for the same reason.
				 */
				/*
				 * mali_fwnc=1 forces every section to NON_CACHEABLE.
				 *
				 * The MCU does not snoop the CPU's caches. mem_flush() below writes the
				 * image out, but the PTE still says "cached", so a section the CPU has
				 * since brought back into its cache could be read by the MCU from DRAM
				 * as stale data -- and if that section is the vector table at VA 0, the
				 * reset fetch never happens. That is one explanation for status=1,
				 * no fault, untouched version word and a permanently busy debug_top.
				 */
				index = (gMaliFwNc || (flags & SEC_CACHE_MASK) != SEC_CACHED)
				    ? IDX_NC : IDX_CACHE;
				cached = (index == IDX_CACHE);
				/*
				 * PTE_PAGE is not decoration. Everything below it in this
				 * expression is attributes; the two low bits are what say
				 * "this is a level 3 page and the walk stops here". They
				 * were missing for the whole of M1: every leaf the MCU
				 * walked read back as an invalid descriptor, which is a
				 * translation failure that raises no MMU interrupt and
				 * writes no fault address -- exactly the silence this
				 * driver spent days chasing. The firmware's own vector
				 * table at VA 0 was unmapped in that sense, so the reset
				 * fetch never happened and no instruction ever ran.
				 */
				leaf = PTE_PAGE | PTE_AF | PTE_UNPRIV |
				    ((uint64_t)index << 2);
				/* kbase convert_mem_flags/get_mmu_flags omit SH and NG on
				 * noncoherent firmware mappings; keep the baseline selectable. */
				if (!gMaliPtVendor) {
					leaf |= PTE_NG | (cached ? PTE_SH_IS : PTE_SH_OS);
				}
				if (!(flags & SEC_WR)) {
					leaf |= PTE_RDONLY;
				}
				/*
				 * mali_fwx=1 forces every firmware section executable.
				 *
				 * The reset vector lives in section 0, and that section's flags word
				 * says r-- (no SEC_EX), so this code marks every one of its pages XN.
				 * The hardware reads the initial SP from VA 0 and the reset address
				 * from VA 4 -- a data read -- but the reset address it reads points
				 * into section 1 (0x010018ed), and if the fetch that follows is
				 * refused by the MMU there is no fault on AS0 because the fetch never
				 * becomes a transaction: status stays 1, debug_top goes busy and the
				 * version word keeps the sentinel. That is this board's exact shape.
				 */
				if (!(flags & SEC_EX) && !gMaliFwX) {
					leaf |= PTE_XN;
				}
				if (!(flags & SEC_WR) && gMaliFwX) {
					leaf &= ~PTE_RDONLY;
				}
				if (gMaliPtVendor) {
					printf("mali: vendor leaf sec%u attrs %#llx (SH=0 NG=0)\n",
					    gSectCount, (unsigned long long)leaf);
				}
				/*
				 * Step by the leaf granule, not by PAGE_SIZE.
				 *
				 * PAGE_SIZE on this kernel is 16K: the loader publishes dram-size
				 * 0xfe000000 (>1GB), so arm_init.c:417 picks PAGE_MAX_SHIFT and
				 * PAGE_SHIFT becomes 14. pt_map(), however, indexes the level-3 table
				 * with (va >> 12) & 0x1ff -- a 4K granule -- so this loop advancing by
				 * PAGE_SIZE wrote one 4K leaf out of every four and left the other
				 * twelve kilobytes of each 16K window without a valid descriptor.
				 *
				 * That is not a theoretical hole. The vendor image's reset vector is
				 * 0x010018ed, which lies in section 1 at leaf 1 (0x1001000-0x1001fff);
				 * leaf 1 was never mapped, so the very first instruction fetch after
				 * MCU_CONTROL=AUTO had no translation, and the MCU -- which cannot take
				 * a data abort for an instruction fetch it never issued -- reported
				 * enabled and executed nothing. Section 1 was 25% mapped: leaves 0, 4,
				 * 8 and 12 only.
				 *
				 * The witness experiment survived this by luck: its stub sits at VA
				 * 0x1000000 (leaf 0) and its two stores land at 0x04000000/0x04000004
				 * (section 9 leaf 0), all of which were mapped. That is why a 32-byte
				 * program and a quarter-megabyte firmware image disagreed.
				 */
				for (va = va_start; va < va_end; va += PT_LEAF_GRANULE) {
					if (!pt_map(gMcuRoot, va,
					    mem_pa(&s->mem, (size_t)(va - va_start)), leaf)) {
						printf("mali: out of page tables at %#llx\n", va);
						return false;
					}
				}
				if (va_start == MALI_SHARED_VA) {
					if (!(flags & SEC_SHARED)) {
						printf("mali: the interface section is not"
						    " marked shared\n");
						return false;
					}
					gShared = s;
				}
				printf("mali: section %u: va %#llx-%#llx flags %#x "
				    "(%s%s%s%s%s) %u bytes\n", gSectCount, va_start, va_end,
				    flags, (flags & SEC_RD) ? "r" : "-",
				    (flags & SEC_WR) ? "w" : "-", (flags & SEC_EX) ? "x" : "-",
				    cached ? " cached" : " nc",
				    (flags & SEC_SHARED) ? " shared" : "",
				    data_end - data_start);
				printf("mali: section %u: memattr index %u\n", gSectCount, index);
				gSectCount++;
			}
		}
		offset += esize;
	}
	/*
	 * Walk the tables the way the MCU will and print what it would read.
	 *
	 * A leaf whose low two bits are not 11 is an invalid descriptor: the walk stops there and
	 * nothing maps. That is exactly what the driver shipped through all of M1, so the numbers
	 * worth having in the log are the descriptor words themselves, not a conclusion about them.
	 * VA zero is the firmware's vector table and is the first thing the MCU fetches.
	 */
	{
		static const uint64_t vaList[] = { 0ull, 0x1000000ull, 0x2000000ull,
		    MALI_SHARED_VA, 0x40000000ull, 0x40022000ull };
		unsigned n;

		for (n = 0; n < sizeof(vaList) / sizeof(vaList[0]); n++) {
			uint64_t va = vaList[n];
			uint64_t *table = gMcuRoot;
			unsigned shift;
			int level;

			printf("mali: walk %#llx:", (unsigned long long)va);
			for (level = 0; level <= 3; level++) {
				uint64_t entry;

				if (table == NULL) {
					printf(" no table at L%d", level);
					break;
				}
				/* pt_map() uses 39, 30 and 21 for the three table levels. */
				shift = (level == 3) ? 12u : (unsigned)(39 - 9 * level);
				entry = table[(unsigned)((va >> shift) & 0x1ffu)];
				printf(" L%d %#llx", level, (unsigned long long)entry);
				if (level == 3) {
					/* The leaf: low two bits 11 means a valid page. */
					printf("%s", (entry & 3u) == 3u ? " page" : " INVALID");
					break;
				}
				if (!(entry & 1)) {
					printf(" table unset");
					break;
				}
				table = pt_kva(entry & PTE_ADDRESS);
			}
			printf("\n");
		}
	}
	if (gShared == NULL) {
		printf("mali: no shared interface section at %#llx\n", MALI_SHARED_VA);
		return false;
	}
	printf("mali: %u sections, %u page tables\n", gSectCount, gPtCount);
	/*
	 * Coverage self-check, so a mapping hole shows up as a number and not as a mystery.
	 *
	 * The 16K-versus-4K step bug survived fifteen rounds because nothing ever asked "how much
	 * of the firmware is actually mapped". Walking every leaf granule and comparing what the
	 * walk finds against what the section needs turns that question into one line of output.
	 */
	{
		unsigned si;
		unsigned bad = 0;

		for (si = 0; si < gSectCount; si++) {
			struct mali_section *sec = &gSect[si];
			uint64_t v;
			unsigned need = 0;
			unsigned have = 0;

			for (v = sec->va_start; v < sec->va_end; v += PT_LEAF_GRANULE) {
				uint64_t *table = gMcuRoot;
				int lvl;
				bool ok = true;

				need++;
				for (lvl = 0; lvl < 3; lvl++) {
					unsigned idx = (unsigned)((v >> (39 - 9 * lvl)) & 0x1ffu);

					if ((table[idx] & 1u) == 0u) {
						ok = false;
						break;
					}
					table = pt_kva(table[idx] & PTE_ADDRESS);
					if (table == NULL) {
						ok = false;
						break;
					}
				}
				if (ok && (table[(v >> 12) & 0x1ffu] & 1u) != 0u) {
					have++;
				}
			}
			if (need != have) {
				printf("mali: COVERAGE sec%u %#llx-%#llx: %u of %u leaves mapped\n",
				    si, (unsigned long long)sec->va_start,
				    (unsigned long long)sec->va_end, have, need);
				bad++;
			}
		}
		printf("mali: coverage: %s\n", bad ? "HOLES FOUND" : "every section fully mapped");
	}
	/*
	 * The reset vector, now that the sections exist.
	 *
	 * The hardware takes the initial stack pointer from VA 0 and the reset address from VA 4.
	 * This driver has never printed either, so every argument so far has assumed the vendor image
	 * puts 0x010018ed at +4 and that section 1 therefore holds the entry point. One line settles
	 * whether that is true; if it is not, the "we loaded the right image" premise needs re-examining.
	 */
	{
		volatile uint32_t *vec = (volatile uint32_t *)sect_at(0ull);
		void *code = sect_at(0x1000000ull);

		if (vec != NULL) {
			printf("mali: reset vector: sp %#x  pc %#x\n", vec[0], vec[1]);
			printf("mali: entry words at VA 0x1000000: %#x %#x %#x %#x\n",
			    vec[2], vec[3],
			    code ? *(volatile uint32_t *)code : 0u,
			    code ? *((volatile uint32_t *)code + 1) : 0u);
		} else {
			printf("mali: reset vector: no section covers VA 0\n");
		}
	}
	return true;
}

static void *
shared_at(uint64_t va)
{
	if (gShared == NULL || va < gShared->va_start || va >= gShared->va_end) {
		return NULL;
	}
	return (void *)(gShared->mem.kva + (size_t)(va - gShared->va_start));
}

/*
 * A witness small enough to have no excuses.
 *
 * The vendor firmware is a quarter of a megabyte arranged in four sections behind a page table
 * this driver builds; when nothing happens there are a hundred things it could be, and every
 * explanation raised so far has been an inference about what the core must have done rather than
 * an observation of it doing something. This replaces the question with one that has three
 * possible answers and needs no interpretation:
 *
 *	VA 0x04000000 <- 0x11		the reset fetch happened at all
 *	VA 0x04000004 <- 0x22		and the execution stream is linear
 *	VA 0x04000000 <- 0xdeadbeef	and it ran to the end of the code
 *
 * It is 32 bytes of Cortex-M7, assembled by clang for armv7m and checked with llvm-objdump:
 *
 *	f240 0000	movw	r0, #0x0000
 *	f2c0 4000	movt	r0, #0x0400	r0 = 0x04000000, the shared page
 *	f240 0111	movw	r1, #0x0011
 *	6001		str	r1, [r0]	first marker
 *	f240 0122	movw	r1, #0x0022
 *	6041		str	r1, [r0, #4]	second marker
 *	f64b 61ef	movw	r1, #0xbeef
 *	f6cd 61ad	movt	r1, #0xdead	r1 = 0xdeadbeef
 *	6001		str	r1, [r0]	third marker, over the first
 *	e7fe		b	.		stop where we are
 *
 * No stack, no memory beyond the two words, no dependency on anything the driver set up other
 * than the two page table entries that are already known good from the walk. If this runs, the
 * MCU works and the vendor image is what fails. If it does not, nothing about the firmware is
 * worth investigating until the fetch path itself is understood.
 */
static const unsigned char kMaliWitness[32] = {
	0x40, 0xf2, 0x00, 0x00, 0xc0, 0xf2, 0x00, 0x40,
	0x40, 0xf2, 0x11, 0x01, 0x01, 0x60, 0x40, 0xf2,
	0x22, 0x01, 0x41, 0x60, 0x4b, 0xf6, 0xef, 0x61,
	0xcd, 0xf6, 0xad, 0x61, 0x01, 0x60, 0xfe, 0xe7,
};

#define MALI_WITNESS_CODE_VA	0x1000000ull
#define MALI_WITNESS_FIRST	0x11u
#define MALI_WITNESS_SECOND	0x22u
#define MALI_WITNESS_DONE	0xdeadbeefu

static void *
sect_at(uint64_t va)
{
	unsigned i;

	for (i = 0; i < gSectCount; i++) {
		if (va >= gSect[i].va_start && va < gSect[i].va_end) {
			return (void *)(gSect[i].mem.kva + (size_t)(va - gSect[i].va_start));
		}
	}
	return NULL;
}

static void
flush_all_sections(void)
{
	unsigned i;

	for (i = 0; i < gSectCount; i++) {
		mem_flush(&gSect[i].mem);
	}
}

/*
 * Point the reset vector at the witness instead of the firmware.
 *
 * The vector table lives at VA zero, in section 0, and the hardware reads the stack pointer from
 * +0 and the reset address from +4. The vendor image puts 0x010018ed there -- section 1, offset
 * 0x18ec, thumb bit set -- so the patch is two writes: the witness over the first bytes of
 * section 1, and the vector in section 0 pointed at it. Nothing else in the image is touched, so
 * the page tables, the shared section and the address space are exactly what the full run builds.
 */
static void
bolt_witness(void)
{
	volatile uint32_t *vec = (volatile uint32_t *)sect_at(0ull);
	void *code = sect_at(MALI_WITNESS_CODE_VA);

	if (vec == NULL || code == NULL) {
		printf("mali: witness: no section covers VA 0 or VA %#llx\n",
		    (unsigned long long)MALI_WITNESS_CODE_VA);
		return;
	}
	printf("mali: witness: vector was sp %#x reset %#x\n", vec[0], vec[1]);
	memcpy(code, kMaliWitness, sizeof(kMaliWitness));
	vec[1] = (uint32_t)MALI_WITNESS_CODE_VA | 1u;
	flush_all_sections();
	printf("mali: witness: code at %#llx, reset vector now %#x\n",
	    (unsigned long long)MALI_WITNESS_CODE_VA, vec[1]);
}

/*
 * Read back what the witness left. Called after the start attempt, so the three values describe
 * how far the core got rather than what the CPU wrote there beforehand.
 */
static void
read_witness(void)
{
	volatile uint32_t *words = (volatile uint32_t *)sect_at(MALI_SHARED_VA);
	uint32_t first, second;

	if (words == NULL) {
		printf("mali: witness: the shared section is gone\n");
		return;
	}
	/*
	 * Both markers are read, and the first one is the verdict: the stub writes 0x11 there and
	 * overwrites it with 0xdeadbeef at the end, so a 0xdeadbeef means the whole thing ran and a
	 * 0x11 means it died between the two stores. Anything else means the first store never
	 * happened at all, which is the answer every previous run assumed.
	 */
	/*
	 * Read it three times, with barriers, before believing a zero.
	 *
	 * These pages come from mem_alloc_low(), which maps them with MALI_WIMG_WCOMB --
	 * write-combining, not cacheable. Writes may be gathered there; the read direction is
	 * not guaranteed to bypass a pending combine buffer either. The whole "the core never
	 * executed the first store" verdict rests on one read of one word, so ask three times
	 * and put a barrier between them: a stable 0xff5aa5f0 settles the question, a word that
	 * changes means the path, not the core, was the problem.
	 */
	__asm__ volatile ("dmb sy" ::: "memory");
	first = words[0];
	second = words[1];
	__asm__ volatile ("dmb sy" ::: "memory");
	printf("mali: witness: read1 %#x %#x", first, second);
	{
		uint32_t a = words[0];
		uint32_t b = words[1];

		__asm__ volatile ("dmb sy" ::: "memory");
		printf(" read2 %#x %#x", a, b);
		a = words[0];
		b = words[1];
		__asm__ volatile ("dmb sy" ::: "memory");
		printf(" read3 %#x %#x", a, b);
		if (a != first) {
			printf(" (UNSTABLE -- the read path is suspect, not the core)");
		}
	}
	printf(" trial %u\n", gMaliTrial);
	printf("mali: witness: verdict trial %u", gMaliTrial);
	if (first == MALI_WITNESS_DONE) {
		printf(" -- the core ran the witness to the end, so the MCU works\n");
	} else if (first == MALI_WITNESS_FIRST) {
		printf(" -- the first store landed and nothing after it\n");
	} else {
		printf(" -- the core never executed the first store\n");
	}
}

/* ------------------------------------------------------------------ M0: power and clocks */

static bool
domain_on(uint32_t at, const char *what)
{
	uint32_t v;
	unsigned spins;

	v = reg_rd(gSpm, at);
	/*
	 * "Already on" has to mean all six steps, and this test used to check five of them.
	 *
	 * The acknowledge was checked, the clock-disable was checked, the isolation was checked --
	 * and the reset release was not. A domain sitting at 0xc000000c passes all three of those
	 * tests while its PWR_RST_B bit is still clear, which is a domain powered up with its reset
	 * held. That is not a hypothetical shape: mfg1 is the domain the M7 core lives in (the
	 * vendor's own header annotates its device as "MPU, CM7"), and every run so far has taken
	 * this early return for mfg1 -- "mfg1 MPU was already on" -- so the three lines further down
	 * that are the only place PWR_RST_B is ever raised have never executed for the one domain
	 * that has a core in it. The branch also prints nothing but "already on", so the control word
	 * was never even observed there.
	 *
	 * Vendor scpsys_power_on() has no such short circuit at all: it reads, then walks all six
	 * steps unconditionally, and leaves "was it already on" to genpd's reference count above it.
	 * Following that is cheaper than reasoning about which subset is safe to skip.
	 *
	 * The SRAM bits are part of the test for the same reason they are part of the sequence. A
	 * domain whose rail is up, whose isolation is clear and whose reset is released can still
	 * have its SRAM powered down, and that is precisely the state mfg1 was in: 0xc000110d passes
	 * every one of the three tests this line used to make while bit 8 is asserting power-down on
	 * the one domain that holds the Cortex-M7. Taking the early return there left the core in
	 * an instruction memory that is not there.
	 */
	if ((v & SPM_STA_MASK) == SPM_STA_MASK && !(v & (SPM_PWR_ISO | SPM_PWR_CLK_DIS)) &&
	    (v & SPM_PWR_RST_B) && !(v & SPM_SRAM_PDN)) {
		printf("mali: %s was already on (%#x)\n", what, v);
		return true;
	}
	/* The order is scpsys_power_on()'s: ask, confirm, then release the isolation and the reset.
	 * Every step is written every time: a domain that comes up because its parent did still
	 * needs its own isolation cleared. */
	v |= SPM_PWR_ON;
	reg_wr(gSpm, at, v);
	v |= SPM_PWR_ON_2ND;
	reg_wr(gSpm, at, v);
	/*
	 * Wait the way scpsys_power_on() waits, not the way a tight loop waits.
	 *
	 * scpsys_pwr_con_is_on() (mtk-scpsys.c:236-256) has three answers, not two: both status
	 * bits set means on, neither set means off, and exactly one set returns -EINVAL, which it
	 * documents as "This happens while powering up a domain". The caller polls with tmp > 0,
	 * so -EINVAL counts as "keep waiting", with MTK_POLL_DELAY_US = 10 and MTK_POLL_TIMEOUT =
	 * USEC_PER_SEC -- a full second, one hundred thousand samples.
	 *
	 * This loop used to spin two million times with no delay at all. On this part each MMIO
	 * read is a few hundred nanoseconds, so it gave up in well under a second, and the run
	 * after that reported exactly the shape -EINVAL describes: mfg5 and mfg6 came back as
	 * 0x4000000d, one status bit set, and the code below then cleared the isolation and
	 * raised the reset on a domain that was still mid-handshake.
	 */
	{
		/*
		 * Two budgets, chosen by mali_domainwait.
		 *
		 * 0: 2000000 back-to-back reads (~0.72 s measured) -- the historical behaviour,
		 *    kept so a single run can show whether the delay is what matters.
		 * 1: 100000 reads at 10 us apart -- scpsys's MTK_POLL_DELAY_US/MTK_POLL_TIMEOUT,
		 *    i.e. the vendor's own one-second budget.
		 */
		unsigned limit = gMaliDomainWait ? 100000u : 2000000u;

		for (spins = 0; spins < limit; spins++) {
			if ((reg_rd(gSpm, at) & SPM_STA_MASK) == SPM_STA_MASK) {
				break;
			}
			/*
			 * mali_domainwait=1: wait the way scpsys waits, not the way a tight loop waits.
			 *
			 * The comment above already said this loop "used to spin two million times with
			 * no delay at all" and that scpsys polls with a 10us delay for a full second --
			 * but the loop itself was never changed. Two million back-to-back MMIO reads
			 * take about 0.72 s here (measured: 0.723 s between consecutive domain give-ups
			 * in trial 402), short of the one-second budget the vendor allows. On a domain
			 * that needs longer than 0.72 s to acknowledge, this loop gives up before the
			 * hardware would have answered, and the caller then clears isolation and raises
			 * reset on a domain that is still mid-handshake.
			 */
			if (gMaliDomainWait) {
				delay(10);
			}
		}
	}
	v = reg_rd(gSpm, at);
	/*
	 * An acknowledge that never comes is still not a reason to stop the sequence.
	 *
	 * The vendor aborts here (goto err_pwr_ack), and if it aborted on this part the GPU would
	 * never come up at all: PWR_STATUS at 0xF34 has read 0x1fc on runs where mfg5 and mfg6
	 * both reported no acknowledge, and 0x1fc is seven domains up (bits 2..8). Whatever the
	 * control words say, the rails behind them are live. So the sequence continues -- but it
	 * now says so with the status it actually saw instead of calling the result "on".
	 */
	if ((v & SPM_STA_MASK) != SPM_STA_MASK) {
		printf("mali: %s no ack after %u reads (%#x), continuing anyway\n",
		    what, spins, v);
	}
	v &= ~SPM_PWR_CLK_DIS;
	reg_wr(gSpm, at, v);
	v &= ~SPM_PWR_ISO;
	reg_wr(gSpm, at, v);
	v |= SPM_PWR_RST_B;
	reg_wr(gSpm, at, v);
	/*
	 * Every one of the twelve domains carries its own SRAM, mfg1 and mfg2 included.
	 *
	 * This used to be guarded by "at >= MALI_MFG3", on the reasoning that only the shader
	 * domains had SRAM behind them. The vendor's table for this very part says otherwise: all
	 * twelve of MT6895_POWER_DOMAIN_MFG1..MFG12 carry sram_pdn_bits GENMASK(8, 8) and
	 * sram_pdn_ack_bits GENMASK(12, 12) (mtk-scpsys-mt6895.c:450-560), and scpsys_power_on()
	 * calls scpsys_sram_enable() for every domain it raises, with no such test.
	 *
	 * The reading that exposed it: mfg1 came back as 0xc000110d and mfg2 as 0xc000110d, while
	 * mfg3 onwards came back as 0xc000000d. The difference is bit 8 and bit 12 -- SRAM power-down
	 * still asserted, SRAM acknowledge still high -- on exactly the two domains the guard
	 * skipped. mfg1 is the domain the Cortex-M7 lives in. An M7 whose SRAM is powered down
	 * fetches nothing, writes nothing, and raises nothing: the sentinel word stays untouched and
	 * no fault is ever latched, which is the state every run so far has reported.
	 */
	v = reg_rd(gSpm, at);
	v &= ~SPM_SRAM_PDN;
	reg_wr(gSpm, at, v);
	/*
	 * scpsys_sram_on() (mtk-scpsys.c:219-259) polls the same bit with the same 10 us / 1 s
	 * budget, and returns an error if the acknowledge stays high. This loop used to burn two
	 * million back-to-back reads instead. The reading that matters is the one after it, and
	 * bit 12 still set means the SRAM behind that domain is physically powered down -- which
	 * for a shader stack means the core there has no instruction memory to fetch from.
	 */
	{
		/* Same two budgets as the acknowledge poll above; see the comment there. */
		unsigned limit = gMaliDomainWait ? 100000u : 2000000u;

		for (spins = 0; spins < limit; spins++) {
			if ((reg_rd(gSpm, at) & SPM_SRAM_ACK) == 0) {
				break;
			}
			if (gMaliDomainWait) {
				delay(10);
			}
		}
	}
	/*
	 * "on" now means what the vendor means by it. The status field is GENMASK(31, 30) on all
	 * twelve MFG domains (mtk-scpsys-mt6895.c:449-556) and both bits have to be set. Every
	 * run before this one printed "on:" for three different shapes -- 0xc000000d (both set,
	 * really on), 0x4000000d (one set, mid-handshake) and 0xd (neither set, off) -- because
	 * the print had no test in it. Those three shapes are exactly the three answers
	 * scpsys_pwr_con_is_on() returns, and only the first one is a powered domain.
	 */
	{
		uint32_t now = reg_rd(gSpm, at);

		if ((now & SPM_STA_MASK) == SPM_STA_MASK && !(now & SPM_SRAM_PDN) &&
		    !(now & SPM_SRAM_ACK)) {
			printf("mali: %s on: %#x\n", what, now);
		} else {
			printf("mali: %s did NOT come up: %#x (sta %#x, sram pdn %u ack %u)\n",
			    what, now, (now & SPM_STA_MASK) >> 30,
			    (now & SPM_SRAM_PDN) ? 1u : 0u, (now & SPM_SRAM_ACK) ? 1u : 0u);
		}
	}
	return true;
}

/*
 * The AOC handshake and the SRAM isolation, which is step one of the vendor's nine.
 *
 * Written from scratch here because until now the loader did it before jumping into this kernel,
 * which means the kernel has never actually run the vendor's POWER_ON from the top. The register
 * pair is SOC_BUCK_ISO_CON 0x1C001F30: bit 9 drives the isolation into the GPU's SRAM, bit 10
 * holds the latch that keeps it there. Releasing them in the order the latch is dropped first
 * then the isolation is what makes a powered GPU one whose instruction memory can be read;
 * doing it the other way round leaves the latch holding whatever the isolation had on the bus.
 *
 * SPM_SEMA_M4 bit 0 at 0x1C0016AC is a hardware semaphore shared with the always-on companion
 * (AOC). The vendor spins on it unbounded -- the comment on their side is that AOC never sleeps
 * so the bit returns in a few cycles. It is bounded here: a loader or kernel that hangs before it
 * has printed anything is a boot loop with no evidence in it.
 */
static void
mali_aoc_up(void)
{
	uint32_t v;
	unsigned spins;

	for (spins = 0; spins < 100000u; spins++) {
		/*
		 * Read-only, and deliberately so.
		 *
		 * The vendor requests this semaphore with
		 *   do { val = readl(SPM_SEMA_M4); val |= 1; writel(val, SPM_SEMA_M4); }
		 *   while ((readl(SPM_SEMA_M4) & 1) != 1);
		 * (gpufreq_mt6895.c:3244-3252). Adding that write here is what turned a tree that
		 * reached Android into one that reset in the preloader, so it is behind mali_sema=1
		 * and off by default: one variable at a time, and the last known-good shape wins.
		 */
		if (gMaliSema) {
			reg_wr(gSpm, MALI_SPM_SEMA_M4, reg_rd(gSpm, MALI_SPM_SEMA_M4) | 1u);
		}
		if ((reg_rd(gSpm, MALI_SPM_SEMA_M4) & 1u) == 1u) {
			break;
		}
	}
	v = reg_rd(gSpm, MALI_SPM_BUCK_ISO);
	printf("mali: aoc: semaphore %#x after %u spins, iso_con %#x\n",
	    reg_rd(gSpm, MALI_SPM_SEMA_M4), spins, v);

	/* AOCISO -> AOCLHENB: the isolation goes out first, then the latch that holds it. */
	v &= ~MALI_AOC_VGPU_SRAM_ISO;
	reg_wr(gSpm, MALI_SPM_BUCK_ISO, v);
	v = reg_rd(gSpm, MALI_SPM_BUCK_ISO);
	v &= ~MALI_AOC_VGPU_LATCH;
	reg_wr(gSpm, MALI_SPM_BUCK_ISO, v);
	__asm__ volatile ("dsb sy" ::: "memory");

	reg_wr(gSpm, MALI_SPM_SEMA_M4, reg_rd(gSpm, MALI_SPM_SEMA_M4) | 1u);
	__asm__ volatile ("dsb sy" ::: "memory");
	printf("mali: aoc: released, iso_con now %#x\n", reg_rd(gSpm, MALI_SPM_BUCK_ISO));
}

static unsigned pdc_enable(void);
static void pdc_report(void);

/*
 * The twelve domains, parents before children (mtk-scpsys-mt6895.c's dependency table). The
 * vendor's own path powers only MFG1 by hand -- PDC takes the rest once the GPU asks -- so a
 * failure here is not fatal as long as MFG1 and MFG2 answered; the block is checked afterwards by
 * reading GPU_ID, which is the only proof that matters.
 */
static unsigned
power_on(void)
{
	static const struct {
		uint32_t at;
		const char *what;
	} tree[] = {
		{ MALI_MFG1,  "mfg1  MPU" },  { MALI_MFG2,  "mfg2  L2" },
		{ MALI_MFG3,  "mfg3  ST0" },  { MALI_MFG4,  "mfg4  ST1" },
		{ MALI_MFG5,  "mfg5  ST4" },  { MALI_MFG6,  "mfg6  ST5" },
		{ MALI_MFG7,  "mfg7  ST0T0" }, { MALI_MFG8,  "mfg8  ST1T0" },
		{ MALI_MFG9,  "mfg9  ST0T1" }, { MALI_MFG10, "mfg10 ST4T0" },
		{ MALI_MFG11, "mfg11 ST5T0" }, { MALI_MFG12, "mfg12 ST4T1" },
	};
	unsigned i;
	unsigned up = 0;
	unsigned pass;

	/*
	 * mali_pdcauto=1: touch mfg1 only, then hand mfg2..mfg12 to the power controller.
	 *
	 * The vendor does exactly this and nothing more. __gpufreq_mtcmos_control(POWER_ON)
	 * (gpufreq_mt6895.c:3371) calls pm_runtime_get_sync() on g_mtcmos->mfg1_dev alone, and
	 * everything that would raise mfg2..mfg12 by hand sits inside "#if !GPUFREQ_PDCv2_ENABLE"
	 * -- a block that does not compile on this platform, because include/gpufreq_mt6895.h:70
	 * sets GPUFREQ_PDCv2_ENABLE to 1. What remains is one call to __gpufreq_pdc_control(),
	 * which sets the MFG_ACTIVE_POWER_CON bits and lets the controller bring the rest up.
	 *
	 * This driver has always done the opposite: twelve domains written one at a time, and the
	 * result has been identical every run -- seven answer, five do not, and the control words
	 * of the five sit at 0x100d with the SRAM acknowledge still asserted. Writing a domain's
	 * control word while the power controller owns that domain is not a race the driver can
	 * win; at best it is redundant, at worst the two disagree about who may release the rail.
	 *
	 * The status register the vendor checks is also not the one this driver has been reading.
	 * PWR_STATUS_OFS is 0xF3C (include/gpufreq_mt6895.h:72) with MFG_1_12_PWR_MASK 0x3FFC, so
	 * bit (n+2) of 0xF3C is MFGn. This driver reads 0xF34/0xF38 and calls the low nine bits
	 * "seven domains"; the vendor only ever reads 0xF3C.
	 */
	if (gMaliPdcAuto) {
		/*
		 * mali_pdcauto=1 must not run pdc_enable() here: this function is called before
		 * clock_26m(), and mfgcfg lives inside mfg1 -- that is, on the far side of a gate
		 * clock_26m() is the only thing that opens. The first run of this branch proved it:
		 * all seventeen PDC stores read back zero (pdc: sc0 0 sc9 0 cg 0 st0 0 st4 0 st6 0)
		 * and 0xF3C came back 0x6 instead of 0x3FFE.
		 */
		(void)domain_on(MALI_MFG1, "mfg1  MPU");
		if ((reg_rd(gSpm, MALI_MFG1) & SPM_STA_MASK) != SPM_STA_MASK) {
			printf("mali: mfg1 did not come up; PDC cannot take over\n");
			return 0;
		}
		printf("mali: pdc auto: mfg1 up (PDC deferred until after clock_26m)\n");
		/* The controller is hardware, not a register write; give it 200 ms. */
		delay(200000);
		printf("mali: pdc auto: 0xF3C %#x (want 0x3ffe) 0xF40 %#x 0xF34 %#x\n",
		    reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF) & 0x3ffcu,
		    reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF + 4u) & 0x3ffcu,
		    reg_rd(gSpm, MALI_SPM_PWR_STATUS) & MALI_MFG_PWR_MASK);
		/*
		 * Report what scpsys's own register says mfg1 did, and let the bring-up continue.
		 *
		 * The PDC verdict is printed after clock_26m(); this branch only decides that mfg1 is
		 * up and that the caller should not walk mfg2..mfg12 by hand.
		 */
		for (i = 0; i < 12u; i++) {
			if (reg_rd(gSpm, MALI_SPM_PWR_STATUS) & (1u << (2 + i))) {
				up++;
			}
		}
		printf("mali: pdc auto: %u of twelve answered by scpsys before PDC\n", up);
		return up;
	}

	/*
	 * Twice, because the second level cannot come up in the same sweep as its parent.
	 *
	 * The vendor's dependency table (mtk-scpsys-mt6895.c:590-595) makes mfg7 a child of mfg3,
	 * mfg8 of mfg4, mfg9 of mfg7, mfg10 of mfg5, mfg11 of mfg6 and mfg12 of mfg10. One pass in
	 * table order therefore asks the children to come up in the same breath as the parents
	 * they are waiting for, and the run before this one showed exactly that: the first seven
	 * domains reached PWR_STATUS bits 2..8 while mfg8..mfg12 stayed off, and their control
	 * words sat at 0x100d -- request accepted, reset released, rail still down.
	 *
	 * The second pass now asks only the domains that failed, and that is not a micro-
	 * optimisation: with the vendor's poll budget each failure costs a full second, so asking
	 * all twelve twice unconditionally would spend most of a minute waiting on domains that
	 * had already answered. 'up' is still read back from PWR_STATUS at the end, because the
	 * control words are not proof of anything -- that is the lesson of the last three runs.
	 */
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < sizeof(tree) / sizeof(tree[0]); i++) {
			uint32_t at = tree[i].at;

			(void)domain_on(at, tree[i].what);
		}
	}
	for (i = 0; i < sizeof(tree) / sizeof(tree[0]); i++) {
		if (reg_rd(gSpm, MALI_SPM_PWR_STATUS) & (1u << (2 + i))) {
			up++;
		}
	}
	return up;
}

/*
 * Hand the shader stacks to PDC, which is the only thing on this SoC that can actually raise them.
 *
 * Called after power_on() and before the MCU is started, the same slot the vendor puts it in
 * (step seven of nine, gpufreq_mt6895.c:802 for on and :807 for off). The domains that power_on()
 * failed to acknowledge are the ones this is for: mfg5..mfg12 are wired to the GPU's own power
 * controller, not to software, and until the enables below are set that controller never asks.
 *
 * Only bit 0 of each register is written, read-modify-write, because neighbouring bits in the
 * same word belong to other blocks and are not ours to clear.
 */
static unsigned
pdc_enable(void)
{
	unsigned i;
	unsigned written = 0;

	for (i = 0; i < MALI_PDC_CORES; i++) {
		uint32_t at = MALI_PDC_SC_BASE + i * MALI_PDC_SC_STEP;

		reg_wr(gMfgcfg, at, reg_rd(gMfgcfg, at) | MALI_PDC_SC_EN);
		reg_wr(gMfgcfg, at + MALI_PDC_SC_RSV,
		    reg_rd(gMfgcfg, at + MALI_PDC_SC_RSV) | MALI_PDC_SC_RSV_BIT);
		written++;
	}
	for (i = 0; i < sizeof(kPdcGroups) / sizeof(kPdcGroups[0]); i++) {
		uint32_t at = kPdcGroups[i];

		reg_wr(gMfgcfg, at, reg_rd(gMfgcfg, at) | MALI_PDC_GROUP_EN);
		written++;
	}
	return written;
}

/* What PDC actually took: bit 0 of the first and last cluster, plus the core-group block. A zero
 * here means the writes never landed, which is a different failure from a controller that took
 * them and still did not raise anything. */
static void
pdc_report(void)
{
	printf("mali: pdc: sc0 %#x sc9 %#x cg %#x st0 %#x st4 %#x st6 %#x\n",
	    reg_rd(gMfgcfg, MALI_PDC_SC_BASE),
	    reg_rd(gMfgcfg, MALI_PDC_SC_BASE + 9 * MALI_PDC_SC_STEP),
	    reg_rd(gMfgcfg, 0x100), reg_rd(gMfgcfg, 0x120),
	    reg_rd(gMfgcfg, 0xc0), reg_rd(gMfgcfg, 0x1c0));
}

/* ------------------------------------------------------------------ mfg1's AXI protections */

/*
 * Hand the GPU its bus back.
 *
 * Unconditional on purpose. The assertions are left by whatever ran before this kernel -- the
 * loader in the fastboot path, the bootloader before that -- and they do not follow the power
 * domains: mfg1 came up on every run and the protections were still there, read back as exactly
 * the four masks below. Tying this to the cold-start branch would have skipped it on the two runs
 * where the loader had already powered the GPU, which are the two runs we care about.
 *
 * Read first and print, so a run tells us what it found rather than only that it wrote something.
 * Eight words, no polling: BUS_PROT_IGN's whole point upstream is that this SoC never
 * acknowledges the clear, and mtk-scpsys.c:409 returns before the status read.
 */
static void
bus_protect_release(void)
{
	static const struct {
		uint64_t at;
		uint32_t mask;
		const char *what;
	} kProt[] = {
		{ MALI_BP_MD0_MFG1,     MALI_BP_MD0_MFG1_MASK,     "top axi md0" },
		{ MALI_BP_MFGRPC_MFG1,  MALI_BP_MFGRPC_MFG1_MASK,  "mfgrpc" },
		{ MALI_BP_EMISYS0_MFG1, MALI_BP_EMISYS0_MFG1_MASK, "emisys0" },
		{ MALI_BP_EMISYS1_MFG1, MALI_BP_EMISYS1_MFG1_MASK, "emisys1" },
	};
	unsigned i;

	for (i = 0; i < sizeof(kProt) / sizeof(kProt[0]); i++) {
		vm_offset_t block;
		uint32_t off;
		uint32_t before;

		if (kProt[i].at >= MALI_MFGRPC_PHYS && kProt[i].at < MALI_MFGRPC_PHYS + MALI_MFGRPC_SIZE) {
			block = gMfgrpc;
			off = (uint32_t)(kProt[i].at - MALI_MFGRPC_PHYS);
		} else {
			block = gInfracfg;
			off = (uint32_t)(kProt[i].at - MALI_INFRACFG_PHYS);
		}
		if (block == 0) {
			printf("mali: bus protect %s: no mapping\n", kProt[i].what);
			continue;
		}
		before = reg_rd(block, off);
		reg_wr(block, off, kProt[i].mask);
		printf("mali: bus protect %s %#x -> %#x (mask %#x)\n",
		    kProt[i].what, before, reg_rd(block, off), kProt[i].mask);
	}

	/* The CLR register readback above is the first thing that looked odd: it answers 0xf for
	 * mfgrpc both before and after the write, so it tells us nothing about whether the
	 * protection actually dropped. mtk-scpsys.c:377-430 shows the real layout --
	 * (set_ofs, clr_ofs, en_ofs, sta_ofs) -- and mtk-scpsys-mt6983.c:438 gives the mfgrpc
	 * tuple as (0x1040, 0x1044, 0x103C, 0x1048). The status word is the one that matters:
	 * clear_bus_protection() polls !(val & mask) on it, except BUS_PROT_IGN skips the poll
	 * entirely, which is exactly why nobody ever looked. infracfg_ao is a known-readable
	 * window (the four clears above live in it), so reading its status costs nothing. */
	if (gInfracfg != 0) {
		printf("mali: bus protect sta: md0 en %#x sta %#x (mask %#x)\n",
		    reg_rd(gInfracfg, MALI_BP_MD0_MFG1_EN_OFF),
		    reg_rd(gInfracfg, MALI_BP_MD0_MFG1_STA_OFF), MALI_BP_MD0_MFG1_MASK);
		printf("mali: bus protect sta: emisys0 en %#x sta %#x (mask %#x)\n",
		    reg_rd(gInfracfg, MALI_BP_EMISYS0_EN_OFF),
		    reg_rd(gInfracfg, MALI_BP_EMISYS0_STA_OFF), MALI_BP_EMISYS0_MFG1_MASK);
		printf("mali: bus protect sta: emisys1 en %#x sta %#x (mask %#x)\n",
		    reg_rd(gInfracfg, MALI_BP_EMISYS1_EN_OFF),
		    reg_rd(gInfracfg, MALI_BP_EMISYS1_STA_OFF), MALI_BP_EMISYS1_MFG1_MASK);
	}
	/*
	 * The verification, inline.
	 *
	 * Every round so far writes CLR and moves on: BUS_PROT_IGN means "never acknowledge", so
	 * mtk-scpsys.c:409 returns before reading the status word and a clear that landed and a
	 * clear that did nothing look identical. Poll it here instead and print the answer. This
	 * lives inside bus_protect_release() rather than in its own function because the separate
	 * version got compiled but never ran.
	 */
	if (gInfracfg != 0) {
		static const struct {
			uint32_t clr_off, sta_off, mask;
			const char *what;
		} kPoll[] = {
			{ 0x0ca8u, 0x0cacu, 0x0000000fu, "md0" },
			{ 0x0c68u, 0x0c6cu, 0x000c0000u, "emisys0" },
			{ 0x0c78u, 0x0c7cu, 0x000c0000u, "emisys1" },
		};
		unsigned q;

		for (q = 0; q < sizeof(kPoll) / sizeof(kPoll[0]); q++) {
			uint32_t s0 = reg_rd(gInfracfg, kPoll[q].sta_off);
			uint32_t s1;
			unsigned spins;

			printf("mali: bp raw %s: en %#x set %#x clr %#x sta %#x\n",
			    kPoll[q].what,
			    reg_rd(gInfracfg, 0x0000u + (q == 0 ? 0x0ca0u : (q == 1 ? 0x0c60u : 0x0c70u))),
			    reg_rd(gInfracfg, q == 0 ? 0x0ca4u : (q == 1 ? 0x0c64u : 0x0c74u)),
			    reg_rd(gInfracfg, kPoll[q].clr_off),
			    s0);
			reg_wr(gInfracfg, kPoll[q].clr_off, kPoll[q].mask);
			__asm__ volatile ("dsb sy" ::: "memory");
			for (spins = 0; spins < 10000u; spins++) {
				s1 = reg_rd(gInfracfg, kPoll[q].sta_off);
				if ((s1 & kPoll[q].mask) == 0u) {
					break;
				}
				delay(10);
			}
			s1 = reg_rd(gInfracfg, kPoll[q].sta_off);
			printf("mali: bpverify %s: sta %#x -> %#x after %u polls (%s)\n",
			    kPoll[q].what, s0, s1, spins,
			    (s1 & kPoll[q].mask) == 0u ? "OPENED" : "STILL SET -- clear did not land");
			/*
			 * Second route, and only for the group that refused the first one.
			 *
			 * scpsys takes this fork whenever a descriptor has no set_ofs
			 * (mtk-scpsys.c:386-389 / :412-415): touch the enable word instead of the clear
			 * word. The phone's own reading for this register is 0x0 (it is
			 * MD_MFGSYS_PROTECT_EN_0, clkchk-mt6895.c:177), so setting the bits is not the
			 * goal -- the goal is to find out whether AP writes to this word land at all.
			 * If the value does not change even briefly, the word is not ours to write.
			 */
			if ((s1 & kPoll[q].mask) != 0u) {
				uint32_t enow = reg_rd(gInfracfg,
				    q == 0 ? 0x0ca0u : (q == 1 ? 0x0c60u : 0x0c70u));

				reg_wr(gInfracfg,
				    q == 0 ? 0x0ca0u : (q == 1 ? 0x0c60u : 0x0c70u),
				    enow | kPoll[q].mask);
				__asm__ volatile ("dsb sy" ::: "memory");
				printf("mali: bpverify %s: EN route %#x -> %#x, sta now %#x (%s)\n",
				    kPoll[q].what, enow,
				    reg_rd(gInfracfg,
					q == 0 ? 0x0ca0u : (q == 1 ? 0x0c60u : 0x0c70u)),
				    reg_rd(gInfracfg, kPoll[q].sta_off),
				    (reg_rd(gInfracfg, kPoll[q].sta_off) & kPoll[q].mask) == 0u
					? "OPENED" : "still set");
			}
		}
	}
	if (gMfgrpc != 0) {
		/* 0x13F91044 (the CLR word, read back above) answers 0xf on every run, so that one
		 * sub-block is decoded and clocked. 0x13F91034 is not -- touching it hangs the bus,
		 * twice, and it is only sixteen bytes from the status word below. The two facts are
		 * not contradictory in principle (a free-run control can bite without being
		 * undecoded), but a guess costs a whole flash cycle, so the mfgrpc status read waits
		 * behind its own switch: mali_rpc=1 turns it on. */
		if (gMaliRpc != 0) {
			printf("mali: bus protect sta: mfgrpc en %#x sta %#x (mask %#x)\n",
			    reg_rd(gMfgrpc, MALI_BP_MFGRPC_EN_OFF),
			    reg_rd(gMfgrpc, MALI_BP_MFGRPC_STA_OFF), MALI_BP_MFGRPC_MFG1_MASK);
		} else {
			printf("mali: bus protect sta: mfgrpc not read (mali_rpc=0)\n");
		}
	}
}

/*
 * mali_bpverify=1: release the bus protection and then actually confirm it.
 *
 * Every round so far has written the four CLR words and moved on, because BUS_PROT_IGN means
 * "this SoC never acknowledges the clear" and mtk-scpsys.c:409 returns before the status read.
 * But a silently-ignored clear and a clear that landed look identical when nobody looks:
 *
 *	md0 sta 0xffffffe0  before mfg1   (bits 0..3 = 0)
 *	md0 sta 0xffffffef  after  CLR    (bits 0..3 = 1)
 *
 * and set_bus_protection() polls for (val & mask) == mask while clear_bus_protection() polls for
 * !(val & mask), so bit=1 means the port is protected. On that reading the clear never landed.
 *
 * That conclusion is drawn for md0 only. On the same phone dump emisys0/1 read bits 18/19 = 1
 * (protected) and that phone's GPU works, so "sta bit 1 means blocked" does not generalise to
 * every port group. md0 is the only group where the phone and this board disagree
 * (phone 0xCAC = 0xffffffe0, bits 0..3 = 0; this board = 0xffffffef, bits 0..3 = 1).
 *
 * This routine does it the way the vendor's own helpers do: make sure the enable bit is set,
 * write CLR, then poll STA until the mask reads zero, printing every step. If it does not
 * converge, that is the answer -- and it would explain the witness result (a core that cannot
 * fetch a single instruction) better than any firmware-side theory.
 */
static void
bus_protect_release_verified(void)
{
	static const struct {
		uint32_t set_off, clr_off, en_off, sta_off, mask;
		const char *what;
	} kProt[] = {
		{ 0x0ca4u, 0x0ca8u, 0x0ca0u, 0x0cacu, 0x0000000fu, "md0" },
		{ 0x0c64u, 0x0c68u, 0x0c60u, 0x0c6cu, 0x000c0000u, "emisys0" },
		{ 0x0c74u, 0x0c78u, 0x0c70u, 0x0c7cu, 0x000c0000u, "emisys1" },
	};
	unsigned i;

	if (gInfracfg == 0) {
		printf("mali: bpverify: infracfg not mapped\n");
		return;
	}
	for (i = 0; i < sizeof(kProt) / sizeof(kProt[0]); i++) {
		uint32_t en = reg_rd(gInfracfg, kProt[i].en_off);
		uint32_t sta0 = reg_rd(gInfracfg, kProt[i].sta_off);
		uint32_t sta1;
		unsigned spins;

		printf("mali: bpverify %s: set %#x clr %#x en %#x sta %#x (mask %#x)\n",
		    kProt[i].what, reg_rd(gInfracfg, kProt[i].set_off),
		    reg_rd(gInfracfg, kProt[i].clr_off), en, sta0, kProt[i].mask);
		/*
		 * Never pre-assert the enable word.
		 *
		 * SET / CLR / EN are three views of one latch, and the vendor's helpers pick exactly
		 * one of them (mtk-scpsys.c:386-389 and :412-415: use set_ofs if present, else touch
		 * en_ofs -- never both). Writing EN |= mask before CLR would arm a port that is
		 * already open and then hope the CLR disarms it again. emisys0/1 are open on this
		 * board right now (sta 0xff000020, bits 18/19 = 0), so that sequence would put two
		 * good ports at risk to test one whose CLR is the one under suspicion.
		 */
		(void)en;
		reg_wr(gInfracfg, kProt[i].clr_off, kProt[i].mask);
		__asm__ volatile ("dsb sy" ::: "memory");
		for (spins = 0; spins < 100000u; spins++) {
			sta1 = reg_rd(gInfracfg, kProt[i].sta_off);
			if ((sta1 & kProt[i].mask) == 0u) {
				break;
			}
			delay(10);
		}
		sta1 = reg_rd(gInfracfg, kProt[i].sta_off);
		printf("mali: bpverify %s: sta %#x -> %#x after %u spins (%s)\n",
		    kProt[i].what, sta0, sta1, spins,
		    (sta1 & kProt[i].mask) == 0u ? "CLEARED" : "STILL SET -- the clear did not land");
	}
}

/* The status word, on its own, so it can be read again later.
 *
 * bus_protect_release() runs at the very top of the bring-up, and the log ring on this board only
 * keeps the last few hundred lines -- the first run with these prints came back with no
 * "bus protect" line in it at all, only the two lines the loader writes at shutdown. Reading the
 * same four words again just before the MCU is started costs four MMIO reads and puts them where
 * the ring cannot lose them. */
static void
bus_protect_sta(const char *when)
{
	if (gInfracfg != 0) {
		printf("mali: bus protect sta %s: md0 en %#x sta %#x, emisys0 en %#x sta %#x, "
		    "emisys1 en %#x sta %#x\n", when,
		    reg_rd(gInfracfg, MALI_BP_MD0_MFG1_EN_OFF),
		    reg_rd(gInfracfg, MALI_BP_MD0_MFG1_STA_OFF),
		    reg_rd(gInfracfg, MALI_BP_EMISYS0_EN_OFF),
		    reg_rd(gInfracfg, MALI_BP_EMISYS0_STA_OFF),
		    reg_rd(gInfracfg, MALI_BP_EMISYS1_EN_OFF),
		    reg_rd(gInfracfg, MALI_BP_EMISYS1_STA_OFF));
	}
	if (gMfgrpc != 0 && gMaliRpc != 0) {
		printf("mali: bus protect sta %s: mfgrpc en %#x sta %#x\n", when,
		    reg_rd(gMfgrpc, MALI_BP_MFGRPC_EN_OFF),
		    reg_rd(gMfgrpc, MALI_BP_MFGRPC_STA_OFF));
	}
}

/* Put the GPU's clock on the 26 MHz crystal: the crystal runs for the whole SoC, so this cannot
 * leave the block without a clock, and it is enough to make the registers answer. */
static void
clock_26m(void)
{
	reg_wr(gTopckgen, MALI_CLK_CFG_4_CLR, 1u << 31);        /* ungate mfg_ref_sel itself */
	reg_wr(gTopckgen, MALI_CLK_CFG_4_CLR, MALI_MFG_REF_MASK);
	reg_wr(gTopckgen, MALI_CLK_CFG_4_SET, MALI_MFG_REF_26M);
	reg_wr(gTopckgen, MALI_CLK_UPDATE, 1u << MALI_MFG_REF_SHIFT);
	delay(2);
	reg_wr(gTopckgen, MALI_CLK_CFG_30_CLR, MALI_MFG_SEL_0_BIT);   /* 0 = mfg_ref_ck */
	delay(2);
	reg_wr(gMfgcfg, MALI_MFGCFG_CLR, MALI_MFGCFG_BG3D);
	delay(2);
	printf("mali: clock: cfg4 %#x cfg30 %#x mfgcfg %#x\n", reg_rd(gTopckgen, MALI_CLK_CFG_4),
	    reg_rd(gTopckgen, MALI_CLK_CFG_30), reg_rd(gMfgcfg, MALI_MFGCFG_STA));
}

/* MFGPLL to 300 MHz, then move mfg_sel_0 onto it. 26 MHz is enough to prove the block answers but
 * nowhere near enough to run firmware that has to execute shader programs. */
static void
pll_300mhz(void)
{
	uint32_t v;

	reg_wr(gMfgpll, MALI_MFGPLL_CON3, MALI_PLL_PWR_ON);
	delay(2);
	v = reg_rd(gMfgpll, MALI_MFGPLL_CON3);
	reg_wr(gMfgpll, MALI_MFGPLL_CON3, v & ~MALI_PLL_ISO_EN);
	delay(2);
	reg_wr(gMfgpll, MALI_MFGPLL_CON1,
	    MALI_PLL_PCW_CHG | MALI_PLL_POSDIV_8 | MALI_PLL_PCW_300MHZ);
	v = reg_rd(gMfgpll, MALI_MFGPLL_CON0);
	reg_wr(gMfgpll, MALI_MFGPLL_CON0, v | MALI_PLL_EN);
	delay(50);
	reg_wr(gTopckgen, MALI_CLK_CFG_30_SET, MALI_MFG_SEL_0_BIT);
	delay(2);

	/*
	 * The second selector, which this driver had never touched.
	 *
	 * The vendor's power-on enables both muxes -- clk_mux for the GPU and clk_sc_mux for the
	 * shader stacks -- and then switches both to CLOCK_MAIN. The phone's own clock summary reads
	 * mfg_sel_0_ck at 299999955 and mfg_sel_1_ck at 218400000, which is mainpll_d5_d2, so the
	 * second one is not left on the crystal. Selector 2 is that parent in CLK_CFG_5 bits 1:0.
	 * The vendor mux has UPDATE bit20; the historical baseline did not commit it.
	 *
	 * Whether the MCU fetches through this mux is not established. It is set because the only
	 * working configuration in evidence sets it, and because a mux left on a stopped parent is
	 * exactly the kind of silent stall this search keeps meeting.
	 */
	reg_wr(gTopckgen, MALI_CLK_CFG_5_CLR, MALI_MFG_SEL_1_MASK);
	reg_wr(gTopckgen, MALI_CLK_CFG_5_SET, 2u);
	/*
	 * mali_scclk=1: clear the power-down bit of mfgsc_ref_sel.
	 *
	 * The vendor registers this mux with MUX_GATE_CLR_SET_UPD (clk-mt6895.c:2279-2283) and a
	 * pdn bit at 7, while mfg_ref_sel one register over is a plain MUX_CLR_SET_UPD with no gate
	 * (clk-mt6895.c:1917-1921). That difference matters: mfg_ref_sel is the GPU's own clock and
	 * costs nothing to leave on, but mfgsc_ref_sel -- the second, shader-side selector -- sits
	 * behind a gate that nothing in this driver has ever cleared.
	 *
	 * The board reads CLK_CFG_5 = 0x01010182 before this runs: selector 2 is already set, and
	 * bit 7 is 1. Under MUX_GATE semantics that is a mux whose parent is selected and whose
	 * output is gated off, which is precisely the shape of "the MCU's state machine answers but
	 * no instruction is ever fetched".
	 */
	if (gMaliScClk || gMaliScUpd) {
		if (gMaliScUpd && gMaliSc26) {
			reg_wr(gTopckgen, MALI_CLK_CFG_5_CLR, MALI_MFG_SEL_1_MASK);
		}
		reg_wr(gTopckgen, MALI_CLK_CFG_5_CLR, 1u << 7);
		if (gMaliScUpd) {
			/* clk-mux.c:54-63 commits the restored mux after clearing its gate. */
			reg_wr(gTopckgen, MALI_CLK_UPDATE, 1u << MALI_MFGSC_REF_SHIFT);
		}
		delay(2);
		printf("mali: scclk: cfg5 %#x after un-gating bit7\n",
		    reg_rd(gTopckgen, MALI_CLK_CFG_5));
		printf("mali: sc mux trial %u: update20 %u parent %u gate %u cfg5 %#x\n",
		    gMaliTrial, gMaliScUpd ? 1u : 0u,
		    reg_rd(gTopckgen, MALI_CLK_CFG_5) & MALI_MFG_SEL_1_MASK,
		    (reg_rd(gTopckgen, MALI_CLK_CFG_5) >> 7) & 1u,
		    reg_rd(gTopckgen, MALI_CLK_CFG_5));
	}
	delay(2);

	printf("mali: pll: con0 %#x con1 %#x con3 %#x cfg5 %#x cfg30 %#x\n",
	    reg_rd(gMfgpll, MALI_MFGPLL_CON0), reg_rd(gMfgpll, MALI_MFGPLL_CON1),
	    reg_rd(gMfgpll, MALI_MFGPLL_CON3), reg_rd(gTopckgen, MALI_CLK_CFG_5),
	    reg_rd(gTopckgen, MALI_CLK_CFG_30));
}

/*
 * The block's own bus-idle gate, which nothing in this driver had ever touched.
 *
 * gpufreq_mt6895.c:1249-1272 __gpufreq_check_bus_idle() does three things and then spins:
 *
 *	0x13FBF0B4 bit 0 = 1		MFG_ACTIVE_SEL, so the debug channel watches the live bus
 *	0x13FBF170 bits 1:0 = 0b11	MFG_DEBUG_TOP_SEL, so MFG_DEBUG_TOP reports that channel
 *	0x13FBF178 bit 0		read: 0 bus idle, 1 bus busy
 *
 * It is called from __gpufreq_dump_infra_status() (twice) and from both sides of
 * __gpufreq_gpm_control(), which is step nine of the vendor's power-on. The vendor reads that
 * status bit as a debug aid rather than a gate, so not doing it is not a missing step. It is
 * worth having here anyway because it answers a question this search cannot otherwise answer:
 * whether the MFG subsystem's AXI channel is idle at the moment the MCU is asked to fetch. An
 * MCU that cannot fetch through an AXI port that never goes idle would show exactly the signs
 * this board shows -- ENABLED, no fault, no fetch -- and every other explanation for those signs
 * has been eliminated.
 *
 * Bounded, unlike the vendor's loop: a hang here is a board that has to be reflashed by hand,
 * and a status bit that never clears is worth a line of log rather than a dead phone.
 */
static void
mfg_bus_idle(const char *when)
{
	uint32_t v;
	unsigned spins;

	v = reg_rd(gMfgcfg, MALI_MFG_QCHANNEL_CON);
	reg_wr(gMfgcfg, MALI_MFG_QCHANNEL_CON, v | MALI_MFG_ACTIVE_SEL);
	v = reg_rd(gMfgcfg, MALI_MFG_DEBUG_SEL);
	reg_wr(gMfgcfg, MALI_MFG_DEBUG_SEL, v | MALI_MFG_DEBUG_TOP_SEL);
	delay(2);

	for (spins = 0; spins < 100000u; spins++) {
		if ((reg_rd(gMfgcfg, MALI_MFG_DEBUG_TOP) & MALI_MFG_DEBUG_TOP_BUSY) == 0) {
			break;
		}
		delay(1);
	}
	printf("mali: bus idle %s: qchannel %#x debug_sel %#x debug_top %#x (%s after %u spins)\n",
	    when, reg_rd(gMfgcfg, MALI_MFG_QCHANNEL_CON), reg_rd(gMfgcfg, MALI_MFG_DEBUG_SEL),
	    reg_rd(gMfgcfg, MALI_MFG_DEBUG_TOP),
	    (reg_rd(gMfgcfg, MALI_MFG_DEBUG_TOP) & MALI_MFG_DEBUG_TOP_BUSY) ? "still busy" : "idle",
	    spins);
}

/*
 * The same observation, but read four times a millisecond apart and without waiting.
 *
 * The first run that sampled the channel got 0x4 before the table walk, 0x4 after it, and 0x1b
 * once the core had been told to run. That reads like idle/busy, and it may be exactly that --
 * but two different words are not proof of a flag, because bits 2, 3 and 4 moved as well and the
 * vendor only ever looks at bit 0. A debug bus that is really a multiplexed status word would
 * change on its own with nothing running, and this function is what tells the two apart: a busy
 * bit is stable across a millisecond, a multiplexed debug value is not.
 *
 * It also catches the case that matters more. If the word moves while the MCU is disabled, then
 * whatever is driving that channel is not the core, and every conclusion drawn from "the bus went
 * busy when we started it" would be drawn from a coincidence.
 */
static uint32_t
mfg_debug_top(void)
{
	return reg_rd(gMfgcfg, MALI_MFG_DEBUG_TOP);
}

/*
 * Is the GPUEB coprocessor still running?
 *
 * Two reads of its watchdog kick register, one millisecond apart. The firmware rewrites that word
 * continuously while it is alive, so a difference is proof it is still executing and a match is
 * strong evidence it is not. The interrupt raw status is read alongside because a live coprocessor
 * that is talking to the AP leaves bits set in it.
 *
 * This matters here for one specific reason: if GPUEB survives the jump to XNU and keeps driving
 * the GPU's power and clock sequencing, then it owns the MFG bus. That would explain, in one
 * stroke, the two things this driver cannot otherwise reconcile -- a bus that reads non-idle from
 * the instant the MCU is enabled and never falls back, and a core that fetches nothing at all
 * while every register the AP can see looks correct.
 */
static void
gpueb_watch(const char *when)
{
	uint32_t a, b, raw, con;

	if (gGpueb == 0) {
		printf("mali: gpueb %s: not mapped\n", when);
		return;
	}
	a = reg_rd(gGpueb, MALI_GPUEB_WDT_KICK);
	delay(1000);
	b = reg_rd(gGpueb, MALI_GPUEB_WDT_KICK);
	raw = reg_rd(gGpueb, MALI_GPUEB_IRQ_RAW);
	con = reg_rd(gGpueb, MALI_GPUEB_WDT_CON);
	printf("mali: gpueb %s: kick %#x -> %#x (%s), irq raw %#x, wdt_con %#x\n",
	    when, a, b, a == b ? "frozen" : "running", raw, con);
	/*
	 * The GPR word the protected-memory-allocator node points at.
	 *
	 * protected-memory-allocator in the device tree (xaga_live.dts) gives gpueb_base
	 * 0x13c40000, gpr_offset 0x1fd1c and gpr_id 6, so the word is at
	 * 0x13c40000 + 0x1fd1c + 6*4 = 0x13c5fd34. The MTK patch that implements the allocator
	 * computes the page table base from it as (value << PAGE_SHIFT) + gmpu_table_size, which
	 * means a non-zero value there is proof the GPU memory protection unit has been given a
	 * table, and zero means it never was. On xaga the table area is me_GPUmputab_PMA at
	 * 0x78c00000, so a programmed word should read 0x78c00 or thereabouts.
	 */
	printf("mali: gpueb %s: gpr[6] %#x (gmpu table %s at %#llx)\n", when,
	    reg_rd(gGpueb, MALI_GPUEB_GPR + 6u * 4u),
	    reg_rd(gGpueb, MALI_GPUEB_GPR + 6u * 4u) != 0 ? "programmed" : "never programmed",
	    (unsigned long long)((uint64_t)reg_rd(gGpueb, MALI_GPUEB_GPR + 6u * 4u) << 12));
}

static void
mfg_bus_watch(const char *when)
{
	uint32_t a, b, c, d;

	a = mfg_debug_top();
	delay(1000);
	b = mfg_debug_top();
	delay(1000);
	c = mfg_debug_top();
	delay(1000);
	d = mfg_debug_top();
	printf("mali: bus watch %s: %#x %#x %#x %#x (busy %u%u%u%u)\n",
	    when, a, b, c, d,
	    a & MALI_MFG_DEBUG_TOP_BUSY, b & MALI_MFG_DEBUG_TOP_BUSY,
	    c & MALI_MFG_DEBUG_TOP_BUSY, d & MALI_MFG_DEBUG_TOP_BUSY);
}

/*
 * The GPU's own memory protection unit.
 *
 * Two nodes in the device tree belong to nothing: g3d_mpu@13bff000 and g3d_brisket@13bfee00. No
 * driver in the vendor tree claims either compatible string, the kernel never touches them, and
 * the reserved-memory node me_GPUmputab_PMA hands the GPU twenty megabytes at 0x78c00000 with
 * no-map -- a page table area for something. A region that exists to hold translations is a
 * region that something consults before a GPU access is allowed to reach DRAM, and neither this
 * driver nor any run has ever read a single register out of it.
 *
 * Whether it gates the MCU is a separate question and one register dump will not settle it, but
 * an all-zero block and a programmed block look completely different and the answer decides
 * whether there is a second MMU in the path at all.
 */
static void
gmpu_probe(const char *when)
{
	vm_offset_t mpu;
	unsigned i;

	mpu = ml_io_map(MALI_G3D_MPU_PHYS, MALI_G3D_MPU_SIZE);
	if (mpu == 0) {
		printf("mali: gmpu %s: the block will not map\n", when);
		return;
	}
	printf("mali: gmpu %s:", when);
	for (i = 0; i < 64u; i++) {
		uint32_t v = reg_rd(mpu, i * 4u);

		if (v != 0u) {
			printf(" %#x:%#x", i * 4u, v);
		}
	}
	printf(" (nonzero words above; the rest read 0)\n");
	/*
	 * g3d_brisket@13bfee00, the 0x200 bytes directly below this block, is not read here.
	 *
	 * It cost a whole run to find out. The first attempt at this function ended with the
	 * g3d_mpu line and nothing after it: the read of 0x13bfee00 never completed, the core
	 * sat waiting on it, and the log stopped at 33.287 seconds with the address space work,
	 * the page table fix and the MCU start all still ahead of it. Reading a gated MTK
	 * peripheral does not fault -- it hangs -- and this one hangs in a register that looks
	 * like it belongs to the GPU.
	 *
	 * The same hazard the GPUEB window has, one page lower, and the same rule applies: it
	 * only gets touched when there is an independently opened clock behind it, and there is
	 * no such clock in this kernel.
	 */
}

/*
 * The other coprocessor block in the MFG window, asked last.
 *
 * The vendor's gpufreq driver maps a "mfg_cpe_control" resource at 0x13FB9C00 and writes
 * 0x0901031f to offset 0 and 0x0000ffff to offset 4 before it reads aging data out of a separate
 * sensor block. Two words, written unconditionally, and nothing here has ever touched them.
 *
 * It is called only from the tail of a failed run, after everything about the GPU has already
 * been printed, because a block in this window that is not decoded behaves like g3d_brisket did:
 * the read never returns and the log simply stops. Losing a run to learn that this address is
 * also undecoded is an acceptable trade; losing the run before the MCU start is not.
 */
static void
cpe_probe(void)
{
	vm_offset_t cpe;

	cpe = ml_io_map(MALI_MFG_CPE_PHYS, MALI_MFG_CPE_SIZE);
	if (cpe == 0) {
		printf("mali: cpe: the block will not map\n");
		return;
	}
	printf("mali: cpe: %#x %#x %#x %#x (ctrlen %s)\n",
	    reg_rd(cpe, 0x00), reg_rd(cpe, 0x04),
	    reg_rd(cpe, 0x08), reg_rd(cpe, 0x0c),
	    (reg_rd(cpe, 0x04) & 0xffffu) != 0 ? "programmed" : "never programmed");
}

/*
 * Open the master-side clock gates on the MFG interconnect.
 *
 * The vendor driver for this part turns on two of the four memory clock gates in MFG_ASYNC_CON
 * and calls it done, and that is the whole reason this went unnoticed: the two it writes are the
 * slave side, and the MCU is a master. The older driver in the same tree writes all four, and
 * names them in its own comments. The two extra bits are 22 and 24.
 *
 * Everything else here comes from the same older function (mt_gpufreq_external_cg_control):
 * QCHANNEL_ENABLE in MFG_QCHANNEL_CON bit 4, and FAXI_CK_SOC_IN_EN in MFG_ASYNC_CON_1 bit 0,
 * the SOC-side input of the AXI bridge the masters go out through.
 *
 * This runs before the firmware is loaded because a gate is not something to open after the
 * first fetch has already been swallowed. The readings before and after are printed: a register
 * that does not hold the bits back is a register behind a shut bus, and that is worth knowing
 * before anything else is blamed on it.
 */
static void
mfg_master_gates(const char *when)
{
	uint32_t dcm, async0, async1, glob, glob_before, qchan, rpc;

	dcm = reg_rd(gMfgcfg, MALI_MFG_DCM_CON_0);
	async0 = reg_rd(gMfgcfg, MALI_MFG_ASYNC_CON);
	async1 = reg_rd(gMfgcfg, MALI_MFG_ASYNC_CON_1);
	glob = reg_rd(gMfgcfg, MALI_MFG_GLOBAL_CON);
	glob_before = glob;
	qchan = reg_rd(gMfgcfg, MALI_MFG_QCHANNEL_CON);
	/*
	 * mfg_rpc is off by default, and that is a result rather than caution.
	 *
	 * Two builds that touch this register both put the board into the three-minute preloader
	 * loop; the build immediately before them differs in nothing else and returns to Android
	 * every time. One of those two builds mapped the wider g3d_config window to reach it, the
	 * other kept the four-kilobyte window and only corrected the offset, so the window size is
	 * not the variable -- reading or writing 0x13F91034 is.
	 *
	 * The neighbouring register in the same page, the mfgrpc bus-protect clear at 0x13F91044, is
	 * read and written a few lines above on every boot without incident, so the page is not
	 * simply unreachable. Something about this one register needs a clock that is not running
	 * when this driver gets to it, and on this part a read of a register behind a stopped clock
	 * does not fault: it parks the AXI transaction and the watchdog takes the board long before
	 * anything can print.
	 *
	 * mali_rpc=2 reads it and does not write; mali_rpc=1 reads and writes. Both are opt-in so
	 * that the default image is the one that boots.
	 */
	rpc = 0;
	if (gMaliRpc) {
		rpc = reg_rd(gMfgrpc, MALI_MFG_RPC_AO_CLK_CFG);
	}

	dcm |= 0x3fu;
	dcm &= ~(1u << 6);
	dcm |= (1u << 15);
	reg_wr(gMfgcfg, MALI_MFG_DCM_CON_0, dcm);

	async0 |= MALI_MFG_MEM0_SLV_CG | MALI_MFG_MEM1_SLV_CG;
	async0 |= MALI_MFG_MEM0_MST_CG | MALI_MFG_MEM1_MST_CG;
	reg_wr(gMfgcfg, MALI_MFG_ASYNC_CON, async0);

	async1 |= MALI_MFG_FAXI_SOC_IN_EN;
	reg_wr(gMfgcfg, MALI_MFG_ASYNC_CON_1, async1);

	glob &= ~(1u << 8);
	glob &= ~(1u << 10);
	glob |= (1u << 13) | (1u << 14) | (1u << 17) | (1u << 18);
	glob &= ~(1u << 21);
	if (gMaliFreeRun) {
		glob |= (1u << 8) | (1u << 9) | (1u << 10) | (1u << 19);
	}
	reg_wr(gMfgcfg, MALI_MFG_GLOBAL_CON, glob);

	qchan |= MALI_MFG_QCHANNEL_EN;
	reg_wr(gMfgcfg, MALI_MFG_QCHANNEL_CON, qchan);

	if (gMaliRpc > 1) {
		rpc &= ~(1u << 0);
		reg_wr(gMfgrpc, MALI_MFG_RPC_AO_CLK_CFG, rpc);
	}

	delay(2);

	printf("mali: interconnect %s: dcm0 %#x->%#x async %#x->%#x async1 %#x->%#x"
	    " global %#x->%#x qchannel %#x->%#x rpc %#x->%#x\n",
	    when,
	    dcm, reg_rd(gMfgcfg, MALI_MFG_DCM_CON_0),
	    async0, reg_rd(gMfgcfg, MALI_MFG_ASYNC_CON),
	    async1, reg_rd(gMfgcfg, MALI_MFG_ASYNC_CON_1),
	    glob_before, reg_rd(gMfgcfg, MALI_MFG_GLOBAL_CON),
	    qchan, reg_rd(gMfgcfg, MALI_MFG_QCHANNEL_CON),
	    rpc, gMaliRpc ? reg_rd(gMfgrpc, MALI_MFG_RPC_AO_CLK_CFG) : 0u);
}

/* ------------------------------------------------------------------ register access */

/*
 * The GPU's coherence port, which the loader has been opening on this driver's behalf.
 *
 * MFG_AXCOHERENCE 0x13FBF168 bit 3 is M3_coherence_enable, and it is step seven of the vendor's
 * nine-step POWER_ON (__gpufreq_acp_control, gpufreq_mt6895.c:3291). The eight gals_* registers
 * below it are coherency attributes for the individual AXI channels. Without this the M7 fetches
 * its instructions through a port that is not coherent with the writes the CPU made to DRAM --
 * coherent reads would still find the data, but the fetches have to be allowed through at all
 * before that question can be asked.
 *
 * Reading the register back matters here: these live in mfg_top_config, the same block whose
 * outage has already cost three boot loops when written while its own clock is stopped, so a
 * value that does not hold is worth printing rather than assuming.
 */
static void
mali_acp_up(void)
{
	static const struct { uint32_t at; uint32_t or; } gals[] = {
		{ 0x8e0u, 0x855u }, { 0x8e8u, 0x855u }, { 0x910u, 0x855u }, { 0x918u, 0x855u },
		{ 0x900u, 0x055u }, { 0x908u, 0x055u }, { 0x920u, 0x055u }, { 0x928u, 0x055u },
	};
	unsigned i;

	reg_wr(gMfgcfg, MALI_MFG_AXCOHERENCE, reg_rd(gMfgcfg, MALI_MFG_AXCOHERENCE) | 0xfu);
	__asm__ volatile ("dsb sy" ::: "memory");
	for (i = 0; i < sizeof(gals) / sizeof(gals[0]); i++) {
		uint32_t v = reg_rd(gMfgcfg, gals[i].at);

		reg_wr(gMfgcfg, gals[i].at, v | gals[i].or);
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	delay(10);
	printf("mali: coherence %#x gals0 %#x gals4 %#x\n",
	    reg_rd(gMfgcfg, MALI_MFG_AXCOHERENCE), reg_rd(gMfgcfg, 0x8e0u),
	    reg_rd(gMfgcfg, 0x900u));
}

/*
 * Arm the initiator-to-memory protector, which has never been programmed on this board.
 *
 * The vendor does four plain writes, waits a microsecond, and then re-writes the first word with
 * bit 0 set. It never reads any of them back and it never checks a status bit, which is the
 * honest shape of the thing: whatever this protector does, it does it without telling anyone.
 * Copying the sequence exactly is the only defensible starting point -- the values are not
 * documented anywhere, and "0x20300316 then 0x20300317" is not a pattern one invents.
 *
 * The readback is added here because it costs one register access and answers whether this block
 * is decoded at all. If the four words do not hold the values just written, the block is either
 * gated or not decoded, and both of those change what the next run should try.
 */
static void
mfg_gpm_arm(const char *when)
{
	reg_wr(gMfgcfg, MALI_MFG_I2M_CFG_00, 0x20300316u);
	reg_wr(gMfgcfg, MALI_MFG_I2M_CFG_01, 0x1800000cu);
	reg_wr(gMfgcfg, MALI_MFG_I2M_CFG_02, 0x01010802u);
	reg_wr(gMfgcfg, MALI_MFG_I2M_CFG_03, 0x000227f3u);
	__asm__ volatile ("dsb sy" ::: "memory");
	delay(1);
	reg_wr(gMfgcfg, MALI_MFG_I2M_CFG_00, 0x20300317u);
	__asm__ volatile ("dsb sy" ::: "memory");
	delay(2);

	printf("mali: i2m protector %s: %#x %#x %#x %#x\n", when,
	    reg_rd(gMfgcfg, MALI_MFG_I2M_CFG_00), reg_rd(gMfgcfg, MALI_MFG_I2M_CFG_01),
	    reg_rd(gMfgcfg, MALI_MFG_I2M_CFG_02), reg_rd(gMfgcfg, MALI_MFG_I2M_CFG_03));
}

/*
 * The outstanding-transaction counter some notes point at, gated behind a boot argument.
 *
 * The address (0x13FBFF40, in the same page as the protector words above) comes from a second
 * hand report of a register called MFG_MAX_OSTD_R_CON_0, and no source in this tree mentions it:
 * a grep for FF40 and for OSTD across the vendor gpufreq tree returns nothing. Reading a word in
 * this block that turns out not to be decoded does not fault -- it parks the transaction and the
 * watchdog takes the board, which is exactly what 0x13F91034 and 0x13BFEE00 did here. A register
 * that answers tells us whether reads are queued on the bus; a register that hangs costs a run.
 * That trade is not worth making by default, so it is opt in: mali_ostd=1.
 */
static void
mfg_ostd_probe(const char *when)
{
	uint32_t a, b;

	if (!gMaliOstd) {
		return;
	}
	a = reg_rd(gMfgcfg, MALI_MFG_I2M_OSTD);
	delay(50);
	b = reg_rd(gMfgcfg, MALI_MFG_I2M_OSTD);
	printf("mali: ostd %s: %#x -> %#x (%s)\n", when, a, b,
	    a == b ? (a != 0u ? "stuck, reads are queued and not draining" : "zero") :
	    "moving");
}

/*
 * The GPU timestamp counter, which has read zero on every run of this driver.
 *
 * gpufreq_mt6895.c:1240-1247 __gpufreq_set_timestamp() writes 0x00000003 to 0x13FBF130:
 * bit 0 enables the timestamp, bits 1:0 = 0b01 as a pair means the counter is taken from the
 * internal module rather than from the SoC. The register has never been written here, and the
 * failure path prints it as zero every time, next to a cycle counter that is plainly running
 * (about 340 million ticks a second, measured). Two counters in the same block disagreeing is
 * worth resolving before blaming anything downstream: if the timestamp starts counting after
 * this write, the block's internal clock tree is alive end to end; if it stays at zero while
 * the cycle counter runs, the block is running on a clock that the timestamp path does not see.
 */
static void
mfg_set_timestamp(void)
{
	reg_wr(gMfgcfg, MALI_MFG_TIMESTAMP, MALI_MFG_TIMESTAMP_ON);
	delay(2);
	printf("mali: timestamp register %#x (was 0 before the write)\n",
	    reg_rd(gMfgcfg, MALI_MFG_TIMESTAMP));
}

/* ------------------------------------------------------------------ M1: start the MCU */

struct glb_control {
	uint32_t version, features, input_va, output_va;
	uint32_t group_num, group_stride, perfcnt_size, instr_features;
};

struct glb_input {
	uint32_t req, ack_irq_mask, doorbell_req, reserved1;
	uint32_t progress_timer, poweroff_timer;
	uint64_t core_en_mask;
};

struct glb_output {
	uint32_t ack, reserved1, doorbell_ack, reserved2;
	uint32_t halt_status, perfcnt_status, perfcnt_insert;
};

struct csg_control {
	uint32_t features, input_va, output_va, suspend_size, protm_suspend_size;
	uint32_t stream_num, stream_stride;
};

enum {
	kGlbCfgProgress = 1u << 1,
	kGlbCfgAlloc    = 1u << 2,
	kGlbCfgPoweroff = 1u << 3,
	kGlbPing        = 1u << 8,
	kGlbIdleEn      = 1u << 10,
	kGlbIdle        = 1u << 26,
	/*
	 * GLB_REQ.HALT, bit zero of the request word (mali_gpu_csf_registers.h:1105-1106,
	 * GLB_REQ_HALT_SHIFT 0 / GLB_REQ_HALT_MASK (0x1 << 0)).
	 */
	kGlbReqHalt     = 1u << 0,
};

static bool
as_ready(unsigned as)
{
	unsigned spins;

	for (spins = 0; spins < 100000; spins++) {
		if ((mrd(asReg(as, kAsStatus)) & 1u) == 0) {
			return true;
		}
		delay(1);
	}
	return false;
}

static bool
as_command(unsigned as, uint32_t cmd)
{
	if (!as_ready(as)) {
		return false;
	}
	mwr(asReg(as, kAsCommand), cmd);
	/*
	 * And wait for it to finish.
	 *
	 * panthor_mmu.c:556-568 as_send_cmd_and_wait() brackets the write with wait_ready() on both
	 * sides: AS_STATUS bit0 is set while the command is outstanding and the hardware clears it
	 * when the command retires. Waiting only before the write, which is what this function used
	 * to do, cannot tell a command that completed from one that was never accepted, and the
	 * caller then reads AS_FAULTSTATUS on a space that may still be mid-update.
	 */
	if (!as_ready(as)) {
		printf("mali: AS%u never finished command %#x\n", as, cmd);
		return false;
	}
	return true;
}

static uint64_t
gpu_rd64(uint32_t at)
{
	uint64_t lo = (uint64_t)mrd(at);
	uint64_t hi = (uint64_t)mrd(at + 4);

	return lo | (hi << 32);
}

static void
gpu_wr64(uint32_t at, uint64_t value)
{
	mwr(at, (uint32_t)value);
	mwr(at + 4, (uint32_t)(value >> 32));
}

/*
 * The one register block nothing has ever looked at.
 *
 * kbase's CSF register map defines exactly one thing in it -- GPU_CONTROL_MCU, 0x3000, with the
 * comment "MCU control registers" and no sub-register definitions anywhere in the tree, and no
 * access from any driver. That silence is not evidence the block is empty: it is the block whose
 * name matches the thing that will not run, and nothing here has ever read a word of it.
 *
 * The dump only prints words that are not zero and not all ones, so a block of unimplemented
 * registers costs one line and a block with state in it shows its state.
 */
static void
mcu_window_dump(const char *when)
{
	unsigned at;
	unsigned shown = 0;

	printf("mali: MCU window %s:", when);
	/*
	 * mali_mcuwin=1 prints every word, including the zeros.
	 *
	 * The filter below was there because the window looked empty and printing 64 lines of
	 * nothing is noise. It is the opposite of what this run needs: Android's register history
	 * contains no access at all to 0x3000..0x3100 in 2460 records, so this block has never
	 * been compared against anything, and "the words that are zero" is information here --
	 * a state word that should be non-zero and reads zero is a different finding from a state
	 * word that is simply not implemented.
	 */
	for (at = 0x3000u; at < 0x3100u; at += 4u) {
		uint32_t v = mrd(at);

		if (!gMaliMcuWin && (v == 0u || v == 0xffffffffu)) {
			continue;
		}
		if ((shown % 4u) == 0u) {
			printf("\n ");
		}
		printf(" %#x=%#x", at, v);
		shown++;
	}
	if (shown == 0u) {
		printf(" nothing but zeros and ones");
	}
	printf("\n");
}

/* All words here were already read by the existing GPU and MCU-window dumps.
 * Bracketing separates a counter view from a coincidentally changing word. */
static void
mcu_view_sample(const char *when)
{
	unsigned i;

	if (!gMaliTrace) {
		return;
	}
	printf("mali: views %s: gpu status %#x/%#x irq raw %#x/%#x mcu control %#x status %#x\n",
	    when, mrd(kGpuStatus), mrd(0x3034u), mrd(kIntRaw), mrd(0x3020u),
	    mrd(kMcuControl), mrd(kMcuStatus));
	for (i = 0; i < 2; i++) {
		uint32_t c0 = mrd(kGpuCycleCount);
		uint32_t cm = mrd(0x3090u);
		uint32_t c1 = mrd(kGpuCycleCount);
		uint32_t t0 = mrd(kGpuTimestamp);
		uint32_t tm = mrd(0x3098u);
		uint32_t t1 = mrd(kGpuTimestamp);

		printf("mali: views %s %u: cycle %#x/%#x/%#x enclosed %u timestamp %#x/%#x/%#x enclosed %u\n",
		    when, i, c0, cm, c1, (uint32_t)(cm - c0) <= (uint32_t)(c1 - c0),
		    t0, tm, t1, (uint32_t)(tm - t0) <= (uint32_t)(t1 - t0));
		if (i == 0) {
			delay(1000);
		}
	}
}

static bool
as_enable(unsigned as, uint64_t transtab)
{
	unsigned va_bits = mrd(kMmuFeatures) & 0xffu;
	uint64_t transcfg;

	/*
	 * No LOCK and no FLUSH_MEM here.
	 *
	 * panthor_mmu_as_enable() (panthor_mmu.c:605-617) writes TRANSTAB, MEMATTR and TRANSCFG and
	 * then sends one UPDATE. LOCK only exists for the region-locking feature a user VM can ask
	 * for (panthor_mmu.c:1813), and FLUSH_MEM is used when a VM is torn down. Doing both before
	 * the very first UPDATE means the address space is handed an operation whose meaning depends
	 * on a lock region that was never requested, and on some parts that is enough to make the
	 * UPDATE never complete.
	 */
	if (!as_ready(as)) {
		printf("mali: AS%u is busy before it would take the tables\n", as);
		return false;
	}
	/*
	 * No input address size field.
	 *
	 * The value 0x420001c6 came from panthor_mmu.c:790, which does put INA_BITS(55 - va_bits)
	 * in bits 11:6. kbase has no such field: kbase_mmu_hw_direct() (mmu/mali_kbase_mmu_hw_direct.c
	 * :125-152) starts from ADRMODE_AARCH64_4K, clears PTW_MEMATTR, sets PTW_MEMATTR_WRITE_BACK
	 * and R_ALLOCATE, and stops there -- 0x42000006. The phone agrees: every address space on it
	 * is programmed with 0x42000006, and with 0x420001c6 the walk geometry is wrong in a way that
	 * would keep the MCU from fetching anything.
	 */
	(void)va_bits;
	transcfg = (2ull << 24)                /* PTW_MEMATTR: write-back */
	    | (1ull << 30)                     /* PTW_RA: read allocate */
	    | 6ull;                            /* AARCH64 4 KiB granule */
	/*
	 * Which write raises the bus error, asked one write at a time.
	 *
	 * Clearing the latch before AS0 was touched and reading it straight after showed the error
	 * appearing somewhere inside this function: zero after the clear, 0x10000 -- MMU_BUS_ERROR(0)
	 * -- once the address space had taken the update. That narrows it to four candidate writes
	 * but not to one, and the difference matters: if the error appears on the first of the three
	 * configuration writes then the error is the register write itself being refused, and no
	 * amount of table work will help; if it appears only after UPDATE, then it is the walk
	 * reading the tables and the tables are what has to change.
	 *
	 * So each step clears the latch before its own write and reads it after. Four extra register
	 * accesses, and they answer a question that has been guessed at across a dozen runs.
	 */
#define AS_STEP(name, expr)	do { \
		if (!gMaliNoApClear) { mwr(kMmuIntClear, 0xffffffffu); } \
		__asm__ volatile ("dsb sy" ::: "memory"); \
		expr; \
		__asm__ volatile ("dsb sy" ::: "memory"); \
		printf("mali: AS%u step %-9s mmu irq %#x\n", as, name, mrd(kMmuIntRaw)); \
	} while (0)
	AS_STEP("idle", (void)0);
	AS_STEP("transtab", gpu_wr64(asReg(as, kAsTranstab), transtab));
	AS_STEP("memattr", gpu_wr64(asReg(as, kAsMemattr), MALI_MEMATTR));
	AS_STEP("transcfg", gpu_wr64(asReg(as, kAsTranscfg), transcfg));
	/*
	 * Read back what the three writes actually left in the registers.
	 *
	 * Every step so far has been judged by the interrupt latch, which says whether the write was
	 * refused, not whether it landed. If a write had silently gone nowhere, the walker would be
	 * handed a TRANSTAB of zero and would read its root table from physical address zero -- a bus
	 * error with no page fault, which is what every run has reported. Two lines settle it.
	 */
	printf("mali: AS%u readback: transtab %#llx memattr %#llx transcfg %#llx\n", as,
	    (unsigned long long)gpu_rd64(asReg(as, kAsTranstab)),
	    (unsigned long long)gpu_rd64(asReg(as, kAsMemattr)),
	    (unsigned long long)gpu_rd64(asReg(as, kAsTranscfg)));
	AS_STEP("update", mwr(asReg(as, kAsCommand), kAsCmdUpdate));
	/* Inline in the observed AS_STEP path; all registers were read in prior runs. */
	printf("mali: round7 AS%u after UPDATE: raw %#x status %#x fault %#x addr %#llx gpu_fault %#x trial %u\n",
	    as, mrd(kMmuIntRaw), mrd(asReg(as, kAsStatus)),
	    mrd(asReg(as, kAsFaultStatus)),
	    (unsigned long long)gpu_rd64(asReg(as, kAsFaultAddress)), mrd(kGpuFaultStatus), gMaliTrial);
	/*
	 * One UPDATE, not two.
	 *
	 * This used to follow the step with a second one through as_command(), so every space took
	 * the command twice and the second one raced the first one's completion bit in the interrupt
	 * latch. The reply is already visible in the step line above.
	 */
	if (!as_ready(as)) {
		printf("mali: AS%u did not take the update\n", as);
		return false;
	}
	printf("mali: AS%u enabled: root %#llx transcfg %#llx\n", as, transtab, transcfg);
	return true;
}

/*
 * The two things the block wants before the MCU can be started.
 *
 * Both come straight out of mainline's Panthor, which is the only working implementation of this
 * front end there is: panthor_fw_init() calls panthor_hw_l2_power_on() before it loads a byte of
 * firmware, and panthor_fw_start() is only ever reached after a panthor_hw_soft_reset(). Neither
 * had ever been done here -- the loader only powers the domains and the PLL -- and the symptom is
 * exactly what a missing L2 looks like: everything answers (GPU_ID, the identification block, the
 * address space write is taken) and MCU_STATUS stays at zero for ever, because the firmware has
 * nowhere to run from.
 *
 * The order inside l2_power_on matters. Coherency is chosen first, because it is a property of the
 * block rather than of a powered one, and the L2 config register is set before the power request
 * so the cache comes up already configured. Then the request, then two different waits: PWRTRANS
 * must fall to zero (the transition finished) and READY must rise (the block is usable). Panthor
 * polls PWRTRANS before the request as well, to catch a transition left over from a previous life.
 */
/*
 * The GPU's stack supply, the rail that POWER_CHANGED_ALL is waiting on.
 *
 * The settle check in gpu_l2_power_on() now reports it directly: the phone raises POWER_CHANGED_ALL
 * forty times in a boot, this board raises POWER_CHANGED_SINGLE and never ALL, which is a power
 * transition that began and did not finish. The vendor's own power-up order puts the buck ahead of
 * the domains for exactly this reason (gpufreq_mt6895.c:740-820, step 02 between the AOC handshake
 * and the MTCMOS switch), and this driver has never enabled it -- the loader powers the domains
 * and the PLL and stops there.
 *
 * The rail in question is VSTACK: MT6368 VBUCK2, on SPMI bus 5, enable bit 2 of register 0x240.
 * Enabling is a write to the set alias at 0x241 rather than a read-modify-write, which is what the
 * regulator driver's stubs do (mt6368_buck_enable reads enable_reg and enable_mask out of its own
 * descriptor and adds EN_SET_OFFSET); the status register at 0x240 is read-only and is what
 * regulator_is_enabled_regmap() consults. The voltage needs no attention: the phone leaves it at
 * selector 0x6e, 687500 uV, which is the tenth operating point, and reading the selector back
 * before writing anything is the cheap way to notice if that is not true here.
 */
#define MALI_PMIF_BASE          0x1c804000ull
#define MALI_PMIF_SIZE          0x1000u
#define MALI_PMIF_ACC           0x0880u         /* SWINF_2, the only channel inside the 0x8ff window */
#define MALI_PMIF_WDATA         0x0884u
#define MALI_PMIF_RDATA         0x0894u
#define MALI_PMIF_VLDCLR        0x08a4u
#define MALI_PMIF_STA           0x08a8u
#define MALI_PMIF_INIT_DONE     0x0000u
#define MALI_PMIF_CMD_EXT_LONG  3u              /* SPMI 0x20..0x2f / 0x38..0x3f, 16-bit registers */
#define MALI_PMIF_WRITE_BIT     (1u << 29)      /* set on a write, clear on a read */
#define MALI_PMIF_IDLE          0x00u
#define MALI_PMIF_WFVLDCLR      0x06u
#define MALI_PMIF_SPINS         200000u

#define MALI_MT6368_SID         5u              /* pmic@4 in the spmi node is MT6363 at 4 */
#define MALI_VBUCK2_EN          0x0240u
#define MALI_VBUCK2_EN_SET      (MALI_VBUCK2_EN + 1u)
#define MALI_VBUCK2_EN_MASK     (1u << 2)
#define MALI_VBUCK2_VSEL        0x024eu
#define MALI_VBUCK2_VSEL_MASK   0xffu

static vm_offset_t gPmif;

#define pmif_rd(at)             reg_rd(gPmif, (at))
#define pmif_wr(at, v)          reg_wr(gPmif, (at), (v))

/*
 * One SPMI transaction. The command word is the arbiter's, not the PMIC's: bits 31..30 are the
 * PMIF's own opcode translation, bit 29 marks a write, 28..24 the bus id, 23..16 the length minus
 * one, 15..0 the register. Leaving bit 29 out is the failure mode that costs an afternoon -- the
 * transfer runs, the state machine reaches the same place, and the value never arrives.
 */
static bool
mali_spmi(uint32_t addr, uint32_t *value, bool write)
{
	uint32_t cmd = (MALI_PMIF_CMD_EXT_LONG << 30) |
	    (write ? MALI_PMIF_WRITE_BIT : 0u) |
	    (MALI_MT6368_SID << 24) | addr;
	unsigned spins;

	for (spins = 0; spins < MALI_PMIF_SPINS; spins++) {
		if (((pmif_rd(MALI_PMIF_STA) >> 1) & 0x7u) == MALI_PMIF_IDLE) {
			break;
		}
	}
	if (spins == MALI_PMIF_SPINS) {
		return false;
	}
	if (write) {
		pmif_wr(MALI_PMIF_WDATA, *value);
	}
	pmif_wr(MALI_PMIF_ACC, cmd);
	for (spins = 0; spins < MALI_PMIF_SPINS; spins++) {
		if (((pmif_rd(MALI_PMIF_STA) >> 1) & 0x7u) == MALI_PMIF_WFVLDCLR) {
			break;
		}
	}
	if (spins == MALI_PMIF_SPINS) {
		printf("mali: spmi %s %#x stalled at state %u\n", write ? "write" : "read", addr,
		    (unsigned)((pmif_rd(MALI_PMIF_STA) >> 1) & 0x7u));
		return false;
	}
	if (!write) {
		*value = pmif_rd(MALI_PMIF_RDATA) & 0xffu;
	}
	pmif_wr(MALI_PMIF_VLDCLR, 1u);
	return true;
}

/*
 * Turn the stack rail on and confirm it from the PMIC's side rather than from the request.
 *
 * Reading 0x241 back proves nothing -- it is a write-only alias -- so the check is 0x240 bit 2,
 * which is the register regulator_is_enabled_regmap() would read. A rail that is already on is
 * left alone: re-writing the set bit is harmless but it would hide the difference between "was
 * already up" and "we brought it up" in the log, and that difference is the experiment.
 */
static void
mali_vstack_rail(void)
{
	uint32_t status = 0, vsel = 0, four = MALI_VBUCK2_EN_MASK;

	gPmif = ml_io_map(MALI_PMIF_BASE, MALI_PMIF_SIZE);
	if (gPmif == 0) {
		printf("mali: the arbiter at %#llx will not map; the stack rail stays as it is\n",
		    (unsigned long long)MALI_PMIF_BASE);
		return;
	}
	if (pmif_rd(MALI_PMIF_INIT_DONE) == 0u) {
		printf("mali: the arbiter has not finished initialising; not touching the rail\n");
		return;
	}
	if (!mali_spmi(MALI_VBUCK2_EN, &status, false)) {
		return;
	}
	if (!mali_spmi(MALI_VBUCK2_VSEL, &vsel, false)) {
		return;
	}
	printf("mali: stack rail: v buck2 status %#x (enabled %u), vsel %#x\n", status,
	    (status & MALI_VBUCK2_EN_MASK) ? 1u : 0u, vsel);
	if (status & MALI_VBUCK2_EN_MASK) {
		printf("mali: the stack rail was already up; nothing to do\n");
		return;
	}
	if (!mali_spmi(MALI_VBUCK2_EN_SET, &four, true)) {
		return;
	}
	/*
	 * The device tree gives the regulator a 200 microsecond ramp (regulator-enable-ramp-delay =
	 * 0xc8), and nothing downstream can be trusted before it is done.
	 */
	delay(500);
	if (mali_spmi(MALI_VBUCK2_EN, &status, false)) {
		printf("mali: stack rail: status %#x after the enable (enabled %u)\n", status,
		    (status & MALI_VBUCK2_EN_MASK) ? 1u : 0u);
	}
}

/*
 * The interrupt masks the vendor installs before it asks the GPU to power up.
 *
 * Taken from the phone: GPU IRQ cleared and masked with 0x00080503 -- fault, protected fault,
 * reset completed, power changed single and all -- the job IRQ cleared and masked with all ones,
 * the MMU IRQ cleared and masked with 0xffff. Two of these were backwards here for a long time:
 * the job mask was written as zero, which unmasks rather than masks, and GPU_IRQ_MASK had never
 * been written at all.
 *
 * Masking does not stop an event being latched. GPU_IRQ_RAWSTAT fills whether or not the mask in
 * front of it is closed, which is what makes this worth doing before the L2 request: the settle
 * check in gpu_l2_power_on() reads the raw register, and it should be reading a transition this
 * run caused rather than one the bootloader left behind.
 *
 * The masks do not stay on. The trace has twelve writes across a boot, alternating all-ones
 * before a start with zero immediately after the firmware comes up -- zero meaning unmasked,
 * 0xffffffff meaning masked. start_mcu() writes the second half once the boot is confirmed.
 */
static void
gpu_irq_mask_prestart(void)
{
	mwr(kIntClear, kGpuIrqMaskVendor);
	mwr(kIntMask, kGpuIrqMaskVendor);
	mwr(kJobIntClear, 0xffffffffu);
	mwr(kJobIntMask, 0xffffffffu);
	mwr(kMmuIntClear, 0xffffffffu);
	mwr(kMmuIntMask, 0x0000ffffu);
}

static bool
gpu_l2_power_on(void)
{
	unsigned spins;

	/*
	 * The power key, which the phone's driver never writes and this one only writes on the reset
	 * path.
	 *
	 * GPU_PWR_KEY is a write-only unlock in front of the power manager. Panfrost writes it once at
	 * the top of its own GPU init (panfrost_gpu.c:86, GPU_PWR_KEY_UNLOCK = 0x2968A819); kbase on
	 * this part does not, and neither of the two register traces off this phone contains a single
	 * store to 0x050. It was added here on the theory that the L2 request was being accepted and
	 * ignored, which turned out to have a different cause, and it has stayed since on the theory
	 * that a key is harmless if it was never needed. That is true of a key and not true of this
	 * register: whatever it unlocks, the vendor's firmware start does not have it unlocked, and
	 * the point of the cold path is to differ from a working machine in as few ways as possible.
	 */
	if (!gMaliCold) {
		mwr(kPwrKey, kPwrKeyUnlock);
		__asm__ volatile ("dsb sy" ::: "memory");
		printf("mali: power key written; trans %#x active %#x ready %#x\n",
		    mrd(kL2PwrTrans), mrd(kL2PwrActive), mrd(kL2Ready));
	}

	/* Only the first core group: the same simplification Panthor makes on one-group parts. */
	if (mrd(kL2Present) != kL2PresentFirst) {
		printf("mali: the L2 reports itself as %#x, not one core group\n", mrd(kL2Present));
	}
	/*
	 * Coherency: NONE on the reset path, untouched on the cold path.
	 *
	 * 0x304 is written exactly once in the five hundred records of the reset trace -- 0x1f, which
	 * is GPU_COHERENCY_NONE -- and never in the twenty-four hundred records of a boot. Panthor
	 * writes it too, but from panthor_gpu_coherency_set(), which runs under a feature check whose
	 * outcome on this part is unknown to us: the value it would write here is the same 0x1f, so
	 * the write is either redundant or was already done before the trace window opens. Either way
	 * the cold path does not need it, and one fewer difference from a working machine is worth
	 * more than one more guess.
	 */
	if (!gMaliCold) {
		mwr(kCoherencyProtocol, kCoherencyNone);
	}
	/*
	 * The ASN hash, which is the one thing panthor has to do differently on a MediaTek part.
	 *
	 * panthor_gpu_l2_config_set() runs here -- after the coherency write and before the L2
	 * power request -- on parts that carry asn_hash_enable in their soc_data, and mt8196 is
	 * the only one upstream. The program reads the three hash words back and reports them
	 * even when the switch is off, because a readback costs nothing and the difference
	 * between "wired to these values" and "never written" is worth having in the log.
	 */
	{
		unsigned i;

		printf("mali: l2_config before %#x; asn_hash", mrd(kL2Config));
		for (i = 0; i < kAsnHashCount; i++) {
			printf(" %#x", mrd(kAsnHash0 + i * kAsnHashStep));
		}
		/*
		 * Off on the cold path for a reason that is no longer a guess: this part does not have
		 * the registers. A run with the writes enabled read l2_config back unchanged, bit 24
		 * clear, and all three hash words still zero -- G610 is architecture 10 and the upstream
		 * patch that adds them is guarded by GPU_ARCH_MAJOR < 11. The read-only report stays,
		 * because it costs nothing and confirms the conclusion on every boot.
		 */
		if (gMaliAsnHash && !gMaliCold) {
			for (i = 0; i < kAsnHashCount; i++) {
				mwr(kAsnHash0 + i * kAsnHashStep, kAsnHashMt8196[i]);
			}
			mwr(kL2Config, mrd(kL2Config) | kL2ConfigAsnHashEnable);
			__asm__ volatile ("dsb sy" ::: "memory");
			printf("; wrote them\n");
		} else {
			printf("; left alone\n");
		}
		printf("mali: l2_config after %#x; asn_hash", mrd(kL2Config));
		for (i = 0; i < kAsnHashCount; i++) {
			printf(" %#x", mrd(kAsnHash0 + i * kAsnHashStep));
		}
		printf("\n");
	}

	for (spins = 0; spins < 100000u; spins++) {
		if ((mrd(kL2PwrTrans) & kL2PresentFirst) == 0u) {
			break;
		}
		delay(1);
	}
	if (spins == 100000u) {
		/*
		 * A transition that never finishes before anything was asked for is a block that is
		 * mid-way through a power change it started on its own -- or a power manager that is
		 * still locked. Say so and then write the request anyway: Panthor fails here, but
		 * failing here leaves the L2 off, and the request costs nothing if the block is deaf.
		 */
		printf("mali: the L2 is still transitioning before the request (trans %#x)\n",
		    mrd(kL2PwrTrans));
	}
	mwr(kL2PwrOn, kL2PresentFirst);
	for (spins = 0; spins < 5000u; spins++) {
		if ((mrd(kL2Ready) & kL2PresentFirst) == kL2PresentFirst) {
			break;
		}
		if (spins == 1500u) {
			/*
			 * Ask again rather than give up.
			 *
			 * The phone reaches READY three or four polls after the request -- about twenty
			 * microseconds -- so anything in the milliseconds is already far outside the
			 * hardware's own behaviour. If it has not happened by then the request was dropped,
			 * and the thing a working machine does with a dropped request is issue it again,
			 * not declare the rail dead. Panthor returns an error here; Panthor also owns its
			 * own power domains, and this driver is working with rails a bootloader left on.
			 */
			printf("mali: the L2 did not come up on the first request (trans %#x active"
			    " %#x ready %#x), asking again\n", mrd(kL2PwrTrans), mrd(kL2PwrActive),
			    mrd(kL2Ready));
			mwr(kL2PwrOn, kL2PresentFirst);
		}
		delay(1);
	}
	if (spins == 5000u) {
		printf("mali: the L2 never became ready (trans %#x active %#x ready %#x)\n",
		    mrd(kL2PwrTrans), mrd(kL2PwrActive), mrd(kL2Ready));
		return false;
	}
	printf("mali: L2 up: trans %#x active %#x ready %#x\n", mrd(kL2PwrTrans),
	    mrd(kL2PwrActive), mrd(kL2Ready));
	/*
	 * Wait for the power controller to say the whole block has settled, not just one domain.
	 *
	 * This is the check that was missing for the entire M1 effort, and it is the one that
	 * explains all three symptoms at once. GPU_IRQ_RAWSTAT bit 10 is POWER_CHANGED_ALL -- every
	 * domain in the GPU has finished transitioning -- and bit 9 is POWER_CHANGED_SINGLE, one
	 * domain has. On the phone's own boot the raw status carries bit 10 forty times and bit 9
	 * never once. On this board it has carried bit 9 and never bit 10, on every run, which is the
	 * signature of a power transition that started and did not finish: the L2 reports itself
	 * ready while the block as a whole is still between states, and an MCU fetching through an
	 * L2 that is not fully clocked raises no fault, because nothing ever leaves the core.
	 *
	 * The interrupt line cannot be used to wait for this -- the mask in front of it is
	 * 0x00080503, which has bit 10 set, so the event is latched raw and never surfaces. Poll the
	 * raw register instead.
	 */
	{
		uint32_t stale = mrd(kIntRaw);
		uint32_t raw = 0;

		/*
		 * Read before clearing, and print both views.
		 *
		 * This check shipped reading the register after writing the clear bit, and
		 * GPU_IRQ_CLEAR is write-one-to-clear, so the read that followed it could never contain
		 * bit 10. The check was therefore incapable of passing, and its verdict -- "SINGLE only"
		 * -- was then treated as evidence that the power was not settling. Two things are
		 * printed here instead: the latch as it was found, and the masked view, because whether
		 * GPU_IRQ_MASK hides this bit on this part is exactly what nobody here knows.
		 */
		if (!gMaliNoPwrClear) {
			mwr(kIntClear, kGpuIrqPowerChangedAll | kGpuIrqPowerChangedSingle);
		}
		for (spins = 0; spins < 20000u; spins++) {
			raw = mrd(kIntRaw);
			if (raw & kGpuIrqPowerChangedAll) {
				break;
			}
			delay(1);
		}
		printf("mali: power settle: found %#x (all %u single %u), now raw %#x status"
		    " %#x\n", stale,
		    (stale & kGpuIrqPowerChangedAll) ? 1u : 0u,
		    (stale & kGpuIrqPowerChangedSingle) ? 1u : 0u,
		    raw, mrd(kIntStatus));
		printf("mali: %s\n",
		    ((stale | raw) & kGpuIrqPowerChangedAll) ?
		    "the whole block settled, so power and clocks are not the problem" :
		    "no ALL either side of the request, so a transition never finished");
		if (!gMaliNoPwrClear) {
			mwr(kIntClear, kGpuIrqPowerChangedAll | kGpuIrqPowerChangedSingle);
		}
	}
	/*
	 * The shader and tiler cores are deliberately not touched here.
	 *
	 * An earlier version of this file wrote SHADER_PWRON (0x180) with the present bitmap, on the
	 * reading that kbase derives the power request from the presence register by subtraction
	 * (backend/gpu/mali_kbase_pm_driver.c:88-89). The write is accepted -- SHADER_PWRTRANS takes
	 * the whole mask immediately -- and nothing comes of it: POWER_CHANGED_ALL never arrives and
	 * READY stays zero. The phone's own trace explains why the write is absent there. Across 2460
	 * records nothing writes 0x180, and SHADER_READY goes from zero to 0x00130013 and back again
	 * eight times in one boot (records 228, 630, 1016, 1044, 1097, 1493, 1898, 2286). It is not a
	 * rail indicator at all; it tracks firmware activity. A zero there is a consequence of the MCU
	 * not running and cannot be used to argue that the cores have no power.
	 *
	 * The tiler is left alone for the same reason, though its path does work: writing 0x190 with
	 * 0x1 raised its READY bit immediately.
	 */
	return true;
}

/*
 * The IPA block, which the vendor driver arms before every MCU start and this one never touched.
 *
 * The phone's cold-start trace has this exact sequence at records 69 to 90, between the interrupt
 * masks and L2_PWRON -- that is, after the address spaces are programmed and before the L2 is
 * raised:
 *
 *	 69 w 0x40010 = 0x20181004	the block's configuration word
 *	 70..76 w 0x40014..0x4002c = 0
 *	 77 r 0x40004			status
 *	 78 w 0x40000 = 1		one command
 *	 79 r 0x40004
 *	 80 w 0x40000 = 3		another
 *	 81 r 0x40004
 *	 82 w 0x40008 = 0
 *	 83..90 r 0x40100..0x4011c	the eight counters, all zero before a start
 *
 * And after every start that worked, the same block is written again at records 390 to 400 --
 * 0x40008 = 0, 0x40000 = 3 -- and the counters at 0x40100 come back non-zero:
 *
 *	round 1: 0x40100 = 0x00224328   0x40108 = 0x00007f7a   0x40118 = 0x0012a773
 *	round 2: 0x40100 = 0x001813a5   0x40108 = 0x00003186   0x40118 = 0x000885a4
 *	round 3: 0x40100 = 0x003ab8a6   0x40108 = 0x0001740c   0x40118 = 0x0016afee
 *
 * Those counters advance only if the MCU is executing, so they are a second, independent answer to
 * the question MCU_STATUS cannot answer. A start that leaves them at zero did not run anything,
 * whatever the status word says.
 *
 * Whether the block is a prerequisite or only a witness is not something the trace can settle --
 * but it is the only thing in the vendor's cold path this driver does not do, so it is written
 * here and the result is read either way.
 */
static void
gpu_ipa_arm(void)
{
	unsigned at;

	mwr(kIpaCfg, kIpaCfgValue);
	for (at = kIpaCfg + 4u; at <= kIpaCfgEnd; at += 4u) {
		mwr(at, 0);
	}
	(void)mrd(kIpaStatus);
	mwr(kIpaCmd, 1u);
	(void)mrd(kIpaStatus);
	mwr(kIpaCmd, 3u);
	(void)mrd(kIpaStatus);
	mwr(kIpaClear, 0);
	printf("mali: ipa armed; %#x %#x %#x %#x\n", mrd(kIpaCounter0), mrd(kIpaCounter0 + 8u),
	    mrd(kIpaCounter0 + 0x10u), mrd(kIpaCounter0 + 0x18u));
}

/*
 * Report the IPA counters, which move only while the MCU is executing.
 */
static void
gpu_ipa_report(const char *when)
{
	printf("mali: ipa %s: %#x %#x %#x %#x %#x %#x %#x %#x\n", when, mrd(kIpaCounter0),
	    mrd(kIpaCounter0 + 4u), mrd(kIpaCounter0 + 8u), mrd(kIpaCounter0 + 0xcu),
	    mrd(kIpaCounter0 + 0x10u), mrd(kIpaCounter0 + 0x14u), mrd(kIpaCounter0 + 0x18u),
	    mrd(kIpaCounter0 + 0x1cu));
}

/*
 * Park the seven address spaces the MCU does not use.
 *
 * The vendor driver programs all eight before it starts the firmware, and the trace says what
 * each one gets: address space zero is handed the MCU's own page tables with transcfg 0x42000006
 * (AARCH64, 4 KiB), and the rest are written 0x42000001 -- ADRMODE_UNMAPPED -- with a zero
 * translation table. That is not the same as leaving them alone: a space left at whatever the
 * block powered up with still answers walks, and a walk into one of them is a fault raised into a
 * line nobody is watching.
 *
 * This driver had only ever programmed space zero. The writes cost nine stores each and make the
 * block match the only configuration known to boot.
 */
static void
as_park_unused(void)
{
	unsigned present = mrd(kAsPresent);
	unsigned as;

	/*
	 * AS1 is absent from the cold-start prologue. Record 9 addresses AS2, and record 18
	 * addresses AS3. Both get MALI_MEMATTR; AS4 through AS7 get zero. mali_parkattrs changes
	 * exactly those four AS2/AS3 words, leaving the earlier all-zero baseline available for a
	 * direct comparison. All six spaces remain UNMAPPED, so a trace difference alone does not
	 * establish that their attributes are required for the MCU's AS0 fetches.
	 */
	/*
	 * Measure each space's UPDATE, one at a time.
	 *
	 * Every argument about MMU_BUS_ERROR(0) so far rested on the claim that spaces two through
	 * seven take the same write without raising it, and that claim was never measured: the loop
	 * below wrote and moved on, and only AS0's latch was ever read. If those spaces raise the
	 * error too then it belongs to the block, not to the address space, and the whole line of
	 * reasoning that treats zero as an AS0-specific wall is wrong.
	 *
	 * So: clear, write, read, per space. Seven extra register accesses and the claim becomes a
	 * fact either way.
	 */
	for (as = 2; as < 8u; as++) {
		if ((present & (1u << as)) == 0u) {
			continue;
		}
		if (!gMaliNoApClear) { mwr(kMmuIntClear, 0xffffffffu); }
		__asm__ volatile ("dsb sy" ::: "memory");
		gpu_wr64(asReg(as, kAsTranstab), 0);
		gpu_wr64(asReg(as, kAsMemattr),
		    (gMaliParkAttrs && as <= 3u) ? MALI_MEMATTR : 0);
		gpu_wr64(asReg(as, kAsTranscfg), kAsTranscfgUnmapped);
		(void)as_command(as, kAsCmdUpdate);
		__asm__ volatile ("dsb sy" ::: "memory");
		printf("mali: AS%u parked, mmu irq %#x\n", as, mrd(kMmuIntRaw));
	}
	printf("mali: address spaces 2 to 7 are unmapped, 1 is left for the firmware (present %#x)\n",
	    present);
}

/*
 * A soft reset of the whole block, and the one interrupt that confirms it.
 *
 * Panthor does this before every firmware start, and clears the reset bit from the raw status
 * first so that what comes back is this reset and not the one the block did when it was powered.
 * Without it the MCU is handed a block whose state is whatever the loader's power-up left behind;
 * with it the block is in the state the firmware was written against.
 *
 * Where it belongs is before the L2 comes up, not after. This function used to be called
 * immediately before MCU_CONTROL, and there it was actively harmful: a soft reset returns the
 * block to its powered-on default, which drops the L2 that had just been raised, and the MCU is
 * then handed nothing to run out of. That is why it was removed and why its removal did not help.
 * The phone's own cold-start trace settles where it goes: after the loader has powered the block
 * and before GPU_PWR_KEY, the first thing kbase writes is GPU_CMD = 0x00000101, and only then
 * does it write PWR_KEY and L2_PWRON.
 */
static bool
gpu_soft_reset(void)
{
	unsigned spins;

	mwr(kIntClear, kGpuIrqResetCompleted);
	mwr(kGpuCmd, kGpuCmdSoftReset);
	for (spins = 0; spins < 100000u; spins++) {
		if (mrd(kIntRaw) & kGpuIrqResetCompleted) {
			break;
		}
		delay(1);
	}
	if (spins == 100000u) {
		printf("mali: the soft reset never completed (status %#x int %#x)\n",
		    mrd(kGpuStatus), mrd(kIntRaw));
		return false;
	}
	mwr(kIntClear, kGpuIrqResetCompleted);
	printf("mali: soft reset done: status %#x\n", mrd(kGpuStatus));
	return true;
}

/*
 * The hardware-issue registers, which on this part do not come out of reset the way the block
 * needs them.
 *
 * kbase calls this kbase_pm_hw_issues_apply() (backend/gpu/mali_kbase_pm_driver.c:2470-2500) and
 * writes SHADER_CONFIG, TILER_CONFIG, L2_MMU_CONFIG and CSF_CONFIG in that order, from values its
 * hw-issues table carries per GPU revision. The phone's cold-start trace has the same four writes
 * back to back, all zero, at records 269-272 -- after the soft reset and before the address space
 * is programmed:
 *
 *	269 w 0x0f04 = 0x00000000	SHADER_CONFIG
 *	270 w 0x0f08 = 0x00000000	TILER_CONFIG
 *	271 w 0x0f0c = 0x00000000	L2_MMU_CONFIG
 *	272 w 0x0f00 = 0x00000000	CSF_CONFIG
 *
 * They do not appear in the boot-time trace at all, because the bootloader had already left the
 * block configured; only the reset path rewrites them. This driver had never written any of the
 * four, and if any of them comes out of reset with a bit that stops the MCU from fetching, that
 * alone explains a block which answers every register read and executes nothing.
 */
static void
gpu_hw_issues(void)
{
	/*
	 * Read before writing, because the write is unconditional zero and a zero that was
	 * already there proves nothing. One of the two vendor notes claims these registers come
	 * out of reset with non-zero quirks and the other claims they read back all zero; only
	 * the board can settle it, and it costs four loads.
	 */
	/*
	 * Leave them alone unless they are not already zero.
	 *
	 * The vendor driver does not write zero here: it copies the values the GPU advertises into
	 * these four words (0x3b92c, 0x3b94c, 0x3b96c, 0x3b98c, source kbdev[0..0xc] from gpuprops).
	 * On this part they read back as zero anyway, which is why the blanking looked harmless, but
	 * a zero written over a non-zero quirk word is a way to make the MCU behave differently from
	 * the only configuration known to run. Reading first and writing only a genuine difference
	 * keeps the run comparable.
	 */
	{
		static const struct {
			uint32_t at;
			const char *what;
		} kIssues[] = {
			{ kShaderConfig, "shader" }, { kTilerConfig, "tiler" },
			{ kL2MmuConfig, "l2mmu" }, { kCsfConfig, "csf" },
		};
		unsigned i;

		for (i = 0; i < sizeof(kIssues) / sizeof(kIssues[0]); i++) {
			uint32_t before = mrd(kIssues[i].at);

			if (before != 0u) {
				printf("mali: hardware issue %s was %#x; leaving it\n",
				    kIssues[i].what, before);
			}
		}
	}
	printf("mali: hardware issues: shader %#x tiler %#x l2mmu %#x csf %#x\n",
	    mrd(kShaderConfig), mrd(kTilerConfig), mrd(kL2MmuConfig), mrd(kCsfConfig));
}

/*
 * Flush the block's caches, and wait for the interrupt that says it is done.
 *
 * The phone's cold-start trace has GPU_CMD = 0x00003304 at record 189, between arming the
 * interrupts and raising the L2, and again at record 265 right after the soft reset. That value is
 * GPU_CMD_DEF(4, 0x33) = GPU_FLUSH_CACHES(l2 = 3, lsc = 3, other = 0)
 * (panthor_gpu_regs.h:50, panthor_gpu.c:350-395), and 0x33 is "clean and invalidate" for both the
 * L2 and the LSC.
 *
 * It matters here for a reason that has nothing to do with performance: the firmware image is
 * written into DRAM by the CPU and then read by the MCU through the L2. If the L2 holds anything
 * from before those writes -- and it does, because the loader ran with the block powered and the
 * L2 was raised over whatever the previous life left in it -- the MCU can fetch stale
 * instructions and never reach its first real one.
 *
 * The interrupt is CLEAN_CACHES_COMPLETED in GPU_IRQ_RAWSTAT bit 17; the trace masks it in with
 * 0x000a0503 at record 188 and back out at 193, which is exactly this window.
 */
static bool
gpu_flush_caches(void)
{
	unsigned spins;

	mwr(kIntClear, kGpuIrqCleanCaches);
	mwr(kGpuCmd, kGpuCmdFlushCaches);
	for (spins = 0; spins < 100000u; spins++) {
		if (mrd(kIntRaw) & kGpuIrqCleanCaches) {
			break;
		}
		delay(1);
	}
	if (spins == 100000u) {
		printf("mali: the cache flush never completed (status %#x int %#x)\n",
		    mrd(kGpuStatus), mrd(kIntRaw));
		return false;
	}
	mwr(kIntClear, kGpuIrqCleanCaches);
	printf("mali: caches flushed, %u spins\n", spins);
	return true;
}

static bool
start_mcu(void)
{
	unsigned spins;
	bool booted = false;
	bool sawHalt = false;
	bool sawFatal = false;

	/*
	 * These two are the whole difference from the run that stopped at "MCU did not start".
	 * They belong here rather than earlier because both touch the block, and mali=3 and mali=4
	 * are supposed to touch nothing beyond the firmware and the address space -- a level that
	 * cannot be compared with the one below it is not a step in a bisection.
	 */
	/*
	 * Nothing is reset here, and the L2 was brought up long before this point.
	 *
	 * Both used to be here. Panthor does neither at this stage: panthor_fw_init() calls
	 * panthor_hw_l2_power_on() before it loads a byte of firmware (panthor_fw.c:1521), and
	 * panthor_gpu_soft_reset() is only reached from panthor_gpu_suspend(), never on the path that
	 * starts the MCU. A soft reset immediately before MCU_CONTROL also has a specific cost: it
	 * clears the block back to the powered-on default, which drops the L2 that had just been
	 * raised, and the MCU then has nothing to run out of.
	 */
	/*
	 * The interrupt masks were installed before the L2 was raised, by gpu_irq_mask_prestart(),
	 * which is where the phone's trace has them -- after the address spaces, before the power
	 * request. Nothing to do here, and nothing to undo: the same six values are still in place.
	 */
	/*
	 * A sentinel in the word the firmware is supposed to write.
	 *
	 * The loaded image already carries a plausible global interface -- eight groups, the input
	 * and output addresses, the instrumentation bits -- so reading it back tells us nothing about
	 * whether the MCU ever ran: those values are what the CPU put there. The version word is the
	 * one the firmware has to overwrite, and in the image it is zero, which is also what "never
	 * ran" reads. Writing a value that no firmware would ever produce breaks the tie: if it
	 * survives, not one instruction executed; if it is gone, the firmware ran and then wrote zero
	 * or faulted after writing.
	 */
	if (gShared != NULL) {
		volatile struct glb_control *sentinel =
		    (volatile struct glb_control *)gShared->mem.kva;

		sentinel->version = kSentinelVersion;
		mem_flush(&gShared->mem);
		printf("mali: sentinel %#x written to the version word\n", kSentinelVersion);
	}
	/* Where the ring cannot lose it: the protection state as it stands at the moment the MCU is
	 * about to be started, which is the only moment it can still matter. */
	bus_protect_sta("before the start request");
	/*
	 * Three writes, in this order, and a status read after each.
	 *
	 * The encodings are not symmetric and getting them wrong has already cost one confusing run:
	 * writing one here is MCU_CONTROL_ENABLE, MCU_CONTROL_DISABLE is zero, and AUTO -- the only
	 * value kbase ever uses to start the firmware -- is two. So a run that wrote one and reported
	 * "status went to one after DISABLE" was reporting an enable, and the readback only proved
	 * that the status follows the control word.
	 *
	 * That readback is worth having anyway, which is why it is done properly here. A control
	 * register whose status never changes is a block with no clock on it, and every silence on
	 * this side would follow from that in one step. A control register whose status tracks the
	 * write is a block that is listening, and then the silence means something narrower.
	 */
	/*
	 * Report the state, do not walk it through the three values.
	 *
	 * The three-write probe that used to be here wrote ENABLE, and the vendor driver never writes
	 * one to this register: every real write in the module's 1.8 megabytes is zero or two (twelve
	 * 'add #0x700' sites in the disassembly, five of them real, values {0, 0, 0, 2, 2, 2}). One
	 * leaves the MCU in a state neither of the two drivers ever visits, and on a core that is not
	 * executing, "which state does it sit in" is precisely the question being asked -- so it must
	 * not be overwritten on the way to asking.
	 */
	printf("mali: MCU status %u before the start request\n", mrd(kMcuStatus));
	printf("mali: MCU control reads %#x before the start request (doorbell disable %s)\n",
	    mrd(kMcuControl),
	    (mrd(kMcuControl) & kMcuControlDoorbellDisable) ? "SET -- the core would ignore every doorbell" : "clear");
	/*
	 * GPU_IRQ_STATUS bit 19 and not MCU_STATUS bit 19.
	 *
	 * mali_kbase_gpu_regmap_csf.h:314 defines MCU_STATUS_GPU_IRQ as (1 << 19) but the name is
	 * misleading -- it is a bit in GPU_IRQ_STATUS/RAWSTAT (0x020-0x02c), the channel the MCU uses
	 * to say "I need attention". Reading 0x704 for it cannot work: 0x704 is a three-value enum.
	 * This project has never read 0x02c for that bit, and a core that comes out of reset and
	 * cannot fetch would be expected to raise exactly this.
	 */
	printf("mali: gpu irq status %#x before the start request (mcu attention %s)\n",
	    mrd(kIntStatus),
	    (mrd(kIntStatus) & kGpuIrqMcuAttention) ? "RAISED" : "quiet");
	printf("mali: MCU control reads %u before the start request\n", mrd(kMcuControl));
	mcu_window_dump("before the start request");
	mcu_view_sample("before AUTO");
	/*
	 * No doorbell is cleared here, and the comment that used to say Panthor does it was wrong.
	 *
	 * panthor_fw_start() (panthor_fw.c:1132-1165) is five statements: mark not booted, enable every
	 * event on the firmware interrupt, resume that interrupt, write MCU_CONTROL = AUTO, wait up to
	 * a second. Not one of them touches a doorbell. Across the whole tree CSF_DOORBELL appears
	 * twice and the only value ever written is one. The phone's trace says the same from the other
	 * side: all forty-nine writes to 0x80000 are one and none is zero, and the first of them
	 * (record 174) comes eleven records after the firmware had already announced itself at record
	 * 163. The vendor rings the bell once the firmware is up; it never rings it to wake it. Writing
	 * zero into a doorbell whose state cannot be read is, at best, a request withdrawn from a
	 * firmware that was not there to see it.
	 */
	/*
	 * Clear the MMU interrupt latch before the core is told to run.
	 *
	 * It has been carrying MMU_BUS_ERROR(0) since AS0 took its update, and nothing in this driver
	 * has ever cleared it again -- so every reading taken after the start request so far has been
	 * the same stale bit, and "the core faulted while fetching" and "the core never fetched" have
	 * been indistinguishable at that register. What the walk's fault means and what a fetch fault
	 * would mean are different questions; separating them costs one write.
	 */
	if (!gMaliNoApClear) { mwr(kMmuIntClear, 0xffffffffu); }
	__asm__ volatile ("dsb sy" ::: "memory");
	printf("mali: mmu irq %#x with the latch freshly cleared, just before the start request\n",
	    mrd(kMmuIntRaw));
	/*
	 * Clear GLB_REQ.HALT before the core is told to run.
	 *
	 * This is the one start-up precondition that the firmware's own request word carries, and
	 * every source that mentions it says the same thing. The 2025 kbase does it unconditionally,
	 * with the comment "Clear the HALT bit before triggering the boot of MCU firmware, regardless
	 * of FW I/O status" (kbase_csf_fw_io_global_write_mask(fw_io, GLB_REQ, 0, GLB_REQ_HALT_MASK)).
	 * An Arm engineer on the list put the consequence in words: leaving HALT set can in certain
	 * cases make the MCU fail to boot.
	 *
	 * r32p1 -- the version this phone's own driver is built from -- does not have this step. A
	 * grep finds GLB_REQ_HALT_MASK exactly once in the whole tree, at
	 * mali_kbase_csf_firmware.c:2084, and that is the halt direction. enable_mcu() is a single
	 * store of AUTO (csf/mali_kbase_csf_firmware.h:479). So on a phone whose GPU works, the bit
	 * happens to be clear already, which is why the omission never shows there: the request word
	 * is part of the shared interface, and the firmware image carries it as zero.
	 *
	 * Here the shared page is ours. It was filled from the image, so HALT should read zero --
	 * but "should" is the word that has cost every run so far, and the field is writable by this
	 * side. Clearing it costs two accesses and removes a whole class of explanation: a core that
	 * comes up, sees HALT in its own request word, and waits for a handshake that nothing on this
	 * machine is going to send. That is precisely the shape of what is observed -- status one,
	 * never two, no version word, no fault.
	 *
	 * The readback is printed because the value before the write matters: if it was already zero,
	 * this whole theory is dead on the spot and one run says so.
	 */
	if (gShared != NULL) {
		volatile struct glb_control *control =
		    (volatile struct glb_control *)gShared->mem.kva;
		volatile struct glb_input *input =
		    (volatile struct glb_input *)shared_at(control->input_va);

		if (input != NULL) {
			uint32_t before = input->req;

			input->req = before & ~kGlbReqHalt;
			mem_flush(&gShared->mem);
			__asm__ volatile ("dsb sy" ::: "memory");
			printf("mali: glb req %#x -> %#x (halt was %s)\n", before, input->req,
			    (before & kGlbReqHalt) ? "set, now cleared" : "already clear");
		} else {
			printf("mali: the input block is outside the shared section\n");
		}
	}
	/* Freeze before AUTO, then print after the attempt so tracing cannot delay the request. */
	gMaliWritesFrozen = true;
	/*
	 * mali_mcuseq=1: two writes instead of one.
	 *
	 * kbase's register header defines MCU_CNTRL_ENABLE as (1 << 0) and MCU_CNTRL_AUTO as
	 * (1 << 1) -- two separate bits in one field, not two values of one enum. Every kbase site
	 * writes AUTO alone, and so does this driver, which is why "write 2" has been treated as
	 * the whole start sequence. But the header also defines MCU_STATUS_HALTED as (1 << 1) and
	 * nothing else, so the "1" this board reads back from MCU_STATUS is not a value the header
	 * names at all -- the reading "1 means ENABLED" is this project's interpretation, not
	 * something the vendor documents.
	 *
	 * If that "1" is MCU_STATUS bit 0 rather than an enum value, then the core is sitting in a
	 * state the AUTO write does not clear, and one ENABLE written before it is the cheapest
	 * way to find out. The vendor's comment on enable_mcu gives the reason it might matter:
	 * AUTO exists so that on a fast reset the MCU can leave protected mode by itself -- it
	 * presumes a core that has been through an ENABLE at least once.
	 */
	if (gMaliMcuSeq) {
		mwr(kMcuControl, kMcuControlEnable);
		__asm__ volatile ("dsb sy" ::: "memory");
		delay(5);
		printf("mali: mcuseq: after ENABLE write, control reads %#x status %#x\n",
		    mrd(kMcuControl), mrd(kMcuStatus));
	}
	mwr(kMcuControl, kMcuControlAuto);
	if (gMaliMcuSeq) {
		__asm__ volatile ("dsb sy" ::: "memory");
		delay(5);
		printf("mali: mcuseq: after AUTO write, control reads %#x status %#x (status bits: 0=%u 1=%u 2=%u 3=%u)\n",
		    mrd(kMcuControl), mrd(kMcuStatus),
		    mrd(kMcuStatus) & 1u, (mrd(kMcuStatus) >> 1) & 1u,
		    (mrd(kMcuStatus) >> 2) & 1u, (mrd(kMcuStatus) >> 3) & 1u);
	}
	/*
	 * And ring the doorbell.
	 *
	 * This is the one write the vendor makes that this driver never made at all, and it is the
	 * only way the firmware is ever told there is something to look at. The trace from the phone
	 * has it eight times in the first second (0x80000 = 1 at records 174, 216, 232, 233, 236, 237,
	 * 260, 280), interleaved with reads of JOB_INT_STATUS and clears of whatever it raised. The
	 * global interface doorbell is register zero, CSF_DOORBELL(0) = 0x80000
	 * (panthor_fw_regs.h, CSF_GLB_DOORBELL_ID).
	 *
	 * Ringing it repeatedly is deliberate and matches the vendor: one ring per pass through the
	 * loop, because a doorbell raised before the MCU has finished its own start-up is not queued
	 * anywhere.
	 */
	/*
	 * One loop, two questions: did the firmware announce itself, and did the core ever leave the
	 * enabled state at all.
	 *
	 * JOB_INT_STATUS rather than JOB_INT_RAWSTAT. The two differ in the way that matters: RAWSTAT
	 * is the latch, STATUS is what survives the mask. The phone never reads 0x1000 -- the boot is
	 * announced by a read of 0x100c returning 0x80000000, twenty-seven records after the control
	 * word -- and this loop had been polling the raw register, which is why it burned its whole
	 * budget and reported nothing six runs running. Panthor reads INT_STAT, the masked one, in the
	 * same place (panthor_fw.c:1142-1148).
	 *
	 * The status histogram is the part that was missing. On the phone every round reads 1, 1, 1,
	 * then 2, and only then does the driver write DISABLE: two is HALT, and reaching it means the
	 * core ran far enough to stop itself. This board has only ever been read at the end, where it
	 * says 1, and one is ambiguous -- kbase's own table calls it "enabled", not "running". So the
	 * status is sampled throughout instead, and a two anywhere is reported as the finding it
	 * would be.
	 *
	 * The doorbell is gone from this loop. All forty-nine writes to 0x80000 in the trace are one
	 * and none is zero, and the first of them lands eleven records after the firmware has already
	 * spoken: the bell is rung once the firmware is up, never to wake it.
	 */
	for (spins = 0; spins < 1000000; spins++) {
		uint32_t stat = mrd(kJobIntStatus);
		uint32_t st = mrd(kMcuStatus);

		/*
		 * Sample the MFG channel as well, at four widely spaced points.
		 *
		 * The channel read idle before this loop and busy after it, which is the first thing in
		 * this whole search that moved when the core was told to run. When in the loop it moves
		 * decides what moved it: at the first sample the core has barely been enabled and the
		 * only thing that changed is the control word, deeper in it the core has had time to
		 * fetch and fault. Four samples cost four register reads out of a million iterations.
		 */
		if (spins == 0u || spins == 1000u || spins == 100000u || spins == 500000u) {
			/*
			 * The MMU side joins the sample from here on.
			 *
			 * A fetch that leaves the core and is refused by the address space raises a page fault
			 * or a bus error on AS0 and records the offending address; a fetch that never leaves
			 * the core raises neither. Reading both the latch and the fault address at the same
			 * four points is the only way to tell those apart, and the address is what names the
			 * page if one is named.
			 */
			printf("mali: spin %u: status %u job %#x mmu %#x fault %#x at %#llx top %#x\n",
			    spins, st, stat, mrd(kMmuIntRaw),
			    mrd(asReg(0, kAsFaultStatus)),
			    (unsigned long long)gpu_rd64(asReg(0, kAsFaultAddress)),
			    mfg_debug_top());
		}
		if (st == 2u && !sawHalt) {
			sawHalt = true;
			printf("mali: the MCU halted itself at spin %u\n", spins);
		}
		if (st == 3u && !sawFatal) {
			sawFatal = true;
			printf("mali: the MCU reported fatal at spin %u\n", spins);
		}
		if (stat & kJobGlobalIf) {
			booted = true;
			break;
		}
		delay(1);
	}
	printf("mali: the start loop ended after %u spins (halt seen %u, fatal seen %u)\n",
	    spins, sawHalt ? 1u : 0u, sawFatal ? 1u : 0u);
	mali_write_trace("after AUTO (stores captured before AUTO)");

	/*
	 * The version word is the harder of the two proofs, so ask it even when the loop timed out.
	 *
	 * kbase reads glb_control.version immediately after writing AUTO and treats a zero there as
	 * "Firmware may have failed to boot" (mali_kbase_csf_firmware.c, kbase_csf_firmware_init).
	 * That read was previously in the success branch only, which meant a run that never reached
	 * the branch never asked the one question whose answer cannot be forged: the word held our
	 * sentinel going in, and only an executing firmware can put anything else there.
	 */
	if (!booted && gShared != NULL) {
		volatile struct glb_control *check =
		    (volatile struct glb_control *)gShared->mem.kva;

		mem_flush(&gShared->mem);
		if (check->version != kSentinelVersion) {
			printf("mali: the version word changed to %#x -- the firmware ran\n",
			    check->version);
			booted = true;
		}
	}
	if (booted) {
		/*
		 * Clear what the firmware raised, then unmask, which is the other half of the cycle.
		 *
		 * The trace interleaves these: 0x1004 = 0x80000000 immediately after the status reads
		 * back set, and 0x1008 = 0 four records later. Leaving the masks on would hide every
		 * interrupt the firmware goes on to raise, which for a driver that polls means the
		 * difference between a dead MCU and one that is waiting to be asked for something.
		 */
		mwr(kJobIntClear, kJobGlobalIf);
		mwr(kJobIntMask, 0u);
		mwr(kIntClear, kGpuIrqMaskVendor);
		mwr(kIntMask, 0u);
		mwr(kMmuIntClear, 0xffffffffu);
		mwr(kMmuIntMask, 0u);
		printf("mali: interrupts unmasked after the boot\n");
	}
	/*
	 * MCU_STATUS is the authority, not the interrupt.
	 *
	 * Panthor waits on the interrupt alone (panthor_fw.c:1136-1168), which is fine when the job
	 * interrupt is a real GIC line the driver is subscribed to. Here the line is polled, and the
	 * first run of this code came back with status one -- MCU_STATUS_ENABLED, the firmware up and
	 * running -- while JOB_INT_RAWSTAT still read zero, and was reported as "did not start". The
	 * two disagree and the register is the one that cannot lie: a core that had not started would
	 * read DISABLED, and one that had stopped would read HALT or FATAL.
	 *
	 * So the interrupt is treated as a bonus and the status decides. Whatever the firmware does
	 * or does not raise, the global interface is the next thing to read, and its version word is
	 * the real answer to whether it is running: a firmware that never reached its main loop leaves
	 * that at zero.
	 */
	printf("mali: MCU status %u (%s), job irq %#x, gpu irq %#x\n", mrd(kMcuStatus),
	    mrd(kMcuStatus) == 1u ? "enabled" : mrd(kMcuStatus) == 2u ? "halted" :
	    mrd(kMcuStatus) == 3u ? "fatal" : "disabled", mrd(kJobIntStatus), mrd(kIntRaw));
	/*
	 * What the block itself says about the attempt.
	 *
	 * MCU_STATUS only reports the state machine we poked with MCU_CONTROL; it is one if the
	 * request was taken, not proof that a single instruction ran. The registers that can tell the
	 * difference are these, and not one of them had ever been read: an MCU that walked into a
	 * translation fault leaves the address in AS_FAULTADDRESS and the access type in
	 * AS_FAULTSTATUS, and the MMU interrupt latch says the walk failed. Without them "did not
	 * start" and "started and faulted on its first fetch" look identical, which is exactly the
	 * corner this has been stuck in.
	 */
	printf("mali: after the start request: gpu status %#x, gpu irq status %#x,"
	    " job irq status %#x, mmu irq %#x, as0 status %#x faultstatus %#x faultaddress %#llx\n",
	    mrd(kGpuStatus), mrd(kIntStatus), mrd(kJobIntStatus), mrd(kMmuIntRaw),
	    mrd(asReg(0, kAsStatus)), mrd(asReg(0, kAsFaultStatus)),
	    (unsigned long long)(((uint64_t)mrd(asReg(0, kAsFaultAddress) + 4) << 32) |
	    mrd(asReg(0, kAsFaultAddress))));
	/*
	 * The raw status as well, not only the masked one.
	 *
	 * kbase defines the power bits exactly: POWER_CHANGED_SINGLE is bit 9, "a single core has
	 * finished powering up or down", and POWER_CHANGED_ALL is bit 10, "all cores have finished
	 * powering up or down". A raw status carrying bit 9 but not bit 10 is a power controller that
	 * is still working through the request; one carrying neither is a controller that never
	 * started. The masked value cannot separate those two, and the masked value is all that has
	 * been printed here so far -- every run has shown 0x200, which is bit 9 alone, and that is
	 * the shape of a transition that begins and does not finish.
	 */
	printf("mali: gpu irq raw %#x (reset %u single %u all %u caches %u mcu %u);"
	    " shader trans %#x tiler trans %#x l2 trans %#x\n",
	    mrd(kIntRaw), (mrd(kIntRaw) >> 8) & 1u, (mrd(kIntRaw) >> 9) & 1u,
	    (mrd(kIntRaw) >> 10) & 1u, (mrd(kIntRaw) >> 17) & 1u, (mrd(kIntRaw) >> 19) & 1u,
	    mrd(kShaderPwrTrans), mrd(kTilerPwrTrans), mrd(kL2PwrTrans));
	/*
	 * And the block's own fault, which is a different place from the MMU's.
	 *
	 * Every register read so far has been the MMU's opinion of the MCU's memory accesses, and it
	 * has been unanimous and uninformative: no fault, no fault address, no walk error. The GPU has
	 * a second set entirely its own -- GPU_FAULTSTATUS at 0x03c and the 64-bit GPU_FAULTADDRESS
	 * split over 0x040 and 0x044 -- and this driver had never read either. A core that faults on
	 * its own fetch or hits an illegal opcode without going through a page walk lands here
	 * instead, and would look exactly like "did not start" from the MMU's side.
	 *
	 * The READY words are here for the same reason: they are what the vendor polls to decide the
	 * cores are really up, and the shutter trace from the phone shows shader ready going from zero
	 * to the present mask at the moment the firmware took over (record 228, 0x140 = 0x00130013).
	 * Reading them after the start request answers whether the shader side ever powered at all,
	 * which is the one thing this port cannot infer from the control registers.
	 */
	printf("mali: gpu fault %#x at %#llx; shader ready %#x tiler ready %#x l2 ready %#x\n",
	    mrd(kGpuFaultStatus),
	    (unsigned long long)(((uint64_t)mrd(kGpuFaultAddressHi) << 32) |
	    mrd(kGpuFaultAddress)),
	    mrd(kShaderReady), mrd(kTilerReady), mrd(kL2Ready));
	/*
	 * The two counters, which answer the question the power registers cannot.
	 *
	 * GPU_CYCLE_COUNT (0x090) and GPU_TIMESTAMP (0x098) advance with the GPU's own clock. Read
	 * twice a millisecond apart they either move or they do not, and that separates "the block
	 * is clocked and the MCU declined to run" from "the block has no clock at all" without any
	 * further guessing about power domains. It is the cheapest experiment on this list, and the
	 * two outcomes send the work in opposite directions.
	 */
	{
		uint32_t cycle0 = mrd(kGpuCycleCount), stamp0 = mrd(kGpuTimestamp);

		delay(1000);
		printf("mali: clock check: cycle %#x -> %#x, timestamp %#x -> %#x\n", cycle0,
		    mrd(kGpuCycleCount), stamp0, mrd(kGpuTimestamp));
	}
	/*
	 * The status of the MCU is not the status of the boot.
	 *
	 * It was returned as the answer once, and that was wrong: MCU_STATUS one is what Panthor
	 * reads when it reports "Failed to boot MCU" (panthor_fw.c:1150-1161), because the state
	 * machine accepts the AUTO write whether or not anything executes. The only evidence that
	 * a firmware instruction ran is the version word, and the sentinel put there is the
	 * version no firmware would ever write.
	 */
	if (gShared != NULL) {
		uint32_t version = ((volatile struct glb_control *)gShared->mem.kva)->version;

		printf("mali: version word %#x (%s) trial %u\n", version,
		    version == kSentinelVersion ? "untouched; no firmware version published" :
		    version == 0u ? "zero, the firmware never wrote it either" :
		    "written by the firmware", gMaliTrial);
		/* Fixed offsets in allocated DRAM, before DISABLE or any pointer-following. */
		if (gShared->mem.size >= 0x1020u &&
		    gShared->va_end - gShared->va_start >= 0x1020u) {
			volatile struct glb_control *raw =
			    (volatile struct glb_control *)gShared->mem.kva;
			volatile struct csg_control *csg =
			    (volatile struct csg_control *)(gShared->mem.kva + 0x1000u);
			printf("mali: round7 GLB: version %#x in %#x out %#x groups %u stride %#x\n",
			    raw->version, raw->input_va, raw->output_va,
			    raw->group_num, raw->group_stride);
			printf("mali: round7 CSG0: in %#x out %#x streams %u stride %#x\n",
			    csg->input_va, csg->output_va, csg->stream_num, csg->stream_stride);
		}
	}
	/*
	 * The answer is the interrupt, and only the interrupt.
	 *
	 * This used to be `mrd(kMcuStatus) == 1u || booted`, which returned true on every run no
	 * matter what happened: MCU_STATUS one means the state machine accepted the AUTO write,
	 * which it does whether or not a single instruction executes. Panthor reads that register
	 * only after it has already given up, and prints "Failed to boot MCU" with it
	 * (panthor_fw.c:1150-1161) -- in other words, in the vendor's reading, one is the value a
	 * boot that failed shows. Panthor's only success test is JOB_INT_RAWSTAT bit 31
	 * (panthor_fw.c:1109-1110, 1142-1148), and with the interrupt line unsubscribed its one
	 * fallback is the same bit in INT_STAT.
	 *
	 * Returning false when the global interface never raises is the correct failure: it stops
	 * the bring-up at the step that actually failed instead of carrying a dead MCU into the
	 * next hundred lines and blaming the interface for being silent.
	 */
	if (!booted) {
		printf("mali: the global interface never raised; the MCU did not boot\n");
		/*
		 * The SRAM isolation, read where it cannot be lost.
		 *
		 * mali_aoc_up() clears AOC_VGPU_SRAM_ISO_DIN(bit9) and AOC_VGPU_SRAM_LATCH_ENB(bit10)
		 * at the very top of the cold-start path, and the ring buffer on this board has thrown
		 * that line away on every run -- so whether the isolation was ever released has never
		 * been observed, only assumed. The failure path is the right place to look because it
		 * sits in the part of the log that survives.
		 *
		 * This matters more than any of the firmware-side theories. A GPU whose SRAM is held
		 * in isolation still answers every register read (the register file is on the bus, not
		 * in that memory), still reports itself present, still accepts a firmware image into
		 * DRAM -- and still cannot fetch an instruction. That is this board's signature
		 * exactly: MCU_STATUS reads enabled because the state machine took the request, no
		 * translation fault because no fetch was ever attempted, and the sentinel word left
		 * as written because nothing ran.
		 *
		 * Read only. Nothing here changes state.
		 */
		{
			uint32_t iso = reg_rd(gSpm, MALI_SPM_BUCK_ISO);
			uint32_t sema = reg_rd(gSpm, MALI_SPM_SEMA_M4);

			printf("mali: aoc verdict: 0xF30 %#x (srclk iso %u, latch %u) 0x6AC %#x\n",
			    iso, (iso & MALI_AOC_VGPU_SRAM_ISO) ? 1u : 0u,
			    (iso & MALI_AOC_VGPU_LATCH) ? 1u : 0u, sema);
			printf("mali: aoc verdict: %s\n",
			    ((iso & MALI_AOC_VGPU_SRAM_ISO) || (iso & MALI_AOC_VGPU_LATCH))
			    ? "SRAM STILL ISOLATED -- the MCU has no memory to fetch from"
			    : "isolation released as intended; look elsewhere");
		}
	}
	return booted;
}

static bool
global_interface(void)
{
	volatile struct glb_control *control = (volatile struct glb_control *)gShared->mem.kva;
	volatile struct glb_input *input;
	volatile struct glb_output *output;
	uint32_t toggles = kGlbCfgAlloc | kGlbCfgPoweroff | kGlbCfgProgress | kGlbPing;
	uint32_t version;
	unsigned spins;
	bool acked = false;

	version = control->version;
	printf("mali: CSF interface v%u.%u.%u, features %#x, %u groups (stride %#x),"
	    " instrumentation %#x, input %#x output %#x\n", version >> 24, (version >> 16) & 0xff,
	    version & 0xffff, control->features, control->group_num, control->group_stride,
	    control->instr_features, control->input_va, control->output_va);
	if (version == 0) {
		printf("mali: the firmware reports version 0: it is not running\n");
		return false;
	}
	input = (volatile struct glb_input *)shared_at(control->input_va);
	output = (volatile struct glb_output *)shared_at(control->output_va);
	if (input == NULL || output == NULL) {
		printf("mali: the global interface is outside the shared section\n");
		return false;
	}
	if (control->group_num != 0) {
		volatile struct csg_control *csg =
		    (volatile struct csg_control *)(gShared->mem.kva + 0x1000);

		printf("mali: group 0: %u streams (stride %#x), features %#x, suspend %u\n",
		    csg->stream_num, csg->stream_stride, csg->features, csg->suspend_size);
	}
	/*
	 * The four things Panthor does here, in its order: tell it which shader cores it may use,
	 * give it the two timers, and then toggle the configuration requests -- a request is made by
	 * flipping the bit away from the ack, never by setting it.
	 */
	input->core_en_mask = mrd(kShaderPresent);
	input->poweroff_timer = 235;            /* 10 ms in units of 1024 cycles of a 24 MHz clock */
	input->progress_timer = (5u * 500u * 1024u * 1024u) >> 10;
	input->ack_irq_mask = kGlbCfgAlloc | kGlbPing | kGlbCfgProgress | kGlbCfgPoweroff |
	    kGlbIdleEn | kGlbIdle;
	input->req = (input->req & ~toggles) | ((output->ack ^ toggles) & toggles);
	/*
	 * The clean, not just the barrier. The shared section is mapped cacheable on this side, so
	 * the writes above are sitting in the CPU's data cache; a barrier orders them but does not
	 * write them out, and the MCU reads DRAM. Without this the doorbell rings for a request that
	 * is still only in the CPU's cache, which is indistinguishable from a firmware that ignored
	 * it. Panthor does not need this because it maps the shared section uncached, and calls
	 * dma_sync_single_for_device, which on arm64 is exactly this clean.
	 */
	mem_flush(&gShared->mem);
	__asm__ volatile ("dsb sy" ::: "memory");
	mwr(kDoorbell0, 1);
	for (spins = 0; spins < 1000000; spins++) {
		if (((input->req ^ output->ack) & toggles) == 0) {
			acked = true;
			break;
		}
		delay(1);
	}
	printf("mali: global interface config + ping %s (req %#x ack %#x, job irq %#x)\n",
	    acked ? "answered" : "NOT answered", input->req, output->ack, mrd(kJobIntStatus));
	if (!acked) {
		printf("mali: MCU status %u, job irq %#x, mmu irq %#x, gpu irq %#x\n",
		    mrd(kMcuStatus), mrd(kJobIntStatus), mrd(kMmuIntRaw), mrd(kIntRaw));
	}
	return acked;
}

/* ------------------------------------------------------------------ bring-up */

static bool
mali_early_as0(void)
{
	if (!load_firmware()) {
		return false;
	}
	if (!gMaliNoApClear) { mwr(kMmuIntClear, 0xffffffffu); }
	printf("mali: mmu irq %#x after the clear, before AS0 and before the L2\n",
	    mrd(kMmuIntRaw));
	if (!as_enable(0, gMcuRootPa)) {
		return false;
	}
	printf("mali: mmu irq %#x after AS0 took the update, before the L2\n",
	    mrd(kMmuIntRaw));
	mfg_bus_idle("after the early table walk");
	gMaliEarlyAs0 = 1u;
	return true;
}

static void
mali_bring_up(void)
{
	unsigned up;

	gGpu = ml_io_map(MALI_GPU_PHYS, MALI_GPU_SIZE);
	if (gGpu == 0) {
		printf("mali: the GPU block will not map\n");
		return;
	}
	/*
	 * Both blocks the bus protections live in, mapped before anything else and released before
	 * the MCU is asked to do anything. They belong to no power domain this driver manages, so
	 * they are not part of the cold-start branch below.
	 */
	gInfracfg = ml_io_map(MALI_INFRACFG_PHYS, MALI_INFRACFG_SIZE);
	gMfgrpc = ml_io_map(MALI_MFGRPC_PHYS, MALI_MFGRPC_SIZE);
	/*
	 * GPUEB is behind its own clock gate. The phone's gpueb driver opens it through the clock
	 * framework; nothing in this kernel does. Reading a gated MTK peripheral does not return an
	 * error -- the access never completes, the core waits, and the hardware watchdog resets the
	 * board. A diagnostic that produces a boot loop is worse than no diagnostic, so the block is
	 * only mapped when mali_gpueb=1, and only after the gate has been opened by whoever turns it
	 * on. Left off, every read here reports "not mapped" and costs nothing.
	 */
	if (gMaliGpueb) {
		gGpueb = ml_io_map(MALI_GPUEB_PHYS, MALI_GPUEB_SIZE);
	}
	/*
	 * mfgcfg is mapped here rather than inside the cold-start branch, because the interconnect
	 * gates below are opened on both paths. Mapping does not touch the device -- the first read
	 * does -- and every read of it in this function happens after the block has a clock.
	 */
	gMfgcfg = ml_io_map(MALI_MFGCFG_PHYS, MALI_MFGCFG_SIZE);
	if (gInfracfg == 0 || gMfgrpc == 0 || gMfgcfg == 0) {
		printf("mali: infracfg, mfgrpc or mfgcfg will not map\n");
		return;
	}
	bus_protect_release();
	/*
	 * Unconditional. The switch this used to sit behind (mali_bpverify) was passed on the
	 * command line and read back as zero, so the one experiment that answers "does the MD0
	 * clear actually land" never ran. Nothing here is new write traffic: every address is one
	 * bus_protect_release() already touches two lines above.
	 */
	bus_protect_release_verified();

	/*
	 * What the loader has already done, and why nothing here repeats it.
	 *
	 * MTK's fastboot path leaves this board with every irrelevant domain off -- that is the
	 * state the first kernel-side attempt ran into, and why it had to switch the twelve MFG
	 * domains on itself. It is not the state this kernel starts in. The loader paints the
	 * screen, arms the watchdog and jumps to XNU with the GPU powered on the 26 MHz crystal and
	 * its MFGPLL at 300 MHz, because that is the last step of its own probe -- and a reset from
	 * there is either a watchdog fire, which reloads the loader, or a jump into an image that
	 * never returns. Neither of them reaches this function with the GPU cold.
	 *
	 * So the first thing to do is ask the block what state it is in, not to put it in one. If it
	 * answers, the domains and the clock are already right and the SPM and topckgen and MFGPLL
	 * writes below must never happen: they are the three blocks this SoC faults on when they are
	 * written while their own clock is off, and a bus fault with no vector taken stops the core
	 * where it stands, so a mistake there costs a boot loop and says nothing at all.
	 */
	printf("mali: gpu_id as the loader left it %#x\n", mrd(kGpuId));
	if (mrd(kGpuId) == MALI_GPU_ID && mrd(kCsfId) == 0) {
		printf("mali: the firmware has not been loaded yet, as expected\n");
	}
	if (mrd(kGpuId) == 0 || mrd(kGpuId) == 0xffffffffu) {
		/*
		 * Cold. Only now is it worth touching the power and clock blocks, and only the ones
		 * that were proved to work from the loader.
		 */
		gSpm = ml_io_map(MALI_SPM_PHYS, MALI_SPM_SIZE);
		gTopckgen = ml_io_map(MALI_TOPCKGEN_PHYS, MALI_TOPCKGEN_SIZE);
		gMfgpll = ml_io_map(MALI_MFGPLL_PHYS, MALI_MFGPLL_SIZE);
		if (gSpm == 0 || gTopckgen == 0 || gMfgpll == 0) {
			printf("mali: the SPM, topckgen or the MFGPLL will not map\n");
			return;
		}
		/*
		 * The AOC handshake first, as the vendor has it.
		 *
		 * This is the step the loader has been doing on this kernel's behalf, and doing it here
		 * instead is the whole point of this run: Android reaches the MCU start from a block that
		 * has never been powered, this driver never has, and the only way to know whether that
		 * matters is to make this driver walk the same path.
		 */
		mali_aoc_up();
		/*
		 * The domains first, and the clock after them -- and the reason is embarrassing, because
		 * the previous version of this comment argued the opposite and was wrong.
		 *
		 * scpsys_power_on() does enable clk before PWR_ON_BIT (mtk-scpsys.c:549-570), so moving
		 * clock_26m() ahead of power_on() looked like following the vendor. It is not the same
		 * thing: the clock that function enables is scpd->clk, the domain's own gate, and all
		 * twelve MFG domains have an empty basic_clk_name (mtk-scpsys-mt6895.c:449-556) so there
		 * is no such gate to enable. clock_26m() writes topckgen *and mfgcfg*, and mfgcfg lives
		 * at 0x13FBF000 -- inside mfg1. Writing it before mfg1 has a rail is the one thing this
		 * driver's own comment at the top warns about: a bus fault with no vector taken stops the
		 * core where it stands, costs a boot loop, and says nothing. That is exactly what
		 * happened: the run that had this ordering never came back to Android at all.
		 *
		 * So the order stays as it was. The acknowledge fix in domain_on() is independent of it
		 * and is the part that mattered.
		 */
		up = power_on();
		printf("mali: %u of twelve domains answered\n", up);
		if ((reg_rd(gSpm, MALI_MFG1) & SPM_STA_MASK) != SPM_STA_MASK) {
			printf("mali: mfg1 never came up; stopping here\n");
			return;
		}
		/*
		 * Release the AXI protections again, and this time where the vendor does it.
		 *
		 * bus_protect_release() already ran at the top of this function, before any domain had a
		 * rail. The status word now says that was too early: on this board
		 * MD_MFGSYS_PROTECT_EN_STA_0 (0x10001CAC) still reads 0xFFFFFFEF, i.e. bits 0..3 -- the
		 * whole MT6895_TOP_AXI_PROT_EN_MD0_MFG1 mask -- are still set, while emisys0 and emisys1
		 * read their masked bits as zero. The stock phone reads 0xFFFFFFE0 in that same register,
		 * with the low four bits clear.
		 *
		 * The vendor's order says the same thing. scpsys_power_on() only reaches
		 * scpsys_bus_protect_disable() after PWR_RST_B, subsys_clk and scpsys_sram_enable()
		 * (mtk-scpsys.c:44-67 of that function, scpsys_bus_protect_disable() being the last
		 * call), and scpsys_power_off() re-arms it before dropping the rail. The protection is
		 * therefore armed for exactly as long as the domain is down, and clearing it while the
		 * domain is still down is a write into a block that will re-assert it on the way up.
		 *
		 * That matters here because md0 is the MFG-to-main-memory port. A GPU whose reads are
		 * still behind that protection issues an AXI transaction that is never answered -- no
		 * data, no error, no fault status -- which is precisely the shape of the failure this
		 * driver has been chasing: debug_top goes to busy and never returns, MCU_STATUS stays at
		 * 1, AS_FAULTSTATUS stays 0, and the firmware's version word is never written.
		 */
		bus_protect_release();
		/*
		 * The stack rail, which the vendor enables before the domains and this driver never has.
		 *
		 * POWER_CHANGED_SINGLE without POWER_CHANGED_ALL is the signature of a rail that is
		 * missing rather than of a request that was mis-written, and the shader stacks are the
		 * four domains that never answer on this board. Both point at the same supply. Putting
		 * it up here -- after the domains are known good, before the block is asked to settle --
		 * keeps the change isolated to one thing.
		 */
		mali_vstack_rail();
		/* The crystal first, so the block always has a clock to answer with. */
		clock_26m();
		/*
		 * mali_pdcauto=1: the PDC handover, now that the gate is open.
		 *
		 * This is the run the vendor makes: mfg1 by hand, then MFG_ACTIVE_POWER_CON and the
		 * controller does the rest. Placed here because the first attempt wrote all of it
		 * behind a shut BG3D gate and every store came back zero.
		 */
		if (gMaliPdcAuto) {
			unsigned pdcup = 0;
			unsigned pdi;
			uint32_t gf;

			printf("mali: pdc auto: %u pdc words written (gate open)\n", pdc_enable());
			pdc_report();
			/* The controller is hardware, not a register write; give it 200 ms. */
			delay(200000);
			gf = reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF) & 0x3ffcu;
			printf("mali: pdc auto: 0xF3C %#x (want 0x3ffe) 0xF40 %#x 0xF34 %#x\n",
			    gf, reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF + 4u) & 0x3ffcu,
			    reg_rd(gSpm, MALI_SPM_PWR_STATUS) & MALI_MFG_PWR_MASK);
			for (pdi = 0; pdi < 12u; pdi++) {
				if (gf & (1u << (pdi + 2))) {
					pdcup++;
				}
			}
			printf("mali: pdc auto: %u of twelve domains up via PDC\n", pdcup);
			/*
			 * Raw dump, no mask. 0x3FFC is the vendor's MFG_1_12_PWR_MASK; anything above
			 * the low bits of 0xF3C is a different field and is worth seeing rather than
			 * masking away. Also re-read each PDC enable to find which ones the hardware
			 * cleared again (st6 and sc9 were 0 in trial 602 straight after being written).
			 */
			printf("mali: pwr raw: 0xF3C %#x 0xF40 %#x 0xF34 %#x 0xF38 %#x\n",
			    reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF),
			    reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF + 4u),
			    reg_rd(gSpm, MALI_SPM_PWR_STATUS),
			    reg_rd(gSpm, MALI_SPM_PWR_STATUS2));
			{
				char line[160];
				unsigned off = 0;

				line[0] = 0;
				for (pdi = 0; pdi < MALI_PDC_CORES; pdi++) {
					off += scnprintf(line + off, sizeof(line) - off, " sc%u=%#x",
					    pdi, reg_rd(gMfgcfg, MALI_PDC_SC_BASE + pdi * MALI_PDC_SC_STEP) & 1u);
					if (off > sizeof(line) - 20) break;
				}
				printf("mali: pdc readback:%s\n", line);
			}
			printf("mali: shader/bus: present %#x ready %#x pwron %#x pwrtran %#x"
			    " l2pwron %#x l2active %#x\n",
			    mrd(kShaderPresent), mrd(kShaderReady), mrd(kShaderPwrOn),
			    mrd(kShaderPwrTrans), mrd(kL2PwrOn), mrd(kL2PwrActive));
		}
		/*
		 * PDC after the clock, never before.
		 *
		 * mfgcfg's own gate is BG3D, and clock_26m() is what opens it. Written while the gate
		 * is shut, all seventeen stores read back as zero -- which is exactly what the first
		 * run of this did, because the call was placed one line too early. The vendor's order
		 * says the same thing from the other side: clock_control is step four of nine and
		 * pdc_control is step seven (gpufreq_mt6895.c:802).
		 */
		printf("mali: pdc enabled on %u registers\n", pdc_enable());
		pdc_report();
		printf("mali: after pdc: mfg5 %#x mfg6 %#x mfg10 %#x mfg12 %#x\n",
		    reg_rd(gSpm, MALI_MFG5), reg_rd(gSpm, MALI_MFG6),
		    reg_rd(gSpm, MALI_MFG10), reg_rd(gSpm, MALI_MFG12));
		/*
		 * PWR_STATUS, which is the register the vendor actually reads.
		 *
		 * All of the above is the control word talking about itself: a domain can answer
		 * "request taken, power pending" forever, and the bit that says whether the rail is
		 * really live is elsewhere. scpsys keeps one copy at 0xF34/0xF38 and gpufreq keeps
		 * another at 0xF3C/0xF40 (they are eight bytes apart because the two consumers were
		 * written separately); MFG_1_12_PWR_MASK is bits 2..13 of the second one, one bit per
		 * domain. This is the same register the vendor's own error message prints as
		 * "incorrect MFG1-12 power off status" (gpufreq_mt6895.c:3586).
		 */
		printf("mali: pwr status %#x / %#x (scpsys 0xF34/0xF38), gpufreq %#x (0xF3C); want %#x\n",
		    reg_rd(gSpm, MALI_SPM_PWR_STATUS) & MALI_MFG_PWR_MASK,
		    reg_rd(gSpm, MALI_SPM_PWR_STATUS2) & MALI_MFG_PWR_MASK,
		    reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF) & MALI_MFG_PWR_MASK, MALI_MFG_PWR_MASK);
		printf("mali: gpu_id %#x l2 %#x shader %#x\n", mrd(kGpuId), mrd(kL2Features),
		    mrd(kShaderPresent));
		if (mrd(kGpuId) == 0 || mrd(kGpuId) == 0xffffffffu) {
			printf("mali: the block still does not answer, so it is not a clock problem\n");
			return;
		}
		/* Then the PLL beside it, and the mux moved once it is locked. */
		pll_300mhz();
		/*
		 * The timestamp counter, which the vendor enables from the same block and which has read
		 * zero on every run here. It is not part of any start sequence -- nothing waits on it --
		 * so this is a measurement, not a step: a counter that starts moving proves the block's
		 * internal clock reaches the timestamp logic, and one that stays at zero while the cycle
		 * counter runs says the two paths are fed differently, which would be worth knowing
		 * before blaming the MCU for not fetching.
		 */
		mfg_set_timestamp();
	} else {
		printf("mali: the loader left it powered and clocked; not touching the SPM\n");
	}
	/*
	 * The last three steps of the vendor's nine, in the vendor's order.
	 *
	 * __gpufreq_power_control() runs acp at :806, hwdcm at :810 and gpm at :815, and this driver
	 * had the first two the other way round and the third not at all. That ordering was never
	 * argued for; it accumulated. The vendor's is the one that leaves a phone with a working GPU,
	 * so it is the one to follow, and putting the protector last matches the sequence the
	 * firmware itself uses in its own power path.
	 *
	 * All three are outside the cold-start test. The loader powers the block and leaves it
	 * running on some boots and not others, and none of these three steps is conditioned on which
	 * happened there: a gate that was opened is opened again harmlessly, and a protector that was
	 * never armed is armed.
	 */
	mali_acp_up();
	mfg_master_gates("before the firmware");
	if (gMaliGpm) {
		mfg_gpm_arm("before the firmware");
	} else {
		printf("mali: i2m protector skipped (mali_gpm=0)\n");
	}
	mfg_ostd_probe("before the firmware");
	gMaliProgress = 1;

	printf("mali: this is GPU %#x, CSF %#x\n", mrd(kGpuId), mrd(kCsfId));
	printf("mali: mmu %#x (va %u bits, pa %u bits) address spaces %#x\n", mrd(kMmuFeatures),
	    mrd(kMmuFeatures) & 0xffu, (mrd(kMmuFeatures) >> 8) & 0xffu, mrd(kAsPresent));
	printf("mali: shader %#x tiler %#x l2 %#x stack %#x thread_max %#x\n",
	    mrd(kShaderPresent), mrd(kTilerPresent), mrd(kL2Present), mrd(kStackPresent),
	    mrd(kThreadMax));

	if (gMaliProgress != 1) {
		return;
	}
	if (gMaliLevel <= 1) {
		printf("mali: stopping here because mali=1: the block is powered, clocked and identified\n");
		return;
	}

	/*
	 * ---- M1, one step at a time
	 *
	 * mali=2 asks for all of it, but every level from three up is also a stopping point of its
	 * own: mali=3 loads the firmware and stops, mali=4 also enables address space 0, mali=5 also
	 * starts the MCU. That is not a convenience. The first attempt at mali=2 reset the board
	 * three times in a row and left no log at all -- a write into a block whose clock is not
	 * running is a bus fault on this SoC, and a bus fault with no vector taken is the core
	 * stopping where it stands, so the kernel never gets to say a word and the loader's own
	 * logbook stays empty. A level that ends by itself returns to Android and its log is in the
	 * logbook, so the highest level that produces one is the last step that is known good and the
	 * next one is where it dies.
	 *
	 * The steps are ordered by how much of the GPU they touch, because that is the risk: loading
	 * the firmware only writes DRAM and the MCU's page tables, enabling the address space is the
	 * first write into the GPU, and starting the MCU is the first write that makes the block act.
	 */
	/*
	 * The order below is the phone's ordinary boot, in the order its own driver performs it. The
	 * trace is 2460 records long and covers six complete start attempts; the shape of each one is
	 * the same and it is the shape written here:
	 *
	 *	  0.. 62  program all eight address spaces	(as_park_unused + as_enable)
	 *	 63.. 68  clear and mask the three interrupt sets
	 *	 69.. 90  arm the IPA block, read its eight counters	(gpu_ipa_arm)
	 *	 98      L2_PWRON = 1					(gpu_l2_power_on)
	 *	 99..124 poll PWRTRANS down, then READY up
	 *	136      MCU_CONTROL = AUTO				(start_mcu)
	 *	163      JOB_INT_STATUS = 0x80000000			(the boot)
	 *	174..    ring the global doorbell
	 *	386      unmask the job interrupts
	 *
	 * What is deliberately absent from it: GPU_CMD (0x030), the power key (0x050), the four
	 * configuration registers (0x0f00..0x0f0c) and the coherency protocol (0x304). Not one of
	 * those appears anywhere in those 2460 records. They come from the other trace -- the five
	 * hundred records taken after the driver's reset node was written -- and this driver had been
	 * following that one, on the reasoning that a block handed over by a bootloader is closer to a
	 * reset than to a warm start. The reasoning produced a sequence no working machine ever runs.
	 *
	 * The reset path is still available behind mali_cold=0, which is the only way to compare two
	 * runs that differ in exactly one decision.
	 */
	if (!gMaliCold) {
		if (!gpu_soft_reset()) {
			/* A block that will not reset is not a block to write firmware into. */
			return;
		}
		gpu_hw_issues();
	}
	/*
	 * The seven unused address spaces go first, before the interrupt masks.
	 *
	 * The trace programs all eight before it touches a mask (records 0 to 62), and the order is
	 * not cosmetic: a space left at whatever the block powered up with still answers walks, and a
	 * walk into one of them raises an interrupt. Parking them while the lines are still open means
	 * anything that does leak through is visible; parking them after would hide it under the mask
	 * this driver is about to install.
	 *
	 * AS0 is not among them -- it needs the firmware's table, so it comes after load_firmware().
	 */
	/*
	 * Before anything is programmed: is the coprocessor that owns this block's power still
	 * running? Asked at the earliest point this driver touches the block, so the answer is not
	 * contaminated by anything it does later.
	 */
	gpueb_watch("before the address spaces");
	gmpu_probe("before the address spaces");
	if (gMaliPreAs0 && !gMaliAs0 && gMaliLevel > 4 && gMaliAs0First) {
		if (!mali_early_as0()) {
			return;
		}
	}
	as_park_unused();
	/*
	 * The masks go on before the L2 is asked to come up, not inside start_mcu().
	 *
	 * The phone's order is address spaces, then masks, then IPA, then L2, then the control word --
	 * and the masks are in the middle for a reason. Masking does not stop an event being latched
	 * in GPU_IRQ_RAWSTAT, it only keeps it out of the masked view, so a raw read during the power
	 * transition sees POWER_CHANGED_ALL whether or not the line is armed. Having them in place
	 * before the L2 request is what makes the settle check in gpu_l2_power_on() read our own
	 * transition rather than leftovers.
	 */
	gpu_irq_mask_prestart();
	/*
	 * The IPA block, between the interrupt masks and the L2 request, which is where the trace has
	 * it (records 69 to 90). Its counters only move while the MCU executes, so arming it here
	 * gives the run a second answer to the question MCU_STATUS cannot answer.
	 */
	gpu_ipa_arm();
	/*
	 * mali_preas0=1 puts AS0's update where the phone's trace has it: before the L2 request.
	 *
	 * Record seven of that trace is AS0's AS_COMMAND=UPDATE and record ninety-eight is
	 * L2_PWRON. This driver has always done it the other way round, and after the page tables
	 * and the walk were cleared one by one that order is the only structural difference left.
	 * The parked spaces AS2..AS7 are written before the L2 comes up and raise no interrupt at
	 * all; AS0 is written after it and raises MMU_BUS_ERROR(0) every single run. Two facts, one
	 * variable, never separated.
	 */
	if (gMaliPreAs0 && !gMaliAs0 && gMaliLevel > 4 && !gMaliAs0First) {
		if (!mali_early_as0()) {
			return;
		}
	}
	if (!gpu_l2_power_on()) {
		return;
	}
	if (!gMaliCold) {
		/*
		 * The cache flush, which the cold path does not do.
		 *
		 * It reads well and it is the one piece of the reset path that sounds like it belongs
		 * here: the firmware is written into DRAM by the CPU and read by the MCU through the L2, so
		 * anything stale in the cache would be fetched instead. But the trace says kbase does no
		 * such thing on a boot, and there is a reason it does not have to -- the flush is issued
		 * as part of its reset sequence, before the trace window opens, and a block that has just
		 * come out of reset has nothing in its caches to flush. Ours has been powered by the
		 * loader, so the argument is not free; the way to settle it is one run each way, which is
		 * what mali_cold=0 is for.
		 */
		if (!gpu_flush_caches()) {
			return;
		}
	}
	/*
	 * The image before the address spaces, because AS0's translation table is the one this builds.
	 *
	 * The trace programmes the eight address spaces first, at records zero through sixty-two, a
	 * hundred and thirty-six records before the MCU is asked to start. It can, because kbase maps
	 * the firmware through a kernel mapping of its own and only hands the MCU a table once the
	 * sections exist. This driver builds the table and the sections in the same function, so the
	 * image has to be in before there is a root to point at -- and the L2 has to be up before
	 * that, because the MCU reads the image through it.
	 */
	if (!gMaliEarlyAs0 && !load_firmware()) {
		return;
	}
	printf("mali: firmware loaded; %s\n",
	    gMaliLevel <= 3 ? "stopping here because mali=3" : "enabling the address spaces");
	if (gMaliLevel <= 3) {
		mcu_view_sample("firmware loaded, MCU not started");
		mali_write_trace("mali=3 stop");
		gMaliProgress = 2;
		return;
	}
	/*
	 * Then AS0, last of the eight, because its table is the one load_firmware() just built.
	 *
	 * The other seven were parked before the masks went on; this one could not be, because the
	 * root it needs does not exist until the images are mapped.
	 */
	/*
	 * Whether the block's own AXI channel is idle, read either side of the walk.
	 *
	 * This is the one observation nothing in the driver has made. Every other explanation for an
	 * MCU that reports ENABLED and executes nothing has been eliminated by experiment: power and
	 * clocks settle (POWER_CHANGED_ALL is raised), the firmware image and its page tables are
	 * accepted, AS0 takes the update, the bus protections are released, and a hand-written
	 * thirty-two byte Cortex-M7 stub at the same address does not run either. What is left is
	 * that the fetch itself never leaves the core -- and a channel that never goes idle is a
	 * mechanical reason for exactly that, with the same signature: no fault, no progress.
	 *
	 * Taken twice because a bus that goes busy during the table walk and never comes back is a
	 * different finding from one that was busy before the walk even began.
	 */
	mfg_bus_idle("before the table walk");
	/*
	 * Clear the MMU latch immediately before the table walk, and read it immediately after.
	 *
	 * Without this the run only ever reports mmu irq 0x10000 as a standing value by the time the
	 * start request is about to be written, and that cannot distinguish a fault raised by this
	 * walk from one left behind by the firmware mapping or by the loader. Writing the clear and
	 * reading back right away turns the question into a single step: whatever is latched after
	 * the clear was raised by as_enable() and by nothing else.
	 */
	if (gMaliAs0) {
		/*
		 * The control run: AS0 gets exactly what AS2..AS7 got, and nothing else changes.
		 *
		 * Written as its own branch rather than as a flag inside as_enable() so that the three
		 * register writes and the one UPDATE are textually the same shape as the parking loop
		 * that has never raised an error. If this is silent and the real path is not, the
		 * difference between the two is the table and only the table.
		 */
		mwr(kMmuIntClear, 0xffffffffu);
		printf("mali: mmu irq %#x just after the clear, AS0 parked as a control\n",
		    mrd(kMmuIntRaw));
		gpu_wr64(asReg(0, kAsTranstab), 0);
		gpu_wr64(asReg(0, kAsMemattr), 0);
		gpu_wr64(asReg(0, kAsTranscfg), kAsTranscfgUnmapped);
		(void)as_command(0, kAsCmdUpdate);
		printf("mali: AS0 parked: transtab %#llx memattr %#llx transcfg %#llx mmu irq %#x\n",
		    (unsigned long long)gpu_rd64(asReg(0, kAsTranstab)),
		    (unsigned long long)gpu_rd64(asReg(0, kAsMemattr)),
		    (unsigned long long)gpu_rd64(asReg(0, kAsTranscfg)), mrd(kMmuIntRaw));
		mfg_bus_idle("after the parked AS0");
		gMaliProgress = 2;
		return;
	}
	if (!gMaliEarlyAs0) {
		mwr(kMmuIntClear, 0xffffffffu);
		printf("mali: mmu irq %#x just after the clear, before AS0\n", mrd(kMmuIntRaw));
		if (!as_enable(0, gMcuRootPa)) {
			return;
		}
		printf("mali: mmu irq %#x just after AS0 took the update\n", mrd(kMmuIntRaw));
		mfg_bus_idle("after the table walk");
	}
	/*
	 * Read the MMU latch before the start request, not only after it.
	 *
	 * The first run that showed anything at all had mmu irq 0x10000 -- MMU_BUS_ERROR for address
	 * space 0, a walk the bus refused -- while AS_FAULTSTATUS stayed zero, which is not a page
	 * fault. Whether that latch was set by the walker itself or by something earlier decides
	 * which half of the address space setup to look at, and a single reading taken afterwards
	 * cannot tell the two apart.
	 */
	printf("mali: mmu irq %#x before the start request (page faults %#x, request completions %#x)\n",
	    mrd(kMmuIntRaw), mrd(kMmuIntRaw) & 0xffu, (mrd(kMmuIntRaw) >> 16) & 0xffu);
	printf("mali: address space 0 is live; %s\n",
	    gMaliLevel <= 4 ? "stopping here because mali=4" : "starting the MCU");
	if (gMaliLevel <= 4) {
		gMaliProgress = 2;
		return;
	}
	/*
	 * The witness runs instead of the firmware, not alongside it, and only when asked for: the
	 * whole point is that a run with it says something a run without it cannot, so the two
	 * results are only comparable while the rest of the path stays identical. Everything below
	 * this point -- the MCU control word, the doorbell -- is the same code either way.
	 */
	if (gMaliWitness) {
		bolt_witness();
	}
	if (!start_mcu()) {
		/*
		 * Read the witness even though the start failed, and read it here rather than after the
		 * success path: a failed start is the case the witness exists for, so returning first
		 * would throw away the only run that has something to say.
		 */
		gpu_ipa_report("after the failed start");
		if (gMaliWitness) {
			read_witness();
		}
		/*
		 * One last look at the two words that say whether the block considers its windows open.
		 *
		 * GPU_STATUS bit 1 is PWR_ACTIVE and bit 0 is ACTIVE. Neither driver waits on the first --
		 * panthor defines it and never reads it -- so its value here has never been recorded on
		 * this board, and a block that reports ACTIVE while declining to say PWR_ACTIVE is
		 * describing exactly the state the MCU is stuck in. The SPM control word for mfg1 is
		 * printed beside it because that is the same instant's answer to the same question from
		 * the power controller's side.
		 */
		/*
		 * GPU_STATUS as kbase itself defines it, not as panthor does.
		 *
		 * panthor's header has a PWR_ACTIVE in bit 1 and this driver had been reading it, which
		 * is why a run reported "power active 0" with nothing to compare it against. The copy of
		 * the driver MediaTek ships for this very part -- mali-r32p1 under the vendor tree -- has
		 * no such bit: its GPU_STATUS comment block lists only PRFCNT_ACTIVE (bit 2),
		 * CYCLE_COUNT_ACTIVE (bit 6) and PROTECTED_MODE_ACTIVE (bit 7). Bit 6 is the one that
		 * matters here. It says the block's cycle counter is running, and a counter that runs is
		 * a block with a live clock that the GPU itself admits to.
		 */
		printf("mali: gpu status %#x (active %u, prfcnt %u, cycle count %u, protected %u);"
		    " mfg1 %#x\n", mrd(kGpuStatus),
		    mrd(kGpuStatus) & 1u,
		    (mrd(kGpuStatus) >> 2) & 1u,
		    (mrd(kGpuStatus) >> 6) & 1u,
		    (mrd(kGpuStatus) >> 7) & 1u,
		    reg_rd(gSpm, MALI_MFG1));
		printf("mali: gpu fault %#x at %#x %#x; timestamp %#x\n",
		    mrd(kGpuFaultStatus), mrd(kGpuFaultAddress), mrd(kGpuFaultAddressHi),
		    reg_rd(gMfgcfg, MALI_MFG_TIMESTAMP));
		/*
		 * And one more look at the channel, now that the core has been told to run and did not.
		 *
		 * A bus that went busy between the walk and here is the core pulling something, even if it
		 * never gets far enough to write a marker; a bus that stayed idle throughout says the core
		 * never issued an access at all. Those two failures need different next steps, and this is
		 * the cheapest way to tell them apart.
		 */
		mfg_bus_idle("after the failed start");
		/*
		 * Then take the core away and look at the channel again. This is the causal half.
		 *
		 * A channel that was busy and goes idle the moment the MCU is disabled was being held by
		 * the MCU; a channel that stays busy with the core switched off is being held by
		 * something else entirely -- the firmware coprocessor, a display master, or a debug
		 * multiplexer whose value simply has nothing to do with the core. The earlier reading
		 * cannot tell those apart, and they call for opposite next steps: chase the core's own
		 * state, or stop blaming it.
		 *
		 * MCU_CONTROL = 0 is DISABLE, the value kbase itself writes at the end of every round on
		 * the phone (kbase_csf_firmware_trigger_mcu_halt -> MCU_CONTROL_DISABLE). Nothing is
		 * resumed by this; the run has already failed and the loader takes the block down before
		 * Android comes back.
		 */
		mwr(kMcuControl, kMcuControlDisable);
		delay(2000);
		printf("mali: MCU control written %u, status now %u, control reads %u\n",
		    kMcuControlDisable, mrd(kMcuStatus), mrd(kMcuControl));
		mcu_window_dump("after the core was switched off");
		mcu_view_sample("after DISABLE");
		mfg_bus_idle("after the core was switched off");
		mfg_bus_watch("after the core was switched off");
		/*
		 * And the other candidate owner of the bus, asked at the moment when it would have to
		 * be the one holding it. If GPUEB is still kicking its watchdog while the MFG channel
		 * reads non-idle and the core has done nothing, the two facts are about each other.
		 */
		gpueb_watch("after the failed start");
		gmpu_probe("after the failed start");
		cpe_probe();
		/* Read fixed offsets in our allocated DRAM only. Do not follow firmware pointers
		 * or ring a doorbell when the firmware has not announced a successful boot. */
		printf("mali: shared probe trial %u: failed-start tail, level %u; global_interface skipped\n",
		    gMaliTrial, gMaliLevel);
		if (gShared != NULL && gShared->va_end >= gShared->va_start &&
		    gShared->mem.kva != 0 && gShared->mem.size >= 0x1020u &&
		    gShared->va_end - gShared->va_start >= 0x1020u) {
			volatile struct glb_control *raw =
			    (volatile struct glb_control *)gShared->mem.kva;
			volatile struct csg_control *csg =
			    (volatile struct csg_control *)(gShared->mem.kva + 0x1000u);
			uint64_t lo = gShared->va_start;
			uint64_t hi = gShared->va_end;
			uint32_t version = raw->version;
			uint32_t input_va = raw->input_va;
			uint32_t output_va = raw->output_va;
			uint32_t csg_in = csg->input_va;
			uint32_t csg_out = csg->output_va;
			bool in_ok = input_va >= lo && input_va < hi &&
			    sizeof(struct glb_input) <= hi - input_va;
			bool out_ok = output_va >= lo && output_va < hi &&
			    sizeof(struct glb_output) <= hi - output_va;

			printf("mali: shared probe: VA %#llx-%#llx size %#lx kva %#llx pa0 %#llx pa1000 %#llx\n",
			    (unsigned long long)lo, (unsigned long long)hi,
			    (unsigned long)gShared->mem.size, (unsigned long long)gShared->mem.kva,
			    (unsigned long long)mem_pa(&gShared->mem, 0),
			    (unsigned long long)mem_pa(&gShared->mem, 0x1000u));
			printf("mali: shared probe GLB: version %#x features %#x input_va %#x output_va %#x\n",
			    version, raw->features, input_va, output_va);
			printf("mali: shared probe GLB: groups %u stride %#x perfcnt %#x instr %#x input_span_ok %u output_span_ok %u\n",
			    raw->group_num, raw->group_stride, raw->perfcnt_size, raw->instr_features,
			    in_ok ? 1u : 0u, out_ok ? 1u : 0u);
			printf("mali: shared probe CSG0: features %#x input_va %#x output_va %#x suspend %#x protected_suspend %#x\n",
			    csg->features, csg_in, csg_out, csg->suspend_size, csg->protm_suspend_size);
			printf("mali: shared probe CSG0: streams %u stride %#x input_in_section %u output_in_section %u\n",
			    csg->stream_num, csg->stream_stride,
			    csg_in >= lo && csg_in < hi ? 1u : 0u,
			    csg_out >= lo && csg_out < hi ? 1u : 0u);
		} else {
			printf("mali: shared probe: fixed GLB/CSG ranges unavailable; no reads performed\n");
		}
		printf("mali: shared probe trial %u: completed; returning before global_interface\n",
		    gMaliTrial);
		return;
	}
	if (gMaliWitness) {
		/*
		 * Read it before anything else can overwrite it: global_interface() writes the input
		 * block and would bury a marker that landed there, and the address space and control
		 * words are all the witness needs anyway.
		 */
		read_witness();
	}
	gpu_ipa_report("after the start");
	printf("mali: the MCU is up; %s\n",
	    gMaliLevel <= 5 ? "stopping here because mali=5" : "talking to the global interface");
	if (gMaliLevel <= 5) {
		gMaliProgress = 2;
		return;
	}
	if (!global_interface()) {
		return;
	}
	gMaliProgress = 2;
	gMaliDone = true;
	printf("mali: M1 done: the firmware is running and the global interface answered\n");
}

static void
mali_thread(void *parameter, wait_result_t result)
{
	(void)parameter;
	(void)result;

	delay(2000 * 1000);                     /* let the rest of the machine settle first */
	mali_bring_up();
}

void
hakios_mali610_init(void)
{
	unsigned which = 0;
	thread_t thread;

	if (!PE_parse_boot_argn("mali", &which, sizeof(which)) || which == 0) {
		return;
	}
	/*
	 * Six is the highest step there is: the global interface, its configuration and a ping. The
	 * clamp used to be five, which silently turned a request for six into five, so a run that was
	 * meant to reach the interface stopped at "the MCU is up" and said so about a step nobody had
	 * asked for.
	 */
	if (which > 6) {
		which = 6;
	}
	gMaliLevel = which;
	/*
	 * mali_asn=0 leaves GPU_L2_CONFIG and the three hash words exactly as the board came up,
	 * which is what every run before this one did. It exists so that one image can answer the
	 * question from both sides: on, off, on again, without a rebuild between.
	 */
	{
		unsigned asn = 1;

		if (PE_parse_boot_argn("mali_asn", &asn, sizeof(asn))) {
			gMaliAsnHash = asn ? 1u : 0u;
		}
	}
	/*
	 * mali_witness=1 swaps the 32-byte stub in for the firmware's entry point. It is a boot-arg
	 * rather than a build switch on purpose: the two runs have to be the same binary for the
	 * comparison to mean anything, and the phone is the only place either result exists.
	 */
	{
		unsigned witness = 0;

		if (PE_parse_boot_argn("mali_witness", &witness, sizeof(witness))) {
			gMaliWitness = witness ? 1u : 0u;
		}
	}
	{
		int gpueb = 0;

		if (PE_parse_boot_argn("mali_gpueb", &gpueb, sizeof(gpueb))) {
			gMaliGpueb = gpueb ? 1u : 0u;
		}
	}
	{
		int rpc = 0;
		int as0 = 0;
		int preas0 = 0;
		int as0first = 0;

		if (PE_parse_boot_argn("mali_as0", &as0, sizeof(as0))) {
			gMaliAs0 = (as0 > 0) ? 1u : 0u;
		}
		if (PE_parse_boot_argn("mali_preas0", &preas0, sizeof(preas0))) {
			gMaliPreAs0 = (preas0 > 0) ? 1u : 0u;
		}
		if (PE_parse_boot_argn("mali_as0first", &as0first, sizeof(as0first))) {
			gMaliAs0First = (as0first > 0) ? 1u : 0u;
		}
		if (PE_parse_boot_argn("mali_rpc", &rpc, sizeof(rpc))) {
			gMaliRpc = (rpc > 0) ? (unsigned)rpc : 0u;
		}
	}
	/*
	 * mali_cold=0 puts back the writes the phone's reset path makes and its boot path does not,
	 * which is how the two sequences are compared without rebuilding: GPU_CMD, the power key, the
	 * four configuration registers and the coherency protocol. Default one, the boot path.
	 */
	{
		unsigned cold = 1;

		if (PE_parse_boot_argn("mali_cold", &cold, sizeof(cold))) {
			gMaliCold = cold ? 1u : 0u;
		}
	}
	/*
	 * mali_low=0 puts the MCU's memory back above four gigabytes.
	 *
	 * The interesting reading this run is built around: the walker reported
	 * MMU_BUS_ERROR(0) on AS_COMMAND=UPDATE and on nothing before it, with the root table at
	 * 8.5 GiB. A walk that cannot read its own root produces exactly that signature. One image
	 * with a boot-arg answers both sides of the question.
	 */
	{
		unsigned low = 1;

		if (PE_parse_boot_argn("mali_low", &low, sizeof(low))) {
			gMaliLowMem = low ? 1u : 0u;
		}
	}
	/*
	 * mali_gpm=0 skips the I2M protector, mali_ostd=1 samples the outstanding counter.
	 *
	 * Both are new in the run that adds the protector: one turns the new step off again without a
	 * rebuild, the other turns on a register read that may hang the block, so neither can be a
	 * default.
	 */
	{
		unsigned gpm = 1;
		unsigned ostd = 0;

		if (PE_parse_boot_argn("mali_gpm", &gpm, sizeof(gpm))) {
			gMaliGpm = gpm ? 1u : 0u;
		}
		if (PE_parse_boot_argn("mali_ostd", &ostd, sizeof(ostd))) {
			gMaliOstd = ostd ? 1u : 0u;
		}
	}
	{
		unsigned sema = 0;
		unsigned pdcauto = 0;

		if (PE_parse_boot_argn("mali_pdcauto", &pdcauto, sizeof(pdcauto))) {
			gMaliPdcAuto = pdcauto ? 1u : 0u;
		}

		if (PE_parse_boot_argn("mali_sema", &sema, sizeof(sema))) {
			gMaliSema = sema ? 1u : 0u;
		}
	}
	PE_parse_boot_argn("mali_freerun", &gMaliFreeRun, sizeof(gMaliFreeRun));
	PE_parse_boot_argn("mali_parkattrs", &gMaliParkAttrs, sizeof(gMaliParkAttrs));
	PE_parse_boot_argn("mali_trace", &gMaliTrace, sizeof(gMaliTrace));
	PE_parse_boot_argn("mali_trial", &gMaliTrial, sizeof(gMaliTrial));
	PE_parse_boot_argn("mali_noapclear", &gMaliNoApClear, sizeof(gMaliNoApClear));
	PE_parse_boot_argn("mali_nopwrclear", &gMaliNoPwrClear, sizeof(gMaliNoPwrClear));
	PE_parse_boot_argn("mali_domainwait", &gMaliDomainWait, sizeof(gMaliDomainWait));
	PE_parse_boot_argn("mali_fwnc", &gMaliFwNc, sizeof(gMaliFwNc));
	PE_parse_boot_argn("mali_fwx", &gMaliFwX, sizeof(gMaliFwX));
	PE_parse_boot_argn("mali_ptvendor", &gMaliPtVendor, sizeof(gMaliPtVendor));
	PE_parse_boot_argn("mali_bpverify", &gMaliBpVerify, sizeof(gMaliBpVerify));
	PE_parse_boot_argn("mali_scclk", &gMaliScClk, sizeof(gMaliScClk));
	PE_parse_boot_argn("mali_scupd", &gMaliScUpd, sizeof(gMaliScUpd));
	PE_parse_boot_argn("mali_sc26", &gMaliSc26, sizeof(gMaliSc26));
	PE_parse_boot_argn("mali_mcuwin", &gMaliMcuWin, sizeof(gMaliMcuWin));
	PE_parse_boot_argn("mali_mcuseq", &gMaliMcuSeq, sizeof(gMaliMcuSeq));
	if (kernel_thread_start(mali_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		printf("mali: no thread to bring the GPU up with\n");
	}
}
