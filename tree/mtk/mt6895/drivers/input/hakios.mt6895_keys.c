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
#include <sys/systm.h>

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

static uint16_t state[KP_STATE_REGS];
static unsigned changes, keylog_level, reports;

/* Which line a key of ours sits on, and whether the last poll saw it down. */
struct key {
	unsigned line;
	uint32_t button;
	bool down;
};

static struct key keys[] = {
	{ LINE_VOLUMEDOWN, HAKIOS_BUTTON_VOLUME_DOWN, false },
	{ LINE_VOLUMEUP,   HAKIOS_BUTTON_VOLUME_UP,   false },
	{ LINE_POWER,      HAKIOS_BUTTON_LOCK,        false },
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
	if (poll_ms < 5 || poll_ms > 500) {
		poll_ms = 20;
	}
	kpd = ml_io_map(KPD_PHYS, KPD_SIZE);

	/*
	 * The block is not in Android's iomem map (only 1c00f000, the dvfsrc, is), which on this SoC is
	 * what a block whose clock or power domain is off looks like. Reading one that is off is not an
	 * error return here: with no exception handling in this kernel it is a bus abort that takes the
	 * whole machine down. So read the state once, and treat a register block that answers with all
	 * ones or all zeros as absent rather than looping on it.
	 */
	read_state(state);
	if ((state[0] & state[1] & state[2] & state[3] & state[4]) == 0xffffu ||
	    (state[0] | state[1] | state[2] | state[3] | state[4]) == 0u) {
		printf("mt6895-keys: the keypad at %#x reads %04x %04x %04x %04x %04x, which is not a"
		    " block that is running; leaving it alone\n", (unsigned)KPD_PHYS,
		    state[0], state[1], state[2], state[3], state[4]);
		return;
	}

	/*
	 * Start the block, in the order kpd_pdrv_probe uses: enable first, then the debounce period.
	 * The period the phone asks for is 1024, which is what bits 13:0 of the register hold.
	 */
	wr16(KP_ENABLE, (uint16_t)(rd16(KP_ENABLE) | 0x1u));
	wr16(KP_DEBOUNCE, (uint16_t)1024u);

	read_state(state);
	printf("mt6895-keys: the keypad reads %04x %04x %04x %04x %04x every %u ms; "
	    "line %u is volume down, %u volume up, %u the power key\n",
	    state[0], state[1], state[2], state[3], state[4], poll_ms,
	    LINE_VOLUMEDOWN, LINE_VOLUMEUP, LINE_POWER);
	if (keylog_level >= 1) {
		dump();
	}

	for (unsigned tick = 1;; tick++) {
		uint16_t now[KP_STATE_REGS];
		bool sent = false;

		read_settled(now);
		for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
			bool down = line_down(now, keys[i].line);

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
