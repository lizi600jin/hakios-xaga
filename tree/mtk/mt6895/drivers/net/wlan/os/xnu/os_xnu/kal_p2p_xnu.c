/*
 * kal_p2p_xnu.c — STA-mode safe stubs for all 38 Wi-Fi Direct / P2P KAL symbols
 *                 referenced by libgen4m_soc7_core.a (72/72 units).
 *
 * Initial iOS 16.5 en0 bring-up targets STA association; P2P/GO/SoftAP hooks
 * return safe disabled/no-op defaults without crashing the FSM.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

void     kalP2PCacFinishedUpdate(void *g, uint8_t idx) { (void)g; (void)idx; }
void     kalP2PCacStartedUpdate(void *g, uint8_t idx) { (void)g; (void)idx; }
uint32_t kalP2PCalP2P_IELen(void *g, uint8_t idx, uint8_t r) { (void)g; (void)idx; (void)r; return 0U; }
uint32_t kalP2PCalWSC_IELen(void *g, uint8_t idx, uint8_t r) { (void)g; (void)idx; (void)r; return 0U; }
uint8_t  kalP2PCmpBlackList(void *g, uint8_t *bssid, uint8_t role) {
    (void)g; (void)bssid; (void)role; return 0U;
}
void     kalP2PEnableNetDev(void *g, uint8_t idx) { (void)g; (void)idx; }
void     kalP2PGCIndicateConnectionStatus(void *g, uint8_t idx, void *info,
                                          uint8_t *ie, uint16_t len, uint16_t st, uint32_t r) {
    (void)g; (void)idx; (void)info; (void)ie; (void)len; (void)st; (void)r;
}
void     kalP2PGOStationUpdate(void *g, uint8_t idx, void *sta, uint8_t add) {
    (void)g; (void)idx; (void)sta; (void)add;
}
void     kalP2PGenP2P_IE(void *g, uint8_t idx, uint8_t *buf, uint8_t role) {
    (void)g; (void)idx; (void)buf; (void)role;
}
void     kalP2PGenWSC_IE(void *g, uint8_t type, uint8_t *buf, uint8_t role) {
    (void)g; (void)type; (void)buf; (void)role;
}
uint8_t  kalP2PGetCcmpCipher(void *g, uint8_t idx) { (void)g; (void)idx; return 0U; }
uint32_t kalP2PGetCipher(void *g, uint8_t idx) { (void)g; (void)idx; return 0U; }
uint8_t  kalP2PGetRole(void *g, uint8_t idx) { (void)g; (void)idx; return 0U; }
uint8_t  kalP2PGetTkipCipher(void *g, uint8_t idx) { (void)g; (void)idx; return 0U; }
uint8_t  kalP2PGetWepCipher(void *g, uint8_t idx) { (void)g; (void)idx; return 0U; }
void     kalP2PIndicateBssInfo(void *g, uint8_t *buf, uint32_t len, void *ch, int32_t rssi) {
    (void)g; (void)buf; (void)len; (void)ch; (void)rssi;
}
/* gl_p2p_kal.h: (glue, u8SeqNum, u4ChannelNum, eBand, eSco) -- 5 args,
 * and the second is a sequence number, not a role index. */
void     kalP2PIndicateChannelExpired(void *g, uint64_t seq, uint32_t ch,
                                      int band, int sco) {
    (void)g; (void)seq; (void)ch; (void)band; (void)sco;
}
void     kalP2PIndicateChannelReady(void *g, uint8_t idx, uint64_t cookie, void *ch, uint8_t b, uint32_t d) {
    (void)g; (void)idx; (void)cookie; (void)ch; (void)b; (void)d;
}
/* gl_p2p_kal.h: (glue, MSDU_INFO *, u_int8_t fgIsAck) -- 3 args. */
void     kalP2PIndicateMgmtTxStatus(void *g, void *msdu, uint8_t ack) {
    (void)g; (void)msdu; (void)ack;
}
void     kalP2PIndicateRxMgmtFrame(void *g, uint8_t idx, void *rfb, uint8_t ch, int32_t rssi) {
    (void)g; (void)idx; (void)rfb; (void)ch; (void)rssi;
}
void     kalP2PIndicateScanDone(void *g, uint8_t idx, uint8_t aborted) { (void)g; (void)idx; (void)aborted; }
uint8_t  kalP2PIsTxCarrierOn(void *g, uint8_t idx) { (void)g; (void)idx; return 0U; }
uint8_t  kalP2PMaxClients(void *g, uint32_t n, uint8_t role) {
    (void)g; (void)role; return (uint8_t)n;
}
void     kalP2PRddDetectUpdate(void *g, uint8_t idx) { (void)g; (void)idx; }
uint8_t  kalP2PResetBlackList(void *g, uint8_t role) { (void)g; (void)role; return 0U; }
void     kalP2PSetCipher(void *g, uint32_t c, uint8_t idx) { (void)g; (void)c; (void)idx; }
void     kalP2PSetRole(void *g, uint8_t r, uint8_t idx) { (void)g; (void)r; (void)idx; }
void     kalP2PTxCarrierOn(void *g, uint8_t idx) { (void)g; (void)idx; }
/* gl_p2p_kal.h:183: (glue, pucFrameBody, u4FrameBodyLen, fgReassocRequest,
 * ucBssIndex) -- 5 args. */
void     kalP2PUpdateAssocInfo(void *g, uint8_t *body, uint32_t blen,
                               uint8_t reassoc, uint8_t bss) {
    (void)g; (void)body; (void)blen; (void)reassoc; (void)bss;
}
void     kalP2PUpdateP2P_IE(void *g, uint8_t idx, uint8_t *ie, uint16_t len, uint8_t role) {
    (void)g; (void)idx; (void)ie; (void)len; (void)role;
}
void     kalP2PUpdateWSC_IE(void *g, uint8_t idx, uint8_t type, uint8_t *ie, uint32_t len) {
    (void)g; (void)idx; (void)type; (void)ie; (void)len;
}
/* gl_p2p_kal.h: (glue, ucRoleIndex, eBand, ucPrimaryCh, ucSecondCh,
 * ucSeg0Ch, ucSeg1Ch, eChnlBw, eHwMode) -- 9 args. */
void     kalP2pIndicateAcsResult(void *g, uint8_t idx, int band, uint8_t ch,
                                 uint8_t second, uint8_t seg0, uint8_t seg1,
                                 int bw, int hw_mode) {
    (void)g; (void)idx; (void)band; (void)ch; (void)second; (void)seg0;
    (void)seg1; (void)bw; (void)hw_mode;
}
void     kalP2pIndicateChnlSwitch(void *ad, void *bss) { (void)ad; (void)bss; }
void     kalP2pIndicateQueuedMgmtFrame(void *g, void *q) { (void)g; (void)q; }
uint8_t  kalP2pIsStoppingAp(void *g, uint8_t idx) { (void)g; (void)idx; return 0U; }
void     kalP2pNotifyStopApComplete(void *ad, uint8_t idx) { (void)ad; (void)idx; }
void     kalP2pPreStartRdd(void *g, uint8_t idx, uint32_t ch, int band) {
    (void)g; (void)idx; (void)ch; (void)band;
}
void     kalP2pUnlinkBss(void *g, uint8_t *bssid) { (void)g; (void)bssid; }
