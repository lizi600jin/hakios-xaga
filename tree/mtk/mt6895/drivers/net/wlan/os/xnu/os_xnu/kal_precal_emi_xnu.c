/*
 * kal_precal_emi_xnu.c — xaga-live.dts EMI globals (0xBD000000 / 20 MiB),
 *                        NVRAM shadow, ioremap/mem_region & connfem getters.
 */
#include "hakios_gen4m_kal_ext.h"

extern struct hakios_kal_ext_ops g_ext_ops;
#include <string.h>

/* Verified from xaga-live.dts mblock-46-shared-dma-pool_wifi-reserve-memory_dma */
uint64_t gConEmiPhyBaseFinal = 0xBD000000ULL;
uint64_t gConEmiSizeFinal    = 0x01400000ULL;
/* include/CFG_Wifi_File.h:569-570 -- the sizes the driver itself compares
 * against (os/linux/gl_init.c:1294 does
 * "if (sizeof(struct WIFI_CFG_PARAM_STRUCT) > sizeof(g_aucNvram)").
 * The Stage 12 pack had both at 2048, which is a quarter of the real
 * record size and makes that self-check fail for a correct image.
 */
uint8_t  g_aucNvram[1024 * 8];
uint8_t  g_aucNvram_OnlyPreCal[512];

static uint8_t g_precal_status = 0U;

uint8_t get_pre_cal_status(void) { return g_precal_status; }
void    update_pre_cal_status(uint8_t st) { g_precal_status = st; }

/* chips/common/pre_cal.c:66 ioremaps the EMI region and then walks it byte by
 * byte to read the calibration result. Returning the physical address itself
 * (as the Stage 12 pack did) hands the driver a pointer into physical memory:
 * on XNU that is a page fault on the first read, not a "mapping". So the
 * mapping goes through the host ops; with no io_map bound it fails honestly
 * and pre_cal takes its "ioremap failed" path instead of reading garbage.
 */
void *ioremap(uint64_t phys_addr, size_t size)
{
    if (!g_ext_ops.io_map)
        return NULL;
    return g_ext_ops.io_map(g_ext_ops.ctx, phys_addr, size);
}

void iounmap(volatile void *addr)
{
    if (addr && g_ext_ops.io_unmap)
        g_ext_ops.io_unmap(g_ext_ops.ctx, (void *)addr);
}

/* A physical window the boot loader already reserved: the region is ours by
 * construction (the EMI base is checked against the factory one in
 * hakios.mt6895_connsys.c before the radio is allowed to start). Still, the
 * pointer only says "known window"; it is not an ioremap.
 */
void *request_mem_region(uint64_t start, size_t n, const char *name)
{
    (void)n; (void)name;
    return (void *)(uintptr_t)start;
}

void release_mem_region(uint64_t start, size_t n) { (void)start; (void)n; }

/* connfem Qorvo FEM getters (xaga-live.dts connfem@18000000) */
/* connfem/include/connfem_api.h: the three out-params are
 * struct connfem_epaelna_fem_info (two 64-byte part_name arrays),
 * struct connfem_epaelna_pin_info (32 pins) and
 * struct connfem_epaelna_laa_pin_info (8 pins), i.e. several hundred bytes.
 * The Stage 12 pack memset 16 bytes and returned success -- the caller then
 * reads pin[] past the cleared prefix. Clearing the whole plausible size and
 * keeping success means "a real table exists, and it declares no FEM", which
 * is what an XNU bring-up without a connfem daemon actually has.
 */
#define CONNFEM_INFO_CLEAR 512

int connfem_epaelna_get_fem_info(void *out) {
    if (out) memset(out, 0, CONNFEM_INFO_CLEAR);
    return 0;
}
int connfem_epaelna_get_pin_info(void *out) {
    if (out) memset(out, 0, CONNFEM_INFO_CLEAR);
    return 0;
}
int connfem_epaelna_laa_get_pin_info(void *out) {
    if (out) memset(out, 0, CONNFEM_INFO_CLEAR);
    return 0;
}
