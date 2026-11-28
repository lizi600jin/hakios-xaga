/*
 * hakios_gen4m_kal_ext.h — Closure layer for the remaining 113 KAL symbols
 *                          (38 P2P + 75 Core) + pre_cal EMI/NVRAM + connfem
 *                          required by libgen4m_soc7_core.a (72/72 units, 6ef7e62).
 *
 * Companion to Stage 10/11 hakios_gen4m_kal_xnu.c (which provides the first 27).
 * Together these cover 140/140 external_kal.txt symbols -> 0 KAL link gaps.
 */
#ifndef HAKIOS_GEN4M_KAL_EXT_H
#define HAKIOS_GEN4M_KAL_EXT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runtime hooks bound by hakios.mt6895_connsys.c / host tests */
struct hakios_kal_ext_ops {
    void     *ctx;
    uint32_t (*reg_rd32)(void *ctx, uint32_t reg_ofs);
    void     (*reg_wr32)(void *ctx, uint32_t reg_ofs, uint32_t val);
    void    *(*io_map)(void *ctx, uint64_t phys, size_t size);
    void     (*io_unmap)(void *ctx, void *kva);
    void     (*rx_deliver)(void *ctx, const uint8_t *eth, size_t len);
    uint64_t (*boot_ms)(void *ctx);
    int      (*load_fw)(void *ctx, const char *name, const uint8_t **buf, size_t *len);
    bool     (*get_wifi_mac)(void *ctx, uint8_t out_mac[6]);
};

struct hakios_kal_ext_stats {
    uint32_t mutex_acquires;
    uint32_t mutex_releases;
    uint32_t spin_acquires;
    uint32_t spin_releases;
    uint32_t reg_reads;
    uint32_t reg_writes;
    uint32_t rx_indicated;
    uint32_t tx_completed;
    uint32_t cmds_enqueued;
    uint32_t ioctls;
    uint32_t scan_done_cnt;
    uint32_t media_state;
    bool     emi_mpu_enabled;
    uint64_t emi_mpu_base;
};

/* Shared with kal_precal_emi_xnu.c (ioremap routes through io_map). */
extern struct hakios_kal_ext_ops g_ext_ops;

void hakios_kal_ext_bind(const struct hakios_kal_ext_ops *ops);
const struct hakios_kal_ext_stats *hakios_kal_ext_get_stats(void);

/* Pre-cal / EMI globals (real xaga-live.dts values: 0xBD000000 / 20 MiB).
 * NVRAM sizes are the driver's own: include/CFG_Wifi_File.h:569-570.
 */
extern uint64_t gConEmiPhyBaseFinal;
extern uint64_t gConEmiSizeFinal;
extern uint8_t  g_aucNvram[1024 * 8];
extern uint8_t  g_aucNvram_OnlyPreCal[512];

#ifdef __cplusplus
}
#endif

#endif /* HAKIOS_GEN4M_KAL_EXT_H */
