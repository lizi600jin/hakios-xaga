/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MT6368 accdet on xaga (MT6895), SPMI slave 5.
 *
 * headset=1: dump 129 registers, with no PMIC writes.
 * headset=2: initialise and poll the physical jack. No PCM output or ADC key decoding yet.
 *
 * Register definitions and the EINT0 / mode 4 / no-moisture sequence are from
 * XagaForge/android_kernel_xiaomi_mt6895, commit 0a8d13c1a180fed82803f56337a0b065ff704fb4,
 * sound/soc/codecs/mt6368-accdet.c and mt6368.h (MediaTek, 2021).
 * Parameters are from this phone's live DT, second_pmic@5/accdet.
 *
 * Init and poll run only in the keys worker, between its complete PMIF transactions.
 * Quiesce runs only after loader_return() stops the other cores. There is no second bus worker.
 * Only PMIF SWINF_2 at 0x0880..0x08a8 is accessed; no arbiter/clock/gate programming.
 */
#include <stdbool.h>
#include <stdint.h>
#include <kern/clock.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.audio.h>
#include <sys/systm.h>

/* The one PMIF transaction path, shared with the keys and the GPU (<sys/hakios.pmif.h>). */
#include <sys/hakios.pmif.h>

/* The layout, the command word and the poll budget are <sys/hakios.pmif.h>'s; this file keeps
 * only which SID and which registers the accdet is. */
#define MT6368_SID          PMIF_SID_MT6368
#define ACCDET_RG_LO        0x2688
#define ACCDET_RG_HI        0x26d3
#define AUDDEC_ANA_LO       0x2508
#define AUDDEC_ANA_HI       0x2537

/* Byte addresses and masks resolved from the vendor's mt6368.h. */
#define AUD_TOP_RESET       0x2319 /* RG_ACCDET_RST, bit 1 */
#define AUD_IRQ_STATUS      0x2323 /* W1C: ACCDET bit 1, EINT0 bit 2 */
#define NCP_PULLDOWN        0x248c /* RG_NCP_PDDIS_EN, bit 0 */
#define MICBIAS1            0x2529 /* LOWPEN bit 2, VREF bits 6:4 */
#define MICBIAS1_HIGH       0x252a /* HVEN bit 4, HVVREF bit 5 */
#define MIC_MODE            0x252d
#define EINT_CONFIG         0x252e /* CONFIGACCDET bit 3, HIRENB bit 4 */
#define EINT_VTH            0x252f /* RG_EINTCOMPVTH, bits 7:4 */
#define ANALOG_FAST_DISCH   0x2530 /* bits 5:4 */
#define EINT_ENABLE         0x2531 /* EINT0EN bit 2 */
#define ACCDET_SPARE_LO     0x2533
#define ACCDET_SPARE_HI     0x2534
#define ACCDET_CTRL         0x268a /* SW_EN bit 0, SEQ_INIT bit 1, EINT0 inverter bit 6 */
#define ACCDET_EINT_CTRL    0x268b /* EINT0_M_SW_EN bit 2, moisture enable bit 4 */
#define ACCDET_PWM_EN       0x268c
#define ACCDET_IRQ          0x26a9 /* ACCDET bit 0; EINT0 bit 2 */
#define ACCDET_IRQ_CLEAR    0x26aa /* level clear bits 0/2; M_PLUG_IN_NUM bit 4 */
#define ACCDET_DA_STABLE    0x26ab
#define ACCDET_HW_MODE      0x26ae
#define ACCDET_AB           0x26b6 /* MEM_IN bits 7:6 */
#define ACCDET_FSM          0x26b7 /* STATE bits 2:0 */
#define ACCDET_EINT0_AB     0x26b8 /* EINT0_MEM_IN bits 7:6, separate from ACCDET AB */
#define VUSB_HW_OP          0x1bc8
#define IRQ_ACCDET         0x01u
#define IRQ_EINT0          0x04u

enum cable_type { CABLE_NONE, CABLE_MIC, CABLE_NO_MIC };
enum detector_state { PLUG_OUT, MIC_BIAS, HOOK_SWITCH };

static uint32_t mode, poll_elapsed;
static enum cable_type cable;
static enum detector_state detector;
static bool active, eint_sync, button_down, first_sample, micbias_disabled;
static uint32_t inserted_ms;
static uint8_t last_ab = 0xff, last_eint = 0xff, last_fsm = 0xff;

/* Only stable configuration bits are saved. IRQ status/clear operations are never replayed. */
static struct {
	uint16_t addr;
	uint8_t original, mask;
} saved[48];
static unsigned saved_count;
static bool quiesced;

/*
 * The accdet's two accessors, on the shared path. The transaction is pmif_read8/pmif_write8's:
 * the IDLE wait, the WFVLDCLR wait, the RDATA read before the ACK, the ACK, the second IDLE
 * wait, the NAK/ERR/timeout distinction, the deadline and the lock. What is this file's is
 * `mode` -- nothing is written until headset=2 has asked for initialisation -- and the log.
 *
 * The status is kept rather than flattened to a bool: the four ways a write can fail (BUSY,
 * NAK, TIMEOUT, ACK) need different things done about them, and the first of each is worth a
 * line in the log.
 */
static pmif_status_t mt6368_last_status;

static bool
mt6368_read(uint16_t addr, uint8_t *value)
{
	if (__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) {
		return false;
	}

	pmif_status_t status = pmif_read8(MT6368_SID, addr, value);

	if (status != PMIF_OK && status != mt6368_last_status) {
		printf("headset: read %#04x: %s\n", addr, pmif_status_name(status));
	}
	mt6368_last_status = status;
	return status == PMIF_OK;
}

static bool
mt6368_write(uint16_t addr, uint8_t value)
{
	if (__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) {
		return false;
	}

	pmif_status_t status;

	if (mode != 2) {
		return false;
	}
	status = pmif_write8(MT6368_SID, addr, value);
	if (status != PMIF_OK && status != mt6368_last_status) {
		printf("headset: write %#04x = %#02x: %s\n", addr, value, pmif_status_name(status));
	}
	mt6368_last_status = status;
	return status == PMIF_OK;
}

static bool
update_bits(uint16_t addr, uint8_t mask, uint8_t bits, bool remember)
{
	uint8_t old, got;
	unsigned slot;

	/* AUD_IRQ_STATUS is W1C, and ACCDET_IRQ_CLEAR's bits 0/2 are level handshakes. */
	if (mode != 2 || addr == 0x014b || addr == 0x0240 ||
	    (bits & (uint8_t)~mask) || addr == AUD_IRQ_STATUS ||
	    (addr == ACCDET_IRQ_CLEAR && (mask & (IRQ_ACCDET | IRQ_EINT0)))) {
		return false;
	}
	if (!mt6368_read(addr, &old)) {
		return false;
	}
	if (remember) {
		for (slot = 0; slot < saved_count && saved[slot].addr != addr; slot++) {}
		if (slot == saved_count) {
			if (saved_count == sizeof(saved) / sizeof(saved[0])) {
				return false;
			}
			saved[slot].addr = addr;
			saved[slot].original = old;
			saved[slot].mask = 0;
			saved_count++;
		}
		/* A later operation can own more bits of this byte; capture them before changing them. */
		uint8_t added = mask & (uint8_t)~saved[slot].mask;
		saved[slot].original = (saved[slot].original & (uint8_t)~added) | (old & added);
		saved[slot].mask |= mask;
	}
	uint8_t value = (old & (uint8_t)~mask) | bits;
	if ((value != old && !mt6368_write(addr, value)) || !mt6368_read(addr, &got)) {
		return false;
	}
	if ((got & mask) != bits) {
		printf("headset: verify failed at 0x%04x mask 0x%02x wanted 0x%02x got 0x%02x\n",
		    addr, mask, bits, got);
		return false;
	}
	return true;
}

static bool
configure(uint16_t addr, uint8_t mask, uint8_t bits)
{
	return update_bits(addr, mask, bits, true);
}

static bool
write16(uint16_t lo, uint16_t value)
{
	return configure(lo, 0xff, (uint8_t)value) &&
	       configure(lo + 1, 0xff, (uint8_t)(value >> 8));
}

/* accdet_init(): pulse SEQ_INIT, then this phone's debounce values. */
static bool
accdet_init(void)
{
	if (!configure(ACCDET_CTRL, 0x02, 0x02)) {
		return false;
	}
	delay(2000);
	if (!configure(ACCDET_CTRL, 0x02, 0)) {
		return false;
	}
	delay(1000);
	return write16(0x2697, 0x800) && write16(0x2699, 0x800) &&
	       write16(0x269d, 0x20) && write16(0x269f, 0x44) &&
	       configure(0x26a3, 0xf0, 0x30) && configure(0x26a5, 0xff, 0x0e);
}

/* mt6368_accdet_late_init(): init, debounce, then init_once, specialised to xaga's DT. */
static bool
accdet_init_once(void)
{
	return accdet_init() &&
	       configure(0x26a3, 0x0f, 0x05) && configure(0x26a4, 0xf0, 0x50) &&
	       configure(AUD_TOP_RESET, 0x02, 0x02) && configure(AUD_TOP_RESET, 0x02, 0) &&
	       configure(MICBIAS1_HIGH, 0x30, 0) && configure(MICBIAS1, 0x70, 0) &&
	       write16(0x268e, 0x4ff) && write16(0x2690, 0x4ff) &&
	       configure(0x2692, 0xff, 0xf0) && configure(0x2693, 0xff, 0x81) &&
	       configure(MICBIAS1, 0x04, 0x04) &&
	       /* mic_vol=8: HVEN and the adjacent HVVREF, not just the one-bit HVEN mask. */
	       configure(MICBIAS1_HIGH, 0x30, 0x30) &&
	       configure(MIC_MODE, 0x8f, 0x8f) && configure(ANALOG_FAST_DISCH, 0x30, 0x30) &&
	       configure(EINT_ENABLE, 0x04, 0x04) && configure(ACCDET_SPARE_HI, 0x01, 0x01) &&
	       configure(NCP_PULLDOWN, 0x01, 0x01) && configure(ACCDET_SPARE_LO, 0x03, 0x03) &&
	       configure(EINT_VTH, 0xf0, 0x20) && configure(0x2694, 0xff, 0x41) &&
	       configure(ACCDET_DA_STABLE, 0xff, 0x27) &&
	       configure(ACCDET_IRQ_CLEAR, 0x10, 0x10) &&
	       configure(ACCDET_EINT_CTRL, 0x10, 0x10) && configure(ACCDET_HW_MODE, 0xff, 0x01) &&
	       configure(ACCDET_EINT_CTRL, 0x10, 0) && configure(ACCDET_PWM_EN, 0xff, 0x67) &&
	       configure(ACCDET_CTRL, 0x40, 0x40);
}

/* IRQ_CLR is held high until IRQ drops, then lowered; the top-level status is W1C. */
static bool
clear_irq(uint8_t mask)
{
	uint8_t value, irq;
	bool cleared = false;

	if (!mt6368_read(ACCDET_IRQ_CLEAR, &value) ||
	    !mt6368_write(ACCDET_IRQ_CLEAR, value | mask)) {
		return false;
	}
	for (unsigned tries = 0; tries < 16; tries++) {
		if (!mt6368_read(ACCDET_IRQ, &irq)) {
			break;
		}
		if (!(irq & mask)) {
			cleared = true;
			break;
		}
		delay(25);
	}
	/* Release even on timeout. Never replay a stale read of unrelated clear bits. */
	if (!mt6368_read(ACCDET_IRQ_CLEAR, &value) ||
	    !mt6368_write(ACCDET_IRQ_CLEAR, value & (uint8_t)~mask)) {
		return false;
	}
	if (!cleared) {
		printf("headset: IRQ clear timeout, mask 0x%02x\n", mask);
		return false;
	}
	return mt6368_write(AUD_IRQ_STATUS, mask == IRQ_ACCDET ? 0x02 : 0x04);
}

/* Worker-only rollback. Never called by the post-stop loader return path. */
static void
restore_configuration(void)
{
	bool ok = true;
	pmif_status_t status = pmif_recover();

	if (status != PMIF_OK) {
		printf("headset: PMIF not idle (%s); configuration restore could not start\n",
		    pmif_status_name(status));
		active = false;
		return;
	}
	for (unsigned i = saved_count; i > 0; i--) {
		if (!update_bits(saved[i - 1].addr, saved[i - 1].mask,
		    saved[i - 1].original & saved[i - 1].mask, false)) {
			ok = false;
			printf("headset: restore failed at 0x%04x\n", saved[i - 1].addr);
			break;
		}
	}
	printf("headset: restored %u configuration bytes: %s\n", saved_count, ok ? "ok" : "incomplete");
	active = false;
	eint_sync = false;
	if (ok) {
		saved_count = 0;
	}
}

/* EINT is the insertion gate. ACCDET AB alone is not a physical plug-out event. */
static bool
handle_eint(bool plugged, uint8_t eint)
{
	if (eint == 0) {
		if (!configure(ACCDET_CTRL, 0x40, 0) ||
		    !configure(ACCDET_DA_STABLE, 0x20, 0) ||
		    !configure(ACCDET_SPARE_HI, 0x01, 0) ||
		    !configure(EINT_CONFIG, 0x08, 0x08)) {
			return false;
		}
	} else if (!configure(0x26a3, 0x0f, 0x05)) {
		return false;
	}
	if (!clear_irq(IRQ_EINT0)) {
		return false;
	}
	if (plugged == eint_sync) {
		return true;
	}
	if (plugged) {
		/* The vendor's EINT callback temporarily disables VUSB LP while classifying. */
		if (!configure(VUSB_HW_OP, 0xff, 0x80) || !accdet_init() ||
		    !configure(ACCDET_CTRL, 0x01, 0x01)) {
			return false;
		}
		eint_sync = true;
		inserted_ms = 0;
		micbias_disabled = false;
		detector = PLUG_OUT;
		printf("headset: EINT0 plug in; awaiting ACCDET classification\n");
	} else {
		eint_sync = false;
		if (!configure(ACCDET_CTRL, 0x01, 0) || !clear_irq(IRQ_ACCDET) ||
		    !write16(0x2697, 0x800) || !write16(0x2699, 0x800) || !write16(0x269d, 0x20)) {
			return false;
		}
		/* recover_eint_setting() is gated by the hardware's M_PLUG_OUT value. */
		if (eint == 3 && (!configure(ACCDET_SPARE_HI, 0x01, 0x01) ||
		    !configure(EINT_CONFIG, 0x18, 0) || !configure(ACCDET_EINT_CTRL, 0x04, 0) ||
		    !configure(ACCDET_DA_STABLE, 0x20, 0x20) || !configure(ACCDET_CTRL, 0x40, 0x40))) {
			return false;
		}
		cable = CABLE_NONE;
		detector = PLUG_OUT;
		button_down = false;
		printf("headset: EINT0 plug out; no device\n");
	}
	return true;
}

/* check_cable_type(), retaining its previous state and eint_sync_flag gate. */
static bool
observe_ab(uint8_t ab)
{
	enum cable_type before = cable;
	bool pressed = button_down;

	if (!eint_sync) {
		return true;
	}
	switch (detector) {
	case PLUG_OUT:
		if (ab == 0) {
			cable = CABLE_NO_MIC;
			detector = HOOK_SWITCH;
			if (!configure(0x26a4, 0xf0, 0x50)) {
				return false;
			}
		} else if (ab == 1) {
			cable = CABLE_MIC;
			detector = MIC_BIAS;
		} else if (ab == 3) {
			cable = CABLE_NONE;
		}
		break;
	case MIC_BIAS:
		if (ab == 0) {
			detector = HOOK_SWITCH;
			button_down = true; /* retain CABLE_MIC: AB00 here is a button press */
		} else if (ab == 3) {
			detector = PLUG_OUT; /* EINT reports removal, rather than this transient AB */
		}
		break;
	case HOOK_SWITCH:
		if (ab == 1) {
			detector = MIC_BIAS;
			cable = CABLE_MIC;
			button_down = false;
		} else if (ab == 3) {
			detector = PLUG_OUT;
		}
		break;
	}
	if (ab == 1 && detector == MIC_BIAS) {
		if (!write16(0x2697, 0x400) || !write16(0x2699, 0x800) ||
		    !configure(0x26a4, 0xf0, 0x10)) {
			return false;
		}
	}
	if (cable != before) {
		printf("headset: cable %s (AB %u, detector %u)\n",
		    cable == CABLE_MIC ? "headset with mic" : cable == CABLE_NO_MIC ? "headphone without mic" :
		    "none", ab, detector);
	}
	if (button_down != pressed) {
		printf("headset: microphone button %s; ADC key identity unavailable\n",
		    button_down ? "pressed" : "released");
	}
	return cable == CABLE_NONE || configure(VUSB_HW_OP, 0xff, 0x85);
}

static bool
poll_sample(void)
{
	uint8_t irq, mem, ein, fsm;

	if (!mt6368_read(ACCDET_IRQ, &irq) || !mt6368_read(ACCDET_AB, &mem) ||
	    !mt6368_read(ACCDET_EINT0_AB, &ein) || !mt6368_read(ACCDET_FSM, &fsm)) {
		return false;
	}
	uint8_t ab = (mem >> 6) & 3u, eint = (ein >> 6) & 3u;
	fsm &= 7u;
	if (first_sample || ab != last_ab || eint != last_eint || fsm != last_fsm) {
		printf("headset: sample IRQ 0x%02x AB %u EINT0 %u FSM %u sync %u\n",
		    irq, ab, eint, fsm, eint_sync);
		last_ab = ab;
		last_eint = eint;
		last_fsm = fsm;
	}
	bool startup_plug = first_sample && eint == 0;
	first_sample = false;
	if ((irq & IRQ_EINT0) || startup_plug) {
		if (eint != 0 && eint != 3) {
			printf("headset: unexpected EINT0 state %u for no-moisture profile\n", eint);
			return false;
		}
		/* pmic_eint_queue_work(): an IRQ while already plugged means removal; the
		 * latched EINT memory alone is not the complete state machine. */
		bool plugged = startup_plug || (!eint_sync && eint != 3);
		return handle_eint(plugged, eint);
	}
	if (irq & IRQ_ACCDET) {
		if (!clear_irq(IRQ_ACCDET) || !observe_ab(ab)) {
			return false;
		}
	}
	/* Vendor's six-second mic-bias timer; four-pole headsets keep the detector enabled. */
	if (eint_sync && !micbias_disabled && inserted_ms >= 6000 &&
	    (cable == CABLE_NO_MIC || ab == 0 || ab == 3)) {
		if (!configure(ACCDET_CTRL, 0x01, 0) || !clear_irq(IRQ_ACCDET) ||
		    !write16(0x2697, 0x800) || !write16(0x2699, 0x800) || !write16(0x269d, 0x20)) {
			return false;
		}
		micbias_disabled = true;
		printf("headset: six-second timer disabled mic bias\n");
	}
	return true;
}

static bool
dump_block(uint16_t lo, uint16_t hi)
{
	for (uint16_t addr = lo; addr <= hi; addr++) {
		uint8_t value;

		if (!mt6368_read(addr, &value)) {
			printf("headset: read timeout at 0x%04x; dump stopped\n", addr);
			return false;
		}
		printf("headset: 0x%04x = 0x%02x\n", addr, value);
	}
	return true;
}

void
hakios_mt6895_headset_init(void)
{
	if (!PE_parse_boot_argn("headset", &mode, sizeof(mode)) || mode == 0) {
		return;
	}
	if (mode != 1 && mode != 2) {
		printf("headset: unsupported mode %u\n", mode);
		return;
	}
	{
		pmif_status_t mapped = pmif_init();

		if (mapped != PMIF_OK) {
			printf("headset: cannot map PMIF (%s)\n", pmif_status_name(mapped));
			return;
		}
	}
	printf("headset: mode %u on SID 5, serialised in keys worker\n", mode);
	/* IDs, two supply/clock preconditions and the nonzero bus anchor. Read only. */
	static const uint16_t baseline_extra[] = { 0x0008, 0x0009, 0x1cb2, 0x230c, 0x231d };
	for (unsigned i = 0; i < sizeof(baseline_extra) / sizeof(baseline_extra[0]); i++) {
		if (!dump_block(baseline_extra[i], baseline_extra[i])) {
			return;
		}
	}
	if (mode == 1) {
		printf("headset: mode 1 is read-only; AB is not detection evidence unless VAUD18 is enabled\n");
	}
	if (!dump_block(ACCDET_RG_LO, ACCDET_RG_HI)) {
		return;
	}
	if (!dump_block(AUDDEC_ANA_LO, AUDDEC_ANA_HI) || mode == 1) {
		return;
	}
	active = true;
	first_sample = true;
	if (!accdet_init_once() || !poll_sample()) {
		printf("headset: initialisation failed; restoring configuration\n");
		restore_configuration();
		return;
	}
	printf("headset: mode 2 initialised, polling every 50 ms; PCM playback not implemented\n");
}

void
hakios_mt6895_headset_poll(uint32_t elapsed_ms)
{
	if (__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE) || !active) {
		return;
	}
	poll_elapsed += elapsed_ms;
	if (poll_elapsed < 50) {
		return;
	}
	if (eint_sync && inserted_ms < 6000) {
		inserted_ms += poll_elapsed;
	}
	poll_elapsed = 0;
	if (!poll_sample()) {
		printf("headset: polling failed; restoring configuration and stopping\n");
		restore_configuration();
	}
}

/*
 * Post-stop path: no lock, PMIF, ACK, restore or printf. A CPU may still be
 * running after a stop timeout; an unlocked ACK could consume its valid word.
 * saved_count is deliberately retained: configuration was NOT restored here.
 */
void
hakios_mt6895_headset_quiesce(void)
{
	__atomic_store_n(&quiesced, true, __ATOMIC_RELEASE);
}
