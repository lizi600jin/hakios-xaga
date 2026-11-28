/*
 * kal_core_xnu.c — Real freestanding XNU / Host implementation of the 75
 *                  remaining non-P2P KAL symbols from external_kal.txt.
 */
#include "hakios_gen4m_kal_ext.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct hakios_kal_ext_ops        g_ext_ops;
static struct hakios_kal_ext_stats g_ext_stats;
static uint32_t g_prng_state = 0x68952026U;

void hakios_kal_ext_bind(const struct hakios_kal_ext_ops *ops)
{
    memset(&g_ext_stats, 0, sizeof(g_ext_stats));
    if (ops)
        g_ext_ops = *ops;
    else
        memset(&g_ext_ops, 0, sizeof(g_ext_ops));
}

const struct hakios_kal_ext_stats *hakios_kal_ext_get_stats(void)
{
    return &g_ext_stats;
}

/* ---- 1. Locks, Sync, Events, Timers & State (17) ---- */
void kalAcquireMutex(void *g, int cat) { (void)g; (void)cat; g_ext_stats.mutex_acquires++; }
void kalReleaseMutex(void *g, int cat) { (void)g; (void)cat; g_ext_stats.mutex_releases++; }
void kalAcquireSpinLock(void *g, int cat, unsigned long *flags) {
    (void)g; (void)cat; if (flags) *flags = 0UL; g_ext_stats.spin_acquires++;
}
void kalReleaseSpinLock(void *g, int cat, unsigned long flags) {
    (void)g; (void)cat; (void)flags; g_ext_stats.spin_releases++;
}
uint8_t  kalSetTimer(void *g, uint32_t interval_ms) { (void)g; (void)interval_ms; return 1U; }
uint32_t kalGetBootTime(void) {
    return g_ext_ops.boot_ms ? (uint32_t)g_ext_ops.boot_ms(g_ext_ops.ctx) : 0U;
}
void kalSetEvent(void *g)            { (void)g; }
void kalSetHifDbgEvent(void *g)      { (void)g; }
void kalSetTxCmdDoneEvent(void *g)   { (void)g; }
void kalSetTxCmdEvent2Hif(void *g)   { (void)g; }
void kalSetTxEvent2Hif(void *g)      { (void)g; }
void kalSetWmmUpdateEvent(void *g)   { (void)g; }
uint8_t kalIsCardRemoved(void *g)    { (void)g; return 0U; }
uint8_t kalIsHalted(void)            { return 0U; }
uint8_t kalIsResetting(void)         { return 0U; }
void kalUpdateBssTimestamp(void *g)  { (void)g; }
uint32_t kalRandomNumber(void) {
    /* xorshift32 fallback for host/bring-up */
    uint32_t x = g_prng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_prng_state = x;
    return x;
}

/* ---- 2. Register, IO, EMI MPU, NVRAM & Firmware (14) ---- */
uint8_t kalDevRegRead(void *g, uint32_t reg, uint32_t *val) {
    (void)g;
    if (!val || !g_ext_ops.reg_rd32) return 0U;
    *val = g_ext_ops.reg_rd32(g_ext_ops.ctx, reg);
    g_ext_stats.reg_reads++;
    return 1U;
}
uint8_t kalDevRegWrite(void *g, uint32_t reg, uint32_t val) {
    (void)g;
    if (!g_ext_ops.reg_wr32) return 0U;
    g_ext_ops.reg_wr32(g_ext_ops.ctx, reg, val);
    g_ext_stats.reg_writes++;
    return 1U;
}
void kalMemCopyFromIo(void *dst, const volatile void *src, uint32_t len) {
    if (!dst || !src) return;
    uint8_t *d = (uint8_t *)dst;
    const volatile uint8_t *s = (const volatile uint8_t *)src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}
void kalMemCopyToIo(volatile void *dst, const void *src, uint32_t len) {
    if (!dst || !src) return;
    volatile uint8_t *d = (volatile uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}
uint8_t kalCfgDataRead16(void *g, uint32_t ofs, uint16_t *out) {
    (void)g;
    if (!out || ofs + 2U > sizeof(g_aucNvram)) return 0U;
    *out = (uint16_t)g_aucNvram[ofs] | ((uint16_t)g_aucNvram[ofs + 1U] << 8);
    return 1U;
}
uint8_t kalCfgDataWrite16(void *g, uint32_t ofs, uint16_t val) {
    /* Read-only flash policy: writes only touch RAM shadow g_aucNvram */
    (void)g;
    if (ofs + 2U > sizeof(g_aucNvram)) return 0U;
    g_aucNvram[ofs]      = (uint8_t)(val & 0xFFU);
    g_aucNvram[ofs + 1U] = (uint8_t)(val >> 8);
    return 1U;
}
uint8_t kalCfgDataWrite8(void *g, uint32_t ofs, uint8_t val) {
    (void)g;
    if (ofs >= sizeof(g_aucNvram)) return 0U;
    g_aucNvram[ofs] = val;
    return 1U;
}
void kalSetEmiMpuProtection(uint64_t base, bool enable) {
    g_ext_stats.emi_mpu_base = base;
    g_ext_stats.emi_mpu_enabled = enable;
}
int kalRequestFirmware(const char *name, uint8_t *buf, uint32_t buf_sz, uint32_t *out_sz, uint32_t ofs) {
    if (!name || !buf || !out_sz || !g_ext_ops.load_fw) return -2;
    const uint8_t *fw = NULL;
    size_t fw_len = 0;
    int rc = g_ext_ops.load_fw(g_ext_ops.ctx, name, &fw, &fw_len);
    if (rc != 0 || !fw || ofs >= fw_len) return -2;
    size_t n = fw_len - ofs;
    if (n > buf_sz) n = buf_sz;
    memcpy(buf, fw + ofs, n);
    *out_sz = (uint32_t)n;
    return 0;
}
void *kalFirmwareImageMapping(void *g, void **buf, uint32_t *len, int dl_mode) {
    (void)g; (void)dl_mode;
    if (buf) *buf = NULL;
    if (len) *len = 0U;
    return NULL;
}
uint32_t kalFirmwareImageUnmapping(void *g, void *hdl, void *buf) {
    (void)g; (void)hdl; (void)buf; return 0U;
}
uint8_t kalRetrieveNetworkAddress(void *g, uint8_t *mac_out) {
    (void)g;
    if (!mac_out) return 0U;
    if (g_ext_ops.get_wifi_mac && g_ext_ops.get_wifi_mac(g_ext_ops.ctx, mac_out))
        return 1U;
    /* Fallback: nvdata shadow offset 0x04..0x09 */
    memcpy(mac_out, g_aucNvram + 4, 6);
    return 1U;
}
/* gl_kal.h: struct REG_INFO *(struct GLUE_INFO *) -- ONE arg, returns a
 * pointer that wlan_lib.c:5911 hands straight to wlanLoadManufactureData().
 * Returning NULL there is a dereference of NULL, so this hands back a
 * zero-filled registry image: the driver runs with every registry default,
 * which is the honest state until the XNU host layer supplies the real one.
 */
static struct { unsigned char b[1024] __attribute__((aligned(8))); } g_reg_info_zero;
void *kalGetConfiguration(void *g) { (void)g; return &g_reg_info_zero; }
uint8_t kalIsConfigurationExist(void *g) { (void)g; return 0U; }

/* ---- 3. Packet / TX-RX / Queue (11) ---- */
struct xnu_kal_pkt {
    uint8_t *raw;
    uint8_t *data;
    uint32_t len;
};

void *kalPacketAllocWithHeadroom(void *g, uint32_t size, uint8_t **ppucData) {
    (void)g;
    struct xnu_kal_pkt *p = (struct xnu_kal_pkt *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->raw = (uint8_t *)calloc(1, size + 64U);
    if (!p->raw) { free(p); return NULL; }
    p->data = p->raw + 32U;
    p->len  = size;
    if (ppucData) *ppucData = p->data;
    return p;
}
void kalPacketFree(void *g, void *pkt) {
    (void)g;
    struct xnu_kal_pkt *p = (struct xnu_kal_pkt *)pkt;
    if (!p) return;
    free(p->raw);
    free(p);
}
uint32_t kalRxIndicateOnePkt(void *g, void *pkt) {
    (void)g;
    struct xnu_kal_pkt *p = (struct xnu_kal_pkt *)pkt;
    if (p && g_ext_ops.rx_deliver)
        g_ext_ops.rx_deliver(g_ext_ops.ctx, p->data, p->len);
    g_ext_stats.rx_indicated++;
    return 0U;
}
void kalSendCompleteAndAwakeQueue(void *g, void *pkt) { (void)g; (void)pkt; g_ext_stats.tx_completed++; }
void kalSecurityFrameSendComplete(void *g, void *pkt, uint32_t st) { (void)g; (void)pkt; (void)st; }
uint8_t kalQoSFrameClassifierAndPacketInfo(void *g, void *pkt, void *info) {
    (void)g; (void)pkt; (void)info; return 1U;
}
uint8_t kalGetEthDestAddr(void *g, void *pkt, uint8_t *dest_mac) {
    (void)g;
    struct xnu_kal_pkt *p = (struct xnu_kal_pkt *)pkt;
    if (!p || !dest_mac || p->len < 6U) return 0U;
    memcpy(dest_mac, p->data, 6U);
    return 1U;
}
uint32_t kalGetTxPendingFrameCount(void *g) { (void)g; return 0U; }
void kalClearCommandQueue(void *g, uint8_t need_hdlr) { (void)g; (void)need_hdlr; }
void kalClearMgmtFramesByBssIdx(void *g, uint8_t bss) { (void)g; (void)bss; }
void kalClearSecurityFramesByBssIdx(void *g, uint8_t bss) { (void)g; (void)bss; }

/* ---- 4. OID / Command Control Plane (7) ---- */
uint32_t kalEnqueueCommand(void *g, void *cmd) { (void)g; (void)cmd; g_ext_stats.cmds_enqueued++; return 0U; }
uint32_t kalIoctl(void *g, void *fn, void *buf, uint32_t len, uint8_t is_read,
                  uint8_t wait, uint8_t is_cmd, uint32_t *out_len) {
    (void)g; (void)fn; (void)buf; (void)len; (void)is_read; (void)wait; (void)is_cmd;
    if (out_len) *out_len = 0U;
    g_ext_stats.ioctls++;
    return 0U;
}
uint32_t kalIoctlByBssIdx(void *g, void *fn, void *buf, uint32_t len, uint8_t is_read,
                          uint8_t wait, uint8_t is_cmd, uint32_t *out_len, uint8_t bss) {
    (void)bss;
    return kalIoctl(g, fn, buf, len, is_read, wait, is_cmd, out_len);
}
void kalOidClearance(void *g) { (void)g; }
void kalOidCmdClearance(void *g) { (void)g; }
void kalOidComplete(void *g, void *cmd, uint32_t info_len, uint32_t status) {
    (void)g; (void)cmd; (void)info_len; (void)status;
}
void kalResetStats(void *dev) { (void)dev; }

/* ---- 5. Scan, Assoc, Media, IE & Utility (26) ---- */
void kalScanDone(void *g, int net_type, uint32_t status) {
    (void)g; (void)net_type; (void)status; g_ext_stats.scan_done_cnt++;
}
void kalSchedScanResults(void *g) { (void)g; }
void kalIndicateBssInfo(void *g, uint8_t *buf, uint32_t len, uint8_t ch, int32_t rssi, int band) {
    (void)g; (void)buf; (void)len; (void)ch; (void)rssi; (void)band;
}
void kalIndicateChannelSwitch(void *g, uint8_t sco, uint8_t ch, int band) {
    (void)g; (void)sco; (void)ch; (void)band;
}
void kalIndicateMgmtTxStatus(void *g, uint64_t cookie, uint8_t ack,
                             uint8_t *buf, uint32_t len, uint8_t bss) {
    (void)g; (void)cookie; (void)ack; (void)buf; (void)len; (void)bss;
}
int8_t kalIndicateOpModeChange(void *adapter, uint8_t bss, uint8_t bw,
                               uint8_t tx_nss, uint8_t rx_nss) {
    (void)adapter; (void)bss; (void)bw; (void)tx_nss; (void)rx_nss; return 0;
}
void kalIndicateRxMgmtFrame(void *adapter, void *g, void *rfb, uint8_t bss) {
    (void)adapter; (void)g; (void)rfb; (void)bss;
}
void kalIndicateStatusAndComplete(void *g, uint32_t status, void *buf, uint32_t len, uint8_t bss) {
    (void)g; (void)status; (void)buf; (void)len; (void)bss;
}
uint32_t kalGetMediaStateIndicated(void *g, uint8_t bss) { (void)g; (void)bss; return g_ext_stats.media_state; }
void     kalSetMediaStateIndicated(void *g, uint32_t st, uint8_t bss) { (void)g; (void)bss; g_ext_stats.media_state = st; }
uint32_t kalGetMfpSetting(void *g, uint8_t bss) { (void)g; (void)bss; return 0U; }
uint8_t  kalGetRsnIeMfpCap(void *g, uint8_t bss) { (void)g; (void)bss; return 0U; }
void kalUpdateReAssocReqInfo(void *g, uint8_t *ie, uint32_t len, uint8_t reassoc, uint8_t bss) {
    (void)g; (void)ie; (void)len; (void)reassoc; (void)bss;
}
void kalUpdateReAssocRspInfo(void *g, uint8_t *ie, uint32_t len, uint8_t bss) {
    (void)g; (void)ie; (void)len; (void)bss;
}
const uint8_t *kalFindVendorIe(uint32_t oui, uint8_t oui_type, const uint8_t *ies, size_t len) {
    if (!ies) return NULL;
    size_t pos = 0U;
    uint8_t b0 = (uint8_t)((oui >> 16) & 0xFFU);
    uint8_t b1 = (uint8_t)((oui >> 8) & 0xFFU);
    uint8_t b2 = (uint8_t)(oui & 0xFFU);
    while (pos + 2U <= len) {
        uint8_t id = ies[pos], elen = ies[pos + 1U];
        if (pos + 2U + elen > len) break;
        if (id == 0xDDU && elen >= 4U &&
            ies[pos + 2U] == b0 && ies[pos + 3U] == b1 &&
            ies[pos + 4U] == b2 && ies[pos + 5U] == oui_type)
            return ies + pos;
        pos += 2U + elen;
    }
    return NULL;
}
int kalMaskMemCmp(const void *cs, const void *ct, const void *mask, size_t count) {
    const uint8_t *a = (const uint8_t *)cs, *b = (const uint8_t *)ct, *m = (const uint8_t *)mask;
    for (size_t i = 0; i < count; i++) {
        uint8_t ma = m ? m[i] : 0xFFU;
        uint8_t da = a[i] & ma, db = b[i] & ma;
        if (da != db) return (int)da - (int)db;
    }
    return 0;
}
uint8_t kalIsValidMacAddr(const uint8_t *mac) {
    if (!mac) return 0U;
    bool z = true, ff = true;
    for (int i = 0; i < 6; i++) { if (mac[i] != 0) z = false; if (mac[i] != 0xFF) ff = false; }
    return (!z && !ff && (mac[0] & 1U) == 0U) ? 1U : 0U;
}
/* gl_kal.h: u_int8_t (cfg80211_chan_def *, ieee80211_channel *,
 * RF_CHANNEL_INFO *) -- three pointers, 3 args. */
uint8_t  kalChannelFormatSwitch(void *channel_def, void *channel, void *rf_chnl_info) {
    (void)channel_def; (void)channel; (void)rf_chnl_info; return 0U;
}
uint32_t kalGetCpuBoostThreshold(void) { return 0U; }
void kalReadyOnChannel(void *g, uint64_t cookie, int band, int sco,
                        uint8_t ch, uint32_t dur_ms, uint8_t bss) {
    (void)g; (void)cookie; (void)band; (void)sco; (void)ch; (void)dur_ms; (void)bss;
}
void kalRemainOnChannelExpired(void *g, uint64_t cookie, int band, int sco,
                                uint8_t ch, uint8_t bss) {
    (void)g; (void)cookie; (void)band; (void)sco; (void)ch; (void)bss;
}
void kalRemoveBss(void *g, uint8_t *bssid, uint8_t ch, int band) {
    (void)g; (void)bssid; (void)ch; (void)band;
}
/* os/linux/include/gl_kal.h:1011 把它 #define 成 kalIndicateDriverEvent，
 * 而该符号在 .o 里以未定义名出现（这一路的宏没展开），所以按那个函数
 * 的真原型给 5 个参数：gl_kal.h:1229 (adapter, event, dataLen, bssIdx,
 * fgForceReport)。 */
uint8_t KAL_REPORT_ERROR_EVENT(void *adapter, uint32_t event, uint16_t data_len,
                               uint8_t bss, uint8_t force) {
    (void)adapter; (void)event; (void)data_len; (void)bss; (void)force;
    return 0U;
}
int kal_strtou16(const char *s, unsigned int base, uint16_t *res) {
    if (!s || !res) return -22;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, (int)base);
    if (end == s || v > 0xFFFFUL) return -22;
    *res = (uint16_t)v;
    return 0;
}
int kal_strtou32(const char *s, unsigned int base, uint32_t *res) {
    if (!s || !res) return -22;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, (int)base);
    if (end == s || v > 0xFFFFFFFFUL) return -22;
    *res = (uint32_t)v;
    return 0;
}
void kalWowProcess(void *g, uint8_t enable) { (void)g; (void)enable; }
