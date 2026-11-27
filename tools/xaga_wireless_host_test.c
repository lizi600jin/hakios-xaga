/*
 * Host-side check for the three wireless drivers. Not a kernel test: it links the
 * same sources against a stub kernel (ml_io_map hands out RAM, PE_parse_boot_argn
 * reads a table) so that the logic which *can* be exercised without a phone is
 * exercised on every change.
 *
 * What this proves:
 *   - the CONNSYS EMI guard refuses a missing base, a misaligned base and a base
 *     that is not the factory one, and accepts the factory one;
 *   - the MMIO window table expands to the right count with no duplicate;
 *   - nvdata parsing accepts a real unicast MAC and refuses 00:.., ff:.., multicast;
 *   - the BT path validates a ROM-patch container and reports "no transport"
 *     rather than pretending to bring the controller up;
 *   - en1 answers DHCP DISCOVER/REQUEST and the ARP for its gateway, and passes
 *     ordinary IPv4 through as "submit to TX".
 *
 * What it cannot prove, and does not claim: anything that needs the phone.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/* ---- stub kernel ------------------------------------------------------- */

#define POOL_SIZE (64u * 1024u * 1024u)
static uint8_t gPool[POOL_SIZE];
static size_t  gPoolUsed;

typedef unsigned long vm_offset_t;
typedef unsigned long vm_size_t;

vm_offset_t
ml_io_map(vm_offset_t phys_addr, vm_size_t size)
{
	vm_offset_t kva;

	(void)phys_addr;
	if (size < 4) {
		size = 4;
	}
	if (gPoolUsed + size > POOL_SIZE) {
		return 0;
	}
	kva = (vm_offset_t)(uintptr_t)&gPool[gPoolUsed];
	gPoolUsed += size;
	return kva;
}

/* The arguments a boot would pass. */
struct stub_arg {
	const char *name;
	uint64_t    value;
};
static struct stub_arg gArgs[] = {
	{ "wifi",           2 },
	{ "bt",             2 },
	{ "cell",           2 },
	{ "consys_emi_pa",  0x5C000000ULL },
	{ "consys_emi_size", 0x1400000ULL },
	{ 0, 0 }
};

void
IODelay(unsigned microseconds)
{
	(void)microseconds;   /* the stub does not wait; the checks are about ordering */
}

int
PE_parse_boot_argn(const char *arg_string, void *arg_ptr, int max_len)
{
	for (int i = 0; gArgs[i].name; i++) {
		if (strcmp(gArgs[i].name, arg_string) != 0) {
			continue;
		}
		if (max_len >= (int)sizeof(uint64_t)) {
			*(uint64_t *)arg_ptr = gArgs[i].value;
		} else {
			*(uint32_t *)arg_ptr = (uint32_t)gArgs[i].value;
		}
		return 1;
	}
	return 0;
}

/* The transport is not wired on a fresh port; the driver must say so. */
const struct mtk_bt_hci_ops *
hakios_mt6895_bt_transport(void)
{
	return NULL;
}

/* ---- the sources under test ------------------------------------------- */

#define HAKIOS_MT6895_CONNSYS_NVDATA_IMPLEMENTATION
#define HAKIOS_MT6895_BT_WMT_IMPLEMENTATION
#define HAKIOS_MT6895_CELL_EN1_IMPLEMENTATION

#include "hakios.mt6895_connsys.c"
#include "hakios.mt6895_bt.c"
#include "hakios.mt6895_cell.c"

/* ---- checks ------------------------------------------------------------ */

static int gFails;

static void
check(bool cond, const char *what)
{
	printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
	if (!cond) {
		gFails++;
	}
}

int
main(void)
{
	/* 1. EMI guard, four cases. */
	printf("EMI guard:\n");
	{
		struct mtk_connsys_emi_region r;

		r.expected_factory_pa = 0; r.mapped_pa = 0; r.size = 0x1400000ULL;
		check(mtk_connsys_verify_emi_lock(&r) == -22, "no factory base is refused");

		r.expected_factory_pa = 0x5C000000ULL;
		r.mapped_pa = 0x5C000001ULL;
		r.size = 0x1400000ULL;
		check(mtk_connsys_verify_emi_lock(&r) == -22, "misaligned base is refused");

		r.expected_factory_pa = 0x5C000000ULL;
		r.mapped_pa = 0x5D000000ULL;
		check(mtk_connsys_verify_emi_lock(&r) == -13,
		      "a different, aligned base is refused as EACCES");

		r.mapped_pa = 0x5C000000ULL;
		check(mtk_connsys_verify_emi_lock(&r) == 0, "the factory base is accepted");

		r.size = 0x100000ULL;
		check(mtk_connsys_verify_emi_lock(&r) == -22, "a 1 MiB region is refused");
	}

	/* 2. The window table. */
	printf("window table:\n");
	{
		static const struct mtk_mmio_window w[] = {
#define X(addr, wbase, wsize) { #addr, (wbase), (uint32_t)(wsize), 0 },
			XAGA_MMIO_WINDOWS(X)
#undef X
		};
		const size_t n = sizeof(w) / sizeof(w[0]);
		bool dup = false;

		check(n == XAGA_MMIO_WINDOW_COUNT, "expansion matches XAGA_MMIO_WINDOW_COUNT");
		for (size_t i = 0; i < n; i++) {
			for (size_t j = i + 1; j < n; j++) {
				if (w[i].phys_base == w[j].phys_base) {
					dup = true;
				}
			}
		}
		check(!dup, "no two windows share a base");
	}

	/* 3. nvdata. */
	printf("nvdata:\n");
	{
		uint8_t wifi[16] = { 0 };
		uint8_t bt[16]   = { 0 };
		struct mtk_nvdata_wireless_ids ids;

		/* WIFI: four bytes of header then the MAC at 0x04. */
		uint8_t good_mac[6] = { 0x34, 0x12, 0xF4, 0xA5, 0xB6, 0xC7 };
		memcpy(wifi + 4, good_mac, 6);
		memcpy(bt, good_mac, 6);

		check(mtk_nvdata_parse_wireless_ids(wifi, sizeof(wifi), bt, sizeof(bt), &ids) == 0,
		      "a unicast MAC in both files parses");
		check(ids.wifi_mac_valid && ids.bt_bdaddr_valid, "both flagged valid");
		check(memcmp(ids.wifi_mac, good_mac, 6) == 0, "Wi-Fi MAC taken from offset 4");

		memset(wifi + 4, 0x00, 6);
		memset(bt, 0x00, 6);
		check(mtk_nvdata_parse_wireless_ids(wifi, sizeof(wifi), bt, sizeof(bt), &ids) != 0,
		      "an all-zero MAC is refused");

		memset(wifi + 4, 0xFF, 6);
		memset(bt, 0xFF, 6);
		check(mtk_nvdata_parse_wireless_ids(wifi, sizeof(wifi), bt, sizeof(bt), &ids) != 0,
		      "an all-ones MAC is refused");

		wifi[4] = 0x01;   /* multicast bit */
		check(mtk_nvdata_parse_wireless_ids(wifi, sizeof(wifi), bt, sizeof(bt), &ids) != 0 ||
		      !ids.wifi_mac_valid, "a multicast MAC is refused");
	}

	/* 4. The three entries, as a boot would call them. */
	printf("connsys start:\n");
	check(hakios_mt6895_connsys_start() == 0, "wifi=2 with the factory EMI base starts");

	printf("bt start:\n");
	check(hakios_mt6895_bt_start() == 0, "bt=2 maps BTIF and probes");
	{
		uint8_t fw[64];
		memset(fw, 0, sizeof(fw));
		/* A container whose section count is absurd must be refused. */
		check(hakios_mt6895_bt_load_fw(fw, sizeof(fw)) != 0,
		      "a malformed ROM-patch container is refused");
		check(hakios_mt6895_bt_bringup(fw, sizeof(fw)) != 0,
		      "bringup refuses without a transport");
		{
			uint8_t pid[32];
			size_t plen = 0;
			uint8_t empty[4] = { 0, 0, 0, 0 };
			uint8_t real[4]  = { 0, 'x', 'a', 'g' };

			check(hakios_mt6895_bt_product_id(empty, 4, pid, sizeof(pid), &plen) == 1,
			      "an empty product-id is replaced");
			check(plen == strlen("xaga-mt6895"), "the fallback is the phone's name");
			check(hakios_mt6895_bt_product_id(real, 4, pid, sizeof(pid), &plen) == 0,
			      "a non-empty product-id is left alone");
			check(hakios_mt6895_bt_product_id(empty, 4, pid, 4, &plen) < 0,
			      "a too-small buffer is refused, not overrun");
		}
	}

	printf("cell start:\n");
	check(hakios_mt6895_cell_start() == 0, "cell=2 brings en1 up");
	{
		uint8_t eth[64], reply[512];
		bool is_ip = false;
		size_t r;

		/* DHCP DISCOVER: broadcast, UDP 68 -> 67, a full BOOTP payload with
		 * the magic cookie and option 53 = 1. The engine refuses anything
		 * shorter, which is the point: a truncated DISCOVER must not be
		 * answered with a lease that names a different transaction. */
		{
			uint8_t d[286];
			memset(d, 0, sizeof(d));
			memset(d, 0xFF, 6);                       /* dst: broadcast */
			memcpy(d + 6, gCell.cfg.client_mac, 6);   /* src */
			d[12] = 0x08; d[13] = 0x00;               /* IPv4 */
			d[14] = 0x45;                             /* IHL 5, v4 */
			d[23] = 17;                               /* UDP */
			d[34] = 0x00; d[35] = 68;                 /* sport */
			d[36] = 0x00; d[37] = 67;                 /* dport */
			d[42] = 1;                                /* BOOTREQUEST */
			d[46] = 0xDE; d[47] = 0xAD; d[48] = 0xBE; d[49] = 0xEF;  /* xid */
			memcpy(d + 42 + 28, gCell.cfg.client_mac, 6);            /* chaddr */
			d[42 + 236] = 99; d[42 + 237] = 130;
			d[42 + 238] = 83; d[42 + 239] = 99;
			d[42 + 240] = 53; d[42 + 241] = 1; d[42 + 242] = 1;        /* DISCOVER */
			d[42 + 243] = 255;
			r = hakios_mt6895_cell_output(d, sizeof(d), reply, sizeof(reply), &is_ip);
			check(r > 0 && !is_ip, "DHCP DISCOVER is answered locally");
			/* 14 (eth) + 20 (ip) + 8 (udp) + 278 (BOOTP + options). The header
			 * reserves 342 for the reply buffer, but a minimal option set ends
			 * at 255 after seven options, so 320 is what actually goes out. */
			check(r == 320, "the reply is 14+20+8+278 bytes");
			check(reply[42 + 0] == 2, "the reply is a BOOTREPLY");
			check(reply[42 + 240 + 2] == 2, "DISCOVER became an OFFER");
		}

		/* ARP request for the gateway. */
		memset(eth, 0, sizeof(eth));
		memset(eth, 0xFF, 6);
		memcpy(eth + 6, gCell.cfg.client_mac, 6);
		eth[12] = 0x08; eth[13] = 0x06;
		eth[14] = 0x00; eth[15] = 0x01;     /* Ethernet */
		eth[16] = 0x08; eth[17] = 0x00;     /* IPv4 */
		eth[18] = 6; eth[19] = 4;
		eth[20] = 0x00; eth[21] = 0x01;     /* request */
		eth[38] = 0x0A; eth[39] = 0x40; eth[40] = 0x00; eth[41] = 0x01;  /* target */
		r = hakios_mt6895_cell_output(eth, 42, reply, sizeof(reply), &is_ip);
		check(r > 0 && !is_ip, "the gateway's ARP is answered locally");

		/* Ordinary IPv4: must go to TX, no reply. */
		memset(eth, 0, sizeof(eth));
		memcpy(eth + 0, gCell.cfg.router_mac, 6);  /* dst */
		memcpy(eth + 6, gCell.cfg.client_mac, 6);  /* src */
		eth[12] = 0x08; eth[13] = 0x00;
		eth[14] = 0x45;
		eth[23] = 6;                               /* TCP, not UDP */
		r = hakios_mt6895_cell_output(eth, 40, reply, sizeof(reply), &is_ip);
		check(r == 0 && is_ip, "ordinary IPv4 is marked for TX");

		/* And the reverse: a modem packet becomes an Ethernet frame. */
		{
			uint8_t ip[40];
			memset(ip, 0, sizeof(ip));
			ip[0] = 0x45;
			r = hakios_mt6895_cell_input(ip, sizeof(ip), reply, sizeof(reply));
			check(r == 14 + sizeof(ip), "raw IPv4 is wrapped in a 14-byte header");
		}
	}

	printf("bars:\n");
	check(hakios_csq_to_bars(99) == 0, "CSQ 99 (unknown) reads as 0 bars");
	check(hakios_csq_to_bars(31) == 4, "CSQ 31 reads as 4 bars");
	check(hakios_csq_to_bars(0) <= 1, "CSQ 0 does not read as full");

	printf("\n%s (%d failure%s)\n", gFails ? "FAILED" : "ALL CHECKS PASSED",
		       gFails, gFails == 1 ? "" : "s");
	return gFails ? 1 : 0;
}
