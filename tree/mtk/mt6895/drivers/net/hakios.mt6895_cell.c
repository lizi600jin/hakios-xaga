/*
 * The cellular interface of the Redmi Note 11T Pro (xaga): `en1`, a virtual Ethernet
 * port that stands in front of the MT6895's modem. Only with boot-arg cell=2.
 *
 * WHY THERE IS A VIRTUAL PORT AT ALL
 *
 * iOS has no cellular "interface" to hand to user space: there is no PPP, no
 * rmnet, no /dev/ccci. What user space sees -- configd, the status bar, Safari --
 * is a BSD interface with an IP address, and everything above it (DHCP, ARP,
 * routing, the captive-portal check) is ordinary Ethernet-to-IPv4 behaviour. So
 * the cheapest honest way to give this port a working network before the modem
 * itself is understood is to present the modem as if it were an Ethernet segment
 * with exactly one peer: a synthetic router that answers ARP and DHCP locally.
 *
 * That is what this file does, and what it deliberately does not do:
 *
 *   - it DOES answer the DHCP DISCOVER/REQUEST that configd sends, and the ARP
 *     request for the gateway that the IPv4 stack sends as soon as it has an
 *     address. Both are generated in hakios_mt6895_cell_en1.h and both are tested
 *     on the host. Between them, `en1` gets an address, a default route and DNS
 *     without a single byte reaching the modem.
 *   - it DOES wrap every IPv4 packet the modem hands up into an Ethernet frame,
 *     so the BSD stack sees the same shape it would see from a wire.
 *   - it does NOT speak CCCI. The AP-to-modem control channel (CCCI/CCIF, the
 *     "AT+ESUO"/"AT+CSQ" side) is a different block, at different addresses, with
 *     its own handshake; the DPMAIF data path at 0x1022C000 is the other half.
 *     Wiring either of them is a later step, and the en1 that this file builds
 *     works without them -- with a lease, a gateway and no actual traffic. That
 *     is the state the plan calls "configd 闭环".
 *
 * The bars are a separate, smaller lie: SpringBoard reads a number of signal bars
 * from `kern.hakios.cellular_bars`, and until the modem reports AT+CSQ that number
 * comes from whatever the caller supplies. The mapping itself (0..31 RSSI to 0..4
 * bars, 99 meaning "unknown") is in the header and is unit-tested, so when the
 * modem does start answering, the only thing that changes is who calls it.
 *
 * WHERE THE ADDRESSES COME FROM
 *
 * DPMAIF: three windows, all in one page each -- 0x1022C000 (the AO block, always
 * on), 0x1022E000 (the PD block, powered with the modem) and 0x1022F000 (infra).
 * The register offsets in the generated header (AO_UL_INT_MASK, PD_DL_BAT_INIT_DONE,
 * PD_MISC_HW_INIT_DONE, ...) come from the MT6895 machine model in
 * Cyber-Mobile/xaga-qemu, which names them after the vendor driver's own symbols;
 * the same names appear in MT6895-Mainline/linux's mtk_dpmaif. This file does not
 * touch any of them yet -- it maps the AO page only, once, to prove it is there.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "hakios_mt6895_wireless_addrs.h"
#include "hakios_mt6895_cell_en1.h"

#if defined(HAKIOS_HOST_TEST)
typedef unsigned long vm_offset_t;
typedef unsigned long vm_size_t;
extern int printf(const char *fmt, ...);
extern int PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_len);
#endif
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

#define CELL_ARG        "cell"
#define CELL_IP_ARG     "cell_ip"
#define CELL_GW_ARG     "cell_gw"
#define CELL_MAC_ARG    "cell_mac"

/* A /24 with the gateway at .1, which is what a phone tethering over USB does and
 * what every DHCP client already knows how to handle. */
#define CELL_DEFAULT_MTU      1500u
#define CELL_DEFAULT_LEASE    3600u

struct hakios_cell_state {
	bool     up;
	vm_offset_t dpmaif_ao;
	struct hakios_cell_en1_cfg cfg;
	uint8_t  bars;
	uint64_t rx_packets;
	uint64_t tx_packets;
	uint64_t arp_replies;
	uint64_t dhcp_replies;
};

static struct hakios_cell_state gCell;

/* ---------------------------------------------------------------------------
 * 1. Configuration
 *
 * The IP is not guessed: a modem's PDP context assigns it, and a synthetic one
 * has to be chosen once and kept, because configd caches the lease and a new
 * address on every boot looks like a different network to every app that
 * remembers one. The default (10.64.0.2) is inside the range Android's own
 * rmnet0 uses, so nothing else in this port will collide with it.
 * ------------------------------------------------------------------------- */

static void
cell_set_defaults(struct hakios_cell_en1_cfg *cfg)
{
	static const uint8_t mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
	static const uint8_t rmac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x02 };

	memcpy(cfg->client_mac, mac, 6);
	memcpy(cfg->router_mac, rmac, 6);
	/* network byte order: 10.64.0.2 / 10.64.0.1 / 255.255.255.0 */
	cfg->client_ip_be  = 0x0A400002u;
	cfg->gateway_ip_be = 0x0A400001u;
	cfg->netmask_be    = 0xFFFFFF00u;
	cfg->dns1_ip_be    = 0x0A400001u;   /* the synthetic router answers DNS by dropping */
	cfg->dns2_ip_be    = 0;
	cfg->mtu           = CELL_DEFAULT_MTU;
	cfg->lease_sec     = CELL_DEFAULT_LEASE;
}

/*
 * A boot argument overrides one field, so a bench run can move the interface to
 * a subnet that matches whatever is on the other end without a rebuild.
 */
static void
cell_apply_boot_args(struct hakios_cell_en1_cfg *cfg)
{
	uint32_t v = 0;

	if (PE_parse_boot_argn(CELL_IP_ARG, &v, sizeof(v)) && v != 0) {
		cfg->client_ip_be = v;
	}
	if (PE_parse_boot_argn(CELL_GW_ARG, &v, sizeof(v)) && v != 0) {
		cfg->gateway_ip_be = v;
	}
	if (PE_parse_boot_argn(CELL_MAC_ARG, &v, sizeof(v)) && v != 0) {
		/* Low 24 bits only: the first three stay 02:00:00, the locally
		 * administered prefix, so a stray address never looks like a vendor's. */
		cfg->client_mac[3] = (uint8_t)((v >> 16) & 0xFF);
		cfg->client_mac[4] = (uint8_t)((v >> 8) & 0xFF);
		cfg->client_mac[5] = (uint8_t)(v & 0xFF);
	}
}

/* ---------------------------------------------------------------------------
 * 2. The engine, called from the BSD side
 * ------------------------------------------------------------------------- */

/*
 * Outgoing frame. Returns what the BSD stack has to do with it:
 *   > 0   a reply was synthesised here (ARP or DHCP); hand it back up the stack
 *         as an incoming frame and do not touch the modem.
 *   == 0  with is_ip_tx set: a normal IPv4 packet; strip the 14-byte Ethernet
 *         header and submit the payload to DPMAIF TX.
 *   == 0  otherwise: drop.
 */
size_t
hakios_mt6895_cell_output(const uint8_t *eth, size_t len,
                          uint8_t *reply, size_t reply_cap, bool *is_ip_tx)
{
	struct hakios_cell_state *st = &gCell;
	size_t r;

	if (!st->up || eth == NULL || is_ip_tx == NULL) {
		return 0;
	}
	*is_ip_tx = false;

	r = hakios_cell_handle_en1_tx(&st->cfg, eth, len, reply, reply_cap, is_ip_tx);
	if (r > 0) {
		/*
		 * Count them separately: a run where ARP answers but DHCP does not is
		 * a different bug from one where neither does, and the counters are
		 * the only thing that tells them apart on a console log.
		 */
		if (len >= 14 && eth[12] == 0x08 && eth[13] == 0x06) {
			st->arp_replies++;
		} else {
			st->dhcp_replies++;
		}
		printf("cell: en1 answered a %s locally (%zu bytes)\n",
		       (len >= 14 && eth[12] == 0x08 && eth[13] == 0x06) ? "ARP" : "DHCP",
		       r);
		return r;
	}
	if (*is_ip_tx) {
		st->tx_packets++;
	}
	return 0;
}

/*
 * Incoming packet from the modem, raw IPv4. Wrapped into an Ethernet frame so
 * the BSD stack sees one peer, then handed up. The return is the frame length,
 * or 0 when the packet did not fit or was not IPv4 at all.
 */
size_t
hakios_mt6895_cell_input(const uint8_t *ipv4, size_t len,
                         uint8_t *eth, size_t eth_cap)
{
	struct hakios_cell_state *st = &gCell;
	size_t r;

	if (!st->up || ipv4 == NULL) {
		return 0;
	}
	r = hakios_cell_wrap_ipv4_rx(&st->cfg, ipv4, len, eth, eth_cap);
	if (r > 0) {
		st->rx_packets++;
	}
	return r;
}

/* ---------------------------------------------------------------------------
 * 3. Signal bars
 *
 * The value SpringBoard reads. Until the modem answers AT+CSQ this stays at
 * whatever the last caller set; bars 0 with the interface up is the honest
 * "no measurement", not "no signal", which is why 99 (unknown) maps to 0 and
 * is documented rather than special-cased.
 * ------------------------------------------------------------------------- */

void
hakios_mt6895_cell_set_csq(int csq_rssi)
{
	gCell.bars = hakios_csq_to_bars(csq_rssi);
	printf("cell: CSQ rssi=%d -> %u bars\n", csq_rssi, gCell.bars);
}

uint8_t
hakios_mt6895_cell_bars(void)
{
	return gCell.bars;
}

/* ---------------------------------------------------------------------------
 * 4. Entry
 * ------------------------------------------------------------------------- */

int
hakios_mt6895_cell_start(void)
{
	struct hakios_cell_state *st = &gCell;
	uint32_t which = 0;
	vm_offset_t ao;

	if (!PE_parse_boot_argn(CELL_ARG, &which, sizeof(which)) || which != 2) {
		return 0;   /* not asked for */
	}

	memset(st, 0, sizeof(*st));
	cell_set_defaults(&st->cfg);
	cell_apply_boot_args(&st->cfg);

	/*
	 * The AO page is the only DPMAIF window that is powered before the modem
	 * is, so it is the only one safe to map here. A read of the interrupt mask
	 * proves the block is there; nothing is written, because the mask the
	 * loader programmed already describes which queues this AP expects, and
	 * overwriting it before the queues exist is how a data path starts life
	 * with its interrupts off.
	 */
	ao = ml_io_map((vm_offset_t)XAGA_WIN_DPMAIF_BASE, XAGA_WIN_DPMAIF_SIZE);
	if (ao == 0) {
		printf("cell: DPMAIF AO page did not map\n");
		return -1;
	}
	st->dpmaif_ao = ao;
	printf("cell: DPMAIF AO @0x%llx, UL int mask 0x%08x (read only)\n",
	       (unsigned long long)XAGA_WIN_DPMAIF_BASE,
	       *(volatile uint32_t *)((uintptr_t)ao + XAGA_DPMAIF_AO_UL_INT_MASK));

	st->up = true;

	printf("cell: en1 configured %08x/%08x gw %08x, mtu %u, lease %u s\n",
	       st->cfg.client_ip_be, st->cfg.netmask_be, st->cfg.gateway_ip_be,
	       st->cfg.mtu, st->cfg.lease_sec);
	printf("cell: en1 up. DHCP/ARP answer locally; DPMAIF TX and CCCI are NOT "
	       "wired, so the interface has a lease and no traffic.\n");
	return 0;
}
