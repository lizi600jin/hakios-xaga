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
#include <kern/locks.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include <miscfs/devfs/devfs.h>
#include <sys/systm.h>
/* Trial912: what the /dev/hakios-gpueb character device needs. */
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <vm/vm_kern.h>

#include "hakios.mali610_fw.h"
/*
 * The one PMIF transaction path, shared with the keys and the headset. The deadline it used
 * to take from <sys/hakios.pmif.h> is inside pmif_wait() now, where it can also see the STA
 * error bit; the lock is taken once around the whole transaction.
 */
#include <sys/hakios.pmif.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
/*
 * Trial908: declared here for the same reason the three-argument form below is -- osfmk's
 * machine_routines.h is not on this subtree's include path. VM_PROT_READ is 0x01
 * (osfmk/mach/vm_prot.h), restated so a read-only mapping can be asked for by name.
 */
extern vm_offset_t ml_io_map_with_prot(vm_offset_t phys_addr, vm_size_t size, int prot);
#ifndef VM_PROT_READ
#define VM_PROT_READ    0x01
#endif
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
/* Trial919: the second word of the gpufreq copy, which gpufreq reads alongside 0xF3C. */
#define MALI_SPM_PWR_STATUS_GF2 0x0f40u
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
/*
 * Trial906: the two words the vendor boot chain uses to decide whether GPUEB is alive.
 *
 * The engineering preloader sets bit 0 of 0x13fa0808 (MFGPLL + 0x808) and then measures
 * 0x13fa0840/0x844; the original LK refuses to go on unless MFG0 shows up in the SPM. Neither
 * value exists anywhere in this driver yet, and reading them costs nothing -- both blocks are
 * already mapped on the path that runs here.
 */
#define MALI_MFGPLL_GPUEB_EN    0x0808u         /* bit 0: the GPUEB PLL enable the preloader sets */
#define MALI_MFGPLL_GPUEB_CTRL  0x0840u         /* gpueb_pll_ctrl the preloader polls */

/* SPM words the LK checks before it starts the GPU: MFG0 status, and the control word. */
#define MALI_SPM_MFG0_PWR_CON   0x0eb8u   /* the GPU domain proper */
#define MALI_SPM_MFG1_PWR_CON   0x0ebcu   /* MPU, the domain the vendor powers by hand */
#define MALI_SPM_EMI1_PWR_CON   0x0eb0u   /* the memory interconnect, not a GPU domain */
/*
 * Trial908: what the bits in a *_PWR_CON word mean.
 *
 * PWR ACK is bits 31:30 -- both must be set before a domain is really powered. Bit 12 is the
 * SRAM power-down ACK, a separate question that was mistaken for the power ACK in the run that
 * produced the wrong conclusion about the GPU domain.
 */
#define MALI_SPM_PWR_ACK(word)      (((word) >> 30) & 0x3u)

/*
 * Trial910: the GPUEB mailbox, as mainline's mtk-gpueb-mailbox.c lays it out.
 *
 * Three separate blocks, and they are not adjacent -- 0x13c5fd80 + 0x280 ends at 0x13c60000
 * while the control bank starts at 0x13c62000, so the gap between them must not be computed.
 *
 *   0x13c5fd80  message SRAM, 160 four-byte slots. Not registers: reading it as one gives junk.
 *   0x13c62000  control bank, four one-bit-per-channel bitmaps.
 *
 * Which bit is ours: channel 1. Which slot: TX is slot 4 (byte 0x10), RX is slot 47 (byte 0xbc)
 * -- the two are not the same slot number, which is the easiest thing to get wrong here.
 */
#define MALI_GPUEB_MBOX_SRAM_PHYS   0x13c5fd80u
#define MALI_GPUEB_MBOX_SRAM_SIZE   0x280u
#define MALI_GPUEB_MBOX_CTL_PHYS    0x13c62000u
#define MALI_GPUEB_MBOX_CTL_SIZE    0x80u
#define MALI_GPUEB_CTL_TX_STS       0x00u       /* RO: the previous message is still there */
#define MALI_GPUEB_CTL_IRQ_SET      0x04u       /* WO: write BIT(chan) to raise */
#define MALI_GPUEB_CTL_IRQ_CLR      0x74u       /* WO: write BIT(chan) to clear / answer */
#define MALI_GPUEB_CTL_RX_STS       0x78u       /* RO: the other side has sent us something */
#define MALI_GPUEB_CHAN             1u          /* GPUFREQ */
#define MALI_GPUEB_TX_OFF           0x010u      /* slot 4 */
#define MALI_GPUEB_RX_OFF           0x0bcu      /* slot 47, not 4 */
#define MALI_GPUEB_MSG_WORDS        8u
#define MALI_GPUEB_CMD_INIT_SHARED  0u
/*
 * Trial918: the command numbers, from the vendor's own enum.
 *
 * gpufreq_ipi.h:29-49:
 *   0 CMD_INIT_SHARED_MEM   1 CMD_GET_FREQ_BY_IDX   2 CMD_GET_POWER_BY_IDX
 *   3 CMD_GET_OPPIDX_BY_FREQ   4 CMD_GET_LEAKAGE_POWER   5 CMD_SET_LIMIT
 *   6 CMD_POWER_CONTROL   7 CMD_COMMIT
 *
 * The power command is 6. This file said 4, which is the leakage-power query, so the two
 * writes recorded as power on and power off were two queries that answered 30 -- a reading, not
 * a reference. Both transactions were real; neither did what the label said.
 */
#define MALI_GPUEB_CMD_POWER        6u          /* CMD_POWER_CONTROL */
#define MALI_GPUEB_CMD_COMMIT       7u          /* CMD_COMMIT */
#define MALI_GPUEB_TARGET_STACK     2u          /* TARGET_STACK (Shader Stack: opp 0=852..39=219 MHz) */
#define MALI_GPUEB_POWER_OFF        0u          /* gpufreq_v2.h:85-88 */
#define MALI_GPUEB_POWER_ON         1u
#define MALI_GPUEB_SHARED_PHYS      0x7c600000u
#define MALI_GPUEB_SHARED_SIZE      0x200000u
#define MALI_GPUFREQ_MEM_SIZE       0x1000u
#define MALI_GPUFREQ_STATUS_MEM_SZ  0x400u
#define MALI_SPM_SRAM_PDN_ACK(word) (((word) >> 12) & 0x1u)
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
	kStackReady = 0xe10, kStackPwrOn = 0xe20, kStackPwrTrans = 0xe40,
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
/* Trial922: the bring-up handshake, defined next to the device node further down. */
bool hakios_gpueb_bringup_power_on(void);
void hakios_gpueb_bringup_power_off(void);
bool hakios_mali610_stop(void);
void HakiosMali610Start(void);
static void mali_uapi_stop_groups(void);
static void mali_uapi_park_all_vms(void);
struct m2c_trial;
static bool m2_try_group_zero(struct m2c_trial *trial);
static void m2c_run(void);

/*
 * Trial922/929: mali_gpuebpower asks GPUEB to power the GPU before the MCU is started.
 *
 * On by default now that the bring-up and the orderly six-step stop are proven together
 * (boot-928f: M1 done, then 30 s back to Android without a hand-run stop). Panthor reaches the
 * same point through mt6895_gpueb_power_on() before mcufw_init; this is the equivalent step.
 *
 * mali_gpuebpower=0 falls back to the manual path -- the twelve domains driven by this driver,
 * which is what every run before trial 922 did.
 *
 * Only consulted inside mali_bring_up() and only after mali_probe_read_only() has returned, so
 * neither mali=0 nor mali=0 mali_probe=1 reaches it: those two stay the ways to run this driver
 * without touching the GPU's power at all.
 */
static bool gpueb_bringup_held;                  /* Trial922: this run holds a GPUEB reference */
static unsigned gMaliStopped;                    /* Trial927: the clean stop has run */
static unsigned gMaliGpuebPower = 1;
/*
 * Trial931/936: mali_m2 builds a command stream group and runs two short instruction sequences
 * through it after the firmware starts.
 *
 * On by default now that the whole chain has been proven end to end (boot-935: NOPs fetched, then
 * a real MOV48/STORE_STATE/SYNC_ADD64 sequence that wrote 0xCAFEBABE and a live cycle count into
 * DRAM, with RETURN still 34 s). Pass mali_m2=0 to skip it.
 *
 * Separate from mali=N on purpose: M2 changes what the GPU is told to do rather than whether it is
 * powered, and a run that wants only the firmware started should not have a group built underneath
 * it. Neither mali=0 nor mali=0 mali_probe=1 reaches this -- they return before mali_bring_up().
 */
static unsigned gMaliM2 = 1;
static unsigned gMaliM2c;                       /* 1: AS1 high-PA; 2: + RUN_COMPUTE shader */
static lck_mtx_t m2c_lock; /* Initialized only for the AS1 boot probe. */
static unsigned gMaliGpu;                       /* mali_gpu=1: start HakiosGPU Panthor service */
static int gMaliOpp = -1;                       /* mali_opp=0..39: GPUEB TARGET_STACK OPP commit (-1=default) */
static volatile unsigned gMaliStopping;         /* set first in stop() to reject new UAPI calls */
static lck_mtx_t gMaliApiLock;                  /* serialises UAPI MMIO, AS1..7 PTs, CSGs & stop */
static bool gMaliApiLockInit;
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
static unsigned gMaliCacheFlush;                /* mali_cacheflush=1 flushes GPU caches just before AUTO */
static unsigned gMaliResetOnly;                 /* mali_resetonly=1 adds only the early soft reset */
static unsigned gMaliCohNone;                   /* mali_cohnone=1 selects vendor noncoherent mode before L2 */
static unsigned gMaliHwIssues;                  /* mali_hwissues=1 zeroes the four vendor quirk registers */
static unsigned gMaliStackPwrOn;                /* mali_stackpwron=1 requests core-stack power and observes READY */
static unsigned gMaliDoorbell;                  /* mali_doorbell=1 rings doorbell 0 once after AUTO */
static unsigned gMaliPdcSettle;                 /* mali_pdcsettle=1 waits for eight equal 1-ms SPM samples */
static unsigned gMaliStatusSem;                 /* mali_statussem=1 observes the existing failed-start DISABLE tail */
static unsigned gMaliAssOrder;                  /* mali_assorder=1 uses Android's AS0 register order */
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
static unsigned gMaliNoPdc;                      /* mali_nopdc=1 skips the default-path PDC configuration stores */
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
 * The original reason for this window was an interpretation of IRQ bit16 after AS UPDATE as a
 * root-fetch bus error. That interpretation was not established: parked/unmapped AS2..7 also
 * latch their respective upper IRQ bits, without fault status/address. Keep this allocator as
 * an experimental option, not as proof that addresses above 4GB are inaccessible to the GPU.
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
/* Match kbase's GPU permissions (mali_kbase_mmu_mode_aarch64.c:39-40):
 * ENTRY_ACCESS_RW = 1 << 6 and ENTRY_ACCESS_RO = 3 << 6. Do not infer an
 * additional unprivileged-RO mode from the old PTE_UNPRIV name. */
#define PTE_UNPRIV      (1ull << 6)     /* kbase ENTRY_ACCESS_RW */
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
			/* kbase entry_set_pte() includes ENTRY_ACCESS_BIT on table descriptors.
			 * Match that encoding; the upper IRQ bit after UPDATE does not prove a
			 * fault caused by this flag or a completed table walk. */
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

/* Private tables for a single, offline-built user VM. The first 4 KiB of a
 * wired 16 KiB XNU page is the hardware table; ownership metadata lives after it.
 * All pages in this pool are cacheable kmem pages, independent of the AS0 pool. */
struct mali_vm_table {
	uint64_t entry[512];
	struct mali_vm_table *next;
	uint64_t pa;
};

struct mali_vm {
	struct mali_vm_table *root, *tables;
	unsigned slot;
};

static bool
mali_vm_pa_valid(uint64_t pa)
{
	return pa && (pa & (PT_LEAF_GRANULE - 1)) == 0 && pa < (1ull << 40);
}

static struct mali_vm_table *
mali_vm_new_table(struct mali_vm *vm)
{
	vm_offset_t kva = 0;
	if (sizeof(struct mali_vm_table) > PAGE_SIZE ||
	    kmem_alloc(kernel_map, &kva, PAGE_SIZE, KMA_KOBJECT | KMA_DATA | KMA_ZERO,
	    VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		return NULL;
	}
	struct mali_vm_table *table = (struct mali_vm_table *)kva;
	table->pa = kvtophys(kva);
	if (!mali_vm_pa_valid(table->pa)) {
		kmem_free(kernel_map, kva, PAGE_SIZE);
		return NULL;
	}
	table->next = vm->tables;
	vm->tables = table;
	return table;
}

/* Map only a wired page owned by this probe. Never accept a caller-supplied PA. */
static bool
mali_vm_map_page(struct mali_vm *vm, uint64_t va, vm_offset_t kva, uint64_t access)
{
	if (!vm->root || (va & PAGE_MASK) || (kva & PAGE_MASK) || va >= (1ull << 48)) {
		return false;
	}
	for (size_t off = 0; off < PAGE_SIZE; off += PT_LEAF_GRANULE) {
		uint64_t addr = va + off, pa = kvtophys(kva + off);
		struct mali_vm_table *table = vm->root;
		if (!mali_vm_pa_valid(pa) || pa < (1ull << 32)) {
			return false;
		}
		for (int shift = 39; shift > 12; shift -= 9) {
			uint64_t *desc = &table->entry[(addr >> shift) & 511u];
			if (*desc == 0) {
				table = mali_vm_new_table(vm);
				if (!table) {
					return false;
				}
				*desc = table->pa | PTE_TABLE | PTE_AF;
			} else {
				uint64_t child = *desc & PTE_ADDRESS;
				for (table = vm->tables; table && table->pa != child; table = table->next) {
				}
				if (!table) {
					return false;
				}
			}
		}
		uint64_t *leaf = &table->entry[(addr >> 12) & 511u];
		if (*leaf) {
			return false;
		}
		/* Same non-cacheable policy as the board's existing mapper.
		 * Caller supplies PTE_XN in `access` for non-executable pages;
		 * shader binary pages omit PTE_XN so shader cores can fetch. */
		uint64_t attrs = PTE_PAGE | PTE_AF | (IDX_NC << 2) | access;
		if (!gMaliPtVendor) {
			attrs |= PTE_NG | PTE_SH_OS;
		}
		*leaf = pa | attrs;
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
				 * leaf 1 was never mapped, so the firmware entry had no valid descriptor.
				 * Section 1 was 25% mapped: leaves 0, 4, 8 and 12 only. This is a real
				 * mapping defect; whether the MCU makes progress after the fix still
				 * needs runtime evidence.
				 *
				 * The witness experiment survived this by luck: its stub sits at VA
				 * 0x1000000 (leaf 0) and its two stores land at 0x04000000/0x04000004
				 * (section 9 leaf 0), all of which were mapped. The old coverage hole
				 * therefore does not explain the witness's unchanged markers.
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
		/*
		 * Trial911: the vector page, the page the reset vector actually jumps to, and the
		 * shared page the version word lives in -- the three the next run has to compare.
		 *
		 * 0x01000000 was in this list and 0x01001000 was not, so every log so far printed the
		 * descriptor for the page *before* the entry point. The reset vector is read here rather
		 * than later on this path for exactly that reason: the address worth walking is the one
		 * in the image, not the one a comment assumed.
		 */
		uint64_t entry_va = 0ull;
		uint64_t vaList[8];
		unsigned vaCount = 0;
		unsigned n;

		{
			volatile uint32_t *vector = (volatile uint32_t *)sect_at(0ull);

			/* The hardware takes the reset address from VA 4; bit 0 is the Thumb bit. */
			if (vector != NULL && vector[1] != 0u) {
				printf("mali: reset vector says pc %#x; its page is the one that matters\n",
				    vector[1]);
				entry_va = (uint64_t)(vector[1] & ~1u) & ~0xfffull;
			}
		}
		vaList[vaCount++] = 0ull;                       /* vector table */
		vaList[vaCount++] = entry_va;                   /* actual entry page */
		vaList[vaCount++] = 0x1000000ull;               /* section 1, first page */
		vaList[vaCount++] = 0x2000000ull;
		vaList[vaCount++] = MALI_SHARED_VA;             /* version word */
		vaList[vaCount++] = 0x40000000ull;
		vaList[vaCount++] = 0x40022000ull;
		vaList[vaCount++] = 0x2000000ull + 0x2000ull;   /* second page of the rw section */

		for (n = 0; n < vaCount; n++) {
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
					/*
					 * The leaf: low two bits 11 means a valid page.
					 *
					 * Trial911: the physical address is the part that answers "does the MCU
					 * read what this kernel wrote". A descriptor word alone says a page is
					 * there; the PA beside the VA says whether the two sides of the handover
					 * are looking at the same memory, which is the question the next run has
					 * to settle. Shared memory is the one that matters most: the version word
					 * at 0x04000000 is written by firmware and read here.
					 */
					if ((entry & 3u) == 3u) {
						printf(" page pa %#llx (va+off %#llx)",
						    (unsigned long long)(entry & PTE_ADDRESS),
						    (unsigned long long)((entry & PTE_ADDRESS) | (va & 0xfffull)));
					} else {
						printf(" INVALID");
					}
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
	 * overwrites it with 0xdeadbeef at the end, so 0xdeadbeef confirms completion and 0x11
	 * confirms at least the first store. An unchanged marker only says that no completed
	 * store was observed; it cannot distinguish instruction fetch from a stalled store.
	 */
	/*
	 * Read it three times, with barriers, before believing a zero.
	 *
	 * These pages come from mem_alloc_low(), which maps them with MALI_WIMG_WCOMB --
	 * write-combining, not cacheable. Writes may be gathered there; the read direction is
	 * not guaranteed to bypass a pending combine buffer either. Read three times with
	 * barriers to check stability. A stable sentinel still only bounds visible output;
	 * it does not provide a trace of the core's instructions.
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
		printf(" -- no completed first store observed (not an instruction trace)\n");
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

/*
 * Trial931: the group and stream interfaces, as the firmware lays them out.
 *
 * panthor_fw.h:127-180 for the group, the same file above it for the stream, and the offsets
 * from panthor_fw.c:214-215:
 *
 *   group control   shared + 0x1000 + group_idx * group_stride
 *   stream control  that    + 0x40   + stream_idx * stream_stride
 *
 * Each control block carries input_va and output_va pointing back into the same shared page,
 * so the request goes into a different offset than the control block itself.
 *
 * Only the fields this trial writes are named. The rest is padding that has to keep its place,
 * because the firmware reads the whole structure.
 */
struct csg_input {
	uint32_t req;
	uint32_t ack_irq_mask;
	uint32_t doorbell_req;
	uint32_t cs_irq_ack;
	uint32_t reserved1[4];
	uint64_t allow_compute;
	uint64_t allow_fragment;
	uint32_t allow_other;
	uint32_t endpoint_req;
	uint64_t endpoint_req2;
	uint64_t suspend_buf;
	uint64_t protm_suspend_buf;
	uint32_t config;
	uint32_t iter_trace_config;
};

/* panthor_fw.h:181-192, field for field. */
struct csg_output {
	uint32_t ack;                       /* 0x00 */
	uint32_t reserved1;                 /* 0x04 */
	uint32_t doorbell_ack;              /* 0x08 */
	uint32_t cs_irq_req;                /* 0x0c */
	uint32_t status_endpoint_current;   /* 0x10 */
	uint32_t status_endpoint_req;       /* 0x14 */
	uint32_t status_state;              /* 0x18: bit 0 = idle */
	uint32_t resource_dep;              /* 0x1c */
};

/*
 * The stream structures, exactly as panthor_fw.h:25-42 has them.
 *
 * The first version of this guessed, and guessed wrong in a way that would have written the
 * ring address into a field that is not the ring address: cs_control has three fields, not
 * five, and cs_input puts config second, ringbuf_base at 0x10 and the ring input/output pair
 * down at 0x30. Both were corrected against the header before anything was submitted.
 */
struct cs_control {
	uint32_t features;
	uint32_t input_va;
	uint32_t output_va;
};

struct cs_input {
	uint32_t req;                   /* 0x00 */
	uint32_t config;                /* 0x04: CS_CONFIG_PRIORITY(0-3), CS_CONFIG_DOORBELL(8-15) */
	uint32_t reserved1;             /* 0x08 */
	uint32_t ack_irq_mask;          /* 0x0c */
	uint64_t ringbuf_base;          /* 0x10 */
	uint32_t ringbuf_size;          /* 0x18 */
	uint32_t reserved2;             /* 0x1c */
	uint64_t heap_start;            /* 0x20 */
	uint64_t heap_end;              /* 0x28 */
	uint64_t ringbuf_input;         /* 0x30 */
	uint64_t ringbuf_output;        /* 0x38 */
	uint32_t instr_config;          /* 0x40 */
	uint32_t instrbuf_size;         /* 0x44 */
	uint64_t instrbuf_base;         /* 0x48 */
	uint64_t instrbuf_offset_ptr;   /* 0x50 */
};

struct cs_output {
	uint32_t ack;                   /* 0x00 */
	uint32_t reserved0[15];         /* 0x04..0x3c */
	uint64_t status_cmd_ptr;        /* 0x40 */
	uint32_t status_wait;           /* 0x48 */
	uint32_t status_req_res;        /* 0x4c */
	uint64_t status_wait_ptr;       /* 0x50 */
	uint32_t status_wait_val;       /* 0x58 */
	uint32_t status_scoreboards;    /* 0x5c */
	uint32_t status_blocked;        /* 0x60 */
	uint32_t reserved1[7];          /* 0x64..0x7c */
	uint32_t fault;                 /* 0x80 */
	uint32_t fatal;                 /* 0x84 */
	uint64_t fault_info;            /* 0x88 */
	uint64_t fatal_info;            /* 0x90 */
	uint32_t reserved2[10];         /* 0x98..0xbc */
	uint32_t heap_vt_start;         /* 0xc0 */
	uint32_t heap_vt_end;           /* 0xc4 */
	uint32_t reserved3;             /* 0xc8 */
	uint32_t heap_frag_end;         /* 0xcc */
	uint64_t heap_address;          /* 0xd0 */
};

/* The group request word, from panthor_fw.h:138-148. */
#define kCsgStateMask       0x7u
#define kCsgStateStart      1u
#define kCsgEndpointConfig  (1u << 4)
#define kCsgStatusUpdate    (1u << 5)
#define kCsgSyncUpdate      (1u << 28)
#define kCsgIdle            (1u << 29)
#define kCsgDoorbell        (1u << 30)
/* The stream request word, from panthor_fw.h:37-50. */
#define kCsStateMask        0x7u
#define kCsStateStart       1u
#define kCsExtractEvent     (1u << 4)
#define kCsIdleSyncWait     (1u << 8)
#define kCsIdleEmpty        (1u << 10)
/* CS_CONFIG_DOORBELL(x) = ((x) << 8) & 0xff00, from panthor_fw.h:56. */
#define kCsConfigDoorbell(x) (((uint32_t)(x) << 8) & 0xff00u)
#define kCsFatalBit         (1u << 30)

/*
 * The two pages the firmware and the driver share for a stream's ring positions.
 * Layout from mali_gpu_csf_registers.h:138-145 (CS_INSERT_LO at 0, CS_EXTRACT at 8) and
 * the output side at 0x38 in cs_input. Offsets are in BYTES, not instructions.
 */
struct cs_user_input {
	uint64_t insert;
	uint64_t extract_init;
};

struct cs_user_output {
	uint64_t extract;
	uint32_t active;
	uint32_t pad;
};
#define kCsConfigPriority(x) ((uint32_t)(x) & 0x0fu)

#define kCsfGroupControlOffset   0x1000u
#define kCsfStreamControlOffset  0x40u
#define kCsfDoorbellBase         0x80000u
#define kCsfDoorbellStride       0x10000u

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
	/* Observation only, paired with the full MCU dump. Preserve complete SPM words. */
	if (gMaliMcuWin && gSpm != 0 && gMfgcfg != 0) {
		uint32_t f30 = reg_rd(gSpm, MALI_SPM_BUCK_ISO);
		uint32_t f34 = reg_rd(gSpm, MALI_SPM_PWR_STATUS);
		uint32_t f38 = reg_rd(gSpm, MALI_SPM_PWR_STATUS2);
		uint32_t f3c = reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF);
		uint32_t f40 = reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF + 4u);
		uint32_t sc[MALI_PDC_CORES];
		char line[256];
		unsigned i, off = 0;

		printf("mali: spm raw trial %u %s: F30 %#x F34 %#x F38 %#x F3C %#x F40 %#x\n",
		    gMaliTrial, when, f30, f34, f38, f3c, f40);
		line[0] = 0;
		for (i = 0; i < 32u; i++) {
			off += scnprintf(line + off, sizeof(line) - off, " b%u=%u", i, (f3c >> i) & 1u);
		}
		printf("mali: spm F3C bits trial %u %s:%s\n", gMaliTrial, when, line);
		line[0] = 0;
		off = 0;
		for (i = 0; i < MALI_PDC_CORES; i++) {
			sc[i] = reg_rd(gMfgcfg, MALI_PDC_SC_BASE + i * MALI_PDC_SC_STEP);
			off += scnprintf(line + off, sizeof(line) - off, " sc%u=%#x", i, sc[i]);
		}
		printf("mali: pdc raw trial %u %s:%s\n", gMaliTrial, when, line);
		line[0] = 0;
		off = 0;
		for (i = 0; i < MALI_PDC_CORES; i++) {
			off += scnprintf(line + off, sizeof(line) - off, " sc%u=%u", i, sc[i] & MALI_PDC_SC_EN);
		}
		printf("mali: pdc readback: trial %u %s:%s\n", gMaliTrial, when, line);
		printf("mali: pdc groups trial %u %s: cg %#x st0 %#x st1 %#x st2 %#x st4 %#x st5 %#x st6 %#x\n",
		    gMaliTrial, when, reg_rd(gMfgcfg, 0x100), reg_rd(gMfgcfg, 0x120),
		    reg_rd(gMfgcfg, 0x140), reg_rd(gMfgcfg, 0x118), reg_rd(gMfgcfg, 0xc0),
		    reg_rd(gMfgcfg, 0x98), reg_rd(gMfgcfg, 0x1c0));
	} else if (gMaliMcuWin) {
		printf("mali: spm/pdc observation trial %u %s: mapping unavailable\n", gMaliTrial, when);
	}
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
	if (gMaliAssOrder && as == 0) {
		AS_STEP("transcfg", gpu_wr64(asReg(as, kAsTranscfg), transcfg));
		AS_STEP("transtab", gpu_wr64(asReg(as, kAsTranstab), transtab));
		AS_STEP("memattr", gpu_wr64(asReg(as, kAsMemattr), MALI_MEMATTR));
		printf("mali: as_enable trial %u: AS%u applied Android order (CFG->TAB->ATTR)\n",
		    gMaliTrial, as);
	} else {
		AS_STEP("transtab", gpu_wr64(asReg(as, kAsTranstab), transtab));
		AS_STEP("memattr", gpu_wr64(asReg(as, kAsMemattr), MALI_MEMATTR));
		AS_STEP("transcfg", gpu_wr64(asReg(as, kAsTranscfg), transcfg));
	}
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
/* The layout, the command word and the deadline are <sys/hakios.pmif.h>'s; what is here is
 * the MT6368's VBUCK2, the only PMIC register the GPU touches. */
#define MALI_MT6368_SID         PMIF_SID_MT6368
#define MALI_VBUCK2_EN          0x0240u
#define MALI_VBUCK2_EN_SET      (MALI_VBUCK2_EN + 1u)
#define MALI_VBUCK2_EN_MASK     (1u << 2)
#define MALI_VBUCK2_VSEL        0x024eu
#define MALI_VBUCK2_VSEL_MASK   0xffu

/*
 * The GPU's one PMIC read and write, on the shared path. Nothing of the transaction is left
 * here: the IDLE wait, the write path that must not wait for WFVLDCLR (a write produces no
 * valid word), the RDATA read before the ACK, the ACK, the second IDLE wait, the deadline
 * and the lock are pmif_read8/pmif_write8's.
 *
 * This is the file that had the worst of the defects: its write path waited for WFVLDCLR,
 * which a write never reaches, so mali_vstack_rail()'s write of 0x241 always reported a
 * failure after a full timeout while the write itself had landed -- a log that said the
 * opposite of what the hardware did.
 */
static pmif_status_t mali_pmif_status;

static bool
mali_spmi(uint32_t addr, uint32_t *value, bool write)
{
	pmif_status_t status;

	if (value == NULL || addr > 0xffffu || (write && *value > 0xffu)) {
		return false;
	}
	if (write) {
		status = pmif_write8((uint8_t)MALI_MT6368_SID, (uint16_t)addr, (uint8_t)*value);
	} else {
		uint8_t byte = 0;

		status = pmif_read8((uint8_t)MALI_MT6368_SID, (uint16_t)addr, &byte);
		if (status == PMIF_OK) {
			*value = byte;
		}
	}
	if (status != PMIF_OK && status != mali_pmif_status) {
		printf("mali: spmi %s %#x: %s\n", write ? "write" : "read", addr,
		    pmif_status_name(status));
	}
	mali_pmif_status = status;
	return status == PMIF_OK;
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

	{
		pmif_status_t mapped = pmif_init();

		if (mapped != PMIF_OK) {
			printf("mali: the arbiter at %#x will not map (%s); the stack rail stays as it is\n",
			    (unsigned)PMIF_BASE, pmif_status_name(mapped));
			return;
		}
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
	/* kbase's initial PM setup selects system coherency before the deferred FW
	 * load (pm_driver.c:2939); Panthor also selects it before L2 power-on.
	 * A trace starting at AS configuration cannot establish that this earlier
	 * setup was absent. Keep the old cold path selectable and observe both
	 * register values in the already-reached pre-L2 path. */
	{
		uint32_t before = mrd(kCoherencyProtocol);

		if (!gMaliCold || gMaliCohNone) {
			mwr(kCoherencyProtocol, kCoherencyNone);
		}
		printf("mali: protocol trial %u: features %#x before %#x after %#x wrote %u\n",
		    gMaliTrial, mrd(kCoherencyFeatures), before, mrd(kCoherencyProtocol),
		    (!gMaliCold || gMaliCohNone) ? 1u : 0u);
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

/* Diagnostic-only snapshot. RAW reads bracket STATUS/MASK; nothing is cleared. */
static void
job_irq_snapshot(const char *where, unsigned spins)
{
	uint32_t raw_before = mrd(kJobIntRaw);
	uint32_t mask = mrd(kJobIntMask);
	uint32_t status = mrd(kJobIntStatus);
	uint32_t raw_after = mrd(kJobIntRaw);

	printf("mali: jobirq probe trial %u %s spin %u: raw %#x/%#x stat %#x mask %#x raw_same %u\n",
	    gMaliTrial, where, spins, raw_before, raw_after, status, mask,
	    raw_before == raw_after ? 1u : 0u);
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
	 * survives, no version publication was observed. A changed word needs interpretation
	 * together with the boot IRQ or the witness markers; neither reading is a PC trace.
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
	/* Isolate cache maintenance from mali_cold=0's other changes. This branch is
	 * inline in the observed glb-req -> AUTO path, after all CPU image edits.
	 * The command and completion bit match gpu_flush_caches() and vendor CSF.
	 * A timeout is an incomplete precondition, not an MCU execution result. */
	if (gMaliCacheFlush) {
		unsigned cache_spins;
		uint32_t raw;

		mwr(kIntClear, kGpuIrqCleanCaches);
		mwr(kGpuCmd, kGpuCmdFlushCaches);
		for (cache_spins = 0; cache_spins < 100000u; cache_spins++) {
			if (mrd(kIntRaw) & kGpuIrqCleanCaches) {
				break;
			}
			delay(1);
		}
		raw = mrd(kIntRaw);
		printf("mali: cache trial %u: cmd %#x spins %u raw %#x completed %u\n",
		    gMaliTrial, kGpuCmdFlushCaches, cache_spins, raw,
		    (raw & kGpuIrqCleanCaches) ? 1u : 0u);
		if (!(raw & kGpuIrqCleanCaches)) {
			printf("mali: cache trial %u: stopping before AUTO\n", gMaliTrial);
			return false;
		}
		mwr(kIntClear, kGpuIrqCleanCaches);
	}
	/* Freeze before AUTO, then print after the attempt so tracing cannot delay the request. */
	gMaliWritesFrozen = true;
	if (gMaliPdcSettle) {
		uint32_t before = reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF);
		uint32_t prev = before, cur = before;
		unsigned stable = 0, spins, changes = 0;

		printf("mali: pdcsettle trial %u: before %#x fd %#x target 8 interval 1000 us limit 200000\n",
		    gMaliTrial, before, reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF + 4u));
		for (spins = 0; spins < 200000u; spins++) {
			delay(1000);
			cur = reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF);
			if (cur == prev) {
				if (++stable >= 8u) {
					break;
				}
			} else {
				stable = 0;
				changes++;
				printf("mali: pdcsettle trial %u: PWR_STATUS %#x -> %#x (fd %#x)\n",
				    gMaliTrial, prev, cur, reg_rd(gSpm, MALI_SPM_PWR_STATUS_GF + 4u));
				prev = cur;
			}
		}
		printf("mali: pdcsettle trial %u: %s at %#x (& 0x3ffc = %#x) after %u spins, stable %u changes %u before %#x\n",
		    gMaliTrial, stable >= 8u ? "settled" : "NOT SETTLED", cur, cur & 0x3ffcu,
		    spins < 200000u ? spins + 1u : spins, stable, changes, before);
	}
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
	job_irq_snapshot("before AUTO", 0u);
	mwr(kMcuControl, kMcuControlAuto);
	if (gMaliMcuSeq) {
		__asm__ volatile ("dsb sy" ::: "memory");
		delay(5);
		printf("mali: mcuseq: after AUTO write, control reads %#x status %#x (status bits: 0=%u 1=%u 2=%u 3=%u)\n",
		    mrd(kMcuControl), mrd(kMcuStatus),
		    mrd(kMcuStatus) & 1u, (mrd(kMcuStatus) >> 1) & 1u,
		    (mrd(kMcuStatus) >> 2) & 1u, (mrd(kMcuStatus) >> 3) & 1u);
	}
	if (gMaliDoorbell) {
		uint32_t job_before = mrd(kJobIntStatus);

		mwr(kDoorbell0, 1u);
		__asm__ volatile ("dsb sy" ::: "memory");
		printf("mali: doorbell trial %u: wrote 1 to 0x80000, job_irq before %#x after %#x, mcu_st %#x\n",
		    gMaliTrial, job_before, mrd(kJobIntStatus), mrd(kMcuStatus));
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
			job_irq_snapshot("sample", spins);
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
			job_irq_snapshot("GLOBAL_IF observed by existing loop", spins);
			booted = true;
			break;
		}
		delay(1);
	}
	printf("mali: the start loop ended after %u spins (halt seen %u, fatal seen %u)\n",
	    spins, sawHalt ? 1u : 0u, sawFatal ? 1u : 0u);
	job_irq_snapshot("loop end before any boot-success clear", spins);
	mali_write_trace("after AUTO (stores captured before AUTO)");
	if (gMaliMcuWin) {
		mcu_window_dump("after AUTO (before DISABLE)");
	}

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
	    mrd(kMcuStatus) == 1u ? "raw state 1; boot unproven" : mrd(kMcuStatus) == 2u ? "halted" :
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

/*
 * Trial908: the read-only probe, and the reason it comes first.
 *
 * Everything below this point changes the GPU's state: it maps the block, releases the bus
 * protections, powers MFG domains, moves the muxes and finally asks the MCU to start. A run that
 * does any of that ends with a GPU the loader did not leave in that state, and the last few runs
 * took minutes to come back to Android rather than half a minute.
 *
 * This function answers the same question -- what does the SPM and the GPUEB PLL say about this
 * board -- by reading only, through mappings made for the purpose, and then returning before
 * gGpu is ever assigned. Nothing is written, no domain is powered, and quiesce() finds gGpu zero
 * and keeps its hands off the block on the way out.
 *
 * The mapping is made here rather than reused from the globals because those are filled in later
 * on this same path, and using them would mean this probe could not run before them.
 */
/*
 * Trial910: is the GPUEB mailbox reachable at all?
 *
 * Before asking GPUEB to power the GPU on, the two things worth knowing are whether the message
 * SRAM answers and whether the control bank says anything is in flight. Both are reads. A write
 * here would be a message sent to a coprocessor whose firmware this kernel has never spoken to,
 * and the plan is to send exactly two of those, deliberately, once the path is known good.
 *
 * The values are printed with their names rather than as a blob because the interesting answers
 * are boolean: a TX bit set means the previous message was never taken, an RX bit set means
 * GPUEB has already answered something nobody asked for.
 */
static void
mali_probe_gpueb(void)
{
	vm_offset_t sram, ctl;
	uint32_t tx = 0, rx = 0, first = 0, last = 0;

	sram = ml_io_map_with_prot(MALI_GPUEB_MBOX_SRAM_PHYS, MALI_GPUEB_MBOX_SRAM_SIZE,
	    VM_PROT_READ);
	ctl = ml_io_map_with_prot(MALI_GPUEB_MBOX_CTL_PHYS, MALI_GPUEB_MBOX_CTL_SIZE,
	    VM_PROT_READ);
	if (sram == 0 || ctl == 0) {
		printf("mali: gpueb probe: the mailbox will not map read-only "
		    "(sram %p ctl %p)\n", (void *)sram, (void *)ctl);
		return;
	}

	tx = reg_rd(ctl, MALI_GPUEB_CTL_TX_STS);
	rx = reg_rd(ctl, MALI_GPUEB_CTL_RX_STS);
	first = reg_rd(sram, 0u);
	last = reg_rd(sram, MALI_GPUEB_MBOX_SRAM_SIZE - 4u);

	printf("mali: gpueb probe: ctl tx %#x (chan %u %s) rx %#x (chan %u %s)\n",
	    tx, MALI_GPUEB_CHAN, (tx & (1u << MALI_GPUEB_CHAN)) ? "BUSY" : "idle",
	    rx, MALI_GPUEB_CHAN, (rx & (1u << MALI_GPUEB_CHAN)) ? "PENDING" : "empty");
	printf("mali: gpueb probe: sram slot0 %#x slot159 %#x; tx slot %#x rx slot %#x\n",
	    first, last,
	    reg_rd(sram, MALI_GPUEB_TX_OFF), reg_rd(sram, MALI_GPUEB_RX_OFF));
	/*
	 * Zeroes in the control bank are the documented idle state, not proof of unreachability:
	 * TX_STS is zero when nothing is in flight and RX_STS is zero when nothing is waiting. To
	 * tell "idle" from "the bus does not answer", scan the message SRAM for any non-zero word.
	 *
	 * The firmware LK loaded sits in GPUEB's own SRAM, not here, so this buffer can legitimately
	 * be empty -- but a word that is neither zero nor all ones proves the window is read at all,
	 * and the count of non-zero words says how much traffic this run has seen.
	 */
	{
		uint32_t at, nonzero = 0, last_nonzero = 0, last_at = 0;

		for (at = 0; at < MALI_GPUEB_MBOX_SRAM_SIZE; at += 4u) {
			uint32_t word = reg_rd(sram, at);

			if (word != 0u) {
				nonzero++;
				last_nonzero = word;
				last_at = at;
			}
		}
		printf("mali: gpueb probe: sram %u of %u words non-zero",
		    nonzero, MALI_GPUEB_MBOX_SRAM_SIZE / 4u);
		if (nonzero != 0u) {
			printf(" (last %#x at +%#x)", last_nonzero, last_at);
		}
		printf("; nothing written\n");
	}
}

static bool
mali_probe_read_only(void)
{
	unsigned which = 0;
	vm_offset_t spm, pll;
	uint32_t mfg0, mfg1, emi1, enable, status4;

	if (!PE_parse_boot_argn("mali_probe", &which, sizeof(which)) || which == 0) {
		return false;
	}

	spm = ml_io_map_with_prot(MALI_SPM_PHYS, MALI_SPM_SIZE, VM_PROT_READ);
	pll = ml_io_map_with_prot(MALI_MFGPLL_PHYS, MALI_MFGPLL_SIZE, VM_PROT_READ);
	if (spm == 0 || pll == 0) {
		printf("mali: probe: the SPM or the MFGPLL will not map read-only\n");
		return true;                    /* the probe was asked for; it just could not run */
	}

	mfg0 = reg_rd(spm, MALI_SPM_MFG0_PWR_CON);
	mfg1 = reg_rd(spm, MALI_SPM_MFG1_PWR_CON);
	emi1 = reg_rd(spm, MALI_SPM_EMI1_PWR_CON);
	enable = reg_rd(pll, MALI_MFGPLL_GPUEB_EN);

	printf("mali: probe MFG0_PWR_CON %#x (pwr ack %u sram pdn ack %u) "
	    "MFG1_PWR_CON %#x (pwr ack %u) EMI1_PWR_CON %#x\n",
	    mfg0, MALI_SPM_PWR_ACK(mfg0), MALI_SPM_SRAM_PDN_ACK(mfg0),
	    mfg1, MALI_SPM_PWR_ACK(mfg1), emi1);
	printf("mali: probe MFGPLL+0x808 %#x (gpueb pll enable %u) +0x840 %#x "
	    "con0 %#x con1 %#x con3 %#x\n",
	    enable, enable & 1u, reg_rd(pll, MALI_MFGPLL_GPUEB_CTRL),
	    reg_rd(pll, MALI_MFGPLL_CON0), reg_rd(pll, MALI_MFGPLL_CON1),
	    reg_rd(pll, MALI_MFGPLL_CON3));
	printf("mali: probe PWR_STATUS 0xF34 %#x 0xF38 %#x 0xF3C %#x (MFG1_12 %#x)\n",
	    reg_rd(spm, MALI_SPM_PWR_STATUS), reg_rd(spm, MALI_SPM_PWR_STATUS2),
	    reg_rd(spm, MALI_SPM_PWR_STATUS_GF),
	    reg_rd(spm, MALI_SPM_PWR_STATUS_GF) & 0x3ffcu);
	mali_probe_gpueb();
	printf("mali: probe done; gGpu is still 0, so quiesce() will not touch the block\n");
	return true;
}

static void
mali_bring_up(void)
{
	unsigned up;

	if (mali_probe_read_only()) {
		return;                         /* Trial908: read only, and leave gGpu at zero */
	}

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
	/*
	 * Trial923: the GPUEB power request, before this function asks the block anything.
	 *
	 * This is where panthor does it (mt6895_gpueb_power_on() before mcufw_init), and the
	 * position matters for a reason that is not stylistic: the three branches below decide what
	 * to do by reading GPU_ID. If letting GPUEB power the block makes that read answer, the
	 * whole manual cold-start branch -- SPM, VBUCK2, MFGPLL, the twelve domains -- is skipped,
	 * and the run measures the coprocessor's path alone instead of a mixture of both.
	 *
	 * If GPU_ID still reads zero afterwards, nothing is lost: the cold-start branch runs as it
	 * always has, with a GPUEB reference held, and the log says which one it took.
	 *
	 * A refusal does not abort. The point of the run is to find out what changes, and stopping
	 * at a refused power request would answer nothing.
	 */
	if (gMaliGpuebPower) {
		printf("mali: gpueb: asking GPUEB to power the GPU before the first read\n");
		if (!hakios_gpueb_bringup_power_on()) {
			printf("mali: gpueb: the request did not succeed; continuing so the rest of "
			    "the sequence still reports what it finds\n");
		}
		printf("mali: gpu_id after the GPUEB request: %#x (csf %#x)\n",
		    mrd(kGpuId), mrd(kCsfId));
	}
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
		if (!gMaliNoPdc) {
			printf("mali: pdc enabled on %u registers\n", pdc_enable());
		} else {
			printf("mali: pdc setup skipped (mali_nopdc=1); inherited values unchanged\n");
		}
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
	if (!gMaliCold || gMaliResetOnly) {
		printf("mali: reset trial %u: early soft reset, resetonly %u cold %u\n",
		    gMaliTrial, gMaliResetOnly, gMaliCold);
		if (!gpu_soft_reset()) {
			/* A block that will not reset is not a block to write firmware into. */
			return;
		}
		if (!gMaliCold) {
			gpu_hw_issues();
		}
	}
	/* Opt-in four-write experiment; after any successful reset, before AS/L2 setup. */
	if (gMaliHwIssues) {
		uint32_t shader = mrd(kShaderConfig), tiler = mrd(kTilerConfig);
		uint32_t l2mmu = mrd(kL2MmuConfig), csf = mrd(kCsfConfig);

		mwr(kShaderConfig, 0);
		mwr(kTilerConfig, 0);
		mwr(kL2MmuConfig, 0);
		mwr(kCsfConfig, 0);
		__asm__ volatile ("dsb sy" ::: "memory");
		printf("mali: hwissues trial %u: before shader %#x tiler %#x l2mmu %#x csf %#x -> after %#x %#x %#x %#x\n",
		    gMaliTrial, shader, tiler, l2mmu, csf,
		    mrd(kShaderConfig), mrd(kTilerConfig), mrd(kL2MmuConfig), mrd(kCsfConfig));
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
	/* One opt-in STACK request after L2; a timeout must still reach the witness. */
	if (gMaliStackPwrOn) {
		uint32_t present = mrd(kStackPresent);
		uint32_t ready0 = mrd(kStackReady), trans0 = mrd(kStackPwrTrans);
		uint32_t ready = ready0;
		unsigned spins;

		printf("mali: stackpwron trial %u: present %#x ready %#x trans %#x (before)\n",
		    gMaliTrial, present, ready0, trans0);
		/* kbase_pm_invoke(ACTION_PWRON) writes this register despite the RO header comment. */
		mwr(kStackPwrOn, present);
		__asm__ volatile ("dsb sy" ::: "memory");
		for (spins = 0; spins < 100000u; spins++) {
			ready = mrd(kStackReady);
			if (ready == present) {
				break;
			}
			delay(1);       /* XNU delay() takes microseconds. */
		}
		printf("mali: stackpwron trial %u: present %#x ready %#x trans %#x (after %u spins, %s)\n",
		    gMaliTrial, present, ready, mrd(kStackPwrTrans), spins,
		    ready == present ? "ALL READY" : "NOT READY");
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
		if (gMaliStatusSem) {
			uint32_t control = mrd(kMcuControl);
			uint32_t first = mrd(kMcuStatus);
			uint32_t again, status;
			unsigned spins;

			__asm__ volatile ("dsb sy" ::: "memory");
			again = mrd(kMcuStatus);
			printf("mali: status semantics trial %u: after DISABLE ctl %#x st %u | after re-read st %u | gpu_status %#x debug_top %#x\n",
			    gMaliTrial, control, first, again, mrd(kGpuStatus), mfg_debug_top());
			mwr(kMcuControl, kMcuControlDisable);
			delay(1000);
			status = mrd(kMcuStatus);
			printf("mali: status semantics trial %u: second DISABLE ctl %#x st %u (after 1000 us)\n",
			    gMaliTrial, mrd(kMcuControl), status);
			/* Panthor allows 100 ms to stop. Bound reads; record departure from raw 1. */
			for (spins = 0; spins < 10000u; spins++) {
				status = mrd(kMcuStatus);
				if (status != 1u) {
					break;
				}
				delay(10);
			}
			printf("mali: status semantics trial %u: bounded re-read ctl %#x st %u after %u polls (delay 10 us, limit 10000)\n",
			    gMaliTrial, mrd(kMcuControl), status, spins);
		}
		if (!gMaliMcuWin) {
			mcu_window_dump("after the core was switched off");
		}
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
			/*
			 * Three words the firmware writes to, each of which starts at zero in the image
			 * and receives a value that cannot be zero. Reading all three costs three loads
			 * out of memory this file already owns and already mapped.
			 *
			 * 0x4001040 is stream0.control.features: the group control block lives at
			 * CSF_GROUP_CONTROL_OFFSET (0x1000, panthor_fw.c:215) and the stream control block
			 * at CSF_STREAM_CONTROL_OFFSET (0x40, panthor_fw.c:216) inside it, so
			 * csg_idx 0 / cs_idx 0 lands on shared+0x1040. The firmware fills it from
			 * 0x1002ff8 with a value that always has 0x70000 set.
			 *
			 * The other two are glb_control.version (0x01010000 from 0x1003012) and
			 * glb_control.perfcnt_size (3 + a bit length, so at least 3, from 0x1002fd6).
			 *
			 * None of these proves the MCU ran if it stays zero: a store that reaches the bus
			 * can still fail translation, and nothing here reads SCB->CFSR. What a non-zero
			 * value would establish is that the firmware executed the corresponding store.
			 */
			{
				uint32_t stream0_features = 0;
				uint32_t perfcnt_size = raw->perfcnt_size;
				uint32_t read_ok = 0;

				/*
				 * The guard cannot fail for this image (the section is 0x94000 bytes), but print
				 * whether it ran: a stream0_features of zero has to be distinguishable from a
				 * zero that was never read.
				 */
				if (gShared->va_end - gShared->va_start >= 0x1044u &&
				    gShared->mem.size >= 0x1044u) {
					stream0_features =
					    *(volatile uint32_t *)(gShared->mem.kva + 0x1040u);
					read_ok = 1;
				}
				printf("mali: discriminate trial %u: version %#x perfcnt %#x"
				    " stream0_features %#x read_ok %u\n",
				    gMaliTrial, version, perfcnt_size, stream0_features, read_ok);
			}
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

	/*
	 * Trial931: and then, only if asked, one group with one stream on a ring.
	 *
	 * A failure here does not undo M1: the firmware is running either way, and a run that
	 * cannot build a group still says so in the log rather than taking the whole bring-up down
	 * with it. That distinction is the point of putting M2 behind its own switch.
	 */
	if (gMaliGpu) {
		printf("mali: mali_gpu=1: leaving CSG slots and AS1..AS7 clean for HakiosGPU\n");
	} else if (gMaliM2c) {
		m2c_run(); /* Claim group zero before any AS0 M2 submission. */
	} else if (gMaliM2) {
		if (!m2_try_group_zero(NULL)) {
			printf("mali: m2: the group was not built; M1 stands\n");
		}
	}
}

/* The command ring uses the second 4 KiB leaf, the output/descriptors the fourth,
 * and (for mali_m2c=2) the executable shader binary the third leaf of a separate page.
 * Neither VA belongs to the firmware. Queue interfaces and suspend storage
 * remain in the firmware VM, just as in panthor_fw_alloc_queue_iface_mem(). */
#define M2C_RING_PAGE_VA   0x200000000ull
#define M2C_RESULT_PAGE_VA 0x200004000ull
#define M2C_SHADER_PAGE_VA 0x200008000ull
#define M2C_RING_OFFSET    0x1000u
#define M2C_RESULT_OFFSET  0x3000u
#define M2C_SHADER_OFFSET  0x2000u

struct m2c_trial {
	struct mali_vm vm;
	struct mali_mem candidates;
	vm_offset_t ring, result, shader;
	volatile struct csg_input *group_in;
	volatile struct csg_output *group_out;
	volatile struct glb_input *global_in;
	volatile struct glb_output *global_out;
	bool programmed, terminate_sent;
};

static struct m2c_trial gM2c;

/* The aperture is one permanent ml_io_map. Check the actual translations for
 * every register page this probe will touch before writing. M2c and orderly
 * power-off hold the same lock for the lifetime of each such check. */
static bool
m2c_mmio_ready(void)
{
	const uint32_t offsets[] = { kGpuCmd, kJobIntRaw, kMmuIntRaw,
	    asReg(1u, kAsTranscfg) + 4, kCsfDoorbellBase, kCsfDoorbellBase + kCsfDoorbellStride };
	if (!gGpu || !gpueb_bringup_held || gMaliStopped) {
		return false;
	}
	for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); i++) {
		if (offsets[i] > MALI_GPU_SIZE - sizeof(uint32_t) ||
		    kvtophys(gGpu + offsets[i]) != MALI_GPU_PHYS + offsets[i]) {
			return false;
		}
	}
	return true;
}

static void
m2c_free(struct m2c_trial *t)
{
	while (t->vm.tables) {
		struct mali_vm_table *table = t->vm.tables;
		t->vm.tables = table->next;
		kmem_free(kernel_map, (vm_offset_t)table, PAGE_SIZE);
	}
	if (t->candidates.kva) {
		kmem_free(kernel_map, t->candidates.kva, t->candidates.size);
	}
	memset(t, 0, sizeof(*t));
}

static bool
m2c_shared_fits(uint64_t va, size_t size)
{
	if (!gShared || (va & 3u) || va < gShared->va_start || va > gShared->va_end) {
		return false;
	}
	return size <= gShared->va_end - va && size <= gShared->mem.size &&
	    va - gShared->va_start <= gShared->mem.size - size;
}

static bool
m2c_prepare(struct m2c_trial *t)
{
	const unsigned as = 1;
	if (PAGE_SIZE != 4 * PT_LEAF_GRANULE || !m2c_mmio_ready() ||
	    (mrd(kAsPresent) & (1u << as)) == 0 || (mrd(kMmuFeatures) & 0xffff) != 0x2830) {
		printf("mali: m2c: AS1, page geometry, power or MMIO unavailable\n");
		return false;
	}
	uint64_t root = gpu_rd64(asReg(as, kAsTranstab));
	uint64_t cfg = gpu_rd64(asReg(as, kAsTranscfg));
	uint32_t status = mrd(asReg(as, kAsStatus));
	printf("mali: m2c: AS1 before root %#llx attr %#llx cfg %#llx status %#x\n",
	    (unsigned long long)root, (unsigned long long)gpu_rd64(asReg(as, kAsMemattr)),
	    (unsigned long long)cfg, status);
	if ((status & 1u) || root || (cfg & 15u) > 1u) {
		return false;
	}

	/* Keep a bounded batch wired while selecting two distinct high pages.
	 * Repeated free/allocate can otherwise return the same low page. */
	if (!mem_alloc_high(&t->candidates, 8 * PAGE_SIZE)) {
		return false;
	}
	for (unsigned i = 0; i < t->candidates.pages; i++) {
		vm_offset_t kva = t->candidates.kva + (vm_offset_t)i * PAGE_SIZE;
		uint64_t pa = kvtophys(kva);
		if (pa < (1ull << 32) || !mali_vm_pa_valid(pa)) {
			continue;
		}
		if (!t->ring) {
			t->ring = kva;
		} else if (!t->result) {
			t->result = kva;
			if (gMaliM2c < 2u) {
				break;
			}
		} else {
			t->shader = kva;
			break;
		}
	}
	if (!t->result || (gMaliM2c >= 2u && !t->shader)) {
		printf("mali: m2c: insufficient high physical pages in %u candidates; skipping submission\n",
		    t->candidates.pages);
		return false;
	}
	t->vm.slot = as;
	t->vm.root = mali_vm_new_table(&t->vm);
	if (!mali_vm_map_page(&t->vm, M2C_RING_PAGE_VA, t->ring, PTE_RDONLY | PTE_XN) ||
	    !mali_vm_map_page(&t->vm, M2C_RESULT_PAGE_VA, t->result, PTE_UNPRIV | PTE_XN) ||
	    (gMaliM2c >= 2u && !mali_vm_map_page(&t->vm, M2C_SHADER_PAGE_VA, t->shader, PTE_RDONLY))) {
		printf("mali: m2c: cannot build AS1 page tables\n");
		return false;
	}
	t->ring += M2C_RING_OFFSET;
	t->result += M2C_RESULT_OFFSET;
	if (gMaliM2c >= 2u) {
		t->shader += M2C_SHADER_OFFSET;
	}
	for (struct mali_vm_table *p = t->vm.tables; p; p = p->next) {
		FlushPoC_DcacheRegion((vm_offset_t)p->entry, sizeof(p->entry));
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	/* Unlike the AS0 bring-up diagnostic, never clear global fault latches. */
	if (!as_ready(as)) {
		return false;
	}
	t->programmed = true; /* A timed-out UPDATE may still complete. */
	gpu_wr64(asReg(as, kAsTranstab), t->vm.root->pa);
	gpu_wr64(asReg(as, kAsMemattr), MALI_MEMATTR);
	gpu_wr64(asReg(as, kAsTranscfg), 0x42000006ull);
	__asm__ volatile ("dsb sy" ::: "memory");
	if (!as_command(as, kAsCmdUpdate) ||
	    gpu_rd64(asReg(as, kAsTranstab)) != t->vm.root->pa ||
	    gpu_rd64(asReg(as, kAsMemattr)) != MALI_MEMATTR ||
	    gpu_rd64(asReg(as, kAsTranscfg)) != 0x42000006ull ||
	    (mrd(kMmuIntRaw) & 0xffffu) || (mrd(asReg(as, kAsFaultStatus)) & 0xffu)) {
		return false;
	}
	printf("mali: m2c: AS1 enabled root PA %#llx ring PA %#llx result PA %#llx shader PA %#llx\n",
	    (unsigned long long)t->vm.root->pa, (unsigned long long)kvtophys(t->ring),
	    (unsigned long long)kvtophys(t->result),
	    (unsigned long long)(t->shader ? kvtophys(t->shader) : 0ull));
	return true;
}

static bool
m2c_terminate(struct m2c_trial *t)
{
	if (!t->group_in) {
		return true;
	}
	if (!m2c_mmio_ready()) {
		return false;
	}
	if (!t->terminate_sent) {
		/* Zero is also the reset ACK. Drain START before accepting TERMINATE. */
		for (unsigned i = 0; ; i++) {
			FlushPoC_DcacheRegion((vm_offset_t)t->group_out, sizeof(*t->group_out));
			FlushPoC_DcacheRegion((vm_offset_t)t->global_out, sizeof(*t->global_out));
			if ((t->group_out->ack & kCsgStateMask) == kCsgStateStart &&
			    !((t->global_out->doorbell_ack ^ t->global_in->doorbell_req) & 1u)) {
				break;
			}
			if (i == 200) {
				return false;
			}
			delay(1000);
		}
		t->group_in->req &= ~kCsgStateMask;
		t->global_in->doorbell_req ^= 1u;
		mem_flush(&gShared->mem);
		mwr(kCsfDoorbellBase, 1u);
		t->terminate_sent = true;
	}
	for (unsigned i = 0; i < 200; i++) {
		FlushPoC_DcacheRegion((vm_offset_t)t->group_out, sizeof(*t->group_out));
		FlushPoC_DcacheRegion((vm_offset_t)t->global_out, sizeof(*t->global_out));
		if (!(t->group_out->ack & kCsgStateMask) &&
		    !((t->global_out->doorbell_ack ^ t->global_in->doorbell_req) & 1u)) {
			t->group_in = NULL;
			return true;
		}
		delay(1000);
	}
	return false;
}

/* Even if termination failed, RETURN may park AS1 after disabling the MCU.
 * Only release storage with positive evidence for all three cleanup steps. */
static bool
m2c_park(struct m2c_trial *t)
{
	unsigned as = t->vm.slot;
	if (!m2c_mmio_ready() || !as_ready(as)) {
		printf("mali: m2c: AS1 park refused; MMIO unavailable or AS busy\n");
		return false;
	}
	if (!gpu_flush_caches()) {
		return false;
	}
	gpu_wr64(asReg(as, kAsTranstab), 0);
	gpu_wr64(asReg(as, kAsMemattr), 0);
	gpu_wr64(asReg(as, kAsTranscfg), kAsTranscfgUnmapped);
	__asm__ volatile ("dsb sy" ::: "memory");
	if (!as_command(as, kAsCmdUpdate) || gpu_rd64(asReg(as, kAsTranstab)) ||
	    gpu_rd64(asReg(as, kAsMemattr)) ||
	    gpu_rd64(asReg(as, kAsTranscfg)) != kAsTranscfgUnmapped) {
		printf("mali: m2c: AS1 park not confirmed\n");
		return false;
	}
	t->programmed = false;
	return true;
}

static void
m2c_run(void)
{
	struct m2c_trial *t = &gM2c;
	bool passed = m2c_prepare(t) && m2_try_group_zero(t);
	if (t->programmed) {
		printf("mali: m2c: AS1 faultstatus %#x faultaddr %#llx mmu_raw %#x gpu_fault %#x\n",
		    mrd(asReg(1u, kAsFaultStatus)), (unsigned long long)gpu_rd64(asReg(1u, kAsFaultAddress)),
		    mrd(kMmuIntRaw), mrd(kGpuFaultStatus));
		if (!m2c_terminate(t)) {
			printf("mali: m2c_as1_high_pa: FAIL group 0 TERMINATE unconfirmed; keeping pages for RETURN\n");
			return;
		}
		if (!m2c_park(t)) {
			printf("mali: m2c_as1_high_pa: FAIL AS1 cleanup incomplete; keeping pages for RETURN\n");
			return;
		}
		uint32_t raw = mrd(kMmuIntRaw), fs = mrd(asReg(t->vm.slot, kAsFaultStatus));
		printf("mali: m2c: AS1 parked mmu_raw %#x faultstatus %#x\n", raw, fs);
		passed = passed && !(raw & 0xffffu) && !(fs & 0xffu) && !(mrd(kIntRaw) & 1u);
	}
	m2c_free(t);
	printf("mali: %s: %s\n", gMaliM2c >= 2u ? "m2c_compute" : "m2c_as1_high_pa",
	    passed ? "PASS" : "FAIL");
}

/*
 * M2a: give one group one stream, and see whether the firmware accepts them.
 *
 * This is the step between "the firmware runs" and "the firmware does work". M1 proved the
 * global interface answers; a command stream group with a command stream bound to a ring is what
 * a job is later submitted through, so it is the next thing that either works or does not.
 *
 * The group is built from four writes and one doorbell:
 *
 *   1. the group's own control block (shared + 0x1000 + group_stride * 0) already carries
 *      input_va and output_va, put there by the firmware; read them rather than assume them
 *   2. the same for stream 0 inside that group (control + 0x40 + stream_stride * 0)
 *   3. the ring is allocated on this side and its address goes into the stream's input
 *   4. CSG_REQ then CS_REQ, each with the state bits the firmware expects
 *   5. ring doorbell 0
 *
 * Every value read from the firmware is printed before it is used, so a run that fails says
 * which offset was unexpected instead of only that the doorbell went unanswered.
 */
#define MALI_CS_RING_SIZE       0x1000u         /* one page: room for the NOPs a later trial needs */

static bool
m2_try_group_zero(struct m2c_trial *trial)
{
	volatile struct glb_control *glb_control;
	volatile struct glb_input *glb_in;
	volatile struct glb_output *glb_out;
	volatile struct csg_control *csg_control;
	volatile struct csg_input *csg_in;
	volatile struct csg_output *csg_out;
	volatile struct cs_control *cs_control;
	volatile struct cs_input *cs_in;
	volatile struct cs_output *cs_out;
	void *ring;
	uint64_t ring_va;
	uint64_t user_in_va;
	uint64_t user_out_va;
	uint64_t sus_va;
	uint32_t waited;
	uint32_t ack;
	uint32_t core_count;
	uint32_t tiler_count;

	if (gShared == NULL || gShared->mem.kva == 0) {
		printf("mali: m2: no shared page; nothing to build a group from\n");
		return false;
	}
	if (trial && (gShared->va_start != MALI_SHARED_VA ||
	    !m2c_shared_fits(MALI_SHARED_VA, sizeof(*glb_control)) ||
	    !m2c_shared_fits(MALI_SHARED_VA + kCsfGroupControlOffset, sizeof(*csg_control)) ||
	    !m2c_shared_fits(MALI_SHARED_VA + kCsfGroupControlOffset +
	    kCsfStreamControlOffset, sizeof(*cs_control)))) {
		return false;
	}
	glb_control = (volatile struct glb_control *)gShared->mem.kva;
	printf("mali: m2: glb groups %u stride %#x\n",
	    glb_control->group_num, glb_control->group_stride);
	if (glb_control->group_num == 0u || glb_control->group_stride == 0u) {
		printf("mali: m2: the firmware reports no groups; stopping here\n");
		return false;
	}

	csg_control = (volatile struct csg_control *)shared_at(MALI_SHARED_VA +
	    kCsfGroupControlOffset);
	if (csg_control == NULL) {
		printf("mali: m2: group 0 control is outside the shared section\n");
		return false;
	}
	csg_in = (volatile struct csg_input *)shared_at(csg_control->input_va);
	csg_out = (volatile struct csg_output *)shared_at(csg_control->output_va);
	if (csg_in == NULL || csg_out == NULL) {
		printf("mali: m2: group 0 input/output is outside the shared section\n");
		return false;
	}

	cs_control = (volatile struct cs_control *)shared_at(MALI_SHARED_VA +
	    kCsfGroupControlOffset + kCsfStreamControlOffset);
	if (cs_control == NULL) {
		printf("mali: m2: stream 0 control is outside the shared section\n");
		return false;
	}
	cs_in = (volatile struct cs_input *)shared_at(cs_control->input_va);
	cs_out = (volatile struct cs_output *)shared_at(cs_control->output_va);
	if (cs_in == NULL || cs_out == NULL) {
		printf("mali: m2: stream 0 input/output is outside the shared section\n");
		return false;
	}

	glb_in = (volatile struct glb_input *)shared_at(glb_control->input_va);
	glb_out = (volatile struct glb_output *)shared_at(glb_control->output_va);
	if (glb_in == NULL || glb_out == NULL) {
		printf("mali: m2: no global input/output block\n");
		return false;
	}

	/*
	 * Where the ring and the suspend buffer go.
	 *
	 * shared + 0x1000 is the group and stream control page, so the ring cannot go there -- an
	 * earlier version of this put it exactly there and memset() the descriptors it was about
	 * to read. The firmware's own structures end at 0x54000 (CS0's output); the space after
	 * that, up to the section's end at 0x94000, is unused.
	 */
#define MALI_CS_RING_OFFSET     0x60000u
#define MALI_CS_USER_IN_OFFSET  (MALI_CS_RING_OFFSET + MALI_CS_RING_SIZE)
#define MALI_CS_USER_OUT_OFFSET (MALI_CS_USER_IN_OFFSET + 0x1000u)
#define MALI_CS_SUSPEND_OFFSET  (MALI_CS_USER_OUT_OFFSET + 0x1000u)
#define MALI_CS_SUSPEND_SIZE    0x7000u
	if (trial && (!m2c_shared_fits(glb_control->input_va, sizeof(*glb_in)) ||
	    !m2c_shared_fits(glb_control->output_va, sizeof(*glb_out)) ||
	    !m2c_shared_fits(csg_control->input_va, sizeof(*csg_in)) ||
	    !m2c_shared_fits(csg_control->output_va, sizeof(*csg_out)) ||
	    !m2c_shared_fits(cs_control->input_va, sizeof(*cs_in)) ||
	    !m2c_shared_fits(cs_control->output_va, sizeof(*cs_out)) ||
	    !m2c_shared_fits(MALI_SHARED_VA + MALI_CS_USER_IN_OFFSET,
	    MALI_CS_SUSPEND_OFFSET + MALI_CS_SUSPEND_SIZE - MALI_CS_USER_IN_OFFSET))) {
		printf("mali: m2c: firmware interface out of bounds\n");
		return false;
	}
	if (MALI_CS_SUSPEND_OFFSET + MALI_CS_SUSPEND_SIZE >
	    (gShared->va_end - gShared->va_start)) {
		printf("mali: m2: the shared section is too small for a ring\n");
		return false;
	}
	if (trial) {
		FlushPoC_DcacheRegion((vm_offset_t)csg_out, sizeof(*csg_out));
		if ((csg_out->ack & kCsgStateMask) || (csg_in->req & kCsgStateMask) ||
		    csg_control->suspend_size > MALI_CS_SUSPEND_SIZE) {
			printf("mali: m2c: group 0 occupied or suspend buffer too small\n");
			return false;
		}
		ring = (void *)trial->ring;
		ring_va = M2C_RING_PAGE_VA + M2C_RING_OFFSET;
	} else {
		ring = (void *)(gShared->mem.kva + MALI_CS_RING_OFFSET);
		ring_va = MALI_SHARED_VA + MALI_CS_RING_OFFSET;
	}
	user_in_va = MALI_SHARED_VA + MALI_CS_USER_IN_OFFSET;
	user_out_va = MALI_SHARED_VA + MALI_CS_USER_OUT_OFFSET;
	sus_va = MALI_SHARED_VA + MALI_CS_SUSPEND_OFFSET;
	memset(ring, 0, MALI_CS_RING_SIZE);
	if (trial) {
		FlushPoC_DcacheRegion((vm_offset_t)ring, MALI_CS_RING_SIZE);
	}
	memset((void *)(gShared->mem.kva + MALI_CS_USER_IN_OFFSET), 0, 0x1000u);
	memset((void *)(gShared->mem.kva + MALI_CS_USER_OUT_OFFSET), 0, 0x1000u);
	memset((void *)(gShared->mem.kva + MALI_CS_SUSPEND_OFFSET), 0, MALI_CS_SUSPEND_SIZE);

	/*
	 * The order matters, and it is the order panthor_sched.c uses: the stream is programmed
	 * first, then the group's endpoint request, then the group is started, and only then does
	 * the stream's own start take effect.
	 *
	 * cs_slot_prog_locked() is called from inside csg_slot_prog_locked() (sched.c:1441), which
	 * is why the stream's fields are set before the group is asked to start. A group told to
	 * start before its stream exists acknowledges the request and reports an endpoint count of
	 * zero, which is exactly what trials 931g-931k showed: ack 0x11 with status_endpoint_current
	 * 0, and a stream that never left state zero.
	 */

	/* 1. the stream: its ring, its doorbell, its idle policy. */
	cs_in->config = kCsConfigPriority(0) | kCsConfigDoorbell(1);
	cs_in->ack_irq_mask = 0xffffffffu;
	cs_in->ringbuf_base = ring_va;
	cs_in->ringbuf_size = MALI_CS_RING_SIZE;
	/*
	 * These two are not optional, and zero is not a value they can have.
	 *
	 * ringbuf_input points at a { insert, extract } pair and ringbuf_output at an
	 * { extract, active } pair (panthor_fw.h:15-23). The firmware keeps the ring's producer and
	 * consumer positions there, so it dereferences both addresses. Left at zero they would name
	 * VA 0 in AS0 -- the firmware's own vector table -- and a stream that cannot record its
	 * position has no reason to leave state zero.
	 */
	cs_in->ringbuf_input = user_in_va;
	cs_in->ringbuf_output = user_out_va;
	cs_in->heap_start = 0ull;
	cs_in->heap_end = 0ull;
	cs_in->instr_config = 0u;
	cs_in->instrbuf_size = 0u;
	cs_in->instrbuf_base = 0ull;
	cs_in->instrbuf_offset_ptr = 0ull;
	cs_in->req = kCsStateStart | kCsIdleSyncWait | kCsIdleEmpty;

	/* 2. the group: which cores it may use, and how many it asks for. */
	core_count = (uint32_t)__builtin_popcountll(mrd(kShaderPresent));
	tiler_count = (uint32_t)__builtin_popcountll(mrd(kTilerPresent));
	csg_in->allow_compute = mrd(kShaderPresent);
	csg_in->allow_fragment = mrd(kShaderPresent);
	csg_in->allow_other = (uint32_t)mrd(kTilerPresent);
	csg_in->endpoint_req = (core_count & 0xffu) | ((core_count & 0xffu) << 8) |
	    ((tiler_count & 0xfu) << 16);
	csg_in->endpoint_req2 = 0ull;
	csg_in->suspend_buf = sus_va;
	csg_in->protm_suspend_buf = 0ull;
	csg_in->config = trial ? trial->vm.slot : 0u;
	csg_in->ack_irq_mask = 0xffffffffu;
	printf("mali: m2: cs cfg %#x base %#llx size %#x | csg ep_req %#x cores %u tiler %u\n",
	    cs_in->config, (unsigned long long)cs_in->ringbuf_base, cs_in->ringbuf_size,
	    csg_in->endpoint_req, core_count, tiler_count);

	/*
	 * 3. one doorbell, and it starts both.
	 *
	 * mali_kbase_csf_scheduler.c:2187-2251 does not ring twice. The stream's request is written
	 * first, the group's request second, both doorbell words are flipped once, and a single
	 * write to DOORBELL(0) wakes the firmware to look at all of it. Ringing again for the
	 * stream afterwards is not what the vendor driver does -- and the second ring cannot even
	 * be expressed in this protocol, since a toggle that flips to the value already there is
	 * not a request.
	 *
	 * DOORBELL(0) is the kernel's own doorbell and stays the kernel's: doorbell 1 is what the
	 * stream's config names for user submissions, which is a different doorbell rung at a
	 * different time.
	 *
	 * Both output words are read after being invalidated -- they live in cacheable memory on
	 * this side, and a stale read here is what made an earlier version think it had to ring
	 * again.
	 */
	if (trial) {
		trial->group_in = csg_in;
		trial->group_out = csg_out;
		trial->global_in = glb_in;
		trial->global_out = glb_out;
	}
	csg_in->req = kCsgStateStart | kCsgEndpointConfig;
	FlushPoC_DcacheRegion((vm_offset_t)csg_out, sizeof(*csg_out));
	FlushPoC_DcacheRegion((vm_offset_t)glb_out, sizeof(*glb_out));
	__asm__ volatile ("dsb sy" ::: "memory");
	csg_in->doorbell_req = (csg_in->doorbell_req & ~1u) |
	    ((csg_out->doorbell_ack ^ 1u) & 1u);
	glb_in->doorbell_req = (glb_in->doorbell_req & ~1u) |
	    ((glb_out->doorbell_ack ^ 1u) & 1u);
	mem_flush(&gShared->mem);
	__asm__ volatile ("dsb sy" ::: "memory");
	mwr(kCsfDoorbellBase, 1u);

	for (waited = 0; waited < 200; waited++) {
		FlushPoC_DcacheRegion((vm_offset_t)csg_out, sizeof(*csg_out));
		FlushPoC_DcacheRegion((vm_offset_t)cs_out, sizeof(*cs_out));
		__asm__ volatile ("dsb sy" ::: "memory");
		if ((csg_out->ack & kCsgStateMask) == kCsgStateStart &&
		    (cs_out->ack & kCsStateMask) == kCsStateStart) {
			break;
		}
		delay(1000);
	}
	printf("mali: m2: after one doorbell, %u ms: csg ack %#x cs ack %#x\n",
	    waited, csg_out->ack, cs_out->ack);
	printf("mali: m2: glb db req %#x ack %#x | csg db req %#x ack %#x\n",
	    glb_in->doorbell_req, glb_out->doorbell_ack,
	    csg_in->doorbell_req, csg_out->doorbell_ack);
	printf("mali: m2: csg cs_irq_req %#x state %#x ep_cur %#x ep_req %#x\n",
	    csg_out->cs_irq_req, csg_out->status_state,
	    csg_out->status_endpoint_current, csg_out->status_endpoint_req);
	printf("mali: m2: cs fault %#x fatal %#x cmd_ptr %#llx\n",
	    cs_out->fault, cs_out->fatal, (unsigned long long)cs_out->status_cmd_ptr);
	printf("mali: m2: irq raw %#x job %#x mmu %#x\n",
	    mrd(kIntRaw), mrd(kJobIntRaw), mrd(kMmuIntRaw));

	if ((csg_out->ack & kCsgStateMask) != kCsgStateStart) {
		printf("mali: m2: the group did not start (ack %#x)\n", csg_out->ack);
		return false;
	}
	if ((cs_out->ack & kCsStateMask) != kCsStateStart) {
		printf("mali: m2: the stream did not start (ack %#x)\n", cs_out->ack);
		return false;
	}
	if ((cs_out->ack & kCsFatalBit) || cs_out->fatal != 0u) {
		printf("mali: m2: the stream started but the firmware reported a fatal (ack %#x "
		    "fatal %#x); the ring contents are the next thing to look at\n",
		    cs_out->ack, cs_out->fatal);
		if (trial) {
			return false;
		}
	}

	printf("mali: m2: group 0 stream 0 running, ring %#llx\n",
	    (unsigned long long)ring_va);

	/*
	 * M2b: put instructions in the ring and see whether the CEU fetches them.
	 *
	 * Everything up to here proved the firmware accepted a group and a stream. None of it
	 * proved anything reads the ring. These two steps do, and they are separate on purpose:
	 *
	 *   A. eight NOPs. Nothing can go wrong inside a NOP, so if the ring's extract pointer
	 *      moves, the fetch path works -- decode, prefetch, the lot.
	 *   B. a short real sequence that writes the GPU's own cycle counter to memory and then
	 *      adds a constant to a second word. If those two addresses in DRAM change, the GPU
	 *      executed instructions and wrote to memory.
	 *
	 * The encoding is the one panthor_sched.c:3218-3265 builds, verified field by field:
	 * every instruction is one 64-bit little-endian word with its opcode in bits [63:56],
	 * and a submission is padded with NOPs to a 64-byte boundary because the prefetcher
	 * reads in cache lines.
	 */
	{
		struct cs_user_input *uin;
		struct cs_user_output *uout;
		volatile uint64_t *sync;
		volatile uint64_t *cycles;
		uint64_t *cmds;
		uint64_t seq_before;
		uint64_t cyc_before;

#define MALI_CS_SCRATCH_OFFSET  0x6a000u
#define MALI_CS_USER_IN_VA      (MALI_SHARED_VA + MALI_CS_USER_IN_OFFSET)
#define MALI_CS_USER_OUT_VA     (MALI_SHARED_VA + MALI_CS_USER_OUT_OFFSET)
#define MALI_CS_SCRATCH_VA      (MALI_SHARED_VA + MALI_CS_SCRATCH_OFFSET)
#define MALI_M2_SYNC_VA         (MALI_CS_SCRATCH_VA)

#define M2_OP_MOV48         1ull
#define M2_OP_MOV32         2ull
#define M2_OP_STORE_STATE   40ull
#define M2_OP_SYNC_ADD64    51ull
#define M2_OP_ERROR_BARRIER 47ull

		if (!trial && MALI_CS_SCRATCH_OFFSET + 0x1000u > (gShared->va_end - gShared->va_start)) {
			printf("mali: m2b: no room for a scratch page\n");
			return true;
		}
		vm_offset_t scratch = trial ? trial->result : gShared->mem.kva + MALI_CS_SCRATCH_OFFSET;
		uint64_t sync_va = trial ? M2C_RESULT_PAGE_VA + M2C_RESULT_OFFSET : MALI_M2_SYNC_VA;
		memset((void *)scratch, 0, 0x1000u);
		uin = (struct cs_user_input *)(gShared->mem.kva + MALI_CS_USER_IN_OFFSET);
		uout = (struct cs_user_output *)(gShared->mem.kva + MALI_CS_USER_OUT_OFFSET);
		sync = (volatile uint64_t *)scratch;
		cycles = (volatile uint64_t *)(scratch + 0x40);
		cmds = (uint64_t *)ring;

		*sync = 0x1000000000000000ull;
		*cycles = 0ull;
		mem_flush(&gShared->mem);
		__asm__ volatile ("dsb sy" ::: "memory");

		/* ---- Step A: eight NOPs, nothing but a fetch ---- */
		memset(cmds, 0, 64);
		if (trial) {
			FlushPoC_DcacheRegion(scratch, 0x1000u);
			FlushPoC_DcacheRegion((vm_offset_t)cmds, 64);
		}
		mem_flush(&gShared->mem);
		__asm__ volatile ("dsb sy" ::: "memory");
		uin->extract_init = 0ull;
		uin->insert = 64ull;
		FlushPoC_DcacheRegion((vm_offset_t)uin, sizeof(*uin));
		__asm__ volatile ("dsb sy" ::: "memory");
		mwr(kCsfDoorbellBase + kCsfDoorbellStride, 1u);   /* DOORBELL(1): the stream's own */

		for (waited = 0; waited < 100; waited++) {
			FlushPoC_DcacheRegion((vm_offset_t)uout, sizeof(*uout));
			FlushPoC_DcacheRegion((vm_offset_t)cs_out, sizeof(*cs_out));
			__asm__ volatile ("dsb sy" ::: "memory");
			if (uout->extract == 64ull) {
				break;
			}
			delay(1000);
		}
		printf("mali: m2b: step A: extract %llu (want 64) active %u after %u ms; "
		    "cs ack %#x fault %#x fatal %#x cmd_ptr %#llx\n",
		    (unsigned long long)uout->extract, uout->active, waited,
		    cs_out->ack, cs_out->fault, cs_out->fatal,
		    (unsigned long long)cs_out->status_cmd_ptr);
		printf("mali: m2b: step A: irq raw %#x job %#x mmu %#x\n",
		    mrd(kIntRaw), mrd(kJobIntRaw), mrd(kMmuIntRaw));
		if (uout->extract != 64ull) {
			printf("mali: m2b: the ring was not read; stopping before the real sequence\n");
			return trial == NULL;
		}

		/*
		 * ---- Step B: two real writes to DRAM ----
		 *
		 * The instructions go at ring + 64, not at ring. Step A consumed the first 64 bytes,
		 * and writing there again would leave the second cache line as the NOPs memset() put
		 * there -- the CEU would execute those, advance extract to 128, and this would look
		 * exactly like a job that ran and did nothing. Which is what boot-934 was.
		 */
		cmds = (uint64_t *)((uintptr_t)ring + 64);
		cmds[0] = (M2_OP_MOV48 << 56) | (0ull << 48) | ((sync_va + 0x40) & 0xffffffffffffull);
		cmds[1] = (M2_OP_STORE_STATE << 56) | (0ull << 40) | (1ull << 32);
		cmds[2] = (M2_OP_MOV48 << 56) | (0ull << 48) | (sync_va & 0xffffffffffffull);
		cmds[3] = (M2_OP_MOV48 << 56) | (2ull << 48) | 0x0000cafebabeull;
		cmds[4] = (M2_OP_SYNC_ADD64 << 56) | (0ull << 48) | (0ull << 40) |
		    (2ull << 32) | (0ull << 16) | 1ull;
		cmds[5] = (M2_OP_ERROR_BARRIER << 56);
		cmds[6] = 0ull;
		cmds[7] = 0ull;
		if (trial) {
			FlushPoC_DcacheRegion((vm_offset_t)cmds, 64);
		}
		mem_flush(&gShared->mem);
		__asm__ volatile ("dsb sy" ::: "memory");

		seq_before = *sync;
		cyc_before = *cycles;
		printf("mali: m2b: before step B: sync %#llx cycles %#llx\n",
		    (unsigned long long)seq_before, (unsigned long long)cyc_before);

		uin->extract_init = 64ull;
		uin->insert = 128ull;
		FlushPoC_DcacheRegion((vm_offset_t)uin, sizeof(*uin));
		__asm__ volatile ("dsb sy" ::: "memory");
		mwr(kCsfDoorbellBase + kCsfDoorbellStride, 1u);

		for (waited = 0; waited < 100; waited++) {
			FlushPoC_DcacheRegion((vm_offset_t)uout, sizeof(*uout));
			FlushPoC_DcacheRegion((vm_offset_t)cs_out, sizeof(*cs_out));
			FlushPoC_DcacheRegion((vm_offset_t)sync, 0x80);
			__asm__ volatile ("dsb sy" ::: "memory");
			if (uout->extract == 128ull && *sync == 0x10000000cafebabeull) {
				break;
			}
			delay(1000);
		}
		printf("mali: m2b: step B: extract %llu (want 128) active %u after %u ms\n",
		    (unsigned long long)uout->extract, uout->active, waited);
		printf("mali: m2b: step B: sync %#llx (want 0x10000000cafebabe) cycles %#llx "
		    "(hardware %#llx)\n",
		    (unsigned long long)*sync, (unsigned long long)*cycles,
		    (unsigned long long)reg_rd(gMfgcfg, MALI_MFG_TIMESTAMP));
		printf("mali: m2b: step B: cs ack %#x fault %#x fatal %#x cmd_ptr %#llx\n",
		    cs_out->ack, cs_out->fault, cs_out->fatal,
		    (unsigned long long)cs_out->status_cmd_ptr);
		printf("mali: m2b: step B: irq raw %#x job %#x mmu %#x\n",
		    mrd(kIntRaw), mrd(kJobIntRaw), mrd(kMmuIntRaw));

		if (trial) {
			uint32_t mmu = mrd(kMmuIntRaw), fs = mrd(asReg(trial->vm.slot, kAsFaultStatus));
			bool step_b_ok = uout->extract == 128ull && *sync == 0x10000000cafebabeull &&
			    !cs_out->fault && !cs_out->fatal &&
			    !((cs_out->ack ^ cs_in->req) & (3u << 30)) &&
			    !(mmu & 0xffffu) && !(fs & 0xffu) && !(mrd(kIntRaw) & 1u);
			if (!step_b_ok || gMaliM2c < 2u) {
				return step_b_ok;
			}

			/*
			 * ---- Step C (mali_m2c=2): Valhall v10 RUN_COMPUTE on Shader Cores ----
			 *
			 * 3-instruction shader assembled by Mesa 25.0.7 valhall/asm.py:
			 *   SHADDX.u64 r0, u0, r60.w0, shift:0x2     ; r0:r1 = out_va + tid * 4
			 *   IADD.u32   r2, u2, ^r60                  ; r2 = 0xdead0000 + tid
			 *   STORE.i32.slot0.end @r2, ^r0, offset:0   ; out[tid] = r2; wait+end
			 *
			 * Dispatches 4 workgroups x (4,1,1) threads = 16 lanes writing
			 * out[0..15] = 0xdead0000..0xdead000f, then FLUSH_CACHE2 + SYNC_ADD64.
			 */
#define M2_OP_WAIT          3ull
#define M2_OP_RUN_COMPUTE   4ull
#define M2_OP_SET_SB_ENTRY  23ull
#define M2_OP_REQ_RESOURCE  34ull
#define M2_OP_FLUSH_CACHE2  36ull
#define M2C_COMPUTE_THREADS 16u
#define M2C_COMPUTE_MAGIC   0xdead0000u
#define M2C_COMPUTE_SYNC_INC 0x000012345678ull
#define M2C_COMPUTE_SYNC_END (0x10000000cafebabeull + M2C_COMPUTE_SYNC_INC)

			uint64_t shader_va = M2C_SHADER_PAGE_VA + M2C_SHADER_OFFSET;
			uint64_t spd_va = sync_va + 0x100ull;
			uint64_t tsd_va = sync_va + 0x140ull;
			uint64_t fau_va = sync_va + 0x180ull;
			uint64_t out_va = sync_va + 0x200ull;
			uint64_t fau_reg = (fau_va & 0xffffffffffffull) | (2ull << 56);
			uint64_t *code = (uint64_t *)trial->shader;
			uint32_t *spd = (uint32_t *)(scratch + 0x100u);
			uint32_t *tsd = (uint32_t *)(scratch + 0x140u);
			uint64_t *fau = (uint64_t *)(scratch + 0x180u);
			volatile uint32_t *out = (volatile uint32_t *)(scratch + 0x200u);
			unsigned n = 0, matched = 0;

			memset(code, 0, 64);
			code[0] = 0x01a3c00020273c80ull;
			code[1] = 0x00a0c20000007c82ull;
			code[2] = 0x7861420218000040ull;

			/* v10.xml Shader Program (32 B, 32 B aligned):
			 * word0: Type=8(Shader) | Stage=1(Compute) | RegAlloc=0(64/thread)
			 * word1: Preload.R60 (bit 12 = 0x1000) for global_invocation_id.x */
			memset(spd, 0, 32);
			spd[0] = 8u | (1u << 4) | (0u << 30);
			spd[1] = 1u << 12;
			spd[2] = (uint32_t)shader_va;
			spd[3] = (uint32_t)(shader_va >> 32);

			/* v10.xml Local Storage (32 B, 64 B aligned):
			 * word1[4:0]: WLS Instances = log2(0x80000000) = 31 (NO_WORKGROUP_MEM) */
			memset(tsd, 0, 32);
			tsd[1] = 31u;

			/* FAU: u0:u1 = out_va, u2 = M2C_COMPUTE_MAGIC, u3 = 0 */
			fau[0] = out_va;
			fau[1] = (uint64_t)M2C_COMPUTE_MAGIC;

			for (unsigned i = 0; i < M2C_COMPUTE_THREADS; i++) {
				out[i] = 0x55555555u;
			}
			FlushPoC_DcacheRegion(trial->shader, 64);
			FlushPoC_DcacheRegion(scratch, 0x1000u);

			cmds = (uint64_t *)((uintptr_t)ring + 128);
			memset(cmds, 0, 256);
			cmds[n++] = (M2_OP_REQ_RESOURCE << 56) | 1ull;                    /* Compute=1 */
			cmds[n++] = (M2_OP_SET_SB_ENTRY << 56) | (2ull << 0) | (0ull << 4);
			cmds[n++] = (M2_OP_MOV48 << 56) | (0ull << 48) | 0ull;            /* SRT=0 */
			cmds[n++] = (M2_OP_MOV32 << 56) | (8ull << 48) | (fau_reg & 0xffffffffull);
			cmds[n++] = (M2_OP_MOV32 << 56) | (9ull << 48) | (fau_reg >> 32);
			cmds[n++] = (M2_OP_MOV48 << 56) | (16ull << 48) | (spd_va & 0xffffffffffffull);
			cmds[n++] = (M2_OP_MOV48 << 56) | (24ull << 48) | (tsd_va & 0xffffffffffffull);
			cmds[n++] = (M2_OP_MOV32 << 56) | (32ull << 48) | 0ull;           /* attr_off=0 */
			cmds[n++] = (M2_OP_MOV32 << 56) | (33ull << 48) | (3ull | (1ull << 31)); /* wg(4,1,1) */
			cmds[n++] = (M2_OP_MOV32 << 56) | (34ull << 48) | 0ull;           /* offset X */
			cmds[n++] = (M2_OP_MOV32 << 56) | (35ull << 48) | 0ull;           /* offset Y */
			cmds[n++] = (M2_OP_MOV32 << 56) | (36ull << 48) | 0ull;           /* offset Z */
			cmds[n++] = (M2_OP_MOV32 << 56) | (37ull << 48) | 4ull;           /* grid X=4 wg */
			cmds[n++] = (M2_OP_MOV32 << 56) | (38ull << 48) | 1ull;           /* grid Y=1 */
			cmds[n++] = (M2_OP_MOV32 << 56) | (39ull << 48) | 1ull;           /* grid Z=1 */
			cmds[n++] = (M2_OP_RUN_COMPUTE << 56) | (0ull << 14) | 4ull;      /* inc=4 axis=X */
			cmds[n++] = (M2_OP_WAIT << 56) | (0xffull << 16);                 /* wait all sb */
			cmds[n++] = (M2_OP_MOV32 << 56) | (74ull << 48) | 0ull;           /* flush_id=0 */
			cmds[n++] = (M2_OP_FLUSH_CACHE2 << 56) | (0ull << 48) | (74ull << 40) |
			    (1ull << 9) | (3ull << 4) | 3ull;                             /* L2+LSC CI */
			cmds[n++] = (M2_OP_WAIT << 56) | (1ull << 16);                    /* wait slot 0 */
			cmds[n++] = (M2_OP_MOV48 << 56) | (0ull << 48) | (sync_va & 0xffffffffffffull);
			cmds[n++] = (M2_OP_MOV48 << 56) | (2ull << 48) | M2C_COMPUTE_SYNC_INC;
			cmds[n++] = (M2_OP_SYNC_ADD64 << 56) | (0ull << 48) | (0ull << 40) |
			    (2ull << 32) | (0ull << 16) | 1ull;
			cmds[n++] = (M2_OP_ERROR_BARRIER << 56);
			FlushPoC_DcacheRegion((vm_offset_t)cmds, 256);
			mem_flush(&gShared->mem);
			__asm__ volatile ("dsb sy" ::: "memory");

			uin->extract_init = 128ull;
			uin->insert = 384ull;
			FlushPoC_DcacheRegion((vm_offset_t)uin, sizeof(*uin));
			__asm__ volatile ("dsb sy" ::: "memory");
			mwr(kCsfDoorbellBase + kCsfDoorbellStride, 1u);

			for (waited = 0; waited < 200; waited++) {
				FlushPoC_DcacheRegion((vm_offset_t)uout, sizeof(*uout));
				FlushPoC_DcacheRegion((vm_offset_t)cs_out, sizeof(*cs_out));
				FlushPoC_DcacheRegion((vm_offset_t)csg_out, sizeof(*csg_out));
				FlushPoC_DcacheRegion(scratch, 0x300u);
				__asm__ volatile ("dsb sy" ::: "memory");
				if (uout->extract == 384ull && *sync == M2C_COMPUTE_SYNC_END) {
					break;
				}
				delay(1000);
			}
			for (unsigned i = 0; i < M2C_COMPUTE_THREADS; i++) {
				if (out[i] == M2C_COMPUTE_MAGIC + i) {
					matched++;
				}
			}
			mmu = mrd(kMmuIntRaw);
			fs = mrd(asReg(trial->vm.slot, kAsFaultStatus));
			printf("mali: m2c: step C compute: extract %llu (want 384) after %u ms | "
			    "shader_ready %#x ep_cur %#x\n",
			    (unsigned long long)uout->extract, waited,
			    mrd(kShaderReady), csg_out->status_endpoint_current);
			printf("mali: m2c: step C sync %#llx (want %#llx) matched %u/%u | "
			    "out[0..3]=%#x %#x %#x %#x out[12..15]=%#x %#x %#x %#x\n",
			    (unsigned long long)*sync, (unsigned long long)M2C_COMPUTE_SYNC_END,
			    matched, M2C_COMPUTE_THREADS,
			    out[0], out[1], out[2], out[3],
			    out[12], out[13], out[14], out[15]);
			printf("mali: m2c: step C cs ack %#x fault %#x fatal %#x cmd_ptr %#llx | "
			    "irq raw %#x job %#x mmu %#x fs %#x\n",
			    cs_out->ack, cs_out->fault, cs_out->fatal,
			    (unsigned long long)cs_out->status_cmd_ptr,
			    mrd(kIntRaw), mrd(kJobIntRaw), mmu, fs);
			return uout->extract == 384ull && *sync == M2C_COMPUTE_SYNC_END &&
			    matched == M2C_COMPUTE_THREADS &&
			    !cs_out->fault && !cs_out->fatal &&
			    !((cs_out->ack ^ cs_in->req) & (3u << 30)) &&
			    !(mmu & 0xffffu) && !(fs & 0xffu) && !(mrd(kIntRaw) & 1u);
		}
		if (*sync == 0x10000000cafebabeull) {
			printf("mali: m2b: PASS -- the GPU executed instructions and wrote memory\n");
		} else if (uout->extract == 128ull) {
			printf("mali: m2b: the ring was consumed but the writes did not land "
			    "(sync %#llx)\n", (unsigned long long)*sync);
		} else {
			printf("mali: m2b: the ring was not consumed at all\n");
		}
	}
	return true;
}

/* ------------------------------------------------------------------ M3: Panthor UAPI backend */

/*
 * Kernel backend for the IOKit HakiosGPU service (iokit/Kernel/hakios.mali610.cpp).
 *
 * Activated only when boot-arg mali_gpu=1 is set. AS0 stays untouched for the CSF MCU firmware;
 * user VMs bind to AS1..AS7 (first VM always gets AS1) with dynamic 4 KiB page tables, region
 * locking (AS_LOCKADDR + AS_COMMAND_LOCK + GPU_FLUSH_CACHES(3,3,2) + AS_COMMAND_UNLOCK), and
 * automatic reclamation of empty page tables. User command streams execute via kernel-owned CS
 * ringbuffers that emit FLUSH_CACHE2 + CALL + scoreboard WAIT + FLUSH_CACHE2 + SYNC_ADD64 +
 * ERROR_BARRIER using the top unpreserved CS registers reported by the firmware.
 */

struct mali610_gpu_info {
	uint32_t gpu_id;
	uint32_t gpu_rev;
	uint32_t csf_id;
	uint32_t l2_features;
	uint32_t tiler_features;
	uint32_t mem_features;
	uint32_t mmu_features;
	uint32_t thread_features;
	uint32_t max_threads;
	uint32_t thread_max_workgroup_size;
	uint32_t thread_max_barrier_size;
	uint32_t coherency_features;
	uint32_t texture_features[4];
	uint32_t as_present;
	uint32_t selected_coherency;
	uint64_t shader_present;
	uint64_t l2_present;
	uint64_t tiler_present;
	uint32_t core_features;
	uint32_t pad;
	uint64_t gpu_features;
};

struct mali610_csif_info {
	uint32_t csg_slot_count;
	uint32_t cs_slot_count;
	uint32_t cs_reg_count;
	uint32_t scoreboard_slot_count;
	uint32_t unpreserved_cs_reg_count;
	uint32_t pad;
};

struct mali610_timestamp_info {
	uint64_t timestamp_frequency;
	uint64_t current_timestamp;
	uint64_t timestamp_offset;
	uint32_t flags;
	uint32_t duration_nsec;
	uint64_t cycle_count;
	uint64_t cpu_timestamp_sec;
	uint64_t cpu_timestamp_nsec;
};

struct mali610_queue_spec {
	uint8_t priority;
	uint8_t pad[3];
	uint32_t ringbuf_size;
};

struct mali610_group_create_args {
	uint32_t vm_id;
	uint8_t max_compute;
	uint8_t max_fragment;
	uint8_t max_tiler;
	uint8_t priority;
	uint64_t compute_mask;
	uint64_t fragment_mask;
	uint64_t tiler_mask;
	uint32_t queue_count;
	const struct mali610_queue_spec *queues;
};

bool hakios_mali610_ready(void);
uint32_t hakios_mali610_bump_flush_id(void);
bool hakios_mali610_query_gpu_info(struct mali610_gpu_info *out);
bool hakios_mali610_query_csif_info(struct mali610_csif_info *out);
bool hakios_mali610_query_timestamp_info(struct mali610_timestamp_info *inout);
void hakios_mali610_flush_dcache(void *kva, size_t size);
void hakios_mali610_report_fault(const char *why);
int hakios_mali610_vm_create(uint64_t *inout_user_va_range, uint32_t *out_vm_id);
int hakios_mali610_vm_destroy(uint32_t vm_id);
int hakios_mali610_vm_get_state(uint32_t vm_id, uint32_t *out_state);
int hakios_mali610_vm_map_pas(uint32_t vm_id, uint64_t va, const uint64_t *pas,
    uint32_t count_4k, uint32_t map_flags);
int hakios_mali610_vm_unmap(uint32_t vm_id, uint64_t va, uint64_t size);
int hakios_mali610_vm_alloc_kernel_va(uint32_t vm_id, uint64_t size, uint64_t align,
    uint64_t *out_va);
void hakios_mali610_vm_free_kernel_va(uint32_t vm_id, uint64_t va, uint64_t size);
int hakios_mali610_group_create(const struct mali610_group_create_args *args,
    uint32_t *out_hw_group);
int hakios_mali610_group_destroy(uint32_t hw_group);
int hakios_mali610_group_get_state(uint32_t hw_group, uint32_t *out_state,
    uint32_t *out_fatal_queues);
int hakios_mali610_queue_submit(uint32_t hw_group, uint32_t queue_idx,
    uint64_t stream_addr, uint32_t stream_size, uint32_t latest_flush, uint64_t *out_seq);
int hakios_mali610_queue_poll(uint32_t hw_group, uint32_t queue_idx,
    uint64_t target_seq, bool *out_done);
void hakios_mali610_group_mark_timedout(uint32_t hw_group);
extern bool hakios_mali610_tiler_oom_grow(uint32_t vm_id, uint64_t heap_ctx_va,
    uint64_t *out_encoded_va, uint32_t *out_chunk_count);

#define kGpuTimestampOffset     0x088u
#define kThreadMaxWorkgroup     0x0a4u
#define kThreadMaxBarrier       0x0a8u
#define kTextureFeatures0       0x0b0u
#define kGpuRevId               0x280u
#define kGpuCmdFlushAllCaches   0x23304u        /* GPU_FLUSH_CACHES(L2=CI, LSC=CI, OTH=INV) */
#define kAsCmdUnlock            3u              /* panthor_mmu_regs.h: AS_COMMAND_UNLOCK */

#define kCsTilerOomBit          (1u << 26)
#define kCsProTmPendingBit      (1u << 27)
#define kCsFaultBit             (1u << 31)
#define kCsEvtMask              (kCsTilerOomBit | kCsProTmPendingBit | kCsFatalBit | kCsFaultBit)

#define MALI_UVM_SLOTS          7u              /* AS1..AS7 */
#define MALI_UVM_DEFAULT_USER   (1ull << 47)
#define MALI_UVM_FULL_VA        (1ull << 48)
#define MALI_UVM_KVA_HOLES      64u

#define MALI_UAPI_MAX_CSGS      4u              /* MAX_CSG_PRIO + 1 */
#define MALI_UAPI_MAX_QUEUES    4u
#define MALI_UAPI_JOB_WORDS     16u             /* 16 * 8 = 128 B (2 cache lines) per submit */
#define MALI_UAPI_JOB_BYTES     (MALI_UAPI_JOB_WORDS * sizeof(uint64_t))

#define MALI_UAPI_SHARED_BASE   0x58000u        /* free region in AS0 shared section [0x58000, 0x94000) */
#define MALI_UAPI_SHARED_END    0x94000u
#define MALI_UAPI_SHARED_PAGES  ((MALI_UAPI_SHARED_END - MALI_UAPI_SHARED_BASE) >> 12)

#define MALI_MAP_RDONLY         (1u << 0)
#define MALI_MAP_NOEXEC         (1u << 1)
#define MALI_MAP_UNCACHED       (1u << 2)

struct mali_uvm_pt {
	uint64_t entry[512];
	struct mali_uvm_pt *next;
	uint64_t pa;
	uint16_t live_entries;
};

struct mali_kva_hole {
	uint64_t va;
	uint64_t size;
};

struct mali_uvm {
	bool active;
	bool unusable;
	bool hw_parked;
	unsigned as_slot;                       /* 1..7 */
	struct mali_uvm_pt *root;
	struct mali_uvm_pt *tables;
	uint64_t user_va_range;
	uint64_t kva_base;
	uint64_t kva_next;
	uint64_t kva_end;
	struct mali_kva_hole holes[MALI_UVM_KVA_HOLES];
};

struct mali_uapi_queue {
	bool active;
	bool oom_handling;
	uint8_t priority;
	uint8_t doorbell_id;                    /* 1..15 */
	uint32_t ring_size;
	struct mali_mem ring_mem;
	uint64_t ring_gpu_va;
	uint32_t shared_off;                    /* 1 x 4 KiB page in gShared: +0 in, +0x800 out */
	uint64_t user_in_va;
	uint64_t user_out_va;
	volatile struct cs_user_input *uin;
	volatile struct cs_user_output *uout;
	volatile struct cs_input *cs_in;
	volatile struct cs_output *cs_out;
	uint64_t insert_pos;
	uint64_t submitted_seq;
	uint64_t completed_seq;
};

struct mali_uapi_group {
	bool active;
	bool terminated;
	bool timedout;
	uint32_t fatal_queues;
	unsigned csg_slot;                      /* 0..3 */
	uint32_t vm_id;                         /* 1..7 */
	uint32_t queue_count;
	struct mali_uapi_queue queues[MALI_UAPI_MAX_QUEUES];
	struct mali_mem sync_mem;               /* 64 B per queue at sync_mem.kva + q * 64 */
	uint64_t sync_gpu_va;
	uint32_t sus_off;
	uint32_t sus_pages;
	uint32_t protm_off;
	uint32_t protm_pages;
	volatile struct csg_input *csg_in;
	volatile struct csg_output *csg_out;
	volatile struct glb_input *glb_in;
	volatile struct glb_output *glb_out;
};

static struct mali_uvm gUserVms[MALI_UVM_SLOTS];
static struct mali_uapi_group gUserGroups[MALI_UAPI_MAX_CSGS];
static uint64_t gSharedPageMask;
static uint32_t gDoorbellMask = 0x1u;           /* bit 0 reserved for CSF_GLB_DOORBELL_ID */
static volatile uint32_t gMaliLatestFlushId = 1u;

static bool
mali_uapi_hw_live_locked(void)
{
	return gMaliGpu && gMaliDone && !gMaliStopped &&
	    !__atomic_load_n(&gMaliStopping, __ATOMIC_ACQUIRE) &&
	    gGpu != 0 && gpueb_bringup_held && gShared != NULL && gShared->mem.kva != 0;
}

bool
hakios_mali610_ready(void)
{
	return gMaliGpu && gMaliDone && !gMaliStopped &&
	    !__atomic_load_n(&gMaliStopping, __ATOMIC_ACQUIRE);
}

uint32_t
hakios_mali610_bump_flush_id(void)
{
	uint32_t next = __atomic_add_fetch(&gMaliLatestFlushId, 1u, __ATOMIC_ACQ_REL);
	if (next == 0u) {
		next = __atomic_add_fetch(&gMaliLatestFlushId, 1u, __ATOMIC_ACQ_REL);
	}
	return next;
}

void
hakios_mali610_flush_dcache(void *kva, size_t size)
{
	if (kva != NULL && size != 0) {
		FlushPoC_DcacheRegion((vm_offset_t)kva, size);
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

static uint64_t
gpu_rd64_counter(uint32_t at)
{
	uint32_t hi1, lo, hi2;

	do {
		hi1 = mrd(at + 4u);
		lo = mrd(at);
		hi2 = mrd(at + 4u);
	} while (hi1 != hi2);
	return ((uint64_t)hi2 << 32) | (uint64_t)lo;
}

static bool
mali_gpu_flush_all_caches_locked(void)
{
	unsigned spins;

	mwr(kIntClear, kGpuIrqCleanCaches);
	mwr(kGpuCmd, kGpuCmdFlushAllCaches);
	for (spins = 0; spins < 100000u; spins++) {
		if (mrd(kIntRaw) & kGpuIrqCleanCaches) {
			mwr(kIntClear, kGpuIrqCleanCaches);
			return true;
		}
		delay(1);
	}
	printf("mali: uapi: cache flush timed out (status %#x raw %#x)\n",
	    mrd(kGpuStatus), mrd(kIntRaw));
	return false;
}

static uint64_t
mali_pack_lock_region(uint64_t start, uint64_t size)
{
	uint64_t end = start + size;
	uint64_t diff = start ^ (end - 1ull);
	unsigned fls = diff ? (64u - (unsigned)__builtin_clzll(diff)) : 0u;
	unsigned width = (fls > 15u ? fls : 15u) - 1u;

	if (width > 47u) {
		width = 47u;
	}
	uint64_t mask = (width >= 63u) ? 0ull : ~((1ull << (width + 1u)) - 1ull);
	return (start & mask) | (uint64_t)width;
}

static bool
mali_as_lock_region_locked(unsigned as, uint64_t va, uint64_t size)
{
	if (size == 0 || !as_ready(as)) {
		return false;
	}
	gpu_wr64(asReg(as, kAsLockaddr), mali_pack_lock_region(va, size));
	__asm__ volatile ("dsb sy" ::: "memory");
	return as_command(as, kAsCmdLock);
}

static bool
mali_as_unlock_region_locked(unsigned as)
{
	bool flushed = mali_gpu_flush_all_caches_locked();
	bool unlocked = as_command(as, kAsCmdUnlock);

	(void)hakios_mali610_bump_flush_id();
	return flushed && unlocked;
}

static bool
mali_uvm_pa_valid(uint64_t pa)
{
	return pa >= 0x40000000ull && (pa & (PT_LEAF_GRANULE - 1ull)) == 0ull && pa < (1ull << 40);
}

static struct mali_uvm_pt *
mali_uvm_alloc_pt(struct mali_uvm *vm)
{
	vm_offset_t kva = 0;

	if (sizeof(struct mali_uvm_pt) > PAGE_SIZE ||
	    kmem_alloc(kernel_map, &kva, PAGE_SIZE, KMA_KOBJECT | KMA_DATA | KMA_ZERO,
	    VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		return NULL;
	}
	struct mali_uvm_pt *pt = (struct mali_uvm_pt *)kva;
	pt->pa = kvtophys(kva);
	if (!mali_uvm_pa_valid(pt->pa)) {
		kmem_free(kernel_map, kva, PAGE_SIZE);
		return NULL;
	}
	FlushPoC_DcacheRegion((vm_offset_t)pt->entry, sizeof(pt->entry));
	__asm__ volatile ("dsb sy" ::: "memory");
	pt->next = vm->tables;
	vm->tables = pt;
	return pt;
}

static struct mali_uvm_pt *
mali_uvm_find_pt(struct mali_uvm *vm, uint64_t pa)
{
	for (struct mali_uvm_pt *pt = vm->tables; pt != NULL; pt = pt->next) {
		if (pt->pa == pa) {
			return pt;
		}
	}
	return NULL;
}

static void
mali_uvm_unlink_pt(struct mali_uvm *vm, struct mali_uvm_pt *target, struct mali_uvm_pt **retired)
{
	struct mali_uvm_pt **pp = &vm->tables;

	while (*pp != NULL) {
		if (*pp == target) {
			*pp = target->next;
			target->next = *retired;
			*retired = target;
			return;
		}
		pp = &(*pp)->next;
	}
}

static struct mali_uvm *
mali_uvm_by_id_locked(uint32_t vm_id)
{
	if (vm_id < 1u || vm_id > MALI_UVM_SLOTS) {
		return NULL;
	}
	struct mali_uvm *vm = &gUserVms[vm_id - 1u];
	return vm->active ? vm : NULL;
}

static int
mali_uvm_map_pas_locked(struct mali_uvm *vm, uint64_t va, const uint64_t *pas,
    uint32_t count_4k, uint32_t map_flags)
{
	uint64_t size = (uint64_t)count_4k * PT_LEAF_GRANULE;

	if (!vm || !vm->active || vm->unusable || !vm->root || count_4k == 0u) {
		return EINVAL;
	}
	if ((va & (PT_LEAF_GRANULE - 1ull)) != 0ull || va >= MALI_UVM_FULL_VA ||
	    size > MALI_UVM_FULL_VA - va) {
		return EINVAL;
	}
	for (uint32_t i = 0; i < count_4k; i++) {
		if (!mali_uvm_pa_valid(pas[i])) {
			printf("mali: uapi: invalid PA %#llx at index %u\n",
			    (unsigned long long)pas[i], i);
			return EINVAL;
		}
	}
	if (!mali_as_lock_region_locked(vm->as_slot, va, size)) {
		vm->unusable = true;
		return EIO;
	}

	uint64_t idx = (map_flags & MALI_MAP_UNCACHED) ? IDX_NC : IDX_CACHE;
	uint64_t sh = (idx == IDX_CACHE) ? PTE_SH_IS : PTE_SH_OS;
	uint64_t ap = (map_flags & MALI_MAP_RDONLY) ? PTE_RDONLY : PTE_UNPRIV;
	uint64_t xn = (map_flags & MALI_MAP_NOEXEC) ? PTE_XN : 0ull;
	uint64_t attrs = PTE_PAGE | PTE_AF | PTE_NG | sh | (idx << 2) | ap | xn;

	int err = 0;
	for (uint32_t i = 0; i < count_4k; i++) {
		uint64_t cur_va = va + (uint64_t)i * PT_LEAF_GRANULE;
		struct mali_uvm_pt *tbl = vm->root;

		for (int shift = 39; shift > 12; shift -= 9) {
			unsigned slot = (unsigned)((cur_va >> shift) & 511u);
			uint64_t *desc = &tbl->entry[slot];

			if ((*desc & 1ull) == 0ull) {
				struct mali_uvm_pt *child = mali_uvm_alloc_pt(vm);
				if (child == NULL) {
					err = ENOMEM;
					break;
				}
				*desc = child->pa | PTE_TABLE | PTE_AF;
				tbl->live_entries++;
				FlushPoC_DcacheRegion((vm_offset_t)desc, sizeof(*desc));
				tbl = child;
			} else {
				tbl = mali_uvm_find_pt(vm, *desc & PTE_ADDRESS);
				if (tbl == NULL) {
					err = EIO;
					break;
				}
			}
		}
		if (err != 0) {
			break;
		}
		unsigned l3_slot = (unsigned)((cur_va >> 12) & 511u);
		uint64_t *leaf = &tbl->entry[l3_slot];
		if ((*leaf & 3ull) == 0ull) {
			tbl->live_entries++;
		}
		*leaf = (pas[i] & PTE_ADDRESS) | attrs;
		FlushPoC_DcacheRegion((vm_offset_t)leaf, sizeof(*leaf));
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	if (!mali_as_unlock_region_locked(vm->as_slot)) {
		vm->unusable = true;
		if (err == 0) {
			err = EIO;
		}
	}
	if (err != 0) {
		vm->unusable = true;
	}
	return err;
}

static int
mali_uvm_unmap_locked(struct mali_uvm *vm, uint64_t va, uint64_t size)
{
	if (!vm || !vm->active || !vm->root || size == 0ull) {
		return EINVAL;
	}
	if ((va & (PT_LEAF_GRANULE - 1ull)) != 0ull ||
	    (size & (PT_LEAF_GRANULE - 1ull)) != 0ull ||
	    va >= MALI_UVM_FULL_VA || size > MALI_UVM_FULL_VA - va) {
		return EINVAL;
	}
	bool locked = !vm->hw_parked && mali_uapi_hw_live_locked() &&
	    mali_as_lock_region_locked(vm->as_slot, va, size);
	struct mali_uvm_pt *retired = NULL;
	uint64_t end = va + size;
	uint64_t cur = va;

	while (cur < end) {
		unsigned i0 = (unsigned)((cur >> 39) & 511u);
		uint64_t e0 = vm->root->entry[i0];
		if ((e0 & 1ull) == 0ull) {
			uint64_t next0 = (cur & ~((1ull << 39) - 1ull)) + (1ull << 39);
			cur = (next0 > cur && next0 < end) ? next0 : end;
			continue;
		}
		struct mali_uvm_pt *t1 = mali_uvm_find_pt(vm, e0 & PTE_ADDRESS);
		if (t1 == NULL) {
			break;
		}
		unsigned i1 = (unsigned)((cur >> 30) & 511u);
		uint64_t e1 = t1->entry[i1];
		if ((e1 & 1ull) == 0ull) {
			uint64_t next1 = (cur & ~((1ull << 30) - 1ull)) + (1ull << 30);
			cur = (next1 > cur && next1 < end) ? next1 : end;
			continue;
		}
		struct mali_uvm_pt *t2 = mali_uvm_find_pt(vm, e1 & PTE_ADDRESS);
		if (t2 == NULL) {
			break;
		}
		unsigned i2 = (unsigned)((cur >> 21) & 511u);
		uint64_t e2 = t2->entry[i2];
		if ((e2 & 1ull) == 0ull) {
			uint64_t next2 = (cur & ~((1ull << 21) - 1ull)) + (1ull << 21);
			cur = (next2 > cur && next2 < end) ? next2 : end;
			continue;
		}
		struct mali_uvm_pt *t3 = mali_uvm_find_pt(vm, e2 & PTE_ADDRESS);
		if (t3 == NULL) {
			break;
		}
		unsigned i3 = (unsigned)((cur >> 12) & 511u);
		if ((t3->entry[i3] & 3ull) != 0ull) {
			t3->entry[i3] = 0ull;
			FlushPoC_DcacheRegion((vm_offset_t)&t3->entry[i3], sizeof(uint64_t));
			if (t3->live_entries > 0u) {
				t3->live_entries--;
			}
			if (t3->live_entries == 0u) {
				t2->entry[i2] = 0ull;
				FlushPoC_DcacheRegion((vm_offset_t)&t2->entry[i2], sizeof(uint64_t));
				mali_uvm_unlink_pt(vm, t3, &retired);
				if (t2->live_entries > 0u) {
					t2->live_entries--;
				}
				if (t2->live_entries == 0u) {
					t1->entry[i1] = 0ull;
					FlushPoC_DcacheRegion((vm_offset_t)&t1->entry[i1], sizeof(uint64_t));
					mali_uvm_unlink_pt(vm, t2, &retired);
					if (t1->live_entries > 0u) {
						t1->live_entries--;
					}
					if (t1->live_entries == 0u) {
						vm->root->entry[i0] = 0ull;
						FlushPoC_DcacheRegion((vm_offset_t)&vm->root->entry[i0], sizeof(uint64_t));
						mali_uvm_unlink_pt(vm, t1, &retired);
						if (vm->root->live_entries > 0u) {
							vm->root->live_entries--;
						}
					}
				}
			}
		}
		cur += PT_LEAF_GRANULE;
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	bool safe_to_free = vm->hw_parked;
	if (locked) {
		safe_to_free = mali_as_unlock_region_locked(vm->as_slot);
		if (!safe_to_free) {
			vm->unusable = true;
		}
	}
	while (retired != NULL) {
		struct mali_uvm_pt *next = retired->next;
		if (safe_to_free) {
			kmem_free(kernel_map, (vm_offset_t)retired, PAGE_SIZE);
		} else {
			/* Retain page tables if AS unlock failed while MMU may still walk them. */
			retired->next = vm->tables;
			vm->tables = retired;
		}
		retired = next;
	}
	return 0;
}

static int
mali_uvm_alloc_kva_locked(struct mali_uvm *vm, uint64_t size, uint64_t align, uint64_t *out_va)
{
	if (!vm || !vm->active || size == 0ull) {
		return EINVAL;
	}
	size = (size + PT_LEAF_GRANULE - 1ull) & ~(PT_LEAF_GRANULE - 1ull);
	if (align < PT_LEAF_GRANULE || (align & (align - 1ull)) != 0ull) {
		align = PT_LEAF_GRANULE;
	}
	for (unsigned i = 0; i < MALI_UVM_KVA_HOLES; i++) {
		if (vm->holes[i].size == 0ull) {
			continue;
		}
		uint64_t aligned = (vm->holes[i].va + align - 1ull) & ~(align - 1ull);
		uint64_t pad = aligned - vm->holes[i].va;
		if (vm->holes[i].size >= pad && vm->holes[i].size - pad >= size) {
			*out_va = aligned;
			if (pad == 0ull) {
				vm->holes[i].va += size;
				vm->holes[i].size -= size;
			} else {
				uint64_t rem = vm->holes[i].size - pad - size;
				vm->holes[i].size = pad;
				if (rem > 0ull) {
					for (unsigned j = 0; j < MALI_UVM_KVA_HOLES; j++) {
						if (vm->holes[j].size == 0ull) {
							vm->holes[j].va = aligned + size;
							vm->holes[j].size = rem;
							break;
						}
					}
				}
			}
			return 0;
		}
	}
	uint64_t aligned = (vm->kva_next + align - 1ull) & ~(align - 1ull);
	if (aligned < vm->kva_base || aligned > vm->kva_end || size > vm->kva_end - aligned) {
		return ENOMEM;
	}
	*out_va = aligned;
	vm->kva_next = aligned + size;
	return 0;
}

static void
mali_uvm_free_kva_locked(struct mali_uvm *vm, uint64_t va, uint64_t size)
{
	if (!vm || size == 0ull) {
		return;
	}
	size = (size + PT_LEAF_GRANULE - 1ull) & ~(PT_LEAF_GRANULE - 1ull);
	if (va + size == vm->kva_next) {
		vm->kva_next = va;
		return;
	}
	for (unsigned i = 0; i < MALI_UVM_KVA_HOLES; i++) {
		if (vm->holes[i].size != 0ull && vm->holes[i].va + vm->holes[i].size == va) {
			vm->holes[i].size += size;
			return;
		}
		if (vm->holes[i].size != 0ull && va + size == vm->holes[i].va) {
			vm->holes[i].va = va;
			vm->holes[i].size += size;
			return;
		}
	}
	for (unsigned i = 0; i < MALI_UVM_KVA_HOLES; i++) {
		if (vm->holes[i].size == 0ull) {
			vm->holes[i].va = va;
			vm->holes[i].size = size;
			return;
		}
	}
}

static int
mali_uvm_map_kmem_locked(struct mali_uvm *vm, const struct mali_mem *mem,
    uint64_t map_bytes, uint64_t align, uint32_t map_flags, uint64_t *out_gpu_va)
{
	uint64_t gpu_va = 0;
	uint32_t count_4k = (uint32_t)(map_bytes >> 12);
	int err;

	if (map_bytes == 0 || (map_bytes & (PT_LEAF_GRANULE - 1ull)) != 0ull ||
	    map_bytes > mem->size) {
		return EINVAL;
	}
	err = mali_uvm_alloc_kva_locked(vm, map_bytes, align, &gpu_va);
	if (err != 0) {
		return err;
	}
	/* Map in 64-leaf (256 KiB) chunks to keep stack footprint tiny on 16 KiB XNU stacks. */
	uint64_t pas[64];
	uint32_t done = 0;
	while (done < count_4k) {
		uint32_t batch = count_4k - done > 64u ? 64u : count_4k - done;
		for (uint32_t i = 0; i < batch; i++) {
			pas[i] = kvtophys(mem->kva + (size_t)(done + i) * (size_t)PT_LEAF_GRANULE);
		}
		err = mali_uvm_map_pas_locked(vm, gpu_va + (uint64_t)done * PT_LEAF_GRANULE,
		    pas, batch, map_flags);
		if (err != 0) {
			if (done > 0u) {
				(void)mali_uvm_unmap_locked(vm, gpu_va,
				    (uint64_t)done * PT_LEAF_GRANULE);
			}
			mali_uvm_free_kva_locked(vm, gpu_va, map_bytes);
			return err;
		}
		done += batch;
	}
	*out_gpu_va = gpu_va;
	return 0;
}

static bool
mali_uvm_park_hw_locked(struct mali_uvm *vm)
{
	if (!vm || !vm->active || vm->hw_parked) {
		return true;
	}
	if (!gGpu || !gpueb_bringup_held || !as_ready(vm->as_slot)) {
		return false;
	}
	(void)mali_gpu_flush_all_caches_locked();
	gpu_wr64(asReg(vm->as_slot, kAsTranstab), 0ull);
	gpu_wr64(asReg(vm->as_slot, kAsMemattr), 0ull);
	gpu_wr64(asReg(vm->as_slot, kAsTranscfg), kAsTranscfgUnmapped);
	__asm__ volatile ("dsb sy" ::: "memory");
	if (!as_command(vm->as_slot, kAsCmdUpdate) ||
	    gpu_rd64(asReg(vm->as_slot, kAsTranstab)) != 0ull ||
	    gpu_rd64(asReg(vm->as_slot, kAsTranscfg)) != kAsTranscfgUnmapped) {
		printf("mali: uapi: AS%u park not confirmed\n", vm->as_slot);
		return false;
	}
	mwr(kMmuIntClear, (1u << vm->as_slot) | (1u << (vm->as_slot + 16u)));
	vm->hw_parked = true;
	return true;
}

static bool
mali_shared_alloc_pages_locked(unsigned count, uint32_t *out_offset)
{
	if (count == 0u || count > MALI_UAPI_SHARED_PAGES || !gShared || !gShared->mem.kva) {
		return false;
	}
	if (MALI_UAPI_SHARED_END > (gShared->va_end - gShared->va_start) ||
	    MALI_UAPI_SHARED_END > gShared->mem.size) {
		return false;
	}
	for (unsigned start = 0; start + count <= MALI_UAPI_SHARED_PAGES; start++) {
		uint64_t mask = ((count == 64u) ? ~0ull : ((1ull << count) - 1ull)) << start;
		if ((gSharedPageMask & mask) == 0ull) {
			gSharedPageMask |= mask;
			uint32_t off = MALI_UAPI_SHARED_BASE + start * 0x1000u;
			memset((void *)(gShared->mem.kva + off), 0, (size_t)count * 0x1000u);
			if (!gShared->mem.uncached) {
				FlushPoC_DcacheRegion(gShared->mem.kva + off,
				    (size_t)count * 0x1000u);
			}
			__asm__ volatile ("dsb sy" ::: "memory");
			*out_offset = off;
			return true;
		}
	}
	return false;
}

static void
mali_shared_free_pages_locked(uint32_t offset, unsigned count)
{
	if (count == 0u || offset < MALI_UAPI_SHARED_BASE || offset >= MALI_UAPI_SHARED_END) {
		return;
	}
	unsigned start = (offset - MALI_UAPI_SHARED_BASE) >> 12;
	if (start + count > MALI_UAPI_SHARED_PAGES) {
		return;
	}
	uint64_t mask = ((count == 64u) ? ~0ull : ((1ull << count) - 1ull)) << start;
	gSharedPageMask &= ~mask;
}

static bool
mali_uapi_group_terminate_locked(struct mali_uapi_group *grp)
{
	if (!grp || !grp->active || grp->terminated) {
		return true;
	}
	if (!gGpu || !gpueb_bringup_held || !grp->csg_in || !grp->csg_out ||
	    !grp->glb_in || !grp->glb_out) {
		return false;
	}
	uint32_t slot_bit = 1u << grp->csg_slot;

	for (unsigned q = 0; q < grp->queue_count; q++) {
		if (grp->queues[q].active && grp->queues[q].cs_in != NULL) {
			grp->queues[q].cs_in->req &= ~kCsStateMask;
		}
	}
	FlushPoC_DcacheRegion((vm_offset_t)grp->csg_out, sizeof(*grp->csg_out));
	FlushPoC_DcacheRegion((vm_offset_t)grp->glb_out, sizeof(*grp->glb_out));
	__asm__ volatile ("dsb sy" ::: "memory");

	grp->csg_in->req &= ~kCsgStateMask;     /* CSG_STATE_TERMINATE = 0 */
	uint32_t qmask = (1u << grp->queue_count) - 1u;
	grp->csg_in->doorbell_req = (grp->csg_in->doorbell_req & ~qmask) |
	    ((grp->csg_out->doorbell_ack ^ qmask) & qmask);
	grp->glb_in->doorbell_req = (grp->glb_in->doorbell_req & ~slot_bit) |
	    ((grp->glb_out->doorbell_ack ^ slot_bit) & slot_bit);
	mem_flush(&gShared->mem);
	__asm__ volatile ("dsb sy" ::: "memory");
	mwr(kCsfDoorbellBase, 1u);

	for (unsigned i = 0; i < 200u; i++) {
		FlushPoC_DcacheRegion((vm_offset_t)grp->csg_out, sizeof(*grp->csg_out));
		FlushPoC_DcacheRegion((vm_offset_t)grp->glb_out, sizeof(*grp->glb_out));
		__asm__ volatile ("dsb sy" ::: "memory");
		if ((grp->csg_out->ack & kCsgStateMask) == 0u &&
		    ((grp->glb_out->doorbell_ack ^ grp->glb_in->doorbell_req) & slot_bit) == 0u) {
			grp->terminated = true;
			return true;
		}
		delay(1000);
	}
	printf("mali: uapi: CSG%u terminate timed out (ack %#x glb_db %#x/%#x)\n",
	    grp->csg_slot, grp->csg_out->ack,
	    grp->glb_in->doorbell_req, grp->glb_out->doorbell_ack);
	return false;
}

static void
mali_uapi_group_release_locked(struct mali_uapi_group *grp)
{
	if (!grp || !grp->active) {
		return;
	}
	bool term_ok = mali_uapi_group_terminate_locked(grp);
	bool mcu_off = gMaliStopped || (gGpu != 0 && (mrd(kMcuStatus) & 0xfu) == 0u);
	bool can_free_dma = term_ok || mcu_off;
	struct mali_uvm *vm = mali_uvm_by_id_locked(grp->vm_id);

	for (unsigned q = 0; q < grp->queue_count; q++) {
		struct mali_uapi_queue *qu = &grp->queues[q];
		if (!qu->active) {
			continue;
		}
		if (qu->doorbell_id > 0u && qu->doorbell_id < 32u) {
			gDoorbellMask &= ~(1u << qu->doorbell_id);
		}
		if (vm != NULL && qu->ring_gpu_va != 0ull) {
			(void)mali_uvm_unmap_locked(vm, qu->ring_gpu_va, qu->ring_size);
			mali_uvm_free_kva_locked(vm, qu->ring_gpu_va, qu->ring_size);
		}
		if (can_free_dma) {
			if (qu->ring_mem.kva != 0) {
				kmem_free(kernel_map, qu->ring_mem.kva, qu->ring_mem.size);
			}
			mali_shared_free_pages_locked(qu->shared_off, 1u);
		}
	}
	if (vm != NULL && grp->sync_gpu_va != 0ull) {
		(void)mali_uvm_unmap_locked(vm, grp->sync_gpu_va, PT_LEAF_GRANULE);
		mali_uvm_free_kva_locked(vm, grp->sync_gpu_va, PT_LEAF_GRANULE);
	}
	if (can_free_dma) {
		if (grp->sync_mem.kva != 0) {
			kmem_free(kernel_map, grp->sync_mem.kva, grp->sync_mem.size);
		}
		if (grp->sus_pages != 0u) {
			mali_shared_free_pages_locked(grp->sus_off, grp->sus_pages);
		}
		if (grp->protm_pages != 0u) {
			mali_shared_free_pages_locked(grp->protm_off, grp->protm_pages);
		}
	} else {
		printf("mali: uapi: retaining CSG%u DMA pages across unconfirmed terminate\n",
		    grp->csg_slot);
	}
	memset(grp, 0, sizeof(*grp));
}

static void
mali_uapi_stop_groups(void)
{
	for (unsigned i = 0; i < MALI_UAPI_MAX_CSGS; i++) {
		if (gUserGroups[i].active) {
			(void)mali_uapi_group_terminate_locked(&gUserGroups[i]);
		}
	}
}

static void
mali_uapi_park_all_vms(void)
{
	for (unsigned i = 0; i < MALI_UVM_SLOTS; i++) {
		if (gUserVms[i].active) {
			(void)mali_uvm_park_hw_locked(&gUserVms[i]);
		}
	}
}

bool
hakios_mali610_query_gpu_info(struct mali610_gpu_info *out)
{
	if (out == NULL || !gMaliApiLockInit) {
		return false;
	}
	lck_mtx_lock(&gMaliApiLock);
	if (!mali_uapi_hw_live_locked()) {
		lck_mtx_unlock(&gMaliApiLock);
		return false;
	}
	memset(out, 0, sizeof(*out));
	out->gpu_id = mrd(kGpuId);
	out->gpu_rev = mrd(kGpuRevId);
	out->csf_id = mrd(kCsfId);
	out->l2_features = mrd(kL2Features);
	out->tiler_features = mrd(kTilerFeatures);
	out->mem_features = mrd(kMemFeatures);
	out->mmu_features = mrd(kMmuFeatures);
	out->thread_features = mrd(kThreadFeatures);
	out->max_threads = mrd(kThreadMax);
	out->thread_max_workgroup_size = mrd(kThreadMaxWorkgroup);
	out->thread_max_barrier_size = mrd(kThreadMaxBarrier);
	out->coherency_features = mrd(kCoherencyFeatures);
	for (unsigned i = 0; i < 4u; i++) {
		out->texture_features[i] = mrd(kTextureFeatures0 + i * 4u);
	}
	out->as_present = mrd(kAsPresent);
	out->selected_coherency = kCoherencyNone;
	out->shader_present = gpu_rd64(kShaderPresent);
	out->l2_present = gpu_rd64(kL2Present);
	out->tiler_present = gpu_rd64(kTilerPresent);
	out->core_features = mrd(kCoreFeatures);
	out->pad = 0u;
	out->gpu_features = 0ull;
	lck_mtx_unlock(&gMaliApiLock);
	return true;
}

static bool
mali_get_csif_locked(struct mali610_csif_info *out)
{
	if (!gShared || !gShared->mem.kva) {
		return false;
	}
	volatile struct glb_control *glb = (volatile struct glb_control *)gShared->mem.kva;
	volatile struct csg_control *csg =
	    (volatile struct csg_control *)shared_at(MALI_SHARED_VA + kCsfGroupControlOffset);
	volatile struct cs_control *cs =
	    (volatile struct cs_control *)shared_at(MALI_SHARED_VA + kCsfGroupControlOffset +
	    kCsfStreamControlOffset);
	if (csg == NULL || cs == NULL) {
		return false;
	}
	uint32_t groups = glb->group_num;
	if (groups > MALI_UAPI_MAX_CSGS) {
		groups = MALI_UAPI_MAX_CSGS;
	}
	uint32_t regs = (cs->features & 0xffu) + 1u;
	uint32_t sbs = (cs->features >> 8) & 0xffu;
	if (sbs == 0u) {
		sbs = 8u;
	}
	memset(out, 0, sizeof(*out));
	out->csg_slot_count = groups;
	out->cs_slot_count = csg->stream_num;
	out->cs_reg_count = regs;
	out->scoreboard_slot_count = sbs;
	out->unpreserved_cs_reg_count = 4u;
	out->pad = 0u;
	return regs >= 8u && (regs & 1u) == 0u && groups >= 1u && csg->stream_num >= 1u;
}

bool
hakios_mali610_query_csif_info(struct mali610_csif_info *out)
{
	if (out == NULL || !gMaliApiLockInit) {
		return false;
	}
	lck_mtx_lock(&gMaliApiLock);
	bool ok = mali_uapi_hw_live_locked() && mali_get_csif_locked(out);
	lck_mtx_unlock(&gMaliApiLock);
	return ok;
}

bool
hakios_mali610_query_timestamp_info(struct mali610_timestamp_info *inout)
{
	if (inout == NULL || !gMaliApiLockInit) {
		return false;
	}
	lck_mtx_lock(&gMaliApiLock);
	if (!mali_uapi_hw_live_locked()) {
		lck_mtx_unlock(&gMaliApiLock);
		return false;
	}
	uint32_t flags = inout->flags;
	if (flags == 0u) {
		flags = (1u << 0) | (1u << 4) | (1u << 6);      /* GPU | OFFSET | FREQ */
	}
	uint64_t cntfrq = 0ull;
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r"(cntfrq));

	uint64_t t0 = mach_absolute_time();
	uint64_t gpu_ts = (flags & (1u << 0)) ? gpu_rd64_counter(kGpuTimestamp) : 0ull;
	uint64_t cpu_abs = mach_continuous_time();
	uint64_t cycles = (flags & (1u << 5)) ? gpu_rd64_counter(kGpuCycleCount) : 0ull;
	uint64_t t1 = mach_absolute_time();
	uint64_t dur_ns = 0ull, cpu_ns = 0ull;
	absolutetime_to_nanoseconds(t1 - t0, &dur_ns);
	absolutetime_to_nanoseconds(cpu_abs, &cpu_ns);

	inout->timestamp_frequency = (flags & (1u << 6)) ? cntfrq : 0ull;
	inout->current_timestamp = gpu_ts;
	inout->timestamp_offset = (flags & (1u << 4)) ? gpu_rd64(kGpuTimestampOffset) : 0ull;
	inout->duration_nsec = (flags & (1u << 7)) ? (uint32_t)dur_ns : 0u;
	inout->cycle_count = cycles;
	if (flags & (7u << 1)) {
		inout->cpu_timestamp_sec = cpu_ns / NSEC_PER_SEC;
		inout->cpu_timestamp_nsec = cpu_ns % NSEC_PER_SEC;
	} else {
		inout->cpu_timestamp_sec = 0ull;
		inout->cpu_timestamp_nsec = 0ull;
	}
	lck_mtx_unlock(&gMaliApiLock);
	return true;
}

void
hakios_mali610_report_fault(const char *why)
{
	if (!gMaliApiLockInit) {
		return;
	}
	lck_mtx_lock(&gMaliApiLock);
	if (!gGpu || !gpueb_bringup_held || gMaliStopped) {
		lck_mtx_unlock(&gMaliApiLock);
		return;
	}
	printf("mali: fault report (%s): gpu_status %#x gpu_fault %#x addr %#llx "
	    "irq raw %#x job %#x mmu %#x\n",
	    why ? why : "unknown", mrd(kGpuStatus), mrd(kGpuFaultStatus),
	    (unsigned long long)gpu_rd64(kGpuFaultAddress),
	    mrd(kIntRaw), mrd(kJobIntRaw), mrd(kMmuIntRaw));
	for (unsigned i = 0; i < MALI_UVM_SLOTS; i++) {
		if (gUserVms[i].active) {
			unsigned as = gUserVms[i].as_slot;
			printf("mali: fault AS%u: status %#x faultstatus %#x faultaddr %#llx\n",
			    as, mrd(asReg(as, kAsStatus)), mrd(asReg(as, kAsFaultStatus)),
			    (unsigned long long)gpu_rd64(asReg(as, kAsFaultAddress)));
		}
	}
	for (unsigned g = 0; g < MALI_UAPI_MAX_CSGS; g++) {
		struct mali_uapi_group *grp = &gUserGroups[g];
		if (!grp->active || !grp->csg_out) {
			continue;
		}
		FlushPoC_DcacheRegion((vm_offset_t)grp->csg_out, sizeof(*grp->csg_out));
		printf("mali: fault CSG%u (vm %u): req %#x ack %#x state %#x ep_cur %#x\n",
		    grp->csg_slot, grp->vm_id, grp->csg_in->req, grp->csg_out->ack,
		    grp->csg_out->status_state, grp->csg_out->status_endpoint_current);
		for (unsigned q = 0; q < grp->queue_count; q++) {
			struct mali_uapi_queue *qu = &grp->queues[q];
			if (!qu->active || !qu->cs_out || !qu->uout) {
				continue;
			}
			FlushPoC_DcacheRegion((vm_offset_t)qu->cs_out, sizeof(*qu->cs_out));
			FlushPoC_DcacheRegion((vm_offset_t)qu->uout, sizeof(*qu->uout));
			volatile uint64_t *seqp =
			    (volatile uint64_t *)(grp->sync_mem.kva + q * 64u);
			FlushPoC_DcacheRegion((vm_offset_t)seqp, 64u);
			printf("mali: fault CSG%u.Q%u: ins %llu ext %llu seq %llu/%llu "
			    "cs_ack %#x fault %#x (%#llx) fatal %#x (%#llx) cmd_ptr %#llx "
			    "wait %#x sb %#x blk %#x\n",
			    grp->csg_slot, q,
			    (unsigned long long)qu->insert_pos,
			    (unsigned long long)qu->uout->extract,
			    (unsigned long long)*seqp,
			    (unsigned long long)qu->submitted_seq,
			    qu->cs_out->ack, qu->cs_out->fault,
			    (unsigned long long)qu->cs_out->fault_info,
			    qu->cs_out->fatal,
			    (unsigned long long)qu->cs_out->fatal_info,
			    (unsigned long long)qu->cs_out->status_cmd_ptr,
			    qu->cs_out->status_wait, qu->cs_out->status_scoreboards,
			    qu->cs_out->status_blocked);
		}
	}
	lck_mtx_unlock(&gMaliApiLock);
}

int
hakios_mali610_vm_create(uint64_t *inout_user_va_range, uint32_t *out_vm_id)
{
	if (!inout_user_va_range || !out_vm_id || !gMaliApiLockInit) {
		return EINVAL;
	}
	uint64_t uva = *inout_user_va_range;
	if (uva == 0ull) {
		uva = MALI_UVM_DEFAULT_USER;
	}
	if ((uva & (PAGE_SIZE - 1ull)) != 0ull || uva > MALI_UVM_DEFAULT_USER ||
	    uva < (1ull << 32)) {
		return EINVAL;
	}

	lck_mtx_lock(&gMaliApiLock);
	if (!mali_uapi_hw_live_locked()) {
		lck_mtx_unlock(&gMaliApiLock);
		return ENXIO;
	}
	uint32_t as_present = mrd(kAsPresent);
	struct mali_uvm *vm = NULL;
	unsigned slot = 0;
	for (unsigned i = 0; i < MALI_UVM_SLOTS; i++) {
		unsigned candidate = i + 1u;
		if (!gUserVms[i].active && (as_present & (1u << candidate)) != 0u) {
			vm = &gUserVms[i];
			slot = candidate;
			break;
		}
	}
	if (vm == NULL) {
		lck_mtx_unlock(&gMaliApiLock);
		return ENOSPC;
	}
	memset(vm, 0, sizeof(*vm));
	vm->as_slot = slot;
	vm->root = mali_uvm_alloc_pt(vm);
	if (vm->root == NULL) {
		memset(vm, 0, sizeof(*vm));
		lck_mtx_unlock(&gMaliApiLock);
		return ENOMEM;
	}
	if (!as_ready(slot)) {
		kmem_free(kernel_map, (vm_offset_t)vm->root, PAGE_SIZE);
		memset(vm, 0, sizeof(*vm));
		lck_mtx_unlock(&gMaliApiLock);
		return EIO;
	}
	mwr(kMmuIntClear, (1u << slot) | (1u << (slot + 16u)));
	gpu_wr64(asReg(slot, kAsTranstab), vm->root->pa);
	gpu_wr64(asReg(slot, kAsMemattr), MALI_MEMATTR);
	gpu_wr64(asReg(slot, kAsTranscfg), 0x42000006ull);
	__asm__ volatile ("dsb sy" ::: "memory");
	if (!as_command(slot, kAsCmdUpdate) ||
	    gpu_rd64(asReg(slot, kAsTranstab)) != vm->root->pa ||
	    gpu_rd64(asReg(slot, kAsMemattr)) != MALI_MEMATTR ||
	    gpu_rd64(asReg(slot, kAsTranscfg)) != 0x42000006ull ||
	    (mrd(asReg(slot, kAsFaultStatus)) & 0xffu) != 0u) {
		(void)mali_uvm_park_hw_locked(vm);
		kmem_free(kernel_map, (vm_offset_t)vm->root, PAGE_SIZE);
		memset(vm, 0, sizeof(*vm));
		lck_mtx_unlock(&gMaliApiLock);
		return EIO;
	}
	vm->active = true;
	vm->unusable = false;
	vm->hw_parked = false;
	vm->user_va_range = uva;
	vm->kva_base = uva;
	vm->kva_next = uva;
	vm->kva_end = MALI_UVM_FULL_VA;
	*inout_user_va_range = uva;
	*out_vm_id = slot;
	lck_mtx_unlock(&gMaliApiLock);
	return 0;
}

int
hakios_mali610_vm_destroy(uint32_t vm_id)
{
	if (!gMaliApiLockInit) {
		return EINVAL;
	}
	lck_mtx_lock(&gMaliApiLock);
	struct mali_uvm *vm = mali_uvm_by_id_locked(vm_id);
	if (vm == NULL) {
		lck_mtx_unlock(&gMaliApiLock);
		return EINVAL;
	}
	/* Ensure any group still referencing this VM is terminated first. */
	for (unsigned g = 0; g < MALI_UAPI_MAX_CSGS; g++) {
		if (gUserGroups[g].active && gUserGroups[g].vm_id == vm_id) {
			mali_uapi_group_release_locked(&gUserGroups[g]);
		}
	}
	bool parked = mali_uvm_park_hw_locked(vm) || gMaliStopped ||
	    (gGpu != 0 && (mrd(kMcuStatus) & 0xfu) == 0u);
	if (parked) {
		while (vm->tables != NULL) {
			struct mali_uvm_pt *pt = vm->tables;
			vm->tables = pt->next;
			kmem_free(kernel_map, (vm_offset_t)pt, PAGE_SIZE);
		}
	} else {
		printf("mali: uapi: retaining AS%u PT pages across unconfirmed park\n",
		    vm->as_slot);
	}
	memset(vm, 0, sizeof(*vm));
	lck_mtx_unlock(&gMaliApiLock);
	return 0;
}

int
hakios_mali610_vm_get_state(uint32_t vm_id, uint32_t *out_state)
{
	if (!out_state || !gMaliApiLockInit) {
		return EINVAL;
	}
	lck_mtx_lock(&gMaliApiLock);
	struct mali_uvm *vm = mali_uvm_by_id_locked(vm_id);
	if (vm == NULL) {
		lck_mtx_unlock(&gMaliApiLock);
		return EINVAL;
	}
	if (!vm->hw_parked && gGpu != 0 && gpueb_bringup_held &&
	    (mrd(asReg(vm->as_slot, kAsFaultStatus)) & 0xffu) != 0u) {
		vm->unusable = true;
	}
	*out_state = vm->unusable ? 1u : 0u;
	lck_mtx_unlock(&gMaliApiLock);
	return 0;
}

int
hakios_mali610_vm_map_pas(uint32_t vm_id, uint64_t va, const uint64_t *pas,
    uint32_t count_4k, uint32_t map_flags)
{
	if (!pas || count_4k == 0u || !gMaliApiLockInit) {
		return EINVAL;
	}
	lck_mtx_lock(&gMaliApiLock);
	if (!mali_uapi_hw_live_locked()) {
		lck_mtx_unlock(&gMaliApiLock);
		return ENXIO;
	}
	struct mali_uvm *vm = mali_uvm_by_id_locked(vm_id);
	int err = mali_uvm_map_pas_locked(vm, va, pas, count_4k, map_flags);
	lck_mtx_unlock(&gMaliApiLock);
	return err;
}

int
hakios_mali610_vm_unmap(uint32_t vm_id, uint64_t va, uint64_t size)
{
	if (!gMaliApiLockInit) {
		return EINVAL;
	}
	lck_mtx_lock(&gMaliApiLock);
	struct mali_uvm *vm = mali_uvm_by_id_locked(vm_id);
	int err = mali_uvm_unmap_locked(vm, va, size);
	lck_mtx_unlock(&gMaliApiLock);
	return err;
}

int
hakios_mali610_vm_alloc_kernel_va(uint32_t vm_id, uint64_t size, uint64_t align,
    uint64_t *out_va)
{
	if (!out_va || !gMaliApiLockInit) {
		return EINVAL;
	}
	lck_mtx_lock(&gMaliApiLock);
	struct mali_uvm *vm = mali_uvm_by_id_locked(vm_id);
	int err = mali_uvm_alloc_kva_locked(vm, size, align, out_va);
	lck_mtx_unlock(&gMaliApiLock);
	return err;
}

void
hakios_mali610_vm_free_kernel_va(uint32_t vm_id, uint64_t va, uint64_t size)
{
	if (!gMaliApiLockInit) {
		return;
	}
	lck_mtx_lock(&gMaliApiLock);
	struct mali_uvm *vm = mali_uvm_by_id_locked(vm_id);
	if (vm != NULL) {
		mali_uvm_free_kva_locked(vm, va, size);
	}
	lck_mtx_unlock(&gMaliApiLock);
}

int
hakios_mali610_group_create(const struct mali610_group_create_args *args,
    uint32_t *out_hw_group)
{
	if (!args || !out_hw_group || !args->queues || args->queue_count == 0u ||
	    args->queue_count > MALI_UAPI_MAX_QUEUES || args->priority > 3u ||
	    !gMaliApiLockInit) {
		return EINVAL;
	}
	for (uint32_t q = 0; q < args->queue_count; q++) {
		uint32_t rsz = args->queues[q].ringbuf_size;
		if (args->queues[q].pad[0] || args->queues[q].pad[1] ||
		    args->queues[q].pad[2] || args->queues[q].priority > 15u ||
		    rsz < 4096u || rsz > 65536u || (rsz & (rsz - 1u)) != 0u) {
			return EINVAL;
		}
	}

	lck_mtx_lock(&gMaliApiLock);
	if (!mali_uapi_hw_live_locked()) {
		lck_mtx_unlock(&gMaliApiLock);
		return ENXIO;
	}
	struct mali_uvm *vm = mali_uvm_by_id_locked(args->vm_id);
	if (vm == NULL || vm->unusable) {
		lck_mtx_unlock(&gMaliApiLock);
		return EINVAL;
	}
	uint64_t shader_pres = gpu_rd64(kShaderPresent);
	uint64_t tiler_pres = gpu_rd64(kTilerPresent);
	if ((args->compute_mask & ~shader_pres) != 0ull ||
	    (args->fragment_mask & ~shader_pres) != 0ull ||
	    (args->tiler_mask & ~tiler_pres) != 0ull ||
	    (uint32_t)__builtin_popcountll(args->compute_mask) < args->max_compute ||
	    (uint32_t)__builtin_popcountll(args->fragment_mask) < args->max_fragment ||
	    (uint32_t)__builtin_popcountll(args->tiler_mask) < args->max_tiler) {
		lck_mtx_unlock(&gMaliApiLock);
		return EINVAL;
	}

	volatile struct glb_control *glb_ctl = (volatile struct glb_control *)gShared->mem.kva;
	volatile struct glb_input *glb_in =
	    (volatile struct glb_input *)shared_at(glb_ctl->input_va);
	volatile struct glb_output *glb_out =
	    (volatile struct glb_output *)shared_at(glb_ctl->output_va);
	if (!glb_in || !glb_out || glb_ctl->group_num == 0u || glb_ctl->group_stride == 0u) {
		lck_mtx_unlock(&gMaliApiLock);
		return EIO;
	}
	uint32_t max_slots = glb_ctl->group_num < MALI_UAPI_MAX_CSGS
	    ? glb_ctl->group_num : MALI_UAPI_MAX_CSGS;
	struct mali_uapi_group *grp = NULL;
	unsigned csg_slot = 0;
	for (unsigned i = 0; i < max_slots; i++) {
		if (!gUserGroups[i].active) {
			grp = &gUserGroups[i];
			csg_slot = i;
			break;
		}
	}
	if (grp == NULL) {
		lck_mtx_unlock(&gMaliApiLock);
		return ENOSPC;
	}

	uint64_t csg_ctl_va = MALI_SHARED_VA + kCsfGroupControlOffset +
	    (uint64_t)csg_slot * glb_ctl->group_stride;
	volatile struct csg_control *csg_ctl =
	    (volatile struct csg_control *)shared_at(csg_ctl_va);
	if (!csg_ctl || args->queue_count > csg_ctl->stream_num) {
		lck_mtx_unlock(&gMaliApiLock);
		return EINVAL;
	}
	volatile struct csg_input *csg_in =
	    (volatile struct csg_input *)shared_at(csg_ctl->input_va);
	volatile struct csg_output *csg_out =
	    (volatile struct csg_output *)shared_at(csg_ctl->output_va);
	if (!csg_in || !csg_out) {
		lck_mtx_unlock(&gMaliApiLock);
		return EIO;
	}

	memset(grp, 0, sizeof(*grp));
	grp->active = true;
	grp->csg_slot = csg_slot;
	grp->vm_id = args->vm_id;
	grp->queue_count = args->queue_count;
	grp->csg_in = csg_in;
	grp->csg_out = csg_out;
	grp->glb_in = glb_in;
	grp->glb_out = glb_out;

	int err = 0;
	if (csg_ctl->suspend_size > 0u) {
		grp->sus_pages = (csg_ctl->suspend_size + 0xfffu) >> 12;
		if (!mali_shared_alloc_pages_locked(grp->sus_pages, &grp->sus_off)) {
			grp->sus_pages = 0u;
			err = ENOMEM;
			goto fail;
		}
	}
	if (csg_ctl->protm_suspend_size > 0u && csg_ctl->protm_suspend_size <= 0x8000u) {
		grp->protm_pages = (csg_ctl->protm_suspend_size + 0xfffu) >> 12;
		if (!mali_shared_alloc_pages_locked(grp->protm_pages, &grp->protm_off)) {
			grp->protm_pages = 0u;
		}
	}
	if (!mem_alloc_high(&grp->sync_mem, PAGE_SIZE)) {
		err = ENOMEM;
		goto fail;
	}
	memset((void *)grp->sync_mem.kva, 0, PAGE_SIZE);
	FlushPoC_DcacheRegion(grp->sync_mem.kva, PAGE_SIZE);
	err = mali_uvm_map_kmem_locked(vm, &grp->sync_mem, PT_LEAF_GRANULE,
	    PT_LEAF_GRANULE, MALI_MAP_NOEXEC | MALI_MAP_UNCACHED, &grp->sync_gpu_va);
	if (err != 0) {
		goto fail;
	}

	for (uint32_t q = 0; q < args->queue_count; q++) {
		struct mali_uapi_queue *qu = &grp->queues[q];
		uint64_t cs_ctl_va = csg_ctl_va + kCsfStreamControlOffset +
		    (uint64_t)q * csg_ctl->stream_stride;
		volatile struct cs_control *cs_ctl =
		    (volatile struct cs_control *)shared_at(cs_ctl_va);
		if (!cs_ctl) {
			err = EIO;
			goto fail;
		}
		qu->cs_in = (volatile struct cs_input *)shared_at(cs_ctl->input_va);
		qu->cs_out = (volatile struct cs_output *)shared_at(cs_ctl->output_va);
		if (!qu->cs_in || !qu->cs_out) {
			err = EIO;
			goto fail;
		}
		uint8_t db = 0u;
		for (uint8_t cand = 1u; cand < 16u; cand++) {
			if ((gDoorbellMask & (1u << cand)) == 0u) {
				gDoorbellMask |= (1u << cand);
				db = cand;
				break;
			}
		}
		if (db == 0u) {
			err = ENOSPC;
			goto fail;
		}
		qu->active = true;
		qu->doorbell_id = db;
		qu->priority = args->queues[q].priority;
		qu->ring_size = args->queues[q].ringbuf_size;
		if (!mali_shared_alloc_pages_locked(1u, &qu->shared_off)) {
			qu->shared_off = 0u;
			err = ENOMEM;
			goto fail;
		}
		qu->user_in_va = MALI_SHARED_VA + qu->shared_off;
		qu->user_out_va = MALI_SHARED_VA + qu->shared_off + 0x800u;
		qu->uin = (volatile struct cs_user_input *)(gShared->mem.kva + qu->shared_off);
		qu->uout = (volatile struct cs_user_output *)(gShared->mem.kva + qu->shared_off + 0x800u);

		size_t ring_alloc = ((size_t)qu->ring_size + PAGE_MASK) & ~(size_t)PAGE_MASK;
		if (!mem_alloc_high(&qu->ring_mem, ring_alloc)) {
			err = ENOMEM;
			goto fail;
		}
		memset((void *)qu->ring_mem.kva, 0, ring_alloc);
		FlushPoC_DcacheRegion(qu->ring_mem.kva, ring_alloc);
		err = mali_uvm_map_kmem_locked(vm, &qu->ring_mem, qu->ring_size,
		    qu->ring_size, MALI_MAP_NOEXEC | MALI_MAP_UNCACHED, &qu->ring_gpu_va);
		if (err != 0) {
			goto fail;
		}

		FlushPoC_DcacheRegion((vm_offset_t)qu->cs_out, sizeof(*qu->cs_out));
		__asm__ volatile ("dsb sy" ::: "memory");
		qu->cs_in->config = kCsConfigPriority(qu->priority) |
		    kCsConfigDoorbell(qu->doorbell_id);
		qu->cs_in->ack_irq_mask = 0xffffffffu;
		qu->cs_in->ringbuf_base = qu->ring_gpu_va;
		qu->cs_in->ringbuf_size = qu->ring_size;
		qu->cs_in->heap_start = 0ull;
		qu->cs_in->heap_end = 0ull;
		qu->cs_in->ringbuf_input = qu->user_in_va;
		qu->cs_in->ringbuf_output = qu->user_out_va;
		qu->cs_in->instr_config = 0u;
		qu->cs_in->instrbuf_size = 0u;
		qu->cs_in->instrbuf_base = 0ull;
		qu->cs_in->instrbuf_offset_ptr = 0ull;
		qu->cs_in->req = (qu->cs_out->ack & kCsEvtMask) |
		    kCsStateStart | kCsIdleSyncWait | kCsIdleEmpty;
	}

	FlushPoC_DcacheRegion((vm_offset_t)csg_out, sizeof(*csg_out));
	FlushPoC_DcacheRegion((vm_offset_t)glb_out, sizeof(*glb_out));
	__asm__ volatile ("dsb sy" ::: "memory");

	csg_in->allow_compute = args->compute_mask;
	csg_in->allow_fragment = args->fragment_mask;
	csg_in->allow_other = (uint32_t)args->tiler_mask;
	uint64_t ep_req = ((uint64_t)args->max_compute & 0xffull) |
	    (((uint64_t)args->max_fragment & 0xffull) << 8) |
	    (((uint64_t)args->max_tiler & 0xfull) << 16) |
	    (((uint64_t)(csg_slot & 0xfu)) << 28);
	if (glb_ctl->version >= 0x04000000u) {
		csg_in->endpoint_req2 = ep_req;
		csg_in->endpoint_req = 0u;
	} else {
		csg_in->endpoint_req = (uint32_t)ep_req;
		csg_in->endpoint_req2 = 0ull;
	}
	csg_in->suspend_buf = grp->sus_pages ? (MALI_SHARED_VA + grp->sus_off) : 0ull;
	csg_in->protm_suspend_buf = grp->protm_pages ? (MALI_SHARED_VA + grp->protm_off) : 0ull;
	csg_in->config = vm->as_slot;
	csg_in->ack_irq_mask = 0xffffffffu;
	csg_in->cs_irq_ack = csg_out->cs_irq_req;

	uint32_t want_ep = (csg_out->ack ^ kCsgEndpointConfig) & kCsgEndpointConfig;
	csg_in->req = (csg_out->ack & ~(kCsgStateMask | kCsgEndpointConfig)) |
	    kCsgStateStart | want_ep;
	uint32_t qmask = (1u << args->queue_count) - 1u;
	uint32_t slot_bit = 1u << csg_slot;
	csg_in->doorbell_req = (csg_in->doorbell_req & ~qmask) |
	    ((csg_out->doorbell_ack ^ qmask) & qmask);
	glb_in->doorbell_req = (glb_in->doorbell_req & ~slot_bit) |
	    ((glb_out->doorbell_ack ^ slot_bit) & slot_bit);
	mem_flush(&gShared->mem);
	__asm__ volatile ("dsb sy" ::: "memory");
	mwr(kCsfDoorbellBase, 1u);

	bool started = false;
	for (unsigned waited = 0; waited < 200u; waited++) {
		FlushPoC_DcacheRegion((vm_offset_t)csg_out, sizeof(*csg_out));
		__asm__ volatile ("dsb sy" ::: "memory");
		if ((csg_out->ack & kCsgStateMask) == kCsgStateStart &&
		    (csg_out->ack & kCsgEndpointConfig) == want_ep) {
			bool all_cs = true;
			for (uint32_t q = 0; q < args->queue_count; q++) {
				FlushPoC_DcacheRegion((vm_offset_t)grp->queues[q].cs_out,
				    sizeof(*grp->queues[q].cs_out));
				__asm__ volatile ("dsb sy" ::: "memory");
				if ((grp->queues[q].cs_out->ack & kCsStateMask) != kCsStateStart) {
					all_cs = false;
					break;
				}
			}
			if (all_cs) {
				started = true;
				break;
			}
		}
		delay(1000);
	}
	if (!started) {
		printf("mali: uapi: CSG%u failed to start (ack %#x want_ep %#x)\n",
		    csg_slot, csg_out->ack, want_ep);
		err = EIO;
		goto fail;
	}
	*out_hw_group = csg_slot + 1u;
	lck_mtx_unlock(&gMaliApiLock);
	return 0;

fail:
	grp->terminated = true;
	mali_uapi_group_release_locked(grp);
	lck_mtx_unlock(&gMaliApiLock);
	return err;
}

int
hakios_mali610_group_destroy(uint32_t hw_group)
{
	if (hw_group < 1u || hw_group > MALI_UAPI_MAX_CSGS || !gMaliApiLockInit) {
		return EINVAL;
	}
	lck_mtx_lock(&gMaliApiLock);
	struct mali_uapi_group *grp = &gUserGroups[hw_group - 1u];
	if (!grp->active) {
		lck_mtx_unlock(&gMaliApiLock);
		return EINVAL;
	}
	mali_uapi_group_release_locked(grp);
	lck_mtx_unlock(&gMaliApiLock);
	return 0;
}

int
hakios_mali610_group_get_state(uint32_t hw_group, uint32_t *out_state,
    uint32_t *out_fatal_queues)
{
	if (!out_state || !out_fatal_queues || hw_group < 1u ||
	    hw_group > MALI_UAPI_MAX_CSGS || !gMaliApiLockInit) {
		return EINVAL;
	}
	lck_mtx_lock(&gMaliApiLock);
	struct mali_uapi_group *grp = &gUserGroups[hw_group - 1u];
	if (!grp->active) {
		lck_mtx_unlock(&gMaliApiLock);
		return EINVAL;
	}
	for (uint32_t q = 0; q < grp->queue_count; q++) {
		struct mali_uapi_queue *qu = &grp->queues[q];
		if (qu->active && qu->cs_out != NULL) {
			FlushPoC_DcacheRegion((vm_offset_t)qu->cs_out, sizeof(*qu->cs_out));
			__asm__ volatile ("dsb sy" ::: "memory");
			if (qu->cs_out->fatal != 0u ||
			    ((qu->cs_out->ack ^ qu->cs_in->req) & kCsFatalBit) != 0u) {
				grp->fatal_queues |= (1u << q);
			}
		}
	}
	uint32_t st = 0u;
	if (grp->timedout) {
		st |= (1u << 0);                /* DRM_PANTHOR_GROUP_STATE_TIMEDOUT */
	}
	if (grp->fatal_queues != 0u) {
		st |= (1u << 1);                /* DRM_PANTHOR_GROUP_STATE_FATAL_FAULT */
	}
	*out_state = st;
	*out_fatal_queues = grp->fatal_queues;
	lck_mtx_unlock(&gMaliApiLock);
	return 0;
}

void
hakios_mali610_group_mark_timedout(uint32_t hw_group)
{
	if (hw_group < 1u || hw_group > MALI_UAPI_MAX_CSGS || !gMaliApiLockInit) {
		return;
	}
	lck_mtx_lock(&gMaliApiLock);
	if (gUserGroups[hw_group - 1u].active) {
		gUserGroups[hw_group - 1u].timedout = true;
	}
	lck_mtx_unlock(&gMaliApiLock);
}

int
hakios_mali610_queue_submit(uint32_t hw_group, uint32_t queue_idx,
    uint64_t stream_addr, uint32_t stream_size, uint32_t latest_flush, uint64_t *out_seq)
{
	if (!out_seq || hw_group < 1u || hw_group > MALI_UAPI_MAX_CSGS ||
	    (latest_flush & 0x7f000000u) != 0u || !gMaliApiLockInit) {
		return EINVAL;
	}
	if (stream_size == 0u) {
		if (stream_addr != 0ull) {
			return EINVAL;
		}
	} else {
		if (stream_addr == 0ull || (stream_size & 7u) != 0u ||
		    (stream_addr & 63ull) != 0ull || stream_addr >= MALI_UVM_FULL_VA) {
			return EINVAL;
		}
	}

	lck_mtx_lock(&gMaliApiLock);
	if (!mali_uapi_hw_live_locked()) {
		lck_mtx_unlock(&gMaliApiLock);
		return ENXIO;
	}
	struct mali_uapi_group *grp = &gUserGroups[hw_group - 1u];
	if (!grp->active || grp->terminated || grp->timedout || grp->fatal_queues != 0u ||
	    queue_idx >= grp->queue_count || !grp->queues[queue_idx].active) {
		lck_mtx_unlock(&gMaliApiLock);
		return EINVAL;
	}
	struct mali_uvm *vm = mali_uvm_by_id_locked(grp->vm_id);
	if (!vm || vm->unusable) {
		lck_mtx_unlock(&gMaliApiLock);
		return EINVAL;
	}
	struct mali_uapi_queue *qu = &grp->queues[queue_idx];
	if (stream_size == 0u) {
		*out_seq = qu->submitted_seq;
		lck_mtx_unlock(&gMaliApiLock);
		return 0;
	}

	struct mali610_csif_info csif;
	if (!mali_get_csif_locked(&csif) || csif.cs_reg_count < 8u ||
	    (csif.cs_reg_count & 1u) != 0u) {
		lck_mtx_unlock(&gMaliApiLock);
		return EIO;
	}
	uint64_t addr_reg = (uint64_t)((csif.cs_reg_count - 4u) & ~1u);
	uint64_t val_reg = addr_reg + 2ull;
	uint64_t waitall_mask = (csif.scoreboard_slot_count >= 16u)
	    ? 0xffffull : ((1ull << csif.scoreboard_slot_count) - 1ull);

	/* Ensure space in the circular CS ring before writing 128 bytes. */
	for (unsigned w = 0; w < 1000u; w++) {
		FlushPoC_DcacheRegion((vm_offset_t)qu->uout, sizeof(*qu->uout));
		__asm__ volatile ("dsb sy" ::: "memory");
		if ((qu->insert_pos + MALI_UAPI_JOB_BYTES) - qu->uout->extract <= qu->ring_size) {
			break;
		}
		if (w == 999u) {
			lck_mtx_unlock(&gMaliApiLock);
			return EBUSY;
		}
		delay(1000);
	}

	uint64_t sync_va = grp->sync_gpu_va + (uint64_t)queue_idx * 64ull;
	uint64_t cmds[MALI_UAPI_JOB_WORDS];
	unsigned n = 0u;

	/* Pre-CALL cache clean+invalidate + CALL into user command stream */
	cmds[n++] = (2ull << 56) | (val_reg << 48) | (uint64_t)latest_flush;
	cmds[n++] = (36ull << 56) | (0ull << 48) | (val_reg << 40) | 0x233ull;
	cmds[n++] = (1ull << 56) | (addr_reg << 48) | (stream_addr & 0xffffffffffffull);
	cmds[n++] = (2ull << 56) | (val_reg << 48) | (uint64_t)stream_size;
	cmds[n++] = (3ull << 56) | (1ull << 16);
	cmds[n++] = (32ull << 56) | (addr_reg << 40) | (val_reg << 32);
	/* Post-CALL scoreboard barrier + unconditional L2+LSC clean+inv before SYNC_ADD64 */
	cmds[n++] = (3ull << 56) | (waitall_mask << 16);
	cmds[n++] = (2ull << 56) | (val_reg << 48) | 0ull;
	cmds[n++] = (36ull << 56) | (0ull << 48) | (val_reg << 40) | 0x233ull;
	cmds[n++] = (3ull << 56) | (1ull << 16);
	cmds[n++] = (1ull << 56) | (addr_reg << 48) | (sync_va & 0xffffffffffffull);
	cmds[n++] = (1ull << 56) | (val_reg << 48) | 1ull;
	cmds[n++] = (51ull << 56) | (0ull << 48) | (addr_reg << 40) | (val_reg << 32) | 1ull;
	cmds[n++] = (47ull << 56);
	while (n < MALI_UAPI_JOB_WORDS) {
		cmds[n++] = 0ull;
	}

	uint32_t ring_off = (uint32_t)(qu->insert_pos & (uint64_t)(qu->ring_size - 1u));
	uint32_t first = qu->ring_size - ring_off;
	if (first >= MALI_UAPI_JOB_BYTES) {
		memcpy((void *)(qu->ring_mem.kva + ring_off), cmds, MALI_UAPI_JOB_BYTES);
		FlushPoC_DcacheRegion(qu->ring_mem.kva + ring_off, MALI_UAPI_JOB_BYTES);
	} else {
		memcpy((void *)(qu->ring_mem.kva + ring_off), cmds, first);
		memcpy((void *)qu->ring_mem.kva, (const uint8_t *)cmds + first,
		    MALI_UAPI_JOB_BYTES - first);
		FlushPoC_DcacheRegion(qu->ring_mem.kva + ring_off, first);
		FlushPoC_DcacheRegion(qu->ring_mem.kva, MALI_UAPI_JOB_BYTES - first);
	}
	__asm__ volatile ("dsb sy" ::: "memory");

	qu->insert_pos += MALI_UAPI_JOB_BYTES;
	qu->submitted_seq++;
	qu->uin->extract_init = qu->uout->extract;
	qu->uin->insert = qu->insert_pos;
	if (!gShared->mem.uncached) {
		FlushPoC_DcacheRegion((vm_offset_t)qu->uin, sizeof(*qu->uin));
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	mwr(kCsfDoorbellBase + (uint32_t)qu->doorbell_id * kCsfDoorbellStride, 1u);

	(void)hakios_mali610_bump_flush_id();
	*out_seq = qu->submitted_seq;
	lck_mtx_unlock(&gMaliApiLock);
	return 0;
}

int
hakios_mali610_queue_poll(uint32_t hw_group, uint32_t queue_idx,
    uint64_t target_seq, bool *out_done)
{
	if (!out_done || hw_group < 1u || hw_group > MALI_UAPI_MAX_CSGS || !gMaliApiLockInit) {
		return EINVAL;
	}
	if (target_seq == 0ull) {
		*out_done = true;
		return 0;
	}
	lck_mtx_lock(&gMaliApiLock);
	struct mali_uapi_group *grp = &gUserGroups[hw_group - 1u];
	if (!grp->active || queue_idx >= grp->queue_count || !grp->queues[queue_idx].active) {
		lck_mtx_unlock(&gMaliApiLock);
		return EINVAL;
	}
	struct mali_uapi_queue *qu = &grp->queues[queue_idx];
	if (qu->completed_seq >= target_seq) {
		*out_done = true;
		lck_mtx_unlock(&gMaliApiLock);
		return 0;
	}
	if (!mali_uapi_hw_live_locked()) {
		lck_mtx_unlock(&gMaliApiLock);
		return ENXIO;
	}

	volatile uint64_t *seqp = (volatile uint64_t *)(grp->sync_mem.kva + queue_idx * 64u);
	FlushPoC_DcacheRegion((vm_offset_t)seqp, 64u);
	FlushPoC_DcacheRegion((vm_offset_t)qu->uout, sizeof(*qu->uout));
	FlushPoC_DcacheRegion((vm_offset_t)qu->cs_out, sizeof(*qu->cs_out));
	FlushPoC_DcacheRegion((vm_offset_t)grp->csg_out, sizeof(*grp->csg_out));
	__asm__ volatile ("dsb sy" ::: "memory");

	uint64_t cur_seq = *seqp;
	if (cur_seq > qu->completed_seq) {
		qu->completed_seq = cur_seq;
	}
	if (cur_seq >= target_seq) {
		*out_done = true;
		lck_mtx_unlock(&gMaliApiLock);
		return 0;
	}

	/*
	 * Check stream IRQ events. CS_TILER_OOM (bit 26) is a toggle bit: once OOM #1 sets
	 * req bit 26 to 1, OR-ing with cs_req would falsely re-trigger on every subsequent
	 * CS IRQ while heap_address remains non-zero. Because this driver never suspends or
	 * resumes CSGs, the MT6895 suspend/resume mirror erratum cannot fire here, so XOR
	 * detects genuine OOM toggles cleanly without leaking chunks.
	 */
	uint32_t qbit = 1u << queue_idx;
	if (((grp->csg_out->cs_irq_req ^ grp->csg_in->cs_irq_ack) & qbit) != 0u &&
	    !qu->oom_handling) {
		uint32_t cs_req = qu->cs_in->req;
		uint32_t cs_ack = qu->cs_out->ack;
		if (((cs_req ^ cs_ack) & kCsTilerOomBit) != 0u &&
		    qu->cs_out->heap_address != 0ull) {
			uint32_t oom_vm = grp->vm_id;
			unsigned oom_csg = grp->csg_slot;
			uint64_t oom_heap_va = qu->cs_out->heap_address;
			uint32_t oom_vt_start = qu->cs_out->heap_vt_start;
			uint32_t oom_vt_end = qu->cs_out->heap_vt_end;
			uint32_t oom_frag_end = qu->cs_out->heap_frag_end;
			uint64_t encoded_va = 0ull;
			uint32_t chunk_cnt = 0u;

			qu->oom_handling = true;
			/*
			 * Strict lock order: gPanthorLock -> gMaliApiLock.
			 * Drop gMaliApiLock before calling hakios_mali610_tiler_oom_grow(),
			 * which takes gPanthorLock and maps the new chunk under gMaliApiLock.
			 */
			lck_mtx_unlock(&gMaliApiLock);
			bool grown = hakios_mali610_tiler_oom_grow(oom_vm, oom_heap_va,
			    &encoded_va, &chunk_cnt);
			lck_mtx_lock(&gMaliApiLock);

			if (!grp->active || queue_idx >= grp->queue_count ||
			    !grp->queues[queue_idx].active) {
				qu->oom_handling = false;
				lck_mtx_unlock(&gMaliApiLock);
				return EINVAL;
			}
			qu->oom_handling = false;
			if (!mali_uapi_hw_live_locked()) {
				lck_mtx_unlock(&gMaliApiLock);
				return ENXIO;
			}

			FlushPoC_DcacheRegion((vm_offset_t)qu->cs_out, sizeof(*qu->cs_out));
			FlushPoC_DcacheRegion((vm_offset_t)grp->csg_out, sizeof(*grp->csg_out));
			if (grp->glb_out != NULL) {
				FlushPoC_DcacheRegion((vm_offset_t)grp->glb_out,
				    sizeof(*grp->glb_out));
			}
			__asm__ volatile ("dsb sy" ::: "memory");

			cs_req = qu->cs_in->req;
			cs_ack = qu->cs_out->ack;
			qu->cs_in->heap_start = grown ? encoded_va : 0ull;
			qu->cs_in->heap_end = grown ? encoded_va : 0ull;
			qu->cs_in->req = (cs_req & ~kCsTilerOomBit) | (cs_ack & kCsTilerOomBit);
			if (grown) {
				printf("mali: uapi: CSG%u.Q%u CS_TILER_OOM grown chunk #%u "
				    "encoded=%#llx (heap %#llx vt %u..%u frag %u)\n",
				    oom_csg, queue_idx, chunk_cnt,
				    (unsigned long long)encoded_va,
				    (unsigned long long)oom_heap_va,
				    oom_vt_start, oom_vt_end, oom_frag_end);
			} else {
				printf("mali: uapi: CSG%u.Q%u CS_TILER_OOM declined "
				    "(heap %#llx vt %u..%u frag %u)\n",
				    oom_csg, queue_idx,
				    (unsigned long long)oom_heap_va,
				    oom_vt_start, oom_vt_end, oom_frag_end);
			}
		} else if (grp->glb_out != NULL) {
			FlushPoC_DcacheRegion((vm_offset_t)grp->glb_out, sizeof(*grp->glb_out));
			__asm__ volatile ("dsb sy" ::: "memory");
		}
		if (((qu->cs_in->req ^ cs_ack) & kCsFaultBit) != 0u) {
			qu->cs_in->req = (qu->cs_in->req & ~kCsFaultBit) | (cs_ack & kCsFaultBit);
		}
		grp->csg_in->cs_irq_ack =
		    (grp->csg_in->cs_irq_ack & ~qbit) | (grp->csg_out->cs_irq_req & qbit);
		grp->csg_in->doorbell_req =
		    (grp->csg_in->doorbell_req & ~qbit) |
		    ((grp->csg_out->doorbell_ack ^ qbit) & qbit);
		if (grp->glb_in != NULL && grp->glb_out != NULL) {
			uint32_t slot_bit = 1u << grp->csg_slot;
			grp->glb_in->doorbell_req =
			    (grp->glb_in->doorbell_req & ~slot_bit) |
			    ((grp->glb_out->doorbell_ack ^ slot_bit) & slot_bit);
		}
		mem_flush(&gShared->mem);
		__asm__ volatile ("dsb sy" ::: "memory");
		mwr(kCsfDoorbellBase, 1u);
	}

	struct mali_uvm *vm = mali_uvm_by_id_locked(grp->vm_id);
	uint32_t as_fs = vm ? mrd(asReg(vm->as_slot, kAsFaultStatus)) : 0u;
	if (qu->cs_out->fatal != 0u ||
	    ((qu->cs_out->ack ^ qu->cs_in->req) & kCsFatalBit) != 0u ||
	    (as_fs & 0xffu) != 0u || (mrd(kIntRaw) & 1u) != 0u) {
		grp->fatal_queues |= qbit;
		if (vm && (as_fs & 0xffu) != 0u) {
			vm->unusable = true;
		}
		lck_mtx_unlock(&gMaliApiLock);
		return EIO;
	}
	*out_done = false;
	lck_mtx_unlock(&gMaliApiLock);
	return 0;
}

/*
 * Trial907: put the GPU back the way the loader expects to find it.
 *
 * The run ends with whatever this driver did still in effect: the MCU asked to start, twelve MFG
 * domains powered, and the bus protections released. The loader then boots Android around a GPU
 * it did not leave in that state, and the last few runs took minutes to come back rather than
 * half a minute -- the difference between this being called and not.
 *
 * Only what this driver turned on is turned off, and each step is best-effort: this runs on the
 * way out with no console to report to, so a step that does not take must not stop the rest.
 */
void
hakios_mali610_quiesce(void)
{
	/*
	 * Trial927: the panic-time fallback, and nothing else.
	 *
	 * Every orderly way out of this kernel -- the return device node, the return timer, the stop
	 * poll, halt_all_cpus -- now calls hakios_mali610_stop() while there is still a machine to
	 * call it from. By the time this function runs, loader_return() has stopped the other
	 * processors and masked every interrupt, so the console is not usable, locks may be held by
	 * a processor that is already off, and delay() has no clock behind it.
	 *
	 * That is why the old body of this function had to go. It printed, it polled, and it ran the
	 * six steps -- and run from here the six steps do not finish: trial 925 panicked inside
	 * printf from a secondary processor, and trial 926 reached the return with the GPU stopped
	 * in a context that could not stop it.
	 *
	 * What is left is a fallback for the case the orderly paths never saw: a kernel that panics
	 * with the GPU running. It checks the flag the orderly path sets, and if that is clear it
	 * does the least that makes the hardware safe -- no printing, no locks, no waiting on a
	 * clock, just a bounded number of register reads.
	 */
	if (__atomic_load_n(&gMaliStopped, __ATOMIC_ACQUIRE)) {
		return;                         /* the orderly path already did it, or is doing it */
	}
	if (gGpu == 0) {
		return;                         /* nothing was ever mapped */
	}
	/*
	 * Trial928: and if the power may already be gone, do not touch the block at all.
	 *
	 * gGpu is only an address that was mapped once; it says nothing about whether a domain is
	 * still powered behind it. Writing MMIO into a block whose clock has stopped does not fail
	 * politely on this interconnect -- it can hang, and it can hang in the middle of a write or
	 * a dsb, which is the one place a panic handler cannot recover from.
	 *
	 * The orderly stop sets gMaliStopped before it releases the power and clears
	 * gpueb_bringup_held only after the release has been acknowledged. So a clear flag beside a
	 * clear reference means the power is gone but stop() never finished -- by far the most
	 * likely moment for a panic to be caught here, and the one case where the safest thing to
	 * do with this hardware is nothing.
	 *
	 * A power reference still held (or one that was never taken) means the block answers, and
	 * the six writes below are what a panic needs.
	 */
	if (!gpueb_bringup_held) {
		return;                         /* no reference: the block is not ours to touch */
	}
	{
		static volatile int claim;

		if (!__atomic_compare_exchange_n(&claim, &(int){ 0 }, 1, false,
		    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			return;                 /* another processor is already here */
		}
	}
	mwr(kMcuControl, kMcuControlDisable);
	mwr(kJobIntMask, 0u);
	mwr(kJobIntClear, 0xffffffffu);
	mwr(kMmuIntMask, 0u);
	mwr(kMmuIntClear, 0xffffffffu);
	mwr(kIntMask, 0u);
	mwr(kIntClear, 0xffffffffu);
	mwr(kL2PwrOff, 1u);
	__asm__ volatile ("dsb sy" ::: "memory");
	gMaliStopped = 1u;
	/* The GPUEB release is the one thing that talks to another block; it is left to the
	 * orderly path, which runs while that block can still be reached. */
}


/* ------------------------------------------------------------------ /dev/hakios-gpueb */

/*
 * Trial912: a knob for the GPUEB handshake, deliberately separate from the bring-up path.
 *
 * The question this answers is narrow and the risk of asking it wrongly is high: GPUEB is a
 * coprocessor this kernel has never spoken to, and a transaction that is sent at the wrong
 * moment or to a block whose clock is off hangs the board rather than failing. So the node
 * exists, opening it does nothing at all, and every step is a separate write from user space
 * that can be repeated or abandoned without another flash cycle.
 *
 * Four commands, one per write:
 *
 *   probe    read the mailbox and the shared page; writes nothing
 *   init     CMD_INIT_SHARED_MEM, once per boot; remembered so a second one is refused
 *   on       CMD_POWER_CONTROL(POWER_ON); paired with a reference this driver took
 *   off      CMD_POWER_CONTROL(POWER_OFF); only for a reference this driver took
 *
 * A command that gets no answer within the vendor's ten-second budget is reported as unknown
 * and is NOT retried: the reply may still arrive, and a second send would make the first
 * reply impossible to attribute.
 */
/*
 * The message the GPUFREQ channel carries: eight words, the first the command.
 *
 * The layout is the vendor's gpufreq_ipi.h:51-78, and only the fields this file writes are
 * named. It is declared here rather than taken from gpueb-ipi.h because that header is a
 * document, not something on this subtree's include path.
 */
union gpufreq_ipi_msg {
	uint32_t w[8];
};

/*
 * Four one-bit-per-channel bitmaps in the control bank, at the offsets the DT gives them.
 *
 * send and recv are read-only status, set and clr are write-one-to-act. Note that "clear"
 * doubles as the answer: writing our channel's bit releases the reply slot, which is why a
 * transaction ends with it and not with a read.
 */
static uint32_t
gpueb_tx_busy(vm_offset_t ctl)
{
	return (reg_rd(ctl, MALI_GPUEB_CTL_TX_STS) & (1u << MALI_GPUEB_CHAN)) != 0u;
}

static uint32_t
gpueb_rx_pending(vm_offset_t ctl)
{
	return (reg_rd(ctl, MALI_GPUEB_CTL_RX_STS) & (1u << MALI_GPUEB_CHAN)) != 0u;
}

static void
gpueb_irq_raise(vm_offset_t ctl)
{
	reg_wr(ctl, MALI_GPUEB_CTL_IRQ_SET, 1u << MALI_GPUEB_CHAN);
}

static void
gpueb_irq_clear(vm_offset_t ctl)
{
	reg_wr(ctl, MALI_GPUEB_CTL_IRQ_CLR, 1u << MALI_GPUEB_CHAN);
}

/* Word at a time, as mtk-gpueb-mailbox.c:136-143 requires; the SRAM is not normal memory. */
static void
gpueb_msg_write(vm_offset_t sram, const union gpufreq_ipi_msg *msg)
{
	uint32_t word;

	for (word = 0; word < MALI_GPUEB_MSG_WORDS; word++) {
		reg_wr(sram, MALI_GPUEB_TX_OFF + word * 4u, msg->w[word]);
	}
}

static void
gpueb_msg_read(vm_offset_t sram, union gpufreq_ipi_msg *msg)
{
	uint32_t word;

	for (word = 0; word < MALI_GPUEB_MSG_WORDS; word++) {
		msg->w[word] = reg_rd(sram, MALI_GPUEB_RX_OFF + word * 4u);
	}
}

#define GPUEB_NAME              "hakios-gpueb"
#define GPUEB_CMD_MAX           64
#define GPUEB_SHARED_DUMP_WORDS 32
#define GPUEB_IPI_TIMEOUT_MS    10000u          /* gpueb_ipi.h:10, the vendor's budget */
#define GPUEB_POLL_US           1000u

static int gpueb_major = -1;
static lck_grp_t *gpueb_lck_grp;
static vm_offset_t gpueb_sram, gpueb_ctl, gpueb_shared, gpueb_spm;
static bool gpueb_inited;                       /* INIT_SHARED_MEM has succeeded this boot */
static bool gpueb_held;                         /* this driver took a POWER_ON reference */

/*
 * Trial917: the last transaction, kept whole, for read() to hand back.
 *
 * Printing was not enough. The console path interleaves output from every processor, and an
 * eight-word line from here landed inside a TCP summary line -- written, then half lost inside
 * somebody else's message. A value a caller has to recover from a shared log is one that can be
 * lost that way; a value it can read() is not.
 *
 * Everything a reader could want is recorded, including the cases that did not produce a reply:
 * whether the send happened at all, what RX_STS was before the clear, and the eight words in
 * order. Without that, a poll that gave up and a transaction that answered look the same from
 * outside -- and the answer to "did the handshake happen" would depend on which one it was.
 *
 * A sequence number lets a caller tell a fresh answer from the previous one; the text is built
 * once under the command lock and read out by offset, so a short read returns the next part
 * rather than the beginning again.
 */
struct gpueb_outcome {
	uint32_t sequence;
	bool     sent;                  /* the message was written and the doorbell rung */
	bool     replied;               /* RX was seen for this send */
	uint32_t rx_status;             /* RX_STS as it was just before the clear */
	int      verdict;               /* 0 ok, negative: what went wrong */
	uint32_t word[MALI_GPUEB_MSG_WORDS];
};

static struct gpueb_outcome gpueb_last;
static char gpueb_result[320];

/*
 * Trial915: one deadline for the whole command, in the clock XNU actually keeps.
 *
 * mach_absolute_time() counts ticks, not nanoseconds; dividing it by a million gave a number
 * that was neither milliseconds nor in the unit this file assumed, so the "ten seconds" was
 * whatever the tick rate happened to make it. clock_interval_to_deadline() converts once, from
 * a named interval, and every later comparison is against the same counter -- which is what a
 * budget has to mean if it is to mean anything.
 */
#define GPUEB_BUDGET_SECONDS    10u

static void
gpueb_set_deadline(uint64_t *deadline)
{
	clock_interval_to_deadline(GPUEB_BUDGET_SECONDS, NSEC_PER_SEC, deadline);
}

static void
gpueb_dump_shared(const char *when)
{
	uint32_t word;

	if (gpueb_shared == 0) {
		printf("gpueb: shared page not mapped\n");
		return;
	}
	printf("gpueb: shared %s:", when);
	for (word = 0; word < GPUEB_SHARED_DUMP_WORDS; word++) {
		if ((word % 8u) == 0u) {
			printf("\n  +%03x:", word * 4u);
		}
		printf(" %08x", reg_rd(gpueb_shared, word * 4u));
	}
	printf("\n");
}

/*
 * Trial915: one transaction, serialised, with exactly one budget.
 *
 * Everything here follows from a single rule: only a reply that arrives for THIS send counts.
 * Three things enforce it. The mailbox is checked for a stale reply before sending, so an old
 * one cannot be mistaken for an answer. A lock keeps two writes from interleaving on the same
 * channel. And every exit that is not a fresh reply returns a negative code, because the caller
 * uses that code to decide whether to record a state change -- the earlier version fell through
 * a spin cap and read a stale slot, which made a handshake that never happened look like one
 * that did.
 *
 * The lock is held across the whole command, so a second write from another thread waits for
 * the first to finish rather than racing it. That is deliberate: there is one mailbox and one
 * channel, and the states it changes (inited, held) only make sense serialised.
 */
static lck_mtx_t *gpueb_lock;

/*
 * Turn an outcome into the text a reader gets, and bump the sequence.
 *
 * Called with the command lock held, so the record and the text always describe the same
 * transaction. The eight words are printed in order because the order is what identifies the
 * message; the signed w[2] is the field the vendor's callers test.
 */
static void
gpueb_publish(const char *what, bool sent, bool replied, uint32_t rx_status, int verdict,
    const union gpufreq_ipi_msg *reply)
{
	uint32_t word;

	gpueb_last.sequence++;
	gpueb_last.sent = sent;
	gpueb_last.replied = replied;
	gpueb_last.rx_status = rx_status;
	gpueb_last.verdict = verdict;
	for (word = 0; word < MALI_GPUEB_MSG_WORDS; word++) {
		gpueb_last.word[word] = (reply != NULL) ? reply->w[word] : 0u;
	}

	snprintf(gpueb_result, sizeof(gpueb_result),
	    "seq %u %s sent %u replied %u rx %#x verdict %d cmd %#x ret %d "
	    "words %08x %08x %08x %08x %08x %08x %08x %08x",
	    gpueb_last.sequence, what, sent ? 1u : 0u, replied ? 1u : 0u,
	    rx_status, verdict,
	    gpueb_last.word[0], (int)gpueb_last.word[2],
	    gpueb_last.word[0], gpueb_last.word[1], gpueb_last.word[2], gpueb_last.word[3],
	    gpueb_last.word[4], gpueb_last.word[5], gpueb_last.word[6], gpueb_last.word[7]);
}

static int
gpueb_xfer(const union gpufreq_ipi_msg *msg, int *ack, const char *what)
{
	union gpufreq_ipi_msg reply;
	uint32_t rx_before = 0;
	uint64_t deadline;
	bool sent = false;
	uint32_t spins = 0;
	int rc = 0;

	if (gpueb_lock == NULL) {
		gpueb_lock = lck_mtx_alloc_init(gpueb_lck_grp, LCK_ATTR_NULL);
	}
	if (gpueb_lock == NULL) {
		printf("gpueb: %s: no lock, so nothing is sent\n", what);
		gpueb_publish(what, false, false, 0u, -4, NULL);
		return -4;
	}
	lck_mtx_lock(gpueb_lock);

	/*
	 * A reply already sitting there belongs to an earlier transaction. Publishing this case
	 * matters: from outside, "refused" and "answered" would otherwise both leave a record that
	 * says nothing about why.
	 */
	if (gpueb_rx_pending(gpueb_ctl)) {
		rx_before = reg_rd(gpueb_ctl, MALI_GPUEB_CTL_RX_STS);
		printf("gpueb: %s: reply pending before the send (rx %#x); not sending, so the "
		    "two cannot be confused\n", what, rx_before);
		gpueb_publish(what, false, false, rx_before, -2, NULL);
		rc = -2;
		goto out;
	}

	/* One budget for the whole command: wait for TX, send, wait for RX. */
	gpueb_set_deadline(&deadline);

	while (gpueb_tx_busy(gpueb_ctl)) {
		if (mach_absolute_time() >= deadline) {
			printf("gpueb: %s: TX never went idle (tx %#x) -- nothing was sent, "
			    "RESULT: not sent\n", what,
			    reg_rd(gpueb_ctl, MALI_GPUEB_CTL_TX_STS));
			gpueb_publish(what, false, false, 0u, -3, NULL);
			rc = -3;
			goto out;
		}
		delay(1);
	}

	gpueb_msg_write(gpueb_sram, msg);
	__asm__ volatile ("dsb sy" ::: "memory");
	gpueb_irq_raise(gpueb_ctl);
	sent = true;

	while (!gpueb_rx_pending(gpueb_ctl)) {
		if (mach_absolute_time() >= deadline) {
			/*
			 * Recorded before anything is cleared: IRQ_CLR does not touch TX_STS, and the
			 * RX bit is the only evidence that this send was ever answered.
			 */
			rx_before = reg_rd(gpueb_ctl, MALI_GPUEB_CTL_RX_STS);
			printf("gpueb: %s: sent, no reply within %u s -- RESULT UNKNOWN "
			    "(tx %#x rx %#x); not retrying, the slot was not read\n",
			    what, GPUEB_BUDGET_SECONDS,
			    reg_rd(gpueb_ctl, MALI_GPUEB_CTL_TX_STS), rx_before);
			gpueb_publish(what, true, false, rx_before, -1, NULL);
			rc = -1;
			goto out;
		}
		delay(1);
		if (++spins > 20000u) {
			rx_before = reg_rd(gpueb_ctl, MALI_GPUEB_CTL_RX_STS);
			printf("gpueb: %s: gave up after %u polls with no reply -- RESULT UNKNOWN "
			    "(tx %#x rx %#x); the slot was not read\n", what, spins,
			    reg_rd(gpueb_ctl, MALI_GPUEB_CTL_TX_STS), rx_before);
			gpueb_publish(what, true, false, rx_before, -1, NULL);
			rc = -1;
			goto out;
		}
	}

	/* Only here is there a reply that belongs to this send. */
	rx_before = reg_rd(gpueb_ctl, MALI_GPUEB_CTL_RX_STS);
	gpueb_msg_read(gpueb_sram, &reply);
	printf("gpueb: %s: reply %08x %08x %08x %08x\n", what,
	    reply.w[0], reply.w[1], reply.w[2], reply.w[3]);
	printf("gpueb: %s: reply (cont) %08x %08x %08x %08x; ret %d\n", what,
	    reply.w[4], reply.w[5], reply.w[6], reply.w[7], (int)reply.w[2]);
	if (ack != NULL) {
		*ack = (int)reply.w[2];
	}
	gpueb_publish(what, true, true, rx_before, 0, &reply);

out:
	/* Release the reply slot either way: an unanswered send must not block the next one. */
	gpueb_irq_clear(gpueb_ctl);
	lck_mtx_unlock(gpueb_lock);
	return rc;
}

/*
 * The domains, sampled next to the shared page.
 *
 * A transaction's effect on the GPU cannot be read from the mailbox alone: GPUEB can answer and
 * still not have powered anything. These four words are the other half of the question, and they
 * have to be taken in the same breath as the shared page or the comparison is between two
 * different moments -- the mistake that produced a wrong conclusion once already.
 *
 * MFG0_PWR_CON and MFG1_PWR_CON carry their own ACK bits (30 and 31); the XPU status word at
 * 0xF3C is the one gpufreq reads, with MFG1..MFG12 in bits 2..13. The 0xF34 word is printed too
 * because it is *not* the same set of domains -- its low bits are audio, ADSP and ISP -- and
 * having both in the log is what stops them being mistaken for each other again.
 */
static void
gpueb_dump_domains(const char *when)
{
	uint32_t mfg0, mfg1, status;

	if (gpueb_spm == 0) {
		gpueb_spm = ml_io_map_with_prot(MALI_SPM_PHYS, MALI_SPM_SIZE, VM_PROT_READ);
		if (gpueb_spm == 0) {
			printf("gpueb: domains %s: the SPM will not map\n", when);
			return;
		}
	}
	mfg0 = reg_rd(gpueb_spm, MALI_SPM_MFG0_PWR_CON);
	mfg1 = reg_rd(gpueb_spm, MALI_SPM_MFG1_PWR_CON);
	status = reg_rd(gpueb_spm, MALI_SPM_PWR_STATUS_GF);
	/*
	 * Which bits mean what, from the vendor's own table.
	 *
	 * mtk-pd-chk-mt6895.c:682-693 maps the MFG domains onto this word one per bit:
	 *   MFG1 = 0x00000004 (bit 2)   MFG2 = 0x00000008 (bit 3)   ...   MFG12 = 0x00002000
	 *
	 * gpufreq_mt6895.h:74-76 defines the three masks the vendor actually tests:
	 *   0x6    MFG0|MFG1   (:3382-3385, after the AP reference path brings MFG1 up)
	 *   0x3ffe MFG0..MFG12
	 *   0x3ffc MFG1..MFG12 (:3580, the power-off check)
	 *
	 * PWR_CON's own bits, mt6895_spm_reg.h:2179-2187:
	 *   bit 0 PWR_RST_B   bit 2 PWR_ON   bit 3 PWR_ON_2ND   bit 4 PWR_CLK_DIS
	 *   bit 30 PWR_ACK    bit 31 PWR_ACK_2ND
	 * The request bits are 2 and 3; bit 0 is the reset release, not a request.
	 *
	 * The manual power-on path does not need all twelve. What it needs is the domain it turned
	 * on, and mtk-scpsys-mt6895.c:462 waits for that domain's own PWR_CON[31:30]. So the two
	 * words are printed side by side: MFG1_PWR_CON says whether MFG1 acknowledged, and F3C
	 * bit 2 says whether the same domain reads as powered.
	 */
	printf("gpueb: domains %s: MFG0 %#x (ack30 %u ack31 %u) "
	    "MFG1 %#x (rst_b %u on %u on2nd %u clk_dis %u ack30 %u ack31 %u) "
	    "F3C %#x (mfg0 %u mfg1 %u mfg0_1 %#x mfg1_12 %#x) F40 %#x (mfg1 %u)\n",
	    when,
	    mfg0, (mfg0 >> 30) & 1u, (mfg0 >> 31) & 1u,
	    mfg1,
	    (mfg1 >> 0) & 1u, (mfg1 >> 2) & 1u, (mfg1 >> 3) & 1u, (mfg1 >> 4) & 1u,
	    (mfg1 >> 30) & 1u, (mfg1 >> 31) & 1u,
	    status, (status >> 1) & 1u, (status >> 2) & 1u,
	    status & 0x6u, status & 0x3ffcu,
	    reg_rd(gpueb_spm, MALI_SPM_PWR_STATUS_GF2),
	    (reg_rd(gpueb_spm, MALI_SPM_PWR_STATUS_GF2) >> 2) & 1u);
}

/* ------------------------------------------------------------------ the clean stop */

/*
 * Trial924: stopping a GPU whose MCU is really running.
 *
 * Every run before this one reached the return path with an MCU that had never executed, so
 * quiesce() could write DISABLE and move on: there was nothing to stop. trial 923g is the first
 * run where the firmware booted -- version word 0x1010000, global interface answered, tiler
 * ready -- and that run hung at the Apple logo on the way out.
 *
 * The order below is panthor's, from panthor_fw_pre_reset(), panthor_fw_stop(),
 * panthor_mmu.c's AS teardown and panthor_gpu.c's L2 power-off:
 *
 *   1. ask the firmware to halt, and wait for MCU_STATUS_HALT
 *   2. disable the MCU, and wait for MCU_STATUS_DISABLED
 *   3. park AS0 so nothing can be translated while memory changes hands
 *   4. power the L2 off, which is what flushes its dirty lines and quiets the AXI port
 *   5. mask and clear the three interrupt blocks
 *   6. only then release the GPUEB power
 *
 * Step 4 is the one that matters most and the one this driver never did. Without it the L2 is
 * still a live cache with a bus master behind it when GPUEB is asked to remove power, and GPUEB
 * waits for an idle that never comes.
 *
 * Each step prints before and after, because the point of a stop that runs while the kernel is
 * alive is to read where it got to if it does not finish.
 */

#define MALI_HALT_TIMEOUT_US    500000u         /* the vendor's own halt budget */
#define MALI_STOP_TIMEOUT_US    100000u
#define MALI_L2_TIMEOUT_US      20000u

/* Poll a register until it satisfies a predicate, or the budget runs out. */
static bool
mali_poll(const char *what, unsigned offset, uint32_t want, uint32_t mask, unsigned budget_us)
{
	unsigned waited = 0;
	uint32_t value = mrd(offset);

	while ((value & mask) != want) {
		if (waited >= budget_us) {
			printf("mali: stop %s: gave up after %u us, reads %#x (want %#x mask %#x)\n",
			    what, waited, value, want, mask);
			return false;
		}
		delay(1);
		waited += 1000u;
		value = mrd(offset);
	}
	printf("mali: stop %s: reads %#x after %u us\n", what, value, waited);
	return true;
}

static bool
mali_stop_hardware(void)
{
	volatile struct glb_input *input = NULL;
	uint32_t status;
	uint32_t mcu_final, tiler_final, l2_final;

	if (gMaliStopped) {
		printf("mali: stop: already stopped\n");
		return true;
	}
	if (gGpu == 0) {
		printf("mali: stop: the block was never mapped; nothing to stop\n");
		/* The reference may still be held, so it is still released. */
		hakios_gpueb_bringup_power_off();
		gMaliStopped = 1u;
		return true;
	}

	/*
	 * Trial928: say we are here before reading anything.
	 *
	 * The line below used to be the first one, and it took its three values from three MMIO
	 * reads before printf could be entered -- so a stop that hung on the very first register
	 * access left no trace at all, and the last thing in the log was whatever came before. A
	 * marker that costs nothing and reads nothing separates "never reached stop()" from
	 * "reached it and hung on the first read", which are different problems.
	 */
	printf("mali: stop: stage 1 reached, gGpu %p, no register read yet\n", (void *)gGpu);
	printf("mali: stop: stage 2 first register read about to happen\n");
	printf("mali: stop: enter (mcu status %u, tiler ready %#x, l2 ready %#x)\n",
	    mrd(kMcuStatus), mrd(kTilerReady), mrd(kL2Ready));
	printf("mali: stop: stage 2 done, the block answers\n");

	if (gMaliGpu) {
		mali_uapi_stop_groups();
	} else if (gMaliM2c && gM2c.programmed && !m2c_terminate(&gM2c)) {
		printf("mali: m2c: RETURN group termination unconfirmed; retaining pages\n");
	}
	printf("mali: stop: stage 3 halt\n");
	/*
	 * Step 1: halt. Level, not a toggle, and the shared page has to be flushed first --
	 * it is cacheable on this side, so a request written only to cache is a request the
	 * firmware never sees.
	 */
	if (gShared != NULL && gShared->mem.kva != 0) {
		volatile struct glb_control *control =
		    (volatile struct glb_control *)gShared->mem.kva;

		input = (volatile struct glb_input *)shared_at(control->input_va);
	}
	if (input != NULL) {
		printf("mali: stop halt: req before %#x\n", input->req);
		input->req |= kGlbReqHalt;
		/*
		 * The flush is not optional. This side maps the shared page cacheable, so a request
		 * written only into cache is a request the firmware never reads -- and then the halt
		 * never comes and this looks like a core that refused to stop.
		 */
		mem_flush(&gShared->mem);
		__asm__ volatile ("dsb sy" ::: "memory");
		printf("mali: stop halt: req after %#x, ringing the doorbell\n", input->req);
		mwr(kDoorbell0, 1u);
		(void)mali_poll("halt", kMcuStatus, 2u, 0xfu, MALI_HALT_TIMEOUT_US);
	} else {
		printf("mali: stop halt: no shared input block; going straight to the hard stop\n");
	}

	printf("mali: stop: stage 4 disable\n");
	/* Step 2: disable, and wait for the core to actually report disabled. */
	mwr(kMcuControl, kMcuControlDisable);
	__asm__ volatile ("dsb sy" ::: "memory");
	(void)mali_poll("disabled", kMcuStatus, 0u, 0xfu, MALI_STOP_TIMEOUT_US);

	status = mrd(kMcuStatus);
	if ((status & 0xfu) != 0u) {
		/*
		 * Only now, with halt and disable both having failed to take, is the reset used.
		 * The vendor never needs it on a normal stop; it is the hammer for a core that
		 * will not stop, and doing it earlier risks restarting a state machine that was
		 * already finishing.
		 */
		printf("mali: stop: the core did not stop (status %u); one bounded soft reset\n",
		    status);
		(void)gpu_soft_reset();
	} else {
		printf("mali: stop: no soft reset needed\n");
	}

	if (gMaliGpu) {
		mali_uapi_park_all_vms();
	} else if (gMaliM2c && gM2c.programmed) {
		/* A failed TERMINATE must never cause reuse of pages still visible to DMA. */
		if ((mrd(kMcuStatus) & 0xfu) == 0 && m2c_park(&gM2c) && !gM2c.group_in) {
			m2c_free(&gM2c);
		} else {
			printf("mali: m2c: RETURN retaining AS1 pages through reset\n");
		}
	}
	printf("mali: stop: stage 5 park as0\n");
	/* Step 3: park AS0 so no translation survives into the handover. */
	gpu_wr64(asReg(0, kAsTranstab), 0ull);
	gpu_wr64(asReg(0, kAsMemattr), 0ull);
	gpu_wr64(asReg(0, kAsTranscfg), kAsTranscfgUnmapped);
	mwr(asReg(0, kAsCommand), 1u);          /* AS_COMMAND_UPDATE */
	__asm__ volatile ("dsb sy" ::: "memory");
	(void)mali_poll("as0 parked", asReg(0, kAsStatus), 0u, 0x1u, MALI_STOP_TIMEOUT_US);

	printf("mali: stop: stage 6 l2 off\n");
	/* Step 4: the L2 off. This is what makes the AXI port quiet. */
	(void)mali_poll("l2 pwrtrans clear", kL2PwrTrans, 0u, 0x1u, MALI_L2_TIMEOUT_US);
	if (mrd(kL2Ready) != 0u) {
		mwr(kL2PwrOff, 1u);
		__asm__ volatile ("dsb sy" ::: "memory");
		(void)mali_poll("l2 pwrtrans after off", kL2PwrTrans, 0u, 0x1u, MALI_L2_TIMEOUT_US);
		printf("mali: stop: l2 ready now %#x\n", mrd(kL2Ready));
	}

	printf("mali: stop: stage 7 irqs\n");
	/* Step 5: mask and clear the three interrupt blocks. */
	mwr(kJobIntMask, 0u);
	mwr(kJobIntClear, 0xffffffffu);
	mwr(kMmuIntMask, 0u);
	mwr(kMmuIntClear, 0xffffffffu);
	mwr(kIntMask, 0u);
	mwr(kIntClear, 0xffffffffu);
	__asm__ volatile ("dsb sy" ::: "memory");
	printf("mali: stop: irqs masked and cleared (raw %#x job %#x mmu %#x)\n",
	    mrd(kIntRaw), mrd(kJobIntRaw), mrd(kMmuIntRaw));

	/*
	 * Trial925: the last look at the block, taken BEFORE the power goes away.
	 *
	 * The previous version printed these three after releasing GPUEB, which is a MMIO read
	 * from a domain that has just been switched off -- the exact mistake this file was
	 * corrected for twice already, in quiesce() and in the ordering of the release itself.
	 * Nothing may touch gGpu once step 6 has run.
	 */
	mcu_final = mrd(kMcuStatus);
	tiler_final = mrd(kTilerReady);
	l2_final = mrd(kL2Ready);

	/*
	 * Trial928: claim the flag before releasing, not after.
	 *
	 * Everything above this line touches the block, and by now it is stopped -- but it is still
	 * powered. The release is what removes the power, and after that every one of those
	 * registers is behind a domain that is off. If a panic lands between the release and the
	 * flag being set, quiesce() reads the flag as clear, decides the GPU still needs stopping,
	 * and writes MMIO into a block with no power behind it. On this interconnect that is a hang
	 * or a bus error, not a no-op.
	 *
	 * So the flag goes up first. From here quiesce() is a return, and any panic in the release
	 * leaves the GPU powered rather than half-written -- the safer of the two failures, because
	 * power still on is something the loader can recover from.
	 */
	gMaliStopped = 1u;
	__asm__ volatile ("dsb sy" ::: "memory");

	/* Step 6: and only now the power. */
	printf("mali: stop: releasing the GPUEB reference\n");
	hakios_gpueb_bringup_power_off();

	printf("mali: stop: done (mcu status %u, tiler ready %#x, l2 ready %#x; read before "
	    "the release)\n", mcu_final, tiler_final, l2_final);
	return true;
}

bool
hakios_mali610_stop(void)
{
	if (gMaliGpu && gMaliApiLockInit) {
		__atomic_store_n(&gMaliStopping, 1u, __ATOMIC_RELEASE);
		lck_mtx_lock(&gMaliApiLock);
		bool ok = mali_stop_hardware();
		lck_mtx_unlock(&gMaliApiLock);
		return ok;
	}
	if (!gMaliM2c) {
		return mali_stop_hardware();
	}
	lck_mtx_lock(&m2c_lock);
	bool ok = mali_stop_hardware();
	lck_mtx_unlock(&m2c_lock);
	return ok;
}

/* ------------------------------------------------------------------ the bring-up caller */

/*
 * Trial922: the same handshake, for the code path that starts the MCU.
 *
 * The device node is a bench tool: a person decides when to send each step. bring-up cannot
 * wait for a person, so it needs the two calls directly, plus a mapping of its own -- the node
 * maps lazily on first write, and on this path nobody writes.
 *
 * The reference is tracked separately from the node's gpueb_held on purpose. They are different
 * owners: a person can hold the node's reference open while bring-up also holds one, and
 * releasing one must not release the other. Each owner releases what it took.
 *
 * Nothing here is called unless the caller asked for it. mali=0, mali_probe=1 and every run
 * before this one behave exactly as they did.
 */

static bool
gpueb_bringup_map(void)
{
	if (gpueb_sram != 0 && gpueb_ctl != 0) {
		return true;
	}
	gpueb_sram = ml_io_map(MALI_GPUEB_MBOX_SRAM_PHYS, MALI_GPUEB_MBOX_SRAM_SIZE);
	gpueb_ctl = ml_io_map(MALI_GPUEB_MBOX_CTL_PHYS, MALI_GPUEB_MBOX_CTL_SIZE);
	gpueb_shared = ml_io_map(MALI_GPUEB_SHARED_PHYS, MALI_GPUFREQ_MEM_SIZE);
	if (gpueb_sram == 0 || gpueb_ctl == 0) {
		printf("mali: gpueb: the mailbox will not map\n");
		return false;
	}
	return true;
}

/*
 * INIT then POWER_ON, in the vendor's order, answering whether the domain came up.
 *
 * The two steps are separate transactions with their own answers, as gpufreq_v2.c sends them.
 * INIT is once per boot and remembered; a second call after a success only re-reads the state.
 *
 * On failure nothing is held: a POWER_ON that did not answer is not a reference this code may
 * later release, because there is nothing to release.
 */
bool
hakios_gpueb_bringup_power_on(void)
{
	union gpufreq_ipi_msg msg;
	int ack = 0;
	int rc;

	if (!gpueb_bringup_map()) {
		return false;
	}
	if (gpueb_lck_grp == NULL) {
		printf("mali: gpueb: no lock group; not sending\n");
		return false;
	}
	if (!gpueb_inited) {
		memset(&msg, 0, sizeof(msg));
		msg.w[0] = MALI_GPUEB_CMD_INIT_SHARED;
		msg.w[2] = (uint32_t)(MALI_GPUEB_SHARED_PHYS & 0xffffffffu);
		msg.w[4] = (uint32_t)((MALI_GPUEB_SHARED_PHYS + MALI_GPUFREQ_STATUS_MEM_SZ) &
		    0xffffffffu);
		msg.w[6] = MALI_GPUFREQ_STATUS_MEM_SZ;
		msg.w[7] = MALI_GPUFREQ_MEM_SIZE - MALI_GPUFREQ_STATUS_MEM_SZ;

		rc = gpueb_xfer(&msg, &ack, "bringup init");
		if (rc != 0 || ack < 0) {
			printf("mali: gpueb: INIT did not succeed (rc %d ack %d); no power request "
			    "is sent, because INIT is what registers the shared page\n", rc, ack);
			return false;
		}
		gpueb_inited = true;
		printf("mali: gpueb: INIT answered %d\n", ack);
	}

	memset(&msg, 0, sizeof(msg));
	msg.w[0] = MALI_GPUEB_CMD_POWER;
	msg.w[2] = MALI_GPUEB_POWER_ON;

	rc = gpueb_xfer(&msg, &ack, "bringup on");
	if (rc != 0) {
		printf("mali: gpueb: POWER_ON got no answer (rc %d) -- RESULT UNKNOWN, "
		    "nothing is held\n", rc);
		return false;
	}
	if (ack < 0) {
		printf("mali: gpueb: POWER_ON refused, return value %d\n", ack);
		return false;
	}

	gpueb_bringup_held = true;
	gpueb_dump_shared("after bringup on");
	gpueb_dump_domains("after bringup on");
	printf("mali: gpueb: POWER_ON answered %d, a reference is held for this run\n", ack);
	if (gMaliOpp >= 0) {
		memset(&msg, 0, sizeof(msg));
		msg.w[0] = MALI_GPUEB_CMD_COMMIT;
		msg.w[1] = MALI_GPUEB_TARGET_STACK;
		msg.w[2] = (uint32_t)gMaliOpp;
		rc = gpueb_xfer(&msg, &ack, "stack opp commit");
		printf("mali: gpueb: CMD_COMMIT(TARGET_STACK, opp=%d) -> rc %d ack %d\n",
		    gMaliOpp, rc, ack);
		if (rc == 0 && ack >= 0) {
			gpueb_dump_shared("after stack commit");
		}
	}
	return true;
}

/* Release only what this path took. */
void
hakios_gpueb_bringup_power_off(void)
{
	union gpufreq_ipi_msg msg;
	int ack = 0;
	int rc;

	if (!gpueb_bringup_held) {
		return;
	}
	memset(&msg, 0, sizeof(msg));
	msg.w[0] = MALI_GPUEB_CMD_POWER;
	msg.w[2] = MALI_GPUEB_POWER_OFF;

	rc = gpueb_xfer(&msg, &ack, "bringup off");
	if (rc != 0 || ack < 0) {
		printf("mali: gpueb: POWER_OFF did not confirm (rc %d ack %d); the reference "
		    "stays marked as held\n", rc, ack);
		return;
	}
	gpueb_bringup_held = false;
	gpueb_dump_shared("after bringup off");
	gpueb_dump_domains("after bringup off");
	printf("mali: gpueb: POWER_OFF answered %d; the reference is released\n", ack);
}

static void
gpueb_do_probe(void)
{
	printf("gpueb: probe: ctl tx %#x rx %#x (nothing written)\n",
	    reg_rd(gpueb_ctl, MALI_GPUEB_CTL_TX_STS),
	    reg_rd(gpueb_ctl, MALI_GPUEB_CTL_RX_STS));
	gpueb_dump_shared("now");
	gpueb_dump_domains("now");
}

static int
gpueb_do_init(void)
{
	union gpufreq_ipi_msg msg;
	int ack = 0;
	int rc;

	if (gpueb_inited) {
		printf("gpueb: init: already done this boot; not sending again\n");
		return 0;
	}
	memset(&msg, 0, sizeof(msg));
	msg.w[0] = MALI_GPUEB_CMD_INIT_SHARED;
	msg.w[2] = (uint32_t)(MALI_GPUEB_SHARED_PHYS & 0xffffffffu);
	msg.w[3] = 0u;
	msg.w[4] = (uint32_t)((MALI_GPUEB_SHARED_PHYS + MALI_GPUFREQ_STATUS_MEM_SZ) &
	    0xffffffffu);
	msg.w[5] = 0u;
	msg.w[6] = MALI_GPUFREQ_STATUS_MEM_SZ;
	msg.w[7] = MALI_GPUFREQ_MEM_SIZE - MALI_GPUFREQ_STATUS_MEM_SZ;

	rc = gpueb_xfer(&msg, &ack, "init");
	if (rc != 0) {
		printf("gpueb: init: nothing recorded, result %d; init is still owed\n", rc);
		return rc;
	}
	if (ack >= 0) {
		gpueb_inited = true;
		printf("gpueb: init: done, return value %d\n", ack);
		/* Basis for the ON comparison: the domains as INIT left them, before any power command. */
		gpueb_dump_domains("after init, before on");
	} else {
		printf("gpueb: init: return value %d (negative -- firmware refused); "
		    "not marked done\n", ack);
		return EIO;
	}
	return 0;
}

static int
gpueb_do_power(bool on)
{
	union gpufreq_ipi_msg msg;
	int ack = 0;
	int rc;

	if (!gpueb_inited) {
		printf("gpueb: %s: init has not succeeded, so this is not sent\n",
		    on ? "on" : "off");
		return EPERM;
	}
	if (on && gpueb_held) {
		printf("gpueb: on: already held by this driver; not sent again\n");
		return 0;
	}
	if (!on && !gpueb_held) {
		printf("gpueb: off: this driver holds no reference; not sent\n");
		return EPERM;
	}
	memset(&msg, 0, sizeof(msg));
	msg.w[0] = MALI_GPUEB_CMD_POWER;
	msg.w[2] = on ? MALI_GPUEB_POWER_ON : MALI_GPUEB_POWER_OFF;

	rc = gpueb_xfer(&msg, &ack, on ? "on" : "off");
	if (rc != 0) {
		/*
		 * Nothing is recorded on a failed transaction. For OFF that means the reference
		 * stays held, which is the safe direction: this driver went on believing it owns
		 * a power vote that may or may not have been released, and the next OFF is allowed
		 * to try again.
		 */
		printf("gpueb: %s: nothing changed, result %d\n", on ? "on" : "off", rc);
		return EIO;
	}
	if (on) {
		if (ack >= 0) {
			gpueb_held = true;
			/* The domains and the shared page together, right after the answer. */
			gpueb_dump_shared("after on");
			gpueb_dump_domains("after on");
			return 0;
		}
		printf("gpueb: on: return value %d (negative -- power was refused, "
		    "reference not taken)\n", ack);
		return EIO;
	}
	if (ack >= 0) {
		gpueb_held = false;
		gpueb_dump_shared("after off");
		gpueb_dump_domains("after off");
		return 0;
	}
	printf("gpueb: off: return value %d (negative -- release refused, "
	    "reference kept)\n", ack);
	return EIO;
}

static int
gpueb_open(__unused dev_t dev, __unused int flags, __unused int devtype,
    __unused struct proc *process)
{
	/* Opening touches nothing: the hardware work is done by a write, one step at a time. */
	return 0;
}

static int
gpueb_close(__unused dev_t dev, __unused int flags, __unused int devtype,
    __unused struct proc *process)
{
	return 0;
}

/*
 * Trial917: read the answer by offset, and never clear it.
 *
 * The first version copied from the start of the buffer on every call and then emptied it, so a
 * reader that asked for less than the whole thing got the beginning twice and a reader that
 * came back got nothing. Neither is wrong in a way that shows up loudly -- both quietly give a
 * wrong answer, which is the kind this file keeps having to correct. The text now belongs to the
 * transaction that produced it and is read like a file: the caller's offset decides what comes
 * next, and re-reading after the fact is allowed because the next transaction overwrites it.
 */
static int
gpueb_read(__unused dev_t dev, struct uio *uio, __unused int flags)
{
	size_t length;
	size_t at;
	size_t take;
	int error;

	if (gpueb_lock != NULL) {
		lck_mtx_lock(gpueb_lock);
	}
	length = strlen(gpueb_result);
	if (gpueb_lock != NULL) {
		lck_mtx_unlock(gpueb_lock);
	}

	at = (size_t)uio_offset(uio);
	if (at >= length) {
		return 0;
	}
	take = MIN(length - at, (size_t)uio_resid(uio));
	error = uiomove(gpueb_result + at, take, uio);
	return error;
}

static int
gpueb_write(__unused dev_t dev, struct uio *uio, __unused int flags)
{
	char command[GPUEB_CMD_MAX];
	uint32_t take = MIN((uint32_t)uio_resid(uio), (uint32_t)sizeof(command) - 1u);
	int error;
	char *end;

	if (take == 0u) {
		return 0;
	}
	error = uiomove(command, take, uio);
	if (error != 0) {
		return error;
	}
	command[take] = 0;
	/* A trailing newline is what a shell echo adds; it is not part of the command. */
	for (end = command; *end != 0; end++) {
		if (*end == '\n' || *end == '\r') {
			*end = 0;
			break;
		}
	}
	if (gpueb_sram == 0) {
		gpueb_sram = ml_io_map(MALI_GPUEB_MBOX_SRAM_PHYS, MALI_GPUEB_MBOX_SRAM_SIZE);
		gpueb_ctl = ml_io_map(MALI_GPUEB_MBOX_CTL_PHYS, MALI_GPUEB_MBOX_CTL_SIZE);
		gpueb_shared = ml_io_map(MALI_GPUEB_SHARED_PHYS, MALI_GPUFREQ_MEM_SIZE);
		if (gpueb_sram == 0 || gpueb_ctl == 0) {
			printf("gpueb: the mailbox will not map; nothing done\n");
			return ENXIO;
		}
		printf("gpueb: mapped sram %p ctl %p shared %p\n",
		    (void *)gpueb_sram, (void *)gpueb_ctl, (void *)gpueb_shared);
	}

	/*
	 * Trial915: the transaction's verdict reaches the caller.
	 *
	 * Returning zero for every write made the only signal a user-space program could see --
	 * the write's result -- say "fine" whether the handshake had answered or not. A caller that
	 * wants to know must look at /dev/hlog either way, but it should not be told the opposite.
	 */
	if (strcmp(command, "probe") == 0) {
		gpueb_do_probe();
		return 0;
	}
	if (strcmp(command, "stop") == 0) {
		/*
		 * Trial924: the clean stop on demand.
		 *
		 * This is the point of the step: run it while the kernel is alive, read /dev/hlog, and
		 * see which of the six stages it reached. If it finishes here and the board still hangs
		 * on RETURN, the cause is not the GPU handover.
		 */
		return hakios_mali610_stop() ? 0 : EIO;
	}
	if (strcmp(command, "init") == 0) {
		return gpueb_do_init();
	}
	if (strcmp(command, "on") == 0) {
		return gpueb_do_power(true);
	}
	if (strcmp(command, "off") == 0) {
		return gpueb_do_power(false);
	}
	if (command[0] != 0) {
		printf("gpueb: unknown command \"%s\"; probe, init, on, off\n", command);
		return EINVAL;
	}
	return 0;
}

static const struct cdevsw gpueb_cdevsw = {
	.d_open = gpueb_open, .d_close = gpueb_close, .d_read = gpueb_read,
	.d_write = gpueb_write, .d_ioctl = eno_ioctl, .d_stop = eno_stop, .d_reset = eno_reset,
	.d_select = eno_select, .d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0
};

void
hakios_gpueb_start(void)
{
	if (gpueb_major >= 0) {
		return;
	}
	gpueb_lck_grp = lck_grp_alloc_init("hakios-gpueb", LCK_GRP_ATTR_NULL);
	gpueb_major = cdevsw_add(-1, &gpueb_cdevsw);
	if (gpueb_major < 0) {
		printf("gpueb: cdevsw_add failed\n");
		return;
	}
	if (devfs_make_node(makedev(gpueb_major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL,
	    0600, GPUEB_NAME) == NULL) {
		printf("gpueb: cannot make /dev/%s (major %d)\n", GPUEB_NAME, gpueb_major);
		return;
	}
	printf("gpueb: /dev/%s major %d; writing probe/init/on/off/stop works it, "
	    "opening it does not\n", GPUEB_NAME, gpueb_major);
}

void hakios_gpueb_start(void);

static void
mali_thread(void *parameter, wait_result_t result)
{
	(void)parameter;
	(void)result;

	delay(2000 * 1000);                     /* let the rest of the machine settle first */
	if (gMaliGpu && gMaliApiLockInit) {
		bool start_service = false;

		lck_mtx_lock(&gMaliApiLock);
		if (!gMaliStopped && !__atomic_load_n(&gMaliStopping, __ATOMIC_ACQUIRE)) {
			mali_bring_up();
			start_service = gMaliDone && !gMaliStopped &&
			    !__atomic_load_n(&gMaliStopping, __ATOMIC_ACQUIRE);
		}
		lck_mtx_unlock(&gMaliApiLock);
		if (start_service) {
			HakiosMali610Start();
		}
	} else if (gMaliM2c) {
		lck_mtx_lock(&m2c_lock);
		if (!gMaliStopped) {
			mali_bring_up();
		}
		lck_mtx_unlock(&m2c_lock);
	} else {
		mali_bring_up();
	}
}

void
hakios_mali610_init(void)
{
	unsigned which = 0;
	unsigned mali_gpu = 0;
	bool have_mali = PE_parse_boot_argn("mali", &which, sizeof(which));
	thread_t thread;

	(void)PE_parse_boot_argn("mali_gpu", &mali_gpu, sizeof(mali_gpu));
	if (mali_gpu != 0 && !have_mali) {
		which = 6u;
		have_mali = true;
	}
	if (!have_mali || which == 0) {
		/*
		 * Trial908: mali_probe works on its own, without mali=N.
		 *
		 * The read-only probe is the one thing this driver can do that does not change the
		 * GPU's state, so it must be reachable without asking for a power-on sequence -- and
		 * mali=0 is exactly the request not to have one. Without this the probe never ran: the
		 * only switch that opens the file also starts the twelve domains.
		 */
		unsigned probe = 0;

		/*
		 * Trial912: the GPUEB knob is registered on every boot, including the stable mali=0
		 * one, because that is the baseline the handshake is meant to be tried against.
		 * Registering a character device and opening it touch no hardware -- the first write
		 * is what maps the mailbox, and the commands after it are what talk to the block.
		 */
		hakios_gpueb_start();

		if (PE_parse_boot_argn("mali_probe", &probe, sizeof(probe)) && probe != 0) {
			(void)kernel_thread_start(mali_thread, NULL, &thread);
			thread_deallocate(thread);
		}
		return;
	}
	hakios_gpueb_start();
	if (mali_gpu != 0) {
		if (gpueb_lck_grp != NULL) {
			lck_mtx_init(&gMaliApiLock, gpueb_lck_grp, LCK_ATTR_NULL);
			gMaliApiLockInit = true;
			gMaliGpu = 1u;
			if (which < 6u) {
				which = 6u;
			}
		} else {
			printf("mali: mali_gpu: no lock group; HakiosGPU disabled\n");
		}
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
		int gpuebpower = 1;

		if (PE_parse_boot_argn("mali_gpuebpower", &gpuebpower, sizeof(gpuebpower))) {
			gMaliGpuebPower = gpuebpower ? 1u : 0u;
		}
	}
	{
		int m2 = 1;

		if (PE_parse_boot_argn("mali_m2", &m2, sizeof(m2))) {
			gMaliM2 = m2 ? 1u : 0u;
		}
		int m2c = 0;
		if (!gMaliGpu && gMaliM2 &&
		    PE_parse_boot_argn("mali_m2c", &m2c, sizeof(m2c)) && (m2c == 1 || m2c == 2)) {
			if (gpueb_lck_grp) {
				lck_mtx_init(&m2c_lock, gpueb_lck_grp, LCK_ATTR_NULL);
				gMaliM2c = (unsigned)m2c;
			} else {
				printf("mali: m2c: no lock group; skipping group submission\n");
				gMaliM2 = 0;
			}
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
		int opp = -1;

		if (PE_parse_boot_argn("mali_opp", &opp, sizeof(opp)) && opp >= 0 && opp <= 39) {
			gMaliOpp = opp;
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
	PE_parse_boot_argn("mali_cacheflush", &gMaliCacheFlush, sizeof(gMaliCacheFlush));
	PE_parse_boot_argn("mali_resetonly", &gMaliResetOnly, sizeof(gMaliResetOnly));
	PE_parse_boot_argn("mali_cohnone", &gMaliCohNone, sizeof(gMaliCohNone));
	PE_parse_boot_argn("mali_hwissues", &gMaliHwIssues, sizeof(gMaliHwIssues));
	PE_parse_boot_argn("mali_stackpwron", &gMaliStackPwrOn, sizeof(gMaliStackPwrOn));
	PE_parse_boot_argn("mali_bpverify", &gMaliBpVerify, sizeof(gMaliBpVerify));
	PE_parse_boot_argn("mali_scclk", &gMaliScClk, sizeof(gMaliScClk));
	PE_parse_boot_argn("mali_scupd", &gMaliScUpd, sizeof(gMaliScUpd));
	PE_parse_boot_argn("mali_sc26", &gMaliSc26, sizeof(gMaliSc26));
	PE_parse_boot_argn("mali_mcuwin", &gMaliMcuWin, sizeof(gMaliMcuWin));
	PE_parse_boot_argn("mali_mcuseq", &gMaliMcuSeq, sizeof(gMaliMcuSeq));
	PE_parse_boot_argn("mali_doorbell", &gMaliDoorbell, sizeof(gMaliDoorbell));
	PE_parse_boot_argn("mali_pdcsettle", &gMaliPdcSettle, sizeof(gMaliPdcSettle));
	PE_parse_boot_argn("mali_statussem", &gMaliStatusSem, sizeof(gMaliStatusSem));
	PE_parse_boot_argn("mali_assorder", &gMaliAssOrder, sizeof(gMaliAssOrder));
	PE_parse_boot_argn("mali_nopdc", &gMaliNoPdc, sizeof(gMaliNoPdc));
	if (gMaliNoPdc && gMaliPdcAuto) {
		printf("mali: mali_nopdc and mali_pdcauto conflict; GPU test not started\n");
		return;
	}
	if (kernel_thread_start(mali_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		printf("mali: no thread to bring the GPU up with\n");
	}
}
