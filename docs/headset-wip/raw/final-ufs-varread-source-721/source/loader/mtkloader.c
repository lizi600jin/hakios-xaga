/*
 * mtkloader: boots XNU on a MediaTek phone (an Infinix Hot 20, X6826B/MT6768, first) from boot_a, and brings Android
 * back afterwards with XNU's log in pstore.
 *
 * The boot image's kernel (tools/chainboot.py) is this program, then Android's own kernel Image
 * at CHAIN_OFFSET, then the XNU payload ("XNUP": kernel, ramdisk). Each boot, by the marker:
 *
 *   none     RUN: load XNU, give it LK's framebuffer, arm the watchdog, start it.
 *   RUNNING  XNU was cut short by the watchdog: copy its log from DRAM into ramoops, start
 *            Android.
 *   DONE     XNU came back to us (panic, early exception, orderly end) and its log was saved
 *            then: start Android.
 *
 * The log ends up in /sys/fs/pstore/console-ramoops-0 under Android. See mtkdbg.h for the
 * screen and ramoops output.
 *
 * Display: the panel is video mode and keeps scanning OVL0 layer 0 (LK's framebuffer,
 * 0x787b0000, pitch 2944 = 736 pixels). LK enables per-pixel alpha on that layer, and XNU's
 * console writes 0x00rrggbb pixels: fully transparent. So per-pixel alpha is switched off, and
 * XNU's color table gets alpha 0xff besides.
 */
#include <stdint.h>
#include <stddef.h>
/*
 * The board's own facts: which SoC this loader runs on and where that SoC put the things this file
 * touches -- the UART, the watchdog, ramoops, LK's framebuffer, the OVL, XNU's window of DRAM, the
 * processors. build.sh names the file of the device being built (mtk/<soc>/loader/board.h) in
 * -DBOARD_HEADER, and it comes before everything below; without it, the MT6768 / x6826b values in
 * this file and in mtkdbg.h are what a build gets.
 */
#ifdef BOARD_HEADER
#include BOARD_HEADER
#endif
#include "mtkdbg.h"
#include "msdc.h"
#include "ufs.h"
#if defined(BOARD_UFS_READ_LEASE) && BOARD_UFS_READ_LEASE
#include "ufs_return_audit.h"
#endif
#include "mtkspi.h"
#include "tcm.h"
#include "../../xnuloader/identity.h"   /* made from your IPSW by xnuloader/mkidentity.sh */

/* XNU's view of memory: one contiguous range that nothing else (LK, Android, TEE, ATF, the
 * framebuffer) uses. On the MT6768 that is 0xc0000000-0xfe000000. A board whose DRAM is carved up
 * differently (mt6895) gives its own window in board.h. */
#ifndef XNU_PHYS_BASE
#define XNU_PHYS_BASE       0xc0000000ULL
#endif
#ifndef XNU_MEM_SIZE
#define XNU_MEM_SIZE        0xfe000000ULL       /* up to dramc-rk1 at 0x1bffff000; room for the system image */
#endif
/* Where the system image is put, clear of the kernel, as xnuloader does it (RESIDENT_RD_OFFSET). */
#ifndef SYSTEM_PA
#define SYSTEM_PA           (XNU_PHYS_BASE + 0x08000000ULL)
#endif
#ifndef SYSTEM_PART
#define SYSTEM_PART         "xnusys"
#endif
/* 0: all of it. Smaller is for trying the path out: packfs will not mount a half image. */
#ifndef SYSTEM_MAX_MB
#define SYSTEM_MAX_MB       0
#endif
/* boot_args.virtBase, as xnuloader/loader.c has it: the kernel's text is 48 MB above it. */
#define KERNEL_VIRT_BASE    0xfffffff004000000ULL
#define XNU_SLIDE           (XNU_PHYS_BASE - KERNEL_VIRT_BASE)
#define PAGE_SIZE_MAX       0x4000ULL

/* Seconds XNU may run before the watchdog resets into Android; 0: run until reset by hand. */
#ifndef XNU_WDT_SECONDS
#define XNU_WDT_SECONDS     0
#endif
/* Seconds after which the second core takes a snapshot of XNU and resets; 0: no watcher. */
#ifndef XNU_WATCH_SECONDS
#define XNU_WATCH_SECONDS   0
#endif
/*
 * While it waits, the watcher draws a red bar along the top of the panel that grows by one step
 * every 100 ms. The console text it writes does not reach the screen (XNU owns it), and the
 * ramoops ring is off on this board, so the bar is the only evidence from outside the phone that
 * this core is alive. Turn it off when the bar would hide something worth reading.
 */
#ifndef XNU_WATCH_HEARTBEAT
#define XNU_WATCH_HEARTBEAT 1
#endif
/*
 * How many processors the device tree offers the kernel. It takes them from the head of
 * core_order below, so what is left of that list is nobody's but ours -- the watcher takes the
 * next one. Leaving all eight in the tree and trusting "cpus=N" to stop the kernel sooner is a
 * guess about its parsing; this is not. XNU_EXTRA_ARGS should carry the same number as "cpus=".
 *
 * A watcher on a core the kernel wants is worse than none: PSCI CPU_ON answers "already on", the
 * kernel counts a processor that never checks in, and every rendezvous between cores waits for
 * it -- which is how a 15 minute run stayed on the Apple logo.
 */
#ifndef XNU_CORES
#define XNU_CORES           4
#endif
_Static_assert(XNU_CORES >= 1 && XNU_CORES <= 7, "one core at least, and one left for the watcher");

/*
 * The processors in the order the kernel is offered them: the one this code runs on first (LK
 * leaves us on core 0), then the two Cortex-A75s, which are about twice the Cortex-A55s, then
 * the rest of the A55s. So "cpus=3" already gives iOS both fast cores.
 *
 * A board gives its own list, its own count of what runs on a big core, and what "core" means to
 * the kernel's device tree: here it is the low byte of MPIDR (the A75s are cores 6 and 7), on a
 * MT6895 the A78 cluster sits at Aff1=1 and its cores are 0x100, 0x101, 0x102 and 0x200.
 */
#ifndef BOARD_CORE_ORDER
#define BOARD_CORE_ORDER    { 0, 7, 6, 1, 2, 3, 4, 5 }
#endif
static const uint32_t core_order[8] = BOARD_CORE_ORDER;
#ifndef BOARD_SMALL_CORE
#define BOARD_SMALL_CORE(core)  ((core) < 6)     /* cores 0-5 are the Cortex-A55s, 6 and 7 the A75s */
#endif
#define WATCH_CORE          core_order[XNU_CORES]

/*
 * The window the device tree maps: every address a node under arm-io names is written relative to
 * ARM_IO_BASE, and the kernel adds it back. One window has to hold the interrupt controller and the
 * console, so it is the span of the SoC's low registers, not the first megabyte of it.
 */
#ifndef ARM_IO_BASE
#define ARM_IO_BASE         0x0c000000ULL
#endif
#ifndef ARM_IO_SIZE
#define ARM_IO_SIZE         0x14000000ULL
#endif
#ifndef BOARD_CPU_REG
#define BOARD_CPU_REG(core) ((uint32_t)((core) << 8))  /* MPIDR: Aff1 in bits 15:8, Aff0 in 7:0 */
#endif

/* The interrupt controller: one distributor.
 *
 * A board whose second cluster has an address space of its own (the MT6895's Cortex-A78s) turns the
 * last element into that cluster's GICR frame: 0x200000 bytes from GICR_BASE, which the kernel finds
 * from the number of frames the register holds.
 */
#ifndef GICD_BASE
#define GICD_BASE           0x0c000000ULL
#endif
#ifndef GICD_SIZE
#define GICD_SIZE           0x10000ULL
#endif
#ifndef GICR_BASE
#define GICR_BASE           0x0c040000ULL
#endif
#ifndef GICR_SIZE
#define GICR_SIZE           0x100000ULL
#endif
#define GICR_SGI    (GICR_BASE + 0x10000)
/* Checkpoint for XNU to return at (osfmk/arm/arm_init.c LOADER_CHECKPOINT), 0 = none. */
#ifndef XNU_CHECKPOINT
#define XNU_CHECKPOINT      0
#endif
/* loader_return_after: XNU comes back by itself after that long, with its log (loader_return.c) */
#ifndef XNU_RETURN_AFTER
#define XNU_RETURN_AFTER    "0"
#endif
/* 1: always start XNU (tools/xnu-cycle.sh); 0: Android unless asked once (tools/boot-xnu.sh). */
/* 1: look at the touch screen's SPI and report, then go back to Android (no XNU). */
#ifndef TOUCH_PROBE
#define TOUCH_PROBE         0
#endif
/* 1: wake the GPU and ask it what it is, if Android asked for it (tools/gpu-probe.sh); then Android. */
#ifndef GPU_PROBE
#define GPU_PROBE           0
#endif
/* 1: read the eMMC and report, then go back to Android (no XNU). */
#ifndef MSDC_TEST
#define MSDC_TEST           0
#endif
/* 1: read the UFS host (the MT6895's storage) and report, then go back to Android (no XNU). */
#ifndef UFS_TEST
#define UFS_TEST            0
#endif
/* 1: hand the touch controller its firmware before XNU starts (the MT6768's TD4160 over SPI2). */
#ifndef BOARD_TOUCH
#define BOARD_TOUCH         1
#endif
/* 1: raise the big cores' PLL before XNU starts (the MT6768's ARMPLL, post-divider 1). */
#ifndef XNU_BIG_CLOCK
#define XNU_BIG_CLOCK       1
#endif
/* 1: check at EL1 that the timer interrupts arrive at all (diagnostics, leaves GIC state). */
#ifndef TIMER_PROBE
#define TIMER_PROBE         0
#endif
#ifndef MENU_DEFAULT_XNU
#define MENU_DEFAULT_XNU    0
#endif
/*
 * 1: the kernel's text console on the screen (-v). It is a slow way to run -- scrolling reads
 * the whole framebuffer back -- and iOS cannot draw while it is on, so a real boot goes without
 * it and the log comes back through the loader instead (v_display = 1 says "graphics mode").
 */
#ifndef XNU_VERBOSE
#define XNU_VERBOSE         0
#endif
/*
 * 1: the framebuffer is the system's (v_display = 1), 0: the kernel's text console owns it.
 *
 * It used to be spelled "XNU_VERBOSE ? 0 : 1" and that is wrong once the two questions are asked
 * separately: a run can want -v (so kprintf goes to the screen and every step is visible) and still
 * want the console to give the framebuffer up afterwards, or want no -v but the text console to
 * stay because nothing else draws yet. Default: follow XNU_VERBOSE, as before.
 */
#ifndef XNU_DISPLAY
#define XNU_DISPLAY         (XNU_VERBOSE ? 0 : 1)
#endif
/* More for the kernel's command line: cores, the clock (epoch=), whatever a run wants to try. */
/* The serial number iOS is told (the device tree's serial-number): a placeholder, not any real device's. */
#ifndef XNU_SERIAL
#define XNU_SERIAL          "HAKIOS0000000000"
#endif
#ifndef XNU_EXTRA_ARGS
#define XNU_EXTRA_ARGS      "cpus=1"
#endif
/*
 * 1: the system image stays on the eMMC and the kernel reads it as it needs it (emmc_root=1,
 * bsd/dev/redwood_emmc.c), the default; 0: it is read into DRAM here as md0 -- 2.4 GB of the kernel's 4 GB,
 * wired from the start, and three and a half minutes of every boot (run 320).
 */
#ifndef XNU_ROOT_EMMC
#define XNU_ROOT_EMMC       1
#endif
#if XNU_ROOT_EMMC
#define XNU_ROOT_ARG        "rd=md0 emmc_root=1"     /* (rd=md0 for libignition's "ramdisk" boot) */
#else
#define XNU_ROOT_ARG        "rd=md0"
#endif
/*
 * No "serial=3": that asks the kernel for KDP over the serial port, and this board's AP UART is
 * not wired -- kdp_init() then sits in the serial callout and the boot stops right after
 * "initializing kdp".  No "debug=0x8" either: it sends kprintf to the debugger instead of the
 * screen, which is the opposite of what -v is asking for.
 */
#if XNU_VERBOSE
#define XNU_BOOT_ARGS       XNU_ROOT_ARG " -v " XNU_EXTRA_ARGS " loader_return_after=" XNU_RETURN_AFTER
#else
#define XNU_BOOT_ARGS       XNU_ROOT_ARG " " XNU_EXTRA_ARGS " loader_return_after=" XNU_RETURN_AFTER
#endif

#if BOARD_MALI_EARLY || BOARD_MALI_PREPOWER
static void mali_leave_powered(void);
#endif
#if BOARD_MALI_POWER
/*
 * Declared here rather than beside its definition eight hundred lines further down: the jump to
 * XNU is the caller, and that comes first in this file. The body sits with the MALI probe it grew
 * out of, because both read the same SPM and topckgen registers.
 */
static void mali_leave_powered(void);
/*
 * Called from xnu_returned(), which is above the definition: a run that comes back with the
 * domains still raised is the run that loops, so this is on the way out of every path.
 */
static void mali_power_down(void);
#endif
#if BOARD_MALI_RETURN
/*
 * The logbook helpers live below the USB probe, which is after the jump path in this file, so the
 * return-to-Android branch that writes the record needs them named here first. Same pair the USB
 * and display probes use: one accumulates text, the other writes it to the end of xnusys.
 */
static void probe_say(const char *text);
static void probe_flash(uint64_t reason);
#endif
/* 1: read the system image off the flash into DRAM here, as SYSTEM_PART; 0: do not look for one.
 * A board whose flash this loader cannot speak (the MT6895 has UFS and no MSDC at all) has to say
 * 0 -- there is no way to fail gracefully when the controller's registers do not decode. */
#ifndef XNU_SYSTEM_IMAGE
#define XNU_SYSTEM_IMAGE    1
#endif

#define CHAIN_OFFSET        0x40000u
/* Where LK puts the kernel it starts, and scratch DRAM for the stub that starts it again: both are
 * facts about the board's memory map, so board.h may name them. */
#ifndef KERNEL_PA
#define KERNEL_PA           0x40080000ULL
#endif
#ifndef CHAIN_STUB_PA
#define CHAIN_STUB_PA       0x4a000000ULL
#endif

/* Run state, kept in DRAM across the watchdog reset (three copies: a bit may flip). It must be a
 * word of DRAM that survives the reset and that neither XNU nor Android writes over. */
#ifndef MARKER_PA
#define MARKER_PA           0x80000000ULL
#endif
#define MARK_RUNNING        0x4e52584eu /* "NXRN" */

/*
 * BOARD_STEPS: a progress ladder instead of scattered probes.
 *
 * Photographs of the scattered probes were not readable -- too small, and the step numbers were
 * placed in three different columns while part of them were painted before a full-screen clear
 * and part after it, so what survived on the glass was a mix of two runs. One band per step,
 * full width, stacked from y=1000 down, painted after the clear, and the ladder only ever grows:
 * the lowest band on the screen is always the last step that returned. Nothing on the real path
 * is skipped, so what this measures is the path the loader actually takes, not a copy of it.
 */
#ifdef BOARD_STEPS
#define STEP_BAND(n, colour) fb_rect(0, 1000 + (n) * 95, PANEL_W, 90, (colour))
#else
#define STEP_BAND(n, colour) do { } while (0)
#endif
#define MARK_DONE           0x4e44584eu /* "NXDN" */

/* The watcher's record of what the kernel was doing when it stopped answering, left in DRAM for
 * the next run of this loader: it cannot write flash itself (the controller is in whatever state
 * XNU left it in), but DRAM survives the reset and the marker above already relies on that. */
#ifndef SNAP_PA
#define SNAP_PA             0x90000100ULL
#endif
#ifndef SNAP_SIZE
#define SNAP_SIZE           0x00100000u
#endif
#define SNAP_MAGIC          0x50414e53u /* "SNAP" */

/* mtkdbg.h appends every console character to this when it is set (the watcher's tee). */
char *dbg_tee;
uint32_t dbg_tee_cap, dbg_tee_at;
struct snap_hdr {
	uint32_t magic;
	uint32_t version;
	uint64_t len;          /* bytes of text after this header */
	uint64_t ticks;         /* the watcher's own clock at the moment of the snapshot */
	uint64_t reserve[4];
};

#define MH_MAGIC_64         0xfeedfacfu
#define LC_SEGMENT_64       0x19
#define LC_SYMTAB           0x2
#define LC_UNIXTHREAD       0x5
#define PAYLOAD_MAGIC       0x50554e58u /* "XNUP" */
#ifndef PAYLOAD_SCAN_LIMIT
/* The whole packed blob this loader sits in, with room: program + Android Image + payload. */
#define PAYLOAD_SCAN_LIMIT  0x4000000ULL /* 64 MiB */
#endif

struct boot_video {
	uint64_t v_baseAddr, v_display, v_rowBytes, v_width, v_height, v_depth;
};

#define BOOT_LINE_LENGTH 608

/* pexpert/pexpert/arm64/boot.h: virtBase comes first (start.s reads BA_VIRT_BASE, BA_PHYS_BASE). */
struct boot_args {
	uint16_t revision;
	uint16_t version;
	uint64_t virtBase;
	uint64_t physBase;
	uint64_t memSize;
	uint64_t topOfKernelData;
	struct boot_video video;
	uint32_t machineType;
	uint64_t deviceTreeP;
	uint32_t deviceTreeLength;
	char commandLine[BOOT_LINE_LENGTH];
	uint64_t bootFlags;
	uint64_t memSizeActual;
};

_Static_assert(__builtin_offsetof(struct boot_args, virtBase) == 8, "boot_args");
_Static_assert(__builtin_offsetof(struct boot_args, physBase) == 16, "boot_args");
_Static_assert(__builtin_offsetof(struct boot_args, video) == 40, "boot_args");
_Static_assert(__builtin_offsetof(struct boot_args, deviceTreeP) == 96, "boot_args");
_Static_assert(__builtin_offsetof(struct boot_args, commandLine) == 108, "boot_args");
_Static_assert(__builtin_offsetof(struct boot_args, bootFlags) == 720, "boot_args");

struct mach_header_64 {
	uint32_t magic, cputype, cpusubtype, filetype, ncmds, sizeofcmds, flags, reserved;
};

struct load_command {
	uint32_t cmd, cmdsize;
};

struct segment_command_64 {
	uint32_t cmd, cmdsize;
	char segname[16];
	uint64_t vmaddr, vmsize, fileoff, filesize;
	uint32_t maxprot, initprot, nsects, flags;
};

struct symtab_command {
	uint32_t cmd, cmdsize, symoff, nsyms, stroff, strsize;
};

struct nlist_64 {
	uint32_t n_strx;
	uint8_t n_type, n_sect;
	uint16_t n_desc;
	uint64_t n_value;
};

struct payload_header {
	uint32_t magic;
	uint32_t size;
};

struct payload_entry {
	char name[16];
	uint32_t size;
	uint32_t reserved;
};

struct payload_info {
	const uint8_t *kernel;
	uint32_t kernel_size;
	const uint8_t *ramdisk;
	uint32_t ramdisk_size;
	const char *kernel_version;     /* kernel.ver (xnuloader/mkpayload.py): the kernel gets it as hakios_kver= */
};

/* The XNU symbols the loader works with (kernel virtual addresses, 0 if absent). */
struct xnu_syms {
	uint64_t stub_slot, vc_colors, log, log_len, log_ring_at, log_wrapped, panic_str, panic_args, iso_font,
	    msgbufp, cpu_ttep, live, uart_tx_ready, uart_tx_data, uart_rx_ready, uart_putc, uart_putc_options, log_putc,
	    nprocs, tasks_count, initproc, gic_cpu_init,
	    gic_unhandled_count, gic_last_unhandled, gic_active_iar, host_keep, host_keep_len, host_keep_dropped,
	    hk_keep, hk_keep_len, hk_keep_dropped;
};

static struct xnu_syms xsyms; /* of the kernel that was started; kept for when it comes back */
static const uint8_t *xkernel;  /* its Mach-O file, for symbols */
/* XNU's x29, x30, TCR_EL1 and TTBR1_EL1 when it came back (start.S) */
uint64_t xnu_fp[4];
static volatile uint64_t xnu_back __attribute__((aligned(64))); /* set by xnu_returned() */

struct dt_writer {
	uint8_t *base, *cur;
	uint32_t *counts[16];
	int depth;
};

extern char _start[];
/* set by start.S: where this code really runs, and where the link map put it */
extern const uint64_t mtk_runtime_start;
extern uint64_t mtk_here(void);
extern const uint64_t mtk_runtime_linked;
extern char chain_stub[], chain_stub_end[];
extern char xnu_return_stub[];
extern void jump_to_kernel(uint64_t entry_pa, uint64_t boot_args_pa);
extern void psci_system_reset(void);
extern int64_t psci_cpu_on(uint64_t mpidr, uint64_t entry, uint64_t context);
extern void watcher_entry(void);
extern void mmu_on(uint64_t l1_table);
extern void dcache_clean_range(uint64_t va, uint64_t len);
extern uint64_t irq_wait(uint64_t until_ticks);

/* set by the IRQ handler in start.S */
uint64_t irq_count, irq_intid;

static uint64_t lk_dtb;

/* ---- small libc (device memory: plain loops, word copies when aligned) ---- */

void *memset(void *dst, int c, size_t n)
{
	uint8_t *d = dst;
	if (c == 0 && !((uintptr_t)d & 7)) {
		for (; n >= 8; n -= 8, d += 8) {
			*(volatile uint64_t *)d = 0;
		}
	}
	while (n--) {
		*(volatile uint8_t *)d++ = (uint8_t)c;
	}
	return dst;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	uint8_t *d = dst;
	const uint8_t *s = src;
	if (!(((uintptr_t)d | (uintptr_t)s) & 7)) {
		for (; n >= 8; n -= 8, d += 8, s += 8) {
			*(volatile uint64_t *)d = *(const volatile uint64_t *)s;
		}
	}
	while (n--) {
		*(volatile uint8_t *)d++ = *(const volatile uint8_t *)s++;
	}
	return dst;
}

size_t strlen(const char *s)
{
	size_t len = 0;
	while (*s++) {
		len++;
	}
	return len;
}

/* 0 when the first n bytes are the same; device memory, so byte by byte. */
static int memcmp_n(const volatile void *a, const void *b, size_t n)
{
	const volatile uint8_t *x = a;
	const uint8_t *y = b;

	for (size_t i = 0; i < n; i++) {
		if (x[i] != y[i]) {
			return 1;
		}
	}
	return 0;
}

int strcmp(const char *s1, const char *s2)
{
	while (*s1 && *s1 == *s2) {
		s1++;
		s2++;
	}
	return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

/* ---- output: screen + ramoops + UART ---- */

#ifndef UART_BASE
#define UART_BASE           0x11002000ULL       /* MT6768: the console LK leaves behind */
#endif

static void uart_putc(char c)
{
#ifdef BOARD_NO_UART
	/*
	 * xaga: nothing is soldered to the AP UART, and with the MMU off a read of a
	 * peripheral that LK clocked down is a bus abort -- the loader stopped after one
	 * character on the real path while every framebuffer-only probe ran to the end.
	 * The screen and the ramoops ring are the two consoles this loader needs: XNU's
	 * log is read out of pstore, not off a wire.
	 */
	(void)c;
#else
	uint32_t timeout = 20000;
	while (!(rd32(UART_BASE + 0x14) & 0x20) && --timeout) {
	}
	wr32(UART_BASE, (uint8_t)c);
#endif
}

static void out_putc(char c)
{
	if (c == '\n') {
		uart_putc('\r');
	}
	uart_putc(c);
	dbg_putc(c);
}

static void out(const char *s)
{
	while (*s) {
		out_putc(*s++);
	}
}

static void out_hex(uint64_t v)
{
	static const char hex[] = "0123456789abcdef";
	int i = 60;
	out("0x");
	while (i > 0 && !((v >> i) & 0xf)) {
		i -= 4;
	}
	for (; i >= 0; i -= 4) {
		out_putc(hex[(v >> i) & 0xf]);
	}
}

static void out_dec(uint64_t v)
{
	char buf[21];
	int n = 0;
	do {
		buf[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n) {
		out_putc(buf[--n]);
	}
}

#include "mtkconnsys.h"         /* Wi-Fi step 1: Android's device tree on CONNSYS, printed (uses out()) */

/* ---- run state ---- */

static void marker_set(uint32_t v)
{
	for (int i = 0; i < 3; i++) {
		wr32(MARKER_PA + 64 * i, v);
	}
}

/*
 * How far the UFS bring-up got, in DRAM rather than on the panel.
 *
 * The screen cannot answer this question. ufs_self_test() arms the watchdog and sets MARK_DONE
 * before it touches the controller, so a hang resets the board, LK paints its logo over everything
 * we drew, and the next boot takes the MARK_DONE path straight to Android -- a photo then shows
 * "starting Android" and nothing else, which is exactly what build 24 produced. Text drawn before
 * a reset does not survive it; a word in DRAM does.
 *
 * So each stage records its number here, and the MARK_DONE path prints it. The number on the photo
 * is then the last stage that returned, whatever happened afterwards.
 */
#define UFS_STEP_PA (MARKER_PA + 512ULL)

static void ufs_step(uint32_t n)
{
	wr32(UFS_STEP_PA, n);
	dcache_clean_range(UFS_STEP_PA, 64);
}

static uint32_t ufs_step_get(void)
{
	return rd32(UFS_STEP_PA);
}

static uint32_t marker_get(void)
{
	uint32_t a = rd32(MARKER_PA), b = rd32(MARKER_PA + 64), c = rd32(MARKER_PA + 128);
	return (a == b || a == c) ? a : (b == c ? b : 0);
}

/* ---- the display ---- */

static uint64_t fb_base;
static uint32_t fb_pitch;

/* LK's layer as OVL0 scans it, if it is where LK said; LK's values otherwise. */
static void display_find(void)
{
	fb_base = LK_FB_BASE;
	fb_pitch = LK_FB_PITCH;
#ifdef BOARD_NO_OVL
	/*
	 * xaga: OVL0's registers are not where the MT6768's are. The live device tree puts
	 * disp_ccorr@1400b000 (not OVL0) where OVL0_BASE points, and ovl0 is at 14002000 -- but the
	 * first read is what stops the phone, whether or not the address is right: with the MMU off,
	 * touching a peripheral LK has clocked down is a bus abort, and the abort handler LK leaves
	 * behind does not come back. LK's own framebuffer address is plain DRAM and never fails, and
	 * the ruler build already proved the geometry in it is right, so nothing is lost here.
	 */
	return;
#endif
	{
	uint32_t addr = rd32(OVL0_BASE + OVL_L0_ADDR);
	uint32_t pitch = rd32(OVL0_BASE + OVL_L0_PITCH) & 0xffff;

	/*
	 * Take LK's layer only if it is a frame of this panel: at least a pixel per pixel wide, and not
	 * wildly wider than one (LK pads the width up to 32 pixels, so a pitch of a few pixels more than
	 * PANEL_W * 4 is normal). The upper bound has to follow the panel: 1080 pixels is already 4320
	 * bytes, so a fixed 4096 threw this away on every wide phone and left the loader writing to a
	 * framebuffer LK had not put there.
	 */
	if (addr >= LK_FB_BASE && addr < LK_FB_BASE + LK_FB_SIZE &&
	    pitch >= PANEL_W * 4 && pitch <= LK_FB_PITCH_MAX) {
		fb_base = addr;
		fb_pitch = pitch;
	}
	}
}

/* Opaque pixels only from here on: XNU's console writes alpha 0. */
static void display_alpha_off(void)
{
#ifdef BOARD_NO_OVL
	/* See display_find(): reading OVL0 is the one thing on this phone that stops the loader. */
	return;
#endif
	{
	uint32_t con = rd32(OVL0_BASE + OVL_L0_CON);
	wr32(OVL0_BASE + OVL_L0_CON, con & ~(1u << 8));
	out("OVL0 L0_CON ");
	out_hex(con);
	out(" -> ");
	out_hex(rd32(OVL0_BASE + OVL_L0_CON));

	/*
	 * LK composes two layers: layer 0 is the picture, and layer 3 is its own overlay with the
	 * warning it prints on an unlocked phone ("Orange State: your device has been unlocked"),
	 * from a buffer of its own. Nothing of iOS writes to that buffer, so the words stayed on
	 * top of the home screen. Leave the overlay's layers out of what the hardware composes.
	 */
	uint32_t src = rd32(OVL0_BASE + OVL_SRC_CON);
	wr32(OVL0_BASE + OVL_SRC_CON, src & 1u);
	wr32(DISP_MUTEX_EN, 1);
	wr32(DISP_MUTEX_RUN, 1);
	out(", SRC_CON ");
	out_hex(src);
	out(" -> ");
	out_hex(rd32(OVL0_BASE + OVL_SRC_CON));
	out("\n");
	}
}

/* ---- payload and the kernel's symbols ---- */

/*
 * A payload header is only worth believing if what it points at looks like our payload: the magic,
 * a size that fits in the blob, and a first entry called "kernel" whose data starts with the
 * Mach-O magic.  Any one of those alone is a four-byte coincidence waiting to happen, and the
 * field in the Image header that was supposed to save us this search cannot be trusted on this
 * board -- it reads back as something other than what chainboot.py wrote, which is also why the
 * boot stopped here with no explanation.
 */
static int payload_header_ok(const struct payload_header *ph)
{
	const uint8_t *e = (const uint8_t *)(ph + 1);
	const uint8_t *eend;

	if (ph->magic != PAYLOAD_MAGIC || ph->size < sizeof(struct payload_entry) ||
	    ph->size > PAYLOAD_SCAN_LIMIT) {
		return 0;
	}
	eend = e + ph->size;
	if (ph->size >= sizeof(struct payload_entry)) {
		const struct payload_entry *en = (const struct payload_entry *)e;

		if (strcmp(en->name, "kernel") == 0 && en->size > 4) {
			const uint8_t *d = e + sizeof(*en);

			if (((const struct mach_header_64 *)d)->magic == MH_MAGIC_64) {
				return 1;
			}
		}
	}
	(void)eend;
	return 0;
}

static const struct payload_header *payload_find(void)
{
	/*
	 * _start, not mtk_runtime_start: by the time this runs, start.S has moved the program to the
	 * link-time address, so the header and everything after it live at _start.  mtk_runtime_start
	 * is only a record of where LK put us before that move.
	 */
	uint64_t base = mtk_here();
	const volatile uint64_t *hdr = (const volatile uint64_t *)base;
	uint64_t off = hdr[6];

	/*
	 * The header field first, because it is the cheap answer and the one chainboot.py promises.
	 * Then a scan, because on this board that field reads back as something else.  The blob is
	 * contiguous -- program, Android's Image at CHAIN_OFFSET, payload last -- so the payload is
	 * in the 64 MiB after the program, and each candidate is checked rather than trusted.
	 */
	if (off != 0 && (off & 15) == 0 && off < PAYLOAD_SCAN_LIMIT) {
		const struct payload_header *ph = (const void *)(base + off);

		if (payload_header_ok(ph)) {
			return ph;
		}
	}
	for (uint64_t at = 0x1000;
	     at + sizeof(struct payload_header) < PAYLOAD_SCAN_LIMIT;
	     at += 16) {
		const volatile uint32_t *w = (const volatile uint32_t *)(base + at);

		if (*w == PAYLOAD_MAGIC) {
			const struct payload_header *ph = (const void *)(base + at);

			if (payload_header_ok(ph)) {
				return ph;
			}
		}
	}
	return NULL;
}

static int parse_payload(struct payload_info *pi)
{
	const struct payload_header *ph = payload_find();

	memset(pi, 0, sizeof(*pi));
	if (!ph) {
		return 0;
	}
	const uint8_t *p = (const uint8_t *)(ph + 1), *pend = p + ph->size;
	while (p + sizeof(struct payload_entry) <= pend) {
		const struct payload_entry *e = (const struct payload_entry *)p;
		if (e->name[0] == 0) {
			break;
		}
		if (strcmp(e->name, "kernel") == 0) {
			pi->kernel = p + sizeof(*e);
			pi->kernel_size = e->size;
		} else if (strcmp(e->name, "ramdisk") == 0) {
			pi->ramdisk = p + sizeof(*e);
			pi->ramdisk_size = e->size;
		} else if (strcmp(e->name, "kernel.ver") == 0 && e->size > 1) {
			pi->kernel_version = (const char *)(p + sizeof(*e));
		}
		p += sizeof(*e) + ((e->size + 15) & ~15u);
	}
	return pi->kernel != NULL && ((const struct mach_header_64 *)pi->kernel)->magic == MH_MAGIC_64;
}

static void find_syms(const uint8_t *kernel, struct xnu_syms *s)
{
	/* in the order of struct xnu_syms: the loop below fills it field by field */
	static const char *const names[] = {
		"_loader_stub_slot", "_vc_colors", "_loader_log", "_loader_log_len", "_loader_log_ring_at",
		"_loader_log_wrapped", "_debugger_panic_str", "_debugger_panic_args", "_iso_font",
		"_msgbufp", "_cpu_ttep", "_loader_live",
		"_vmapple_uart_transmit_ready", "_vmapple_uart_transmit_data",
		"_vmapple_uart_receive_ready", "_uart_putc", "_uart_putc_options", "_loader_log_putc",
		"_nprocs", "_tasks_count", "_initproc", "_gic_v3_irq_cpu_init",
		"_gic_unhandled_count", "_gic_last_unhandled", "_gic_active_iar",
		"_redwood_host_keep", "_redwood_host_keep_len", "_redwood_host_keep_dropped",
		/* (the same in a kernel of the common tree, hakios-xnu-kernel's mtk/mt6768/drivers/gpu/hakios.mali.c) */
		"_hakios_host_keep", "_hakios_host_keep_len", "_hakios_host_keep_dropped",
	};
	_Static_assert(sizeof(struct xnu_syms) == sizeof(names), "every field needs its name, in order");
	uint64_t *slots = (uint64_t *)s;
	const struct mach_header_64 *mh = (const struct mach_header_64 *)kernel;
	const struct load_command *lc = (const struct load_command *)(mh + 1);
	const struct symtab_command *st = NULL;

	memset(s, 0, sizeof(*s));
	for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const void *)((const uint8_t *)lc + lc->cmdsize)) {
		if (lc->cmd == LC_SYMTAB) {
			st = (const void *)lc;
		}
	}
	if (!st) {
		return;
	}
	const struct nlist_64 *sym = (const struct nlist_64 *)(kernel + st->symoff);
	const char *strs = (const char *)(kernel + st->stroff);
	for (uint32_t i = 0; i < st->nsyms; i++) {
		const char *n = strs + sym[i].n_strx;
		if (n[0] != '_' || (n[1] != 'l' && n[1] != 'v' && n[1] != 'd' && n[1] != 'i' && n[1] != 'm' && n[1] != 'c' && n[1] != 'g' && n[1] != 'n' && n[1] != 't' && n[1] != 'r' && n[1] != 'h')) {
			continue;
		}
		for (unsigned k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
			if (!slots[k] && strcmp(n, names[k]) == 0) {
				slots[k] = sym[i].n_value;
			}
		}
	}
}

static uint64_t kva_to_pa(uint64_t va)
{
	return va + XNU_SLIDE;
}


/* ---- the system image, on the flash ---- */

/*
 * userland/system.packfs is the root XNU mounts (md0), 2.8 GB of it: too large for a boot image,
 * so it lives in its own partition (tools/gpt-carve.py made "xnusys" out of the end of userdata)
 * and is read into DRAM here. The payload's small root.packfs stays the second disk (md1).
 */
static uint8_t gpt_entries[128 * 128] __attribute__((aligned(16)));
static uint8_t sector_buf[512] __attribute__((aligned(16)));

/*
 * One way to read the flash, whichever the board has. On the MT6768 that is the eMMC through
 * MSDC0; on the MT6895 there is no eMMC at all and it is UFS LUN 2 through the host controller.
 * Everything above this line (gpt_find, load_system_image, the self test) only ever names
 * board_read, and which controller answers is the board header's choice: BOARD_STORAGE_UFS.
 */
#ifndef BOARD_STORAGE_UFS
#define BOARD_STORAGE_UFS       0
#endif

static int board_read(uint64_t lba, uint32_t blocks, void *dst)
{
#if BOARD_STORAGE_UFS
	return ufs_read(lba, blocks, dst);
#else
	return msdc_read(lba, blocks, dst);
#endif
}

static const char *board_storage_name(void)
{
#if BOARD_STORAGE_UFS
	return "UFS";
#else
	return "eMMC";
#endif
}

/* What the last failed board_read said about itself, whichever controller it was. */
static const char *board_storage_what(void)
{
#if BOARD_STORAGE_UFS
	return ufs_last.what ? ufs_last.what : "no error recorded";
#else
	return msdc_last.what;
#endif
}

static uint32_t board_storage_status(void)
{
#if BOARD_STORAGE_UFS
	return ufs_last.status;
#else
	return msdc_last.status;
#endif
}

/*
 * Read `bytes` at byte offset `off`. The controllers below count in 512-byte sectors, but a GPT
 * does not: its LBAs are in the disk's own logical blocks, and those are 4096 bytes on this UFS
 * (the Linux driver on the phone reports logical_block_size 4096, and the mount table agrees).
 * Reading LBA 1 as if it were sector 1 lands on byte 512, which is zeros -- the header is at
 * byte 4096. So everything here works in bytes and this is the one place that divides by 512.
 */
static int board_read_bytes(uint64_t off, uint64_t bytes, void *dst)
{
	static uint8_t bounce[512] __attribute__((aligned(16)));
	uint8_t *d = (uint8_t *)dst;

	if (off & 511ull) {
		return 0;               /* callers work in GPT blocks, which are 512 or a multiple */
	}
	while (bytes) {
		/*
		 * One request per slot buffer. This used to cap at 2 MB, which quietly cut every caller
		 * down to four times as many requests as it asked for: load_system_image() handed over
		 * UFS_SLOT_BUFFER (8 MB) per chunk and got 4 x 2 MB instead, so raising the slot buffer
		 * on its own bought nothing. The screen showed 2.2 million polls per 8 MB chunk -- those
		 * polls were the fixed cost of the request, paid four times over.
		 */
		uint64_t cap = (uint64_t)UFS_SLOT_BUFFER;
#if !BOARD_STORAGE_UFS
		cap = 4096ull * 512ull;      /* the eMMC path keeps the old 2 MB window */
#endif
		uint64_t want = bytes > cap ? cap : bytes;
		uint32_t n = (uint32_t)((want + 511ull) & ~511ull);

		if (bytes >= (uint64_t)n) {
			if (!board_read(off / 512ull, n / 512u, d)) {
				return 0;
			}
			d += n;
		} else {
			/* the tail is not a whole sector: read it aside, copy out only what was asked */
			if (!board_read(off / 512ull, n / 512u, bounce)) {
				return 0;
			}
			memcpy(d, bounce, (size_t)bytes);
		}
		off += n;
		bytes = bytes > (uint64_t)n ? bytes - (uint64_t)n : 0;
	}
	return 1;
}

/*
 * How many bytes a GPT logical block is on this disk. The header says nothing about it, so ask
 * the disk: try the ordinary 512 first, then 4096. Cached after the first answer.
 */
static uint32_t gpt_block_size;

/* Ask the disk how big its GPT blocks are; 0 if neither 512 nor 4096 finds a header. */
static uint32_t gpt_detect(void)
{
	static const uint32_t try[2] = { 512, 4096 };

	if (gpt_block_size) {
		return gpt_block_size;
	}
	for (int i = 0; i < 2; i++) {
		if (board_read_bytes(try[i], 512, sector_buf) &&
		    !memcmp_n(sector_buf, "EFI PART", 8)) {
			gpt_block_size = try[i];
			out("gpt: ");
			out_dec(gpt_block_size);
			out("-byte blocks\n");
			return gpt_block_size;
		}
	}
	return 0;
}

static int gpt_find(const char *name, uint64_t *lba, uint64_t *blocks)
{
	if (!gpt_detect()) {
		return 0;
	}
	if (!board_read_bytes((uint64_t)gpt_block_size, 512, sector_buf)) {
		return 0;
	}
	if (memcmp_n(sector_buf, "EFI PART", 8)) {
		return 0;
	}
	uint64_t ent_lba = *(volatile uint64_t *)(sector_buf + 72);
	uint32_t count = *(volatile uint32_t *)(sector_buf + 80);
	uint32_t size = *(volatile uint32_t *)(sector_buf + 84);

	if (count > 128 || size != 128) {
		return 0;
	}
	if (!board_read_bytes(ent_lba * gpt_block_size, (uint64_t)count * size, gpt_entries)) {
		return 0;
	}
	for (uint32_t i = 0; i < count; i++) {
		const uint8_t *e = gpt_entries + i * size;
		int same = 1;

		for (int k = 0; same && name[k - (k ? 0 : 0)] != 0; k++) {
			same = e[56 + 2 * k] == (uint8_t)name[k] && e[57 + 2 * k] == 0;
		}
		if (same && e[56 + 2 * (int)strlen(name)] == 0) {
			uint64_t first = *(const volatile uint64_t *)(e + 32);
			uint64_t last = *(const volatile uint64_t *)(e + 40);
			if (first == 0 || last < first) {
				return 0;
			}
			*lba = first;
			*blocks = last - first + 1;
			return 1;
		}
	}
	return 0;
}

/*
 * What load_system_image() achieved, for the panel report. Three numbers, because they answer
 * the three different failures: found==0 means the partition or the GPT was not there, bytes
 * short of the image means the reads stopped part way, secs==0 means it never got going. The
 * function's own printing goes to the same console XNU floods a second later, so it is useless
 * for diagnosis; these survive.
 */
static uint32_t sysimg_found, sysimg_bytes, sysimg_secs;

/* Reads the image into SYSTEM_PA; returns its length, 0 if there is none to read. */
static uint64_t load_system_image(void)
{
	uint64_t lba, blocks, size = 0, done = 0, t0 = dbg_ticks(), f = dbg_freq();

	if (!gpt_find(SYSTEM_PART, &lba, &blocks)) {
		out("no " SYSTEM_PART " partition on the ");
		out(board_storage_name());
		out(" (or the GPT could not be read: ");
		out(board_storage_what());
		out(")\n");
		return 0;
	}
	out(SYSTEM_PART " at block ");
	out_hex(lba);
	out(", ");
	out_dec(blocks * (uint64_t)gpt_block_size / 1024ull / 1024ull);
	out(" MB\n");

	/*
	 * APFS: a container's superblock is at its first block -- "NXSB" at offset 32, then the
	 * block size and how many blocks it has. An image may hold several containers back to
	 * back (the cryptexes come that way), so follow them until one does not begin with NXSB.
	 */
	uint64_t base = lba * (uint64_t)gpt_block_size;   /* the partition starts at a GPT block */
	uint64_t span = blocks * (uint64_t)gpt_block_size;

	for (uint64_t at = 0;;) {
		if (!board_read_bytes(base + at, 512, sector_buf)) {
			out("cannot read it: ");
			out(board_storage_what());
			out("\n");
			return 0;
		}
		if (*(volatile uint32_t *)(sector_buf + 32) != 0x4253584eu) { /* "NXSB" */
			break;
		}
		uint32_t bs = *(volatile uint32_t *)(sector_buf + 36);
		uint64_t count = *(volatile uint64_t *)(sector_buf + 40);

		if (bs < 512 || bs > 65536 || count == 0 || count > span / bs) {
			out("the APFS container at ");
			out_dec(at / 1024 / 1024);
			out(" MB says ");
			out_dec(count);
			out(" blocks of ");
			out_dec(bs);
			out(": that cannot be\n");
			return 0;
		}
		at += count * bs;
		size = at;
		if (at >= span) {
			break;
		}
	}
	if (size == 0) {
		out("no APFS container there (write userland/system.apfs to it)\n");
		return 0;
	}
	out("APFS: ");
	out_dec(size / 1024 / 1024);
	out(" MB\n");
	if (SYSTEM_MAX_MB && size > (uint64_t)SYSTEM_MAX_MB * 1024 * 1024) {
		size = (uint64_t)SYSTEM_MAX_MB * 1024 * 1024;
		out("(only the first ");
		out_dec(SYSTEM_MAX_MB);
		out(" MB of it, as built)\n");
	}

	out("hs ");
	out_dec(ufs_hs_stage); out(" ok "); out_dec(ufs_hs_ok);
	out(" upm "); out_dec(ufs_hs_upmcrs);
	out(" rx "); out_dec(ufs_hs_gear_rx); out("/"); out_dec(ufs_hs_lane_rx);
	out(" tx "); out_dec(ufs_hs_gear_tx); out("/"); out_dec(ufs_hs_lane_tx);
	out("\n");
	out("reading ");
	out_dec(size / 1024 / 1024);
	out(" MB into ");
	out_hex(SYSTEM_PA);
	out(": ");
	uint32_t last_shown = 0;
	uint32_t chunks = 0;
	/*
	 * Read the whole chunk in ONE call.
	 *
	 * This loop used to advance 2 MB at a time while board_read_bytes() silently capped each call
	 * at its own 2 MB window, so the two agreed by luck rather than by design. It is now driven by
	 * UFS_SLOT_BUFFER end to end: the loop hands over exactly as much as one request can move, and
	 * nothing has to be inferred about how far `done` really got.
	 *
	 * The fixed cost per request (descriptor rebuild, door bell, completion poll) is what made
	 * 2.3 GiB look like a hang at 256 KiB per request -- ~9500 round trips.
	 */
	while (done < size) {
		uint64_t chunk = size - done;
		if (chunk > (uint64_t)UFS_SLOT_BUFFER) {
			chunk = UFS_SLOT_BUFFER;
		}

		/*
		 * board_read() takes a 512-byte sector number, board_read_bytes() takes a byte
		 * offset and does the division itself. base + done is a byte offset: passing it
		 * to board_read() asked for sector 0xa53e0000 and ufs_read() refused it with
		 * "lba out of READ(10) range" -- the image never loaded and md0 stayed root.apfs.
		 */
		if (!board_read_bytes(base + done, chunk, (void *)(SYSTEM_PA + done))) {
			out("\nfailed at ");
			out_dec(done / 1024 / 1024);
			out(" MB: ");
			out(board_storage_what());
			out(" status ");
			out_hex(board_storage_status());
			out("\n");
			return 0;
		}
		done += chunk;
		/*
		 * Say how long each of the first few requests took. If the loader stops here again,
		 * the difference between "one request costs 800 ms" and "one request never returns"
		 * is the whole diagnosis, and the percentages alone cannot tell them apart.
		 */
		chunks++;
		uint32_t pct = (uint32_t)(done * 100 / size);
		if (pct >= last_shown + 2 || chunks <= 6u) {
			if (pct >= last_shown + 2) {
				last_shown = pct - pct % 2;
			}
			/* every step carries how many polls that request cost, not just the first six */
			out_dec(pct);
			out("% ");
			out_dec(ufs_run_polls[0]);
			out("p ");
		}
	}
	uint64_t ms = (dbg_ticks() - t0) * 1000 / f;
	out("done in ");
	out_dec(ms / 1000);
	out(" s (");
	out_dec(size / 1024 / (ms ? ms : 1) * 1000 / 1024);
	out(" MB/s)\n");
	sysimg_found = 1;
	sysimg_bytes = (uint32_t)size;
	sysimg_secs = (uint32_t)(ms / 1000u);
	return size;
}

/* ---- device tree for XNU ---- */

static void dt_prop(struct dt_writer *w, const char *name, const void *data, uint32_t len)
{
	memset(w->cur, 0, 32);
	memcpy(w->cur, name, strlen(name));
	*(volatile uint32_t *)(w->cur + 32) = len;
	memset(w->cur + 36, 0, (len + 3) & ~3u);
	memcpy(w->cur + 36, data, len);
	w->cur += 36 + ((len + 3) & ~3u);
	w->counts[w->depth - 1][0]++;
}

static void dt_prop_str(struct dt_writer *w, const char *name, const char *value)
{
	dt_prop(w, name, value, (uint32_t)strlen(value) + 1);
}

static void dt_prop_u32(struct dt_writer *w, const char *name, uint32_t value)
{
	dt_prop(w, name, &value, sizeof(value));
}

static void dt_prop_u64(struct dt_writer *w, const char *name, uint64_t value)
{
	dt_prop(w, name, &value, sizeof(value));
}

static void dt_prop_padded(struct dt_writer *w, const char *name, const char *value, uint32_t size)
{
	uint8_t field[64];
	memset(field, 0, sizeof(field));
	memcpy(field, value, strlen(value));
	dt_prop(w, name, field, size);
}

static void dt_begin(struct dt_writer *w, const char *name)
{
	if (w->depth > 0) {
		w->counts[w->depth - 1][1]++;
	}
	w->counts[w->depth++] = (uint32_t *)w->cur;
	memset(w->cur, 0, 8);
	w->cur += 8;
	dt_prop_str(w, "name", name);
}

static void dt_end(struct dt_writer *w)
{
	w->depth--;
}


/* What the iPhone's own device tree says (model, compatible, product ...), from xnuloader. */
static void dt_identity(struct dt_writer *w, const struct identity_prop *props, uint32_t count, const unsigned char *data)
{
	for (uint32_t i = 0; i < count; i++) {
		/* the panel is sRGB, not the iPhone X's P3 (see xnuloader/loader.c) */
		if (strcmp(props[i].name, "artwork-display-gamut") == 0) {
			dt_prop(w, props[i].name, "sRGB", 5);
			continue;
		}
		/*
		 * The screen class iOS lays itself out for. The identity was generated with the panel's
		 * own height here, 1640, which no iPhone has: iOS takes an unknown class for a phone
		 * with a home button, and the Control Centre came up from the bottom edge as on an
		 * iPhone 8. The display driver reports the iPhone X's 1125 x 2436 whatever the panel,
		 * so the class has to be the iPhone X's too.
		 */
		if (strcmp(props[i].name, "artwork-device-subtype") == 0) {
			static const uint32_t subtype = 2436;

			dt_prop(w, props[i].name, &subtype, sizeof(subtype));
			continue;
		}
		dt_prop(w, props[i].name, data + props[i].offset, props[i].length);
	}
}

/* An empty NVRAM image in CHRP format (iokit/Kernel/IONVRAMCHRPHandler.cpp): IOKit needs one. */
#define NVRAM_BANK_SIZE 0x2000u
static uint8_t nvram_image[NVRAM_BANK_SIZE];

static void nvram_chrp_header(uint8_t *h, uint8_t sig, uint32_t bytes, const char *name)
{
	uint16_t blocks = (uint16_t)(bytes / 16), sum;

	memset(h, 0, 16);
	h[0] = sig;
	h[2] = (uint8_t)blocks;
	h[3] = (uint8_t)(blocks >> 8);
	memcpy(h + 4, name, strlen(name));
	sum = h[0];
	for (int i = 2; i < 16; i++) {
		sum += h[i];
	}
	while (sum > 0xff) {
		sum = (sum & 0xff) + (sum >> 8);
	}
	h[1] = (uint8_t)sum;
}

static void build_nvram_image(uint8_t *image)
{
	uint32_t lo = 1, hi = 0, generation = 1;

	memset(image, 0, NVRAM_BANK_SIZE);
	nvram_chrp_header(image, 0x5a, 0x20, "nvram");
	nvram_chrp_header(image + 0x20, 0x70, NVRAM_BANK_SIZE - 0x20, "common");
	memcpy(image + 20, &generation, 4);
	for (uint32_t i = 20; i < NVRAM_BANK_SIZE; i++) {
		lo = (lo + image[i]) % 65521;
		hi = (hi + lo) % 65521;
	}
	uint32_t adler = (hi << 16) | lo;
	memcpy(image + 16, &adler, 4);
}

/* Panic log region (the device tree's "pram"): DRAM outside XNU's window, kept across a reset. */
#ifndef PANIC_LOG_BASE
#define PANIC_LOG_BASE      0x80100000ULL
#endif
#ifndef PANIC_LOG_SIZE
#define PANIC_LOG_SIZE      0x80000u
#endif

/*
 * The identity the kernel is told it is running on, and the name this board goes by in this tree.
 * A board renames itself in board.h; the values here are what the x6826b build uses. The kernel
 * cares about the shape of these (a known platform, a product with artwork), not about which phone
 * they name -- there is no Apple check to pass, and none of it can match the real silicon anyway.
 */
#ifndef BOARD_NAME
#define BOARD_NAME          "x6826b"
#endif
#ifndef BOARD_TITLE
#define BOARD_TITLE         "X6826B (MT6768)"
#endif
#ifndef BOARD_PLATFORM_NAME
#define BOARD_PLATFORM_NAME "t8015"
#endif
#ifndef BOARD_MODEL_NUMBER
#define BOARD_MODEL_NUMBER  "MQAC2"
#endif
#ifndef BOARD_REGULATORY_MODEL_NUMBER
#define BOARD_REGULATORY_MODEL_NUMBER "A1865"
#endif
#ifndef BOARD_CHIP_ID
#define BOARD_CHIP_ID       0x8015
#endif
#ifndef BOARD_BOARD_ID
#define BOARD_BOARD_ID      0x06
#endif

/* As xnuloader/loader.c builds it for x6826b, minus what needs UEFI. */
static uint32_t build_device_tree(uint64_t dt_pa, uint64_t rd_pa, uint64_t rd_size,
    uint64_t sys_pa, uint64_t sys_size)
{
	struct dt_writer w = { .base = (uint8_t *)dt_pa, .cur = (uint8_t *)dt_pa, .depth = 0 };
	uint64_t cntfrq = dbg_freq();
	uint8_t seed[64];

	for (unsigned i = 0; i < sizeof(seed); i++) {
		seed[i] = (uint8_t)(dbg_ticks() >> (i & 7)) ^ (uint8_t)(i * 37);
	}

	dt_begin(&w, "device-tree");
	dt_identity(&w, identity_root, sizeof(identity_root) / sizeof(identity_root[0]), identity_root_data);
	dt_prop_str(&w, "platform-name", BOARD_PLATFORM_NAME);
	dt_prop_padded(&w, "serial-number", XNU_SERIAL, 32);
	dt_prop_padded(&w, "mlb-serial-number", XNU_SERIAL "MLB", 32);
	dt_prop_padded(&w, "model-number", BOARD_MODEL_NUMBER, 32);
	dt_prop_padded(&w, "region-info", "LL/A", 32);
	dt_prop_padded(&w, "regulatory-model-number", BOARD_REGULATORY_MODEL_NUMBER, 32);
	dt_prop_u32(&w, "#address-cells", 2);
	dt_prop_u32(&w, "#size-cells", 2);

	dt_begin(&w, "chosen");
	dt_prop_str(&w, "darwin-board", BOARD_NAME);
	dt_prop(&w, "random-seed", seed, sizeof(seed));
	dt_prop_u64(&w, "dram-base", XNU_PHYS_BASE);
	dt_prop_u64(&w, "dram-size", XNU_MEM_SIZE);
	dt_prop_u32(&w, "debug-enabled", 1);
	dt_prop_u32(&w, "display-scale", 2);
	dt_prop_u32(&w, "display-rotation", 0);
	dt_prop_str(&w, "psci-method", "smc");
	static const uint8_t wifi_address[6] = { 0x02, 0x52, 0x44, 0x57, 0x00, 0x01 };
	static const uint8_t bluetooth_address[6] = { 0x02, 0x52, 0x44, 0x57, 0x00, 0x02 };
	static const uint8_t manifest_hash[48] = { 0x72, 0x65, 0x64, 0x77, 0x6f, 0x6f, 0x64 };
	static const uint8_t no_behavior[16];
	dt_prop_u64(&w, "unique-chip-id", 0x000052445730001ULL);
	dt_prop_u32(&w, "chip-id", BOARD_CHIP_ID);
	dt_prop_u32(&w, "board-id", BOARD_BOARD_ID);
	dt_prop(&w, "mac-address-wifi0", wifi_address, sizeof(wifi_address));
	dt_prop(&w, "mac-address-bluetooth0", bluetooth_address, sizeof(bluetooth_address));
	dt_prop(&w, "boot-manifest-hash", manifest_hash, sizeof(manifest_hash));
	dt_prop(&w, "software-behavior", no_behavior, sizeof(no_behavior));
	dt_prop_str(&w, "firmware-version", "iBoot-8422.100.650");
	dt_prop_u32(&w, "certificate-production-status", 1);
	dt_prop_u32(&w, "certificate-security-mode", 1);
	dt_prop_u32(&w, "effective-production-status-ap", 1);
	dt_prop_u32(&w, "effective-security-mode-ap", 1);
	dt_prop_u32(&w, "secure-boot", 1);
	dt_prop_u32(&w, "production-cert", 1);
	dt_prop_u32(&w, "development-cert", 0);
	dt_prop_u32(&w, "embedded-panic-log-size", PANIC_LOG_SIZE);
	build_nvram_image(nvram_image);
	dt_prop_u32(&w, "nvram-bank-size", NVRAM_BANK_SIZE);
	dt_prop_u32(&w, "nvram-bank-count", 1);
	dt_prop_u32(&w, "nvram-current-bank", 0);
	dt_prop(&w, "nvram-proxy-data", nvram_image, sizeof(nvram_image));
	/*
	 * "RAMDisk" becomes /dev/md0, which is the root; "RAMDisk1" becomes /dev/md1. With the big
	 * system image present it is the root, and the payload's small one is the second disk --
	 * the same way round as xnuloader has it.
	 */
	dt_begin(&w, "memory-map");
	if (sys_size != 0) {
		uint64_t sys_reg[2] = { sys_pa, sys_size };
		dt_prop(&w, "RAMDisk", sys_reg, sizeof(sys_reg));
		if (rd_size != 0) {
			uint64_t rd_reg[2] = { rd_pa, rd_size };
			dt_prop(&w, "RAMDisk1", rd_reg, sizeof(rd_reg));
		}
	} else if (rd_size != 0) {
		uint64_t rd_reg[2] = { rd_pa, rd_size };
		/* (with the system image on the eMMC the payload stays md1, where stage0 mounts it) */
		dt_prop(&w, XNU_ROOT_EMMC ? "RAMDisk1" : "RAMDisk", rd_reg, sizeof(rd_reg));
	}
	dt_end(&w);
	dt_end(&w);

	dt_begin(&w, "defaults");
	dt_prop_u32(&w, "kern.vm_compressor", 2);
	dt_end(&w);

	dt_begin(&w, "product");
	dt_identity(&w, identity_product, sizeof(identity_product) / sizeof(identity_product[0]), identity_product_data);
	dt_begin(&w, "haptics");
	dt_identity(&w, identity_haptics, sizeof(identity_haptics) / sizeof(identity_haptics[0]), identity_haptics_data);
	dt_end(&w);
	dt_end(&w);

	dt_begin(&w, "options");
	dt_end(&w);

	uint64_t pram_reg[2] = { PANIC_LOG_BASE, PANIC_LOG_SIZE };
	memset((void *)PANIC_LOG_BASE, 0, PANIC_LOG_SIZE);
	dt_begin(&w, "pram");
	dt_prop(&w, "reg", pram_reg, sizeof(pram_reg));
	dt_end(&w);

	dt_begin(&w, "cpus");
	for (int i = 0; i < XNU_CORES; i++) {
		char name[5] = { 'c', 'p', 'u', (char)('0' + i), 0 };
		int core = core_order[i];

		dt_begin(&w, name);
		dt_prop_str(&w, "device_type", "cpu");
		dt_prop_str(&w, "state", i == 0 ? "running" : "waiting");
		dt_prop_u32(&w, "reg", BOARD_CPU_REG(core));
		dt_prop_u32(&w, "cpu-id", (uint32_t)i);
		dt_prop_u64(&w, "timebase-frequency", cntfrq);
		dt_prop_u64(&w, "fixed-frequency", cntfrq);
		dt_prop_u64(&w, "clock-frequency", 1800000000ULL);
		dt_prop_u64(&w, "bus-frequency", 100000000ULL);
		dt_prop_u64(&w, "memory-frequency", 100000000ULL);
		dt_prop_u64(&w, "peripheral-frequency", 24000000ULL);
		if (BOARD_SMALL_CORE(core)) {
			dt_prop_u32(&w, "small-core", 1);
		}
		dt_end(&w);
	}
	dt_end(&w);

	dt_begin(&w, "arm-io");
	dt_prop_str(&w, "device_type", BOARD_NAME);
	uint64_t ranges[3] = { 0, ARM_IO_BASE, ARM_IO_SIZE };
	dt_prop(&w, "ranges", ranges, sizeof(ranges));
	dt_prop_u32(&w, "#address-cells", 2);
	dt_prop_u32(&w, "#size-cells", 2);

	dt_begin(&w, "gic");
	dt_prop_str(&w, "device_type", "interrupt-controller");
	dt_prop_str(&w, "compatible", "arm,gic-v3");
	uint64_t gic_reg[4] = { GICD_BASE - ARM_IO_BASE, GICD_SIZE,
	    GICR_BASE - ARM_IO_BASE, GICR_SIZE };
	dt_prop(&w, "reg", gic_reg, sizeof(gic_reg));
	dt_end(&w);

	/* The console the kernel is told about; its driver is patched in run_xnu(). */
	dt_begin(&w, "uart0");
	dt_prop_str(&w, "device_type", "uart");
	dt_prop_str(&w, "compatible", "mediatek,mt6577-uart");
	uint64_t uart_reg[2] = { UART_BASE - ARM_IO_BASE, 0x1000 };
	dt_prop(&w, "reg", uart_reg, sizeof(uart_reg));
	dt_prop_u32(&w, "boot-console", 1);
	dt_end(&w);

	dt_end(&w);
	dt_end(&w);
	return (uint32_t)(w.cur - w.base);
}

/* ---- leaving: Android, or a reset ---- */

static void __attribute__((noreturn)) reset_board(void)
{
	psci_system_reset();
	wdt_reset();
}

#if TOUCH_PROBE
static void touch_hand_back(void);
#endif

static void __attribute__((noreturn)) chain_android(void)
{
	const volatile uint64_t *hdr = (const volatile uint64_t *)_start;
	const volatile uint32_t *img = (const volatile uint32_t *)((uint64_t)_start + CHAIN_OFFSET);
	/*
	 * res2 is the length of Android's Image, exactly as it sits at CHAIN_OFFSET. It is not padded
	 * here: 42201364 on this board is not a multiple of eight, and the copy in chain_stub moves
	 * the remainder by bytes. Rounding up would read past the end of the blob for no gain.
	 */
	uint64_t image_size = hdr[5]; /* res2 */

	marker_set(0);
	if (image_size == 0 || img[14] != 0x644d5241u) { /* "ARM\x64" */
		out("no Android kernel in this image: reset\n");
		dbg_delay_ms(3000);
		reset_board();
	}
	/*
	 * The head of an arm64 Image, when it carries the EFI stub Linux builds these with, is
	 *     code0: add x13, x18, #0x16
	 *     code1: b   <a few words on>
	 * so the branch is the second word, not the first: 0x91005a4d then 0x148000xx is what a good
	 * blob starts with. A copy that lost its first bytes -- the failure this used to have -- does
	 * not look like that, and jumping into it is a hang with no way out on this board. Say so and
	 * stay here instead; the loader's own screen is more useful than a dead phone.
	 */
	if (!((img[0] == 0x91005a4du && (img[1] & 0xfc000000u) == 0x14000000u) ||
	      (img[0] & 0xfc000000u) == 0x14000000u)) {
		out("the Android kernel at ");
		out_hex((uint64_t)(uintptr_t)img);
		out(" does not start with a branch: not jumping\n");
		/*
		 * Left up on purpose rather than reset: a reset would land back in this same loader and
		 * this same image, so it would loop with the screen flashing and nothing to read. A
		 * message that stays put says what is wrong, and the fix is a reflash either way.
		 */
		for (;;) {
			dbg_delay_ms(1000);
		}
	}
#if TOUCH_PROBE
	touch_hand_back();
#endif
	out("starting Android\n");

	volatile uint32_t *dst = (volatile uint32_t *)CHAIN_STUB_PA;
	const volatile uint32_t *src = (const volatile uint32_t *)chain_stub;
	for (uint64_t i = 0; i < (uint64_t)(chain_stub_end - chain_stub) / 4; i++) {
		dst[i] = src[i];
	}
	__asm__ volatile("dsb sy; ic iallu; dsb sy; isb" ::: "memory");
	void (*stub)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t) = (void *)CHAIN_STUB_PA;
	stub(lk_dtb, KERNEL_PA, (uint64_t)img, image_size, KERNEL_PA);
	__builtin_unreachable();
}

void loader_fault(uint64_t vec, uint64_t esr, uint64_t elr, uint64_t far)
{
	fbc.fg = 0xffff4040u;
	out("\n*** loader fault: vector ");
	out_hex(vec);
	out(" ESR ");
	out_hex(esr);
	out(" ELR ");
	out_hex(elr);
	out(" FAR ");
	out_hex(far);
	out("\n");
	dbg_delay_ms(5000);
	/*
	 * Stop here. This used to call chain_android() on the theory that the Android kernel is the
	 * one path known to work, but a fault raised *inside* chain_android() -- which is what the
	 * xaga does, FAR 0x8a -- calls this function again, and the screen fills with the same two
	 * lines forever: nothing resets this board, so an unattended phone stayed like that all
	 * night. A halt is the honest outcome, and the watchdog below is the only thing measured to
	 * bring the board back (see wdt_reset in mtkdbg.h), so it gets armed to do that.
	 */
	out("halting; the watchdog will restart the board\n");
	wdt_arm(8);
	for (;;) {
		__asm__ volatile ("wfe");
	}
}

/* ---- XNU's log into ramoops ---- */

struct span {
	const volatile char *p;
	uint64_t n;
};

/* Append spans (in order) to the ramoops console: all of it if it fits, else its start and
 * as much of its end as there is room for. */
static void save_log(const struct span *sp, int nsp)
{
	uint64_t total = 0, room, head, tail, skip_to, at = 0;

	for (int i = 0; i < nsp; i++) {
		total += sp[i].n;
	}
	room = PLOG_CAP - PLOG_HDR->size;
	room = room > 1024 ? room - 1024 : 0;
	head = total <= room ? total : room / 4;
	tail = total <= room ? 0 : room - head;
	skip_to = total - tail;

	uint64_t fb = fbc.base;
	fbc.base = 0; /* the screen gets the tail, separately */
	for (int i = 0; i < nsp; i++) {
		for (uint64_t k = 0; k < sp[i].n; k++, at++) {
			if (at < head || at >= skip_to) {
				char c = sp[i].p[k];
				plog_putc(c ? c : ' ');
			} else if (at == head) {
				dbg_puts("\n\n[... ");
				dbg_dec(skip_to - head);
				dbg_puts(" bytes of the XNU log left out here ...]\n\n");
			}
		}
	}
	plog_putc('\n');
	fbc.base = fb;
}

/*
 * The kernel's lines for the host (kern.redwood.host_keep, bsd/dev/redwood_mali.c) into ramoops' pmsg
 * zone, whole: Android shows them as /sys/fs/pstore/pmsg-ramoops-0, and tools/xnu-cycle.sh appends them
 * to the run's log. Called on each way back from XNU, after the boot request in that zone was taken.
 */
static void save_host_keep(const struct xnu_syms *s)
{
	static const char head[] = "=== kept for the host by the kernel: ";
	volatile struct ramoops_hdr *h = (volatile struct ramoops_hdr *)RAMOOPS_PMSG;
	volatile char *d = (volatile char *)(RAMOOPS_PMSG + sizeof(struct ramoops_hdr));
	uint32_t cap = RAMOOPS_PMSG_SIZE - sizeof(struct ramoops_hdr), at = 0, len, dropped;
	char num[12];
	int n;

	uint64_t keep_va = s->host_keep ? s->host_keep : s->hk_keep;
	uint64_t len_va = s->host_keep ? s->host_keep_len : s->hk_keep_len;
	uint64_t dropped_va = s->host_keep ? s->host_keep_dropped : s->hk_keep_dropped;

	if (!keep_va || !len_va) {
		return;
	}
	len = rd32(kva_to_pa(len_va));
	dropped = dropped_va ? rd32(kva_to_pa(dropped_va)) : 0;
	for (const char *c = head; *c; c++) {
		d[at++] = *c;
	}
	for (int part = 0; part < 2; part++) {
		uint32_t v = part ? dropped : len;
		n = 0;
		do {
			num[n++] = (char)('0' + v % 10);
			v /= 10;
		} while (v);
		while (n) {
			d[at++] = num[--n];
		}
		const char *tail = part ? " dropped) ===\n" : " bytes (";
		for (const char *c = tail; *c; c++) {
			d[at++] = *c;
		}
	}
	const volatile char *keep = (const volatile char *)kva_to_pa(keep_va);
	for (uint32_t i = 0; i < len && at < cap; i++) {
		d[at++] = keep[i];
	}
	h->sig = RAMOOPS_SIG;
	h->start = at;          /* not wrapped: Linux takes data[start..size) then data[0..start) */
	h->size = at;
	dcache_clean_range(RAMOOPS_PMSG, RAMOOPS_PMSG_SIZE);
	out("host_keep: ");
	out_dec(len);
	out(" bytes into pmsg, ");
	out_dec(dropped);
	out(" lines dropped\n");
}

/* The last lines of what is in ramoops, on the lower part of the screen. */
static void show_log_tail(uint32_t lines)
{
	uint32_t size = PLOG_HDR->size, start = PLOG_HDR->start, n = 0;
	uint32_t top = PANEL_H - (lines + 1) * 16;
	uint64_t i;

	if (!fbc.base || size == 0 || size != start) { /* only a log that has not wrapped */
		return;
	}
	for (i = size; i > 0 && n <= lines; i--) {
		n += PLOG_DATA[i - 1] == '\n';
	}
	fb_rect(0, top - 4, PANEL_W, PANEL_H - top + 4, 0xff000000u);
	fb_rect(0, top - 4, PANEL_W, 2, 0xffff3030u);
	fbc.top = fbc.y = top;
	fbc.x = 0;
	fbc.scale = 1;
	for (; i < size; i++) {
		fbcon_putc(PLOG_DATA[i]);
	}
}

/* ---- the kernel's log, on the screen in pages that hold still ---- */

/*
 * A photograph of a scrolling 8x16 console is worthless: the camera catches one line of a page
 * that has already gone by and the interesting line is off the frame. It also never worked here
 * for a second reason -- this board is built with BOARD_NO_PLOG, so plog_putc() is empty and
 * show_log_tail() has nothing to draw. What xnu_returned() *is* handed is the kernel's own
 * linearized log, in RAM, with its length: that is the log, and this draws it.
 *
 * Pages hold for XNU_REPORT_DWELL_MS so there is time for a picture, and the last page stays up
 * for good -- no reset after it, so nothing is lost while the phone is put down.
 */
#ifndef XNU_REPORT_SCALE
#define XNU_REPORT_SCALE        3
#endif
#ifndef XNU_REPORT_DWELL_MS
#define XNU_REPORT_DWELL_MS     20000u
#endif
#ifndef XNU_REPORT_MAX_KB
#define XNU_REPORT_MAX_KB       6
#endif

/*
 * Whether the report holds the screen for ever after its last page. It does when the loader has
 * nothing left to do (the kernel came back and the phone is finished); it does not when the
 * report is a message on the way to somewhere else, as with the watcher's record on a MARK_DONE
 * boot, where the phone still has to be handed back to Android so the logbook can be read.
 */
static int report_park = 1;

/*
 * Whether the report pages through the log at all. Paging is for a person standing over the
 * phone; when nobody is, it is only a delay and a way to not reach the code after it. A page
 * holds PANEL_W/(8*scale) * PANEL_H/(16*scale) characters -- 2295 at scale 3 -- and each is held
 * for XNU_REPORT_DWELL_MS, so this board's 400 KB log is 172 pages and fifty-seven minutes.
 *
 * Set 0 on a board that writes the log to the flash and then hands the phone somewhere else
 * (BOARD_RETURN_TO_ANDROID): the text is in the logbook the whole time, and show_log_tail() puts
 * the last lines on the screen anyway.
 */
#ifndef BOARD_REPORT_PAGES
#define BOARD_REPORT_PAGES      1
#endif

/* ---- the log, into the flash, where adb can come and get it ---- */

/*
 * The screen is a bad place to keep a log: it holds one page, it needs a photograph, and the
 * phone gets reset between runs. The flash holds it instead, in the tail of the partition the
 * system image lives in and which Android never mounts. After the phone is put back on its
 * Android boot, root can read it out:
 *
 *   su -c 'dd if=/dev/block/by-name/xnusys of=/data/local/tmp/log.bin bs=4096 skip=SN'
 *
 * where SN is the byte offset below divided by 4096. The header says how long the log is and
 * which run it belongs to, so nothing has to be guessed.
 */
#ifndef XNU_LOGBOOK_SLACK
#define XNU_LOGBOOK_SLACK       (8u << 20)   /* reserved at the end of SYSTEM_PART */
#endif
#ifndef XNU_LOGBOOK_MAX
#define XNU_LOGBOOK_MAX         (4u << 20)   /* more than this and it is a runaway log */
#endif
#define XNU_LOGBOOK_MAGIC       0x474f4c48u  /* "HLOG" */
#define XNU_LOGBOOK_RESCUE      0x52534355u  /* recovered kernel log, already in time order */

/* A later power-down probe must not replace a successfully saved kernel log. */
static int logbook_kernel_saved;

struct logbook_hdr {
	uint32_t magic;
	uint32_t version;
	uint64_t len;          /* bytes of log that follow the header block */
	uint64_t reason;       /* the same four the loader was handed */
	uint64_t code;
	uint64_t elr;
	uint64_t far;
	uint64_t ticks;        /* CNTPCT at the moment of the write */
	uint64_t where;        /* absolute byte offset of the log itself */
	uint64_t sum;          /* 64-bit additive checksum of the log */
};

static uint64_t logbook_offset(void)
{
	uint64_t lba, blocks, size;

	if (!gpt_find(SYSTEM_PART, &lba, &blocks)) {
		return 0;
	}
	size = blocks * (uint64_t)gpt_block_size;
	if (size < (uint64_t)XNU_LOGBOOK_SLACK + 65536ull) {
		return 0;
	}
	return lba * (uint64_t)gpt_block_size + size - (uint64_t)XNU_LOGBOOK_SLACK;
}

/* Write chronological spans without allocating another copy of the kernel's ring. */
static int save_log_spans_to_flash(const struct span *sp, int nsp, uint64_t reason,
				  uint64_t code, uint64_t elr, uint64_t far)
{
#if BOARD_STORAGE_UFS
	static union {
		struct logbook_hdr h;
		uint8_t block[4096];
	} record __attribute__((aligned(64)));
	static uint8_t last[4096] __attribute__((aligned(64)));
	struct logbook_hdr *h = &record.h;
	uint64_t at = logbook_offset();
	uint64_t total = 0, len, skip, written = 0, sum = 0;
	uint32_t used = 0;

	if (at == 0 || nsp <= 0) {
		return 0;
	}
	for (int i = 0; i < nsp; i++) {
		if ((!sp[i].p && sp[i].n) || sp[i].n > ~0ull - total) {
			return 0;
		}
		total += sp[i].n;
	}
	len = total > (uint64_t)XNU_LOGBOOK_MAX ? (uint64_t)XNU_LOGBOOK_MAX : total;
	if (len == 0) {
		return 0;
	}
	skip = total - len; /* keep the tail when the log exceeds the reserved capacity */
	for (int i = 0; i < nsp; i++) {
		const volatile uint8_t *p = (const volatile uint8_t *)sp[i].p;
		uint64_t n = sp[i].n;

		if (skip >= n) {
			skip -= n;
			continue;
		}
		p += skip;
		n -= skip;
		skip = 0;
		while (n) {
			if (used == 0 && n >= 4096u) {
				uint64_t whole = n & ~4095ull;

				for (uint64_t k = 0; k < whole; k++) {
					sum += p[k];
				}
				if (!ufs_write((at + 4096ull + written) / 512ull,
						(uint32_t)(whole / 512ull), (const void *)p)) {
					goto body_failed;
				}
				written += whole;
				p += whole;
				n -= whole;
			} else {
				uint32_t take = n < 4096u - used ? (uint32_t)n : 4096u - used;

				for (uint32_t k = 0; k < take; k++) {
					uint8_t v = p[k];
					last[used++] = v;
					sum += v;
				}
				p += take;
				n -= take;
				if (used == 4096u) {
					if (!ufs_write((at + 4096ull + written) / 512ull, 8u, last)) {
						goto body_failed;
					}
					written += 4096u;
					used = 0;
				}
			}
		}
	}
	if (used) {
		for (uint32_t i = used; i < 4096u; i++) {
			last[i] = 0;
		}
		if (!ufs_write((at + 4096ull + written) / 512ull, 8u, last)) {
			goto body_failed;
		}
	}
	/* Commit the header after the body. The entire header block belongs to this object. */
	for (uint32_t i = 0; i < sizeof(record.block); i++) {
		record.block[i] = 0;
	}
	h->magic = XNU_LOGBOOK_MAGIC;
	h->version = 1;
	h->len = len;
	h->reason = reason;
	h->code = code;
	h->elr = elr;
	h->far = far;
	h->ticks = dbg_ticks();
	h->where = at + 4096ull;
	h->sum = sum;
	if (!ufs_write(at / 512ull, 8u, record.block)) {
		out("logbook: header write failed: ");
		out(board_storage_what());
		out("\n");
		return 0;
	}
	out("logbook: ");
	out_dec(len / 1024u);
	out(" KB at byte ");
	out_hex(at);
	out(" of " SYSTEM_PART "\n");
	return 1;
body_failed:
	out("logbook: body write failed: ");
	out(board_storage_what());
	out("\n");
	return 0;
#else
	(void)sp; (void)nsp; (void)reason; (void)code; (void)elr; (void)far;
	return 0;
#endif
}

static int save_log_to_flash(uint64_t pa, uint64_t len, uint64_t reason, uint64_t code,
			     uint64_t elr, uint64_t far)
{
	const struct span sp = { (const volatile char *)(uintptr_t)pa, len };

	return save_log_spans_to_flash(&sp, 1, reason, code, elr, far);
}

/*
 * A second record, written as soon as the loader knows where it is going, before XNU is entered.
 * Two reasons it exists separately from save_log_to_flash(): it proves the flash write path works
 * on a board where WRITE(10) has never been tried, and it does so in the first minute instead of
 * after the kernel has had its four. If the later record is missing but this one is there, the
 * write path is fine and the kernel never came back.
 */
/* Defined further down, next to the rest of the bring-up reporting. */
static uint32_t cpu_mhz(void);

static void logbook_boot_note(void)
{
#if BOARD_STORAGE_UFS
	/*
	 * What the kernel is told, and what the cores were actually running at when it was told
	 * that. The frequency question cannot be answered from inside the kernel: by then the
	 * value it measures is its own, and a number that low looks the same whether LK left the
	 * PLL there or something below us moved it. Here the cycle counter and the PLL registers
	 * are read in the same breath, on the same core, before any of that can happen.
	 *
	 * The register layout is the one the vendor's cpuhvfs uses (mt6895.dts): ARMPLL_LL/BL/B/DSU
	 * live at apmixedsys + 0x20c/0x21c/0x22c/0x23c, with the post divider in bits 24..26 and
	 * the feedback word in bits 0..21, against a 26 MHz reference. The clock dividers those
	 * PLLs feed are in mcusys_ao_cfg + 0xa2a0/0xa2a4/0xa2a8/0xa2e0.
	 */
	static char buf[768];
	uint32_t n = 0;
	const char *text = "mtkloader: about to enter XNU\nboot args: " XNU_BOOT_ARGS "\n";
	const uint64_t pll[4] = { 0x20c, 0x21c, 0x22c, 0x23c };
	const char *names[4] = { "LL(A55)", "BL(A78)", "B(A78)", "DSU" };

	while (*text && n < sizeof(buf) - 1u) {
		buf[n++] = *text++;
	}

	{
		uint32_t mhz = cpu_mhz();

		text = "clock: this core measures ";
		while (*text && n < sizeof(buf) - 1u) {
			buf[n++] = *text++;
		}
		{
			char digits[12];
			uint32_t d = 0, v = mhz;

			if (v == 0) {
				digits[d++] = '0';
			}
			while (v != 0) {
				digits[d++] = (char)('0' + (v % 10u));
				v /= 10u;
			}
			while (d != 0) {
				buf[n++] = digits[--d];
			}
		}
		text = " MHz\n";
		while (*text && n < sizeof(buf) - 1u) {
			buf[n++] = *text++;
		}
	}

	for (uint32_t i = 0; i < 4; i++) {
		uint32_t con = rd32(APMIXED_BASE + pll[i]);
		uint32_t post = (con >> 24) & 7u;
		uint32_t pcw = con & 0x3fffffu;
		uint32_t div = rd32(0x0c530000ull + 0xa2a0u + i * 4u) & 0x1fu;
		uint64_t mhz = post ? (((26ull * pcw) >> 14) >> post) : 0;

		text = "clock: ";
		while (*text && n < sizeof(buf) - 1u) {
			buf[n++] = *text++;
		}
		for (const char *w = names[i]; *w && n < sizeof(buf) - 1u; w++) {
			buf[n++] = *w;
		}
		{
			const char *w = " con ";
			while (*w && n < sizeof(buf) - 1u) {
				buf[n++] = *w++;
			}
		}
		{
			const char *hex = "0123456789abcdef";
			for (int sh = 28; sh >= 0; sh -= 4) {
				buf[n++] = hex[(con >> sh) & 0xfu];
			}
		}
		{
			char digits[12];
			uint32_t d = 0, v = (uint32_t)mhz;

			text = " pll ";
			while (*text && n < sizeof(buf) - 1u) {
				buf[n++] = *text++;
			}
			if (v == 0) {
				digits[d++] = '0';
			}
			while (v != 0) {
				digits[d++] = (char)('0' + (v % 10u));
				v /= 10u;
			}
			while (d != 0) {
				buf[n++] = digits[--d];
			}
			text = " MHz div ";
			while (*text && n < sizeof(buf) - 1u) {
				buf[n++] = *text++;
			}
			d = 0;
			v = div;
			if (v == 0) {
				digits[d++] = '0';
			}
			while (v != 0) {
				digits[d++] = (char)('0' + (v % 10u));
				v /= 10u;
			}
			while (d != 0) {
				buf[n++] = digits[--d];
			}
			buf[n++] = '\n';
		}
	}
	buf[n] = 0;
	(void)save_log_to_flash((uint64_t)(uintptr_t)buf, n, 0xfeedfaceull, 0, 0, 0);
#endif
}

static void report_page_clear(void)
{
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
}

static void report_kernel_log(uint64_t pa, uint64_t len)
{
	const volatile char *p = (const volatile char *)pa;
	uint32_t scale = XNU_REPORT_SCALE;
	uint32_t cols = PANEL_W / (8u * scale);
	uint32_t rows = PANEL_H / (16u * scale);
	uint32_t lh = 16u * scale, cw = 8u * scale;
	uint32_t col = 0, row = 0, page = 1;
	uint64_t from;

	if (!pa || len == 0) {
		return;
	}
#if !BOARD_REPORT_PAGES
	/*
	 * Nobody is going to page through this: leave the screen alone and let the caller get on
	 * with what it has to do. show_log_tail() draws the last lines, and the whole text is in
	 * the logbook in the flash.
	 */
	return;
#endif
	/* the reason a boot stops is at the end of its log, so the tail is what is shown */
	if (len > (uint64_t)XNU_REPORT_MAX_KB * 1024u) {
		from = len - (uint64_t)XNU_REPORT_MAX_KB * 1024u;
	} else {
		from = 0;
	}

	report_page_clear();
	fbc.top = 0;
	fbc.x = 0;
	fbc.y = 0;
	fbc.scale = scale;
	fbc.fg = 0xffffffffu;
	fbc.bg = 0xff000000u;

	for (uint64_t i = from; i < len; i++) {
		char c = p[i];

		if (c == '\r') {
			continue;
		}
		if (c == '\n') {
			col = 0;
			row++;
		} else {
			if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) {
				c = '?';
			}
			fbc.x = col * cw;
			fbc.y = row * lh;
			fbcon_putc(c);
			if (++col >= cols) {
				col = 0;
				row++;
			}
		}
		if (row >= rows) {
			/* this page is full: hold it, then start the next one */
			dbg_delay_ms(XNU_REPORT_DWELL_MS);
			report_page_clear();
			fbc.scale = scale;
			fbc.x = 0;
			fbc.y = 0;
			row = 0;
			col = 0;
			page++;
		}
	}
	(void)page;
	if (report_park) {
		/* the last page is left on the screen; the phone is reset by hand */
		for (;;) {
			dbg_delay_ms(1000);
		}
	}
	/* they have all been up for their dwell, and the caller has somewhere to be */
	dbg_delay_ms(XNU_REPORT_DWELL_MS);
}

/*
 * The watcher left its text in DRAM. Take it out: put it in the logbook at the end of xnusys so
 * that a shell on Android can read the whole thing with mtk/tools/xaga-pull-log.sh, and leave it
 * on the screen a page at a time so that looking at the phone also tells the story.
 *
 * Called before anything else in mtkloader_main(), because it is the one thing this run knows
 * that the last one could not say. The caller decides whether to keep going afterwards.
 */
static int snapshot_take(void)
{
	struct snap_hdr *h = (struct snap_hdr *)SNAP_PA;
	uint64_t len = h->len;
	char *text = (char *)(SNAP_PA + sizeof(struct snap_hdr));

	if (h->magic != SNAP_MAGIC || len == 0 || len > SNAP_SIZE - sizeof(struct snap_hdr)) {
		return 0;
	}
	/* a reset can flip bits; the header is a copy, so read it again after the text is proven sane */
	dcache_clean_range(SNAP_PA, (uint64_t)len + sizeof(struct snap_hdr) + 64u);

	/* the tail is where the reason is, and the logbook holds at most XNU_LOGBOOK_MAX */
	if (len > (uint64_t)XNU_LOGBOOK_MAX) {
		text += len - (uint64_t)XNU_LOGBOOK_MAX;
		len = (uint64_t)XNU_LOGBOOK_MAX;
	}
	out("\n=== the watcher left a record in DRAM (");
	out_dec(len);
	out(" bytes) ===\n");
	if (!save_log_to_flash((uint64_t)(uintptr_t)text, len, 2u /* checkpoint */, h->ticks, 0, 0)) {
		/* the flash write did not take: leave the magic alone so the next boot tries again */
		report_kernel_log((uint64_t)(uintptr_t)text, len);
		return 1;
	}
	logbook_kernel_saved = 1;
	/*
	 * It is in the logbook now, which is where it is read from -- clear the magic so that an
	 * ordinary boot into Android does not show a record from a run that ended days ago. If a
	 * reset lands in the middle of that, the record is still in flash and only the screen is
	 * repeated once.
	 */
	h->magic = 0;
	dcache_clean_range(SNAP_PA, 64);
	report_kernel_log((uint64_t)(uintptr_t)text, len);
	return 1;
}

/*
 * A kernel virtual address to physical through XNU's own page tables (TTBR1_EL1; 4K or 16K
 * granule), as they were when it came back. 0 if not mapped.
 */
static uint64_t xnu_va_to_pa(uint64_t va)
{
	uint64_t tcr = xnu_fp[2], ttbr1 = xnu_fp[3];
	uint32_t t1sz = (tcr >> 16) & 0x3f, tg1 = (tcr >> 30) & 3;
	uint32_t g = tg1 == 2 ? 12 : tg1 == 1 ? 14 : 0;
	if (!g || t1sz < 16) {
		return 0;
	}
	uint32_t vabits = 64 - t1sz, per = g - 3;
	uint32_t levels = (vabits - g + per - 1) / per;
	uint64_t table = ttbr1 & 0x0000fffffffffffeULL & ~((1ULL << g) - 1);
	for (uint32_t l = 0; l < levels; l++) {
		uint32_t shift = g + per * (levels - 1 - l);
		uint64_t idx = (va >> shift) & ((1ULL << per) - 1);
		if (l == 0) {
			idx &= (1ULL << (vabits - shift)) - 1;
		}
		uint64_t d = *(volatile uint64_t *)(table + idx * 8);
		if (!(d & 1)) {
			return 0;
		}
		uint64_t oa = d & 0x0000ffffffffffffULL & ~((1ULL << g) - 1);
		if (l == levels - 1 || !(d & 2)) { /* page, or block */
			uint64_t span = 1ULL << shift;
			return (oa & ~(span - 1)) | (va & (span - 1));
		}
		table = oa;
	}
	return 0;
}

static uint64_t xnu_rd64(uint64_t va)
{
	uint64_t pa = xnu_va_to_pa(va);
	return pa && !(pa & 7) ? *(volatile uint64_t *)pa : 0;
}

static char xnu_rd8(uint64_t va)
{
	uint64_t pa = xnu_va_to_pa(va);
	return pa ? *(volatile char *)pa : 0;
}

static void out_xnu_str(uint64_t va, int max)
{
	char c;
	for (int i = 0; i < max && (c = xnu_rd8(va + i)) != 0; i++) {
		out_putc(c);
	}
}

/* AAPCS64 va_list, as debugger_panic_args points at it */
struct xnu_va_list {
	uint64_t stack, gr_top, vr_top;
	int32_t gr_offs, vr_offs;
};

static uint64_t va_next(struct xnu_va_list *ap)
{
	uint64_t v;
	if (ap->gr_offs < 0) {
		v = xnu_rd64(ap->gr_top + (int64_t)ap->gr_offs);
		ap->gr_offs += 8;
	} else {
		v = xnu_rd64(ap->stack);
		ap->stack += 8;
	}
	return v;
}

/* The panic message: debugger_panic_str formatted with debugger_panic_args (integers in hex). */
static void show_panic_string(const struct xnu_syms *x)
{
	uint64_t fmt = x->panic_str ? xnu_rd64(x->panic_str) : 0;
	uint64_t argp = x->panic_args ? xnu_rd64(x->panic_args) : 0;
	struct xnu_va_list ap = { 0 };

	if (!fmt) {
		out("(no panic string)\n");
		return;
	}
	if (argp) {
		ap.stack = xnu_rd64(argp);
		ap.gr_top = xnu_rd64(argp + 8);
		ap.vr_top = xnu_rd64(argp + 16);
		uint64_t offs = xnu_rd64(argp + 24);
		ap.gr_offs = (int32_t)offs;
		ap.vr_offs = (int32_t)(offs >> 32);
	}
	out("panic: ");
	for (int i = 0; i < 1024; i++) {
		char c = xnu_rd8(fmt + i);
		if (!c) {
			break;
		}
		if (c != '%') {
			out_putc(c);
			continue;
		}
		/* flags, width, precision, length */
		do {
			c = xnu_rd8(fmt + ++i);
		} while (c && ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == ' ' || c == '#' || c == '.' ||
		    c == 'l' || c == 'h' || c == 'z' || c == 'j' || c == 't' || c == 'q' || c == '*'));
		if (!argp && c != '%') {
			out("<?>");
			continue;
		}
		switch (c) {
		case '%':
			out_putc('%');
			break;
		case 's':
			out_xnu_str(va_next(&ap), 200);
			break;
		case 'c':
			out_putc((char)va_next(&ap));
			break;
		case 'd':
		case 'i':
		case 'u': {
			uint64_t v = va_next(&ap);
			out_dec(v);
			out("(");
			out_hex(v);
			out(")");
			break;
		}
		case 0:
			i = 1024;
			break;
		default:
			out_hex(va_next(&ap));
			break;
		}
	}
	out("\n");
}

/* The symbol at or below a kernel address, from the Mach-O symbol table: "name+0xoff". */
static void out_sym(uint64_t addr)
{
	const struct mach_header_64 *mh = (const struct mach_header_64 *)xkernel;
	const struct load_command *lc;
	const struct symtab_command *st = NULL;
	uint64_t best = 0;
	const char *best_name = NULL;

	out_hex(addr);
	if (!xkernel) {
		return;
	}
	lc = (const struct load_command *)(mh + 1);
	for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const void *)((const uint8_t *)lc + lc->cmdsize)) {
		if (lc->cmd == LC_SYMTAB) {
			st = (const void *)lc;
		}
	}
	if (!st) {
		return;
	}
	const struct nlist_64 *sym = (const struct nlist_64 *)(xkernel + st->symoff);
	const char *strs = (const char *)(xkernel + st->stroff);
	for (uint32_t i = 0; i < st->nsyms; i++) {
		if ((sym[i].n_type & 0x0e) == 0x0e && sym[i].n_value <= addr && sym[i].n_value > best) { /* N_SECT */
			best = sym[i].n_value;
			best_name = strs + sym[i].n_strx;
		}
	}
	if (best_name && addr - best < 0x100000) {
		out(" ");
		out(best_name);
		out("+");
		out_hex(addr - best);
	}
}

static void show_backtrace(void)
{
	uint64_t fp = xnu_fp[0];

	out("TCR_EL1 ");
	out_hex(xnu_fp[2]);
	out(" TTBR1_EL1 ");
	out_hex(xnu_fp[3]);
	out(" fp ");
	out_hex(fp);
	out(" -> pa ");
	out_hex(xnu_va_to_pa(fp));
	out(" sym debugger_panic_str -> pa ");
	out_hex(xnu_va_to_pa(xsyms.panic_str));
	out(" (slide: ");
	out_hex(kva_to_pa(xsyms.panic_str));
	out(")\nbacktrace:\n  lr ");
	out_sym(xnu_fp[1]);
	out("\n");
	for (int i = 0; i < 24 && fp; i++) {
		uint64_t lr = xnu_rd64(fp + 8);
		if (!lr) {
			break;
		}
		out("  ");
		out_sym(lr);
		out("\n");
		fp = xnu_rd64(fp);
	}
}

/*
 * What XNU's text console shows, read back from the framebuffer: each 8x16 cell is looked up in
 * XNU's own font (iso_font, 16 bytes a character, bit 0 the leftmost pixel; video_console.c).
 * Proof that the text reached the screen, and a copy of it.
 */
static void read_screen(void)
{
	static uint32_t font_hash[256];
	uint64_t font = xsyms.iso_font ? kva_to_pa(xsyms.iso_font) : 0;
	const uint32_t cols = PANEL_W / 8, rows = PANEL_H / 16;
	uint32_t lit_cells = 0, known = 0, last_row = 0;
	static char line[PANEL_W / 8 + 1];

	if (!font || !fb_base) {
		return;
	}
	for (int ch = 0; ch < 256; ch++) {
		uint32_t h = 0;
		for (int r = 0; r < 16; r++) {
			h = h * 0x01000193u ^ *(volatile uint8_t *)(font + ch * 16 + r);
		}
		font_hash[ch] = h;
	}
	for (uint32_t row = 0; row < rows; row++) {
		for (uint32_t col = 0; col < cols; col++) {
			uint8_t bits[16];
			uint32_t h = 0, any = 0;
			for (uint32_t r = 0; r < 16; r++) {
				const volatile uint32_t *px = (const volatile uint32_t *)(fb_base + (uint64_t)(row * 16 + r) * fb_pitch) + col * 8;
				uint8_t b = 0;
				for (uint32_t x = 0; x < 8; x++) {
					uint32_t p = px[x];
					uint32_t lum = ((p >> 16) & 0xff) + ((p >> 8) & 0xff) + (p & 0xff);
					if (lum > 3 * 0x60) {
						b |= (uint8_t)(1u << x);
					}
				}
				bits[r] = b;
				any |= b;
			}
			char out_c = ' ';
			if (any) {
				lit_cells++;
				for (int inv = 0; inv < 2 && out_c == ' '; inv++) {
					h = 0;
					for (int r = 0; r < 16; r++) {
						h = h * 0x01000193u ^ (uint8_t)(inv ? ~bits[r] : bits[r]);
					}
					for (int ch = 0x21; ch < 0x7f; ch++) {
						if (font_hash[ch] == h) {
							out_c = (char)ch;
							known++;
							break;
						}
					}
				}
				if (out_c == ' ') {
					out_c = '#';
				}
			}
			line[col] = out_c;
		}
		line[cols] = 0;
		/* trailing spaces off; blank rows kept short */
		int n = (int)cols;
		while (n > 0 && line[n - 1] == ' ') {
			n--;
		}
		line[n] = 0;
		if (n) {
			last_row = row + 1;
		}
		dbg_puts(line);
		dbg_putc('\n');
	}
	dbg_puts("[screen: ");
	dbg_dec(lit_cells);
	dbg_puts(" cells lit, ");
	dbg_dec(known);
	dbg_puts(" read as characters, last text row ");
	dbg_dec(last_row);
	dbg_puts("]\n");
}

/*
 * XNU is back (start.S xnu_return_stub): MMU off, our vectors, our stack. Its log was
 * linearized and written back to RAM by XNU. Save it, show its end, then reset into Android.
 */

/* Trial721 only. No UFS access is allowed on return before this guard. */
static void ufs_read_guard_barrier(void *unused)
{
    (void)unused;
    __asm__ volatile("dsb sy" ::: "memory");
}

static void ufs_read_guard_start(void)
{
#if defined(BOARD_UFS_READ_LEASE) && BOARD_UFS_READ_LEASE
    _Static_assert(UFS_AREA_PA == UR_LEASE_PA, "fixed lease area");
    _Static_assert(UFS_AREA_PA + UFS_UCD_OFFSET == UR_UCD_PA, "fixed UCD");
    _Static_assert(UCD_SIZE == UR_UCD_BYTES, "fixed UCD size");
    _Static_assert(UFS_AREA_PA + UFS_BUF_OFFSET == UR_DATA_PA, "fixed DMA scratch");
#if !defined(UFS_USE_LK_LIST) || UFS_USE_LK_LIST != 1
#error Trial721 requires the existing LK transfer list
#endif
    volatile struct ur_lease *r = (volatile struct ur_lease *)(uintptr_t)UR_LEASE_PA;
    r->magic = 0;
    ufs_read_guard_barrier(0);
    /* boot_note just wrote HLOG. Establish a READ predecessor without broadening
     * the kernel's whitelist. This is a loader read, separate from the kernel test.
     * ufs_read counts512-byte sectors; logical LBA1 is sector8 on this4096-byte unit.
     */
    if (!ufs_read(8u, 8u, (void *)(uintptr_t)UR_DATA_PA) ||
        ur_guard_block_crc32((const volatile uint32_t *)(uintptr_t)UR_DATA_PA,
                             UR_BLOCK_BYTES / 4) != 0x28a2836cu) {
        out("ufs-read guard: predecessor READ/reference failed; manual fastboot required\n");
        for (;;) { __asm__ volatile("wfe"); }
    }
    ur_guard_initialize(r, ufs_read_guard_barrier, 0);
    out("ufs-read guard: fresh trial721 lease; predecessor READ verified\n");
#endif
}

#if defined(BOARD_UFS_READ_LEASE) && BOARD_UFS_READ_LEASE
static struct ur_lease ufs_return_snapshot;
static char ufs_return_audit[192];
static uint32_t ufs_return_audit_n;
#endif
static void ufs_read_guard_before_storage(void)
{
#if defined(BOARD_UFS_READ_LEASE) && BOARD_UFS_READ_LEASE
    const volatile struct ur_lease *r=(const volatile struct ur_lease *)(uintptr_t)UR_LEASE_PA;
    if (!ur_guard_observe_allowed(r,ufs_read_guard_barrier,0,&ufs_return_snapshot)) {
        out("ufs-read guard: ownership NOT returned; no HLOG/Android IO; manual fastboot required\n");
        for (;;) { __asm__ volatile("wfe"); }
    }
    ufs_return_audit_n=(uint32_t)ur_guard_format_return(&ufs_return_snapshot,ufs_return_audit,sizeof(ufs_return_audit));
    if (!ufs_return_audit_n) {
        out("ufs-read guard: return audit refused; no HLOG/Android IO; manual fastboot required\n");
        for (;;) { __asm__ volatile("wfe"); }
    }
#endif
}

void xnu_returned(uint64_t log_pa, uint64_t len, uint64_t reason, uint64_t code, uint64_t elr, uint64_t far)
{
	ufs_read_guard_before_storage();
	struct span sp = { (const volatile char *)log_pa, len };

	wdt_disable();
	xnu_back = 1;
	marker_set(MARK_DONE);
	uint64_t screen = fbc.base;
	fbc.base = 0; /* nothing drawn over XNU's console before it has been read */
	dbg_puts("\n=== what the screen showed when XNU came back ===\n");
	read_screen();
	fbc.base = screen;
	fbc.fg = 0xffffff40u;
	out("\n=== XNU returned to mtkloader: reason ");
	out_dec(reason);
	out(reason == 0 ? " (end)" : reason == 1 ? " (early exception)" : reason == 2 ? " (checkpoint)" : "");
	out(" code ");
	out_hex(code);
	if (reason == 1) {
		out(" ELR ");
		out_hex(elr);
		out(" FAR ");
		out_hex(far);
	}
	out("\n");
	/* Did user space ever start? tasks_count is 1 while only the kernel's task exists. */
	if (xsyms.tasks_count) {
		out("tasks ");
		out_dec(rd32(kva_to_pa(xsyms.tasks_count)));
		if (xsyms.nprocs) {
			out(" processes ");
			out_dec(rd32(kva_to_pa(xsyms.nprocs)));
		}
		if (xsyms.initproc) {
			out(" initproc ");
			out_hex(*(volatile uint64_t *)kva_to_pa(xsyms.initproc));
		}
		out("\n");
	}
	if (reason != 1) {
		show_panic_string(&xsyms);
		show_backtrace();
	}
	out("=== XNU log (");
	out_dec(len);
	out(" bytes) ===\n");
	if (len > 4 * 1024 * 1024) {
		sp.n = 0;
	}
	save_log(&sp, 1);
	dbg_puts("=== end of XNU log ===\n");
	save_host_keep(&xsyms);
	/*
	 * What comes next has to be readable afterwards, and it is not being read on a console that
	 * scrolls past at this size. The log just saved is the kernel's own, in RAM at log_pa with
	 * length len: draw it here, in pages, and leave the last page up. save_log() above still
	 * runs, because a board with BOARD_NO_PLOG keeps nothing of it and a board with ramoops wants
	 * it in pstore.
	 */
	if (log_pa && len) {
		/*
		 * First the copy that outlives this run, then the pages on the screen: a photograph
		 * is worth having, but a file adb can pull is worth more, and the flash write has to
		 * happen before anything is left waiting for a person.
		 */
#if defined(BOARD_UFS_READ_LEASE) && BOARD_UFS_READ_LEASE
        const struct span return_spans[2] = {
            { (const volatile char *)(uintptr_t)log_pa, len },
            { (const volatile char *)ufs_return_audit, ufs_return_audit_n }
        };
        logbook_kernel_saved = save_log_spans_to_flash(return_spans,2,reason,code,elr,far);
#else
		logbook_kernel_saved = save_log_to_flash(log_pa, len, reason, code, elr, far);
#endif
#if BOARD_RETURN_TO_ANDROID
		/*
		 * No paging on this path. A page holds PANEL_W/(8*3) * PANEL_H/(16*3) = 2295
		 * characters and is held for XNU_REPORT_DWELL_MS, so a 400 KB log is 172 pages and
		 * fifty-seven minutes of the phone drawing to itself -- and the phone cannot start
		 * Android until the last page has been up. The log is already in the flash and adb
		 * is about to be able to pull it; show_log_tail() below still puts the last lines on
		 * the screen, which is what a picture would have been for.
		 */
		(void)0;
#else
		report_kernel_log(log_pa, len);
#endif
	}
	show_log_tail(40);
	dbg_delay_ms(20000);
#if BOARD_RETURN_TO_ANDROID
	/*
	 * A run nobody is watching: instead of resetting (which this board does not do -- neither
	 * PSCI_SYSTEM_RESET nor the watchdog's restart takes, see reset_board) the loader starts
	 * Android, which brings up adb, and the log just written to the flash can be pulled with
	 * mtk/tools/xaga-pull-log.sh. The image carries the Android kernel at CHAIN_OFFSET for
	 * exactly this (build_flashed.py): the loader is what boots either system from this slot.
	 */
#if BOARD_MALI_POWER
	/*
	 * Before Android, not after: a domain left raised is why every earlier run came back to
	 * this image instead of to adb. See mali_power_down().
	 */
	mali_power_down();
#endif
	out("starting Android, so that the log above can be pulled\n");
	chain_android();
#else
	reset_board();
#endif
}

/*
 * After a watchdog reset: what XNU had printed (its message buffer, through its own page tables:
 * cpu_ttep, 4K granule, 39-bit kernel addresses as TCR_EL1 had them), whether its clock ticked
 * (loader_live.heartbeat), and any panic string.
 */
static void rescue_state(void)
{
	if (xsyms.cpu_ttep) {
		xnu_fp[3] = rd32(kva_to_pa(xsyms.cpu_ttep)) | (uint64_t)rd32(kva_to_pa(xsyms.cpu_ttep) + 4) << 32;
		xnu_fp[2] = 0x22a51a251aULL;
	}
	out("cpu_ttep ");
	out_hex(xnu_fp[3]);
	if (xsyms.live) {
		out(" heartbeat ");
		out_dec(*(volatile uint64_t *)kva_to_pa(xsyms.live + 8));
	}
	out("\n");
	show_panic_string(&xsyms);
	if (!xsyms.msgbufp || !xnu_fp[3]) {
		return;
	}
	uint64_t mb = xnu_rd64(xsyms.msgbufp);
	uint64_t w0 = xnu_rd64(mb), w1 = xnu_rd64(mb + 8), bufc = xnu_rd64(mb + 16);
	int32_t size = (int32_t)(w0 >> 32), bufx = (int32_t)w1;
	out("msgbuf ");
	out_hex(mb);
	out(" size ");
	out_dec((uint32_t)size);
	out(" bufx ");
	out_dec((uint32_t)bufx);
	out("\n--- kernel message buffer (from DRAM) ---\n");
	if (size <= 0 || size > 1024 * 1024 || bufx < 0 || bufx > size) {
		return;
	}
	uint64_t fbsave = fbc.base;
	fbc.base = 0;
	for (int pass = 0; pass < 2; pass++) {
		int from = pass == 0 ? bufx : 0, to = pass == 0 ? size : bufx;
		for (int i = from; i < to; i++) {
			char c = xnu_rd8(bufc + i);
			if (c) {
				dbg_putc(c);
			}
		}
	}
	fbc.base = fbsave;
	dbg_puts("\n--- end of message buffer ---\n");
}

/* The watchdog cut XNU short: its log is still in DRAM, at the addresses of its symbols. */
static void rescue_log(void)
{
	struct payload_info pi;
	struct xnu_syms s;

	out("\n=== mtkloader: XNU did not return before the reset ===\n");
	if (!parse_payload(&pi)) {
		out("no XNU payload\n");
		return;
	}
	find_syms(pi.kernel, &s);
	if (!s.log || !s.log_len || !s.log_ring_at || !s.log_wrapped) {
		out("XNU log symbols not found\n");
		return;
	}
	/* osfmk/arm64/loader_return.c: a 256 KB head, then a 1 MB ring */
	const uint64_t head_sz = 256 * 1024, ring_sz = 1024 * 1024;
	const volatile char *log = (const volatile char *)kva_to_pa(s.log);
	uint32_t len = rd32(kva_to_pa(s.log_len));
	uint32_t ring_at = rd32(kva_to_pa(s.log_ring_at));
	uint8_t wrapped = *(const volatile uint8_t *)kva_to_pa(s.log_wrapped);
	struct span sp[3];
	int n = 0;

	out("loader_log_len ");
	out_dec(len);
	out(" ring_at ");
	out_dec(ring_at);
	out(" wrapped ");
	out_dec(wrapped);
	out("\n");
	if (ring_at >= ring_sz || wrapped > 1u || (wrapped && len < head_sz)) {
		out("XNU log ring metadata is invalid; not saving it\n");
		return;
	}
	if (!wrapped) {
		if (len > head_sz + ring_sz) {
			len = head_sz + ring_sz;
		}
		sp[n++] = (struct span){ log, len };
	} else {
		sp[n++] = (struct span){ log, head_sz };
		sp[n++] = (struct span){ log + head_sz + ring_at, ring_sz - ring_at };
		sp[n++] = (struct span){ log + head_sz, ring_at };
	}
	/* Persist before the optional state/page-table diagnostics can fault. The HLOG body is
	 * already chronological; the ordinary host decoder needs no ring-specific format. */
	logbook_kernel_saved = save_log_spans_to_flash(sp, n, XNU_LOGBOOK_RESCUE, 0, 0, 0);
	xsyms = s;
	xkernel = pi.kernel;
	rescue_state();
	dbg_puts("=== XNU log (from DRAM; a few bits may have flipped in the reset) ===\n");
	save_log(sp, n);
	dbg_puts("=== end of XNU log ===\n");
	save_host_keep(&s);
	show_log_tail(40);
}

/*
 * Do the timers reach EL1 here? The virtual timer's PPI 27 is what XNU uses, and its interrupt
 * never arrives: the kernel idles for ever waiting for a root device that no timeout ever wakes.
 * Set up the GIC as XNU does, arm each timer in turn and see which one is taken.
 */

#if TIMER_PROBE
static void gic_cpu_prepare(void)
{
	uint64_t sre, ctlr;

	/*
	 * ICC_CTLR_EL1.EOImode: with it set, ICC_EOIR1_EL1 only drops the priority and the
	 * interrupt stays active until ICC_DIR_EL1 -- and XNU never writes that, so its first
	 * timer interrupt would also be its last. Clear it for whoever runs on this core next.
	 */
	__asm__ volatile("mrs %0, ICC_CTLR_EL1" : "=r"(ctlr));
	out("  ICC_CTLR_EL1 ");
	out_hex(ctlr);
	out(ctlr & 2 ? " (EOImode=1: EOI does not deactivate)" : " (EOImode=0)");
	__asm__ volatile("msr ICC_CTLR_EL1, %0; isb" : : "r"(ctlr & ~2ULL));
	__asm__ volatile("mrs %0, ICC_CTLR_EL1" : "=r"(ctlr));
	out(" -> ");
	out_hex(ctlr);
	out("\n");

	__asm__ volatile("mrs %0, ICC_SRE_EL1" : "=r"(sre));
	__asm__ volatile("msr ICC_SRE_EL1, %0; isb" : : "r"(sre | 1));
	__asm__ volatile("msr ICC_AP1R0_EL1, xzr");
	__asm__ volatile("msr ICC_PMR_EL1, %0" : : "r"(0xffUL));
	__asm__ volatile("msr ICC_BPR1_EL1, xzr");
	__asm__ volatile("msr ICC_IGRPEN1_EL1, %0; isb" : : "r"(1UL));
}
#endif

/*
 * EOImode (ICC_CTLR_EL1 bit 1) is what ATF/GenieZone leaves set on this board: ICC_EOIR1_EL1
 * then only drops the running priority, and the interrupt stays *active* until a write to
 * ICC_DIR_EL1. XNU writes only EOIR, so with EOImode set it takes one timer interrupt and never
 * sees another: it idles for ever, waiting for timeouts that do not come. Clear it here, on the
 * core the kernel is about to run on.
 */
static void gic_fix_eoimode(void)
{
	uint64_t ctlr;

	__asm__ volatile("mrs %0, ICC_CTLR_EL1" : "=r"(ctlr));
	out("ICC_CTLR_EL1 ");
	out_hex(ctlr);
	out(ctlr & 2 ? " (EOImode set: clearing)" : " (EOImode clear)");
	if (ctlr & 2) {
		__asm__ volatile("msr ICC_CTLR_EL1, %0; isb" : : "r"(ctlr & ~2ULL));
		__asm__ volatile("mrs %0, ICC_CTLR_EL1" : "=r"(ctlr));
		out(" -> ");
		out_hex(ctlr);
	}
	out("\n");
}

#if TIMER_PROBE
static int timer_try(int ppi, int virt)
{
	uint64_t f = dbg_freq(), now, grp;
	uint32_t before;

	/* Group 1 NS, default priority, enabled: the same as XNU's gic_v3_irq_cpu_init(). */
	wr32(GICR_SGI + 0x080, rd32(GICR_SGI + 0x080) | (1u << ppi));
	wr32(GICR_SGI + 0xd00, rd32(GICR_SGI + 0xd00) & ~(1u << ppi));
	*(volatile uint8_t *)(GICR_SGI + 0x400 + ppi) = 0xa0;
	wr32(GICR_SGI + 0x100, 1u << ppi);
	grp = rd32(GICR_SGI + 0x080);
	wr32(GICR_SGI + 0x380, 1u << ppi); /* nothing of an earlier try left active */
	irq_count = 0;
	irq_intid = 0;

  again:
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(now));
	if (virt) {
		__asm__ volatile("msr cntv_cval_el0, %0; msr cntv_ctl_el0, %1; isb" : : "r"(now + f / 10), "r"(1UL));
	} else {
		__asm__ volatile("msr cntp_cval_el0, %0; msr cntp_ctl_el0, %1; isb" : : "r"(now + f / 10), "r"(1UL));
	}
	before = rd32(GICR_SGI + 0x200);
	irq_wait(now + f / 2);

	out(virt ? "  virtual timer  PPI " : "  physical timer PPI ");
	out_dec((uint64_t)ppi);
	out(": IGROUPR0 ");
	out_hex(grp);
	out(grp & (1u << ppi) ? " (group 1)" : " (GROUP 0: not ours)");
	out(", ISPENDR0 ");
	out_hex(before);
	out("->");
	out_hex(rd32(GICR_SGI + 0x200));
	out(", ISACTIVER0 after EOI ");
	out_hex(rd32(GICR_SGI + 0x300));
	out(", irqs ");
	out_dec(irq_count);
	if (irq_count) {
		out(" intid ");
		out_dec(irq_intid);
	}
	out(", ISACTIVER0 ");
	out_hex(rd32(GICR_SGI + 0x300));
	out("\n");
	if (irq_count == 1) {
		goto again;     /* a second one proves the first was deactivated */
	}
	__asm__ volatile("msr cntv_ctl_el0, %0; msr cntp_ctl_el0, %0; isb" : : "r"(2UL));
	/* Leave this PPI as XNU must find it: disabled, not pending, not active. */
	wr32(GICR_SGI + 0x180, 1u << ppi); /* ICENABLER0 */
	wr32(GICR_SGI + 0x280, 1u << ppi); /* ICPENDR0   */
	wr32(GICR_SGI + 0x380, 1u << ppi); /* ICACTIVER0 */
	__asm__ volatile("dsb sy; isb");
	return irq_count != 0;
}

static void timer_probe(void)
{
	out("timer probe at EL1:\n");
	gic_cpu_prepare();
	timer_try(27, 1);
	timer_try(30, 0);
	/* and the CPU interface as it was, so nothing of ours is left in it */
	__asm__ volatile("msr ICC_IGRPEN1_EL1, xzr; msr ICC_AP1R0_EL1, xzr; isb");
	out("  GICR after cleanup: ISENABLER0 ");
	out_hex(rd32(GICR_SGI + 0x100));
	out(" ISPENDR0 ");
	out_hex(rd32(GICR_SGI + 0x200));
	out(" ISACTIVER0 ");
	out_hex(rd32(GICR_SGI + 0x300));
	out("\n");
}
#endif /* TIMER_PROBE */

/* ---- the watcher on the second core ---- */

static uint64_t watcher_l1[512] __attribute__((aligned(4096)));
static volatile uint64_t watcher_started __attribute__((aligned(64)));

static void plog_flush(void)
{
	dcache_clean_range(RAMOOPS_CONSOLE, RAMOOPS_CONSOLE_SIZE);
	dcache_clean_range(MARKER_PA, 192);
}

/*
 * The watcher writes its record into DRAM and resets; it does not try to write flash, and it does
 * not try to hold the screen either -- both are things the next run of this loader can do after
 * it has brought UFS up properly. What it must get right is that the text is out of the caches
 * before the reset, which is the dcache_clean_range() in snap_end().
 */
static void snap_begin(void)
{
	struct snap_hdr *h = (struct snap_hdr *)SNAP_PA;

	h->magic = 0;
	h->version = 1;
	h->len = 0;
	h->ticks = 0;
	dbg_tee = (char *)(SNAP_PA + sizeof(struct snap_hdr));
	dbg_tee_cap = SNAP_SIZE - (uint32_t)sizeof(struct snap_hdr) - 1u;
	dbg_tee_at = 0;
}

static void snap_end(void)
{
	struct snap_hdr *h = (struct snap_hdr *)SNAP_PA;

	h->len = dbg_tee_at;
	h->ticks = dbg_ticks();
	h->magic = SNAP_MAGIC;
	dbg_tee = 0;
	/* the record has to be in DRAM, not only in a cache that the reset drops */
	dcache_clean_range(SNAP_PA, (uint64_t)dbg_tee_at + sizeof(struct snap_hdr) + 64u);
}

/*
 * XNU never came back (no timer, a hang): with its caches visible through our own cacheable
 * mapping, take what it shows and has written, put it into ramoops and reset the board.
 */
void watcher_main(void)
{
	for (int i = 0; i < 512; i++) {
		uint64_t pa = (uint64_t)i << 30;
		if (i == 0) {
			watcher_l1[i] = pa | (1u << 10) | (0u << 2) | 1;            /* MMIO: device */
		} else if (i < 8) {
			watcher_l1[i] = pa | (1u << 10) | (3u << 8) | (1u << 2) | 1; /* DRAM: normal WB, inner shareable */
		} else {
			watcher_l1[i] = 0;
		}
	}
	mmu_on((uint64_t)watcher_l1);
	watcher_started = 1;
	dcache_clean_range((uint64_t)&watcher_started, 8);
	snap_begin();

#if XNU_WATCH_HEARTBEAT
	/*
	 * The console structure was filled in by the core that started this one, and only part of it
	 * was flushed out of that core's cache, so none of it can be trusted here. Write it here,
	 * from this core, before the bar is first drawn.
	 */
	fbc.base = LK_FB_BASE;
	fbc.pitch = LK_FB_PITCH;
	fbc.w = PANEL_W;
	fbc.h = PANEL_H;
	fbc.scale = 2;
	fbc.fg = 0xffffffffu;
	fbc.bg = 0xff000000u;
	fb_base = LK_FB_BASE;
	fb_pitch = LK_FB_PITCH;
#endif

	/*
	 * The watchdog's own clock. Between the 100 ms naps there is a count of naps, so a counter
	 * that does not run turns "watch for two minutes" into a bounded number of sleeps and then
	 * the snapshot, instead of a core parked here for ever while the phone sits on a screen
	 * nobody draws on again.
	 */
	uint64_t end = dbg_ticks() + dbg_freq() * XNU_WATCH_SECONDS;
	uint32_t naps = XNU_WATCH_SECONDS * 10u;

#if XNU_WATCH_HEARTBEAT
	/*
	 * The question is whether this core reached its first instruction, and it has to be
	 * answerable from a photograph taken across the room, before anything slow or fallible runs.
	 * So the whole panel goes one flat colour here, at the top of the watcher, and nothing that
	 * follows can hide it: the console text draws over the colour but leaves most of it alone.
	 *
	 * Green here and still green later: this core runs, and the long diagnostic block below
	 * either never finishes or hangs. Green here, blue after the watch: the block finished and
	 * the two reset attempts are what does not work. Apple logo and no green at all: this core
	 * never got here, and the fault is in watcher_entry in start.S.
	 */
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff00a000u);
	dcache_clean_range(fb_base, (uint64_t)PANEL_H * fb_pitch);
#endif


	while (dbg_ticks() < end && naps != 0) {
		dcache_clean_range((uint64_t)&xnu_back, 8);
		if (xnu_back) {
			for (;;) {
				__asm__ volatile("wfe");
			}
		}
		naps--;
#if XNU_WATCH_HEARTBEAT
		/*
		 * A bar across the top of the panel that grows by one step per nap. It is the only
		 * thing that can tell, from outside the phone, whether this core is running at all:
		 * the console text does not reach the screen while XNU owns it, and the ramoops ring
		 * is switched off on this board. If the phone shows an Apple logo with no red bar,
		 * this core never got here; if the bar grows, it did, and whatever keeps the board
		 * from resetting is in psci_system_reset()/wdt_reset(), not in the entry path.
		 */
		{
			uint32_t done_naps = XNU_WATCH_SECONDS * 10u - naps;
			uint32_t w = done_naps * 4u;
			if (w > PANEL_W) {
				w = PANEL_W;
			}
			if (w != 0u) {
				fb_rect(0, 0, w, 48u, 0xffff0000u);
				fb_rect(0, 48u, w > 8u ? 8u : w, 48u, 0xffffffffu);
				/* the framebuffer is mapped write-back here, so the pixels need to leave the cache */
				dcache_clean_range(fb_base, 96ull * fb_pitch);
			}
		}
#endif
		dbg_delay_ms(100);
	}
	dcache_clean_range((uint64_t)&xnu_back, 8);
	if (xnu_back) {
		for (;;) {
			__asm__ volatile("wfe");
		}
	}

#if XNU_WATCH_HEARTBEAT
	/*
	 * One question is being asked from here, and it has to be answerable from a photograph taken
	 * across the room: did this core get this far. So the whole panel goes one flat colour and
	 * nothing else is drawn on it afterwards -- no text, because the console characters are
	 * narrow and the phone is in someone's hand; no reset attempt either, because a failed one
	 * leaves the panel showing XNU's screen and that is indistinguishable from this core never
	 * having run at all.
	 *
	 * Green means the watcher ran. The Apple logo still showing means it did not, and whatever
	 * stops the second core is in its entry path in start.S, not in the snapshot below.
	 */
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff00c000u);
	dcache_clean_range(fb_base, (uint64_t)PANEL_H * fb_pitch);
	snap_end();
	/* This branch has not saved XNU's log. A later retained-DRAM boot still needs rescue. */
	marker_set(MARK_RUNNING);
	for (;;) {
		__asm__ volatile("wfe");
	}
#else
	fbc.base = 0; /* the screen is XNU's: read it, do not draw on it */
#endif
	marker_set(MARK_DONE);
	dbg_puts("\n=== watcher: XNU still running after " );
	dbg_dec(XNU_WATCH_SECONDS);
	dbg_puts(" s, snapshot ===\n");
	if (xsyms.cpu_ttep) {
		xnu_fp[3] = *(volatile uint64_t *)kva_to_pa(xsyms.cpu_ttep);
		xnu_fp[2] = 0x22a51a251aULL;
	}
	dbg_puts("cpu_ttep ");
	dbg_hex(xnu_fp[3], 10);
	if (xsyms.live) {
		dbg_puts(" heartbeat ");
		dbg_dec(*(volatile uint64_t *)kva_to_pa(xsyms.live + 8));
	}
	if (xsyms.log_len) {
		dbg_puts(" loader_log_len ");
		dbg_dec(rd32(kva_to_pa(xsyms.log_len)));
	}
	/* Where the first core is: find its PMU in the cpu_dbgapb window, sample PMPCSR. */
	{
		const uint64_t win = 0x0e010000ULL; /* cpu_dbgapb, core 0 */
		uint64_t fb_saved = fbc.base, pmu = 0;
		out("\ncpu_dbgapb components:");
		for (uint64_t off = 0; off < 0x100000; off += 0x10000) {
			uint64_t b = win + off;
			uint32_t cidr1 = rd32(b + 0xff4), devarch = rd32(b + 0xfbc), devtype = rd32(b + 0xfcc);
			out("\n  +");
			out_hex(off);
			out(" CIDR1 ");
			out_hex(cidr1);
			out(" DEVARCH ");
			out_hex(devarch);
			out(" DEVTYPE ");
			out_hex(devtype);
			if ((devarch & 0xffff) == 0x2a16) { /* ARM PMU v3 */
				pmu = b;
			}
		}
		out("\n");
		if (pmu) {
			wr32(pmu + 0xfb0, 0xc5acce55u);
			out("cpu0 PC samples (PMPCSR):\n");
			for (int i = 0; i < 24; i++) {
				uint32_t lo = rd32(pmu + 0x200);
				uint32_t hi = rd32(pmu + 0x204);
				uint64_t pc = ((uint64_t)hi << 32 | lo) & 0x00ffffffffffffffULL;
				if (pc >> 47) {
					pc |= 0xff00000000000000ULL;
				}
				out("  ");
				out_sym(pc);
				out("\n");
				dbg_delay_ms(15);
			}
		}
		fbc.base = fb_saved;
	}
	/*
	 * Is the virtual timer's PPI 27 even reaching the first core? GICR frame 0 is its
	 * redistributor (SGI/PPI page at +0x10000); pending but not active means the GIC latched
	 * the timer and the CPU interface refused it, nothing pending means no timer at all.
	 */
	{
		const uint64_t gicd = 0x0c000000ULL, gicr = 0x0c040000ULL, sgi = gicr + 0x10000;
		dbg_puts("GICD_CTLR ");
		dbg_hex(rd32(gicd + 0x000), 8);
		dbg_puts(" TYPER ");
		dbg_hex(rd32(gicd + 0x004), 8);
		dbg_puts("\nGICR_CTLR ");
		dbg_hex(rd32(gicr + 0x000), 8);
		dbg_puts(" WAKER ");
		dbg_hex(rd32(gicr + 0x014), 8);
		dbg_puts(" TYPER ");
		dbg_hex(rd32(gicr + 0x008), 8);
		dbg_putc(' ');
		dbg_hex(rd32(gicr + 0x00c), 8);
		dbg_puts("\nGICR_SGI: IGROUPR0 ");
		dbg_hex(rd32(sgi + 0x080), 8);
		dbg_puts(" IGRPMODR0 ");
		dbg_hex(rd32(sgi + 0xd00), 8);
		dbg_puts(" ISENABLER0 ");
		dbg_hex(rd32(sgi + 0x100), 8);
		dbg_puts("\n          ISPENDR0 ");
		dbg_hex(rd32(sgi + 0x200), 8);
		dbg_puts(" ISACTIVER0 ");
		dbg_hex(rd32(sgi + 0x300), 8);
		dbg_puts(" IPRIORITY27 ");
		dbg_hex(*(volatile uint8_t *)(sgi + 0x400 + 27), 2);
		dbg_puts(" ICFGR1 ");
		dbg_hex(rd32(sgi + 0xc04), 8);
		dbg_puts("\n");
	}
	if (xsyms.gic_unhandled_count) {
		dbg_puts("kernel: gic_unhandled_count ");
		dbg_dec(*(volatile uint64_t *)kva_to_pa(xsyms.gic_unhandled_count));
		dbg_puts(" last_unhandled ");
		dbg_dec(rd32(kva_to_pa(xsyms.gic_last_unhandled)));
		dbg_puts(" active_iar[0] ");
		dbg_hex(*(volatile uint64_t *)kva_to_pa(xsyms.gic_active_iar), 16);
		dbg_puts("\n");
	}

	dbg_puts("\n=== what the screen shows ===\n");
	read_screen();
	show_panic_string(&xsyms);
	if (xsyms.msgbufp && xnu_fp[3]) {
		uint64_t mb = xnu_rd64(xsyms.msgbufp);
		uint64_t w0 = xnu_rd64(mb), w1 = xnu_rd64(mb + 8), bufc = xnu_rd64(mb + 16);
		int32_t size = (int32_t)(w0 >> 32), bufx = (int32_t)w1;
		dbg_puts("--- kernel message buffer ---\n");
		if (size > 0 && size <= 1024 * 1024 && bufx >= 0 && bufx <= size) {
			for (int pass = 0; pass < 2; pass++) {
				int from = pass == 0 ? bufx : 0, to = pass == 0 ? size : bufx;
				for (int i = from; i < to; i++) {
					char c = xnu_rd8(bufc + i);
					if (c) {
						dbg_putc(c);
					}
				}
			}
		}
		dbg_puts("\n--- end of message buffer ---\n");
	}
	if (xsyms.log && xsyms.log_len) {
		uint32_t len = rd32(kva_to_pa(xsyms.log_len));
		struct span sp = { (const volatile char *)kva_to_pa(xsyms.log), len > 256 * 1024 ? 256 * 1024 : len };
		dbg_puts("--- loader_log (head) ---\n");
		save_log(&sp, 1);
	}
	save_host_keep(&xsyms);
	dbg_puts("=== end of watcher snapshot ===\n");
	plog_flush();
	snap_end();
	/* the next run of this loader takes the record out of DRAM and writes it to the logbook */
	psci_system_reset();
	/*
	 * PSCI's system reset did not take. Say so while the screen still shows the text, then try
	 * the watchdog's software reset; if that does not take either, this core stops here and the
	 * panel keeps the last thing it said, which is the only way the record gets out of this board.
	 */
	dbg_puts("=== the PSCI reset did not take; trying the watchdog ===\n");
	wdt_reset();
	dbg_puts("=== the watchdog reset did not take either; power the phone off ===\n");
	for (;;) {
		__asm__ volatile("wfe");
	}
}

static void watcher_start(void)
{
	if (!XNU_WATCH_SECONDS) {
		return;
	}
	watcher_started = 0;
	int64_t r = psci_cpu_on(BOARD_CPU_REG(WATCH_CORE), (uint64_t)watcher_entry, 0);
	out("watcher on cpu");
	out_dec(WATCH_CORE);
	out(": PSCI CPU_ON ");
	out_dec((uint64_t)r);
	for (int i = 0; i < 100 && !watcher_started; i++) {
		dbg_delay_ms(10);
	}
	out(watcher_started ? ", running\n" : ", not seen\n");
}

/* ---- starting XNU ---- */

/* ---- how fast the cores are running ---- */

/*
 * What the boot loader left the cores at, in megahertz. The system counter runs at a fixed rate
 * whatever the cores do, so counting core cycles between two of its ticks gives the core's clock
 * directly. Nothing here changes a frequency -- it only asks, because a kernel that finds itself
 * on a core the boot loader parked at a third of its speed is worth knowing about before anyone
 * sets about raising it.
 */
static uint32_t cpu_mhz(void)
{
	uint64_t pmcr, cycles_before, cycles_after, ticks_before, ticks_after, hz = dbg_freq();

	__asm__ volatile("mrs %0, pmcr_el0" : "=r"(pmcr));
	__asm__ volatile("msr pmcr_el0, %0" : : "r"((pmcr | 5u) & ~2u));   /* enable, reset the count */
	__asm__ volatile("msr pmcntenset_el0, %0" : : "r"(1ull << 31));
	__asm__ volatile("isb");

	ticks_before = dbg_ticks();
	__asm__ volatile("mrs %0, pmccntr_el0" : "=r"(cycles_before));
	dbg_delay_ms(20);
	__asm__ volatile("mrs %0, pmccntr_el0" : "=r"(cycles_after));
	ticks_after = dbg_ticks();

	if (cycles_after <= cycles_before || ticks_after <= ticks_before) {
		return 0;                       /* the counter is not ours to read */
	}
	return (uint32_t)(((cycles_after - cycles_before) * hz) /
	    ((ticks_after - ticks_before) * 1000000ull));
}

/* ---- the touch screen, before the kernel wants it ---- */

/*
 * Room for the firmware and the fourteen header bytes in front of it: plain DRAM that nothing
 * else wants. The loader's image ends before 0x400c0000, XNU's log ring sits at 0x47b1c9c0 and
 * ramoops at 0x4d0e0000, XNU's own memory starts at 0xc0000000, and DRAM covers all of it.
 */
#ifndef TOUCH_STAGE
#define TOUCH_STAGE         0x50000000ULL       /* scratch DRAM for the controller's firmware */
#endif

/* The SPI clock mux as we found it, so that Android can be given it back that way. */
static uint32_t touch_clk_cfg2;
static int touch_clk_cfg2_seen;

/*
 * The controller keeps its firmware in RAM and loses it at every reset, so whoever boots the
 * phone has to send it afresh -- Android does this at every start, and so must we before a
 * kernel of ours can expect to read a finger. This is that sequence: open the bus, reset the
 * chip into its ROM bootloader, send the image as one message, pause, and give the go command,
 * after which the chip introduces itself unasked saying what it now runs.
 *
 * Returns the mode it reached: 1 or 2 mean the firmware is up. Nothing here is fatal -- a phone
 * whose screen cannot be touched still boots.
 */
static uint8_t touch_bring_up_once(void)
{
	static const uint32_t gate_clr[4] = { 0x8c, 0xa8, 0xc4, 0x84 };
	uint8_t buf[64], code = 0;
	uint32_t got = 0;
	uint8_t *stage = (uint8_t *)TOUCH_STAGE;

	for (int i = 0; i < 4; i++) {
		wr32(INFRACFG_BASE + gate_clr[i], 0xffffffffu);
	}
	__asm__ volatile("dsb sy");
	touch_clk_cfg2 = rd32(TOPCKGEN_BASE + CLK_CFG_2);
	touch_clk_cfg2_seen = 1;
	spi_pins_to_spi2();
	spi_setup();

	tcm_reset();
	if (tcm_read_patiently(&code, buf, sizeof(buf), &got) != 1 || got < 2) {
		out("touch: the controller said nothing after a reset (code ");
		out_hex(code);
		out(")\n");
		return 0;
	}
	out("touch: after a reset, code ");
	out_hex(code);
	out(", mode ");
	out_hex(buf[1]);
	out("\n");
	if (buf[1] != TCM_MODE_ROMBOOT) {
		out("touch: not in its ROM bootloader (mode ");
		out_hex(buf[1]);
		out("), leaving it alone\n");
		return buf[1];
	}

	/* The message whole in DRAM -- command, length, header, image -- to go out in one breath. */
	memcpy(stage + TCM_MESSAGE_HEAD + TCM_ROMBOOT_HEADER, touch_fw, TOUCH_FW_SIZE);
	tcm_load_firmware(stage);
	if (tcm_read_patiently(&code, buf, sizeof(buf), &got) != 1 || code != TCM_STATUS_OK) {
		out("touch: the image was not taken (");
		out_hex(code);
		out(")\n");
		return 0;
	}
	out("touch: the image was taken\n");
	tcm_run_firmware();
	for (uint32_t waited = 0; waited < 4000; waited += 10) {
		if (!tcm_irq()) {
			dbg_delay_ms(10);
			continue;
		}
		if (tcm_read(&code, buf, sizeof(buf), &got) != 1) {
			dbg_delay_ms(10);
			waited += 10;
			continue;
		}
		out("touch: it said code ");
		out_hex(code);
		out(" after ");
		out_hex(waited);
		out(" ms\n");
		if (code == TCM_REPORT_IDENTIFY && got >= 2) {
			out("touch: ");
			for (uint32_t i = 2; i < got && i < 18; i++) {
				out_putc(buf[i] >= 0x20 && buf[i] < 0x7f ? (char)buf[i] : ' ');
			}
			out(" is up, mode ");
			out_hex(buf[1]);
			out("\n");
			return buf[1];
		}
	}
	out("touch: it never said it was up\n");
	return 0;
}

/*
 * The whole sequence, up to six times, waiting a little longer between tries each time. Now and then the chip takes the image and then says
 * nothing after the go command -- two runs in four did -- and a fresh start from a reset brings
 * it up; this was what left iOS without a touch screen.
 *
 * Not every board has this controller: BOARD_TOUCH 0 in board.h leaves the whole bring-up out, and then
 * this function is unused on purpose (hence the attribute, rather than a warning on every build).
 */
static uint8_t __attribute__((unused)) touch_bring_up(void)
{
	for (int attempt = 1; attempt <= 6; attempt++) {
		uint8_t mode = touch_bring_up_once();

		if (mode == TCM_MODE_APPLICATION || mode == TCM_MODE_HOSTDOWNLOAD) {
			return mode;
		}
		out("touch: attempt ");
		out_dec((uint32_t)attempt);
		out(" failed, starting over\n");
		dbg_delay_ms(100 * (uint32_t)attempt);  /* a little longer each time for the chip to settle */
	}
	return 0;
}

/* ---- the big cores' clock ---- */

/*
 * LK leaves the big cluster (Cortex-A75) at 1087 MHz with its rail at 800 mV. MediaTek's own table
 * for this chip (cpufreq_v1, mach/mt6768, "OPP table B") runs it at 1176 MHz at that very voltage,
 * so it goes there: no rail is touched. The cluster is still off while the loader runs -- XNU wakes
 * those cores later -- so nothing is executing on the PLL while it is reprogrammed.
 *
 * ARMPLL_CON1 (apmixedsys + 0x20c): bit 31 asks for the new value, bits 24..26 the post divider
 * (here /2), bits 0..21 the multiplier of 26 MHz / 2^14 -- the same write the vendor's GPU driver
 * makes to its PLL.
 */
#ifndef APMIXED_BASE
#define APMIXED_BASE        0x1000c000ULL
#endif
#define ARMPLL_CON1         0x20c
#define BIG_MHZ             1176u

/* Likewise: XNU_BIG_CLOCK 0 in board.h is a board whose PLL this is not, and then nothing calls this. */
static void __attribute__((unused)) big_cores_clock(void)
{
	uint32_t before = rd32(APMIXED_BASE + ARMPLL_CON1), post = (before >> 24) & 7u;
	uint32_t pcw = (uint32_t)(((uint64_t)BIG_MHZ << post) * 16384u / 26u);

	if (post != 1 || (before & 0x3fffffu) >= pcw) {
		return;                         /* not what LK is known to leave: leave it alone */
	}
	wr32(APMIXED_BASE + ARMPLL_CON1, (1u << 31) | (post << 24) | pcw);
	__asm__ volatile("dsb sy");
	dbg_delay_us(20);
	out("big cores: ");
	out_dec((uint32_t)(((26ull * (before & 0x3fffffu)) >> 14) >> post));
	out(" -> ");
	out_dec((uint32_t)(((26ull * (rd32(APMIXED_BASE + ARMPLL_CON1) & 0x3fffffu)) >> 14) >> post));
	out(" MHz at the same 800 mV\n");
}

static void __attribute__((noreturn)) run_xnu(void)
{
	struct payload_info pi;
	struct xnu_syms s;

	{
		int payload_ok = parse_payload(&pi);

#ifdef BOARD_STEPS
		fb_rect(20, 400, 260, 80, payload_ok ? 0xff00ff00u : 0xffff0000u);
#endif
		if (!payload_ok) {
		out("ERROR: no XNU payload in this image\n");
		dbg_delay_ms(5000);
		chain_android();
		}
	}
	out("XNU kernel ");
	out_dec(pi.kernel_size);
	out(" bytes, ramdisk ");
	out_dec(pi.ramdisk_size);
	out(" bytes\n");
	STEP_BAND(12, 0xff7f7f7fu);                /* 12: the XNU payload parsed */

	const struct mach_header_64 *mh = (const struct mach_header_64 *)pi.kernel;
	uint64_t vm_hi = 0, entry_va = 0;
	const struct load_command *lc = (const struct load_command *)(mh + 1);
	for (uint32_t i = 0; i < mh->ncmds; i++, lc = (const void *)((const uint8_t *)lc + lc->cmdsize)) {
		if (lc->cmd == LC_SEGMENT_64) {
			const struct segment_command_64 *sg = (const void *)lc;
			if (sg->vmsize == 0) {
				continue;
			}
			if (sg->vmaddr + sg->vmsize > vm_hi) {
				vm_hi = sg->vmaddr + sg->vmsize;
			}
			uint64_t dst_pa = kva_to_pa(sg->vmaddr);
			memset((void *)dst_pa, 0, sg->vmsize);
			memcpy((void *)dst_pa, pi.kernel + sg->fileoff, sg->filesize);
		} else if (lc->cmd == LC_UNIXTHREAD) {
			entry_va = ((const uint64_t *)((const uint8_t *)lc + 16))[32];
		}
	}
	out("kernel at ");
	out_hex(kva_to_pa(0xfffffff007004000ULL));
	out(", entry ");
	out_hex(kva_to_pa(entry_va));
	out("\n");
	STEP_BAND(10, 0xff0080ffu);                /* 10: every segment copied into place */

	find_syms(pi.kernel, &s);
	xsyms = s;
	xkernel = pi.kernel;
	STEP_BAND(11, 0xff8080ffu);                /* 11: symbols found in the kernel */

	/* XNU's console colors (32 bpp column) opaque. */
	if (s.vc_colors) {
		volatile uint32_t *c = (volatile uint32_t *)kva_to_pa(s.vc_colors);
		for (int i = 0; i < 8; i++) {
			c[i * 4 + 2] |= 0xff000000u;
		}
		out("vc_colors: alpha set\n");
	}

	/*
	 * This kernel's serial console is a PL011 driver (vmapple_uart): its transmit_ready waits
	 * for a flag the MediaTek UART does not have, so "serial=3" hangs there forever. Make
	 * transmit_ready return 1 and transmit_data do nothing; kprintf then reaches loader_log
	 * (and mt6577_uart_putc, which the kernel calls as well and which drives this UART right).
	 */
	if (s.uart_tx_ready && s.uart_tx_data) {
		volatile uint32_t *r = (volatile uint32_t *)kva_to_pa(s.uart_tx_ready);
		volatile uint32_t *d = (volatile uint32_t *)kva_to_pa(s.uart_tx_data);
		r[0] = 0x52800020u; /* mov w0, #1 */
		r[1] = 0xd65f03c0u; /* ret        */
		d[0] = 0xd65f03c0u; /* ret        */
		/*
		 * ... and the byte itself goes to loader_log_putc, which keeps it in the kernel's log
		 * buffer: that is how anything the first programs print reaches us at all. (This
		 * build of loader_log_putc calls nothing, so there is no way back round.)
		 */
		if (s.log_putc) {
			int64_t rel = (int64_t)(s.log_putc - s.uart_tx_data) >> 2;

			if (rel >= -(1 << 25) && rel < (1 << 25)) {
				d[0] = 0x14000000u | ((uint32_t)rel & 0x03ffffffu);
			}
		}
		/* and no input: the kernel's serial keyboard would otherwise read this UART's
		 * receive register, which here is another register entirely, for ever. */
		if (s.uart_rx_ready) {
			volatile uint32_t *rx = (volatile uint32_t *)kva_to_pa(s.uart_rx_ready);
			rx[0] = 0x52800000u; /* mov w0, #0 */
			rx[1] = 0xd65f03c0u; /* ret        */
		}
		out("vmapple_uart: transmit_ready patched, transmit_data -> loader_log_putc, receive off\n");
	}

	/*
	 * Everything written to /dev/console -- the kernel's own text and whatever the first
	 * programs print -- goes through uart_putc, and on this phone that is a PL011 driver
	 * writing into nothing. Send it to loader_log_putc instead (same argument, a character in
	 * w0), which keeps it in the kernel's log buffer and puts it out of the MediaTek UART as
	 * well. Without this the only voice userland has is lost.
	 */
	if (s.log_putc) {
		/* uart_putc is kprintf's way out, uart_putc_options the console's (and so userland's) */
		const uint64_t from[2] = { 0, 0 };

		for (int i = 0; i < 2; i++) {
			if (!from[i]) {
				continue;
			}
			volatile uint32_t *at = (volatile uint32_t *)kva_to_pa(from[i]);
			int64_t rel = (int64_t)(s.log_putc - from[i]) >> 2;

			if (rel >= -(1 << 25) && rel < (1 << 25)) {
				at[0] = 0x14000000u | ((uint32_t)rel & 0x03ffffffu); /* b loader_log_putc */
			}
		}
		out("uart_putc and uart_putc_options -> loader_log_putc\n");
	}

	/*
	 * EOImode is per processor, and gic_fix_eoimode() below only reaches the one this code runs
	 * on. Every other core comes out of PSCI CPU_ON with the bit set again, takes one timer
	 * interrupt and goes deaf: with four cores iOS crawled on the one that worked, and the
	 * kernel's own heartbeat beat 3 times in 840 seconds.
	 *
	 * Each core calls gic_v3_irq_cpu_init() for itself, and in it writes zero to ICC_BPR1_EL1.
	 * That instruction becomes a write of zero to ICC_CTLR_EL1, which clears EOImode (and CBPR,
	 * which wants to be zero anyway). What is given up is BPR1 = 0, the preemption grouping,
	 * which keeps its reset value -- a small thing beside a core whose timer never fires again.
	 */
	if (s.gic_cpu_init) {
		volatile uint32_t *at = (volatile uint32_t *)kva_to_pa(s.gic_cpu_init);
		int done = 0;

		for (int i = 0; i < 256 && !done; i++) {
			if (at[i] == 0xd518cc7fu) {          /* msr ICC_BPR1_EL1, xzr */
				at[i] = 0xd518cc9fu;         /* msr ICC_CTLR_EL1, xzr */
				done = 1;
			}
		}
		out(done ? "gic_v3_irq_cpu_init: every core clears EOImode\n" :
		    "gic_v3_irq_cpu_init: the BPR1 write is not where it was -- EOImode stays set\n");
	}

	/* Have XNU come back here on a panic or an early exception. */
	if (s.stub_slot) {
		volatile uint64_t *slot = (volatile uint64_t *)kva_to_pa(s.stub_slot);
		if (slot[0] == 0x545352444c554e58ULL) { /* "XNULDRST" */
			slot[1] = (uint64_t)xnu_return_stub;
			slot[2] = XNU_CHECKPOINT;
			slot[3] = XNU_SLIDE;
			out("return stub at ");
			out_hex(slot[1]);
			out("\n");
		}
	}

	uint64_t kern_hi = (kva_to_pa(vm_hi) + PAGE_SIZE_MAX - 1) & ~(PAGE_SIZE_MAX - 1);
	uint64_t rd_pa = kern_hi;
	uint64_t rd_size = (pi.ramdisk_size + PAGE_SIZE_MAX - 1) & ~(PAGE_SIZE_MAX - 1);
	if (rd_size) {
		memset((void *)rd_pa, 0, rd_size);
		memcpy((void *)rd_pa, pi.ramdisk, pi.ramdisk_size);
	}
	/* and the big one off the flash, if it is there (with XNU_ROOT_EMMC the kernel reads it itself) */
	uint64_t sys_size = (XNU_ROOT_EMMC || !XNU_SYSTEM_IMAGE) ? 0 : load_system_image();
	uint64_t sys_pa = sys_size ? SYSTEM_PA : 0;
	uint64_t sys_span = (sys_size + PAGE_SIZE_MAX - 1) & ~(PAGE_SIZE_MAX - 1);
	/*
	 * Say what the flash gave us, on the panel, before XNU's console covers it.
	 *
	 * Everything about this number is easy to get wrong and impossible to see afterwards:
	 * load_system_image() prints to the same console XNU is about to flood, and a 0 return does
	 * not announce itself -- it silently turns md0 back into the small root.apfs and the only
	 * symptom appears minutes later as a userland message about a missing launchd. Build 75
	 * cost a debugging round to exactly that. So: print the three numbers that decide it, and
	 * hold the panel long enough to photograph them.
	 */
#if BOARD_REPORT_SYSTEM_IMAGE
	{
		fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
		fbc.x = 0;
		fbc.y = fbc.top = 0;
		fbc.scale = 4;
		fbc.fg = 0xffffffffu;
		fbc.bg = 0xff000000u;
		out("SYSTEM IMAGE 86 full hs\n");
		out("found "); out_dec(sysimg_found); out("\n");
		out("bytes "); out_dec(sysimg_bytes); out("\n");
		out("kb    "); out_dec(sysimg_bytes / 1024u); out("\n");
		out("secs  "); out_dec(sysimg_secs); out("\n");
		out("io    "); out_dec(ufs_io_stage);
		out(" fdev "); out_dec(ufs_io_fdev);
		out(" fst "); out_dec(ufs_fdevinit_stage);
		out(" lun "); out_dec(ufs_lun);
		out(" found "); out_dec(ufs_lun_found); out("\n");
		out("what  "); out(board_storage_what());
		out(" 0x"); out_hex(board_storage_status()); out("\n");
		out("md0   ");
		out(sys_size ? "system.apfs" : "root.apfs (small!)");
		out("\n");
		out("hs    ");
		out_dec(ufs_hs_stage); out(" ok "); out_dec(ufs_hs_ok);
		out(" upm "); out_dec(ufs_hs_upmcrs);
		out(" rx "); out_dec(ufs_hs_gear_rx); out("/"); out_dec(ufs_hs_lane_rx);
		out(" tx "); out_dec(ufs_hs_gear_tx); out("/"); out_dec(ufs_hs_lane_tx);
		out("\n");
		dbg_delay_ms(60000);   /* the same hold the UFS report uses: long enough to photograph */
	}
#endif

	uint64_t dt_pa = sys_size ? sys_pa + sys_span : rd_pa + rd_size;
	uint32_t dt_len = build_device_tree(dt_pa, rd_pa, rd_size, sys_pa, sys_span);
	STEP_BAND(12, 0xffff8000u);                /* 12: the device tree is built */
	uint64_t ba_pa = (dt_pa + dt_len + PAGE_SIZE_MAX - 1) & ~(PAGE_SIZE_MAX - 1);
	uint64_t top = ba_pa + PAGE_SIZE_MAX;

	struct boot_args *ba = (struct boot_args *)ba_pa;
	memset(ba, 0, sizeof(*ba));
	ba->revision = 2;
	ba->version = 2;
	ba->physBase = XNU_PHYS_BASE;
	ba->virtBase = KERNEL_VIRT_BASE;
	ba->memSize = XNU_MEM_SIZE;
	ba->memSizeActual = XNU_MEM_SIZE;
	ba->topOfKernelData = top;
	ba->deviceTreeP = dt_pa - XNU_SLIDE;
	ba->deviceTreeLength = dt_len;
	ba->video.v_baseAddr = fb_base;
	ba->video.v_display = XNU_DISPLAY; /* 0: the kernel's text console, 1: for iOS to draw */
	ba->video.v_rowBytes = fb_pitch;
	ba->video.v_width = PANEL_W;
	ba->video.v_height = PANEL_H;
	ba->video.v_depth = 32;
	memcpy(ba->commandLine, XNU_BOOT_ARGS, sizeof(XNU_BOOT_ARGS));
	/* the kernel's version, for sysctl kern.hakios.version and its log, as xnuloader gives it (one word) */
	if (pi.kernel_version != NULL) {
		static const char prefix[] = " hakios_kver=";
		uint32_t n = sizeof(XNU_BOOT_ARGS) - 1, len = 0;

		while (pi.kernel_version[len] != 0 && pi.kernel_version[len] != ' ' && len < 128) {
			len++;
		}
		if (n + sizeof(prefix) - 1 + len < BOOT_LINE_LENGTH) {
			memcpy(ba->commandLine + n, prefix, sizeof(prefix) - 1);
			memcpy(ba->commandLine + n + sizeof(prefix) - 1, pi.kernel_version, len);
			ba->commandLine[n + sizeof(prefix) - 1 + len] = 0;
			out("kernel.ver=");
			for (uint32_t i = 0; i < len; i++) {
				out_putc(pi.kernel_version[i]);
			}
			out("\n");
		}
	}

	out("md0 ");
	out_hex(sys_size ? sys_pa : rd_pa);
	out(sys_size ? " (the system image), md1 " : XNU_ROOT_EMMC ? " is none: md1 (root.packfs), the system image on the eMMC" :
	    " (root.packfs only)");
	if (sys_size) {
		out_hex(rd_pa);
	}
	out("\nboot-args: " XNU_BOOT_ARGS "\nframebuffer ");
	out_hex(fb_base);
	out(" pitch ");
	out_dec(fb_pitch);
	out(", XNU memory ");
	out_hex(XNU_PHYS_BASE);
	out("+");
	out_hex(XNU_MEM_SIZE);
	out("\n");
	display_alpha_off();
	STEP_BAND(13, 0xffffffffu);                /* 13: boot args written, alpha off */

	/* An interrupted run needs rescue even when the hardware watchdog is disabled. */
	marker_set(MARK_RUNNING);
	if (XNU_WDT_SECONDS) {
		uint32_t t = XNU_WDT_SECONDS > 31 ? 31 : XNU_WDT_SECONDS;
		wr32(TOPRGU_LENGTH, ((t * 32768u / 512u) << 5) | 0x8u);
		wr32(TOPRGU_RESTART, TOPRGU_RESTART_KEY);
		wr32(TOPRGU_MODE, TOPRGU_MODE_KEY | 0x1u);
		out("watchdog: back to Android in ");
		out_dec(t);
		out(" s\n");
	}
	/* The timers XNU will use: a virtual counter offset by EL2 (MediaTek's GenieZone) would
	 * leave its deadlines forever in the future, and the kernel idle with nothing to wake it. */
	{
		uint64_t pct, vct, frq, kctl, pfr0;
		__asm__ volatile("isb; mrs %0, cntpct_el0; mrs %1, cntvct_el0; mrs %2, cntfrq_el0;"
		    "mrs %3, cntkctl_el1; mrs %4, id_aa64pfr0_el1"
		    : "=r"(pct), "=r"(vct), "=r"(frq), "=r"(kctl), "=r"(pfr0));
		out("timers: CNTPCT ");
		out_hex(pct);
		out(" CNTVCT ");
		out_hex(vct);
		out(" delta ");
		out_hex(pct - vct);
		out(" CNTFRQ ");
		out_dec(frq);
		out(" CNTKCTL ");
		out_hex(kctl);
		out(" EL2 ");
		out_dec((pfr0 >> 8) & 0xf);
		out("\n");
	}

	{
		uint32_t mhz = cpu_mhz();

		out("this core is running at ");
		if (mhz) {
			out_dec(mhz);
			out(" MHz\n");
		} else {
			out("a speed its cycle counter will not say\n");
		}
	}

	/* must happen on the core the kernel runs on, and before it sets its GIC up */
	gic_fix_eoimode();
#if TIMER_PROBE
	timer_probe();
#endif
	watcher_start();
	/*
	 * The touch screen, while there is still a console to complain to. The controller has to be
	 * handed its firmware before anything can read a finger out of it, and the kernel is a bad
	 * place to do it from: it would have to carry those hundred kilobytes itself, and it comes
	 * up long after the chip has forgotten them.
	 *
	 * Not every board has the MT6768's controller, and this bring-up pokes clock gates by the
	 * MT6768's offsets -- on a board where those offsets are other gates it does harm. 0 skips it.
	 */
#if BOARD_TOUCH
	touch_bring_up();
#endif
#if XNU_BIG_CLOCK
	big_cores_clock();
#endif
#if BOARD_MALI_PREPOWER
	/*
	 * The GPU before the panel is cleared: its bring-up prints a dozen lines, and every one of
	 * them is worth having on the screen while this is still being worked out. The clear below
	 * wipes them, which is exactly why this goes first.
	 *
	 * Guarded by PREPOWER rather than POWER. The cleanup in chain_android() keeps the POWER
	 * guard, because the domains have to be handed back whether this loader raised them or the
	 * kernel did -- a build that skipped that cleanup is what kept reloading this image instead
	 * of returning to Android. With PREPOWER=0 the kernel reaches the GPU in the state Android
	 * reaches it from, which is the comparison this work could never make.
	 */
	mali_leave_powered();
#if BOARD_MALI_RETURN
	/*
	 * The whole point of this build: the power-up ran, and now the answer goes to flash and the
	 * board goes back to Android rather than on to XNU.
	 *
	 * An earlier build halted here instead, on the theory that the panel would show how far the
	 * bring-up got. It did not -- the panel came up black, and "the bring-up finished and halted"
	 * and "the bring-up took a bus fault" are the same black screen with the same silent USB. The
	 * logbook tells those apart: it holds a record written before each step, so the last one that
	 * made it is the step that was running when the board stopped. Going back to Android is what
	 * makes it readable, because that is the only state adb answers in.
	 */
	probe_say("gpu: bring-up returned; the log above is what it did\n");
	probe_flash(36u);
	out("gpu: log written; going back to Android to be read\n");
	chain_android();
#endif
#if BOARD_MALI_STOP
	/*
	 * Deliberately never return. The bring-up above prints every domain and then the identifier
	 * line, so whether those lines are complete is the whole answer: complete means this loader
	 * got past the GPU and the loop belongs to the kernel, cut short means it belongs to the
	 * power-up and the line it stopped on names the domain.
	 *
	 * A halt, not a reset -- a reset would reload this image and there would be nothing left on
	 * the panel to photograph. The watchdog was disabled on the way in, so nothing interrupts it.
	 */
	out("\n-- stopped on purpose: BOARD_MALI_STOP is on --\n");
	for (;;) {
		__asm__ volatile ("wfe");
	}
#endif
#endif
	/*
	 * Nothing of ours is left on the panel: iOS paints only what it draws, and where it does not
	 * reach, what LK wrote there stays -- its "Orange State: your device has been unlocked"
	 * showed through the home screen.
	 */
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
	for (uint64_t at = fb_base; at < fb_base + (uint64_t)PANEL_H * fb_pitch; at += 64) {
		__asm__ volatile("dc cvac, %0" : : "r"(at));
	}
	__asm__ volatile("dsb sy");
	/*
	 * The clear above takes every band with it, so this one goes back afterwards: it is the only
	 * evidence that says "the loader finished and the next instruction is the kernel's". If the
	 * panel is black under it, XNU was entered and did not get as far as drawing; if there is
	 * nothing at all, the loader died on the way here.
	 */
	STEP_BAND(14, 0xff00a0ffu);                /* 14: the panel is cleared, jumping to XNU */
	/*
	 * The last thing written before the kernel takes the CPUs: proof that the write path works,
	 * with the boot args in it, so that a run which never comes back still says what it was told.
	 */
	logbook_boot_note();
	ufs_read_guard_start();
	out("Jumping to XNU...\n");
	jump_to_kernel(kva_to_pa(entry_va), ba_pa);
	__builtin_unreachable();
}

#if MSDC_TEST
/*
 * Can we read the phone's eMMC with what LK left set up? Checked against bytes we know: the GPT
 * header at block 1 ("EFI PART") and the start of boot_a, which holds the image this very
 * program came from ("ANDROID!"). Reads only.
 */
static void msdc_dump(const char *name, uint64_t lba, const uint8_t *b, int ok)
{
	out(name);
	out(" lba ");
	out_hex(lba);
	if (!ok) {
		out(": FAILED at ");
		out(msdc_last.what);
		out(", status ");
		out_hex(msdc_last.status);
		out("\n");
		return;
	}
	out(": ");
	for (int i = 0; i < 16; i++) {
		out_hex(b[i]);
		out_putc(' ');
	}
	out(" |");
	for (int i = 0; i < 24; i++) {
		out_putc(b[i] >= 0x20 && b[i] < 0x7f ? (char)b[i] : '.');
	}
	out("|\n");
}

/*
 * The SD card slot (MSDC1). LK never touches it, so its clock may be gated and the card
 * unpowered (vmmc = MT6358 VMCH, vqmmc = ldo_vmc, both over PWRAP). This only looks: it reads
 * MSDC1's registers and tries the first commands of the SD sequence. If the card answers, the
 * rails and clocks are already on and no PMIC work is needed; if nothing answers, they are not.
 */
static void msdc1_probe(void)
{
	uint32_t resp;
	int ok;

	msdc_base = MSDC1_BASE;
	out("\nSD slot (MSDC1) as LK leaves it: CFG ");
	out_hex(rd32(MSDC1_BASE + MSDC_CFG));
	out(" PS ");
	out_hex(rd32(MSDC1_BASE + MSDC_PS));
	out(" SDC_CFG ");
	out_hex(rd32(MSDC1_BASE + SDC_CFG));
	out(" STS ");
	out_hex(rd32(MSDC1_BASE + SDC_STS));
	out(" INT ");
	out_hex(rd32(MSDC1_BASE + MSDC_INT));
	out("\n");
	if (rd32(MSDC1_BASE + MSDC_CFG) == 0 && rd32(MSDC1_BASE + MSDC_PS) == 0) {
		out("  all zero: the controller looks unclocked\n");
	}

	/* CMD0 (go idle, no answer), then CMD8 (which voltages do you take, R7) */
	wr32(MSDC1_BASE + MSDC_INT, rd32(MSDC1_BASE + MSDC_INT));
	msdc_int_seen = 0;
	wr32(MSDC1_BASE + SDC_ARG, 0);
	wr32(MSDC1_BASE + SDC_CMD, CMD_OPC(0));
	msdc_wait_int(INT_CMDRDY, INT_CMDTMO | INT_RSPCRCERR, "CMD0");
	dbg_delay_ms(10);

	wr32(MSDC1_BASE + SDC_ARG, 0x1aa);
	wr32(MSDC1_BASE + SDC_CMD, CMD_OPC(8) | CMD_RSP_R1);
	ok = msdc_wait_int(INT_CMDRDY, INT_CMDTMO | INT_RSPCRCERR, "CMD8");
	resp = rd32(MSDC1_BASE + SDC_RESP0);
	out("  CMD8 (interface condition): ");
	if (ok) {
		out("answered ");
		out_hex(resp);
		out(resp == 0x1aa ? " -- an SD card is powered and listening" : "");
	} else {
		out("no answer (");
		out(msdc_last.what);
		out(", status ");
		out_hex(msdc_last.status);
		out(") -- the slot needs its clock and power set up");
	}
	out("\n");
	msdc_base = MSDC0_BASE;
}

static void msdc_self_test(void)
{
	static uint8_t buf[4096] __attribute__((aligned(16)));
	const uint64_t boot_a_lba = 0xf5000; /* from LK's partition table */
	int ok;

	/* a hang in here must not keep the phone: the watchdog brings Android back */
	wr32(TOPRGU_LENGTH, ((31u * 32768u / 512u) << 5) | 0x8u);
	wr32(TOPRGU_RESTART, TOPRGU_RESTART_KEY);
	wr32(TOPRGU_MODE, TOPRGU_MODE_KEY | 0x1u);

	out("eMMC (MSDC0) as LK left it: CFG ");
	out_hex(rd32(MSDC0_BASE + MSDC_CFG));
	out(" PS ");
	out_hex(rd32(MSDC0_BASE + MSDC_PS));
	out(" SDC_CFG ");
	out_hex(rd32(MSDC0_BASE + SDC_CFG));
	out(" STS ");
	out_hex(rd32(MSDC0_BASE + SDC_STS));
	out(" INT ");
	out_hex(rd32(MSDC0_BASE + MSDC_INT));
	out("\n");

	/* the card's state, before touching the data lines at all */
	if (msdc_wait_idle()) {
		wr32(MSDC0_BASE + MSDC_INT, rd32(MSDC0_BASE + MSDC_INT));
		wr32(MSDC0_BASE + SDC_ARG, 1u << 16); /* RCA 1, as LK sets it */
		wr32(MSDC0_BASE + SDC_CMD, CMD_OPC(MMC_SEND_STATUS) | CMD_RSP_R1);
		ok = msdc_wait_int(INT_CMDRDY, INT_CMDTMO | INT_RSPCRCERR, "status");
		out("CMD13 (card status): ");
		if (ok) {
			out_hex(rd32(MSDC0_BASE + SDC_RESP0));
		} else {
			out("failed at ");
			out(msdc_last.what);
			out(" status ");
			out_hex(msdc_last.status);
		}
		out("\n");
	}

	memset(buf, 0, sizeof(buf));
	gpt_detect();
	ok = board_read_bytes((uint64_t)gpt_block_size, 512, buf);
	msdc_dump("GPT header ", gpt_block_size, buf, ok);

	memset(buf, 0, sizeof(buf));
	ok = board_read(boot_a_lba, 1, buf);
	msdc_dump("boot_a     ", boot_a_lba, buf, ok);

	memset(buf, 0, sizeof(buf));
	ok = board_read(boot_a_lba, 8, buf);
	out("boot_a 8 blocks: ");
	if (ok) {
		uint32_t ksize = *(volatile uint32_t *)(buf + 8);
		out(buf[0] == 'A' ? "ANDROID! kernel_size " : "NOT a boot image, ");
		out_hex(ksize);
		out(", block 7 starts ");
		for (int i = 0; i < 8; i++) {
			out_hex(buf[7 * 512 + i]);
			out_putc(' ');
		}
	} else {
		out("FAILED at ");
		out(board_storage_what());
		out(" status ");
		out_hex(board_storage_status());
	}
	out("\n");

	/* How fast, how far, and is what comes back right: a checksum of 4 MB of boot_a, which
	 * is the image this program was flashed in, so the host can check it (tools/msdc-test.sh). */
	for (uint32_t blocks = 256; blocks <= 8192; blocks *= 4) {
		const uint64_t at = 0xa0000000ULL;
		uint64_t total = 4u * 1024 * 1024, t0, t1, f = dbg_freq();
		uint32_t rounds = (uint32_t)(total / (blocks * 512));
		uint32_t sum = 0;
		int fine = 1;

		t0 = dbg_ticks();
		for (uint32_t r = 0; r < rounds && fine; r++) {
			fine = board_read(boot_a_lba + (uint64_t)r * blocks, blocks, (void *)at);
			for (uint32_t i = 0; fine && i < blocks * 512 / 4; i++) {
				sum = sum * 31u + *(volatile uint32_t *)(at + 4 * i);
			}
		}
		t1 = dbg_ticks();
		out("read 4 MB in ");
		out_dec(blocks);
		out("-block chunks: ");
		if (!fine) {
			out("FAILED at ");
			out(board_storage_what());
			out(" status ");
			out_hex(board_storage_status());
		} else {
			out_dec((t1 - t0) * 1000 / f);
			out(" ms, ");
			out_dec(total * f / ((t1 - t0) ? (t1 - t0) : 1) / 1024 / 1024);
			out(" MB/s (the checksum takes part of that), sum ");
			out_hex(sum);
		}
		out("\n");
	}
	msdc1_probe();
}
#endif /* MSDC_TEST */

#if UFS_TEST
/*
 * The UFS host as LK leaves it, then a few reads: the GPT header, boot_a's ANDROID! magic, and
 * how fast a megabyte comes back. This is the bring-up check for mtk/loader/ufs.h -- run it before
 * trusting a single byte of it, because nothing above it in this file has ever talked to a UFS
 * controller and there is no way to find out otherwise than on the phone.
 *
 * Every read here is the same board_read the real path uses, so a pass means the real path works.
 */
/*
 * The big report.
 *
 * Everything below this line exists because a photograph of a 67-column console at scale 2 is
 * not readable: the last two rounds were spent guessing at digits instead of reading them. This
 * paints twelve values at scale 4 on a cleared screen -- roughly 16 lines of 33 characters, which
 * a phone camera resolves comfortably -- and nothing else. It is called after the test, so the
 * long log still happens (and still lands in ramoops); this is the part that a human can see.
 *
 * Order is deliberate: the things that decide the next build come first.
 *   1 build / 2 link verdict / 3 the three pre_link writes / 4 HCE / 5 the list / 6 the request
 */
static uint32_t ufs_nop_ok;

/* What the self test found, for the report. Its own numbers, not a re-probe. */
static uint32_t sel_sys_found, sel_sys_lba, sel_sys_blocks;
static uint32_t sel_boota_found, sel_gpt_parts;
static uint32_t sel_fdev_ok;
static uint32_t sel_sum[3], sel_sum_stable, sel_sum_ok;   /* three reads of the same 64 KiB */

static void ufs_big_report(void)
{
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
	fbc.x = 0;
	fbc.y = fbc.top = 0;
	fbc.scale = 4;
	fbc.fg = 0xffffffffu;
	fbc.bg = 0xff000000u;

	/* Only what a decision needs. The full trace is in the small report. */
	out("85 hs\n");
	out("u"); out_dec(ufs_link_already_up);
	out(" r"); out_dec(ufs_link_ran);
	out(" l"); out_dec(ufs_link_ok);
	out(" res "); out_hex(ufs_link_result);
	out(" uecpa "); out_hex(ufs_link_uecpa_after);
	out("\n");
	out("rst "); out_dec(ufs_reset_ok);
	out("/"); out_hex(ufs_reset_hcs);
	out(" en "); out_dec(ufs_enable_ok);
	out("/"); out_hex(ufs_enable_hcs);
	out("\n");
	out("IS "); out_hex(ufs_last_is);
	out(" hcs "); out_hex(ufs_hcs_at_ring);
	out("\n");
	out("1st rsr "); out_hex(ufs_first_rsr);
	out(" dbr "); out_hex(ufs_first_dbr_before);
	out("->"); out_hex(ufs_first_dbr_after);
	out("\n");
	out("1st is "); out_hex(ufs_first_is_before);
	out("->"); out_hex(ufs_first_is_after);
	out(" ex "); out_dec(ufs_first_exit);
	out("\n");
	out("1st ocs "); out_hex(ufs_first_ocs);
	out(" r0 "); out_hex(ufs_first_resp[0]);
	out(" r1 "); out_hex(ufs_first_resp[1]);
	out("\n");
	out("1st resp ");
	for (uint32_t b = 0; b < 8u; b++) { out_hex(ufs_first_resp[b]); out(" "); }
	out("\n");
	out("h8 "); out_dec(ufs_hiber_exit_ok);
	out(" nop "); out_dec(ufs_nop_ok);
	out(" ocs "); out_hex(ufs_nop_ocs);
	out(" r0 "); out_hex(ufs_nop_resp0);
	out("\n");
	out("tail-rsr "); out_hex(ufs_hcs_trace[9]);
	out(" clr "); out_hex(ufs_hcs_after_clr);
	out(" stop "); out_hex(ufs_run_after_stop);
	out("\n");
	out("1st hcs "); out_hex(ufs_first_hcs);
	out(" calls "); out_dec(ufs_run_calls);
	out(" lun "); out_dec(ufs_lun);
	out("\n");
	/* Did the answer turn into data?  That is the whole question now. */
	out("gpt "); out_dec(gpt_block_size);
	out(" xnusys "); out_dec(sel_sys_found);
	out(" ");
	out_dec(sel_sys_lba);
	out("+"); out_dec(sel_sys_blocks);
	out("\n");
	out("boot_a "); out_dec(sel_boota_found);
	out(" gptpart "); out_dec(sel_gpt_parts);
	out("\n");
	out("tur r6 "); out_dec(ufs_last_resp6);
	out(" r7 "); out_dec(ufs_last_status7);
	out(" seen "); out_dec(ufs_tur_resp_seen);
	out("\n");
	out("sum "); out_dec(sel_sum_ok);
	out(" same "); out_dec(sel_sum_stable);
	out(" "); out_hex(sel_sum[0]);
	out(" "); out_hex(sel_sum[1]);
	out(" "); out_hex(sel_sum[2]);
	out("\n");
	out("fdev "); out_dec(sel_fdev_ok);
	out(" st "); out_dec(ufs_fdevinit_stage);
	out(" flag "); out_dec(ufs_fdevinit_flag);
	out(" try "); out_dec(ufs_fdevinit_tries);
	out(" ocs "); out_hex(ufs_query_ocs);
	out(" resp "); out_dec(ufs_query_resp);
	out("\n");
	out("last "); out(ufs_last.what ? ufs_last.what : "-");
	out(" "); out_hex(ufs_last.status);
	out("\n");
}

static void ufs_self_test(void)
{
	static uint8_t buf[4096] __attribute__((aligned(16)));
	const uint64_t boot_a_lba = 0x3d100000 / 512;   /* scatter: linear_start_addr, in 512-byte sectors */
	int ok;

	/*
	 * Arm the escape before arming the watchdog: MARK_DONE means "do not run this again", and
	 * mtkloader_main reads it first thing on the way back up. A test that hangs is then a
	 * single reset instead of a loop, and the log it wrote is still in ramoops to read from
	 * Android. Setting it after the test would only cover the case that needs it least.
	 */
	marker_set(MARK_DONE);
	ufs_step(1);            /* 1: about to arm the watchdog */

	/* a hang in here must not keep the phone: the watchdog brings Android back */
	wr32(TOPRGU_LENGTH, ((31u * 32768u / 512u) << 5) | 0x8u);
	wr32(TOPRGU_RESTART, TOPRGU_RESTART_KEY);
	wr32(TOPRGU_MODE, TOPRGU_MODE_KEY | 0x1u);

	/*
	 * Write a harmless value into a register and read it back. Reads of this controller work
	 * (everything below is what LK left), but every write we have tried has been discarded:
	 * HCE stayed 0 after writing 1, UTRLBA stayed 0 after writing the base. That does not look
	 * like a sequencing mistake any more, it looks like the writes are being refused before
	 * they reach the register file -- which on this SoC is what DEVAPC does when it is set to
	 * let the AP read a peripheral's registers and not write them. So say, in one line, whether
	 * a write can land, before blaming any other part of the handshake.
	 */
	ufs_step(2);            /* 2: watchdog armed, first register read next */
	out("UFS write probe: IE ");
	out_hex(ufs_rd(UFS_IE));
	ufs_wr(UFS_IE, 0x00010000u);
	{
		uint32_t back = ufs_rd(UFS_IE);

		out(" -> ");
		out_hex(back);
		out(back == 0x00010000u ? " (writable)\n" : " (WIRES IGNORED)\n");
		ufs_wr(UFS_IE, 0);
	}
	/*
	 * Second probe, and it answers a different question: not "did our value land" but "will
	 * this register take any value at all while HCE stays 1". A distinguishable number is
	 * used so the readback cannot be mistaken for LK's leftover base.
	 */
	/*
	 * This used to write 0xdeadb000 into UTRLBA and read it back, to show the register takes a
	 * value at all. It also left that value there, and ufs_init() reads LK's base out of the
	 * same register afterwards -- so the "LK list" every build reported was our probe's number,
	 * and the descriptors went to an address that was never LK's. Read only now.
	 */
	out("UFS UTRLBA (LK's list base): ");
	out_hex(ufs_rd(UFS_UTRLBA));
	out("\n");
	/*
	 * Third probe: auto-hibernate. If the timer is running the link parks itself and the
	 * attribute block can stop taking writes until it is woken; zero disables it outright.
	 */
	/*
	 * LK's own transfer list, read straight out of the register before anything overwrites it.
	 * LK drove this controller well enough to boot Android, so its descriptor is the one piece
	 * of ground truth available for what this hardware expects to see in the list.
	 */
	{
		uint32_t lk_lo = ufs_rd(UFS_UTRLBA);
		uint32_t lk_hi = ufs_rd(UFS_UTRLBAU);

		out("LK list ");
		out_hex(lk_hi);
		out(":");
		out_hex(lk_lo);
		out("\n");
		if (lk_lo) {
			const volatile uint32_t *lk = (const volatile uint32_t *)(uintptr_t)lk_lo;

			out("LK utrd");
			for (uint32_t i = 0; i < 8; i++) {
				out(" ");
				out_hex(lk[i]);
			}
			out("\n");
		}
	}
	out("UFS AHIT ");
	out_hex(ufs_rd(UFS_AHIT));
	out(" CNR ");
	out_hex(ufs_rd(UFS_UTLRCNR));
	out("\n");
	out("UFS (ufshci) as LK leaves it: CAP ");
	out_hex(ufs_rd(UFS_CAP));
	out(" VER ");
	out_hex(ufs_rd(UFS_VER));
	out(" HCS ");
	out_hex(ufs_rd(UFS_HCS));
	out(" IS ");
	out_hex(ufs_rd(UFS_IS));
	out("\n");
	out("  list ");
	out_hex(ufs_rd(UFS_UTRLBAU));
	out_hex(ufs_rd(UFS_UTRLBA));
	out(", run ");
	out_hex(ufs_rd(UFS_UTRLRSR));
	out(", doorbell ");
	out_hex(ufs_rd(UFS_UTRLDBR));
	out("\n");
	out("  device present: ");
	out((ufs_rd(UFS_HCS) & UFS_HCS_DP) ? "yes" : "NO");
	out(", transfer list ready: ");
	out((ufs_rd(UFS_HCS) & UFS_HCS_UTRLRDY) ? "yes" : "NO");
	out(", slots ");
	out_dec(UFS_CAP_NUTRS(ufs_rd(UFS_CAP)));
	out("\n");

	/*
	 * Two probes used to sit here -- ufs_probe_addr_word() and ufs_pick_layout() -- left over
	 * from when the descriptor layout was a guess: they tried the UCD address in every dword
	 * and every field order, and reported which one produced a response. The authoritative
	 * layout is now in include/ufs/ufshci.h and confirmed against the kernel's own driver, and
	 * those probes overwrote utrd[5]/utrd[6] on their way through, so a later request ran with
	 * fields the probe had left behind. Removed rather than kept for company.
	 */
	out("ufs: taking the list over\n");
	/*
	 * ufs.h is included as text, so its static hooks are visible here: point them at the
	 * screen before the walk starts, and a hang inside it says which unit it was on.
	 */
	ufs_trace = out;
	ufs_trace_hex_fn = out_hex;
	/*
	 * The four-variant probe served its purpose: it established that the command type is read
	 * from bits 8:11 of DW0. It is gone now, and it has to stay gone -- its v2/v3 spellings use a
	 * command type this controller rejects, so the door bell for those slots never clears. A
	 * door-bell bit left standing is not harmless: the next request sets the same bit, the
	 * controller sees it already set and treats the new request as still pending, and every
	 * transfer after that times out. That is exactly what the last boot's tail showed.
	 */
	out("\n");
	ufs_step(3);            /* 3: ufs_init returned: reset + HCE enable + UIC done */
	/*
	 * The device must finish its own initialisation before it will execute a SCSI command.
	 * Build 71 measured exactly that state: NOP OUT answers (NOP IN), a TUR completes with
	 * OCS_SUCCESS and a RESPONSE UPIU, and the UPIU's response byte is still 1 -- target
	 * failure -- so READ(10) returns nothing and every partition lookup fails. Linux's
	 * ufshcd_complete_dev_init() does SET_FLAG fDeviceInit and polls until the device clears
	 * it. Nothing has been read off this disk before that is done.
	 */
	sel_fdev_ok = (uint32_t)ufs_fdevice_init();
	ufs_pick_lun();

	/* Does the device-side UTP layer answer at all?  Linux asks this with a
	 * NOP OUT before it trusts any SCSI command (ufshcd_verify_dev_init).
	 * Ours only ever sent TURs, which cannot tell "peer silent" from
	 * "list broken". */
	ufs_nop_ok = ufs_nop();
	ufs_step(4);            /* 4: the LUN walk returned */
	/* Reported here, not before ufs_init(): every value below is written by
	 * ufs_init()/ufs_pick_lun(), and printing them earlier showed the static variables
	 * exactly as the linker left them -- all zero, which reads like a dead controller and
	 * sent several builds chasing a fault that had not happened yet. */
	out("IS "); out_hex(ufs_fail_is);
	out(" HCS "); out_hex(ufs_fail_hcs);
	out(" CNR "); out_hex(ufs_fail_cnr);
	out(" resp0 "); out_hex(ufs_fail_resp0);
	out("\n  hcs trace:");
	for (uint32_t i = 0; i < 10; i++) {
		if (!ufs_hcs_where[i]) { break; }
		out("\n    "); out(ufs_hcs_where[i]); out(" "); out_hex(ufs_hcs_trace[i]);
	}
	out("\n  link: hcs "); out_hex(ufs_link_hcs_before);
	out(" -> "); out_hex(ufs_link_hcs_after);
	out(" uecpa "); out_hex(ufs_link_uecpa);
	out(" -> "); out_hex(ufs_link_uecpa_after);
	out(" is "); out_hex(ufs_link_is);
	out(" res "); out_dec(ufs_link_result);
	out(" ok "); out_dec(ufs_link_ok);
	/*
	 * The three ufs_mtk_pre_link() DME_SETs, each as "command_ok/result_code". A refused write
	 * shows up here as ok 0 or a non-zero result, which is the difference between "the link was
	 * started properly for the first time" and "the link was started the same half-configured
	 * way as before and will drop again".
	 */
	out("\n  pre: lpm "); out_dec(ufs_pre_lpm_ok); out("/"); out_dec(ufs_pre_lpm_res);
	out(" txlcc "); out_dec(ufs_pre_txlcc_ok); out("/"); out_dec(ufs_pre_txlcc_res);
	out(" stall "); out_dec(ufs_pre_stall_ok); out("/"); out_dec(ufs_pre_stall_res);
	out(" val "); out_hex(ufs_pre_stall_val);
	out("\n  reset "); out_dec(ufs_reset_ok);
	out(" hcs "); out_hex(ufs_reset_hcs);
	out(" enable "); out_dec(ufs_enable_ok);
	out(" hcs "); out_hex(ufs_enable_hcs);
	out("\n  hce: "); out_hex(ufs_hce_after_reset);
	out(" -> "); out_hex(ufs_hce_enabled);
	out("  hcs after enable "); out_hex(ufs_hcs_after_enable);
	out("\n  reset: pdn "); out_hex(ufs_pdn_before);
	out(" -> "); out_hex(ufs_pdn_asserted);
	out(" -> "); out_hex(ufs_pdn_after);
	out(" | ao "); out_hex(ufs_ao_before);
	out(" -> "); out_hex(ufs_ao_asserted);
	out(" -> "); out_hex(ufs_ao_after);
	out("\n  stale clears: "); out_dec(ufs_stale_clears);
	out("  last IS "); out_hex(ufs_last_is);
	out(" DBR "); out_hex(ufs_last_dbr);
	out("\n  hce: init "); out_hex(ufs_hce_init);
	out(" set "); out_hex(ufs_hce_after_set);
	out(" at ring "); out_hex(ufs_hce_at_ring);
	out("\n  mtk: xoufs "); out_hex(ufs_mtk_xoufs_before);
	out(" -> "); out_hex(ufs_mtk_xoufs_after);
	out("  refclk "); out_hex(ufs_mtk_refclk_before);
	out(" -> "); out_hex(ufs_mtk_refclk_after);
	out(" ack "); out_dec(ufs_mtk_refclk_ack);
	out("\n  sip: mtcmos "); out_dec(ufs_sip_mtcmos_off);
	out("/"); out_dec(ufs_sip_mtcmos_on);
	out("  refclk "); out_dec(ufs_sip_ref_pre);
	out("/"); out_dec(ufs_sip_ref_post);
	out("\n  post link: rsr "); out_hex(ufs_post_link_rsr);
	out(" dbr "); out_hex(ufs_post_link_dbr);
	out("  ring rsr "); out_hex(ufs_rsr_at_ring);
	out("\n  UIC errors: phy "); out_hex(ufs_err_phy);
	out(" dl "); out_hex(ufs_err_dl);
	out(" net "); out_hex(ufs_err_net);
	out(" tr "); out_hex(ufs_err_tr);
	out(" dme "); out_hex(ufs_err_dme);


	/*
	 * The link state, not just the list state. A link left in hibernate by LK still answers
	 * TEST UNIT READY but refuses to move data, and nothing in the list registers showed that.
	 */
	out("UIC hibernate exit: HCS "); out_hex(ufs_uic_hcs);
	out(" IS after "); out_hex(ufs_uic_status);
	out(" res "); out_dec(ufs_uic_result);
	out((ufs_uic_status & (UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL)) ? " (link out of H8)\n" : " (no completion: not in H8, or no UIC channel)\n");

	/* take the list over, then ask the unit whether it is there at all */
	out("LUN scan:");
	for (uint32_t l = 0; l < UFS_LUN_MAX; l++) {
		out(" ");
		out_hex(ufs_lun_status[l]);
	}
	out("\n");
	/* What the controller actually wrote back for the last request: descriptor, then response. */
	out("  ucd pa "); out_hex((uint32_t)(ufs_place_pa >> 32)); out(":"); out_hex((uint32_t)ufs_place_pa);
	out(" bytes "); out_dec(ufs_place_bytes);
	out(" canary "); out_hex(ufs_canary_back);
	out(ufs_canary_back == 0xc0de1234u ? " (ucd is real)\n" : " (UCD WRITE DROPPED)\n");
	out("  utrd (index: value):");
	for (uint32_t i = 0; i < 8; i++) {
		out("\n    "); out_dec(i); out(": "); out_hex(ufs_last_utrd[i]);
	}
	/* The same eight dwords read straight out of the list the controller was told to walk.
	 * ufs_last_utrd is a copy taken at ring time; if this differs from it, the difference is
	 * something the controller itself did to the descriptor, not something we printed wrong. */
	{
		uint64_t lb = ((uint64_t)ufs_list_hi << 32) | ufs_list_lo;
		const volatile uint32_t *live = (const volatile uint32_t *)(uintptr_t)lb;
		out("\n  live@"); out_hex(lb); out(":");
		for (uint32_t i = 0; i < 8; i++) { out(" "); out_hex(live[i]); }
	}
	out("\n  wrote:");
	for (uint32_t i = 0; i < 4; i++) { out(" "); out_hex(ufs_place_probe[i]); }
	out(" list@"); out_hex(ufs_place_probe_addr);
	out("\n  resp:");
	for (uint32_t i = 0; i < 16; i++) { out(" "); out_hex(ufs_last_resp[i]); }
	out("\n");

	/* Why the first one that was not 0 said what it said: the transport names itself. */
	for (uint32_t l = 0; l < UFS_LUN_MAX; l++) {
		if (ufs_lun_status[l] == 0u) { continue; }
		out("  lun ");
		out_dec(l);
		out(": ocs ");
		out_hex(ufs_lun_ocs[l]);
		out(" ");
		out(ufs_lun_why[l] ? ufs_lun_why[l] : "?");
		out("\n");
		break;
	}
	out("TEST UNIT READY on LUN ");
	out_dec(ufs_lun);
	out(": ");
	ok = ufs_tur(ufs_lun);
	if (ok) {
		out("answered");
	} else {
		out("FAILED at ");
		out(ufs_last.what);
		out(" status ");
		out_hex(ufs_last.status);
	}
	out("\n");
	sel_boota_found = (uint32_t)(ok ? 1 : 0);
	ufs_step(ok ? 6u : 5u);  /* 5: TUR failed, 6: TUR answered */
	if (!ok) {
		out("  list moved to: ");
		out_hex(ufs_moved_hi);
		out_hex(ufs_moved_lo);
		out(" (cold ");
		out_hex(ufs_lk_cold_hi);
		out_hex(ufs_lk_cold_lo);
		out(")");
		out(", run ");
		out_hex(ufs_rd(UFS_UTRLRSR));
		out(", HCE ");
		out_hex(ufs_hce_before);
		out(" -> ");
		out_hex(ufs_hce_after);
		out("\n");
		out("  before: doorbell ");
		out_hex(ufs_door_before);
		out(" IS ");
		out_hex(ufs_is_before);
		out("; after clear: doorbell ");
		out_hex(ufs_door_after_clear);
		out(" run ");
		out_hex(ufs_run_after_stop);
		out("\n");
		return;
	}

	/*
	 * The GPT header. Its LBA is 1 in the disk's own logical blocks, which is not necessarily
	 * 512 bytes: on this UFS it is 4096, so the header sits at byte 4096 and reading "LBA 1" as
	 * a 512-byte sector lands on zeros. Ask the disk which it is.
	 */
	/*
	 * gpt_detect() is what runs ufs_capacity(), so it has to be asked before any of that
	 * state means anything - printing it first showed a stage of 0 and a descriptor of zeros,
	 * which was the initial value of a variable nothing had written yet.
	 */
	{
		int detected = gpt_detect();
		sel_gpt_parts = (uint32_t)detected;
		out("CAP stage "); out_dec(ufs_cap_stage);
		out(" utrd0 "); out_hex(ufs_cap_utrd0);
		out(" utrd7 "); out_hex(ufs_cap_utrd7); out("\n  resp:");
		for (uint32_t i = 0; i < 16; i++) { out(" "); out_hex(ufs_cap_resp[i]); }
		out("\ncmd@0:");
	for (uint32_t i = 0; i < 16; i++) { out(" "); out_hex(ufs_cap_cmd[i]); }
	out("\n@128:");
	for (uint32_t i = 0; i < 16; i++) { out(" "); out_hex(ufs_cap_at128[i]); }
	out("\n@1024:");
	for (uint32_t i = 0; i < 16; i++) { out(" "); out_hex(ufs_cap_at1024[i]); }
	out("\nREAD CAPACITY data:");
		for (uint32_t i = 0; i < 16; i++) { out(" "); out_hex(ufs_cap_raw[i]); }
		out("\n  last "); out_hex(ufs_cap_last);
		out(" lbs "); out_hex(ufs_cap_lbs);
		/*
		 * The descriptor comes back cleared and the response UPIU is not at the offset we
		 * named, so look for both instead of guessing: the response starts with 0x21 and the
		 * data we asked for starts with "EFI PART". One number each, readable off a photo.
		 */
		{
			volatile const uint8_t *a = (volatile const uint8_t *)UFS_AREA_UCD;
			uint32_t found = 0xffffffffu;

			for (uint32_t at = 0; at + 16u < 65536u; at += 4u) {
				if (a[at] == 0x21u &&
				    (a[at + 1u] == 0x00u || a[at + 1u] == 0x40u || a[at + 1u] == 0x80u)) {
					found = at;
					break;
				}
			}
			out("resp UPIU at "); out_hex(found); out("\n");
		}
		{
			volatile const uint8_t *b = (volatile const uint8_t *)UFS_AREA_BUF;
			uint32_t ef = 0xffffffffu;

			for (uint32_t at = 0; at + 8u < (256u << 10); at++) {
				if (b[at] == 'E' && b[at + 1u] == 'F' && b[at + 2u] == 'I' &&
				    b[at + 3u] == ' ' && b[at + 4u] == 'P' && b[at + 5u] == 'A' &&
				    b[at + 6u] == 'R' && b[at + 7u] == 'T') {
					ef = at;
					break;
				}
			}
			out("EFI PART at "); out_hex(ef); out("\n");
		}
		out("\nGPT header: block ");
		out_dec(detected);
	}
	out(": ");
	memset(buf, 0, sizeof(buf));
	ok = board_read_bytes((uint64_t)gpt_block_size, 512, buf);
	if (ok && !memcmp_n(buf, "EFI PART", 8)) {
		out("EFI PART, entries at ");
		out_hex(*(volatile uint64_t *)(buf + 72));
		out(" count ");
		out_dec(*(volatile uint32_t *)(buf + 80));
	} else if (ok) {
		out("read, but not a GPT header: ");
		for (int i = 0; i < 8; i++) {
			out_hex(buf[i]);
			out_putc(' ');
		}
	} else {
		out("FAILED at ");
		out(ufs_last.what);
		out(" status ");
		out_hex(ufs_last.status);
	}
	out("\n");

	ufs_step(7);            /* 7: GPT read path returned */
	/* boot_a: the image this loader was flashed inside, so the magic is known in advance */
	memset(buf, 0, sizeof(buf));
	ok = board_read(boot_a_lba, 1, buf);
	out("boot_a      LBA ");
	out_hex(boot_a_lba);
	out(": ");
	if (ok) {
		out(buf[0] == 'A' ? "ANDROID! kernel_size " : "NOT a boot image, first bytes ");
		if (buf[0] == 'A') {
			out_hex(*(volatile uint32_t *)(buf + 8));
		} else {
			for (int i = 0; i < 8; i++) {
				out_hex(buf[i]);
				out_putc(' ');
			}
		}
	} else {
		out("FAILED at ");
		out(ufs_last.what);
		out(" status ");
		out_hex(ufs_last.status);
	}
	out("\n");

	/*
	 * Repeatability, before anything is trusted at scale.
	 *
	 * The history of this file has a fault recorded against it: "three different checksums of
	 * three identical reads". That was written when the transport was broken, and it has not been
	 * re-measured since the transport started working. Loading 2.3 GiB into memory on the strength
	 * of one good read would be the wrong order -- if the data path is not deterministic, every
	 * later failure would be blamed on the wrong layer. Read the same 64 KiB three times and print
	 * the three sums: equal sums means the path is sound and the next step is a real load.
	 */
	{
		const uint64_t at = 0xa0000000ULL;      /* free RAM, same buffer the speed test used */
		uint32_t sums[3] = {0, 0, 0};
		int fine = 1;

		for (uint32_t pass = 0; pass < 3u && fine; pass++) {
			memset((void *)at, 0, 64u * 1024u);
			fine = board_read(boot_a_lba, 128, (void *)at);
			for (uint32_t i = 0; fine && i < 128u * 512u / 4u; i++) {
				sums[pass] = sums[pass] * 31u + *(volatile uint32_t *)(at + 4u * i);
			}
		}
		sel_sum_ok = fine ? 1u : 0u;
		sel_sum[0] = sums[0];
		sel_sum[1] = sums[1];
		sel_sum[2] = sums[2];
		sel_sum_stable = (fine && sums[0] == sums[1] && sums[1] == sums[2]) ? 1u : 0u;
	}

	{
		uint64_t lba = 0, blocks = 0;
		if (gpt_find(SYSTEM_PART, &lba, &blocks)) {
			sel_sys_found = 1;
			sel_sys_lba = (uint32_t)lba;
			sel_sys_blocks = (uint32_t)blocks;
		}
	}

	/* how fast, and is what comes back the same every time */
	for (uint32_t blocks = 4; blocks <= 64; blocks *= 4) {
		const uint64_t at = 0xa0000000ULL;
		uint64_t t0, t1, f = dbg_freq();
		/* board_read counts 512-byte sectors, whichever controller answers */
		uint32_t sectors = blocks * (UFS_BLOCK / 512u);
		uint32_t rounds = 256u * 1024u / (sectors * 512u);
		uint32_t sum = 0;
		int fine = 1;

		if (rounds == 0) {
			rounds = 1;
		}
		t0 = dbg_ticks();
		for (uint32_t r = 0; r < rounds && fine; r++) {
			fine = board_read(boot_a_lba + (uint64_t)r * sectors, sectors, (void *)at);
			for (uint32_t i = 0; fine && i < sectors * 512u / 4; i++) {
				sum = sum * 31u + *(volatile uint32_t *)(at + 4 * i);
			}
		}
		t1 = dbg_ticks();
		out("read 256 KB in ");
		out_dec(blocks);
		out("-block chunks: ");
		if (!fine) {
			out("FAILED at ");
			out(ufs_last.what);
			out(" status ");
			out_hex(ufs_last.status);
		} else {
			out_dec((t1 - t0) * 1000 / f);
			out(" ms, sum ");
			out_hex(sum);
		}
		out("\n");
	}
	ufs_step(8);            /* 8: the whole self test returned */
}
#endif /* UFS_TEST */


#if TOUCH_PROBE
/*
 * Room for the firmware and the fourteen header bytes in front of it: plain DRAM that nothing
 * else keeps. The loader's image ends before 0x400c0000, XNU's log ring sits at 0x47b1c9c0 and
 * ramoops at 0x4d0e0000, and DRAM reaches well past this. Growing the loader instead would walk
 * it into the 256 KB chainboot gives it.
 */

/* What an identify report says about itself: the mode it is in, which part it is, which build. */
static void touch_say_identify(const uint8_t *buf, uint32_t got)
{
	if (got < 2) {
		out("  too short to say anything\n");
		return;
	}
	out("  mode ");
	out_hex(buf[1]);
	out(" (");
	out(buf[1] == TCM_MODE_ROMBOOT ? "the ROM bootloader, no firmware" :
	    buf[1] == TCM_MODE_HOSTDOWNLOAD ? "running what it was given" :
	    buf[1] == TCM_MODE_APPLICATION ? "running its application" : "something else");
	out("), part ");
	for (uint32_t i = 2; i < got && i < 18; i++) {
		out_putc(buf[i] >= 0x20 && buf[i] < 0x7f ? (char)buf[i] : ' ');
	}
	if (got >= 22) {
		out(", build ");
		out_hex((uint32_t)buf[18] | ((uint32_t)buf[19] << 8) |
		    ((uint32_t)buf[20] << 16) | ((uint32_t)buf[21] << 24));
	}
	out("\n");
}

/*
 * Wait for the chip to speak, and only then listen.
 *
 * After the go command the chip restarts into the firmware and introduces itself unasked, so
 * there is nothing to ask for and nothing to gain by clocking at it meanwhile -- reading a chip
 * that is not ready only puts its SPI side out of step. So this watches the interrupt line and
 * reads one message each time it goes low, printing whatever comes, until an identify report
 * arrives or the patience runs out. Returns the mode it found, or 0.
 */
static uint8_t touch_wait_identify(uint32_t patience_ms)
{
	static uint8_t buf[320];
	uint8_t code = 0;
	uint32_t got = 0, waited = 0;
	int heard = 0;

	while (waited < patience_ms) {
		if (!tcm_irq()) {
			dbg_delay_ms(10);
			waited += 10;
			continue;
		}
		heard = 1;
		out("  after ");
		out_hex(waited);
		out(" ms: ");
		if (tcm_read(&code, buf, sizeof(buf), &got) != 1) {
			out("the interrupt was up but the message was not readable (");
			out_hex(code);
			out(")\n");
			dbg_delay_ms(20);
			waited += 20;
			continue;
		}
		out("code ");
		out_hex(code);
		out(", ");
		out_hex(got);
		out(" bytes\n");
		if (code == TCM_REPORT_IDENTIFY) {
			touch_say_identify(buf, got);
			return got >= 2 ? buf[1] : 0;
		}
	}
	out(heard ? "  nothing more\n" : "  the interrupt line never went low\n");
	return 0;
}

/*
 * Bits out of a touch report, least significant first within each byte and on across the bytes,
 * which is how the chip packs them (touch_get_report_data in the driver).
 */
static uint32_t touch_bits(const uint8_t *buf, uint32_t at, uint32_t bits)
{
	uint32_t v = 0;

	for (uint32_t i = 0; i < bits; i++) {
		uint32_t bit = at + i;

		v |= (uint32_t)((buf[bit / 8] >> (bit % 8)) & 1u) << i;
	}
	return v;
}

/*
 * What a touch report holds, from the layout the chip itself describes when asked with 0x25:
 *
 *   0x0f 0x01   the capacitive buttons, one bit
 *   0x04        pad to the next byte, so the first byte is theirs alone
 *   0x01        and then, for each finger it can see:
 *   0x06 0x04     which finger      4 bits
 *   0x07 0x04     what it is        4 bits (0 lifted, 1 a finger, 2 a gloved one)
 *   0x08 0x0c     x                12 bits
 *   0x09 0x0c     y                12 bits
 *   0x0a 0x08     pressure          8 bits
 *   0x0b 0x08     width across      8 bits
 *   0x0c 0x08     width down        8 bits
 *   0x0d 0x10     column            16 bits
 *   0x0e 0x10     row               16 bits
 *   0x03 0x00   end of the list, end of the layout
 *
 * Eighty-eight bits a finger, and the report is as long as it has fingers to talk about.
 */
#define TOUCH_FINGER_BITS   88

static int touch_say_report(const uint8_t *buf, uint32_t got)
{
	uint32_t at = 8;                                /* the button byte comes first */
	int fingers = 0;

	while (at + TOUCH_FINGER_BITS <= got * 8) {
		uint32_t which = touch_bits(buf, at, 4);
		uint32_t kind = touch_bits(buf, at + 4, 4);
		uint32_t x = touch_bits(buf, at + 8, 12);
		uint32_t y = touch_bits(buf, at + 20, 12);
		uint32_t pressure = touch_bits(buf, at + 32, 8);

		at += TOUCH_FINGER_BITS;
		if (kind == 0) {
			continue;                       /* that finger has lifted */
		}
		out("  finger ");
		out_hex(which);
		out(" at ");
		out_hex(x);
		out(",");
		out_hex(y);
		out(" pressure ");
		out_hex(pressure);
		out("\n");
		fingers++;
	}
	return fingers;
}

/*
 * Is the touch screen reachable from here, and can it be made to run? The controller keeps its
 * firmware in RAM and loses it at every reset, so Android sends it afresh at every boot and so
 * must we. This walks that same sequence, printing what the chip says at each step: reset into
 * the ROM bootloader, the image as one message, the pause, the go command, and then listen until
 * it introduces itself with the mode it reached.
 */
static void touch_probe(void)
{
	static uint8_t buf[320];
	uint8_t code = 0, mode;
	uint32_t got = 0;
	int r;

	/*
	 * The clock gates of the infra bus (Linux clk-mt6768.c): a bit set in a status register
	 * means that clock is held off. SPI2's is one of them, so open all four registers' worth --
	 * opening a gate only lets a clock run, it tells nothing to do anything.
	 */
	static const uint32_t gate_clr[4] = { 0x8c, 0xa8, 0xc4, 0x84 };

	for (int i = 0; i < 4; i++) {
		wr32(INFRACFG_BASE + gate_clr[i], 0xffffffffu);
	}
	__asm__ volatile("dsb sy");
	touch_clk_cfg2 = rd32(TOPCKGEN_BASE + CLK_CFG_2);
	touch_clk_cfg2_seen = 1;
	spi_pins_to_spi2();
	spi_setup();
	out("\nSPI2: pins ");
	out_hex(rd32(GPIO_BASE + GPIO_MODE3));
	out(", clock mux ");
	out_hex(rd32(TOPCKGEN_BASE + CLK_CFG_2));
	out(", interrupt ");
	out(tcm_irq() ? "asserted\n" : "idle\n");

	/*
	 * A reset first, so the chip is where the sequence expects it however the reboot left it,
	 * and it answers a reset by introducing itself.
	 */
	tcm_reset();
	r = tcm_read_patiently(&code, buf, sizeof(buf), &got);
	if (r != 1) {
		out("nothing after a reset (");
		out_hex(code);
		out(")\n");
		return;
	}
	out("after a reset: code ");
	out_hex(code);
	out("\n");
	touch_say_identify(buf, got);
	if (got < 2 || buf[1] != TCM_MODE_ROMBOOT) {
		out("not the ROM bootloader, so there is nothing to send\n");
		return;
	}

	/*
	 * The message, laid out whole in DRAM so it can go out in one breath: the command, its two
	 * length bytes, the fourteen of header, and the image. Read it back afterwards, because an
	 * address that is not really ours would turn the download into noise.
	 */
	uint8_t *message = (uint8_t *)TOUCH_STAGE;
	uint32_t total = TCM_MESSAGE_HEAD + TCM_ROMBOOT_HEADER + TOUCH_FW_SIZE, bad = 0;

	memcpy(message + TCM_MESSAGE_HEAD + TCM_ROMBOOT_HEADER, touch_fw, TOUCH_FW_SIZE);
	for (uint32_t i = 0; i < TOUCH_FW_SIZE; i++) {
		if (message[TCM_MESSAGE_HEAD + TCM_ROMBOOT_HEADER + i] != touch_fw[i]) {
			bad++;
		}
	}
	if (bad) {
		out("the staged image came back wrong in ");
		out_hex(bad);
		out(" bytes, so that DRAM is not ours\n");
		return;
	}

	out("sending ");
	out_hex(total);
	out(" bytes in one transfer\n");
	tcm_load_firmware(message);
	r = tcm_read_patiently(&code, buf, sizeof(buf), &got);
	out("  answered ");
	if (r == 1) {
		out_hex(code);
		out(code == TCM_STATUS_OK ? " (taken)" : " (not a plain yes)");
		out(", ");
		out_hex(got);
		out(" bytes:");
		for (uint32_t k = 0; k < got && k < 8; k++) {
			out_putc(' ');
			out_hex(buf[k]);
		}
		if (code == TCM_STATUS_OK && got >= 2) {
			out(", blocks written ");
			out_hex((uint32_t)buf[0] | ((uint32_t)buf[1] << 8));
			out(" of ");
			out_hex(TOUCH_FW_SIZE / 16);
		}
	} else {
		out("nothing");
	}
	out("\n");

	out("go: ");
	tcm_run_firmware();
	mode = touch_wait_identify(3000);
	if (mode == TCM_MODE_APPLICATION || mode == TCM_MODE_HOSTDOWNLOAD) {
		out("the firmware is up\n");
	} else {
		out("it did not come up\n");
		return;
	}

	/* What it can do, and how it will lay out a touch report. */
	static const uint8_t asks[2] = { TCM_CMD_GET_APP_INFO, TCM_CMD_GET_REPORT_CONF };

	for (int i = 0; i < 2; i++) {
		if (!tcm_write(asks[i], 0, 0, 0, 0)) {
			continue;
		}
		/*
		 * The chip pushes reports of its own between answers -- a status report, a touch --
		 * and those are not what was asked for, so read past them.
		 */
		for (int skip = 0; skip < 4; skip++) {
			r = tcm_read_patiently(&code, buf, sizeof(buf), &got);
			if (r != 1 || code < 0x10) {
				break;
			}
			out("  (a report on the way: code ");
			out_hex(code);
			out(")\n");
		}
		out("answer to ");
		out_hex(asks[i]);
		out(": code ");
		out_hex(code);
		out(", ");
		out_hex(got);
		out(" bytes:");
		for (uint32_t k = 0; k < got && k < 48; k++) {
			out_putc(' ');
			out_hex(buf[k]);
		}
		out("\n");
	}

	/*
	 * Ask for touch reports. The phone's driver leaves this to the chip's own configuration and
	 * the log shows it never sending the command, but asking costs one message and a chip that
	 * was already reporting does not mind being told again.
	 */
	{
		uint8_t which = TCM_REPORT_TOUCH;

		out("asking for touch reports: ");
		if (tcm_write(TCM_CMD_ENABLE_REPORT, &which, 1, 0, 0) &&
		    tcm_read_patiently(&code, buf, sizeof(buf), &got) == 1) {
			out("code ");
			out_hex(code);
		} else {
			out("no answer");
		}
		out("\n");
	}

	/*
	 * And now the thing itself: a finger on the glass, and whether the chip says so.
	 */
	out("\n>>> TOUCH THE SCREEN NOW -- forty-five seconds <<<\n\n");

	uint32_t fingers = 0, reports = 0;

	for (uint32_t waited = 0; waited < 45000; waited += 5) {
		if (!tcm_irq()) {
			dbg_delay_ms(5);
			continue;
		}
		if (tcm_read(&code, buf, sizeof(buf), &got) != 1) {
			dbg_delay_ms(5);
			waited += 5;
			continue;
		}
		if (code != TCM_REPORT_TOUCH) {
			out("  (code ");
			out_hex(code);
			out(")\n");
			continue;
		}
		reports++;
		fingers += (uint32_t)touch_say_report(buf, got);
	}
	out("\nheard ");
	out_hex(reports);
	out(" reports, ");
	out_hex(fingers);
	out(" of them with a finger on the glass\n");
}
/*
 * Android's driver expects this chip where a power-on leaves it: in its ROM bootloader, with the
 * bus as the preloader set it. Finding anything else it gives up on the touch screen for that
 * whole boot -- "Failed to detect the sensor" -- and the phone comes up unable to be touched. So
 * a probe that has started firmware in the chip, or borrowed its pins, puts both back before
 * Android runs.
 */
static void touch_hand_back(void)
{
	if (touch_clk_cfg2_seen) {
		uint32_t mask = SPI_SEL_FIELD | SPI_SEL_PDN;

		wr32(TOPCKGEN_BASE + CLK_CFG_2_SET, touch_clk_cfg2 & mask);
		wr32(TOPCKGEN_BASE + CLK_CFG_2_CLR, ~touch_clk_cfg2 & mask);
	}
	spi_pins_to_spi2();
	__asm__ volatile("dsb sy");
	tcm_reset();
}
#endif /* TOUCH_PROBE */

/* ---- choosing what to boot ---- */

/*
 * Android unless asked for XNU, once. The keypad's state registers change by themselves (a scan
 * pattern), so keys are not used. The request is a line written to /dev/pmsg0 under Android
 * (tools/boot-xnu.sh): ramoops keeps it in its pmsg zone across the reboot, where this finds it
 * and wipes it, so the boot after XNU is Android again.
 */
static const char boot_xnu_flag[] = "DARWIN-REDWOOD:BOOT-XNU";

/*
 * Whether Android left `flag` in the pmsg zone before rebooting, and if so wipe it, so it is taken
 * once: a run that hangs costs one reboot, after which the loader finds nothing and starts Android.
 */
static int pmsg_take(const char *flag, uint32_t n)
{
	volatile struct ramoops_hdr *h = (volatile struct ramoops_hdr *)RAMOOPS_PMSG;
	volatile char *d = (volatile char *)(RAMOOPS_PMSG + sizeof(struct ramoops_hdr));
	uint32_t cap = RAMOOPS_PMSG_SIZE - sizeof(struct ramoops_hdr);
	int found = 0;

	if (h->sig != RAMOOPS_SIG || h->size > cap) {
		return 0;
	}
	for (uint32_t at = 0; at + n <= cap; at++) {
		uint32_t k = 0;
		while (k < n && d[at + k] == flag[k]) {
			k++;
		}
		if (k == n) {
			for (k = 0; k < n; k++) {
				d[at + k] = '-';
			}
			found = 1;
		}
	}
	return found;
}

static int xnu_requested(void)
{
	return pmsg_take(boot_xnu_flag, sizeof(boot_xnu_flag) - 1);
}

static int boot_choice(void)
{
#ifdef BOARD_STEPS
	fb_rect(780, 40, 120, 80, 0xff00ff00u);    /* b1: entered boot_choice */
#endif
	int xnu = MENU_DEFAULT_XNU || xnu_requested();
#ifdef BOARD_STEPS
	fb_rect(780, 140, 120, 80, 0xffff0000u);   /* b2: pmsg_take returned */
#endif
	uint64_t screen = fbc.base;

#ifdef BOARD_STEPS
	fb_rect(780, 240, 120, 80, 0xff0080ffu);   /* b2a: before dbg_puts */
#endif
	fbc.base = 0;
#ifndef BOARD_STEPS
	dbg_puts(xnu ? "boot: Darwin / XNU (requested)\n" : "boot: Android\n");
#endif
	fbc.base = screen;
#ifdef BOARD_STEPS
	fb_rect(780, 340, 120, 80, 0xff0000ffu);   /* b3: dbg_puts done */
#endif
	fb_rect(0, 600, PANEL_W, 160, 0xff000000u);
#ifdef BOARD_STEPS
	fb_rect(780, 440, 120, 80, 0xffffff00u);   /* b4: fb_rect done */
#endif
	fbc.scale = 3;
	fbc.x = 48;
	fbc.y = 640;
	fbc.fg = xnu ? 0xff40ff40u : 0xffffffffu;
#ifdef BOARD_STEPS
	/*
	 * The screen showed exactly one character of every string this loader ever printed,
	 * at scale 1 and scale 3 alike, so the second call to fbcon_putc is the suspect.
	 * These bars count characters drawn, one colour per character, at both scales.
	 */
	{
		static const char probe[] = "Booting";
		uint32_t px = 780, py = 740;

		for (int k = 0; k < 4; k++) {          /* scale 3: chars 1..4 */
			fbc.scale = 3;
			fbcon_putc(probe[k]);
			fb_rect(px, py, 110, 60, 0xffffffffu);
			py += 80;
		}
		for (int k = 0; k < 4; k++) {          /* scale 1: chars 5..8 */
			fbc.scale = 1;
			fbcon_putc(probe[k + 4]);
			fb_rect(px, py, 110, 60, 0xff40ff40u);
			py += 80;
		}
		fb_rect(px, py, 110, 60, 0xffff00ffu); /* b5: 8 chars drawn */
	}
#endif
#ifndef BOARD_STEPS
	for (const char *c = xnu ? "Booting Darwin / XNU" : "Booting Android"; *c; c++) {
		fbcon_putc(*c);
	}
#endif
	fbc.scale = 1;
	fbc.fg = 0xffffffffu;
#ifdef BOARD_STEPS
	/*
	 * The delay that used to sit here is out while we are still finding out where the boot
	 * stops: it buys nothing but a second of looking at the same pixels, and if the timebase
	 * on this board is the thing that is wrong it would be the last statement reached.  The
	 * marker now goes before it, so "b6 lit, 6 dark" means the delay, and "b6 dark" means the
	 * text loop above it.
	 */
	fb_rect(780, 640, 120, 80, 0xff00ffffu);   /* b6: the big text is done */
	fb_rect(780, 740, 120, 80, 0xffff00ffu);   /* b7: one second of nothing, then return */
	dbg_delay_ms(1000);
#endif
	return xnu;
}

#if GPU_PROBE
#include "mtkpwrap.h"
#include "mtkmali.h"

static const char gpu_probe_flag[] = "DARWIN-REDWOOD:GPU-PROBE";

static void say_mv(const char *name, int on, uint16_t selector)
{
	out(name);
	out(on ? " on at " : " off, set for ");
	out_dec(mt6358_millivolts(selector));
	out(" mV");
}

/*
 * Wake the GPU and ask it what it is. Each step names what it is about to touch before touching
 * it, so if one hangs the panel keeps its last line; and whatever this switches on it switches
 * off again before Android runs, which expects the GPU as the preloader left it.
 */
static void gpu_probe(void)
{
	uint16_t hwcid = 0, vgpu_con = 0, vgpu_dbg = 0, vgpu_sel = 0, vsram_con = 0, vsram_dbg = 0, vsram_sel = 0;
	int we_lit_vgpu = 0, we_lit_vsram = 0, up = 0, stage = 0;
	uint32_t status, status2, clk_cfg1;

	/* ---- the rails ---- */
	out("PMIC: ");
	if (!pwrap_read(MT6358_HWCID, &hwcid)) {
		out("the wrapper does not answer (WACS2_EN ");
		out_hex(rd32(PWRAP_BASE + PWRAP_WACS2_EN));
		out(")\n");
		return;
	}
	out("chip ");
	out_hex(hwcid);
	out("\n  ");
	pwrap_read(MT6358_BUCK_VGPU_CON0, &vgpu_con);
	pwrap_read(MT6358_BUCK_VGPU_DBG1, &vgpu_dbg);
	pwrap_read(MT6358_BUCK_VGPU_ELR0, &vgpu_sel);
	pwrap_read(MT6358_LDO_VSRAM_GPU_CON0, &vsram_con);
	pwrap_read(MT6358_LDO_VSRAM_GPU_DBG1, &vsram_dbg);
	pwrap_read(MT6358_LDO_VSRAM_CON3, &vsram_sel);
	say_mv("VGPU", vgpu_dbg & 1, vgpu_sel);
	out(", ");
	say_mv("VSRAM_GPU", vsram_dbg & 1, vsram_sel);
	out("\n");

	/*
	 * A rail that is off is switched on at the voltage it is already set for -- but only a
	 * voltage a GPU can sensibly run at: nothing here chooses one.
	 */
	if (!(vsram_dbg & 1) || !(vgpu_dbg & 1)) {
		uint32_t mv_gpu = mt6358_millivolts(vgpu_sel), mv_sram = mt6358_millivolts(vsram_sel);

		if (mv_gpu < 575 || mv_gpu > 900 || mv_sram < 750 || mv_sram > 1000) {
			out("  a rail is off and set for a voltage I will not switch on: stopping\n");
			return;
		}
		if (!(vsram_dbg & 1)) {
			out("  switching VSRAM_GPU on\n");
			pwrap_write(MT6358_LDO_VSRAM_GPU_CON0, vsram_con | 1);
			we_lit_vsram = 1;
		}
		if (!(vgpu_dbg & 1)) {
			out("  switching VGPU on\n");
			pwrap_write(MT6358_BUCK_VGPU_CON0, vgpu_con | 1);
			we_lit_vgpu = 1;
		}
		dbg_delay_ms(2);
		pwrap_read(MT6358_BUCK_VGPU_DBG1, &vgpu_dbg);
		pwrap_read(MT6358_LDO_VSRAM_GPU_DBG1, &vsram_dbg);
		out("  now VGPU ");
		out(vgpu_dbg & 1 ? "on" : "STILL OFF");
		out(", VSRAM_GPU ");
		out(vsram_dbg & 1 ? "on" : "STILL OFF");
		out("\n");
		if (!(vgpu_dbg & 1) || !(vsram_dbg & 1)) {
			goto rails_off;
		}
	}

	/*
	 * ---- the processor, as LK left it: read only ----
	 * The three CPU PLLs (Linux clk-mt6768.c: CON1 holds a 22-bit multiplier of 26 MHz / 2^14 and
	 * a power-of-two post divider in bits 24..26) and the two PMIC rails the clusters run on.
	 */
	{
		static const struct { const char *name; uint32_t con1; } plls[3] = {
			{ "ARMPLL (big)", 0x20c }, { "ARMPLL_L (little)", 0x21c }, { "CCIPLL (bus)", 0x22c },
		};
		uint16_t v11 = 0, v12 = 0, s11 = 0, s12 = 0;

		for (int i = 0; i < 3; i++) {
			uint32_t c = rd32(0x1000c000ULL + plls[i].con1);
			uint64_t mhz = (26ull * (c & 0x3fffffu)) >> 14;

			mhz >>= (c >> 24) & 7u;
			out(plls[i].name);
			out(": CON1 ");
			out_hex(c);
			out(" = ");
			out_dec((uint32_t)mhz);
			out(" MHz\n");
		}
		pwrap_read(0x139e, &v11);               /* VPROC11_DBG0: the voltage it is at */
		pwrap_read(0x141e, &v12);               /* VPROC12_DBG0 */
		pwrap_read(0x1b60, &s11);               /* VSRAM_PROC11_DBG0 (bits 14..8) */
		out("VPROC11 ");
		out_dec(mt6358_millivolts(v11));
		out(" mV, VPROC12 ");
		out_dec(mt6358_millivolts(v12));
		out(" mV, VSRAM_PROC11 ");
		out_dec(mt6358_millivolts((uint16_t)(s11 >> 8)));
		out(" mV\n");
		(void)s12;
	}

	/* ---- what the preloader left ---- */
	status = rd32(SPM_BASE + SPM_PWR_STATUS);
	status2 = rd32(SPM_BASE + SPM_PWR_STATUS_2ND);
	clk_cfg1 = rd32(TOPCKGEN_BASE + CLK_CFG_1);
	out("SPM power status ");
	out_hex(status);
	out(" / ");
	out_hex(status2);
	out(", mfg_sel ");
	out_hex(clk_cfg1 & 0xff);
	out(", bus protection ");
	out_hex(rd32(INFRACFG_BASE + INFRA_TOPAXI_PROTECTEN_STA1));
	out("\n");
	for (int i = 0; i < 4; i++) {
		if (status & mali_domains[i].status) {
			out("  ");
			out(mali_domains[i].name);
			out(" is on already; leaving the GPU alone\n");
			goto rails_off;
		}
	}

	/* ---- the clock, then the power ---- */
	out("mfg_sel to syspll_d3 (364 MHz)\n");
	wr32(TOPCKGEN_BASE + CLK_CFG_1_CLR, MFG_SEL_FIELD | MFG_SEL_PDN);
	wr32(TOPCKGEN_BASE + CLK_CFG_1_SET, MFG_SEL_SYSPLL_D3);
	__asm__ volatile("dsb sy");
	dbg_delay_us(10);

	for (up = 0; up < 4; up++) {
		const struct mali_domain *d = &mali_domains[up];

		out("  powering ");
		out(d->name);
		out(": ");
		if (!mali_domain_on(d, &stage)) {
			out("stalled at step ");
			out_dec((uint32_t)stage);
			out(", its control ");
			out_hex(rd32(SPM_BASE + d->con));
			out(", status ");
			out_hex(rd32(SPM_BASE + SPM_PWR_STATUS));
			out("\n");
			mali_domain_off(d);             /* whatever half of it did come up */
			break;
		}
		out("on\n");
		if (up == 1) {
			out("  opening the bus to the GPU block\n");
			mali_bus_open();
		}
	}
	if (up < 4) {
		goto power_off;
	}

	/* ---- inside the block ---- */
	out("mfgcfg clock gates ");
	out_hex(rd32(MFGCFG_BASE + MFG_CG_CON));
	wr32(MFGCFG_BASE + MFG_CG_CLR, MFG_CG_BG3D);
	__asm__ volatile("dsb sy");
	out(" -> ");
	out_hex(rd32(MFGCFG_BASE + MFG_CG_CON));
	out("\n");

	{
		uint32_t id = rd32(MALI_BASE + GPU_ID);
		uint32_t shaders = rd32(MALI_BASE + GPU_SHADER_PRESENT);
		uint32_t tiler = rd32(MALI_BASE + GPU_TILER_PRESENT);
		uint32_t l2 = rd32(MALI_BASE + GPU_L2_PRESENT);

		out("GPU_ID ");
		out_hex(id);
		out(": ");
		out(mali_name(id));
		out(" r");
		out_dec((id >> 12) & 0xf);
		out("p");
		out_dec((id >> 4) & 0xff);
		out("\n  L2 features ");
		out_hex(rd32(MALI_BASE + GPU_L2_FEATURES));
		out(", tiler ");
		out_hex(rd32(MALI_BASE + GPU_TILER_FEATURES));
		out(", memory ");
		out_hex(rd32(MALI_BASE + GPU_MEM_FEATURES));
		out(", MMU ");
		out_hex(rd32(MALI_BASE + GPU_MMU_FEATURES));
		out("\n  address spaces ");
		out_hex(rd32(MALI_BASE + GPU_AS_PRESENT));
		out(", job slots ");
		out_hex(rd32(MALI_BASE + GPU_JS_PRESENT));
		out(", threads ");
		out_dec(rd32(MALI_BASE + GPU_THREAD_MAX));
		out("\n  shader cores ");
		out_hex(shaders);
		out(", tiler ");
		out_hex(tiler);
		out(", L2 ");
		out_hex(l2);
		out("\n");

		/* A soft reset: what any driver does first, and the GPU answers it with an interrupt bit. */
		wr32(MALI_BASE + GPU_INT_MASK, 0);
		wr32(MALI_BASE + GPU_INT_CLEAR, 0xffffffffu);
		wr32(MALI_BASE + GPU_CMD, GPU_CMD_SOFT_RESET);
		out("soft reset: ");
		out(mali_wait(MALI_BASE + GPU_INT_RAWSTAT, GPU_IRQ_RESET_COMPLETED, GPU_IRQ_RESET_COMPLETED, 100) ?
		    "done\n" : "NO ANSWER\n");
		wr32(MALI_BASE + GPU_INT_CLEAR, 0xffffffffu);

		/* Its parts, each asked to power up and watched until it says it is ready. */
		out("L2 power: ");
		wr32(MALI_BASE + GPU_L2_PWRON, l2);
		out(mali_wait(MALI_BASE + GPU_L2_READY, l2, l2, 50) ? "ready" : "NOT READY");
		out(", tiler: ");
		wr32(MALI_BASE + GPU_TILER_PWRON, tiler);
		out(mali_wait(MALI_BASE + GPU_TILER_READY, tiler, tiler, 50) ? "ready" : "NOT READY");
		out(", shader cores: ");
		wr32(MALI_BASE + GPU_SHADER_PWRON, shaders);
		out(mali_wait(MALI_BASE + GPU_SHADER_READY, shaders, shaders, 50) ? "ready" : "NOT READY");
		out(" (");
		out_hex(rd32(MALI_BASE + GPU_SHADER_READY));
		out(")\n");

		/*
		 * The first job. Address space 0 is put in identity mode -- no page tables yet, a
		 * GPU address is the physical one -- and slot 1 is given one job: write 0xc0ffee01
		 * into a word of DRAM that the processor cleared. The processor reading it back is
		 * the GPU's job manager, its MMU and its path to memory all working at once.
		 */
		{
			volatile uint32_t *job = (volatile uint32_t *)0x50000000ULL;
			volatile uint32_t *target = (volatile uint32_t *)0x50001000ULL;
			uint64_t as = MALI_BASE + AS0_BASE, js = MALI_BASE + JS_BASE(1);
			uint32_t raw;

			for (int i = 0; i < 16; i++) {
				job[i] = 0;
			}
			*target = 0;
			job[4] = (MALI_JOB_WRITE_VALUE << 1) | 1u;        /* 64-bit pointers */
			job[4] |= 1u << 16;                                /* job index 1 */
			job[8] = 0x50001000u;                              /* where */
			job[9] = 0;
			job[10] = MALI_WRITE_IMM32;                        /* what kind of value */
			job[12] = 0xc0ffee01u;                             /* the value */
			__asm__ volatile("dsb sy");

			wr32(as + AS_TRANSCFG_LO, AS_TRANSCFG_IDENTITY);
			wr32(as + AS_TRANSCFG_HI, 0);
			wr32(as + AS_MEMATTR_LO, 0x88888888u);
			wr32(as + AS_MEMATTR_HI, 0x88888888u);
			wr32(as + AS_COMMAND, AS_COMMAND_UPDATE);
			out("MMU address space 0 (identity): ");
			out(mali_wait(as + AS_STATUS, AS_STATUS_ACTIVE, 0, 50) ? "set\n" : "STILL BUSY\n");

			wr32(MALI_BASE + JOB_INT_MASK, 0);
			wr32(MALI_BASE + JOB_INT_CLEAR, 0xffffffffu);
			wr32(js + JS_HEAD_NEXT_LO, 0x50000000u);
			wr32(js + JS_HEAD_NEXT_HI, 0);
			wr32(js + JS_AFFINITY_NEXT_LO, shaders);
			wr32(js + JS_AFFINITY_NEXT_HI, 0);
			wr32(js + JS_CONFIG_NEXT, JS_CONFIG_AS0_FLUSHED);
			wr32(js + JS_COMMAND_NEXT, JS_COMMAND_START);
			mali_wait(MALI_BASE + JOB_INT_RAWSTAT, (1u << 1) | (1u << 17), 0xffffffffu, 100);
			raw = rd32(MALI_BASE + JOB_INT_RAWSTAT);
			out("job: interrupts ");
			out_hex(raw);
			out(", slot status ");
			out_hex(rd32(js + JS_STATUS));
			out(", MMU fault ");
			out_hex(rd32(as + AS_FAULTSTATUS));
			out(" at ");
			out_hex(rd32(as + AS_FAULTADDRESS_LO));
			out(", job's own status ");
			out_hex(job[0]);
			out("\n  the word the GPU was to write: ");
			out_hex(*target);
			out(*target == 0xc0ffee01u ? "  <- THE GPU RAN A JOB\n" : "  (not written)\n");
			wr32(MALI_BASE + JOB_INT_CLEAR, 0xffffffffu);
		}

		/* And back down, inside out. */
		wr32(MALI_BASE + GPU_SHADER_PWROFF, shaders);
		mali_wait(MALI_BASE + GPU_SHADER_READY, shaders, 0, 50);
		wr32(MALI_BASE + GPU_TILER_PWROFF, tiler);
		mali_wait(MALI_BASE + GPU_TILER_READY, tiler, 0, 50);
		wr32(MALI_BASE + GPU_L2_PWROFF, l2);
		mali_wait(MALI_BASE + GPU_L2_READY, l2, 0, 50);
	}
	wr32(MFGCFG_BASE + MFG_CG_SET, MFG_CG_BG3D);
	__asm__ volatile("dsb sy");

power_off:
	/* Everything that came up goes down again, the cores first and the bridge last. */
	while (up-- > 0) {
		const struct mali_domain *d = &mali_domains[up];

		if (up == 1 && !mali_bus_close()) {
			out("  the bus protection did not close; leaving MFG up\n");
			break;
		}
		out("  powering ");
		out(d->name);
		out(mali_domain_off(d) ? " off\n" : " off: NO ANSWER\n");
	}
	wr32(TOPCKGEN_BASE + CLK_CFG_1_CLR, MFG_SEL_FIELD | MFG_SEL_PDN);
	wr32(TOPCKGEN_BASE + CLK_CFG_1_SET, clk_cfg1 & (MFG_SEL_FIELD | MFG_SEL_PDN));
	__asm__ volatile("dsb sy");

rails_off:
	if (we_lit_vgpu) {
		pwrap_write(MT6358_BUCK_VGPU_CON0, vgpu_con & ~1u);
	}
	if (we_lit_vsram) {
		pwrap_write(MT6358_LDO_VSRAM_GPU_CON0, vsram_con & ~1u);
	}
	out("GPU probe done\n");
}
#endif /* GPU_PROBE */

/* ---- bare-metal USB attach probe ---- */

#if BOARD_USB_PROBE || BOARD_DISP_PROBE || BOARD_PMIF_PROBE || BOARD_MALI_PROBE || \
	BOARD_MALI_POWER || BOARD_MALI_EARLY
/*
 * BOARD_MALI_POWER is in this list for probe_log and probe_flash only, not for the USB macros
 * below: the GPU bring-up runs on the real path, where nothing is on the screen to read. Two
 * builds have now ended with a black panel and a board that does not reset, which is what both
 * "it finished and halted" and "it took a bus fault and stopped where it stood" look like from
 * outside. The answer has to come from flash, and this is the only routine in the tree that
 * writes to flash before the kernel has run.
 */
#define USB_MAC         0x11201000ULL
#define USB_IPPC        0x11203e00ULL
#define USB_PHY         0x11e40000ULL

#define USB_PWR_MGMT    (USB_MAC + 0x2404u)
#define USB_DEV_CONF    (USB_MAC + 0x0800u)
#define USB_COMMON_INTR (USB_MAC + 0x241Cu)
#define USB_TEST_MODE   (USB_MAC + 0x2414u)
#define USB_MISC_CTRL   (USB_MAC + 0x0C84u)
#define USB_LV1_IECR    (USB_MAC + 0x000Cu)
#define USB_EP_IECR     (USB_MAC + 0x008Cu)
#define USB_EP_RST      (USB_MAC + 0x0804u)
#define USB_RXCOUNT0    (USB_MAC + 0x0108u)

#define USB_IP_PW_CTRL1 (USB_IPPC + 0x0004u)

#define PHY_ACR6        (USB_PHY + 0x318u)
#define PHY_DTM0        (USB_PHY + 0x368u)
#define PHY_DTM1        (USB_PHY + 0x36Cu)

#define PM_SOFT_CONN    (1u << 6)
#define PM_SUSPENDM     (1u << 0)
#define PM_HS_ENABLE    (1u << 5)

#define DTM0_FORCE_UART     (1u << 26)
#define DTM0_FORCE_SUSPENDM (1u << 18)
#define DTM0_RG_DATAIN      (0xfu << 10)
#define DTM0_RG_XCVRSEL     (0x3u << 4)
#define DTM0_PART_MASK      ((1u << 23) | (1u << 21) | (1u << 20) | \
                             (1u << 19) | (1u << 17) | (1u << 7) | (1u << 6) | (1u << 2))
#define DTM1_RG_UART_EN     (1u << 16)
#define DTM1_VBUSVALID      (1u << 5)
#define DTM1_SESSEND        (1u << 4)
#define DTM1_AVALID         (1u << 2)
#define ACR6_OTG_VBUSCMP    (1u << 20)
#define ACR6_BC11_SW_EN     (1u << 23)

#define PROBE_LOG_MAX 8192u
static char probe_log[PROBE_LOG_MAX];
static uint32_t probe_log_len;

static void __attribute__((unused)) probe_say(const char *text)
{
	while (*text != '\0' && probe_log_len + 1u < PROBE_LOG_MAX) {
		probe_log[probe_log_len++] = *text++;
	}
	probe_log[probe_log_len] = '\0';
}

static void __attribute__((unused)) probe_hex(uint32_t value)
{
	static const char digits[] = "0123456789abcdef";

	probe_say("0x");
	for (int shift = 28; shift >= 0; shift -= 4) {
		char one[2];

		one[0] = digits[(value >> shift) & 0xfu];
		one[1] = '\0';
		probe_say(one);
	}
}

static void __attribute__((unused)) probe_num(uint32_t value)
{
	char reversed[12];
	int n = 0;

	if (value == 0u) {
		probe_say("0");
		return;
	}
	while (value != 0u && n < 11) {
		reversed[n++] = (char)('0' + value % 10u);
		value /= 10u;
	}
	for (int i = n - 1; i >= 0; i--) {
		char one[2];

		one[0] = reversed[i];
		one[1] = '\0';
		probe_say(one);
	}
}

/*
 * The kernel's USB driver reaches the point where every register matches what Android leaves
 * behind during a working session, and the host still sees nothing. What has not been separated
 * is whether the port can attach at all from this firmware, or whether the kernel's take-over is
 * what breaks it. This answers that by doing the kernel's sequence here, where there is no
 * kernel, no caches and no thread to get the order wrong, then watching the controller instead
 * of jumping to XNU.
 *
 * The registers are the ones hakios.mt6895_usb.c uses, deliberately: same addresses, same bits,
 * same order. If the host enumerates here, the sequence and the wiring are both fine and the
 * difference is on the kernel side. If it stays silent here too, what is missing is something
 * this sequence never had.
 *
 * The result goes into the logbook at the end of xnusys, because the screen is not readable from
 * a shell and the ramoops ring is already spoken for.
 */
static void __attribute__((unused)) probe_flash(uint64_t reason)
{
	if (logbook_kernel_saved) {
		return;
	}
	dcache_clean_range((uint64_t)(uintptr_t)probe_log, PROBE_LOG_MAX);
	(void)save_log_to_flash((uint64_t)(uintptr_t)probe_log, probe_log_len, reason, 0, 0, 0);
}

static void __attribute__((unused)) usb_probe(void)
{
	uint32_t v, seen = 0, live_at = 0, rxbytes = 0;
	int speed_report = 0;

	probe_say("usb probe: entered\n");
	probe_flash(3u);

	probe_say("usb probe: at handover, dev conf ");
	probe_hex(rd32(USB_DEV_CONF));
	probe_say(" power ");
	probe_hex(rd32(USB_PWR_MGMT));
	probe_say(" misc ");
	probe_hex(rd32(USB_MISC_CTRL));
	probe_say("\n");

	/* The host controller half stays powered down, as it is under Android. */
	wr32(USB_IP_PW_CTRL1, rd32(USB_IP_PW_CTRL1) | 1u);

	/* LK leaves a fastboot address in the device configuration. */
	wr32(USB_DEV_CONF, rd32(USB_DEV_CONF) & ~((0x7fu << 24) | 0x7u));

	/* No interrupts, no stale endpoint state. */
	wr32(USB_LV1_IECR, 0xffffffffu);
	wr32(USB_EP_IECR, 0xffffffffu);
	wr32(USB_COMMON_INTR, 0xffffffffu);
	wr32(USB_EP_RST, 1u);

	/* The PHY, in the order the kernel driver uses. */
	v = rd32(PHY_DTM0);
	wr32(PHY_DTM0, v & ~(DTM0_FORCE_UART | DTM0_FORCE_SUSPENDM));
	v = rd32(PHY_DTM0);
	wr32(PHY_DTM0, v & ~(DTM0_RG_XCVRSEL | DTM0_RG_DATAIN | DTM0_PART_MASK));
	wr32(PHY_DTM1, rd32(PHY_DTM1) & ~DTM1_RG_UART_EN);
	wr32(PHY_ACR6, rd32(PHY_ACR6) & ~ACR6_BC11_SW_EN);
	wr32(PHY_ACR6, rd32(PHY_ACR6) | ACR6_OTG_VBUSCMP);
	wr32(PHY_DTM1, (rd32(PHY_DTM1) & ~(DTM1_VBUSVALID | DTM1_AVALID | DTM1_SESSEND)) |
	    (DTM1_VBUSVALID | DTM1_AVALID));
	__asm__ volatile("dsb sy");

	probe_say("usb probe: phy dtm0 ");
	probe_hex(rd32(PHY_DTM0));
	probe_say(" dtm1 ");
	probe_hex(rd32(PHY_DTM1));
	probe_say(" acr6 ");
	probe_hex(rd32(PHY_ACR6));
	probe_say("\n");

	probe_flash(4u);

	/* High speed, then attach. */
	wr32(USB_TEST_MODE, rd32(USB_TEST_MODE) & ~(1u << 5));
	wr32(USB_PWR_MGMT, rd32(USB_PWR_MGMT) | PM_SOFT_CONN | PM_SUSPENDM | PM_HS_ENABLE);
	__asm__ volatile("dsb sy");

	probe_say("usb probe: attached, power ");
	probe_hex(rd32(USB_PWR_MGMT));
	probe_say("\n");

	/*
	 * The host has three seconds to notice. A host that has reset the port sends SOF from then
	 * on, so COMMON_USB_INTR grows a RESET (bit 2) and the configuration's speed field stops
	 * reading zero. Both are printed: either alone can come from the controller by itself,
	 * together they mean the cable carried it.
	 */
	for (uint32_t ms = 0; ms < 3000u; ms++) {
		uint32_t intr = rd32(USB_COMMON_INTR);

		if (intr & ~seen) {
			probe_say("usb probe: ");
			probe_num(ms);
			probe_say(" ms intr ");
			probe_hex(intr);
			probe_say(" conf ");
			probe_hex(rd32(USB_DEV_CONF));
			probe_say("\n");
			wr32(USB_COMMON_INTR, intr);
			seen |= intr;
		}
		if (!speed_report && (rd32(USB_DEV_CONF) & 0x7u) != 0u) {
			speed_report = 1;
			live_at = ms + 1u;
			probe_say("usb probe: speed is set at ");
			probe_num(live_at);
			probe_say(" ms, conf ");
			probe_hex(rd32(USB_DEV_CONF));
			probe_say("\n");
		}
		rxbytes += rd32(USB_RXCOUNT0);
		dbg_delay_ms(1);
	}

	probe_say("usb probe: done. intr ");
	probe_hex(seen);
	probe_say(" conf ");
	probe_hex(rd32(USB_DEV_CONF));
	probe_say(" rxbytes ");
	probe_num(rxbytes);
	probe_say("\n");
	probe_say(seen ? "usb probe: the host spoke to the port\n"
	               : "usb probe: the host never saw the port\n");

	probe_flash(2u);
	out(probe_log);
	out("usb probe: starting Android now\n");
	chain_android();
	for (;;) {
		__asm__ volatile("wfi");
	}
}
#endif /* BOARD_USB_PROBE || BOARD_DISP_PROBE || BOARD_PMIF_PROBE || BOARD_MALI_PROBE */

#if BOARD_DISP_PROBE
/*
 * Everything that could move the picture sideways, read from the block that owns the pixels.
 *
 * The consoles sits in a framebuffer LK hands over, and the kernel's compositor writes the same
 * memory with an identity mapping (boot-arg fbwidth/fbheight are 1080x2460, and the kernel prints
 * "display 1080x2460 on a panel of 1080x2460"). The picture still sits to the right of the glass,
 * which puts the question one layer down: the overlay that scans that memory out to DSI. Its
 * registers say whether the read window starts a few pixels in (L0_OFFSET), whether the source
 * width is wider than the panel (L0_SRC_SIZE), whether the stride is padded (L0_PITCH), and
 * whether the address it scans even matches the memory the console is in.
 *
 * Read-only: a write here would have to be undone by something that knows the display block, and
 * nothing in this project does. The point is to get the numbers off the phone without a shell.
 */
static void __attribute__((unused)) disp_dump(void)
{
	static const struct { const char *what; uint64_t where; } rows[] = {
		{ "ovl src_con ", OVL_BASE + 0x02cu },
		{ "ovl l0_con  ", OVL_BASE + 0x030u },
		{ "ovl l0_size ", OVL_BASE + 0x038u },
		{ "ovl l0_off  ", OVL_BASE + 0x03cu },
		{ "ovl l0_pitch", OVL_BASE + 0x044u },
		{ "ovl l0_addr ", OVL_BASE + 0x0f40u },
		{ "dsi start   ", DSI_BASE + 0x00u },
		{ "dsi size_con", DSI_BASE + 0x38u },
		{ "dsi vact_nl ", DSI_BASE + 0x2cu },
		{ "dsi hsa_wc  ", DSI_BASE + 0x50u },
		{ "dsi hbp_wc  ", DSI_BASE + 0x54u },
		{ "dsi hfp_wc  ", DSI_BASE + 0x58u },
	};
	uint32_t i;

	probe_say("disp probe: entered\n");
	probe_flash(9u);
	for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
		/*
		 * Write the name down *before* the read. With the MMU off every load is a device
		 * access, and a display block whose power or clock domain is off answers those with
		 * a bus abort rather than a value -- the reason this loader skips display_find() on
		 * this board. Flashing first turns a hang into a readable answer: the last name in
		 * the logbook is the register that stopped it.
		 */
		probe_say(rows[i].what);
		probe_flash(9u);
		{
			uint32_t value = *(volatile uint32_t *)(uintptr_t)rows[i].where;

			probe_say(" ");
			probe_hex(value);
			probe_say("\n");
		}
	}
	probe_say("disp probe: fb base ");
	probe_hex((uint32_t)LK_FB_BASE);
	probe_say(" pitch ");
	probe_num(LK_FB_PITCH);
	probe_say("\n");
	probe_flash(9u);
	probe_say("disp probe: flashed\n");
	probe_flash(10u);
}
#endif /* BOARD_DISP_PROBE */

#if BOARD_PMIF_PROBE
/*
 * The SPMI arbiter as the boot chain left it, read before anything here touches it. XNU's own
 * driver finds CMDISSUE_EN reading zero and cannot make it hold a one, and every software
 * channel's state word reads zero too -- a write that is accepted would at least move a state
 * machine. Reading the same registers at the earliest moment this program runs says whether LK
 * handed over a live arbiter or whether the block is only readable.
 *
 * Each read is announced before it happens: this block sits next to guarded pages, and a bus
 * abort with no exception vectors leaves only the last name written out.
 */
static void
pmif_dump(void)
{
	static const struct { const char * what; uint32_t at; } rows[] = {
		{ "pmif init_done  ", 0x1c804000u + 0x0000u },
		{ "pmif inf_en      ", 0x1c804000u + 0x0024u },
		{ "pmif arb_en      ", 0x1c804000u + 0x0150u },
		{ "pmif cmdissue_en ", 0x1c804000u + 0x03b4u },
		{ "pmif timer_ctrl  ", 0x1c804000u + 0x03e0u },
		{ "pmif spi_mode    ", 0x1c804000u + 0x0400u },
		{ "pmif sw0 acc     ", 0x1c804000u + 0x0c00u },
		{ "pmif sw0 sta     ", 0x1c804000u + 0x0c28u },
		{ "pmif sw2 acc     ", 0x1c804000u + 0x0c80u },
		{ "pmif sw2 rdata   ", 0x1c804000u + 0x0c94u },
		{ "pmif sw2 vldclr  ", 0x1c804000u + 0x0ca4u },
		{ "pmif sw2 sta     ", 0x1c804000u + 0x0ca8u },
		{ "pmif sw3 sta     ", 0x1c804000u + 0x0ce8u },
		{ "spmi op_st_ctrl  ", 0x1c801000u + 0x0000u },
		{ "spmi op_st_sta   ", 0x1c801000u + 0x0008u },
		{ "spmi mst_sampl   ", 0x1c801000u + 0x000cu },
		{ "spmi mst_req_en  ", 0x1c801000u + 0x0010u },
	};

	for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
		uint32_t v;

		probe_say("pmif probe: ");
		probe_say(rows[i].what);
		probe_flash(9u);
		v = rd32(rows[i].at);
		probe_hex(v);
		probe_say("\n");
	}

	probe_say("pmif probe: writing cmdissue_en, then sw2 acc\n");
	probe_flash(9u);
	wr32(0x1c804000u + 0x03b4u, 0xfu);
	wr32(0x1c804000u + 0x0c80u, 0x40000000u | (4u << 24) | 0x1eu);
	probe_say("pmif probe: cmdissue_en now ");
	probe_hex(rd32(0x1c804000u + 0x03b4u));
	probe_say(" sw2 sta now ");
	probe_hex(rd32(0x1c804000u + 0x0ca8u));
	probe_say("\n");
	probe_flash(2u);
}
#endif /* BOARD_PMIF_PROBE */

#if BOARD_MALI_PROBE || BOARD_MALI_POWER || BOARD_MALI_EARLY
static void mali_power_on(void);        /* defined below, called from the middle of the probe */
static void mali_acp_up(void);          /* likewise; it lives in the powered section below */
static void mali_hw_dcm_up(void);       /* step eight of the vendor's POWER_ON */
static void mali_gpm_up(void);          /* step nine */

static const struct { const char * what; uint32_t at; } spm_rows[] = {
	{ "spm 0xF34 sta(scpsys)", 0x1c001000u + 0x0f34u },
	{ "spm 0xF38 sta2       ", 0x1c001000u + 0x0f38u },
	{ "spm 0xF3C sta(gpufreq)", 0x1c001000u + 0x0f3cu },
	{ "spm 0xF40 sta2       ", 0x1c001000u + 0x0f40u },
	{ "spm 0xEBC mfg1  MPU  ", 0x1c001000u + 0x0ebcu },
	{ "spm 0xEC0 mfg2  L2   ", 0x1c001000u + 0x0ec0u },
	{ "spm 0xEC4 mfg3  ST0  ", 0x1c001000u + 0x0ec4u },
	{ "spm 0xEC8 mfg4  ST1  ", 0x1c001000u + 0x0ec8u },
	{ "spm 0xECC mfg5  ST4  ", 0x1c001000u + 0x0eccu },
	{ "spm 0xED0 mfg6  ST5  ", 0x1c001000u + 0x0ed0u },
	{ "spm 0xED4 mfg7  ST0T0", 0x1c001000u + 0x0ed4u },
	{ "spm 0xED8 mfg8  ST1T0", 0x1c001000u + 0x0ed8u },
	{ "spm 0xEDC mfg9  ST0T1", 0x1c001000u + 0x0edcu },
	{ "spm 0xEE0 mfg10 ST4T0", 0x1c001000u + 0x0ee0u },
	{ "spm 0xEE4 mfg11 ST5T0", 0x1c001000u + 0x0ee4u },
	{ "spm 0xEE8 mfg12 ST4T1", 0x1c001000u + 0x0ee8u },
};

/*
 * The GPU's power domains, clock gates and identification registers, read before XNU runs.
 *
 * The Mali-G610 of this SoC answers nothing until its domains are on, and the register layout of
 * the SPM's MTCMOS bank for MT6895 is not in any source on this machine: mainline has mt6893 and
 * mt8195 tables whose offsets differ from each other, and neither is this chip. The vendor tree
 * that would settle it is not available either. So this reads the whole candidate range and puts
 * it in the logbook, and the offsets follow by comparing the domains that Android leaves on with
 * the ones it leaves off.
 *
 * Everything here is read-only on purpose. Unlike the display and the SPMI arbiter, where a probe
 * could not make things worse, a wrong write to the SPM's power control words can take the GPU rail
 * down in a way that only a power cycle recovers, and there is no reason to risk it: the read alone
 * separates an on domain from an off one.
 *
 * The page is read one announced dword at a time. 0x1c001000 is inside a mapped window
 * (1c001000-1c001fff power-controller), so a plain read cannot fault, but announcing each read costs
 * nothing and keeps this probe consistent with the SPMI one, whose block does sit next to guarded
 * pages.
 */
static void
mali_dump(void)
{
	uint32_t v;

	/*
	 * The logbook lives at the end of xnusys, so writing to it needs the UFS and the GPT, and
	 * neither is up yet at this point in the boot: this probe runs before the marker dispatch
	 * and therefore before anything has read the flash. ufs_io_ready() is the same entry the
	 * read path uses and it costs an init plus a unit probe, no bulk transfer. Without this the
	 * first probe_flash() silently returned zero -- gpt_find() could not answer, logbook_offset()
	 * gave zero, and save_log_to_flash() treated that as "no room" and wrote nothing. The run
	 * looked healthy from the outside: the phone was back in Android in twenty-nine seconds,
	 * which is what a probe that finishes and hands back looks like.
	 */
	/*
	 * On the panel, not in the flash. Four probe builds in a row came back with the logbook
	 * untouched -- same length, same ticks -- so the write side is not cooperating from this
	 * point in the boot and there is no budget left to find out why. The screen works: every
	 * earlier bring-up question on this board was answered from a photograph. Small text, scale
	 * three, one register a line, and hold the picture long enough to take it.
	 */
	fbcon_init(LK_FB_BASE, LK_FB_PITCH, 0, 3);
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
	fbc.x = 0;
	fbc.y = fbc.top = 0;
	fbc.fg = 0xffffffffu;
	fbc.bg = 0xff000000u;
	out("MALI PROBE\n");

	/* The two register sets every MediaTek SoC keeps: the domain control words are a run of
	 * dwords, one per domain, and the status words are two dwords that come before them. The
	 * windows below cover both the mt6893 and the mt8195 arrangements. */
	{
		/*
		 * The offsets are the ones the vendor kernel uses, taken from the xaga open-source
		 * release (MiCode/Xiaomi_Kernel_OpenSource, branch xaga-s-oss):
		 *   drivers/soc/mediatek/mtk-scpsys-mt6895.c  -- the twelve MFG control words and
		 *                                                the two status words (0xF34/0xF38)
		 *   drivers/gpu/mediatek/gpufreq/v2/include/gpufreq_mt6895.h -- the status words the
		 *                                                GPU driver itself reads (0xF3C/0xF40)
		 * The two disagree by eight, so both pairs are read and whichever holds 0x3FFE after a
		 * power-on is the live one.
		 *
		 * Twelve domains, one dword each, from 0xEBC to 0xEE8:
		 *   mfg1 MPU/CM7   mfg2 L2 cache   mfg3 ST0    mfg4 ST1
		 *   mfg5 ST4       mfg6 ST5        mfg7 ST0T0  mfg8 ST1T0
		 *   mfg9 ST0T1     mfg10 ST4T0     mfg11 ST5T0 mfg12 ST4T1
		 */
		/* the table itself is at file scope, above */

		for (unsigned i = 0; i < sizeof(spm_rows) / sizeof(spm_rows[0]); i++) {
			out(spm_rows[i].what);
			out(" = ");
			out_hex(rd32(spm_rows[i].at));
			out("\n");
		}
	}

	/* The MFG clock gate block at 0x13fbf000 (the DTS calls it mfg_top_config): a bit set is a
	 * clock held off. If the GPU's clocks are gated here, the GPU cannot answer no matter what
	 * the power domains say, and this is the only place that shows it. */
	{
		static const struct { const char * what; uint32_t at; } rows[] = {
			{ "mfgcfg 0x13fbf000 cg_con  ", 0x13fbf000u + 0x0000u },
			{ "mfgcfg 0x13fbf004 cg_set  ", 0x13fbf000u + 0x0004u },
			{ "mfgcfg 0x13fbf008 cg_clr  ", 0x13fbf000u + 0x0008u },
			{ "mfgcfg 0x13fbf00c        ", 0x13fbf000u + 0x000cu },
			{ "mfgcfg 0x13fbf010        ", 0x13fbf000u + 0x0010u },
			{ "mfgcfg 0x13fbf014        ", 0x13fbf000u + 0x0014u },
			{ "mfgcfg 0x13fbf018        ", 0x13fbf000u + 0x0018u },
			{ "mfgcfg 0x13fbf01c        ", 0x13fbf000u + 0x001cu },
		};

		for (unsigned i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
			out(rows[i].what);
			out(" = ");
			out_hex(rd32(rows[i].at));
			out("\n");
		}
	}

	/*
	 * Read first, switch on, read again. The second pass over the same sixteen words is the
	 * whole experiment: 0x1112 before and 0x...d after is the sequence working, and a GPU_ID
	 * that was zero and is 0xa8670005 afterwards is M0 done on this board.
	 */
	out("\n-- switching the twelve domains on --\n");
	mali_power_on();
	out("\n-- the same words again --\n");
	for (unsigned i = 0; i < sizeof(spm_rows) / sizeof(spm_rows[0]); i++) {
		out(spm_rows[i].what);
		out(" = ");
		out_hex(rd32(spm_rows[i].at));
		out("\n");
	}

	out("done; the phone goes back to Android\n");
	dbg_delay_ms(180000);
#if BOARD_MALI_PROBE
	probe_flash(12u);
#endif
}

/*
 * ---------- M0: the GPU's power domains ----------
 *
 * The probe above answered the open question: all twelve MFG control words read 0x1112, which is
 * PWR_ISO | PWR_CLK_DIS | SRAM_PDN | SRAM_PDN_ACK, with PWR_ON, PWR_ON_2ND, PWR_RST_B and both
 * acknowledge bits clear. Nothing has ever switched this GPU on -- not LK, not this loader, not
 * even the Android kernel, which turns the domains on demand and turns them off again. So the
 * sequence has to be written here, and the vendor kernel says exactly what it is.
 *
 * Sources, both from the xaga open-source release (MiCode/Xiaomi_Kernel_OpenSource, xaga-s-oss):
 *   drivers/soc/mediatek/mtk-scpsys-mt6895.c  the twelve control words 0xEBC..0xEE8, one per
 *                                             domain, all with sta_mask GENMASK(31,30),
 *                                             sram_pdn_bits BIT(8), sram_pdn_ack_bits BIT(12),
 *                                             and caps IS_PWR_CON_ON | BYPASS_INIT_ON
 *   drivers/soc/mediatek/mtk-scpsys.c:65-71   the bit meanings
 *   drivers/soc/mediatek/mtk-scpsys.c:550-600 the order they are written in
 *
 * The caps matter: IS_PWR_CON_ON means the acknowledge is read back out of the control word
 * itself (bits 31:30), *not* out of the global status at 0xF34 -- and the probe shows why that
 * distinction is not academic, since 0xF34 reads 0xde0001fd and would look like success no matter
 * what these twelve words say. BYPASS_INIT_ON means the driver may skip saving state first.
 * Neither domain declares SRAM_ISO, so the sram isoint/clkiso pair is not part of this sequence;
 * only the power-down bit has to be cleared and its acknowledge waited for.
 *
 * The order for one domain, from scpsys_power_on() and scpsys_sram_enable():
 *
 *   1  val |=  PWR_ON              (bit 2)      and write
 *   2  val |=  PWR_ON_2ND          (bit 3)      and write
 *   3  wait until (read() & GENMASK(31,30)) == GENMASK(31,30)
 *   4  val &= ~PWR_CLK_DIS         (bit 4)      and write
 *   5  val &= ~PWR_ISO             (bit 1)      and write
 *   6  val |=  PWR_RST_B           (bit 0)      and write
 *   7  val &= ~SRAM_PDN            (bit 8)      and write
 *   8  wait until (read() & BIT(12)) == 0
 *
 * Steps 1, 2 and 3 are the power-up handshake; 4, 5 and 6 undo the isolation in the order the
 * hardware expects; 7 and 8 bring the domain's SRAM out of retention. Doing them out of order, or
 * skipping the acknowledges, is how a domain ends up looking on in one word and not the other.
 *
 * The domains have to come up parents first: mfg1 feeds mfg2, mfg2 feeds mfg3..mfg6, mfg3 feeds
 * mfg7, mfg4 feeds mfg8, mfg7 feeds mfg9, mfg5 feeds mfg10, mfg6 feeds mfg11, mfg10 feeds mfg12.
 * That is the same tree Android reports in pm_genpd_summary, and the array below is written in an
 * order that already respects it.
 *
 * Read-only was the right rule for the probe. It is not the right rule now: this is the thing
 * that has to happen, and if a write here is wrong the phone reboots and the backup is one flash
 * away.
 */

#define MALISPM_BASE        0x1c001000u
#define MALISPM_PWR_ON      (1u << 2)
#define MALISPM_PWR_ON_2ND  (1u << 3)
#define MALISPM_PWR_CLK_DIS (1u << 4)
#define MALISPM_PWR_ISO     (1u << 1)
#define MALISPM_PWR_RST_B   (1u << 0)
#define MALISPM_SRAM_PDN    (1u << 8)
#define MALISPM_SRAM_ACK    (1u << 12)
#define MALISPM_ACK_MASK    0xc0000000u     /* GENMASK(31,30) */

/*
 * mtkdbg.h's wr32() is a plain store, which is right for the console and wrong here: the SPM is
 * behind a bus bridge and the acknowledge has to be observed only after the write has actually
 * landed. Each write below therefore gets its own barrier rather than relying on the next read
 * being ordered behind it by accident.
 */
static void
spm_wr(uint32_t at, uint32_t v)
{
	wr32(MALISPM_BASE + at, v);
	__asm__ volatile ("dsb sy" ::: "memory");
}

static uint32_t
spm_rd(uint32_t at)
{
	uint32_t v = rd32(MALISPM_BASE + at);

	__asm__ volatile ("dsb sy" ::: "memory");
	return v;
}

#define MALI_TOPCKGEN_BASE  0x10000000u
#define MALI_CLK_CFG_4      0x0050u     /* mfg_ref_sel lives in bits 25:24 */
#define MALI_CLK_CFG_4_SET  0x0054u
#define MALI_CLK_CFG_4_CLR  0x0058u
#define MALI_CLK_CFG_30     0x01f0u     /* mfg_sel_0_sel is bit 16 */
#define MALI_CLK_CFG_30_SET 0x01f4u
#define MALI_CLK_CFG_30_CLR 0x01f8u
#define MALI_CLK_UPDATE     0x0004u     /* one bit per mux that has to be latched */
#define MALI_MFG_REF_MASK   0x03000000u /* bits 25:24 */
#define MALI_MFG_REF_26M    (0u << 24)  /* tck_26m_mx9_ck, the always-on crystal */
#define MALI_MFG_REF_SHIFT  19          /* the update bit for mfg_ref_sel */
#define MALI_MFG_SEL_0_BIT  (1u << 16)  /* 0 = mfg_ref_ck, 1 = mfgpll_ck */
#define MALI_MFG_SEL_1_BIT  (1u << 17)  /* 0 = mfgsc_ref_ck, 1 = mfgscpll_ck */
/*
 * The stack half of the clock tree, which is a second, separate mux chain: mfgsc_ref_sel feeds
 * mfgsc_ref_ck, and mfg_sel_1 picks between that and the stack PLL. Its mux lives in CLK_CFG_5 at
 * bit 1:0 (CLK_CFG_4 bit 25:24 is the GPU half), and its update strobe is bit 20 of CLK_UPDATE
 * rather than bit 19. Both come from clk-mt6895.c: mux_clr_set_upd() for CLK_TOP_MFGSC_REF_SEL.
 *
 * Nobody had ever written this register here, and the phone says what it costs: on Android
 * mfgsc_ref_sel reads 218.4 MHz (parent 2, mainpll_d5_d2) while a reset-valued register leaves the
 * stack on parent 0, the 26 MHz crystal -- the same clock the whole SoC idles at, eight times too
 * slow for firmware the CSF core has to execute.
 */
#define MALI_CLK_CFG_5      0x0060u
#define MALI_CLK_CFG_5_SET  0x0064u
#define MALI_CLK_CFG_5_CLR  0x0068u
#define MALI_MFGSC_REF_MASK 0x00000003u /* bits 1:0 */
#define MALI_MFGSC_REF_SHIFT 20         /* the update bit for mfgsc_ref_sel */
/* Parent 2 is mainpll_d5_d2: mainpll is the SoC's own main PLL and runs whether or not anything
 * here starts it, so selecting it costs one mux write and no PLL bring-up. */
#define MALI_MFGSC_REF_MAINPLL 2u
#define MALI_MFG_GATE_BASE  0x13fbf000u
#define MALI_MFG_RPC_BASE   0x13f91000u
#define MALI_MFG_CG_SET     0x0004u
#define MALI_MFG_CG_CLR     0x0008u
#define MALI_MFG_CG_STA     0x0000u
#define MALI_MFG_CG_BG3D    (1u << 0)

/*
 * Route the 26 MHz crystal to the GPU and open its gate. Nothing here writes a PLL.
 *
 * The register numbers come from the vendor clock driver in the open xaga kernel: topckgen's
 * mfg_ref_sel is CLK_CFG_4 bits 25:24 with its gate in bit 31, mfg_sel_0_sel is CLK_CFG_30 bit 16,
 * and the GPU's own gate is bit 0 of the mfgcfg block.
 *
 * That gate is inverted, and this is the one place in the tree where reading the helper names
 * would mislead: clk-gate.c:83 has mtk_cg_enable call mtk_cg_clr_bit, and :358 registers
 * mtk_clk_gate_ops_setclr with is_enabled = mtk_cg_bit_is_cleared. So writing the clear register
 * passes the clock, writing the set register stops it, and a status bit of zero means running.
 * The readback agrees: the phone reports mfgcfg 0x0 as 0x2, whose bit 0 is clear, and its GPU
 * obviously has a clock. Writing SET here would be the mistake, not writing CLR.
 *
 * The crystal is the right first choice because it is already running for the rest of the SoC. If
 * GPU_ID answers with 26 MHz on it, the power work was fine all along and only the clock was
 * missing; if it stays zero, the problem is somewhere else entirely and no amount of PLL work would
 * have found it.
 */
static void
mali_clock_26m(void)
{
	/* 1. the crystal is already running; nothing to start here. Do not touch CFG_4 bit 31:
	 * the phone has it set (readback 0x80008181) and this code used to clear it. */

	/* 2. point mfg_ref_sel at the crystal and latch it */
	wr32(MALI_TOPCKGEN_BASE + MALI_CLK_CFG_4_CLR, MALI_MFG_REF_MASK);
	wr32(MALI_TOPCKGEN_BASE + MALI_CLK_CFG_4_SET, MALI_MFG_REF_26M);
	wr32(MALI_TOPCKGEN_BASE + MALI_CLK_UPDATE, 1u << MALI_MFG_REF_SHIFT);
	__asm__ volatile ("dsb sy" ::: "memory");
	dbg_delay_ms(2);

	/* 3. mfg_sel_0 has to pick the reference rather than the PLL */
	wr32(MALI_TOPCKGEN_BASE + MALI_CLK_CFG_30_CLR, MALI_MFG_SEL_0_BIT);
	__asm__ volatile ("dsb sy" ::: "memory");

	/* 4. and the GPU's own gate, bg3d, is the last one in the way. This gate is inverted:
	 * writing CLR opens it. See the note above the function. */
	wr32(MALI_MFG_GATE_BASE + MALI_MFG_CG_CLR, MALI_MFG_CG_BG3D);
	__asm__ volatile ("dsb sy" ::: "memory");
	dbg_delay_ms(2);

	out("topckgen cfg4 ");
	out_hex(rd32(MALI_TOPCKGEN_BASE + MALI_CLK_CFG_4));
	out(" cfg30 ");
	out_hex(rd32(MALI_TOPCKGEN_BASE + MALI_CLK_CFG_30));
	out(" mfgcg sta ");
	out_hex(rd32(MALI_MFG_GATE_BASE + MALI_MFG_CG_STA));
	out("\n");
}

/*
 * Move the stack half of the GPU onto mainpll_d5_d2, the same 218.4 MHz the phone runs it at.
 *
 * There is nothing to start: mainpll is the SoC's main PLL and is running whatever this loader
 * does, so this is one mux field and one latch. That is why it comes before the MFGPLL work --
 * it cannot fail in a way that leaves the block without a clock, because the parent it selects is
 * already there.
 *
 * The reason it is worth doing at all: on Android mfg_sel_1_ck reads 218400000 while
 * mfgsc_ref_sel reads the same number from parent mainpll_d5_d2, and on a reset-valued register
 * the whole stack half sits on parent 0, the 26 MHz crystal. The CSF core is the one part of this
 * GPU that has to *execute*, and it is the part the stack clock feeds.
 */
static void
mali_clock_stack(void)
{
	/* CFG_5 bit 31 is not a gate to open: the phone reads 0x81818182 there, bit 31 already
	 * set, and clearing it was part of the same mistake the GPU gate had. Left alone. */

	wr32(MALI_TOPCKGEN_BASE + MALI_CLK_CFG_5_CLR, MALI_MFGSC_REF_MASK);
	wr32(MALI_TOPCKGEN_BASE + MALI_CLK_CFG_5_SET, MALI_MFGSC_REF_MAINPLL);
	wr32(MALI_TOPCKGEN_BASE + MALI_CLK_UPDATE, 1u << MALI_MFGSC_REF_SHIFT);
	__asm__ volatile ("dsb sy" ::: "memory");
	dbg_delay_ms(2);

	/* and mfg_sel_1 has to pick the reference rather than the (never started) stack PLL */
	wr32(MALI_TOPCKGEN_BASE + MALI_CLK_CFG_30_CLR, MALI_MFG_SEL_1_BIT);
	__asm__ volatile ("dsb sy" ::: "memory");
	dbg_delay_ms(2);

	out("topckgen cfg5 ");
	out_hex(rd32(MALI_TOPCKGEN_BASE + MALI_CLK_CFG_5));
	out(" cfg30 ");
	out_hex(rd32(MALI_TOPCKGEN_BASE + MALI_CLK_CFG_30));
	out("\n");
}

#define MALI_MFGPLL_BASE    0x13fa0000u
#define MALI_MFGPLL_CON0    0x0008u     /* base_addr: PLL_EN is bit 0 here */
#define MALI_MFGPLL_CON1    0x000cu     /* pcw_addr: PCW in 21:0, POSDIV in 26:24, PCW_CHG bit 31 */
#define MALI_MFGPLL_CON3    0x0014u     /* pwr_addr: PWR_ON bit 0, ISO_EN bit 1 */
#define MALI_PLL_PWR_ON     (1u << 0)
#define MALI_PLL_ISO_EN     (1u << 1)
#define MALI_PLL_EN         (1u << 0)
#define MALI_PCW_CHG        (1u << 31)

/*
 * Start MFGPLL at 300 MHz and move the GPU onto it.
 *
 * 26 MHz was enough to prove the register block answers, but it is not enough to run firmware out
 * of: the CSF boot takes tens of thousands of cycles and 24 MHz is what the whole SoC idles at.
 * 300 MHz is the lowest frequency the vendor table lists (POSDIV_8 overlaps POSDIV_4's range and
 * gives a 2400 MHz VCO, comfortably inside the 1500..3800 MHz window), so it is the gentlest step
 * that still leaves real headroom.
 *
 * The arithmetic is the vendor's: pcw = (((freq_khz / 100) << posdiv) << 14) / 26 + 5) / 10, with
 * posdiv being the exponent rather than the divisor. For 300 MHz and posdiv 3 that is 1512369,
 * or 0x1713b1. CON1 then carries the power-change strobe in bit 31 so the PLL latches the new
 * value instead of gliding to it.
 *
 * The order avoids ever leaving the GPU without a clock: it is on the crystal now, the PLL is
 * brought up beside it, and only then does mfg_sel_0 move across. If the PLL fails to lock the
 * GPU is still running and GPU_ID still answers, which is the state we want to fail into.
 */
static void
mali_pll_300mhz(void)
{
	uint32_t v;

	/* 1. power the PLL up and release its isolation, the vendor's first two steps */
	wr32(MALI_MFGPLL_BASE + MALI_MFGPLL_CON3, MALI_PLL_PWR_ON);
	__asm__ volatile ("dsb sy" ::: "memory");
	dbg_delay_us(2);

	v = rd32(MALI_MFGPLL_BASE + MALI_MFGPLL_CON3);
	wr32(MALI_MFGPLL_BASE + MALI_MFGPLL_CON3, v & ~MALI_PLL_ISO_EN);
	__asm__ volatile ("dsb sy" ::: "memory");
	dbg_delay_us(2);

	/* 2. the frequency itself: 300 MHz, posdiv 8, with the change strobe set */
	wr32(MALI_MFGPLL_BASE + MALI_MFGPLL_CON1, MALI_PCW_CHG | (3u << 24) | 0x1713b1u);
	__asm__ volatile ("dsb sy" ::: "memory");

	/* 3. enable the PLL output and give it the twenty microseconds the vendor allows */
	v = rd32(MALI_MFGPLL_BASE + MALI_MFGPLL_CON0);
	wr32(MALI_MFGPLL_BASE + MALI_MFGPLL_CON0, v | MALI_PLL_EN);
	__asm__ volatile ("dsb sy" ::: "memory");
	dbg_delay_us(50);

	/* 4. only now move mfg_sel_0 from the crystal to mfgpll_ck */
	wr32(MALI_TOPCKGEN_BASE + MALI_CLK_CFG_30_SET, MALI_MFG_SEL_0_BIT);
	__asm__ volatile ("dsb sy" ::: "memory");
	dbg_delay_ms(2);

	out("mfgpll con0 ");
	out_hex(rd32(MALI_MFGPLL_BASE + MALI_MFGPLL_CON0));
	out(" con1 ");
	out_hex(rd32(MALI_MFGPLL_BASE + MALI_MFGPLL_CON1));
	out(" con3 ");
	out_hex(rd32(MALI_MFGPLL_BASE + MALI_MFGPLL_CON3));
	out(" cfg30 ");
	out_hex(rd32(MALI_TOPCKGEN_BASE + MALI_CLK_CFG_30));
	out("\n");
}

/*
 * Put a domain back the way the loader found it.
 *
 * Whether Android can still boot after this loader has raised a domain is the question that has
 * been blocking every build, and raising it is the only difference between the builds that come
 * back and the ones that do not: stage six reads the same registers at the same point in the same
 * path and returns in thirty seconds, while stage one -- which only raises mfg1 -- never returns.
 * That does not say the power-up is wrong, it says leaving it applied is. This undoes it.
 *
 * The order is the reverse of spm_domain_on(): put the SRAM back into retention, reassert the
 * reset, isolate, stop the clock, and only then drop the request. The SRAM power-down has to be
 * acknowledged again, which is why it is polled here as well -- a domain whose memory is still out
 * of retention is not back to its former state, whatever the request bits say.
 */
static void
spm_domain_off(uint32_t at, const char *name)
{
	uint32_t v;
	unsigned spins;

	v = spm_rd(at);
	v |= MALISPM_SRAM_PDN;
	spm_wr(at, v);
	for (spins = 0; spins < 2000000u; spins++) {
		if ((spm_rd(at) & MALISPM_SRAM_ACK) != 0u) {
			break;
		}
	}

	v = spm_rd(at);
	v &= ~MALISPM_PWR_RST_B;
	spm_wr(at, v);
	v |= MALISPM_PWR_ISO;
	spm_wr(at, v);
	v |= MALISPM_PWR_CLK_DIS;
	spm_wr(at, v);

	v = spm_rd(at);
	v &= ~(MALISPM_PWR_ON | MALISPM_PWR_ON_2ND);
	spm_wr(at, v);
	__asm__ volatile ("dsb sy" ::: "memory");
	dbg_delay_ms(1);

	out(name);
	out(" back to ");
	out_hex(spm_rd(at));
	out("\n");
}

static int
spm_domain_on(uint32_t at, const char *name)
{
	uint32_t v;
	unsigned spins;

	/*
	 * All eight steps, every time, even when the acknowledge already looks set.
	 *
	 * The first version returned early when both acknowledge bits were high, and on the real board
	 * that turned mfg3 and mfg4 into a trap: mfg2 coming up sets their acknowledge, so the early
	 * return skipped the isolation release and the SRAM bring-up, leaving both domains reading
	 * 0x00001112 with ISO and CLK_DIS still set. The next domain then never got its second
	 * acknowledge -- mfg5 sat at 0x4000111e for the whole poll budget. Whether the half-on state
	 * is what blocked it or not, doing the same work for every domain costs nothing and removes
	 * the question.
	 */
	v = spm_rd(at);
	v |= MALISPM_PWR_ON;
	spm_wr(at, v);
	v |= MALISPM_PWR_ON_2ND;
	spm_wr(at, v);

	/*
	 * The acknowledge is the slow part: on this board mfg1 and mfg2 answered almost at once, but
	 * the poll budget of the first version -- a hundred thousand reads -- is only a few
	 * milliseconds at the speed dsb sy allows, and the vendor driver allows ten. Two million
	 * reads with the barrier is comfortably past that and still short enough to fail visibly
	 * rather than hang.
	 */
	for (spins = 0; spins < 300000u; spins++) {
		if ((spm_rd(at) & MALISPM_ACK_MASK) == MALISPM_ACK_MASK) {
			break;
		}
	}
	if (spins == 300000u) {
		out(name);
		out(" no power ack: ");
		out_hex(spm_rd(at));
		out("\n");
		return 0;
	}

	v = spm_rd(at);
	v &= ~MALISPM_PWR_CLK_DIS;
	spm_wr(at, v);
	v &= ~MALISPM_PWR_ISO;
	spm_wr(at, v);
	v |= MALISPM_PWR_RST_B;
	spm_wr(at, v);

	v = spm_rd(at);
	v &= ~MALISPM_SRAM_PDN;
	spm_wr(at, v);

	for (spins = 0; spins < 2000000u; spins++) {
		if ((spm_rd(at) & MALISPM_SRAM_ACK) == 0u) {
			break;
		}
	}

	out(name);
	out(" on: ");
	out_hex(spm_rd(at));
	out("\n");
	return 1;
}

/*
 * Where to stop and hand the board back to Android.
 *
 * 0 is the real path: power the block, clock it and go on to XNU. 1..4 stop after that many
 * stages and return to Android instead, which is what makes the stages separable at all -- the
 * panel goes black as soon as this bring-up runs, so the screen cannot say how far it got, but
 * whether Android comes back afterwards is an answer adb can give. 5 is the control: return to
 * Android before touching the GPU at all, so that a stage which does not come back can be told
 * apart from the return path itself being what breaks.
 *
 * Each stage writes what it was to flash under its own reason (41..45), so a run that fails to
 * come back still says in the logbook how far it got.
 */
#define MALI_STAGE_DOMAINS  1
#define MALI_STAGE_CRYSTAL  2
#define MALI_STAGE_IDS      3
#define MALI_STAGE_PLL      4
#define MALI_STAGE_CONTROL  5
#define MALI_STAGE_READS    6
#define MALI_STAGE_CLOCK    7
#define MALI_STAGE_RESTORE  8

static unsigned mali_stage;

static void __attribute__((noreturn))
mali_stage_stop(unsigned stage)
{
	if (stage == MALI_STAGE_RESTORE) {
		probe_say("stage 8: domains raised and then put back as they were\n");
	} else if (stage == MALI_STAGE_CLOCK) {
		probe_say("stage 7: the crystal first, then one domain, the order Android uses\n");
	} else if (stage == MALI_STAGE_READS) {
		probe_say("stage 6: read only, no SPM write at all\n");
	} else if (stage == MALI_STAGE_CONTROL) {
		probe_say("stage 5: control, the GPU was not touched\n");
	} else {
		probe_say("stage: stopping after stage ");
		probe_say(stage == MALI_STAGE_DOMAINS ? "1, the domains" :
		    stage == MALI_STAGE_CRYSTAL ? "2, the 26 MHz crystal" :
		    stage == MALI_STAGE_IDS ? "3, the identification" : "4, the PLL");
		probe_say("; handing the board back to Android\n");
	}
	probe_flash(40u + stage);
	chain_android();
}

/*
 * The AOC handshake, and the SRAM isolation it releases.
 *
 * This is the vendor's __gpufreq_aoc_control(POWER_ON), and it is the first thing their power-on
 * sequence does -- before the buck, before the domains, before the clock. AOC here is not a core
 * of ours; it is the small always-on controller that owns the GPU's SRAM, and the two bits it
 * clears in SOC_BUCK_ISO_CON are what stands between a powered GPU and one whose memories are
 * actually connected to it:
 *
 *   bit 9  AOC_VGPU_SRAM_ISO_DIN    isolation into the SRAM, must be 0 for power on
 *   bit 10 AOC_VGPU_SRAM_LATCH_ENB  the latch that holds it isolated, must be 0 too
 *
 * Both come out of reset set. A GPU with its SRAM held in isolation answers every register
 * access -- the register file is on the bus, not in that memory -- reports itself present, and
 * accepts a firmware load into DRAM. What it cannot do is fetch an instruction, because the
 * memory it would fetch from is not connected. From the outside that is indistinguishable from
 * what this driver has been reporting for six builds: MCU_STATUS reads enabled because the state
 * machine took the request, no translation fault because no fetch was ever attempted, and the
 * sentinel word left exactly as written because not one instruction ran.
 *
 * The semaphore at 0x1C0016AC bit 0 is how the two sides agree the change has landed. The vendor
 * spins on it without a bound because AOC is always on and the bit comes back in a few cycles;
 * bound it here, because a loader that hangs before it has drawn anything is a boot loop with no
 * evidence.
 */
static void
mali_aoc_up(void)
{
	uint32_t v;
	unsigned spins;

	for (spins = 0; spins < 100000u; spins++) {
		if ((rd32(MALISPM_BASE + 0x6acu) & 1u) == 1u) {
			break;
		}
		rd32(MALISPM_BASE + 0x6acu);
	}
	if (spins == 100000u) {
		out("aoc: the semaphore never came back; carrying on\n");
	}

	/* AOCISO -> AOCLHENB: release the isolation, then the latch that holds it */
	v = rd32(MALISPM_BASE + 0x0f30u);
	v &= ~(1u << 9);
	wr32(MALISPM_BASE + 0x0f30u, v);
	v = rd32(MALISPM_BASE + 0x0f30u);
	v &= ~(1u << 10);
	wr32(MALISPM_BASE + 0x0f30u, v);
	__asm__ volatile ("dsb sy" ::: "memory");

	/* signal the hardware semaphore that the change is done */
	wr32(MALISPM_BASE + 0x6acu, rd32(MALISPM_BASE + 0x6acu) | 1u);
	__asm__ volatile ("dsb sy" ::: "memory");

	out("aoc 0x6ac ");
	out_hex(rd32(MALISPM_BASE + 0x6acu));
	out(" iso_con 0xf30 ");
	out_hex(rd32(MALISPM_BASE + 0x0f30u));
	out("\n");
}

static void
mali_power_on(void)
{
	/*
	 * Parents before children, the order the subdomain table in mtk-scpsys-mt6895.c implies:
	 * mfg1 -> mfg2 -> {mfg3, mfg4, mfg5, mfg6}; mfg3 -> mfg7; mfg4 -> mfg8; mfg7 -> mfg9;
	 * mfg5 -> mfg10; mfg6 -> mfg11; mfg10 -> mfg12.
	 */
	static const struct { const char * name; uint32_t at; } tree[] = {
		{ "mfg1 ", 0x0ebcu },
		{ "mfg2 ", 0x0ec0u },
		{ "mfg3 ", 0x0ec4u },
		{ "mfg4 ", 0x0ec8u },
		{ "mfg5 ", 0x0eccu },
		{ "mfg6 ", 0x0ed0u },
		{ "mfg7 ", 0x0ed4u },
		{ "mfg8 ", 0x0ed8u },
		{ "mfg9 ", 0x0edcu },
		{ "mfg10", 0x0ee0u },
		{ "mfg11", 0x0ee4u },
		{ "mfg12", 0x0ee8u },
	};

	/*
	 * As many domains as BOARD_MALI_DOMAINS says, and no stopping on the first failure.
	 *
	 * One is the default because that is what Android does: see the note on the knob in board.h.
	 * The count is the whole variable here -- raising all twelve is what has made every build so
	 * far fail to come back, and it fails with mfg5 onwards holding a request the SPM accepted and
	 * never completes.
	 *
	 * The first run stopped at mfg5, which threw away the answer to the question worth asking:
	 * whether one shader stack is stubborn or the whole bank is. Twelve attempts with a shorter
	 * budget each costs the same as one with a long one, and a run where mfg5 fails but mfg6
	 * succeeds says something completely different from a run where everything past mfg4 fails.
	 */
	{
		unsigned on = 0, stuck = 0;
		unsigned count = BOARD_MALI_DOMAINS;

		if (count > sizeof(tree) / sizeof(tree[0])) {
			count = sizeof(tree) / sizeof(tree[0]);
		}
		/*
		 * The vendor never raises a domain before its clock. scpsys is reached through
		 * pm_runtime_get_sync, and the clock framework turns mfg_ref_ck on as the first thing
		 * that happens in that call -- so on a real boot the domain is switched on by a block
		 * that is already being clocked, and the MFG side of the chip has something to run on
		 * the moment its power comes up. Raising it with no clock at all is a state Android
		 * never creates: mfg1 answers its acknowledge and then sits there, powered, unclocked,
		 * and whatever it drives is left mid-transaction.
		 *
		 * Stage 7 does it the other way round on purpose: crystal, gate, then one domain.
		 */
		if (mali_stage == MALI_STAGE_CLOCK) {
			out("\n-- clock: 26 MHz before any domain --\n");
			mali_clock_26m();
		}
		/*
		 * AOC first, as the vendor does: the SRAM isolation it releases is what makes a
		 * powered GPU one that can read its own instruction memory. Doing it after the domains
		 * would also work on their ordering, but their sequence has it first for a reason --
		 * a domain coming up into isolated SRAM can latch whatever the isolation left on the
		 * bus, and that value then has to be cleared by a power cycle rather than by a write.
		 */
		probe_say("power: about to release the SRAM isolation\n");
		probe_flash(19u);
		out("\n-- aoc --\n");
		mali_aoc_up();
		for (unsigned i = 0; mali_stage != MALI_STAGE_READS && i < count; i++) {
			/*
			 * Before each domain, not after: on the drop path this is the last record
			 * that will have been written, and the domain it names is the one that was
			 * being switched on when the board stopped answering.
			 */
			probe_say("power: about to raise ");
			probe_say(tree[i].name);
			probe_say("\n");
			probe_flash(20u + i);
			if (spm_domain_on(tree[i].at, tree[i].name)) {
				on++;
			} else {
				stuck++;
			}
		}
		if (mali_stage == MALI_STAGE_RESTORE) {
			for (unsigned i = 0; i < count; i++) {
				spm_domain_off(tree[i].at, tree[i].name);
			}
		}
		out("on ");
		out_dec(on);
		out(" of twelve, stuck ");
		out_dec(stuck);
		out("\n");
		out("pdc 0x400 ");
		out_hex(rd32(0x13fbf400u));
		out(" 0x418 ");
		out_hex(rd32(0x13fbf418u));
		out(" 0x41c ");
		out_hex(rd32(0x13fbf41cu));
		out("\n");
	}
	out("gpu_id ");
	out_hex(rd32(0x13000000u));
	out(" shader_prs ");
	out_hex(rd32(0x13000100u));
	out(" stack_prs ");
	out_hex(rd32(0x13000e00u));
	out("\n");

	if (mali_stage == MALI_STAGE_RESTORE) {
		mali_stage_stop(MALI_STAGE_RESTORE);
	}
	if (mali_stage == MALI_STAGE_CLOCK) {
		mali_stage_stop(MALI_STAGE_CLOCK);
	}
	if (mali_stage == MALI_STAGE_READS) {
		mali_stage_stop(MALI_STAGE_READS);
	}

	/*
	 * The domains are up and the register block has been read while it had no clock of its own.
	 * On the stage builds this is the first place worth stopping: if Android comes back from here
	 * but not from the next stage, the clock writes are what break the display.
	 */
	if (mali_stage == MALI_STAGE_DOMAINS) {
		mali_stage_stop(MALI_STAGE_DOMAINS);
	}

	/*
	 * The clock, before anything else. A Mali answers on its register bus only while its clock
	 * runs, and GPU_ID reading zero with the domains up says exactly that: the register block is
	 * returning nothing because nothing is clocking it.
	 *
	 * The safe half of the clock tree is the reference one. topckgen's mfg_ref_sel picks between
	 * 26 MHz (tck_26m_mx9_ck), univpll_d6 and mainpll_d5_d2; the 26 MHz crystal is always running
	 * on this board, so selecting it cannot leave the GPU with no clock at all. The PLL is the
	 * dangerous half -- a wrong PCW leaves the GPU on a frequency nothing else agrees with -- so
	 * this step only reaches the reference, and the PLL comes after GPU_ID is shown to answer.
	 */
	probe_say("clock: about to select the 26 MHz crystal\n");
	probe_flash(32u);
	out("\n-- clock: 26 MHz onto the GPU --\n");
	mali_clock_26m();
	/*
	 * Then the stack half, which is a second mux chain nobody had touched. It selects mainpll_d5_d2
	 * -- already running -- for 218.4 MHz, which is where the phone leaves it.
	 */
	probe_say("clock: about to move the stack half onto mainpll\n");
	probe_flash(30u);
	out("\n-- clock: 218.4 MHz onto the stack --\n");
	mali_clock_stack();
	out("gpu_id ");
	out_hex(rd32(0x13000000u));
	out(" l2_feat ");
	out_hex(rd32(0x13000004u));
	out(" shader_prs ");
	out_hex(rd32(0x13000100u));
	out("\n");
	if (mali_stage == MALI_STAGE_CRYSTAL) {
		mali_stage_stop(MALI_STAGE_CRYSTAL);
	}

	/*
	 * And now the whole identification, on the crystal, before anything raises the frequency.
	 * Every one of these words is documented in Panthor's register map, so reading them here is
	 * what tells us whether the core is the G610 Android claims and how many shader cores the
	 * firmware will be allowed to bring up.
	 */
	{
		probe_say("ids: about to read the identification\n");
		probe_flash(33u);
		static const struct { const char * what; uint32_t at; } ids[] = {
			{ "gpu_id      ", 0x0000u },
			{ "l2_feat     ", 0x0004u },
			{ "core_feat   ", 0x0008u },
			{ "tiler_feat  ", 0x000cu },
			{ "mem_feat    ", 0x0010u },
			{ "mmu_feat    ", 0x0014u },
			{ "as_present  ", 0x0018u },
			{ "csf_id      ", 0x001cu },
			{ "int_raw     ", 0x0020u },
			{ "thread_max  ", 0x00a0u },
			{ "thread_feat ", 0x00acu },
			{ "shader_prs  ", 0x0100u },
			{ "tiler_prs   ", 0x0110u },
			{ "l2_prs      ", 0x0120u },
			{ "stack_prs   ", 0x0e00u },
			{ "coher_feat  ", 0x0300u },
			{ "mcu_status  ", 0x0704u },
		};

		for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
			out(ids[i].what);
			out(" ");
			out_hex(rd32(0x13000000u + ids[i].at));
			out("\n");
		}
	}
	if (mali_stage == MALI_STAGE_IDS) {
		mali_stage_stop(MALI_STAGE_IDS);
	}

	probe_say("clock: about to start the MFGPLL at 300 MHz\n");
	probe_flash(34u);
	out("\n-- clock: 300 MHz onto the GPU --\n");
	mali_pll_300mhz();
	out("gpu_id ");
	out_hex(rd32(0x13000000u));
	out(" csf_id ");
	out_hex(rd32(0x1300001cu));
	out("\n");
	if (mali_stage == MALI_STAGE_PLL) {
		mali_stage_stop(MALI_STAGE_PLL);
	}

	probe_say("acp: about to open the coherence port\n");
	probe_flash(35u);
	out("\n-- coherence port --\n");
	mali_acp_up();

	/*
	 * Steps eight and nine of the vendor's nine-step POWER_ON. They come after the coherence
	 * port for the same reason there: their order is the vendor's and nothing here has a
	 * cheaper justification.
	 */
	probe_say("hw_dcm: about to arm the clock gating and the AXI sleep-protect bridges\n");
	probe_flash(39u);
	out("\n-- hardware dcm --\n");
	mali_hw_dcm_up();
	probe_say("gpm: about to program the performance monitor protector\n");
	probe_flash(40u);
	out("\n-- gpm --\n");
	mali_gpm_up();
}
#endif /* BOARD_MALI_PROBE */

#if BOARD_MALI_POWER || BOARD_MALI_EARLY || BOARD_MALI_PREPOWER
/*
 * The coherence port, and the eight AXI one-to-two splitters behind it.
 *
 * This is the vendor's __gpufreq_acp_control() and it was the one piece of their power-on
 * sequence that this loader had never done. It matters more than its size suggests: 0x168 bit 3
 * is named M3_coherence_enable in their source, and M3 is the core the CSF firmware runs on. The
 * MCU fetches through this port. With it closed the firmware has no path to its own instruction
 * memory, and what that looks like from outside is exactly what we saw -- MCU_STATUS reads enabled
 * because the state machine accepted the request, no translation fault because no transaction was
 * ever issued, and a sentinel word left untouched because not one instruction ran.
 *
 * The other eight registers set the dispatch mode of the AXI 1-to-2 splitters that sit between
 * the shader cores and the interconnect. Their values are not something to reason about; they are
 * copied from the vendor because the same number appears in their POWER_ON path for this SoC and
 * nowhere else, and a wrong dispatch mode would corrupt reads rather than block them, which is a
 * worse failure to debug than the one this is fixing.
 */
static void
mali_acp_up(void)
{
	static const struct { uint32_t at; uint32_t or; } gals[] = {
		{ 0x8e0u, 0x855u }, { 0x8e8u, 0x855u }, { 0x910u, 0x855u }, { 0x918u, 0x855u },
		{ 0x900u, 0x055u }, { 0x908u, 0x055u }, { 0x920u, 0x055u }, { 0x928u, 0x055u },
	};

	/* M3_coherence_enable in bit 3, alongside the three bits the vendor also raises */
	wr32(MALI_MFG_GATE_BASE + 0x168u, rd32(MALI_MFG_GATE_BASE + 0x168u) | 0xfu);
	__asm__ volatile ("dsb sy" ::: "memory");

	for (unsigned i = 0; i < sizeof(gals) / sizeof(gals[0]); i++) {
		uint32_t v = rd32(MALI_MFG_GATE_BASE + gals[i].at);

		wr32(MALI_MFG_GATE_BASE + gals[i].at, v | gals[i].or);
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	dbg_delay_us(10);

	out("acp ");
	out_hex(rd32(MALI_MFG_GATE_BASE + 0x168u));
	out(" gals0 ");
	out_hex(rd32(MALI_MFG_GATE_BASE + 0x8e0u));
	out(" gals4 ");
	out_hex(rd32(MALI_MFG_GATE_BASE + 0x900u));
	out("\n");
}

/*
 * Hardware dynamic clock management, and the AXI sleep-protect bits around it.
 *
 * This is the vendor's __gpufreq_hw_dcm_control(), step eight of their nine-step POWER_ON and one
 * of two steps this loader had never performed. The interesting half is not the clock gating: it
 * is MFG_GLOBAL_CON's four gals_slpprot_idle_en bits (13, 14, 17 and 18). Those arm the AXI
 * low-power bridges that sit between the shader clusters and the rest of the SoC, and with them
 * not set a transaction can park on a bridge that is waiting for an idle window it will never
 * see -- no completion, no error response, nothing to report. That is indistinguishable from the
 * MCU never starting, which is the state this whole exercise has been stuck in.
 *
 * GOP_SOCIF_MST_FREE_RUN and GPU_CLK_FREE_RUN are cleared because the vendor clears them; the
 * name says why. dvfs_hint_cg_en is cleared for the same reason.
 *
 * The last write is not in the mfgcfg block at all: 0x13F91034 bit 0 lives in the MFG RPC
 * register bank and gates the free-running clock on the SOC interconnect input to the FAXI.
 */
static void
mali_hw_dcm_up(void)
{
	uint32_t val;

	/* MFG_DCM_CON_0 0x13FBF010: BG3D_DBC_CNT = 0x3f in bits 6:0, BG3D_DCM_EN in bit 15. */
	val = rd32(MALI_MFG_GATE_BASE + 0x010u);
	val |= 0x3fu;
	val &= ~(1u << 6);
	val |= (1u << 15);
	wr32(MALI_MFG_GATE_BASE + 0x010u, val);

	/* MFG_ASYNC_CON 0x13FBF020: both memory slave clock gates on. */
	val = rd32(MALI_MFG_GATE_BASE + 0x020u);
	val |= (1u << 23);
	val |= (1u << 25);
	wr32(MALI_MFG_GATE_BASE + 0x020u, val);

	/* MFG_GLOBAL_CON 0x13FBF0B0: the four GALS sleep-protect enables, and two free-runs off. */
	val = rd32(MALI_MFG_GATE_BASE + 0x0b0u);
	val &= ~(1u << 8);
	val &= ~(1u << 10);
	val |= (1u << 13);
	val |= (1u << 14);
	val |= (1u << 17);
	val |= (1u << 18);
	val &= ~(1u << 21);
	wr32(MALI_MFG_GATE_BASE + 0x0b0u, val);

	/* MFG_RPC_AO_CLK_CFG 0x13F91034 bit 0: the SOC-side FAXI free-run, off. */
	val = rd32(MALI_MFG_RPC_BASE + 0x1034u);
	val &= ~(1u << 0);
	wr32(MALI_MFG_RPC_BASE + 0x1034u, val);

	__asm__ volatile ("dsb sy" ::: "memory");
	dbg_delay_us(10);

	out("hw_dcm: dcm_con0 ");
	out_hex(rd32(MALI_MFG_GATE_BASE + 0x010u));
	out(" async ");
	out_hex(rd32(MALI_MFG_GATE_BASE + 0x020u));
	out(" global ");
	out_hex(rd32(MALI_MFG_GATE_BASE + 0x0b0u));
	out("\n");
}

/*
 * GPM 1.0, the GPU performance monitor's own AXI protector.
 *
 * Four constant words into 0x13FBFF60..0x13FBFFA8, a microsecond of settling, then the first word
 * again with its low bit raised -- the same sequence and the same numbers the vendor writes in
 * __gpufreq_gpm_control(). Nothing here is inferred; the values would be meaningless if derived.
 */
static void
mali_gpm_up(void)
{
	wr32(MALI_MFG_GATE_BASE + 0xf60u, 0x20300316u);
	wr32(MALI_MFG_GATE_BASE + 0xf64u, 0x1800000cu);
	wr32(MALI_MFG_GATE_BASE + 0xf68u, 0x01010802u);
	wr32(MALI_MFG_GATE_BASE + 0xfa8u, 0x000227f3u);
	dbg_delay_us(1);
	wr32(MALI_MFG_GATE_BASE + 0xf60u, 0x20300317u);
	__asm__ volatile ("dsb sy" ::: "memory");

	out("gpm: prot0 ");
	out_hex(rd32(MALI_MFG_GATE_BASE + 0xf60u));
	out(" prot3 ");
	out_hex(rd32(MALI_MFG_GATE_BASE + 0xfa8u));
	out("\n");
}

/*
 * The GPU, left powered and clocked for the kernel.
 *
 * The kernel cannot do this for itself. Everything below was worked out and proved from this
 * loader -- twelve domains, the crystal, then the MFGPLL -- and repeating it from XNU cost three
 * boot loops in a row with no log at all, because the SPM, topckgen and mfgcfg blocks fault when
 * they are written while their own clock is stopped, and a fault with no vector taken stops the
 * core where it stands. Here the same writes are safe: this runs with the MMU off on a flat
 * mapping, before anything has taken the exception vectors away, and it is the one context in
 * which a mistake still prints something.
 *
 * The kernel then only has to ask the block what it is and hand it its firmware. If GPU_ID does
 * not answer by the time XNU runs, the kernel says so and stops -- it does not try to fix it.
 */
static void
mali_leave_powered(void)
{
	mali_stage = BOARD_MALI_STAGE;
	if (mali_stage == MALI_STAGE_CONTROL) {
		mali_stage_stop(MALI_STAGE_CONTROL);
	}
	out("gpu: ");
	mali_power_on();
	out("gpu: left powered, clocked at 300 MHz\n");
	/*
	 * The coherence port last, after the domains and the clock it depends on. It is opened here
	 * rather than in the kernel because it lives in mfg_top_config, the same block whose outage
	 * has already cost three boot loops when written from XNU with its clock stopped.
	 */
	out("gpu: ");
	mali_acp_up();
	probe_say("gpu: coherence port opened, leaving it powered\n");
	probe_flash(37u);
}

#if BOARD_MALI_POWER
/*
 * Hand the domains back before Android is started.
 *
 * This is the difference between a run that comes back and one that loops. Every build that left
 * the GPU powered and then started Android reloaded this same image instead: Android's own gpufreq
 * driver looks at the sleep controller, finds a domain on that is not in the state it expects, and
 * the board resets before adb is anywhere to be seen. Stage eight proved the point from the other
 * side -- raise mfg1, put it straight back, and Android boots in thirty seconds.
 *
 * Only the domains this loader raised are undone, and only if it raised any: BOARD_MALI_DOMAINS
 * decides how many, and a build that never touched the GPU must not write to the controller at
 * all. It runs on the way out of a run, so the state Android inherits is the state it would have
 * had if this loader had never been here.
 */
static void
mali_aoc_down(void)
{
	uint32_t v;
	unsigned spins;

	for (spins = 0; spins < 100000u; spins++) {
		if ((rd32(MALISPM_BASE + 0x6acu) & 1u) == 1u) {
			break;
		}
		rd32(MALISPM_BASE + 0x6acu);
	}

	/* AOCLHENB -> AOCISO: latch the isolation, then drive it in. The reverse of the way up. */
	v = rd32(MALISPM_BASE + 0x0f30u);
	v |= (1u << 10);
	wr32(MALISPM_BASE + 0x0f30u, v);
	v = rd32(MALISPM_BASE + 0x0f30u);
	v |= (1u << 9);
	wr32(MALISPM_BASE + 0x0f30u, v);
	__asm__ volatile ("dsb sy" ::: "memory");

	wr32(MALISPM_BASE + 0x6acu, rd32(MALISPM_BASE + 0x6acu) | 1u);
	__asm__ volatile ("dsb sy" ::: "memory");

	out("aoc down; iso_con 0xf30 ");
	out_hex(rd32(MALISPM_BASE + 0x0f30u));
	out("\n");
}

static void
mali_power_down(void)
{
	static const uint32_t at[] = {
		0x0ebcu, 0x0ec0u, 0x0ec4u, 0x0ec8u, 0x0eccu, 0x0ed0u,
		0x0ed4u, 0x0ed8u, 0x0edcu, 0x0ee0u, 0x0ee4u, 0x0ee8u,
	};
	unsigned count = BOARD_MALI_DOMAINS;

	if (count > 12u) {
		count = 12u;
	}
	/*
	 * The sleep controller will not answer at all if its own clock is gone, and after XNU has run
	 * this is the one block whose state cannot be assumed. Reading first costs nothing and says
	 * whether the writes below have anywhere to land.
	 */
	if (spm_rd(at[0]) == 0u || spm_rd(at[0]) == 0xffffffffu) {
		out("gpu: the sleep controller does not answer; leaving the domains alone\n");
		return;
	}
	for (unsigned i = 0; i < count; i++) {
		spm_domain_off(at[i], i == 0u ? "mfg1" : i == 1u ? "mfg2" : "a domain");
	}
	/*
	 * And the isolation goes back on, in the vendor's order: their POWER_OFF ends with
	 * __gpufreq_aoc_control(POWER_OFF), after the clock, the domains and the buck. Leaving it
	 * released was a boot loop -- the run came back once, then reset before adb: domains off but
	 * SRAM still connected is a combination Android's gpufreq never creates, and the sleep
	 * controller reports it as a domain that will not settle.
	 */
	probe_say("gpu: about to put the SRAM isolation back\n");
	mali_aoc_down();
	probe_say("gpu: domains down and isolation back\n");
	probe_flash(38u);
	out("gpu: the domains are back as Android expects them\n");
}
#endif /* BOARD_MALI_POWER */

#endif /* BOARD_MALI_POWER */




void mtkloader_main(uint64_t dtb)
{
	uint32_t mark = marker_get();

	wdt_disable();
	lk_dtb = dtb;

#ifdef BOARD_CHAIN_ONLY
	/*
	 * The shortest question this loader can be asked: am I running at all, can I draw, and
	 * can I hand the phone back? display_find() is deliberately skipped -- it reads OVL0,
	 * whose registers 0x14002000 onward the Android kernel never maps, so a read there with
	 * the MMU off is a bus abort, and that is one of the things this build is here to rule
	 * out. LK's own framebuffer address is used instead: it is plain DRAM, always there.
	 */
	marker_set(MARK_DONE);   /* if this build is loaded again, go straight to Android */
	fbcon_init(LK_FB_BASE, LK_FB_PITCH, 0, 2);
#if BOARD_CHAIN_ONLY == 1
	/*
	 * The ruler settled the geometry: rows 0 to 2460 all take writes, the stride
	 * is right (the vertical bars are straight), and the bottom line at 2400
	 * lands. So nothing here is about the framebuffer any more. What the earlier
	 * builds showed is that the console stops after one line, and this build is
	 * about the console alone.
	 *
	 * Every step paints a band, in a colour of its own, and the steps run in the
	 * order of the bands' colours top to bottom -- so the highest band on screen
	 * is the last step that finished:
	 *
	 *   magenta at the very bottom, first of all: proof that the lowest rows are
	 *     writable, painted before anything else can stop
	 *   red at the top:                     fb_rect from the top works
	 *   400 characters through fbcon_putc   
	 *   green:                              those 400 finished
	 *   2000 more characters
	 *   blue:                               those finished too
	 *   one out() line through all three sinks
	 *   white:                              the sinks finished
	 *
	 * If green is there and blue is not, the console dies somewhere between 400
	 * and 2400 characters and the count is what to bisect next. If green is
	 * missing, 400 is already too many. If blue is there but white is not, the
	 * console is fine and the UART or the ramoops ring is what stops.
	 */
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
	fb_rect(0, 2300, PANEL_W, 100, 0xffff00ffu);      /* bottom, first */
	fb_rect(0, 0, PANEL_W, 100, 0xffff0000u);         /* red */

	fbc.x = 0;
	fbc.y = fbc.top = 120;
	fbc.fg = 0xff00ffffu;
	fbc.bg = 0xff000000u;
	for (int i = 0; i < 400; i++) {
		fbcon_putc((char)('A' + i % 26));
	}
	fb_rect(0, 120, PANEL_W, 100, 0xff00ff00u);       /* green */

	fbc.x = 0;
	fbc.y = fbc.top = 240;
	for (int i = 0; i < 2000; i++) {
		fbcon_putc((char)('A' + i % 26));
	}
	fb_rect(0, 240, PANEL_W, 100, 0xff0000ffu);       /* blue */

	fbc.x = 0;
	fbc.y = fbc.top = 360;
	fbc.fg = 0xff000000u;
	out("chain-only: console done\n");
	fb_rect(0, 360, PANEL_W, 100, 0xffffffffu);       /* white */

	for (;;) {
		__asm__ volatile("wfi");
	}
#elif BOARD_CHAIN_ONLY == 4
	/*
	 * Counting, not guessing. The last build put a full line of glyphs on the
	 * screen, drew the marker line under the next row, and then stopped before
	 * the first character of that row landed -- so the stop is at or just after
	 * the first line wrap, and what is needed now is the exact iteration it dies
	 * on, without having to read characters out of a photograph.
	 *
	 * So every pass of the console loop also drops a bar of its own, two pixels
	 * wide, in a column all of its own: 400 of them across the top. The bars are
	 * left of the text so nothing overlaps, and they are drawn after the glyph so
	 * a bar means that character finished. Count the bars and that is the number
	 * of characters the console survived.
	 *
	 * Under them, four bands mark the four phases, so a bar count on its own is
	 * never the whole answer:
	 *   green  at 700:  the loop over 400 characters finished
	 *   blue   at 800:  a further 400 characters, with a wrap forced every 67,
	 *                   finished -- this is the wrap path being exercised on its
	 *                   own, one wrap per row instead of only the first
	 *   white  at 900:  out() reached all three sinks
	 *
	 * If the bar count stops under 67 the failure is inside glyph drawing. If it
	 * stops at 67 or 68 the first wrap is what breaks. If the bars run all the
	 * way across and green is there but blue is not, the first wrap survived by
	 * luck and the second is what kills it.
	 */
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);

	fbc.x = 0;
	fbc.y = fbc.top = 120;
	fbc.fg = 0xff00ffffu;
	fbc.bg = 0xff000000u;
	for (int i = 0; i < 400; i++) {
		fbcon_putc((char)('A' + i % 26));
		fb_rect((uint32_t)i * 2, 20, 2, 60, 0xffffff00u);
	}
	fb_rect(0, 700, PANEL_W, 60, 0xff00ff00u);       /* green: 400 done */

	fbc.x = 0;
	fbc.y = fbc.top = 800;
	fbc.fg = 0xff00ffffu;
	fbc.bg = 0xff000000u;
	for (int i = 0; i < 400; i++) {
		if (i % 67 == 66) {
			fbcon_putc('\n');
		} else {
			fbcon_putc((char)('a' + i % 26));
		}
	}
	fb_rect(0, 800, PANEL_W, 60, 0xff0000ffu);       /* blue: forced wraps done */

	fbc.x = 0;
	fbc.y = fbc.top = 900;
	fbc.fg = 0xff000000u;
	out("chain-only: console done\n");
	fb_rect(0, 900, PANEL_W, 60, 0xffffffffu);       /* white: sinks done */

	for (;;) {
		__asm__ volatile("wfi");
	}
#elif BOARD_CHAIN_ONLY == 5
	/*
	 * Two questions, one build.
	 *
	 * The bar count in the last build stopped at a full line, 67 characters, and
	 * the next build along -- which draws no bars at all and has a completely
	 * different tail -- stopped at the same 67. Same place, different code after
	 * it, so this is not a time limit: it is the first line wrap.
	 *
	 * So the pieces of a wrap are taken apart, one band per piece, and the
	 * highest band on screen is the last piece that finished:
	 *
	 *   red     at  500: the screen was cleared, nothing else has run
	 *   green   at  540: 60 characters, all on one line, no wrap
	 *   blue    at  580: fbcon_newline() called by hand, exactly once
	 *   white   at  620: 10 more characters, on the row the newline opened
	 *
	 * If green is missing the console cannot even do 60 characters without
	 * wrapping, which the last build already showed it can, so this build would
	 * be inconsistent with the last one and worth doubting.
	 * If blue is missing, fbcon_newline() on its own is what stops -- and the red
	 * marker line it draws will be on screen anyway, which is itself the answer.
	 * If white is missing, the wrap works and drawing on the row it opened does
	 * not, which would be strange enough to look at fb_px instead.
	 *
	 * Then, about a second later, a band far down the screen:
	 *
	 *   magenta at 1500: still being painted, and the panel showed it
	 *
	 * That band is the control for the panel itself. Everything above it lands
	 * within microseconds of the loader starting; this one lands after a spin
	 * long enough to be obvious. If magenta is there, the scan-out is live and
	 * every missing band above it means the code really did stop. If magenta is
	 * not there but the others are, the panel is showing a frame captured early
	 * and every conclusion drawn from counting bands so far is worthless.
	 */
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
	fb_rect(0, 500, PANEL_W, 40, 0xffff0000u);       /* red: cleared */

	fbc.x = 0;
	fbc.y = fbc.top = 120;
	fbc.fg = 0xff00ffffu;
	fbc.bg = 0xff000000u;
	for (int i = 0; i < 60; i++) {
		fbcon_putc((char)('A' + i % 26));
	}
	fb_rect(0, 540, PANEL_W, 40, 0xff00ff00u);       /* green: 60, no wrap */

	fbcon_newline();
	fb_rect(0, 580, PANEL_W, 40, 0xff0000ffu);       /* blue: one newline */

	for (int i = 0; i < 10; i++) {
		fbcon_putc((char)('0' + i));
	}
	fb_rect(0, 620, PANEL_W, 40, 0xffffffffu);       /* white: row after wrap */

	for (volatile uint64_t spin = 0; spin < 100000000ull; spin++) {
		/* about a second; the point is the wait, not the work */
	}
	fb_rect(0, 1500, PANEL_W, 600, 0xffff00ffu);     /* magenta: one second on */

	for (;;) {
		__asm__ volatile("wfi");
	}
#elif BOARD_CHAIN_ONLY == 6
	/*
	 * A clock, not a character count.
	 *
	 * Three builds have now stopped after wildly different amounts of work: one
	 * line of glyphs, 68 characters, 70 characters. Different code each time,
	 * same wall-clock-ish place. That rules out the console and points at time:
	 * the panel stops taking what is written to it at some fixed moment after
	 * the loader starts, and everything painted earlier is what gets seen.
	 *
	 * The earlier attempt to test that used a spin loop, and the band it painted
	 * was missing -- but a spin loop is not a clock, and if the loader stopped
	 * for a different reason that band would be missing too. So measure with the
	 * only clock that survives the MMU being off: the generic timer's counter,
	 * CNTPCT_EL0, whose rate CNTFRQ_EL0 states.
	 *
	 * A row of bars along the top, one per step, each step a fixed wait longer
	 * than the last. The x position of a bar is its step number and the colour
	 * cycles, so the rightmost bar that landed says how long the panel kept
	 * accepting writes:
	 *
	 *   bar 0 is painted immediately, no wait at all: the baseline
	 *   bar 1 after 1 ms, bar 2 after 2, ... bar 39 after 39 ms
	 *
	 * Then, at the bottom, the number that matters even if the row is unreadable:
	 *
	 *   magenta bar at 1500 is painted after 100 ms, alone. If it is there, the
	 *   panel is still live a tenth of a second in and the row above will show
	 *   where it finally stops. If it is not there, 100 ms is already too late
	 *   and the row above brackets the answer from above.
	 *
	 * The counter is read once per step and the step is a busy-wait on that
	 * same counter, so the step length does not depend on how fast this CPU is
	 * at anything except reading the counter.
	 */
	uint64_t freq;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
	if (freq < 1000000u) {
		freq = 13000000u;   /* the board's stated 13 MHz, if the register is silent */
	}
	uint64_t per_ms = freq / 1000u;

	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);

	static const uint32_t cycle[6] = {
		0xffff0000u, 0xff00ff00u, 0xff0000ffu,
		0xffffff00u, 0xff00ffffu, 0xffff00ffu
	};

	uint64_t t0;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t0));
	for (int i = 0; i < 40; i++) {
		uint64_t due = t0 + (uint64_t)i * per_ms;
		uint64_t now;
		do {
			__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(now));
		} while (now < due);
		fb_rect((uint32_t)i * 27, 20, 20, 120, cycle[i % 6]);
	}

	/* The same clock, one long step, far from the row above. */
	uint64_t due = t0 + 100ull * per_ms, now;
	do {
		__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(now));
	} while (now < due);
	fb_rect(0, 1500, PANEL_W, 700, 0xffff00ffu);

	for (;;) {
		__asm__ volatile("wfi");
	}
#elif BOARD_CHAIN_ONLY == 7
	/*
	 * The clock said the panel is fine: 40 bars over 39 ms all landed, and a band
	 * painted at 100 ms landed too. So nothing here stops at 10 ms, and the
	 * earlier "the screen goes dead" reading was wrong.
	 *
	 * What is left is how long the glyph path takes. The builds that seemed to
	 * stop all drew text, and the builds that finished drew rectangles. So draw
	 * text and time it, and put the timing on the screen as a bar chart instead
	 * of guessing from how far the glyphs got.
	 *
	 * Every 50 characters the counter is read and one bar is drawn whose height
	 * is the milliseconds since the start, five pixels per millisecond, standing
	 * on a baseline at row 1000. Eight bars, one per 50 characters, so the chart
	 * reads left to right:
	 *
	 *   x = 100 + 100 * (i / 50)
	 *
	 * A bar that is flat means those 50 characters cost nothing. A tall one means
	 * they cost a lot. If the chart stops partway, the characters after the last
	 * bar are where it breaks, and the time on that bar says whether it broke
	 * because it was slow or for another reason entirely.
	 *
	 * Then a white band at row 1200, drawn only if all 400 finished, and at the
	 * very bottom a magenta band drawn from inside the same loop counter -- so
	 * even a chart that cannot be read by eye has a yes/no under it.
	 */
	uint64_t freq;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
	if (freq < 1000000u) {
		freq = 13000000u;
	}
	uint64_t per_ms = freq / 1000u;
	uint64_t t0, now;
	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(t0));

	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
	fb_rect(0, 1000, PANEL_W, 2, 0xff404040u);       /* baseline of the chart */

	fbc.x = 0;
	fbc.y = fbc.top = 40;
	fbc.fg = 0xff00ffffu;
	fbc.bg = 0xff000000u;

	static const uint32_t bars[8] = {
		0xffff4040u, 0xffff8000u, 0xffffff00u, 0xff00ff00u,
		0xff00ffffu, 0xff4080ffu, 0xff8040ffu, 0xffff40ffu
	};
	for (int i = 0; i < 400; i++) {
		fbcon_putc((char)('A' + i % 26));
		if (i % 50 == 49) {
			__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(now));
			uint64_t ms = (now - t0) / per_ms;
			uint32_t h = ms < 160u ? (uint32_t)ms * 5u : 800u;
			fb_rect(100u + (uint32_t)(i / 50) * 100u, 1000u - h, 60, h,
			    bars[i / 50]);
		}
	}

	__asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(now));
	fb_rect(0, 1200, PANEL_W, 80, 0xffffffffu);      /* white: all 400 done */
	fb_rect(0, 1600, PANEL_W, 800, 0xffff00ffu);     /* magenta: bottom */

	for (;;) {
		__asm__ volatile("wfi");
	}
#elif BOARD_CHAIN_ONLY == 2
	/* Bars, then the ramoops ring once per character, then bars again. */
	fb_rect(0, 0, PANEL_W, PANEL_H / 2, 0xffff0000u);
	plog_reset();
	for (int i = 0; i < 64; i++) {
		plog_putc('A' + (i % 26));
	}
	fb_rect(0, PANEL_H / 2, PANEL_W, PANEL_H / 2, 0xff00ff00u);
	for (;;) {
		__asm__ volatile("wfi");
	}
#elif BOARD_CHAIN_ONLY == 3
	/* Bars, then the UART once per character, then bars again. */
	fb_rect(0, 0, PANEL_W, PANEL_H / 2, 0xffff0000u);
	for (int i = 0; i < 64; i++) {
		uart_putc('A' + (i % 26));
	}
	fb_rect(0, PANEL_H / 2, PANEL_W, PANEL_H / 2, 0xff00ff00u);
	for (;;) {
		__asm__ volatile("wfi");
	}
#else
	/* Everything, character by character, then hand back to Android. */
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xffff0000u);
	fbc.x = 0;
	fbc.y = fbc.top = 0;
	fbc.fg = 0xffffffffu;
	out("mtkloader ran\n");
	out("no display_find\n");
	out("back to Android\n");
	for (int s = BOARD_CHAIN_ONLY; s > 0; s--) {
		out_dec(s);
		out("\n");
		dbg_delay_ms(1000);
	}
	chain_android();
#endif
#endif

	display_find();
	fbcon_init(fb_base, fb_pitch, 0, 1);

#if BOARD_MALI_EARLY
	/*
	 * Bring the GPU up here, at the same point in a boot where the probe build did it, instead of
	 * on the way into XNU.
	 *
	 * The probe build never failed: twelve domains, the crystal, the identifiers and the MFGPLL
	 * all came up and the board went back to Android afterwards. The identical code in
	 * run_xnu(), called a few thousand instructions later, leaves four of twelve raised, reads
	 * GPU_ID as zero and then faults at 0x400003c8 -- over and over. Something between this point
	 * and that one changes what the SPM will do, and the candidates are watcher_start() (a second
	 * core turned on, its own MMU and its own vectors), touch_bring_up() (400 kHz bit-banging on
	 * four GPIOs) and big_cores_clock() (the ARMPLLs reprogrammed). None of them is obviously
	 * related to the MFG domains, which is exactly why the position has to be the variable that
	 * gets moved rather than argued about.
	 *
	 * So this runs in the same place as the probe, and stage five already proved the return path
	 * works with the GPU untouched. If this build comes back, the power-up is fine here and the
	 * fault belongs to one of those three calls; if it does not, the position was never the
	 * difference and the earlier reading of the probe was wrong.
	 */
	mali_leave_powered();
#endif

#if BOARD_STORAGE_UFS
	/*
	 * If the last run ended in the watcher, its record is still in DRAM and this is the first
	 * chance anything has had to do something with it: the watcher cannot write flash, and the
	 * screen it was looking at was the kernel's. Show it, put it in the logbook, then carry on
	 * -- MARK_DONE below takes us to Android, where the logbook can be read out.
	 */
	if (mark == MARK_DONE) {
		report_park = 0;
		(void)snapshot_take();
		report_park = 1;
	}
#endif
#ifdef BOARD_STEPS
	/*
	 * xaga bring-up: one screen, one step per flash. On the real path the loader stopped before it
	 * printed anything, while every BOARD_CHAIN_ONLY probe ran to the end -- so the question is
	 * which call on the real path is the one that does not come back. Each number here is painted
	 * with the framebuffer only (no UART, no ramoops), so the highest number on the panel is the
	 * last step that returned, and nothing here can itself be the hang.
	 */
	{
		/*
		 * Pixels only: fb_px writes pixels and nothing else. No UART, no ramoops, no
		 * font, no console state -- whatever hangs the text path cannot hide the answer
		 * here. Each band that appears is one step that returned.
		 */
		fbcon_init(LK_FB_BASE, LK_FB_PITCH, 0, 1);
		fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);   /* the one and only clear */
		/* 0: display_find returned, framebuffer is up */
		STEP_BAND(0, 0xffff0000u);
		/* 1: the marker in DRAM was read */
		STEP_BAND(1, 0xffffff00u);
		/* 2: past the marker dispatch (neither MARK_DONE nor MARK_RUNNING) */
		STEP_BAND(2, 0xff00ff00u);
		/* 3: reaches the far end of the panel (0xfffff000 is 4 MiB past LK's buffer) */
		STEP_BAND(3, 0xff00ffffu);
	}
#endif

#if BOARD_MALI_PROBE
	/*
	 * Before the marker dispatch, not after it. A probe build runs once: the run that leaves
	 * MARK_DONE behind is what makes the next boot go straight to Android, so a probe placed
	 * after that check can only ever run on a phone whose marker was never set -- which is
	 * exactly the state a first probe destroys. It went in after the check the first time and
	 * the logbook came back holding the previous build's output, with the probe's four strings
	 * present in the binary and none of them on the flash.
	 */
	mali_dump();
	probe_say("mali probe: handing back\n");
	probe_flash(13u);
	marker_set(MARK_DONE);
	chain_android();
	for (;;) {
		__asm__ volatile ("wfe");
	}
#endif

	if (mark == MARK_DONE) {
		/* XNU's log was saved when it came back; leave ramoops as it is. */
#ifdef BOARD_STEPS
		fb_rect(300, 40, 200, 120, 0xff00ffffu);   /* 4: MARK_DONE */
#endif
#if UFS_TEST
		{
			uint32_t step = ufs_step_get();

			if (step != 0 && step != 0xffffffffu) {
				/* Last UFS stage that returned, from the run that hung. */
				fbcon_init(LK_FB_BASE, LK_FB_PITCH, 0, 3);
				fbc.fg = 0xffffff00u;
				out("last ufs step ");
				out_dec(step);
				out("\n");
				dbg_delay_ms(12000);
			}
		}
#endif
		chain_android();
	}
	if (mark == MARK_RUNNING) {
		rescue_log();
		dbg_delay_ms(5000);
		chain_android();
	}

	plog_reset();
#if defined(BOARD_STEPS)
	STEP_BAND(4, 0xff0000ffu);                 /* 4: plog_reset returned */
#endif
#if TOUCH_PROBE
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
	fbc.x = 0;
	fbc.y = fbc.top = 0;
	out("== mtkloader: touch screen probe ==\n");
	touch_probe();
	dbg_delay_ms(4000);
	chain_android();
#endif
#if GPU_PROBE
	if (pmsg_take(gpu_probe_flag, sizeof(gpu_probe_flag) - 1)) {
		fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
		fbc.x = 0;
		fbc.y = fbc.top = 0;
		out("== mtkloader: GPU probe ==\n");
		gpu_probe();
		dbg_delay_ms(4000);
	}
	chain_android();
#endif
#if MSDC_TEST
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
	fbc.x = 0;
	fbc.y = fbc.top = 0;
	out("== mtkloader: eMMC read test ==\n");
	msdc_self_test();
	dbg_delay_ms(4000);
	wdt_disable();
	chain_android();
#endif
#if UFS_TEST
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
	fbc.x = 0;
	fbc.y = fbc.top = 0;
	out("== mtkloader: UFS read test ==\n");
	/* Which build this is: a photo of the screen has to be enough to tell two of them apart. */
	out("build noclr 51\n");
	ufs_self_test();
	/*
	 * Repaint at scale 4 before parking. The log above is unreadable in a photograph, and two
	 * rounds were spent squinting at it; this is the same information reduced to twelve values
	 * that actually decide the next change. The long log is still in ramoops.
	 */
	wdt_disable();
	ufs_big_report();
	dbg_delay_ms(60000);
	/*
	 * Leave MARK_DONE behind, which is what chain_android() used to do for us but only when the
	 * test reached its end. If anything in the test hangs, the watchdog in ufs_self_test resets
	 * the board, and on the way back up this marker is what tells mtkloader_main to go straight
	 * to Android instead of running the test again: without it a hang is a boot loop in which
	 * boot_a looks exactly like a hard brick. Set it before the test too, so a hang counts as
	 * "we were here, do not try again".
	 */
	marker_set(MARK_DONE);
	chain_android();
#endif
#ifdef BOARD_STEPS
	STEP_BAND(5, 0xffff8000u);                 /* 5: everything before boot_choice returned */
#endif
#ifdef BOARD_SKIP_MENU
	/*
	 * No menu on this board: there is nothing to choose from.  The flag it waits for
	 * (DARWIN-REDWOOD:BOOT-XNU) is written by a program that runs under the other OS, and this
	 * loader was put here to boot XNU, so the menu can only ever answer "Android" -- which is
	 * what chain_android() does, and that is the one path on this board that is known not to
	 * work.  Skipping it also takes the second of delay and the text drawing out of the way of
	 * the boot, which is where the trace bands have been stopping.
	 */
	(void)boot_choice;
	const int xnu_chosen = 1;
#else
	const int xnu_chosen = boot_choice();
	if (!xnu_chosen) {
		chain_android();
	}
#endif
#ifdef BOARD_STEPS
	STEP_BAND(6, 0xffff00ffu);                 /* 6: boot_choice came back (so boot_choice's
	                                            *    own delay and menu drew and returned) */
#endif
#ifndef BOARD_STEPS
	fb_rect(0, 0, PANEL_W, PANEL_H, 0xff000000u);
#endif
	fbc.x = 0;
	fbc.y = fbc.top = 0;
	fbc.fg = 0xff40ff40u;
	out("== mtkloader: Darwin/XNU on " BOARD_TITLE " ==\n");
	fbc.fg = 0xffffffffu;
	out("LK dtb ");
	out_hex(dtb);
	out("\n");
#ifdef BOARD_STEPS
	STEP_BAND(7, 0xff40ff40u);                 /* 7: the banner is on the screen */
#endif
#ifndef BOARD_NO_CONNSYS
	connsys_dt_report(dtb);         /* Wi-Fi step 1: what Android's tree says of CONNSYS (mtkconnsys.h) */
#endif
#ifdef BOARD_STEPS
	STEP_BAND(8, 0xffffffffu);                 /* 8: connsys_dt_report returned, run_xnu next */
	/*
	 * Four answers, one flash.  Band 9 is unconditional: it proves this build is the one running,
	 * so a red 10 or 11 means the value is wrong rather than the binary being stale.  Each band
	 * reads exactly one address the code has already proved it can reach -- no scan, no
	 * dereference of an unchecked value: on this board an unmapped address is a bus abort with no
	 * handler, and the screen simply stops at the last band it drew.
	 */
	STEP_BAND(9, 0xff00ff00u);
	{
		/*
		 * Three questions, one full-width band each, green for yes and red for no.  Every
		 * earlier readout asked the eye to count segments or match a legend, and a photograph
		 * of a 1080-wide panel does neither faithfully -- twice the answer was ambiguous.  A
		 * band this size cannot be misread: only its colour matters.
		 *
		 *   top     the code is running at the address the linker chose
		 *   middle  ... at the base LK loads boot images without honouring text_offset
		 *   bottom  the Image header really is where it should be afterwards
		 */
		uint64_t here = mtk_here();
		const volatile uint64_t *base_ = (const volatile uint64_t *)here;
		uint64_t h6 = base_[6];

		fb_rect(0, 60, PANEL_W, 270,
		        here == 0x40000000ULL ? 0xff00ff00u : 0xffff0000u);
		fb_rect(0, 360, PANEL_W, 270,
		        (uint32_t)h6 == 0x0287f120u ? 0xff00ff00u : 0xffff0000u);
		fb_rect(0, 660, PANEL_W, 270,
		        ((const volatile uint64_t *)base_)[0] == 0x0000000014000010ULL
		            ? 0xff00ff00u : 0xffff0000u);
	}
#endif
#if BOARD_USB_PROBE
	usb_probe();
#endif
#if BOARD_PMIF_PROBE
	pmif_dump();
#endif
#if BOARD_DISP_PROBE
	disp_dump();
#endif
	run_xnu();
}
