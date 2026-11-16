/* SPDX-License-Identifier: GPL-2.0 */
/*
 * MT6368 accdet on xaga (MT6895), SPMI slave 5.
 *
 * headset=1: dump 129 registers, with no PMIC writes.
 * headset=2: initialise and poll the physical jack. No PCM output or ADC key decoding yet.
 * headset=3: after classification, exercise the normal HP analog sequence and verify it.
 *            Low-power bias, -40 dB gain; no PCM, AFE route or automatic supply takeover.
 *
 * headset=4: codec-local SGEN with vendor HP_MUTE gain31; explicit trim arguments required.
 *            VAUD18 must be on already; electrical silence is not guaranteed.
 * hp_tone=1: after the normal profile4 check, use gain30 for three digitally gated
 *            one-second pulses, then close and restore. Frequency/output are unmeasured.
 * hp_tone_gain=31 selects the more attenuated vendor reference code for this experiment.
 *
 * Register definitions and the EINT0 / mode 4 / no-moisture sequence are from
 * XagaForge/android_kernel_xiaomi_mt6895, commit 0a8d13c1a180fed82803f56337a0b065ff704fb4,
 * sound/soc/codecs/mt6368-accdet.c and mt6368.h (MediaTek, 2021).
 * Parameters are from this phone's live DT, second_pmic@5/accdet.
 *
 * Init and poll run only in the keys worker, between its complete PMIF transactions.
 * Normal return asks that worker to stop modes 3/4 before stopping CPUs. Post-stop quiesce is
 * software-only. There is no second bus worker.
 * Only PMIF SWINF_2 at 0x0880..0x08a8 is accessed; no controller arbiter/clock/gate programming.
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
#define AUDENC_ANA_LO       0x2508
#define AUDENC_ANA_HI       0x2537

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

/* Mode 3 ownership handshake: a stop before worker entry prevents entry altogether. */
#define HP_WORKER_RUNNING   1u
#define HP_STOP_REQUESTED   2u
#define HP_WORKER_DONE      4u
static unsigned hp_worker_state;
static bool hp_owned, hp_on;
#define HP_GAIN_MASK        0x1fu /* RG_AUDHPLGAIN / RG_AUDHPRGAIN, not the legacy packed gain mask */

/* mtk_hp_enable/disable, HP_MUX_HP=2, hp_hifi_mode=0, rate < 96 kHz.
 * ZCD_CON3/4 are the HP gains; CON1/2 (0x2609/a) belong to lineout, not HP.
 * Only these fields are owned. In particular, preserve other outputs' bias fields and
 * AUDGLB_PWRDN (0x25a2 bit 4); decoder reset/clock is bit 0 of the same byte. */
static struct {
	uint16_t addr;
	uint8_t mask, original;
} hp_saved[] = {
	{0x2588, 0xff, 0}, {0x2589, 0xff, 0}, {0x258a, 0xff, 0},
	{0x258b, 0xff, 0}, {0x258c, 0xff, 0}, {0x258d, 0xff, 0},
	{0x2591, 0xff, 0}, {0x259a, 0xff, 0}, {0x259b, 0xff, 0},
	{0x259c, 0xff, 0}, {0x259f, 0x07, 0}, {0x25a1, 0xc3, 0},
	{0x25a2, 0x01, 0}, {0x260b, HP_GAIN_MASK, 0}, {0x260c, HP_GAIN_MASK, 0},
	{0x2537, 0x01, 0},
};

/*
 * The accdet's two accessors, on the shared path. The transaction is pmif_read8/pmif_write8's:
 * the IDLE wait, the WFVLDCLR wait, the RDATA read before the ACK, the ACK, the second IDLE
 * wait, the NAK/ERR/timeout distinction, the deadline and the lock. What is this file's is
 * `mode` -- nothing is written until headset=2/3 has asked for initialisation -- and the log.
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

/*
 * A read that names its own SID, for the MT6685 probe. Same body as mt6368_read() --
 * the accdet's accessors are this driver's only other users, and they are fixed at SID 5.
 * A read needs no gate on `mode`, unlike mt6368_write(): nothing on the bus changes.
 */
static bool
mt6368_read_sid(uint32_t sid, uint16_t addr, uint8_t *value)
{
	if (__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) {
		return false;
	}

	pmif_status_t status = pmif_read8(sid, addr, value);

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

	if (mode != 2 && mode != 3 && mode != 4) {
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
	if ((mode != 2 && mode != 3 && mode != 4) || addr == 0x014b || addr == 0x0240 ||
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

/* The HP bank is captured before its first write, separately from ACCDET's rollback. */
static bool
hp_admit(uint16_t addr, uint8_t mask)
{
	if (mode != 3 || !hp_owned) {
		return false;
	}
	for (unsigned i = 0; i < sizeof(hp_saved) / sizeof(hp_saved[0]); i++) {
		if (hp_saved[i].addr == addr) {
			return (mask & (uint8_t)~hp_saved[i].mask) == 0;
		}
	}
	return false;
}

static bool
hp_update(uint16_t addr, uint8_t mask, uint8_t bits)
{
	return hp_admit(addr, mask) && update_bits(addr, mask, bits, false);
}

/* Preserve regmap_write's explicit writes, including repeated values in the sequence. */
static bool
hp_write(uint16_t addr, uint8_t value)
{
	uint8_t got;

	if (!hp_admit(addr, 0xff) || !mt6368_write(addr, value) || !mt6368_read(addr, &got)) {
		return false;
	}
	if (got != value) {
		printf("headset: HP verify failed at 0x%04x wanted 0x%02x got 0x%02x\n", addr, value, got);
		return false;
	}
	return true;
}

/* Keep HP_MUTE's vendor bus writes, but verify only the documented five-bit gain.
 * Trial 705 reads 0x1f after writing 0x9f: 8-bit regmap does not imply eight stored bits.
 * This explicit exception is confined to the two gain registers and two vendor values;
 * snapshots and masked updates own only HP_GAIN_MASK. No reserved-bit type is assumed. */
static bool
hp_gain_write(uint16_t addr, uint8_t raw)
{
	uint8_t got;

	if ((addr != 0x260b && addr != 0x260c) || (raw != 0x1e && raw != 0x9f) ||
	    !hp_admit(addr, HP_GAIN_MASK) || !mt6368_write(addr, raw) || !mt6368_read(addr, &got)) {
		return false;
	}
	bool ok = (got & HP_GAIN_MASK) == (raw & HP_GAIN_MASK);
	printf("headset: HP gain write reg=0x%04x raw=0x%02x read=0x%02x mask=0x%02x"
	    " expected=0x%02x actual=0x%02x: %s\n", addr, raw, got, HP_GAIN_MASK,
	    raw & HP_GAIN_MASK, got & HP_GAIN_MASK, ok ? "verified" : "FAILED");
	return ok;
}

/* These three bytes share stable configuration fields with other audio functions.
 * Check both the owned target and every unowned bit against the pre-probe snapshot. */
static bool
hp_verify_shared(bool restored)
{
	bool ok = true;

	for (unsigned i = 0; i < sizeof(hp_saved) / sizeof(hp_saved[0]); i++) {
		uint16_t addr = hp_saved[i].addr;
		if (addr != 0x259f && addr != 0x25a1 && addr != 0x25a2) {
			continue;
		}
		uint8_t got, own = hp_saved[i].mask, keep = (uint8_t)~own;
		uint8_t baseline = hp_saved[i].original;
		uint8_t target = restored ? baseline & own : addr == 0x25a1 ? 0 : 1;
		if (!mt6368_read(addr, &got)) {
			return false;
		}
		bool match = (got & own) == target && (got & keep) == (baseline & keep);
		printf("headset: HP shared %s reg=0x%04x baseline=0x%02x read=0x%02x"
		    " own=0x%02x keep=0x%02x own_expected=0x%02x own_read=0x%02x"
		    " keep_expected=0x%02x keep_read=0x%02x: %s\n",
		    restored ? "restored" : "on", addr, baseline, got, own, keep,
		    target, got & own, baseline & keep, got & keep, match ? "verified" : "FAILED");
		ok = match && ok;
	}
	return ok;
}

static bool
hp_pull_down(bool enable)
{
	/* hp_pull_down(): eight steps, 100..150 us after every step. */
	for (unsigned i = 0; i < 8; i++) {
		unsigned stage = enable ? i : 7u - i;
		if (!hp_update(0x258d, 0x70, (uint8_t)(stage << 4))) {
			return false;
		}
		delay(100);
	}
	return true;
}

static bool
hp_main_ramp(bool up)
{
	/* mt_hp_event+0x514: both channels, eight steps, 600..650 us per pair. */
	for (unsigned i = 0; i < 8; i++) {
		unsigned stage = up ? i : 7u - i;
		if (!hp_update(0x258b, 0x07, (uint8_t)stage) ||
		    !hp_update(0x258b, 0x70, (uint8_t)(stage << 4))) {
			return false;
		}
		delay(600);
	}
	return true;
}

static bool
hp_feedback_ramp(bool up)
{
	/* mt_hp_event+0x574: sixteen steps, 600..650 us after every step. */
	for (unsigned i = 0; i < 16; i++) {
		unsigned stage = up ? i : 15u - i;
		if (!hp_update(0x259b, 0xf0, (uint8_t)(stage << 4))) {
			return false;
		}
		delay(600);
	}
	return true;
}

static bool
hp_gain(uint8_t value)
{
	/* This probe ramps only between the vendor's adjacent -22 / -40 dB codes. */
	if ((value != 0x1e && value != 0x1f) ||
	    !hp_update(0x260b, HP_GAIN_MASK, value) || !hp_update(0x260c, HP_GAIN_MASK, value)) {
		return false;
	}
	delay(600);
	return true;
}

static bool
hp_verify(bool on)
{
	uint8_t dac, output, mux, clock, left, right, clksq;

	if (!mt6368_read(0x2588, &dac) || !mt6368_read(0x258a, &output) ||
	    !mt6368_read(0x2589, &mux) || !mt6368_read(0x25a2, &clock) ||
	    !mt6368_read(0x260b, &left) || !mt6368_read(0x260c, &right) ||
	    !mt6368_read(0x2537, &clksq)) {
		return false;
	}
	/*
	 * CLKSQ is this trial's variable: on it must read bit0 = 1 (enable configuration),
	 * off it must be back to bit0 = 0 (mt6368.c:155-160 is a plain 1/0 setter).
	 */
	bool ok = on ? dac == 0xff && output == 0x03 && mux == 0x3a &&
	    (clock & 1u) && (left & HP_GAIN_MASK) == 0x1f && (right & HP_GAIN_MASK) == 0x1f &&
	    (clksq & 0x01) :
	    dac == 0 && output == 0 && !(mux & 0x0f) && !(clock & 1u) && !(clksq & 0x01);
	printf("headset: HP %s readback 2588=0x%02x 258a=0x%02x 2589=0x%02x"
	    " 25a2=0x%02x gain=0x%02x/0x%02x gain_mask=0x%02x"
	    " gain_fields=0x%02x/0x%02x clksq=0x%02x clksq_bit0=%u: %s\n",
	    on ? "on" : "off",
	    dac, output, mux, clock, left, right, HP_GAIN_MASK,
	    left & HP_GAIN_MASK, right & HP_GAIN_MASK, clksq, clksq & 1u,
	    ok ? "verified" : "FAILED");
	return ok;
}

static bool
hp_capture(void)
{
	uint8_t id, vaud, clocks, clksq, lo, hs, bias, rails, ncp, gain_sel;

	if (!mt6368_read(0x0009, &id) || !mt6368_read(0x1cb2, &vaud) ||
	    !mt6368_read(0x230c, &clocks) || !mt6368_read(0x2537, &clksq) ||
	    !mt6368_read(0x2595, &lo) || !mt6368_read(0x2593, &hs) ||
	    !mt6368_read(0x25a0, &bias) || !mt6368_read(0x25a3, &rails) ||
	    !mt6368_read(0x2488, &ncp) || !mt6368_read(0x23d7, &gain_sel)) {
		return false;
	}
	printf("headset: HP preconditions chip=0x%02x VAUD18=0x%02x CKPDN=0x%02x"
	    " CLKSQ=0x%02x IBIAS=0x%02x LDO/NV=0x%02x NCP=0x%02x"
	    " GAIN_SEL=0x%02x LO=0x%02x HS=0x%02x\n",
	    id, vaud, clocks, clksq, bias, rails, ncp, gain_sel, lo, hs);
	/*
	 * MT6685's AUDCLK window, read only. This is the one register mt6685_set_dcxo()
	 * touches (0x79e bit2, mask 0x4); the disassembly is
	 * docs/headset-wip/raw/mt6685-audclk/set-dcxo.disasm.txt. Reading it answers
	 * whether this kernel can reach SID 9 at all before anything is written there.
	 *
	 * This mode-3 probe does not write SID9. Mode4 has its own ownership gate and
	 * masked access to the transport's sole writable SID9 address, 0x079e.
	 */
	{
		uint8_t dcxo;

		if (mt6368_read_sid(PMIF_SID_MT6685, 0x079e, &dcxo)) {
			printf("headset: MT6685 audclk 0x079e = 0x%02x dcxo_bit2=%u (read only)\n",
			    dcxo, (dcxo >> 2) & 1u);
		} else {
			printf("headset: MT6685 audclk 0x079e unreadable (%s)\n",
			    pmif_status_name(mt6368_last_status));
		}
	}
	/* The vendor selects ZCD gain control at probe; NLE would ignore our HP gain codes. */
	if (id != 0x68 || !(vaud & 1u) || (lo & 3u) || (hs & 3u) || (gain_sel & 0x24u)) {
		printf("headset: HP probe refused: wrong chip, VAUD18 off, NLE gain or another analog output active\n");
		return false;
	}
	for (unsigned i = 0; i < sizeof(hp_saved) / sizeof(hp_saved[0]); i++) {
		if (!mt6368_read(hp_saved[i].addr, &hp_saved[i].original)) {
			return false;
		}
		printf("headset: HP baseline 0x%04x = 0x%02x own 0x%02x\n",
		    hp_saved[i].addr, hp_saved[i].original, hp_saved[i].mask);
	}
	/* No takeover of an already-running DAC/HP path; rollback may restore only an off path. */
	if (hp_saved[0].original != 0 || (hp_saved[1].original & 0x0f) ||
	    (hp_saved[2].original & 0x3f)) {
		printf("headset: HP probe refused: DAC, mux or output was already enabled\n");
		return false;
	}
	hp_owned = true;
	return true;
}

static bool
hp_enable(void)
{
	if (!hp_capture()) {
		return false;
	}
	printf("headset: HP normal path, low-power bias, target -40 dB; register probe only\n");
	/* Mode3 retains the 708/709 sequence: HP_PULL_DOWN then CLKSQ enable.
	 * This verifies register configuration, not the physical clock waveform. */
	if (!hp_pull_down(true) || !hp_update(0x2537, 0x01, 0x01) ||
	    !hp_gain_write(0x260b, 0x1e) || !hp_gain_write(0x260c, 0x1e) ||
	    !hp_update(0x259f, 0x07, 0x01) || !hp_update(0x25a1, 0xc0, 0) ||
	    !hp_update(0x25a1, 0x03, 0) || !hp_write(0x259c, 0x87) ||
	    !hp_write(0x2591, 0) || !hp_write(0x258c, 0x33) || !hp_write(0x258d, 0xf1) ||
	    !hp_write(0x258b, 0) || !hp_write(0x258a, 0x0c) || !hp_write(0x258a, 0x3c) ||
	    !hp_write(0x259a, 0) || !hp_write(0x259b, 0x0c) || !hp_write(0x2589, 0x30) ||
	    !hp_write(0x2588, 0xc0) || !hp_write(0x2588, 0xf0) || !hp_write(0x258a, 0xfc) ||
	    /* hp_in_pair_current() is a no-op for hp_hifi_mode=0. */
	    !hp_write(0x259b, 0x0e) || !hp_write(0x259b, 0x02) || !hp_write(0x258a, 0xff) ||
	    !hp_main_ramp(true) || !hp_feedback_ramp(true) ||
	    !hp_write(0x258a, 0xcf) || !hp_write(0x258b, 0x77) || !hp_gain(0x1f) ||
	    !hp_write(0x258a, 0xc3) || !hp_write(0x258a, 0x03)) {
		return false;
	}
	delay(100); /* mt_hp_event+0x604: 100..120 us, before decoder clock. */
	if (!hp_update(0x25a2, 0x01, 0x01) || !hp_write(0x2589, 0x30) ||
	    !hp_write(0x2588, 0xff) || !hp_write(0x259a, 0) || !hp_write(0x259b, 0xf2)) {
		return false;
	}
	delay(100); /* mt_hp_event+0x678: 100..120 us, before selecting DAC inputs. */
	if (!hp_update(0x2589, 0x03, 0x02) || !hp_update(0x2589, 0x0c, 0x08) ||
	    !hp_pull_down(false) || !hp_verify(true) || !hp_verify_shared(false)) {
		return false;
	}
	hp_on = true;
	printf("headset: HP sequence verified; supply/clock liveness and PCM playback are unverified\n");
	return true;
}

static bool
hp_disable_sequence(void)
{
	/* Complete normal-HP mtk_hp_disable(), not just its eleven nonzero immediate writes. */
	bool sequence = hp_pull_down(true) && hp_update(0x2589, 0x0f, 0) &&
	       hp_update(0x259a, 0x01, 0) && hp_update(0x2588, 0x0f, 0) &&
	       hp_update(0x25a2, 0x01, 0) && hp_write(0x258b, 0x77) &&
	       hp_write(0x258a, 0xc3) && hp_write(0x258a, 0xcf) && hp_gain(0x1e) &&
	       hp_write(0x258a, 0xff) && hp_feedback_ramp(false) && hp_main_ramp(false) &&
	       hp_update(0x258a, 0x03, 0) && hp_write(0x259a, 0x01) &&
	       hp_write(0x259b, 0x0e) && hp_write(0x259b, 0x0c) &&
	       hp_update(0x258a, 0xc0, 0) && hp_update(0x2588, 0x30, 0) &&
	       hp_update(0x2588, 0xc0, 0) && hp_write(0x259a, 0x01) &&
	       hp_write(0x259b, 0x02) && hp_update(0x258a, 0x30, 0) &&
	       hp_update(0x258a, 0x0c, 0);
	if (!sequence) {
		return false;
	}
	/* HP_MUTE POST_PMD: try both channels; a gain failure must not hide the off readback. */
	bool left = hp_gain_write(0x260b, 0x9f);
	bool right = hp_gain_write(0x260c, 0x9f);
	/* Mode3 retains the successful 708/709 CLKSQ close. The vendor's widget
	 * owns this bit outside mtk_hp_disable(); this is not a whole-DAPM recipe.
	 * SUPPLY_S provides subseq; the core sorts by widget type before subseq. */
	bool clksq = hp_update(0x2537, 0x01, 0);
	return left && right && clksq;
}

static bool
hp_shutdown(void)
{
	if (!hp_owned) {
		return true;
	}
	/* One recovery/retry is allowed for a dropped write. Never reset or gate PMIF. */
	bool off = false, off_readback = false;
	for (unsigned attempt = 0; attempt < 2 && !off; attempt++) {
		off_readback = false;
		if (pmif_recover() != PMIF_OK) {
			break;
		}
		bool sequence = hp_disable_sequence();
		/* Never short-circuit the diagnostic on a sequence/gain failure. */
		off_readback = hp_verify(false);
		if (off_readback) {
			hp_on = false;
		}
		off = sequence && off_readback;
		printf("headset: HP shutdown attempt %u/2 sequence=%s off_readback=%s: %s\n",
		    attempt + 1, sequence ? "ok" : "FAILED", off_readback ? "ok" : "FAILED",
		    off ? "verified" : "FAILED");
	}
	if (!off) {
		if (off_readback) {
			printf("headset: HP shutdown incomplete; off readback verified, gain/sequence failed\n");
		} else {
			printf("headset: HP shutdown incomplete; analog off was NOT confirmed\n");
		}
		return false;
	}
	hp_on = false;
	/* Only after verified power-down, restore the captured fields of the initially off path. */
	for (unsigned i = sizeof(hp_saved) / sizeof(hp_saved[0]); i > 0; i--) {
		if (!hp_update(hp_saved[i - 1].addr, hp_saved[i - 1].mask,
		    hp_saved[i - 1].original & hp_saved[i - 1].mask)) {
			printf("headset: HP restore incomplete at 0x%04x\n", hp_saved[i - 1].addr);
			return false;
		}
	}
	if (!hp_verify_shared(true)) {
		printf("headset: HP restore incomplete; shared configuration differs from baseline\n");
		return false;
	}
	hp_owned = false;
	printf("headset: HP off and original configuration restored\n");
	return true;
}


/* Fixed vendor profile; dynamic trim is supplied by the caller, not this reference. */
enum p4_op { P4_UPDATE, P4_WRITE, P4_DELAY, P4_CLOCK, P4_VAUD,
    P4_TRIM_L, P4_FINE_L, P4_TRIM_R, P4_FINE_R };
struct p4_step { uint16_t reg, arg; uint8_t op, mask, hp; };
struct p4_register { uint16_t reg; uint8_t sid, own, defined, on, off, original; };
static struct p4_register p4_registers[] = {
    {0x230c, 5, 0x66, 0xe7, 0x00, 0x66, 0},
    {0x2388, 5, 0x01, 0xc1, 0x01, 0x00, 0},
    {0x2389, 5, 0x01, 0x01, 0x01, 0x00, 0},
    {0x238e, 5, 0x01, 0x1f, 0x01, 0x01, 0},
    {0x238f, 5, 0xc7, 0xff, 0x00, 0xc7, 0},
    {0x2392, 5, 0xff, 0xff, 0xa1, 0xa0, 0},
    {0x2393, 5, 0xff, 0xff, 0xcb, 0xcb, 0},
    {0x2396, 5, 0xdf, 0xdf, 0x0b, 0x00, 0},
    {0x23a2, 5, 0xff, 0xff, 0xa1, 0xa0, 0},
    {0x23a3, 5, 0xff, 0xff, 0xcb, 0xcb, 0},
    {0x23a6, 5, 0xdf, 0xdf, 0x0b, 0x00, 0},
    {0x23c8, 5, 0xfc, 0xfc, 0x80, 0x40, 0},
    {0x23c9, 5, 0x0f, 0x0f, 0x01, 0x01, 0},
    {0x23ca, 5, 0xdf, 0xdf, 0x00, 0x00, 0},
    {0x2488, 5, 0xff, 0xff, 0x01, 0x00, 0},
    {0x2489, 5, 0xff, 0xff, 0xc8, 0xc8, 0},
    {0x2537, 5, 0x01, 0x7f, 0x01, 0x00, 0},
    {0x2588, 5, 0xff, 0xff, 0xff, 0x00, 0},
    {0x2589, 5, 0xff, 0xff, 0x3a, 0x30, 0},
    {0x258a, 5, 0xff, 0xff, 0x03, 0x00, 0},
    {0x258b, 5, 0x77, 0x77, 0x77, 0x00, 0},
    {0x258c, 5, 0xf7, 0xf7, 0x33, 0x33, 0},
    {0x258d, 5, 0xff, 0xff, 0x81, 0x00, 0},
    {0x258e, 5, 0xff, 0xff, 0x00, 0x00, 0},
    {0x258f, 5, 0xff, 0xff, 0x00, 0x00, 0},
    {0x2591, 5, 0xb7, 0xb7, 0x00, 0x00, 0},
    {0x259a, 5, 0xff, 0xff, 0x00, 0x01, 0},
    {0x259b, 5, 0xff, 0xff, 0xf2, 0x02, 0},
    {0x259c, 5, 0xff, 0xff, 0x87, 0x00, 0},
    {0x259d, 5, 0xff, 0xff, 0x00, 0x00, 0},
    {0x259f, 5, 0x07, 0x77, 0x01, 0x01, 0},
    {0x25a0, 5, 0x10, 0x17, 0x00, 0x10, 0},
    {0x25a1, 5, 0xc3, 0xff, 0x00, 0x00, 0},
    {0x25a2, 5, 0x11, 0x77, 0x01, 0x10, 0},
    {0x25a3, 5, 0x15, 0x37, 0x15, 0x00, 0},
    {0x260b, 5, 0x1f, 0x1f, 0x1f, 0x1f, 0},
    {0x260c, 5, 0x1f, 0x1f, 0x1f, 0x1f, 0},
    {0x079e, 9, 0x04, 0xff, 0x04, 0, 0},
};
static const struct p4_step p4_up[] = {
    {0x1cb2, 0x1, P4_VAUD, 0x00, 0}, /* mt6368_vaud18 */
    {0x079e, 0x1, P4_CLOCK, 0x04, 0}, /* CLK_BUF */
    {0x2389, 0x1, P4_UPDATE, 0x01, 0}, /* SGEN DL SRC */
    {0x2396, 0x6, P4_WRITE, 0xdf, 0}, /* SGEN MUTE */
    {0x2393, 0xcb, P4_WRITE, 0xff, 0}, /* SGEN MUTE */
    {0x2392, 0xa1, P4_WRITE, 0xff, 0}, /* SGEN MUTE */
    {0x2396, 0x3, P4_WRITE, 0xdf, 0}, /* SGEN MUTE */
    {0x2396, 0xb, P4_WRITE, 0xdf, 0}, /* SGEN MUTE */
    {0x23c8, 0x0, P4_UPDATE, 0xff, 0}, /* SGEN MUTE */
    {0x23c9, 0x1, P4_UPDATE, 0xff, 0}, /* SGEN MUTE */
    {0x23ca, 0x0, P4_UPDATE, 0xff, 0}, /* SGEN MUTE */
    {0x23c8, 0x80, P4_UPDATE, 0xc0, 0}, /* SGEN DL Enable + SGEN MUTE */
    {0x25a2, 0x0, P4_UPDATE, 0x10, 0}, /* AUDGLB */
    {0x258d, 0x0, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x10, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x20, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x30, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x40, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x50, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x60, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x70, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x2537, 0x1, P4_UPDATE, 0x01, 0}, /* CLKSQ Audio */
    {0x230c, 0x0, P4_UPDATE, 0x64, 0}, /* AUDNCP_CK + ZCD13M_CK + AUDIF_CK */
    {0x230c, 0x0, P4_UPDATE, 0x02, 0}, /* AUD_CK */
    {0x0000, 0xfa, P4_DELAY, 0x00, 0}, /* AUD_CK */
    {0x238f, 0x0, P4_UPDATE, 0x47, 0}, /* AUDIO_TOP_DAC_CTL + AUDIO_TOP_PWR_CLK + AUDIO_TOP_PDN_AFE_TESTMODEL + AUDIO_TOP_PDN_RESERVED */
    {0x238f, 0x0, P4_UPDATE, 0x80, 0}, /* AUDIO_TOP_AFE_CTL */
    {0x0000, 0xfa, P4_DELAY, 0x00, 0}, /* AUDIO_TOP_AFE_CTL */
    {0x2396, 0x2, P4_UPDATE, 0x02, 0}, /* SDM_FIFO_CLK */
    {0x2396, 0x6, P4_UPDATE, 0xfd, 0}, /* SDM */
    {0x2393, 0xcb, P4_WRITE, 0xff, 0}, /* SDM */
    {0x2392, 0xa1, P4_WRITE, 0xff, 0}, /* SDM */
    {0x2396, 0x3, P4_UPDATE, 0xfd, 0}, /* SDM */
    {0x2396, 0xb, P4_UPDATE, 0xfd, 0}, /* SDM */
    {0x23a6, 0x6, P4_WRITE, 0xdf, 0}, /* SDM_3RD */
    {0x23a3, 0xcb, P4_WRITE, 0xff, 0}, /* SDM_3RD */
    {0x23a2, 0xa1, P4_WRITE, 0xff, 0}, /* SDM_3RD */
    {0x23a6, 0x3, P4_WRITE, 0xdf, 0}, /* SDM_3RD */
    {0x23a6, 0xb, P4_WRITE, 0xdf, 0}, /* SDM_3RD */
    {0x2489, 0xc8, P4_WRITE, 0xff, 0}, /* NCP */
    {0x2488, 0x0, P4_WRITE, 0xff, 0}, /* NCP */
    {0x2488, 0x1, P4_UPDATE, 0x01, 0}, /* NCP */
    {0x2388, 0x1, P4_UPDATE, 0x01, 0}, /* AFE_ON */
    {0x2389, 0x1, P4_UPDATE, 0x01, 0}, /* AFE_DL_SRC */
    {0x258d, 0x1, P4_UPDATE, 0x01, 0}, /* ESD_RESIST */
    {0x0000, 0xfa, P4_DELAY, 0x00, 0}, /* ESD_RESIST */
    {0x260b, 0x1e, P4_WRITE, 0x1f, 0}, /* HP_MUTE */
    {0x260c, 0x1e, P4_WRITE, 0x1f, 0}, /* HP_MUTE */
    {0x25a3, 0x4, P4_UPDATE, 0x04, 0}, /* LDO_REMOTE */
    {0x25a3, 0x1, P4_UPDATE, 0x01, 0}, /* LDO */
    {0x25a3, 0x10, P4_UPDATE, 0x10, 0}, /* NV_REGULATOR */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* NV_REGULATOR */
    {0x258d, 0x80, P4_UPDATE, 0x80, 0}, /* HP_ANA_TRIM */
    {0x258e, 0x0, P4_TRIM_L, 0xf8, 0}, /* HP_ANA_TRIM */
    {0x258e, 0x0, P4_FINE_L, 0x07, 0}, /* HP_ANA_TRIM */
    {0x258f, 0x0, P4_TRIM_R, 0xf8, 0}, /* HP_ANA_TRIM */
    {0x258f, 0x0, P4_FINE_R, 0x07, 0}, /* HP_ANA_TRIM */
    {0x25a0, 0x0, P4_UPDATE, 0x10, 0}, /* IBIST */
    {0x259f, 0x1, P4_UPDATE, 0x07, 1}, /* HPL Mux */
    {0x25a1, 0x0, P4_UPDATE, 0xc0, 1}, /* HPL Mux */
    {0x25a1, 0x0, P4_UPDATE, 0x03, 1}, /* HPL Mux */
    {0x259c, 0x87, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x2591, 0x0, P4_WRITE, 0xb7, 1}, /* HPL Mux */
    {0x258c, 0x33, P4_WRITE, 0xf7, 1}, /* HPL Mux */
    {0x258d, 0xf1, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x258b, 0x0, P4_WRITE, 0x77, 1}, /* HPL Mux */
    {0x258a, 0xc, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x258a, 0x3c, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x259a, 0x0, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x259b, 0xc, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x2589, 0x30, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x2588, 0xc0, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x2588, 0xf0, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x258a, 0xfc, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x259b, 0xe, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x259b, 0x2, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x258a, 0xff, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x258b, 0x0, P4_UPDATE, 0x07, 1}, /* HPL Mux */
    {0x258b, 0x0, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258b, 0x1, P4_UPDATE, 0x07, 1}, /* HPL Mux */
    {0x258b, 0x10, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258b, 0x2, P4_UPDATE, 0x07, 1}, /* HPL Mux */
    {0x258b, 0x20, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258b, 0x3, P4_UPDATE, 0x07, 1}, /* HPL Mux */
    {0x258b, 0x30, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258b, 0x4, P4_UPDATE, 0x07, 1}, /* HPL Mux */
    {0x258b, 0x40, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258b, 0x5, P4_UPDATE, 0x07, 1}, /* HPL Mux */
    {0x258b, 0x50, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258b, 0x6, P4_UPDATE, 0x07, 1}, /* HPL Mux */
    {0x258b, 0x60, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258b, 0x7, P4_UPDATE, 0x07, 1}, /* HPL Mux */
    {0x258b, 0x70, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0x0, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0x10, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0x20, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0x30, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0x40, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0x50, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0x60, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0x70, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0x80, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0x90, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0xa0, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0xb0, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0xc0, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0xd0, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0xe0, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x259b, 0xf0, P4_UPDATE, 0xf0, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258a, 0xcf, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x258b, 0x77, P4_WRITE, 0x77, 1}, /* HPL Mux */
    {0x260b, 0x1f, P4_UPDATE, 0x9f, 1}, /* HPL Mux */
    {0x260c, 0x1f, P4_UPDATE, 0x9f, 1}, /* HPL Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258a, 0xc3, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x258a, 0x3, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x25a2, 0x1, P4_UPDATE, 0x01, 1}, /* HPL Mux */
    {0x2589, 0x30, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x2588, 0xff, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x259a, 0x0, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x259b, 0xf2, P4_WRITE, 0xff, 1}, /* HPL Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x2589, 0x2, P4_UPDATE, 0x03, 1}, /* HPL Mux */
    {0x2589, 0x8, P4_UPDATE, 0x0c, 1}, /* HPL Mux */
    {0x258d, 0x70, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258d, 0x60, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258d, 0x50, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258d, 0x40, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258d, 0x30, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258d, 0x20, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258d, 0x10, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPL Mux */
    {0x258d, 0x0, P4_UPDATE, 0x70, 1}, /* HPL Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPL Mux */
};
static const struct p4_step p4_down[] = {
    {0x258d, 0x0, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258d, 0x10, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258d, 0x20, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258d, 0x30, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258d, 0x40, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258d, 0x50, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258d, 0x60, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258d, 0x70, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x64, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x2589, 0x0, P4_UPDATE, 0x0f, 1}, /* HPR Mux */
    {0x259a, 0x0, P4_UPDATE, 0x01, 1}, /* HPR Mux */
    {0x2588, 0x0, P4_UPDATE, 0x0f, 1}, /* HPR Mux */
    {0x25a2, 0x0, P4_UPDATE, 0x01, 1}, /* HPR Mux */
    {0x258b, 0x77, P4_WRITE, 0x77, 1}, /* HPR Mux */
    {0x258a, 0xc3, P4_WRITE, 0xff, 1}, /* HPR Mux */
    {0x258a, 0xcf, P4_WRITE, 0xff, 1}, /* HPR Mux */
    {0x260b, 0x1e, P4_UPDATE, 0x9f, 1}, /* HPR Mux */
    {0x260c, 0x1e, P4_UPDATE, 0x9f, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258a, 0xff, P4_WRITE, 0xff, 1}, /* HPR Mux */
    {0x259b, 0xf0, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0xe0, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0xd0, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0xc0, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0xb0, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0xa0, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0x90, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0x80, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0x70, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0x60, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0x50, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0x40, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0x30, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0x20, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0x10, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x259b, 0x0, P4_UPDATE, 0xf0, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258b, 0x7, P4_UPDATE, 0x07, 1}, /* HPR Mux */
    {0x258b, 0x70, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258b, 0x6, P4_UPDATE, 0x07, 1}, /* HPR Mux */
    {0x258b, 0x60, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258b, 0x5, P4_UPDATE, 0x07, 1}, /* HPR Mux */
    {0x258b, 0x50, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258b, 0x4, P4_UPDATE, 0x07, 1}, /* HPR Mux */
    {0x258b, 0x40, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258b, 0x3, P4_UPDATE, 0x07, 1}, /* HPR Mux */
    {0x258b, 0x30, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258b, 0x2, P4_UPDATE, 0x07, 1}, /* HPR Mux */
    {0x258b, 0x20, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258b, 0x1, P4_UPDATE, 0x07, 1}, /* HPR Mux */
    {0x258b, 0x10, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258b, 0x0, P4_UPDATE, 0x07, 1}, /* HPR Mux */
    {0x258b, 0x0, P4_UPDATE, 0x70, 1}, /* HPR Mux */
    {0x0000, 0x258, P4_DELAY, 0x00, 1}, /* HPR Mux */
    {0x258a, 0x0, P4_UPDATE, 0x03, 1}, /* HPR Mux */
    {0x259a, 0x1, P4_WRITE, 0xff, 1}, /* HPR Mux */
    {0x259b, 0xe, P4_WRITE, 0xff, 1}, /* HPR Mux */
    {0x259b, 0xc, P4_WRITE, 0xff, 1}, /* HPR Mux */
    {0x258a, 0x0, P4_UPDATE, 0xc0, 1}, /* HPR Mux */
    {0x2588, 0x0, P4_UPDATE, 0x30, 1}, /* HPR Mux */
    {0x2588, 0x0, P4_UPDATE, 0xc0, 1}, /* HPR Mux */
    {0x259a, 0x1, P4_WRITE, 0xff, 1}, /* HPR Mux */
    {0x259b, 0x2, P4_WRITE, 0xff, 1}, /* HPR Mux */
    {0x258a, 0x0, P4_UPDATE, 0x30, 1}, /* HPR Mux */
    {0x258a, 0x0, P4_UPDATE, 0x0c, 1}, /* HPR Mux */
    {0x25a0, 0x10, P4_UPDATE, 0x10, 0}, /* IBIST */
    {0x258d, 0x0, P4_UPDATE, 0x80, 0}, /* HP_ANA_TRIM */
    {0x258e, 0x0, P4_WRITE, 0xff, 0}, /* HP_ANA_TRIM */
    {0x258f, 0x0, P4_WRITE, 0xff, 0}, /* HP_ANA_TRIM */
    {0x25a3, 0x0, P4_UPDATE, 0x10, 0}, /* NV_REGULATOR */
    {0x25a3, 0x0, P4_UPDATE, 0x01, 0}, /* LDO */
    {0x25a3, 0x0, P4_UPDATE, 0x04, 0}, /* LDO_REMOTE */
    {0x260b, 0x9f, P4_WRITE, 0x1f, 0}, /* HP_MUTE */
    {0x260c, 0x9f, P4_WRITE, 0x1f, 0}, /* HP_MUTE */
    {0x259c, 0x0, P4_WRITE, 0xff, 0}, /* HP_DAMP */
    {0x259d, 0x0, P4_WRITE, 0xff, 0}, /* HP_DAMP */
    {0x258d, 0x0, P4_UPDATE, 0x01, 0}, /* ESD_RESIST */
    {0x2389, 0x0, P4_UPDATE, 0x01, 0}, /* AFE_DL_SRC */
    {0x2388, 0x0, P4_UPDATE, 0x01, 0}, /* AFE_ON */
    {0x2488, 0x0, P4_UPDATE, 0x01, 0}, /* NCP */
    {0x2396, 0x0, P4_UPDATE, 0xfd, 0}, /* SDM */
    {0x2393, 0xcb, P4_WRITE, 0xff, 0}, /* SDM */
    {0x2392, 0xa0, P4_WRITE, 0xff, 0}, /* SDM */
    {0x23a6, 0x0, P4_WRITE, 0xdf, 0}, /* SDM_3RD */
    {0x23a3, 0xcb, P4_WRITE, 0xff, 0}, /* SDM_3RD */
    {0x23a2, 0xa0, P4_WRITE, 0xff, 0}, /* SDM_3RD */
    {0x2396, 0x0, P4_UPDATE, 0x02, 0}, /* SDM_FIFO_CLK */
    {0x0000, 0xfa, P4_DELAY, 0x00, 0}, /* AUDIO_TOP_AFE_CTL */
    {0x238f, 0x80, P4_UPDATE, 0x80, 0}, /* AUDIO_TOP_AFE_CTL */
    {0x238f, 0x47, P4_UPDATE, 0x47, 0}, /* AUDIO_TOP_DAC_CTL + AUDIO_TOP_PWR_CLK + AUDIO_TOP_PDN_AFE_TESTMODEL + AUDIO_TOP_PDN_RESERVED */
    {0x0000, 0xfa, P4_DELAY, 0x00, 0}, /* AUD_CK */
    {0x230c, 0x2, P4_UPDATE, 0x02, 0}, /* AUD_CK */
    {0x230c, 0x64, P4_UPDATE, 0x64, 0}, /* AUDNCP_CK + ZCD13M_CK + AUDIF_CK */
    {0x2537, 0x0, P4_UPDATE, 0x01, 0}, /* CLKSQ Audio */
    {0x258d, 0x70, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x60, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x50, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x40, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x30, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x20, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x10, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x258d, 0x0, P4_UPDATE, 0x70, 0}, /* HP_PULL_DOWN */
    {0x0000, 0x64, P4_DELAY, 0x00, 0}, /* HP_PULL_DOWN */
    {0x25a2, 0x10, P4_UPDATE, 0x10, 0}, /* AUDGLB */
    {0x079e, 0x0, P4_CLOCK, 0x04, 0}, /* CLK_BUF */
    {0x2389, 0x0, P4_UPDATE, 0x01, 0}, /* SGEN DL SRC */
    {0x23c8, 0x40, P4_UPDATE, 0xc0, 0}, /* SGEN DL Enable + SGEN MUTE */
    {0x2396, 0x0, P4_WRITE, 0xdf, 0}, /* SGEN MUTE */
    {0x2393, 0xcb, P4_WRITE, 0xff, 0}, /* SGEN MUTE */
    {0x2392, 0xa0, P4_WRITE, 0xff, 0}, /* SGEN MUTE */
    {0x1cb2, 0x0, P4_VAUD, 0x00, 0}, /* mt6368_vaud18 */
};
enum { P4_REG_COUNT = 38, P4_UP_COUNT = 173, P4_DOWN_COUNT = 147, P4_HP_DOWN_END = 94 };

/*
 * headset=4: fixed, muted codec-local SGEN reference. No SoC AFE/MTKAIF programming.
 * The plan follows the retained vendor callback/coalescing reference. Defined-byte
 * masks come from the matching mt6368.h; undocumented bits are not readback claims.
 * VAUD18 must already be enabled; this profile only verifies it and never switches it.
 */
enum p4_phase { P4_NONE, P4_ENABLING, P4_HP_CLOSING, P4_SUPPLY_CLOSING, P4_RESTORING };
enum p4_tone_phase { P4_TONE_IDLE, P4_TONE_WAIT, P4_TONE_ON, P4_TONE_GAP, P4_TONE_DONE };
static struct {
    uint32_t trim_l, trim_r, fine_l, fine_r;
    uint32_t tone, tone_gain;
    uint64_t tone_deadline;
    unsigned tone_pulses;
    enum p4_tone_phase tone_phase;
    unsigned up_pc, down_pc, restore_pc;
    enum p4_phase phase;
    bool owned, on, hp_started;
} p4;

static struct p4_register *
p4_find(uint32_t sid, uint16_t reg)
{
    for (unsigned i = 0; i < P4_REG_COUNT; i++) {
        if (p4_registers[i].sid == sid && p4_registers[i].reg == reg) {
            return &p4_registers[i];
        }
    }
    return 0;
}

static bool
p4_configure_arguments(void)
{
    uint32_t tone = 0;
    uint32_t tone_gain = 30;

    if (!PE_parse_boot_argn("hp_trim_l", &p4.trim_l, sizeof(p4.trim_l)) ||
        !PE_parse_boot_argn("hp_trim_r", &p4.trim_r, sizeof(p4.trim_r)) ||
        !PE_parse_boot_argn("hp_fine_l", &p4.fine_l, sizeof(p4.fine_l)) ||
        !PE_parse_boot_argn("hp_fine_r", &p4.fine_r, sizeof(p4.fine_r)) ||
        p4.trim_l > 31 || p4.trim_r > 31 || p4.fine_l > 7 || p4.fine_r > 7) {
        printf("headset: profile4 trim arguments missing/invalid; no PMIF access\n");
        return false;
    }
    if (PE_parse_boot_argn("hp_tone", &tone, sizeof(tone)) && tone > 1) {
        printf("headset: hp_tone must be 0 or 1; no PMIF access\n");
        return false;
    }
    if (!PE_parse_boot_argn("hp_tone_gain", &tone_gain, sizeof(tone_gain))) tone_gain = 30;
    if (tone_gain != 30 && tone_gain != 31) {
        printf("headset: hp_tone_gain must be 30 or 31; no PMIF access\n");
        return false;
    }
    p4.tone = tone;
    p4.tone_gain = tone_gain;
    printf("headset: profile4 caller-supplied trim L=%u/%u R=%u/%u; calibration quality unverified\n",
        p4.trim_l, p4.fine_l, p4.trim_r, p4.fine_r);
    if (p4.tone) {
        printf("headset: tone experiment requested: gain%u, three 1000ms pulses; physical output unmeasured\n",
            p4.tone_gain);
    }
    return true;
}

static bool
p4_write_byte(uint32_t sid, uint16_t reg, uint8_t value)
{
    if (mode != 4 || !p4.owned || __atomic_load_n(&quiesced, __ATOMIC_ACQUIRE) ||
        (sid != MT6368_SID && (sid != PMIF_SID_MT6685 || reg != 0x079e))) {
        return false;
    }
    pmif_status_t status = pmif_write8(sid, reg, value);
    mt6368_last_status = status;
    if (status != PMIF_OK) {
        printf("headset: profile4 write sid=%u reg=0x%04x: %s\n",
            sid, reg, pmif_status_name(status));
    }
    return status == PMIF_OK;
}

static bool
p4_apply(uint32_t sid, uint16_t reg, uint8_t mask, uint8_t bits, bool raw)
{
    struct p4_register *r = p4_find(sid, reg);
    uint8_t old = 0, got;
    if (mode != 4 || !p4.owned || !r) {
        return false;
    }
    mask &= r->defined;
    if (!mask || (mask & (uint8_t)~r->own) || (raw && r->own != r->defined)) {
        return false;
    }
    uint8_t keep = r->defined & (uint8_t)~r->own;
    if ((!raw || keep) && !mt6368_read_sid(sid, reg, &old)) {
        return false;
    }
    if (keep && (old & keep) != (r->original & keep)) {
        printf("headset: profile4 shared field changed sid=%u reg=0x%04x; write refused\n", sid, reg);
        return false;
    }
    uint8_t value = raw ? bits : (old & (uint8_t)~mask) | (bits & mask);
    if ((raw || value != old) && !p4_write_byte(sid, reg, value)) {
        return false;
    }
    if (!mt6368_read_sid(sid, reg, &got)) {
        return false;
    }
    bool ok = (got & mask) == (bits & mask) && (got & keep) == (r->original & keep);
    if (!ok) {
        printf("headset: profile4 verify FAILED sid=%u reg=0x%04x mask=0x%02x expected=0x%02x got=0x%02x\n",
            sid, reg, mask, bits & mask, got);
    }
    return ok;
}

static uint8_t
p4_original(uint32_t sid, uint16_t reg)
{
    struct p4_register *r = p4_find(sid, reg);
    return r ? r->original : 0;
}

static bool
p4_capture(void)
{
    uint8_t id, vaud, lo, hs, gain;
    if (p4.owned || mode != 4 ||
        !mt6368_read(0x0009, &id) || !mt6368_read(0x1cb2, &vaud) ||
        !mt6368_read(0x2595, &lo) || !mt6368_read(0x2593, &hs) ||
        !mt6368_read(0x23d7, &gain)) {
        return false;
    }
    for (unsigned i = 0; i < P4_REG_COUNT; i++) {
        struct p4_register *r = &p4_registers[i];
        if (!mt6368_read_sid(r->sid, r->reg, &r->original)) {
            return false;
        }
    }
    if (id != 0x68 || !(vaud & 1) || (lo & 3) || (hs & 3) || (gain & 0x24) ||
        p4_original(5, 0x2588) || p4_original(5, 0x258a) ||
        (p4_original(5, 0x2589) & 0x0f) || (p4_original(5, 0x2388) & 1) ||
        (p4_original(5, 0x2389) & 1) || (p4_original(5, 0x238e) & 0x1f) ||
        (p4_original(5, 0x23c8) & 0x80) || (p4_original(5, 0x2488) & 1) ||
        (p4_original(5, 0x2537) & 0x7b) ||
        (p4_original(5, 0x25a2) & 0x73) != 0x10 ||
        !(p4_original(5, 0x25a0) & 0x10) || (p4_original(5, 0x25a3) & 0x15) ||
        (p4_original(9, 0x079e) & 7)) {
        printf("headset: profile4 capture refused: chip/supply/source/another active path\n");
        return false;
    }
    p4.up_pc = p4.down_pc = p4.restore_pc = 0;
    p4.on = p4.hp_started = false;
    p4.phase = P4_ENABLING;
    p4.owned = true;
    printf("headset: profile4 captured %u field groups; muted SGEN, low-power 48k reference\n", P4_REG_COUNT);
    return true;
}

static bool
p4_step(const struct p4_step *s)
{
    uint8_t value = (uint8_t)s->arg, vaud;
    if (__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) {
        return false;
    }
    switch (s->op) {
    case P4_DELAY:
        delay(s->arg);
        return true;
    case P4_VAUD:
        /* Borrow an already enabled rail; this profile does not own its regulator. */
        return mt6368_read(0x1cb2, &vaud) && (vaud & 1);
    case P4_CLOCK:
        if (!p4_apply(9, 0x079e, 4, s->arg ? 4 : 0, false)) {
            return false;
        }
        if (s->arg) {
            delay(400); /* Actual mt6685_set_dcxo(): 400..420 us after enabling. */
        }
        return true;
    case P4_TRIM_L: value = (uint8_t)(p4.trim_l << 3); break;
    case P4_FINE_L: value = (uint8_t)p4.fine_l; break;
    case P4_TRIM_R: value = (uint8_t)(p4.trim_r << 3); break;
    case P4_FINE_R: value = (uint8_t)p4.fine_r; break;
    default: break;
    }
    return p4_apply(5, s->reg, s->mask, value, s->op == P4_WRITE);
}

static bool
p4_off_mask(uint32_t sid, uint16_t reg, uint8_t *mask)
{
    *mask = 0;
    if (sid == 9) { *mask = 4; return true; }
    switch (reg) {
    case 0x230c: *mask = 0x66; break;
    case 0x2388: case 0x2389: case 0x2392: case 0x23a2: case 0x2488: case 0x2537:
        *mask = 1; break;
    case 0x238f: *mask = 0xc7; break;
    case 0x2396: case 0x23a6: *mask = 0xdf; break;
    case 0x23c8: *mask = 0xc0; break;
    case 0x2588: case 0x258a: case 0x258e: case 0x258f: case 0x259c: case 0x259d:
        *mask = 0xff; break;
    case 0x2589: *mask = 0x0f; break;
    case 0x258d: *mask = 0xf1; break;
    case 0x25a0: *mask = 0x10; break;
    case 0x25a2: *mask = 0x11; break;
    case 0x25a3: *mask = 0x15; break;
    case 0x260b: case 0x260c: *mask = 0x1f; break;
    default: break;
    }
    return true;
}

static bool
p4_verify(unsigned stage)
{
    bool ok = true;
    const char *name = stage == 0 ? "on" : stage == 1 ? "off" : "restored";
    for (unsigned i = 0; i < P4_REG_COUNT; i++) {
        struct p4_register *r = &p4_registers[i];
        uint8_t got, mask = r->own, target = stage == 2 ? r->original : stage == 1 ? r->off : r->on;
        /* An aborted prefix did not necessarily program the normal bias/frequency codes.
         * Off verifies enables, mute and gates; only restoration requires original codes. */
        if (stage == 1) (void)p4_off_mask(r->sid, r->reg, &mask);
        if (stage == 0) {
            if (r->reg == 0x258e) target = (uint8_t)((p4.trim_l << 3) | p4.fine_l);
            if (r->reg == 0x258f) target = (uint8_t)((p4.trim_r << 3) | p4.fine_r);
            if (r->reg == 0x259d) target = r->original; /* Only HP_DAMP POST_PMD writes it. */
        }
        if (!mt6368_read_sid(r->sid, r->reg, &got)) {
            ok = false;
            continue;
        }
        uint8_t keep = r->defined & (uint8_t)~r->own;
        bool match = (got & mask) == (target & mask) &&
            (got & keep) == (r->original & keep);
        printf("headset: profile4 %s sid=%u reg=0x%04x own=0x%02x check=0x%02x expected=0x%02x read=0x%02x keep=0x%02x: %s\n",
            name, r->sid, r->reg, r->own, mask, target & mask, got, keep, match ? "verified" : "FAILED");
        ok = match && ok;
    }
    return ok;
}

static bool
p4_hp_off_shape(void)
{
    uint8_t dac, output, mux, reset;
    if (!mt6368_read(0x2588, &dac) || !mt6368_read(0x258a, &output) ||
        !mt6368_read(0x2589, &mux) || !mt6368_read(0x25a2, &reset)) {
        return false;
    }
    bool ok = dac == 0 && output == 0 && !(mux & 0x0f) && !(reset & 1);
    printf("headset: profile4 HP off shape 2588=%02x 258a=%02x 2589=%02x 25a2=%02x: %s\n",
        dac, output, mux, reset, ok ? "verified" : "NOT confirmed");
    return ok;
}

/* Optional experiment; the baseline profile and its vendor event trace are unchanged. */
static bool
p4_tone_gain(uint8_t gain)
{
    bool left = p4_apply(5, 0x260b, 0x1f, gain, false);
    bool right = p4_apply(5, 0x260c, 0x1f, gain, false);
    if (!left || !right) return false;
    delay(600); /* Vendor volume-ramp settling interval, after updating both channels. */
    return true;
}

static bool
p4_tone_gate(bool audible, const char *edge)
{
    uint8_t sgen, left, right, amp, freq;
    if (mode != 4 || !p4.owned || !p4.on || p4.phase != P4_ENABLING ||
        __atomic_load_n(&quiesced, __ATOMIC_ACQUIRE) ||
        !p4_apply(5, 0x23c8, 0x40, audible ? 0 : 0x40, false) ||
        !mt6368_read(0x23c8, &sgen) || !mt6368_read(0x260b, &left) ||
        !mt6368_read(0x260c, &right) || !mt6368_read(0x23c9, &amp) ||
        !mt6368_read(0x23ca, &freq)) {
        return false;
    }
    bool ok = (sgen & 0xfc) == (audible ? 0x80 : 0xc0) &&
        (left & 0x1f) == p4.tone_gain && (right & 0x1f) == p4.tone_gain &&
        (amp & 0x0f) == 1 && (freq & 0xdf) == 0;
    printf("headset: tone edge %s pulse=%u/3 sgen=%02x gain=%02x/%02x amp=%02x freq=%02x: %s\n",
        edge, p4.tone_pulses, sgen, left & 0x1f, right & 0x1f,
        amp & 0x0f, freq & 0xdf, ok ? "verified" : "FAILED");
    return ok;
}

static bool
p4_tone_begin(void)
{
    p4.tone_pulses = 0;
    /* Confirm digital mute before changing either analog gain from the baseline31. */
    if (!p4_apply(5, 0x23c8, 0x40, 0x40, false) ||
        !p4_tone_gain((uint8_t)p4.tone_gain) || !p4_tone_gate(false, "armed")) {
        return false;
    }
    p4.tone_phase = P4_TONE_WAIT;
    clock_interval_to_deadline(2000000, NSEC_PER_USEC, &p4.tone_deadline);
    printf("headset: tone armed: 2000ms wait, three 1000ms pulses separated by 1000ms digital mute\n");
    return true;
}

static bool
p4_tone_poll(void)
{
    if (!p4.tone || !p4.owned || !p4.on || p4.tone_phase == P4_TONE_DONE ||
        __atomic_load_n(&quiesced, __ATOMIC_ACQUIRE) ||
        (__atomic_load_n(&hp_worker_state, __ATOMIC_ACQUIRE) & HP_STOP_REQUESTED)) {
        return true;
    }
    if (mach_absolute_time() < p4.tone_deadline) return true;
    if (p4.tone_phase == P4_TONE_WAIT || p4.tone_phase == P4_TONE_GAP) {
        p4.tone_pulses++;
        if (!p4_tone_gate(true, "on")) return false;
        p4.tone_phase = P4_TONE_ON;
    } else if (p4.tone_phase == P4_TONE_ON) {
        if (!p4_tone_gate(false, "mute")) return false;
        if (p4.tone_pulses == 3) {
            if (!p4_tone_gain(31)) return false;
            p4.tone_phase = P4_TONE_DONE;
            return true;
        }
        p4.tone_phase = P4_TONE_GAP;
    } else {
        return false;
    }
    clock_interval_to_deadline(1000000, NSEC_PER_USEC, &p4.tone_deadline);
    return true;
}

static bool
p4_enable(void)
{
    if ((__atomic_load_n(&hp_worker_state, __ATOMIC_ACQUIRE) & HP_STOP_REQUESTED) ||
        !p4_capture() ||
        (__atomic_load_n(&hp_worker_state, __ATOMIC_ACQUIRE) & HP_STOP_REQUESTED) ||
        !p4_apply(5, 0x238e, 1, 1, false)) {
        return false;
    }
    while (p4.up_pc < P4_UP_COUNT) {
        if (__atomic_load_n(&hp_worker_state, __ATOMIC_ACQUIRE) & HP_STOP_REQUESTED) {
            return false;
        }
        const struct p4_step *s = &p4_up[p4.up_pc];
        p4.hp_started |= s->hp != 0;
        if (!p4_step(s)) {
            return false;
        }
        p4.up_pc++;
    }
    if (!p4_verify(0)) {
        return false;
    }
    p4.on = true;
    printf("headset: profile4 codec configuration verified; muted SGEN, physical output unmeasured\n");
    return !p4.tone || p4_tone_begin();
}

static bool
p4_down_until(unsigned end)
{
    while (p4.down_pc < end) {
        const struct p4_step *s = &p4_down[p4.down_pc];
        /* HP_MUTE's two final raw writes must both be attempted on a failure. */
        if (s->op == P4_WRITE && s->reg == 0x260b && s->arg == 0x9f && p4.down_pc + 1 < end &&
            s[1].op == P4_WRITE && s[1].reg == 0x260c && s[1].arg == 0x9f) {
            bool left = p4_step(s);
            bool right = p4_step(s + 1);
            if (!left || !right) return false;
            p4.down_pc += 2;
        } else {
            if (!p4_step(s)) return false;
            p4.down_pc++;
        }
    }
    return true;
}

static bool
p4_close_progress(void)
{
    if (p4.phase == P4_ENABLING) {
        p4.phase = P4_HP_CLOSING;
        p4.down_pc = p4.hp_started ? 0 : P4_HP_DOWN_END;
    }
    if (p4.phase == P4_HP_CLOSING) {
        bool sequence = p4_down_until(P4_HP_DOWN_END);
        bool off = p4_hp_off_shape(); /* Do not short-circuit this diagnostic. */
        if (off) p4.on = false;
        if (!sequence || !off) return false;
        p4.phase = P4_SUPPLY_CLOSING;
    }
    if (p4.phase == P4_SUPPLY_CLOSING) {
        if (!p4_down_until(P4_DOWN_COUNT) || !p4_verify(1)) return false;
        p4.phase = P4_RESTORING;
        p4.restore_pc = 0;
    }
    if (p4.phase == P4_RESTORING) {
        /* Catalogue order puts SID9 last: restore its clock field after all SID5 fields. */
        while (p4.restore_pc < P4_REG_COUNT) {
            unsigned i = p4.restore_pc < P4_REG_COUNT - 1 ? P4_REG_COUNT - 2 - p4.restore_pc : P4_REG_COUNT - 1;
            struct p4_register *r = &p4_registers[i];
            if (!p4_apply(r->sid, r->reg, r->own, r->original, false)) return false;
            p4.restore_pc++;
        }
        if (!p4_verify(2)) return false;
        p4.owned = p4.on = false;
        p4.phase = P4_NONE;
        printf("headset: profile4 off and %u original field groups restored: verified\n", P4_REG_COUNT);
    }
    return !p4.owned;
}

static bool
p4_shutdown(void)
{
    if (!p4.owned) return true;
    if (__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) return false;
    for (unsigned attempt = 0; attempt < 2; attempt++) {
        if (pmif_recover() != PMIF_OK) break;
        bool ok = p4_close_progress();
        printf("headset: profile4 close attempt %u/2 phase=%u down=%u restore=%u: %s\n",
            attempt + 1, p4.phase, p4.down_pc, p4.restore_pc, ok ? "verified" : "FAILED");
        if (ok) return true;
    }
    printf("headset: profile4 cleanup incomplete; snapshot and supply ownership retained\n");
    return false;
}

static bool
audio_path_shutdown(void)
{
    return mode == 4 ? p4_shutdown() : hp_shutdown();
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
	if (!audio_path_shutdown()) {
		if (mode == 4) {
			printf("headset: profile4 pending; ACCDET configuration snapshot retained\n");
			active = false;
			eint_sync = false;
			return;
		}
		ok = false;
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
		if (!audio_path_shutdown()) {
			return false;
		}
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
	if (mode == 3 && eint_sync && cable != CABLE_NONE && !hp_on &&
	    !(__atomic_load_n(&hp_worker_state, __ATOMIC_ACQUIRE) & HP_STOP_REQUESTED)) {
		if (!hp_enable()) {
			printf("headset: HP sequence failed; shutting down and restoring configuration\n");
			return false;
		}
	}
	if (mode == 4 && p4.owned && cable == CABLE_MIC) {
		printf("headset: profile4 classification changed to four-pole; closing\n");
		return false;
	}
	if (mode == 4 && eint_sync && cable == CABLE_NO_MIC && !p4.on && !p4.owned &&
	    !(__atomic_load_n(&hp_worker_state, __ATOMIC_ACQUIRE) & HP_STOP_REQUESTED)) {
		if (!p4_enable()) {
			printf("headset: profile4 enable failed; closing before configuration restore\n");
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
	if (!PE_parse_boot_argn("headset", &mode, sizeof(mode))) {
		printf("headset: no headset= boot-arg\n");
		return;
	}
	if (mode == 0) {
		printf("headset: headset=0, disabled\n");
		return;
	}
	if (mode != 1 && mode != 2 && mode != 3 && mode != 4) {
		printf("headset: unsupported mode %u\n", mode);
		return;
	}
	if (mode == 4 && !p4_configure_arguments()) {
		return;
	}
	if (mode == 3 || mode == 4) {
		unsigned expected = 0;
		if (!__atomic_compare_exchange_n(&hp_worker_state, &expected, HP_WORKER_RUNNING,
		    false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
			/* Normal return requested a stop before this worker began. No PMIF access. */
			return;
		}
	}
	{
		pmif_status_t mapped = pmif_init();

		if (mapped != PMIF_OK) {
			printf("headset: cannot map PMIF (%s)\n", pmif_status_name(mapped));
			goto finished;
		}
	}
	printf("headset: mode %u on SID 5, serialised in keys worker\n", mode);
	/* IDs, supply/clock configuration and the MFD interrupt-enable register. Read only. */
	static const uint16_t baseline_extra[] = { 0x0008, 0x0009, 0x1cb2, 0x230c, 0x231d };
	for (unsigned i = 0; i < sizeof(baseline_extra) / sizeof(baseline_extra[0]); i++) {
		if (!dump_block(baseline_extra[i], baseline_extra[i])) {
			goto finished;
		}
	}
	if (mode == 1) {
		printf("headset: mode 1 is read-only; AB is not detection evidence unless VAUD18 is enabled\n");
	}
	if (!dump_block(ACCDET_RG_LO, ACCDET_RG_HI)) {
		goto finished;
	}
	if (!dump_block(AUDENC_ANA_LO, AUDENC_ANA_HI) || mode == 1) {
		goto finished;
	}
	active = true;
	first_sample = true;
	if (!accdet_init_once() || !poll_sample()) {
		printf("headset: initialisation failed; restoring configuration\n");
		restore_configuration();
		goto finished;
	}
	printf("headset: mode %u initialised, polling every 50 ms; PCM playback not implemented\n", mode);
finished:
	if ((mode == 3 || mode == 4) && !active) {
		__atomic_store_n(&hp_worker_state, HP_WORKER_DONE, __ATOMIC_RELEASE);
	}
}

void
hakios_mt6895_headset_poll(uint32_t elapsed_ms)
{
	if (__atomic_load_n(&quiesced, __ATOMIC_ACQUIRE)) {
		return;
	}
	if (mode == 3 || mode == 4) {
		unsigned state = __atomic_load_n(&hp_worker_state, __ATOMIC_ACQUIRE);
		if (state & HP_STOP_REQUESTED) {
			if (state & HP_WORKER_RUNNING) {
				/* Also retry an earlier incomplete rollback, once, in the owning worker. */
				restore_configuration();
				__atomic_store_n(&hp_worker_state, HP_STOP_REQUESTED | HP_WORKER_DONE,
				    __ATOMIC_RELEASE);
			}
			return;
		}
	}
	if (!active) {
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
		return;
	}
	if (mode == 4 && p4.tone && p4.on) {
		if (!p4_tone_poll()) {
			printf("headset: tone transition failed; closing and restoring configuration\n");
			restore_configuration();
		} else if (p4.tone_phase == P4_TONE_DONE) {
			printf("headset: tone pattern complete; closing and restoring configuration\n");
			restore_configuration();
		}
	}
}

/* Normal thread context, before stopping CPUs. Only the keys worker performs the shutdown. */
void
hakios_mt6895_headset_stop(void)
{
	uint32_t requested = 0;
	if (!PE_parse_boot_argn("headset", &requested, sizeof(requested)) || (requested != 3 && requested != 4)) {
		return;
	}
	unsigned previous = __atomic_fetch_or(&hp_worker_state, HP_STOP_REQUESTED, __ATOMIC_ACQ_REL);
	if (!(previous & HP_WORKER_RUNNING)) {
		return;
	}
	for (unsigned wait = 0; wait < 2000; wait++) {
		if (!(__atomic_load_n(&hp_worker_state, __ATOMIC_ACQUIRE) & HP_WORKER_RUNNING)) {
			return;
		}
		delay(1000);
	}
	printf("headset: HP worker stop timed out; shutdown was NOT confirmed\n");
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
