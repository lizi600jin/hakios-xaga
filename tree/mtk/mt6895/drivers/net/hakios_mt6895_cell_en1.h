/*
 * hakios_mt6895_cell_en1.h — Freestanding XNU Virtual Ethernet (en1) ARP/DHCPv4
 *                            Responder & MT6895 CCCI/AT Control-Plane Helpers
 *
 * Offline-solved drop-in header for lizi600jin/hakios-xaga
 *
 * Architecture grounded in:
 *   1. oosexgang/hakios-xnu-kernel (docs/cellular.md, commit 45df619)
 *      - Expose cellular data path as Ethernet interface `en1`
 *      - Answer ARP for every address and DHCPv4 (DISCOVER->OFFER, REQUEST->ACK)
 *        in-kernel with the session IPv4, /24 mask, virtual gateway, DNS & MTU
 *        so Darwin `configd` routes traffic natively without Apple CommCenter
 *      - Map signal strength to sysctl `kern.hakios.cellular_bars` (0..4)
 *   2. TASZK Security Labs (CVE-2022-21765 / 21769) & Linux drivers/net/wwan/t7xx
 *      - 16-byte MediaTek CCCI header layout
 *   3. Angxddeep/xaga-5g-sa-enabler & Luckyji6/BeaRerM
 *      - Verified MT6895 AT commands (/dev/ttyC0, AT+ESUO=4/5, AT+E5GOPT, AT+CSQ)
 *
 * Usage:
 *   #define HAKIOS_MT6895_CELL_EN1_IMPLEMENTATION
 *   #include "hakios_mt6895_cell_en1.h"
 */

#ifndef HAKIOS_MT6895_CELL_EN1_H
#define HAKIOS_MT6895_CELL_EN1_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 1. MediaTek CCCI 16-Byte Header & MT6895 Verified AT Commands
 * ============================================================================ */

#if defined(__GNUC__) || defined(__clang__)
#define CELL_PACKED __attribute__((packed))
#else
#define CELL_PACKED
#endif

/* Standard 16-byte MediaTek CCCI packet header */
struct CELL_PACKED mtk_ccci_header {
    uint32_t data0;    /* Magic or buffer addr */
    uint32_t data1;    /* Total packet length (including 16B header) */
    uint16_t channel;  /* Logical CCCI channel ID (e.g. AT, FS, RPC, MIPC) */
    uint16_t seq_num;  /* Sequence number + assert bit */
    uint32_t reserved; /* Extra parameter / flags */
};

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(struct mtk_ccci_header) == 16, "mtk_ccci_header must be 16 bytes");
#endif

/* Verified MT6895 AT command strings (Luckyji6/BeaRerM & Angxddeep/xaga-5g-sa-enabler) */
#define MTK_AT_SWITCH_SIM1      "AT+ESUO=4\r"
#define MTK_AT_SWITCH_SIM2      "AT+ESUO=5\r"
#define MTK_AT_ENABLE_5G_SA_NSA "AT+E5GOPT=7\r"
#define MTK_AT_QUERY_SIGNAL     "AT+CSQ\r"
#define MTK_AT_ACTIVATE_PDP     "AT+CGACT=1,1\r"
#define MTK_AT_QUERY_IP_DNS     "AT+CGCONTRDP=1\r"

/* ============================================================================
 * 2. Virtual Ethernet `en1` Session State (HakiOS cellular.md design)
 * ============================================================================ */

#define HAKIOS_ETH_HDR_LEN      14U
#define HAKIOS_ETH_TYPE_IPV4    0x0800U
#define HAKIOS_ETH_TYPE_ARP     0x0806U

struct hakios_cell_en1_cfg {
    uint8_t  client_mac[6];   /* Local en1 MAC address */
    uint8_t  router_mac[6];   /* Synthesized virtual router MAC */
    uint32_t client_ip_be;    /* Assigned cellular IPv4 (network byte order) */
    uint32_t gateway_ip_be;   /* Virtual /24 gateway IPv4 (network byte order) */
    uint32_t netmask_be;      /* Typically 255.255.255.0 (0x00FFFFFF LE / 0xFFFFFF00 BE) */
    uint32_t dns1_ip_be;      /* Primary DNS IPv4 (network byte order) */
    uint32_t dns2_ip_be;      /* Secondary DNS IPv4 (0 if unused) */
    uint16_t mtu;             /* Typically 1420..1500 */
    uint32_t lease_sec;       /* DHCP lease time, e.g. 3600 */
};

/* ============================================================================
 * 3. Public API
 * ============================================================================ */

/* Map AT+CSQ RSSI (0..31, 99=unknown) to SpringBoard kern.hakios.cellular_bars (0..4) */
uint8_t hakios_csq_to_bars(int csq_rssi);

/* Wrap an incoming raw IPv4 packet from DPMAIF into an Ethernet II frame for en1.
 * Returns total Ethernet frame length, or 0 if buffer is too small / invalid IPv4.
 */
size_t hakios_cell_wrap_ipv4_rx(const struct hakios_cell_en1_cfg *cfg,
                                const uint8_t *raw_ipv4, size_t ipv4_len,
                                uint8_t *out_eth, size_t out_cap);

/* Inspect an outgoing Ethernet II frame from Darwin `en1`:
 *   - If ARP Request or DHCPv4 DISCOVER/REQUEST: synthesizes the reply Ethernet
 *     frame into `out_reply_eth` and returns reply length (> 0) with `*out_is_ip_tx = false`.
 *   - If normal IPv4 unicast packet: sets `*out_is_ip_tx = true` and returns 0
 *     (caller strips 14-byte Ethernet header and submits raw IPv4 to DPMAIF TX).
 *   - Otherwise returns 0 with `*out_is_ip_tx = false` (drop frame).
 */
size_t hakios_cell_handle_en1_tx(const struct hakios_cell_en1_cfg *cfg,
                                 const uint8_t *tx_eth, size_t tx_len,
                                 uint8_t *out_reply_eth, size_t reply_cap,
                                 bool *out_is_ip_tx);

/* ============================================================================
 * 4. Implementation
 * ============================================================================ */
#ifdef HAKIOS_MT6895_CELL_EN1_IMPLEMENTATION

static inline uint16_t cell_be16_rd(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline void cell_be16_wr(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFU);
}

static inline void cell_be32_wr(uint8_t *p, uint32_t v_be)
{
    memcpy(p, &v_be, 4U);
}

static uint16_t cell_ip_checksum(const uint8_t *buf, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i + 1U < len; i += 2U)
        sum += cell_be16_rd(buf + i);
    if (len & 1U)
        sum += ((uint32_t)buf[len - 1U] << 8);
    while (sum >> 16)
        sum = (sum & 0xFFFFU) + (sum >> 16);
    return (uint16_t)(~sum & 0xFFFFU);
}

uint8_t hakios_csq_to_bars(int csq_rssi)
{
    if (csq_rssi < 0 || csq_rssi == 99 || csq_rssi < 2)
        return 0U;
    if (csq_rssi <= 9)  return 1U;
    if (csq_rssi <= 14) return 2U;
    if (csq_rssi <= 19) return 3U;
    return 4U;
}

size_t hakios_cell_wrap_ipv4_rx(const struct hakios_cell_en1_cfg *cfg,
                                const uint8_t *raw_ipv4, size_t ipv4_len,
                                uint8_t *out_eth, size_t out_cap)
{
    if (!cfg || !raw_ipv4 || !out_eth || ipv4_len < 20U)
        return 0U;
    if ((raw_ipv4[0] >> 4) != 4U) /* IPv4 only per HakiOS cellular.md */
        return 0U;
    if (out_cap < HAKIOS_ETH_HDR_LEN + ipv4_len)
        return 0U;

    memcpy(out_eth + 0U, cfg->client_mac, 6U);
    memcpy(out_eth + 6U, cfg->router_mac, 6U);
    cell_be16_wr(out_eth + 12U, HAKIOS_ETH_TYPE_IPV4);
    memcpy(out_eth + HAKIOS_ETH_HDR_LEN, raw_ipv4, ipv4_len);
    return HAKIOS_ETH_HDR_LEN + ipv4_len;
}

static size_t cell_build_arp_reply(const struct hakios_cell_en1_cfg *cfg,
                                   const uint8_t *req_eth, size_t req_len,
                                   uint8_t *out, size_t cap)
{
    if (req_len < 42U || cap < 42U)
        return 0U;
    const uint8_t *arp = req_eth + 14U;
    /* Hardware=1 (Eth), Proto=0x0800 (IPv4), HLEN=6, PLEN=4, Opcode=1 (Request) */
    if (cell_be16_rd(arp + 0) != 1U || cell_be16_rd(arp + 2) != 0x0800U ||
        arp[4] != 6U || arp[5] != 4U || cell_be16_rd(arp + 6) != 1U)
        return 0U;

    /* Ethernet header */
    memcpy(out + 0U, arp + 8U, 6U);        /* dst = requester MAC */
    memcpy(out + 6U, cfg->router_mac, 6U); /* src = virtual router MAC */
    cell_be16_wr(out + 12U, HAKIOS_ETH_TYPE_ARP);

    /* ARP reply payload: answer for ANY target IP with router_mac */
    uint8_t *r = out + 14U;
    cell_be16_wr(r + 0, 1U);
    cell_be16_wr(r + 2, 0x0800U);
    r[4] = 6U;
    r[5] = 4U;
    cell_be16_wr(r + 6, 2U);               /* ARP Reply */
    memcpy(r + 8,  cfg->router_mac, 6U);   /* Sender MAC */
    memcpy(r + 14, arp + 24, 4U);          /* Sender IP = requested target IP */
    memcpy(r + 18, arp + 8,  6U);          /* Target MAC = requester MAC */
    memcpy(r + 24, arp + 14, 4U);          /* Target IP = requester IP */
    return 42U;
}

static size_t cell_build_dhcp_reply(const struct hakios_cell_en1_cfg *cfg,
                                    const uint8_t *bootp, size_t bootp_len,
                                    uint8_t *out, size_t cap)
{
    /* Minimum BOOTP (236B) + 4B magic cookie (99.130.83.99) */
    if (bootp_len < 240U || bootp[0] != 1U)
        return 0U;
    if (bootp[236] != 99U || bootp[237] != 130U || bootp[238] != 83U || bootp[239] != 99U)
        return 0U;

    /* Find DHCP Message Type option (option 53) */
    uint8_t msg_type = 0U;
    size_t pos = 240U;
    while (pos < bootp_len) {
        uint8_t opt = bootp[pos++];
        if (opt == 0U) continue;
        if (opt == 255U) break;
        if (pos >= bootp_len) break;
        uint8_t olen = bootp[pos++];
        if (pos + olen > bootp_len) break;
        if (opt == 53U && olen >= 1U) {
            msg_type = bootp[pos];
            break;
        }
        pos += olen;
    }

    uint8_t reply_type = 0U;
    if (msg_type == 1U)      reply_type = 2U; /* DISCOVER -> OFFER */
    else if (msg_type == 3U) reply_type = 5U; /* REQUEST  -> ACK   */
    else return 0U;

    /* Build BOOTP + DHCP options inside 342-byte Ethernet frame */
    if (cap < 342U)
        return 0U;
    memset(out, 0, 342U);

    uint8_t *ip  = out + 14U;
    uint8_t *udp = ip + 20U;
    uint8_t *bp  = udp + 8U;

    bp[0] = 2U;        /* BOOTREPLY */
    bp[1] = 1U;        /* Ethernet */
    bp[2] = 6U;        /* HLEN */
    memcpy(bp + 4,  bootp + 4, 4U);            /* xid */
    cell_be32_wr(bp + 16, cfg->client_ip_be);  /* yiaddr */
    cell_be32_wr(bp + 20, cfg->gateway_ip_be); /* siaddr */
    memcpy(bp + 28, bootp + 28, 6U);           /* chaddr */
    bp[236] = 99U; bp[237] = 130U; bp[238] = 83U; bp[239] = 99U;

    size_t o = 240U;
    /* Option 53: DHCP Message Type */
    bp[o++] = 53U; bp[o++] = 1U; bp[o++] = reply_type;
    /* Option 54: Server Identifier */
    bp[o++] = 54U; bp[o++] = 4U; cell_be32_wr(bp + o, cfg->gateway_ip_be); o += 4U;
    /* Option 51: Lease Time */
    uint32_t lease = cfg->lease_sec ? cfg->lease_sec : 3600U;
    bp[o++] = 51U; bp[o++] = 4U;
    bp[o++] = (uint8_t)(lease >> 24); bp[o++] = (uint8_t)(lease >> 16);
    bp[o++] = (uint8_t)(lease >> 8);  bp[o++] = (uint8_t)(lease & 0xFFU);
    /* Option 1: Subnet Mask */
    bp[o++] = 1U; bp[o++] = 4U; cell_be32_wr(bp + o, cfg->netmask_be); o += 4U;
    /* Option 3: Router */
    bp[o++] = 3U; bp[o++] = 4U; cell_be32_wr(bp + o, cfg->gateway_ip_be); o += 4U;
    /* Option 6: DNS */
    if (cfg->dns1_ip_be) {
        if (cfg->dns2_ip_be) {
            bp[o++] = 6U; bp[o++] = 8U;
            cell_be32_wr(bp + o, cfg->dns1_ip_be); o += 4U;
            cell_be32_wr(bp + o, cfg->dns2_ip_be); o += 4U;
        } else {
            bp[o++] = 6U; bp[o++] = 4U;
            cell_be32_wr(bp + o, cfg->dns1_ip_be); o += 4U;
        }
    }
    /* Option 26: Interface MTU */
    if (cfg->mtu >= 576U) {
        bp[o++] = 26U; bp[o++] = 2U; cell_be16_wr(bp + o, cfg->mtu); o += 2U;
    }
    bp[o++] = 255U; /* End option */

    size_t udp_len = 8U + o;
    size_t ip_len  = 20U + udp_len;

    /* Ethernet header (unicast to client MAC) */
    memcpy(out + 0U, bootp + 28, 6U);
    memcpy(out + 6U, cfg->router_mac, 6U);
    cell_be16_wr(out + 12U, HAKIOS_ETH_TYPE_IPV4);

    /* IPv4 header */
    ip[0] = 0x45U;
    cell_be16_wr(ip + 2, (uint16_t)ip_len);
    ip[8] = 64U;       /* TTL */
    ip[9] = 17U;       /* UDP */
    cell_be32_wr(ip + 12, cfg->gateway_ip_be);
    cell_be32_wr(ip + 16, cfg->client_ip_be);
    cell_be16_wr(ip + 10, cell_ip_checksum(ip, 20U));

    /* UDP header (67 -> 68, checksum 0 allowed in IPv4 UDP) */
    cell_be16_wr(udp + 0, 67U);
    cell_be16_wr(udp + 2, 68U);
    cell_be16_wr(udp + 4, (uint16_t)udp_len);
    cell_be16_wr(udp + 6, 0U);

    return 14U + ip_len;
}

size_t hakios_cell_handle_en1_tx(const struct hakios_cell_en1_cfg *cfg,
                                 const uint8_t *tx_eth, size_t tx_len,
                                 uint8_t *out_reply_eth, size_t reply_cap,
                                 bool *out_is_ip_tx)
{
    if (out_is_ip_tx)
        *out_is_ip_tx = false;
    if (!cfg || !tx_eth || tx_len < HAKIOS_ETH_HDR_LEN)
        return 0U;

    uint16_t etype = cell_be16_rd(tx_eth + 12U);
    if (etype == HAKIOS_ETH_TYPE_ARP)
        return cell_build_arp_reply(cfg, tx_eth, tx_len, out_reply_eth, reply_cap);

    if (etype != HAKIOS_ETH_TYPE_IPV4 || tx_len < 34U)
        return 0U;

    const uint8_t *ip = tx_eth + 14U;
    uint8_t ihl = (uint8_t)((ip[0] & 0x0FU) * 4U);
    if ((ip[0] >> 4) != 4U || ihl < 20U || tx_len < (size_t)(14U + ihl))
        return 0U;

    /* Intercept DHCPv4 client traffic (UDP 68 -> 67) */
    if (ip[9] == 17U && tx_len >= (size_t)(14U + ihl + 8U)) {
        const uint8_t *udp = ip + ihl;
        if (cell_be16_rd(udp + 0) == 68U && cell_be16_rd(udp + 2) == 67U) {
            return cell_build_dhcp_reply(cfg, udp + 8U, tx_len - (size_t)(14U + ihl + 8U),
                                         out_reply_eth, reply_cap);
        }
    }

    if (out_is_ip_tx)
        *out_is_ip_tx = true;
    return 0U;
}

#endif /* HAKIOS_MT6895_CELL_EN1_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /* HAKIOS_MT6895_CELL_EN1_H */
