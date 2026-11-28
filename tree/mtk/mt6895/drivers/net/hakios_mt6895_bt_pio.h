/*
 * hakios_mt6895_bt_pio.h -- BTIF PIO transport for the MT6895 Bluetooth half.
 *
 * Why this file exists: upstream btmtkuart.c never touches BTIF registers
 * (grep for readl/writel in it returns zero hits -- it hands bytes to a serial
 * port), so nothing upstream tells us what BTIF looks like. The register map
 * below comes from MT6895-Mainline/linux @ 7.3-mt6895-xiaomi-xaga,
 * drivers/misc/mediatek/btif/common/plat_inc/btif_priv.h and btif_dma_priv.h,
 * and the base 0x1100C000 is corroborated by both Cyber-Mobile/xaga-qemu
 * (hw/arm/mt6895.c:14391) and the shipped xaga-live.dts.
 *
 * Two rules lifted straight from btif_plat.c, which this implementation follows
 * because they are functional rather than cosmetic:
 *
 *   1. Every write is followed by a read-back (btif_reg_sync_writel). BTIF sits
 *      behind a bus bridge; a write that is not read back may still be in flight
 *      when the next register access lands.
 *   2. 0x08 is shared by IIR (read) and FIFOCTRL (write). Any modification must
 *      be read-modify-write with SET_BIT/CLR_BIT semantics, never a plain
 *      assignment, or the interrupt-pending state is silently cleared.
 *
 * Everything here goes through a two-function bus so the same code runs against
 * real ml_io_map'd registers on the phone and against a modelled register array
 * in the host test.
 */
#ifndef HAKIOS_MT6895_BT_PIO_H
#define HAKIOS_MT6895_BT_PIO_H

#include <stdint.h>
#include <stddef.h>

/* ---- register offsets, relative to BTIF base ----------------------------- */
#define XAGA_BTIF_RBR          0x00u   /* receive buffer / read-only  */
#define XAGA_BTIF_THR          0x00u   /* transmit holding / write-only, same address */
#define XAGA_BTIF_IER          0x04u
#define XAGA_BTIF_IIR          0x08u   /* read  side of the shared 0x08 */
#define XAGA_BTIF_FIFOCTRL     0x08u   /* write side of the shared 0x08 */
#define XAGA_BTIF_FAKELCR      0x0Cu
#define XAGA_BTIF_LSR          0x14u
#define XAGA_BTIF_SLEEP_EN     0x48u
#define XAGA_BTIF_DMA_EN       0x4Cu
#define XAGA_BTIF_RTOCNT       0x54u
#define XAGA_BTIF_TRI_LVL      0x60u
#define XAGA_BTIF_WAK          0x64u
#define XAGA_BTIF_WAT_TIME     0x68u
#define XAGA_BTIF_HANDSHAKE    0x6Cu
#define XAGA_BTIF_TX_DMA       0x880u  /* BTIF_TX_DMA_OFFSET */
#define XAGA_BTIF_RX_DMA       0x900u  /* BTIF_RX_DMA_OFFSET */

/* ---- IER ---------------------------------------------------------------- */
#define XAGA_BTIF_IER_TXEEN    (1u << 1)
#define XAGA_BTIF_IER_RXFEN    (1u << 0)

/* ---- IIR (read of 0x08) ------------------------------------------------- */
#define XAGA_BTIF_IIR_NINT     (1u << 0)
#define XAGA_BTIF_IIR_TX_EMPTY (1u << 1)
#define XAGA_BTIF_IIR_RX       (1u << 2)
#define XAGA_BTIF_IIR_RX_TO    (0x11u << 2)

/* ---- LSR ---------------------------------------------------------------- */
#define XAGA_BTIF_LSR_DR       (1u << 0)   /* data ready      */
#define XAGA_BTIF_LSR_THRE     (1u << 5)   /* THR empty       */
#define XAGA_BTIF_LSR_TEMT     (1u << 6)   /* transmitter idle */

/* ---- FIFOCTRL (write of 0x08) ------------------------------------------- */
#define XAGA_BTIF_FIFOCTRL_CLR_TX (1u << 2)
#define XAGA_BTIF_FIFOCTRL_CLR_RX (1u << 1)

/* ---- FAKELCR ------------------------------------------------------------ */
#define XAGA_BTIF_FAKELCR_NORMAL  0x0u

/* ---- SLEEP_EN ------------------------------------------------------------ */
#define XAGA_BTIF_SLEEP_EN_BIT    (1u << 0)

/* ---- DMA_EN ------------------------------------------------------------- */
#define XAGA_BTIF_DMA_EN_RX       (1u << 0)
#define XAGA_BTIF_DMA_EN_TX       (1u << 1)
#define XAGA_BTIF_DMA_EN_AUTORST  (1u << 2)

/* ---- TRI_LVL ------------------------------------------------------------ */
#define XAGA_BTIF_TRI_LVL_TX(x)   (((x) & 0xFu) << 0)
#define XAGA_BTIF_TRI_LVL_RX(x)   (((x) & 0x7u) << 4)
#define XAGA_BTIF_TRI_LOOP_EN     (1u << 7)
#define XAGA_BTIF_TRI_LOOP_DIS    0u

/* ---- WAK / HANDSHAKE ---------------------------------------------------- */
#define XAGA_BTIF_WAK_BIT         (1u << 0)
#define XAGA_BTIF_HANDSHAKE_EN    1u
#define XAGA_BTIF_HANDSHAKE_DIS   0u

/* ---- FIFO geometry ------------------------------------------------------ */
#define XAGA_BTIF_TX_FIFO_SIZE    16u
#define XAGA_BTIF_RX_FIFO_SIZE    8u
#define XAGA_BTIF_TX_FIFO_THRE    8u
#define XAGA_BTIF_RX_FIFO_THRE    1u

/* ---- H4 packet types ---------------------------------------------------- */
#define XAGA_H4_CMD               0x01u
#define XAGA_H4_ACL               0x02u
#define XAGA_H4_SCO               0x03u
#define XAGA_H4_EVT               0x04u
#define XAGA_H4_VENDOR_EVT        0xFFu   /* WMT vendor event as seen on this port */

/* ---- bus abstraction ---------------------------------------------------- */
struct xaga_btif_bus {
    void *ctx;
    uint32_t (*rd32)(void *ctx, uint32_t off);
    void (*wr32)(void *ctx, uint32_t off, uint32_t val);
    /* Optional: default 1 ms granularity sleep. NULL uses a busy spin via rd32. */
    void (*msleep)(void *ctx, uint32_t ms);
};

struct xaga_btif {
    struct xaga_btif_bus bus;
    int  initialised;
    unsigned last_err;
};

/* 0 on success, negative otherwise. */
int  xaga_btif_init(struct xaga_btif *b);
void xaga_btif_wake(struct xaga_btif *b);
int  xaga_btif_clear_fifo(struct xaga_btif *b, int tx, int rx);

/* byte-level PIO; block until the FIFO reports room / data */
int  xaga_btif_tx_byte(struct xaga_btif *b, uint8_t c, uint32_t spins);
int  xaga_btif_rx_byte(struct xaga_btif *b, uint8_t *out, uint32_t spins);
int  xaga_btif_tx(struct xaga_btif *b, const uint8_t *d, size_t n, uint32_t spins);
int  xaga_btif_rx(struct xaga_btif *b, uint8_t *d, size_t n, uint32_t spins);

/* H4 framing: CMD out, EVT in. Both return bytes transferred or negative. */
int  xaga_btif_h4_send_cmd(struct xaga_btif *b, uint16_t opcode,
                           const uint8_t *pl, uint16_t pl_len, uint32_t spins);
int  xaga_btif_h4_recv_evt(struct xaga_btif *b, uint8_t *evt, uint16_t cap,
                           uint32_t spins);

#endif /* HAKIOS_MT6895_BT_PIO_H */

#ifdef XAGA_BTIF_IMPLEMENTATION

/* Read-back after write. This is the btif_reg_sync_writel() contract. */
static void
xaga_btif_wr_sync(struct xaga_btif *b, uint32_t off, uint32_t val)
{
	b->bus.wr32(b->bus.ctx, off, val);
	(void)b->bus.rd32(b->bus.ctx, off);
}

static uint32_t
xaga_btif_rd(struct xaga_btif *b, uint32_t off)
{
	return b->bus.rd32(b->bus.ctx, off);
}

/* 0x08 is IIR when read and FIFOCTRL when written: never blind-write it. */
static int
xaga_btif_fifoctrl_rmw(struct xaga_btif *b, uint32_t set, uint32_t clr)
{
	uint32_t v;

	v = xaga_btif_rd(b, XAGA_BTIF_IIR);
	v = (v & ~clr) | set;
	xaga_btif_wr_sync(b, XAGA_BTIF_FIFOCTRL, v);
	return 0;
}

int
xaga_btif_init(struct xaga_btif *b)
{
	if (b == NULL || b->bus.rd32 == NULL || b->bus.wr32 == NULL) {
		return -1;
	}

	/* 1. leave the fake-LCR test mode; btif_plat.c writes 0 here first */
	xaga_btif_wr_sync(b, XAGA_BTIF_FAKELCR, XAGA_BTIF_FAKELCR_NORMAL);

	/* 2. flush both FIFOs, through the shared-window read-modify-write */
	xaga_btif_fifoctrl_rmw(b, 0, 0);
	(void)xaga_btif_clear_fifo(b, 1, 1);

	/* 3. trigger level: TX 8 of 16, RX 1 of 8, loopback off */
	xaga_btif_wr_sync(b, XAGA_BTIF_TRI_LVL,
	                  XAGA_BTIF_TRI_LVL_TX(XAGA_BTIF_TX_FIFO_THRE) |
	                  XAGA_BTIF_TRI_LVL_RX(XAGA_BTIF_RX_FIFO_THRE) |
	                  XAGA_BTIF_TRI_LOOP_DIS);

	/* 4. handshake on: the BB-side flow control the LK relies on */
	xaga_btif_wr_sync(b, XAGA_BTIF_HANDSHAKE, XAGA_BTIF_HANDSHAKE_EN);

	/* 5. interrupts: RX ready + TX empty, still polled for now */
	xaga_btif_wr_sync(b, XAGA_BTIF_IER,
	                  XAGA_BTIF_IER_RXFEN | XAGA_BTIF_IER_TXEEN);

	/* 6. PIO path: DMA stays off until a ring is wired */
	xaga_btif_wr_sync(b, XAGA_BTIF_DMA_EN,
	                  XAGA_BTIF_DMA_EN_AUTORST);

	b->initialised = 1;
	return 0;
}

void
xaga_btif_wake(struct xaga_btif *b)
{
	if (b == NULL) {
		return;
	}
	/* 0 -> 1 transition asserts wake; writing 1 again is a no-op */
	xaga_btif_wr_sync(b, XAGA_BTIF_WAK, 0);
	xaga_btif_wr_sync(b, XAGA_BTIF_WAK, XAGA_BTIF_WAK_BIT);
}

int
xaga_btif_clear_fifo(struct xaga_btif *b, int tx, int rx)
{
	uint32_t set = 0;

	if (b == NULL) {
		return -1;
	}
	if (tx) {
		set |= XAGA_BTIF_FIFOCTRL_CLR_TX;
	}
	if (rx) {
		set |= XAGA_BTIF_FIFOCTRL_CLR_RX;
	}
	return xaga_btif_fifoctrl_rmw(b, set, 0);
}

int
xaga_btif_tx_byte(struct xaga_btif *b, uint8_t c, uint32_t spins)
{
	uint32_t i;

	if (b == NULL) {
		return -1;
	}

	/* TX holding register empty (LSR bit 5) or transmitter idle (bit 6) */
	for (i = 0; i < spins; i++) {
		uint32_t lsr = xaga_btif_rd(b, XAGA_BTIF_LSR);
		if ((lsr & (XAGA_BTIF_LSR_THRE | XAGA_BTIF_LSR_TEMT)) != 0) {
			/*
			 * THR is write-only. Reading 0x00 back hits RBR instead and
			 * pops one byte out of the RX FIFO, so the read-back rule
			 * stays limited to configuration registers.
			 */
			b->bus.wr32(b->bus.ctx, XAGA_BTIF_THR, (uint32_t)c);
			return 1;
		}
	}
	return -2;   /* FIFO never drained */
}

int
xaga_btif_rx_byte(struct xaga_btif *b, uint8_t *out, uint32_t spins)
{
	uint32_t i;

	if (b == NULL || out == NULL) {
		return -1;
	}

	/* data ready (LSR bit 0) or the IIR RX pending bit */
	for (i = 0; i < spins; i++) {
		uint32_t lsr = xaga_btif_rd(b, XAGA_BTIF_LSR);
		uint32_t iir = xaga_btif_rd(b, XAGA_BTIF_IIR);
		if ((lsr & XAGA_BTIF_LSR_DR) != 0 ||
		    (iir & XAGA_BTIF_IIR_RX) != 0) {
			*out = (uint8_t)(xaga_btif_rd(b, XAGA_BTIF_RBR) & 0xFFu);
			return 1;
		}
	}
	return -2;   /* nothing arrived */
}

int
xaga_btif_tx(struct xaga_btif *b, const uint8_t *d, size_t n, uint32_t spins)
{
	size_t i;
	int rc;

	if (b == NULL || d == NULL) {
		return -1;
	}
	for (i = 0; i < n; i++) {
		rc = xaga_btif_tx_byte(b, d[i], spins);
		if (rc < 0) {
			return (int)i;
		}
	}
	return (int)n;
}

int
xaga_btif_rx(struct xaga_btif *b, uint8_t *d, size_t n, uint32_t spins)
{
	size_t i;
	int rc;

	if (b == NULL || d == NULL) {
		return -1;
	}
	for (i = 0; i < n; i++) {
		rc = xaga_btif_rx_byte(b, &d[i], spins);
		if (rc < 0) {
			return (int)i;
		}
	}
	return (int)n;
}

/*
 * H4 command frame: [0x01][opcode_lo][opcode_hi][len][payload...]
 * The whole frame goes out in one burst so the BB side does not see a partial
 * command -- the LK writes it the same way.
 */
int
xaga_btif_h4_send_cmd(struct xaga_btif *b, uint16_t opcode,
                      const uint8_t *pl, uint16_t pl_len, uint32_t spins)
{
	uint8_t hdr[4];
	int rc;

	if (b == NULL) {
		return -1;
	}
	if (pl_len > 0xFFu) {
		return -1;
	}

	hdr[0] = XAGA_H4_CMD;
	hdr[1] = (uint8_t)(opcode & 0xFFu);
	hdr[2] = (uint8_t)((opcode >> 8) & 0xFFu);
	hdr[3] = (uint8_t)pl_len;

	rc = xaga_btif_tx(b, hdr, sizeof(hdr), spins);
	if (rc < 0) {
		return rc;
	}
	if (rc != (int)sizeof(hdr)) {
		return -3;
	}
	if (pl_len != 0) {
		rc = xaga_btif_tx(b, pl, pl_len, spins);
		if (rc != (int)pl_len) {
			return (rc < 0) ? rc : -3;
		}
	}
	return (int)(sizeof(hdr) + pl_len);
}

/*
 * H4 event frame in: [0x04][evt_code][len][payload...]
 * Vendor events on this port carry 0xFF in evt_code; either way the frame shape
 * is the same, so we return the raw frame including the 0x04 prefix and let
 * mtk_bt_parse_wmt_evt() interpret the payload.
 */
int
xaga_btif_h4_recv_evt(struct xaga_btif *b, uint8_t *evt, uint16_t cap,
                      uint32_t spins)
{
	uint8_t hdr[3];
	uint8_t len;
	int rc;

	if (b == NULL || evt == NULL || cap < 3) {
		return -1;
	}

	rc = xaga_btif_rx(b, hdr, sizeof(hdr), spins);
	if (rc != (int)sizeof(hdr)) {
		return (rc < 0) ? rc : -4;
	}
	if (hdr[0] != XAGA_H4_EVT) {
		b->last_err = hdr[0];
		return -5;   /* not an event: caller should resync */
	}
	len = hdr[2];
	if ((uint16_t)(len + 3) > cap) {
		return -6;
	}

	evt[0] = hdr[0];
	evt[1] = hdr[1];
	evt[2] = hdr[2];
	if (len != 0) {
		rc = xaga_btif_rx(b, &evt[3], len, spins);
		if (rc != (int)len) {
			return (rc < 0) ? rc : -4;
		}
	}
	return (int)(len + 3);
}

#endif /* XAGA_BTIF_IMPLEMENTATION */
