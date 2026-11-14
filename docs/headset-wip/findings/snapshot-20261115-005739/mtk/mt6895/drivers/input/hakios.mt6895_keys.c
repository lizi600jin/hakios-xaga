/*
 * The buttons of the Redmi Note 11T Pro (xaga), as a source of button presses for
 * /dev/hakios-touch (sys/hakios.input.h). Only with boot-arg buttons=2: every address below belongs
 * to this phone; buttons=1 is redwood's, in hakios.buttons.c.
 *
 * Where the buttons are, and how the block reads, comes from the phone's own Android driver: the
 * TWRP image for this phone ships mtk-kpd.ko (work-xaga/dt-device_xiaomi_xaga-TWRP/recovery/root/
 * lib/modules/mtk-kpd.ko), and its kpd_get_keymap_state and kpd_keymap_handler say all of it:
 *
 *   the state   five 16-bit registers at 0x04, 0x08, 0x0c, 0x10 and 0x14, read with ldrh, each one
 *               holding a bit per key line. kpd_get_keymap_state reads exactly those five and
 *               nothing else, so this is the whole state: eighty lines, of which the device tree
 *               declares 0x48 = 72 usable (mediatek,hw-map-num).
 *   the sense   a line's bit is *clear* while its key is down. kpd_keymap_handler takes the
 *               exclusive-or of the state against the last one, and for every changed line does
 *               "ands" of the new value against that line's bit followed by "cset eq": the equal
 *               case is the one where the bit is zero, and that is what it reports as pressed.
 *   the index   the key's number is register * 16 + bit, and anything at or above 72 is skipped
 *               ("cmp x9, #0x47 / b.hi").
 *
 * Two writes matter and are the only ones the driver makes to the block:
 *
 *   base + 0x20, bit 0      set once in kpd_pdrv_probe to start the block ("ldrh; orr #1; strh").
 *                           Without it the registers read, but no line is sampled.
 *   base + 0x18, bits 13:0  the debounce period, written just after and taken from the device
 *                           tree's mediatek,key-debounce-ms (xaga asks for 1024).
 *
 * Which line is which key is the device tree's business, and the two sources disagree by design:
 *
 *   mediatek,hw-init-map  in mt6895.dts: one entry, 114 (KEY_VOLUMEDOWN), for line 0.
 *   cust/xaga.dtsi        mediatek,hw-pwrkey = 8, mediatek,hw-rstkey = 17, and the reset key is
 *                         115 (KEY_VOLUMEUP). So line 8 is the power key and line 17 volume up.
 *                         The same file's hw-init-map is still "114, 0, 0, ...", which is why the
 *                         phone's dumped tree shows only that one entry.
 *
 * Because that mapping is the one part not proven by code, keylog=1 prints the line number of every
 * change, so a single press of each key confirms or corrects the three constants below.
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include <sys/hakios.audio.h>
#include <sys/systm.h>

/*
 * The one PMIF transaction path, shared with the headset and the GPU. It replaces the
 * hakios_pmif_budget deadline that used to live in <sys/hakios.pmif.h>: the deadline is now
 * inside pmif_wait(), where it can also see the STA error bit and the FSM, and the lock is
 * taken once around the whole transaction instead of never.
 * docs/headset-wip/findings/pmif-shared.h has the interface, pmif-shared.c the state machine.
 */
#include <dev/hakios.pmif.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

#define KPD_PHYS        0x1c00e000
#define KPD_SIZE        0x1000

/* The five state registers, as kpd_get_keymap_state reads them: 16 bits each, 4 bytes apart. */
#define KP_STATE_FIRST  0x04
#define KP_STATE_REGS   5
#define KP_LINES        (KP_STATE_REGS * 16)    /* 80 bits, of which the tree declares 72 */

#define KP_ENABLE       0x20            /* bit 0 starts the block */
#define KP_DEBOUNCE     0x18            /* bits 13:0, in the unit the tree asks for */

/* Line numbers, from cust/xaga.dtsi's hw-init-map and hw-pwrkey/hw-rstkey. */
#define LINE_VOLUMEDOWN 0
#define LINE_POWER      8
#define LINE_VOLUMEUP   17

static vm_offset_t kpd;
/*
 * No PMIF base of its own: the window is mapped once by pmif_init(), so a second driver
 * cannot leave a second mapping of it behind. What stays is whether that worked, which
 * decides whether the PMIC half of the poll runs at all.
 */
static bool pmif_ready;

/*
 * The PMIC's own key registers, reached over SPMI rather than through the keypad block.
 *
 * The keypad at 1c00e000 answers with all ones on this phone -- every one of its five state
 * registers reads ffff no matter what is pressed -- so the volume keys are not wired to it. The
 * device tree says where they are instead: mt6363keys hangs off mt6363 (slave 4 of the SPMI bus)
 * with a power child on keycode 116 and a home child on 115, and the Android side confirms the
 * state register is 0x001e, a bit per key with the bit *clear* while the key is down (bit 1 the
 * power key, bit 3 home, which this phone wires to volume up).
 *
 * Reading it means speaking SPMI, and the SoC's own arbiter is the only way in: MTK calls the
 * block PMIF, and a transfer is a command word written to a software-interface channel followed
 * by a poll on that channel's state for WFVLDCLR. Translated from spmi-mtk-pmif.c, which is the
 * driver Android uses for this exact controller (mediatek,mt6895-spmi, mt6873 register layout):
 *
 *   the channel   swinf_ch_start = 0x0a in this phone's tree, which is PMIF_CHAN_OFFSET (5) times
 *                 soc_chan (2) -- the same arithmetic the vendor driver does at probe.
 *   the command   (opc << 30) | (sid << 24) | ((len - 1) << 16) | addr, plus BIT(29) for a write.
 *                 opc is not the SPMI opcode but the arbiter's own: 1 for a plain register read,
 *                 which is what the driver maps SPMI_CMD_READ (0x60) onto.
 *   the state     bits 3:1 hold the FSM, 0 idle and 6 "valid, clear it"; the data is in RDATA and
 *                 the read is acknowledged by writing 1 to VLD_CLR.
 */
/*
 * Which software-interface channel is ours, and where its registers are.
 *
 * The tree says swinf_ch_start = 0x0a and ap_swinf_no = 0x02, and the vendor driver's arithmetic
 * explains both: chan_offset = PMIF_CHAN_OFFSET (5) * soc_chan (2) = 10, added to the *index* of
 * the PMIF_SWINF_0_* entries in its register table, not to their addresses. Ten entries past
 * SWINF_0 is SWINF_2, whose registers sit a fixed stride further on. Reading the table out:
 * SWINF_2_ACC 0x0c80, WDATA 0x0c84, RDATA 0x0c94, VLD_CLR 0x0ca4, STA 0x0ca8.
 *
 * The first version of this file added ten to the SWINF_0 *addresses*, which lands in the middle
 * of SWINF_0's dwords -- a transfer that could never complete.
 */
/*
 * Two register layouts exist for this block and they differ exactly where it matters. mt6873's
 * table puts the software channels at 0x0c00, mt8195's puts them at 0x0800, and CMDISSUE_EN at
 * 0x03b4 versus 0x03b8 -- the two tables share only the first few registers, which is why
 * INIT_DONE, INF_EN and ARB_EN read correctly on this SoC while every channel read returned zero
 * and a write to TIMER_CTRL at the mt6873 offset did not stick. xaga is closer to the mt8195
 * layout; the mt6873 offsets are kept as a fallback so a wrong guess costs a probe, not a port.
 */
/*
 * Settled on hardware, not by guessing: the vendor kernel leaves the arbiter readable through
 * /sys/bus/platform/drivers/spmi-mtk/pmif_access, which takes an offset and prints the word there.
 * Sweeping it and comparing against the two mainline tables left no room for doubt --
 *
 *	0x3b8 = 0x00000001	mt8195's CMDISSUE_EN	(0x3b4, mt6873's, reads 0)
 *	0x3e4 = 0x00000003	mt8195's TIMER_CTRL	(0x3e0 reads 0)
 *	0x408 = 0x0000225d	mt8195's SPI_MODE_CTRL	(0x400 reads 0)
 *	0x880 = 0xc50011c4	mt8195's SWINF_2_ACC	(0xc80 reads 0)
 *	0x8a8 = 0x00008441	mt8195's SWINF_2_STA	(0xca8 reads 0)
 *
 * The device tree agrees from the other side: it declares the pmif window as 0x1c804000 for only
 * 0x8ff bytes, so the mt6873 channel block at 0xc80 was never addressable from this window at all
 * -- every transfer this driver attempted landed past the end of the region and the state machine
 * stayed at IDLE forever.
 */
/*
 * The four software channels, as offsets from the first one's ACC. Linux picks SWINF_2 from the
 * There is exactly one channel to talk to, and walking the others is what crashed a build. The
 * device tree names the pmif window as 0x1c804000 for 0x8ff bytes only. SWINF_2 there is at
 * 0x880, so the next channel up would be 0x8c0 and the two after it 0x900 and 0x940 -- the last
 * two are past the end of the window. On this board an access beyond a mapped window is a bus
 * abort with no exception vectors: the core stops, the watcher never gets to run, and the board
 * rebooted into the same image three times running. So this driver addresses SWINF_2 and nothing
 * else, and PMIF_CHAN_COUNT is one so no loop can ever step outside the window again.
 */
/*
 * The opcode that goes into bits 31..30 of the command word is not the SPMI opcode: the PMIF
 * translates. Vendor code maps SPMI 0x60..0x7f to PMIF_CMD_REG (1) and SPMI 0x20..0x2f /
 * 0x38..0x3f to PMIF_CMD_EXT_REG_LONG (3), and the MT63xx PMICs are addressed with sixteen-bit
 * extended registers, so everything this driver sends has to be an extended transfer. Asking with
 * PMIF_CMD_REG left the state machine at 4 instead of WFVLDCLR and made the data register read
 * back zero, which then looked like both keys being held at once.
 */
#define PMIC_SID            4               /* mt6363 is "pmic@4" on the bus */
#define MT6363_KEYS_STATE   0x001e          /* bit 1 power, bit 3 home; clear means pressed */
#define MT6363_KEYS_ALT     0x002a          /* TOPSTATUS on mt6359, same two bits: the vendor's pick */
#define MT6363_POWER_BIT    1
#define MT6363_HOME_BIT     3

static inline uint16_t
rd16(uint32_t at)
{
	return *(volatile uint16_t *)(kpd + at);
}

static inline void
wr16(uint32_t at, uint16_t value)
{
	*(volatile uint16_t *)(kpd + at) = value;
}

static inline uint32_t
rd32(uint32_t at)
{
	return *(volatile uint32_t *)(kpd + at);
}

/*
 * The keys' one read, on the shared path. There is nothing of the transaction left here: the
 * channel search, the IDLE wait, the WFVLDCLR wait, the ACK after a timeout, the STA error
 * bits, the deadline and the lock all belong to pmif_read8. What is this driver's is the
 * policy -- read 0x1e, and say why when it fails.
 *
 * The first failure of each distinct status is logged once and the count is kept: a channel
 * that has gone deaf would otherwise print fifty lines a second, and the log the loader
 * carries back is four megabytes shared with the kernel's own.
 */
static pmif_status_t pmif_last_keys_status;
static unsigned pmif_keys_failures;

static bool
pmic_read(uint8_t addr, uint8_t *value)
{
	pmif_status_t status;

	if (!pmif_ready) {
		return false;
	}
	status = pmif_read8(PMIC_SID, addr, value);
	if (status == PMIF_OK) {
		pmif_last_keys_status = PMIF_OK;
		return true;
	}
	pmif_keys_failures++;
	if (status != pmif_last_keys_status) {
		printf("mt6895-keys: SID %u register %#04x: %s (failure %u)\n", PMIC_SID, addr,
		    pmif_status_name(status), pmif_keys_failures);
		pmif_last_keys_status = status;
	}
	return false;
}

/*
 * Which register actually holds the keys is the one thing about this block that code cannot settle:
 * the address 0x1e came from differencing five hundred Android snapshots, while mtk-pmic-keys
 * reads TOPSTATUS at 0x2a on mt6359 with the same two bit numbers. Rather than guess, keep a
 * shadow of the low page and, whenever either candidate moves, walk the whole page and print the
 * addresses that changed. One press of one key then names the register, the bit and the polarity
 * without a second run.
 */
#define PMIC_SCAN_LO    0x00
#define PMIC_SCAN_HI    0x60

static uint8_t pmic_shadow[PMIC_SCAN_HI - PMIC_SCAN_LO];
static bool pmic_shadow_valid;

static void
pmic_scan(const char *why)
{
	bool first = !pmic_shadow_valid;

	for (unsigned at = PMIC_SCAN_LO; at < PMIC_SCAN_HI; at++) {
		uint8_t value = 0;

		if (!pmic_read((uint8_t)at, &value)) {
			continue;
		}
		if (first || pmic_shadow[at - PMIC_SCAN_LO] != value) {
			if (!first) {
				printf("mt6895-keys: %s moved %#04x %02x -> %02x\n", why, at,
				    pmic_shadow[at - PMIC_SCAN_LO], value);
			}
			pmic_shadow[at - PMIC_SCAN_LO] = value;
		}
	}
	if (first) {
		printf("mt6895-keys: %s took a baseline of %#x..%#x\n", why,
		    PMIC_SCAN_LO, PMIC_SCAN_HI - 1);
		pmic_shadow_valid = true;
	}
}

static uint16_t state[KP_STATE_REGS];
static bool have_keypad;
static unsigned changes, keylog_level, reports;
static unsigned scan_page;
static uint8_t last_alt, last_keys_state;

/* Which line a key of ours sits on, and whether the last poll saw it down. */
struct key {
	unsigned line;          /* keypad line, or the PMIC's bit number when from_pmic */
	uint32_t button;
	bool from_pmic;
	bool down;
};

/*
 * Two sources, because this phone splits its buttons. The keypad has one declared key of its own
 * (line 0, KEY_VOLUMEDOWN, in mt6895.dts's hw-init-map) and the PMIC has the other two: power on
 * bit 1 and home on bit 3, and this phone's device tree wires home to keycode 115, volume up.
 */
static struct key keys[] = {
	{ LINE_VOLUMEDOWN, HAKIOS_BUTTON_VOLUME_DOWN, false, false },
	{ MT6363_HOME_BIT, HAKIOS_BUTTON_VOLUME_UP,   true,  false },
	{ MT6363_POWER_BIT, HAKIOS_BUTTON_LOCK,       true,  false },
};

static void
read_state(uint16_t *into)
{
	for (unsigned r = 0; r < KP_STATE_REGS; r++) {
		into[r] = rd16(KP_STATE_FIRST + r * 4);
	}
}

/*
 * Read until the answer settles, rather than once.
 *
 * The keypad is a matrix that the block itself scans, and a read that lands in the middle of a
 * scan sees lines that have not been driven yet -- they read back as clear, which is the same
 * level as a pressed key. That is what the log showed: seven lines clear at once across two
 * registers, in a pattern that moved every poll, when the only key touched was one. A pressed
 * line stays clear in every read, so the OR of several reads keeps the pressed lines at zero
 * and washes out the ones that were only momentarily undriven; a release reads back all ones
 * and survives the same treatment.
 */
#define KEY_SAMPLES 8

static void
read_settled(uint16_t *into)
{
	uint16_t sample[KP_STATE_REGS];

	for (unsigned r = 0; r < KP_STATE_REGS; r++) {
		into[r] = 0;
	}
	for (unsigned n = 0; n < KEY_SAMPLES; n++) {
		read_state(sample);
		for (unsigned r = 0; r < KP_STATE_REGS; r++) {
			into[r] = (uint16_t)(into[r] | sample[r]);
		}
	}
}

static bool
pmic_bit_down(uint8_t value, unsigned bit)
{
	/* Same sense as the keypad: the bit is clear while the key is held. */
	return (value & (1u << bit)) == 0;
}

static bool
line_down(const uint16_t *from, unsigned line)
{
	unsigned r = line / 16;
	unsigned bit = line % 16;

	if (r >= KP_STATE_REGS) {
		return false;
	}
	/* The driver's own sense: clear means down. */
	return (from[r] & (1u << bit)) == 0;
}

static void
dump(void)
{
	printf("mt6895-keys: keypad at %#x:", (unsigned)KPD_PHYS);
	for (uint32_t at = 0; at < 0x40; at += 4) {
		printf(" %02x=%08x", (unsigned)at, rd32(at));
	}
	printf("\n");
}

static void
keys_thread(__unused void *parameter, __unused wait_result_t result)
{
	uint32_t poll_ms = 20;

	PE_parse_boot_argn("key_poll_ms", &poll_ms, sizeof(poll_ms));
	PE_parse_boot_argn("keylog", &keylog_level, sizeof(keylog_level));
	PE_parse_boot_argn("key_scan", &scan_page, sizeof(scan_page));
	if (poll_ms < 5 || poll_ms > 500) {
		poll_ms = 20;
	}
	kpd = ml_io_map(KPD_PHYS, KPD_SIZE);
	/*
	 * The arbiter's window is mapped once, by the shared path; the headset and the GPU find
	 * it already up. A failure here is this driver's too: nothing of the PMIC side runs.
	 */
	{
		pmif_status_t mapped = pmif_init();

		pmif_ready = (mapped == PMIF_OK);
		if (!pmif_ready) {
			printf("mt6895-keys: the arbiter at %#x will not map (%s); "
			    "no PMIC key can be read\n", (unsigned)PMIF_BASE,
			    pmif_status_name(mapped));
		}
	}
	/* Inherit LK's arbiter setup. Use only SWINF_2, without the old timer/gate write probes. */
	pmic_scan("boot");
	{
		uint8_t keys_state = 0;

		if (pmic_read(MT6363_KEYS_STATE, &keys_state)) {
			printf("mt6895-keys: the PMIC's key register %#x reads %02x "
			    "(bit %u power, bit %u home/volume up; clear means held)\n",
			    MT6363_KEYS_STATE, keys_state, MT6363_POWER_BIT, MT6363_HOME_BIT);


			/*
			 * The address above came from differencing five hundred register snapshots
			 * taken on the Android side, and it is not the one the vendor driver uses:
			 * mtk-pmic-keys reads TOPSTATUS at 0x2a on mt6359, with the power key on
			 * bit 1 of the value it gets back and home on bit 3 -- the same bit numbers,
			 * a different register. MT6363 is close enough to mt6359 that either could
			 * be right, so read both and let the log say which one moves when a key is
			 * pressed. The poll uses 0x1e; if only 0x2a changes, that constant is wrong.
			 */
			{
				uint8_t alt = 0;

				if (pmic_read(0x2a, &alt)) {
					printf("mt6895-keys: for comparison register 0x2a reads %02x\n",
					    alt);
				} else {
					printf("mt6895-keys: register 0x2a did not answer\n");
				}
			}
		} else {
			/*
			 * Say which part of the arbiter looks dead, because there are only a few ways
			 * this fails and they need different fixes: INIT_DONE clear means the block
			 * has not been brought up at all, ARB_EN clear means it is brought up but not
			 * enabled, and a state machine parked outside IDLE means someone else is
			 * using the channel.
			 */
			uint32_t before = 0, after = 0;

			printf("mt6895-keys: the arbiter did not answer for the PMIC's key register; "
			    "only the keypad's own line will work\n");
			pmif_last_sta(&before, &after);
			printf("mt6895-keys: SWINF_2 sta %#x (fsm %u), last transaction %#x -> %#x\n",
			    after, (unsigned)PMIF_STA_FSM(after), before, after);
		}
	}

	/* SID 5 detection must not depend on one successful SID 4 key read. */
	hakios_mt6895_headset_init();

	/*
	 * The block is not in Android's iomem map (only 1c00f000, the dvfsrc, is), which on this SoC is
	 * what a block whose clock or power domain is off looks like. Reading one that is off is not an
	 * error return here: with no exception handling in this kernel it is a bus abort that takes the
	 * whole machine down. So read the state once, and treat a register block that answers with all
	 * ones or all zeros as absent rather than looping on it.
	 */
	/*
	 * The keypad cannot be the end of the story any more. It answers with all ones here, which
	 * used to make this function return -- and that return would now take the PMIC's keys with
	 * it, since they are read through the arbiter and have nothing to do with this block. So the
	 * block decides only whether the keypad half of the poll runs.
	 */
	read_state(state);
	have_keypad = !((state[0] & state[1] & state[2] & state[3] & state[4]) == 0xffffu ||
	    (state[0] | state[1] | state[2] | state[3] | state[4]) == 0u);
	if (!have_keypad) {
		printf("mt6895-keys: the keypad at %#x reads %04x %04x %04x %04x %04x, which is not a"
		    " block that is running; only the PMIC's keys will be watched\n",
		    (unsigned)KPD_PHYS, state[0], state[1], state[2], state[3], state[4]);
	} else {
		/*
		 * Start the block, in the order kpd_pdrv_probe uses: enable first, then the debounce
		 * period. The period the phone asks for is 1024, which is what bits 13:0 hold.
		 */
		wr16(KP_ENABLE, (uint16_t)(rd16(KP_ENABLE) | 0x1u));
		wr16(KP_DEBOUNCE, (uint16_t)1024u);

		read_state(state);
		printf("mt6895-keys: the keypad reads %04x %04x %04x %04x %04x every %u ms; "
		    "line %u is volume down\n", state[0], state[1], state[2], state[3], state[4],
		    poll_ms, LINE_VOLUMEDOWN);
	}
	printf("mt6895-keys: polling every %u ms; PMIC bit %u is power, bit %u volume up\n",
	    poll_ms, MT6363_POWER_BIT, MT6363_HOME_BIT);
	if (keylog_level >= 1) {
		dump();
	}

	for (unsigned tick = 1;; tick++) {
		uint16_t now[KP_STATE_REGS];
		bool sent = false;

		read_settled(now);
		{
			uint8_t keys_state = 0;
			bool have_pmic = pmic_read(MT6363_KEYS_STATE, &keys_state);
			/*
			 * The vendor's candidate is watched in the same breath as ours. Whichever one
			 * moves is the real register, and the scan then says which bit and which way.
			 */
			uint8_t alt_state = 0;
			bool have_alt = pmic_read(MT6363_KEYS_ALT, &alt_state);

			if (have_alt && (alt_state != last_alt || keys_state != last_keys_state)) {
				if (keylog_level != 0) {
					printf("mt6895-keys: %#x %02x %#x %02x\n",
					    MT6363_KEYS_STATE, keys_state, MT6363_KEYS_ALT, alt_state);
				}
				if (scan_page) {
					pmic_scan(alt_state != last_alt ? "0x2a" : "0x1e");
				}
				last_alt = alt_state;
				last_keys_state = keys_state;
			}

			for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
				bool down;

				if (!keys[i].from_pmic) {
					continue;
				}
				if (!have_pmic) {
					continue;
				}
				down = pmic_bit_down(keys_state, keys[i].line);
				if (down == keys[i].down) {
					continue;
				}
				keys[i].down = down;
				changes++;
				hakios_touch_post(down ? HAKIOS_BUTTON_DOWN : HAKIOS_BUTTON_UP,
				    keys[i].button, 0, 0);
				sent = true;
				if (keylog_level != 0) {
					printf("mt6895-keys: pmic bit %u %s (register %02x)\n",
					    keys[i].line, down ? "down" : "up", keys_state);
				}
			}
		}
		for (unsigned i = 0; have_keypad && i < sizeof(keys) / sizeof(keys[0]); i++) {
			bool down;

			if (keys[i].from_pmic) {
				continue;
			}
			down = line_down(now, keys[i].line);

			if (down == keys[i].down) {
				continue;
			}
			keys[i].down = down;
			changes++;
			hakios_touch_post(down ? HAKIOS_BUTTON_DOWN : HAKIOS_BUTTON_UP,
			    keys[i].button, 0, 0);
			sent = true;
			if (keylog_level != 0) {
				printf("mt6895-keys: line %u %s\n", keys[i].line,
				    down ? "down" : "up");
			}
		}
		/*
		 * Any line that moved but is not one of ours: print it once, so that a mapping that is
		 * wrong is visible rather than silent. This is what turns one press into a correction.
		 */
		if (keylog_level != 0 && !sent) {
			for (unsigned r = 0; r < KP_STATE_REGS; r++) {
				uint16_t moved = (uint16_t)(now[r] ^ state[r]);

				if (moved == 0) {
					continue;
				}
				for (unsigned b = 0; b < 16; b++) {
					if (moved & (1u << b)) {
						printf("mt6895-keys: line %u is now %s\n",
						    r * 16 + b,
						    (now[r] & (1u << b)) ? "up" : "down");
					}
				}
			}
		}
		for (unsigned r = 0; r < KP_STATE_REGS; r++) {
			state[r] = now[r];
		}
		/*
		 * Once a minute, not once a second: the log the loader carries back is four megabytes
		 * and is shared with the kernel's own.
		 */
		if (keylog_level >= 2 && tick % (60000 / poll_ms) == 0) {
			dump();
		}
		if (tick % (10000 / poll_ms) == 0) {
			printf("mt6895-keys: %u changes so far; the keypad reads %04x %04x %04x %04x %04x\n",
			    changes, state[0], state[1], state[2], state[3], state[4]);
		}
		hakios_mt6895_headset_poll(poll_ms);
		delay(poll_ms * 1000);
	}
}

void
hakios_mt6895_keys_init(void)
{
	uint32_t which = 0;
	thread_t thread;

	/* 2 is this phone's numbering, the same one the touch screen uses for its own kind. */
	if (!PE_parse_boot_argn("buttons", &which, sizeof(which)) || which != 2) {
		return;
	}
	if (kernel_thread_start(keys_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		printf("mt6895-keys: cannot start the thread that reads the keypad\n");
	}
}
