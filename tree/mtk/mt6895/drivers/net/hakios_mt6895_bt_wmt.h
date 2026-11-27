/*
 * hakios_mt6895_bt_wmt.h — Bare-metal / XNU MediaTek Bluetooth WMT-over-HCI
 *                          ROM Patch Parser & Transport-Agnostic Bring-up
 *
 * Offline-solved drop-in header for lizi600jin/hakios-xaga
 *
 * Verified against:
 *   1. Linux Mainline drivers/bluetooth/btmtk.c (commit 3b70dd8)
 *      - Patch header (32 B), Global Descriptor (64 B), Section Map (64 B)
 *      - Overflow-safe layout & section validation
 *      - WMT HCI opcode 0xFC6F, BD_ADDR 0xFC1A, TCI sleep 0xFC7A, Coredump 0xFD5B
 *      - 250-byte chunked patch download (PKT_START / PKT_CONTINUE / PKT_END)
 *   2. MT6895-Mainline/linux (7.3-mt6895-xiaomi-xaga)
 *      - MT6895 Bluetooth over BTIF + nvdata calibration
 *   3. akshaynexus/OneiOS (docs/a56.md)
 *      - iOS 16.5 bluetoothd requires /product product-id in XNU DeviceTree
 *
 * Usage:
 *   #define HAKIOS_MT6895_BT_WMT_IMPLEMENTATION
 *   #include "hakios_mt6895_bt_wmt.h"
 *
 * Bind your BTIF / HakiOS mtk-branch HCI transport in struct mtk_bt_hci_ops.
 */

#ifndef HAKIOS_MT6895_BT_WMT_H
#define HAKIOS_MT6895_BT_WMT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 1. Verified MediaTek BT Firmware & WMT Constants (btmtk.c)
 * ============================================================================ */

#define MTK_BT_FW_PATCH_HDR_SIZE      32U
#define MTK_BT_FW_GD_SIZE             64U
#define MTK_BT_FW_SEC_MAP_SIZE        64U
#define MTK_BT_SEC_MAP_COMMON_SIZE    12U
#define MTK_BT_SEC_MAP_SEND_SIZE      52U
#define MTK_BT_WMT_MAX_CHUNK          250U
#define MTK_BT_WMT_MAX_HCI_PAYLOAD    255U

/* HCI Vendor-Specific Opcodes */
#define MTK_HCI_OP_WMT                0xFC6FU
#define MTK_HCI_OP_SET_BDADDR         0xFC1AU
#define MTK_HCI_OP_TCI_SLEEP          0xFC7AU
#define MTK_HCI_OP_FILTER_VND         0xFC5DU
#define MTK_HCI_OP_COREDUMP           0xFD5BU

/* WMT Opcodes */
enum mtk_wmt_op {
    MTK_WMT_PATCH_DWNLD = 0x01,
    MTK_WMT_TEST        = 0x02,
    MTK_WMT_WAKEUP      = 0x03,
    MTK_WMT_HIF         = 0x04,
    MTK_WMT_FUNC_CTRL   = 0x06,
    MTK_WMT_RST         = 0x07,
    MTK_WMT_SEMAPHORE   = 0x17,
};

/* WMT Packet Sequence Flags */
enum mtk_wmt_pkt_flag {
    MTK_WMT_PKT_HEADER   = 0x00,
    MTK_WMT_PKT_START    = 0x01,
    MTK_WMT_PKT_CONTINUE = 0x02,
    MTK_WMT_PKT_END      = 0x03,
};

/* WMT Patch Status Values */
enum mtk_wmt_status {
    MTK_WMT_PATCH_UNDONE   = 0,
    MTK_WMT_PATCH_PROGRESS = 1,
    MTK_WMT_PATCH_DONE     = 2,
    MTK_WMT_ON_DONE        = 3,
    MTK_WMT_ON_UNDONE      = 4,
    MTK_WMT_ON_PROGRESS    = 5,
    MTK_WMT_INVALID        = 0xFF,
};

/* ============================================================================
 * 2. Packed Firmware & WMT Wire Structures
 * ============================================================================ */

#if defined(__GNUC__) || defined(__clang__)
#define MTK_PACKED __attribute__((packed))
#else
#define MTK_PACKED
#endif

struct MTK_PACKED mtk_bt_patch_header {
    uint8_t  datetime[16];
    uint8_t  platform[4];
    uint16_t hwver;
    uint16_t swver;
    uint32_t magicnum;
};

struct MTK_PACKED mtk_bt_global_desc {
    uint32_t patch_ver;
    uint32_t sub_sys;
    uint32_t feature_opt;
    uint32_t section_num;
};

struct MTK_PACKED mtk_bt_section_map {
    uint32_t sectype;
    uint32_t secoffset;
    uint32_t secsize;
    union {
        uint32_t u4_sec_spec[13];
        struct MTK_PACKED {
            uint32_t dl_addr;
            uint32_t dl_size;
            uint32_t sec_key_idx;
            uint32_t align_len;
            uint32_t sec_type;
            uint32_t dl_mode_crc_type;
            uint32_t crc;
            uint32_t reserved[6];
        } bin_info_spec;
    };
};

struct MTK_PACKED mtk_wmt_hdr {
    uint8_t  dir;   /* 1 = Host -> Controller, 2 = Controller -> Host */
    uint8_t  op;    /* enum mtk_wmt_op */
    uint16_t dlen;  /* LE16: payload_len + 1 (includes flag byte) */
    uint8_t  flag;  /* sequence / sub-command flag */
};

struct MTK_PACKED mtk_bt_tci_sleep {
    uint8_t  mode;             /* 0x05 */
    uint16_t duration;         /* LE16: 0x0640 */
    uint16_t host_duration;    /* LE16: 0x0640 */
    uint8_t  host_wakeup_pin;  /* 0 */
    uint8_t  time_compensation;/* 0 */
};

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(struct mtk_bt_patch_header) == 28, "patch_header base is 28B (padded to 32B in file)");
_Static_assert(sizeof(struct mtk_bt_global_desc) == 16, "global_desc must be 16B");
_Static_assert(sizeof(struct mtk_bt_section_map) == 64, "section_map must be 64B");
_Static_assert(sizeof(struct mtk_wmt_hdr) == 5, "wmt_hdr must be 5B");
_Static_assert(sizeof(struct mtk_bt_tci_sleep) == 7, "tci_sleep must be 7B");
#endif

/* ============================================================================
 * 3. Transport Callbacks & Public API
 * ============================================================================ */

struct mtk_bt_hci_ops {
    void *ctx;
    /* Send an HCI vendor command (opcode + payload) and receive the raw WMT
     * vendor event payload (starting at struct mtk_wmt_hdr) into rx_buf.
     * Returns received byte count >= 0, or negative errno on failure.
     */
    int (*hci_cmd_sync)(void *ctx, uint16_t opcode,
                        const uint8_t *tx_data, uint16_t tx_len,
                        uint8_t *rx_buf, uint16_t rx_cap);
    void (*msleep)(void *ctx, uint32_t ms);
};

/* Overflow-safe validation of MT79xx/CONNAC2 BT ROM patch container */
int mtk_bt_fw_validate_layout(const uint8_t *fw, size_t fw_size, uint32_t *out_section_num);

/* Build a complete WMT command payload (suitable for HCI opcode 0xFC6F).
 * Returns total byte length written to out_buf, or negative on error.
 */
int mtk_bt_build_wmt_cmd(uint8_t *out_buf, size_t out_cap,
                         uint8_t op, uint8_t flag,
                         const uint8_t *data, uint16_t dlen);

/* Parse a WMT vendor event starting at struct mtk_wmt_hdr.
 * Returns enum mtk_wmt_status, or MTK_WMT_INVALID on malformed/mismatched op.
 */
uint8_t mtk_bt_parse_wmt_evt(const uint8_t *evt_buf, size_t evt_len, uint8_t expected_op);

/* Send one WMT command synchronously and return parsed status */
int mtk_bt_wmt_sync(const struct mtk_bt_hci_ops *ops,
                    uint8_t op, uint8_t flag,
                    const uint8_t *data, uint16_t dlen,
                    uint8_t *out_status);

/* Download a sectioned CONNAC2/MT79xx BT ROM patch over WMT HCI (0xFC6F) */
int mtk_bt_download_patch_sectioned(const struct mtk_bt_hci_ops *ops,
                                    const uint8_t *fw, size_t fw_size,
                                    uint32_t dev_id);

/* Enable BT protocol (WMT_FUNC_CTRL flag=0, param=1) + optional BD_ADDR (0xFC1A) */
int mtk_bt_enable_and_set_addr(const struct mtk_bt_hci_ops *ops,
                               const uint8_t bdaddr_6b[6]);

/* ============================================================================
 * 4. Implementation
 * ============================================================================ */
#ifdef HAKIOS_MT6895_BT_WMT_IMPLEMENTATION

static inline uint16_t mtk_le16_read(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

static inline uint32_t mtk_le32_read(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static inline void mtk_le16_write(void *p, uint16_t v)
{
    uint8_t *b = (uint8_t *)p;
    b[0] = (uint8_t)(v & 0xFFU);
    b[1] = (uint8_t)((v >> 8) & 0xFFU);
}

int mtk_bt_fw_validate_layout(const uint8_t *fw, size_t fw_size, uint32_t *out_section_num)
{
    if (!fw || !out_section_num)
        return -22; /* -EINVAL */
    if (fw_size < (size_t)(MTK_BT_FW_PATCH_HDR_SIZE + MTK_BT_FW_GD_SIZE))
        return -22;

    uint32_t sec_num = mtk_le32_read(fw + MTK_BT_FW_PATCH_HDR_SIZE + 12U);
    if (sec_num == 0U || sec_num > 1024U)
        return -22;

    size_t table_bytes = (size_t)sec_num * (size_t)MTK_BT_FW_SEC_MAP_SIZE;
    size_t min_needed  = (size_t)(MTK_BT_FW_PATCH_HDR_SIZE + MTK_BT_FW_GD_SIZE) + table_bytes;
    if (fw_size < min_needed)
        return -22;

    *out_section_num = sec_num;
    return 0;
}

int mtk_bt_build_wmt_cmd(uint8_t *out_buf, size_t out_cap,
                         uint8_t op, uint8_t flag,
                         const uint8_t *data, uint16_t dlen)
{
    size_t total = sizeof(struct mtk_wmt_hdr) + (size_t)dlen;
    if (!out_buf || total > out_cap || total > MTK_BT_WMT_MAX_HCI_PAYLOAD)
        return -22;

    out_buf[0] = 0x01U; /* Host -> Controller */
    out_buf[1] = op;
    mtk_le16_write(&out_buf[2], (uint16_t)(dlen + 1U));
    out_buf[4] = flag;
    if (dlen > 0U && data)
        memcpy(out_buf + sizeof(struct mtk_wmt_hdr), data, dlen);

    return (int)total;
}

uint8_t mtk_bt_parse_wmt_evt(const uint8_t *evt_buf, size_t evt_len, uint8_t expected_op)
{
    if (!evt_buf || evt_len < sizeof(struct mtk_wmt_hdr))
        return MTK_WMT_INVALID;

    uint8_t op   = evt_buf[1];
    uint8_t flag = evt_buf[4];
    if (op != expected_op)
        return MTK_WMT_INVALID;

    switch (op) {
    case MTK_WMT_SEMAPHORE:
        return (flag == 2U) ? MTK_WMT_PATCH_UNDONE : MTK_WMT_PATCH_DONE;

    case MTK_WMT_FUNC_CTRL:
        if (evt_len < sizeof(struct mtk_wmt_hdr) + 2U)
            return flag ? MTK_WMT_ON_UNDONE : MTK_WMT_ON_DONE;
        {
            uint16_t be_sts = (uint16_t)(((uint16_t)evt_buf[5] << 8) | evt_buf[6]);
            if (be_sts == 0x0404U) return MTK_WMT_ON_DONE;
            if (be_sts == 0x0420U) return MTK_WMT_ON_PROGRESS;
            return MTK_WMT_ON_UNDONE;
        }

    case MTK_WMT_PATCH_DWNLD:
        if (flag == 2U) return MTK_WMT_PATCH_DONE;
        if (flag == 1U) return MTK_WMT_PATCH_PROGRESS;
        return MTK_WMT_PATCH_UNDONE;

    case MTK_WMT_RST:
        return MTK_WMT_ON_DONE;

    default:
        return MTK_WMT_INVALID;
    }
}

int mtk_bt_wmt_sync(const struct mtk_bt_hci_ops *ops,
                    uint8_t op, uint8_t flag,
                    const uint8_t *data, uint16_t dlen,
                    uint8_t *out_status)
{
    if (!ops || !ops->hci_cmd_sync)
        return -22;

    uint8_t tx[MTK_BT_WMT_MAX_HCI_PAYLOAD];
    uint8_t rx[64];
    int tx_len = mtk_bt_build_wmt_cmd(tx, sizeof(tx), op, flag, data, dlen);
    if (tx_len < 0)
        return tx_len;

    int rx_len = ops->hci_cmd_sync(ops->ctx, MTK_HCI_OP_WMT, tx, (uint16_t)tx_len, rx, sizeof(rx));
    if (rx_len < 0)
        return rx_len;

    uint8_t sts = mtk_bt_parse_wmt_evt(rx, (size_t)rx_len, op);
    if (out_status)
        *out_status = sts;
    return (sts == MTK_WMT_INVALID) ? -5 : 0;
}

int mtk_bt_download_patch_sectioned(const struct mtk_bt_hci_ops *ops,
                                    const uint8_t *fw, size_t fw_size,
                                    uint32_t dev_id)
{
    uint32_t section_num = 0;
    int rc = mtk_bt_fw_validate_layout(fw, fw_size, &section_num);
    if (rc < 0)
        return rc;

    for (uint32_t i = 0; i < section_num; i++) {
        size_t map_ofs = (size_t)(MTK_BT_FW_PATCH_HDR_SIZE + MTK_BT_FW_GD_SIZE) +
                         (size_t)i * (size_t)MTK_BT_FW_SEC_MAP_SIZE;
        const uint8_t *sec_map = fw + map_ofs;
        uint32_t sec_offset    = mtk_le32_read(sec_map + 4U);
        uint32_t dl_size       = mtk_le32_read(sec_map + 16U); /* bin_info_spec.dl_size */
        uint32_t dl_mode_crc   = mtk_le32_read(sec_map + 32U);

        /* MT6639 quirk from btmtk.c: skip non-BT sections (dlmode byte0 != 0x01) */
        if (dev_id == 0x6639U && dl_size > 0U && (dl_mode_crc & 0xFFU) != 0x01U)
            continue;
        if (dl_size == 0U)
            continue;
        if ((size_t)sec_offset > fw_size || (size_t)dl_size > fw_size - (size_t)sec_offset)
            return -22;

        /* Send section descriptor header (legacy dl mode = 0 + 52-byte bin_info_spec) */
        uint8_t desc_cmd[MTK_BT_SEC_MAP_SEND_SIZE + 1U];
        desc_cmd[0] = 0x00U;
        memcpy(desc_cmd + 1U, sec_map + MTK_BT_SEC_MAP_COMMON_SIZE, MTK_BT_SEC_MAP_SEND_SIZE);

        uint8_t status = MTK_WMT_INVALID;
        int retry = 20;
        while (retry > 0) {
            rc = mtk_bt_wmt_sync(ops, MTK_WMT_PATCH_DWNLD, MTK_WMT_PKT_HEADER,
                                 desc_cmd, sizeof(desc_cmd), &status);
            if (rc < 0)
                return rc;
            if (status == MTK_WMT_PATCH_UNDONE)
                break;
            if (status == MTK_WMT_PATCH_DONE)
                goto next_section;
            if (status == MTK_WMT_PATCH_PROGRESS) {
                if (ops->msleep) ops->msleep(ops->ctx, 100U);
                retry--;
            } else {
                return -5;
            }
        }
        if (retry == 0)
            return -5;

        /* Stream section payload in <= 250-byte chunks */
        const uint8_t *p = fw + sec_offset;
        uint32_t rem = dl_size;
        bool first = true;
        while (rem > 0U) {
            uint16_t chunk = (rem > MTK_BT_WMT_MAX_CHUNK) ? MTK_BT_WMT_MAX_CHUNK : (uint16_t)rem;
            uint8_t flag;
            if (first) {
                flag = MTK_WMT_PKT_START;
                first = false;
            } else if (rem == chunk) {
                flag = MTK_WMT_PKT_END;
            } else {
                flag = MTK_WMT_PKT_CONTINUE;
            }
            rc = mtk_bt_wmt_sync(ops, MTK_WMT_PATCH_DWNLD, flag, p, chunk, &status);
            if (rc < 0 || status == MTK_WMT_PATCH_PROGRESS)
                return -5;
            p += chunk;
            rem -= chunk;
        }

    next_section:
        continue;
    }

    if (ops->msleep)
        ops->msleep(ops->ctx, 100U);
    return 0;
}

int mtk_bt_enable_and_set_addr(const struct mtk_bt_hci_ops *ops,
                               const uint8_t bdaddr_6b[6])
{
    uint8_t param = 1U;
    uint8_t status = 0U;
    int rc = mtk_bt_wmt_sync(ops, MTK_WMT_FUNC_CTRL, 0U, &param, 1U, &status);
    if (rc < 0)
        return rc;

    if (bdaddr_6b) {
        uint8_t rx[16];
        rc = ops->hci_cmd_sync(ops->ctx, MTK_HCI_OP_SET_BDADDR, bdaddr_6b, 6U, rx, sizeof(rx));
        if (rc < 0)
            return rc;
    }
    return 0;
}

#endif /* HAKIOS_MT6895_BT_WMT_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* HAKIOS_MT6895_BT_WMT_H */
