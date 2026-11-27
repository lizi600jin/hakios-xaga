/*
 * hakios_mt6895_connsys_nvdata.h — Bare-metal / XNU MediaTek MT6895 CONNSYS
 *                                  EMI Guard, Read-Only nvdata MAC/Cal Parser
 *                                  & iOS 16.5 bluetoothd DT Guard
 *
 * Offline-solved drop-in header for lizi600jin/hakios-xaga (works on both
 * Cyber-Mobile/xaga-qemu and real Redmi Note 11T Pro hardware).
 *
 * Grounded in:
 *   1. MT6895-Mainline/initramfs (xaga-mt6895/init.c: /dev/sdc13 nvdata mount)
 *   2. Nura GitLab minorum/linux MR !1 (CONNSYS EMI MUST stay at factory PA or
 *      EMI-MPU silently blocks conn-MCU instruction fetch)
 *   3. Cyber-Mobile/xaga-qemu@9cd0e6a (14 CONSYS WF/BGF & DebugTop MMIO windows)
 *   4. akshaynexus/OneiOS docs/a56.md (iOS 16.5 bluetoothd crashes if XNU DT
 *      lacks /product product-id)
 *   5. Elvandito/mtk-bwmod (connsys chipid / adie chipid mismatch guard)
 *
 * Usage:
 *   #define HAKIOS_MT6895_CONNSYS_NVDATA_IMPLEMENTATION
 *   #include "hakios_mt6895_connsys_nvdata.h"
 */

#ifndef HAKIOS_MT6895_CONNSYS_NVDATA_H
#define HAKIOS_MT6895_CONNSYS_NVDATA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 1. CONNSYS EMI Physical Address Guard (minorum/linux MR !1 invariant)
 * ============================================================================ */

struct mtk_connsys_emi_region {
    uint64_t expected_factory_pa; /* Exact PA from Android /reserved-memory */
    uint64_t mapped_pa;           /* Actual PA allocated/mapped by mtkloader/XNU */
    uint64_t size;                /* Region byte size (must be >= 2 MiB) */
};

/* Returns 0 iff mapped_pa strictly equals expected_factory_pa and is 4K-aligned.
 * Prevents silent EMI-MPU lockups on conn-MCU boot.
 */
static inline int mtk_connsys_verify_emi_lock(const struct mtk_connsys_emi_region *r)
{
    if (!r || r->expected_factory_pa == 0ULL || r->size < (2ULL * 1024ULL * 1024ULL))
        return -22; /* -EINVAL */
    if ((r->mapped_pa & 0xFFFULL) != 0ULL)
        return -22;
    if (r->mapped_pa != r->expected_factory_pa)
        return -13; /* -EACCES: EMI-MPU will reject mismatched physical address */
    return 0;
}

/* ============================================================================
 * 2. Read-Only nvdata (/mnt/vendor/nvdata/APCFG/APRDEB/) Parsers
 *    (Strictly read-only in RAM — never writes back to UFS /dev/sdc13!)
 * ============================================================================ */

/* Standard MediaTek APRDEB/BT_Addr layout: first 6 bytes are BD_ADDR */
#define MTK_NVDATA_BT_ADDR_MIN_LEN    6U
/* Standard MediaTek APRDEB/WIFI layout: MAC address sits at offset 0x04..0x09 */
#define MTK_NVDATA_WIFI_MAC_OFS       4U
#define MTK_NVDATA_WIFI_MIN_LEN       10U

struct mtk_nvdata_wireless_ids {
    uint8_t wifi_mac[6];
    uint8_t bt_bdaddr[6];
    bool    wifi_mac_valid;
    bool    bt_bdaddr_valid;
};

/* Reject all-zero or all-FF or multicast (bit0 of byte0 == 1) MAC addresses */
static inline bool mtk_mac_is_valid_unicast(const uint8_t mac[6])
{
    if (!mac) return false;
    bool all_zero = true, all_ff = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0x00U) all_zero = false;
        if (mac[i] != 0xFFU) all_ff = false;
    }
    if (all_zero || all_ff) return false;
    return (mac[0] & 0x01U) == 0U; /* unicast bit */
}

/* Parse read-only buffers copied from /mnt/vendor/nvdata/APCFG/APRDEB/{WIFI,BT_Addr} */
int mtk_nvdata_parse_wireless_ids(const uint8_t *wifi_nv, size_t wifi_len,
                                  const uint8_t *bt_nv, size_t bt_len,
                                  struct mtk_nvdata_wireless_ids *out);

/* ============================================================================
 * 3. CONNSYS MMIO Window Probe & iOS 16.5 bluetoothd DT Guard
 * ============================================================================ */

struct mtk_mmio_window {
    const char *name;
    uint64_t    phys_base;
    uint32_t    size;
    uint32_t    probe_ofs;
};

struct mtk_connsys_probe_ops {
    void     *ctx;
    uint32_t (*read32)(void *ctx, uint64_t pa);
    void     (*log)(void *ctx, const char *fmt, ...);
};

/* Probe a table of CONSYS/BTIF/DPMAIF MMIO windows (generated from xaga-qemu
 * or Android DT). Logs each window's readback so External Aborts vs valid
 * mappings are immediately visible on UART0 (-serial file:OUT.log).
 */
int mtk_connsys_probe_windows(const struct mtk_connsys_probe_ops *ops,
                              const struct mtk_mmio_window *wins,
                              size_t count);

/* iOS 16.5 bluetoothd crash guard (akshaynexus/OneiOS docs/a56.md):
 * Returns true if XNU DeviceTree `/product` has a non-empty `product-id`
 * property; if false, caller MUST synthesize `product-id` before launchd
 * spawns bluetoothd.
 */
static inline bool hakios_dt_has_bluetoothd_product_id(const uint8_t *prop_val, size_t prop_len)
{
    if (!prop_val || prop_len < 4U)
        return false;
    for (size_t i = 0; i < prop_len; i++) {
        if (prop_val[i] != 0U)
            return true;
    }
    return false;
}

/* ============================================================================
 * 4. Implementation
 * ============================================================================ */
#ifdef HAKIOS_MT6895_CONNSYS_NVDATA_IMPLEMENTATION

int mtk_nvdata_parse_wireless_ids(const uint8_t *wifi_nv, size_t wifi_len,
                                  const uint8_t *bt_nv, size_t bt_len,
                                  struct mtk_nvdata_wireless_ids *out)
{
    if (!out)
        return -22;
    memset(out, 0, sizeof(*out));

    if (wifi_nv && wifi_len >= MTK_NVDATA_WIFI_MIN_LEN) {
        const uint8_t *mac = wifi_nv + MTK_NVDATA_WIFI_MAC_OFS;
        if (mtk_mac_is_valid_unicast(mac)) {
            memcpy(out->wifi_mac, mac, 6U);
            out->wifi_mac_valid = true;
        }
    }

    if (bt_nv && bt_len >= MTK_NVDATA_BT_ADDR_MIN_LEN) {
        if (mtk_mac_is_valid_unicast(bt_nv)) {
            memcpy(out->bt_bdaddr, bt_nv, 6U);
            out->bt_bdaddr_valid = true;
        }
    }

    return (out->wifi_mac_valid || out->bt_bdaddr_valid) ? 0 : -2;
}

int mtk_connsys_probe_windows(const struct mtk_connsys_probe_ops *ops,
                              const struct mtk_mmio_window *wins,
                              size_t count)
{
    if (!ops || !ops->read32 || !wins || count == 0U)
        return -22;

    int nonzero = 0;
    for (size_t i = 0; i < count; i++) {
        uint32_t v = ops->read32(ops->ctx, wins[i].phys_base + wins[i].probe_ofs);
        if (v != 0U && v != 0xFFFFFFFFU)
            nonzero++;
        if (ops->log) {
            ops->log(ops->ctx, "connsys: probe %-18s @ 0x%08llx+0x%x -> 0x%08x\n",
                     wins[i].name ? wins[i].name : "win",
                     (unsigned long long)wins[i].phys_base,
                     wins[i].probe_ofs, v);
        }
    }
    return nonzero;
}

#endif /* HAKIOS_MT6895_CONNSYS_NVDATA_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* HAKIOS_MT6895_CONNSYS_NVDATA_H */
