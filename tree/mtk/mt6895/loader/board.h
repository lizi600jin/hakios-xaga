/*
 * mtk/mt6895/loader/board.h: what mtkloader has to be told for a phone with this SoC. The loader
 * is one program for every MediaTek phone (mtk/loader/mtkloader.c); a board is this file, and
 * build.sh hands it over with -DBOARD_HEADER="<mtk/<soc>/loader/board.h>".
 *
 * Every value here was read out of the phone it was written for, not copied from the MT6768 layer
 * next door: the live device tree LK hands to the kernel (work-xaga/dump/xaga_live.dts, 414270
 * bytes, the same tree as lk_live.dts), the kernel's own iomem (work-xaga/dump/iomem.txt), the
 * Android command line (work-xaga/dump/cmdline.txt) and the partition table (work-xaga/dump/
 * byname.txt). Where a value is a guess it says so, and says what would settle it.
 *
 * The phone these were read from: Redmi Note 11T Pro, xaga, MT6895 (Dimensity 8100), 8 GB.
 */

#ifndef MTK_MT6895_BOARD_H
#define MTK_MT6895_BOARD_H

/* ---- who this is ---------------------------------------------------------------- */

#ifndef BOARD_NAME
#define BOARD_NAME              "xaga"
#endif
#ifndef BOARD_TITLE
#define BOARD_TITLE             "XAGA (MT6895)"
#endif
/*
 * platform-name, chip-id, board-id and the model numbers are NOT set here: the kernel and the
 * user space in this tree are built for one platform (t8015 / 0x8015 / board-id 6, "redwood"), and
 * inventing another identity would only make the ramdisk refuse to load. The phone's own name
 * rides along in "darwin-board", which nothing in the kernel reads.
 */

/* ---- the processors ------------------------------------------------------------- */

/*
 * MPIDR on this SoC is Aff1<<8 | Aff0. The live tree has the A55 cluster at 0x00/0x100/0x200/0x300
 * (affinity 0x38..0x3b) and the A78s at 0x400/0x500/0x600 (cluster 1, 0x3c..0x3e) and 0x700
 * (cluster 2, 0x3f) -- three clusters, and cpu-map says so:
 *
 *   cpu@000 arm,cortex-a55 reg=0x00  cpu@100 arm,cortex-a78 reg=0x400
 *   cpu@001 arm,cortex-a55 reg=0x100 cpu@101 arm,cortex-a78 reg=0x500
 *   cpu@002 arm,cortex-a55 reg=0x200 cpu@102 arm,cortex-a78 reg=0x600
 *   cpu@003 arm,cortex-a55 reg=0x300 cpu@200 arm,cortex-a78 reg=0x700
 *
 * The loader's "core" number is the index the kernel is given, and BOARD_CPU_REG turns it into the
 * device tree's reg (core << 8), so the index of the A78 at 0x400 is 4, not 1.
 *
 * The order offered: core 0 (an A55, where LK leaves the boot), then the A78s, then the rest of the
 * A55s, so "cpus=4" already gives the kernel all three big cores. The last element is what the
 * watcher runs on; with the loader's default XNU_CORES=4 that is core 1, an A55, which is what the
 * watcher wants.
 */
#ifndef BOARD_CORE_ORDER
#define BOARD_CORE_ORDER        { 0, 4, 5, 7, 1, 2, 3, 6 }
#endif
#ifndef BOARD_SMALL_CORE
#define BOARD_SMALL_CORE(core)  ((core) < 4)    /* 0..3 are the Cortex-A55s, 4..7 the Cortex-A78s */
#endif

/* ---- the interrupt controller --------------------------------------------------- */

/*
 * arm,gic-v3, one redistributor region, and it is the MT6895's own shape:
 *   reg = <0x00 0xc000000 0x00 0x40000 0x00 0xc040000 0x00 0x200000>
 * Two frames of 0x100000 (the A55 cluster and the A78 clusters) starting at 0xc040000. The loader's
 * GICR_SGI stays at GICR_BASE + 0x10000 (it is the SGI frame of the first redistributor, and every
 * register the loader touches is an offset inside it), so only the size changes: GICR_SIZE is what
 * goes into the kernel's device tree, and the kernel reads the number of frames from it.
 */
#ifndef GICD_BASE
#define GICD_BASE               0x0c000000ULL
#endif
#ifndef GICD_SIZE
#define GICD_SIZE               0x40000ULL
#endif
#ifndef GICR_BASE
#define GICR_BASE               0x0c040000ULL
#endif
#ifndef GICR_SIZE
#define GICR_SIZE               0x200000ULL
#endif

/*
 * The window the device tree maps for arm-io: 0x0c000000..0x1fffffff holds the GIC (0x0c000000),
 * APMIXEDSYS (0x1000c000), TOPCKGEN (0x10000000), INFRACFG (0x10001000), the console UART
 * (0x11001000) and the watchdog (0x1c007000), so the MT6768's window is the right one already.
 */

/* ---- the console ---------------------------------------------------------------- */

/*
 * serial@11001000, "mediatek,mt6577-uart" (the live tree also has "mediatek,mt6779-uart-dma" at the
 * same kind of address). The MT6768's console is at 0x11002000; this one is one port lower.
 */
#ifndef UART_BASE
#define UART_BASE               0x11001000ULL
#endif

/* PLLs, same block as the MT6768: syscon@1000c000, "apmixedsys_reg_base = <0x1000c000>". */
#ifndef APMIXED_BASE
#define APMIXED_BASE            0x1000c000ULL
#endif

/* ---- DRAM: where the kernel goes, and what it is given --------------------------- */

/*
 * The kernel's physical base is a 4 GiB-aligned window above the 4 GiB line, because that is where
 * this phone has room. iomem, DRAM to 0x23fffffff:
 *
 *   0x100000000..0x1ffffffff   4 GiB, no reservations at all
 *   0x200000000..0x21bb77fff   the first thing in the way is at 0x21bb78000
 *
 * Everything below 4 GiB is a minefield: 0x40000000..0x4807ffff is the Android kernel's own
 * code and data, 0x48090000..0x4816ffff is ramoops, and 0xfe14e000..0xffffefff is the
 * framebuffer carve-out that stops the 0xfd280000 block short. The MT6768's 0xc0000000 would land
 * on 0xb7000000..0xbe3fffff (116 MiB reserved) and 0xd0000000..0xdb86ffff.
 *
 * memSize is 4 GiB, not the 4.4 GiB the window could hold: the ramdisk and the boot args are laid
 * out above the kernel image, and a round 4 GiB keeps the arithmetic (and the kernel's own
 * assumptions about a 4 KiB page table's reach) simple.
 */
#ifndef XNU_PHYS_BASE
#define XNU_PHYS_BASE           0x100000000ULL
#endif
#ifndef XNU_MEM_SIZE
#define XNU_MEM_SIZE            0x100000000ULL
#endif

/*
 * Where LK puts the kernel it starts. Not a guess: iomem of a booted phone says
 *   40000000-4807ffff : System RAM
 *     40000000-4201ffff : Kernel code
 *     42320000-4263ffff : Kernel data
 * so 0x40080000 is 512 KiB into the code segment -- LK unpacks the Android Image there and jumps
 * to it, and chain_stub copies the XNU payload to the same place and starts it. Same as the MT6768.
 */
#ifndef KERNEL_PA
#define KERNEL_PA               0x40080000ULL
#endif

/*
 * Scratch DRAM for chain_stub (a few kilobytes of position-independent code, copied here and
 * called with the MMU off). The MT6768 uses 0x4a000000, which on this phone sits 96 MiB above the
 * Android kernel -- inside the region LK may have put Android's own ramdisk in. 0x60000000 is
 * inside 0x48402000-0x6fffffff (System RAM) but above anything LK loads for a boot image, and
 * below the first reservation in that block (0x66f00000).
 */
#ifndef CHAIN_STUB_PA
#define CHAIN_STUB_PA           0x60000000ULL
#endif

/*
 * Run state, three copies 64 bytes apart, read back after the watchdog has reset the phone to
 * decide what to do next (marker_set / marker_get).
 *
 * The MT6768's 0x80000000 is reserved on this phone (0x7ffff000-0x800fffff), so it would not even
 * be written. 0x90000000 is the first address of a 248 MiB System RAM block
 * (0x90000000-0x9f7fefff) that nothing reserves, and it is far below the kernel's window
 * (0x100000000) and below the Android kernel's own code, so neither XNU nor LK writes over it
 * between the write and the reset. mtkloader runs from boot_a on the way into Android, so the read
 * happens before Android can reuse that page.
 */
#ifndef MARKER_PA
#define MARKER_PA               0x90000000ULL
#endif

/* ---- the watcher's snapshot, kept in DRAM across the reset ---------------------- */

/*
 * The second core cannot write flash: by the time it takes the snapshot XNU has run its own UFS
 * driver, the controller is in a state we did not leave it in, and a write there would either
 * fail or write something that is not the log. What does survive the reset is DRAM -- the marker
 * above relies on exactly that -- so the watcher leaves its text here, in the same block as the
 * marker, and the next run of mtkloader (which does bring UFS up) reads it out, puts it in the
 * logbook at the end of xnusys, and shows it a page at a time.
 *
 * 0x90000100 is 256 bytes past the marker and its two copies, and 0x90100000 is 1 MiB later, so
 * the record has a megabyte and cannot reach anything else that matters.
 */
#ifndef SNAP_PA
#define SNAP_PA                 0x90000100ULL
#endif
#ifndef SNAP_SIZE
#define SNAP_SIZE               0x00100000u
#endif

/* ---- the panic log, kept in DRAM across the reset -------------------------------- */

/*
 * Must be real DRAM that the kernel may memset at startup (build_device_tree does). 0x80100000 is
 * the start of the 159 MiB block 0x80100000-0x89ffffff, which nothing reserves. Same address as
 * the MT6768 uses, for the same reason, and it is outside the kernel's window above 4 GiB.
 */
#ifndef PANIC_LOG_BASE
#define PANIC_LOG_BASE          0x80100000ULL
#endif
#ifndef PANIC_LOG_SIZE
#define PANIC_LOG_SIZE          0x80000u
#endif

/*
 * The pstore zones the kernel writes and Android then shows as files in /sys/fs/pstore
 * (dmesg-ramoops-0, console-ramoops-0, pmsg-ramoops-0).
 * Straight out of iomem of a booted phone, and they agree with the Android command line
 * (ramoops.mem_address=0x48090000 ramoops.mem_size=0xe0000 ramoops.console_size=0x40000
 * ramoops.pmsg_size=0x10000):
 *
 *   48090000-4811efff  dmesg   (0x90000)
 *   4811f000-4815efff  console (0x40000)
 *   48160000-4816ffff  pmsg    (0x10000)
 *
 * The MT6768's 0x4d09f000/0x4d0e0000 are that phone's own carve-out and mean nothing here.
 */
#ifndef RAMOOPS_CONSOLE
#define RAMOOPS_CONSOLE         0x4811f000ULL
#endif
#ifndef RAMOOPS_CONSOLE_SIZE
#define RAMOOPS_CONSOLE_SIZE    0x40000u
#endif
#ifndef RAMOOPS_PMSG
#define RAMOOPS_PMSG            0x48160000ULL
#endif
#ifndef RAMOOPS_PMSG_SIZE
#define RAMOOPS_PMSG_SIZE       0x10000u
#endif

/* ---- the watchdog --------------------------------------------------------------- */

/*
 * watchdog@1c007000, "mediatek,mt6895-wdt", "mediatek,mt6589-wdt", "syscon", "simple-mfd"
 * (live tree line 3206). The second compatible string is what makes this work: Linux binds those
 * nodes to the mt6589 register layout, which is the one mtkdbg.h has (MODE +0x00, LENGTH +0x04,
 * RESTART +0x08, SWRST +0x14). What is NOT verified on this phone is the key values
 * (0x22000000 / 0x1971 / 0x1209) -- the mt6895's own sub-node for the reboot reason sits at
 * offset 0x24, which the mt6589 driver does not have, so the block has grown. If the keys are
 * wrong the writes do nothing and the loader simply boots without a watchdog; they cannot brick
 * anything, because the loader only ever disables the thing.
 *
 * The MT6768's 0x10007000 does not exist on this phone at all: grep 10007000 iomem.txt is empty,
 * and 1c007000 is the only watchdog in the map.
 */
#ifndef TOPRGU_BASE
#define TOPRGU_BASE             0x1c007000ULL
#endif

/* ---- the display ---------------------------------------------------------------- */

/*
 * The framebuffer itself belongs to LK, not to us: LK sets the panel up, allocates the buffer and
 * passes its address in the "atag,videolfb-fb_base_h/l" ATAG. mtkloader's display_find() reads the
 * address and pitch back out of the overlay it left running (OVL0 layer 0), and only falls back to
 * these numbers if that read does not look like a framebuffer. So they describe the carve-out, not
 * a buffer we choose:
 *
 *   0xfe14e000-0xffffefff : reserved   (30.7 MiB, iomem line 567)
 *
 * which is 0x1eb1000 bytes and the only framebuffer-sized reservation in the map. The earlier
 * write-up said 0xfe14f000; the map says e000.
 *
 * The panel is "panel-l16-42-02-0a-dsc-vdo" (Tianma, DSC compressed video mode) at 1080x2460. The
 * framebuffer is the logical resolution, so 1080 pitch of 4320 bytes, rounded up to 64 (the OVL's
 * alignment) -> 4352. The exact pitch LK uses is settled by what display_find() reads; the
 * fallback only has to be plausible, and PANEL_W/PANEL_H are 1080x2460 either way.
 */
#ifndef LK_FB_BASE
#define LK_FB_BASE              0xfe14e000ULL
#endif
#ifndef LK_FB_SIZE
#define LK_FB_SIZE              0x1eb1000ULL
#endif
#ifndef LK_FB_PITCH
#define LK_FB_PITCH             4352u
#endif
#ifndef LK_FB_PITCH_MAX
#define LK_FB_PITCH_MAX         8192u
#endif
#ifndef PANEL_W
#define PANEL_W                 1080u
#endif
#ifndef PANEL_H
#define PANEL_H                 2460u
#endif

/*
 * dispsys_config@14000000, disp_mutex@14001000, disp_ovl@14002000 (ovl0). The mutex addresses are
 * the MT6768's (0x14001020/0x14001024) because the mutex block sits at the same offset from its
 * own base; the overlay base is not (0x1400b000 there, 0x14002000 here).
 *
 * The layer offsets (L0_CON 0x030, L0_PITCH 0x044, L0_ADDR 0xf40, SRC_CON 0x02c) are the MT6768's
 * and are NOT verified on the MT6895, whose OVL is a different generation (it has iommus, larb and
 * interconnects). What that costs if they are wrong: display_find() reads a nonsense address,
 * fails its bounds check and uses LK_FB_BASE/LK_FB_PITCH, so the console still lands on the panel;
 * display_alpha_off() writes a few words into the display block, which is reprogrammed by the next
 * boot anyway. Nothing here can stop the kernel from starting.
 */
#ifndef OVL0_BASE
#define OVL0_BASE               0x14002000ULL
#endif
/*
 * The overlay and the DSI host the console's pixels travel through. ovl0 at 14002000 and
 * dsi@14017000 are what the live device tree names; 1400b000 is disp_ccorr there, which is why
 * OVL0_BASE above is 14002000 and not the MT6768's 1400b000.
 */
#ifndef OVL_BASE
#define OVL_BASE                0x14002000ULL
#endif
#ifndef DSI_BASE
#define DSI_BASE                0x14017000ULL
#endif
#ifndef DISP_MUTEX_EN
#define DISP_MUTEX_EN           0x14001020ULL
#endif
#ifndef DISP_MUTEX_RUN
#define DISP_MUTEX_RUN          0x14001024ULL
#endif

/* ---- what this phone does not have ---------------------------------------------- */

/*
 * The MT6895 has no eMMC. iomem has ufshci@112b0000 ("mediatek,mt8183-ufshci") and not one
 * 0x1123xxxx address, and every block device is /dev/block/sdcNN (UFS LUNs). mtk/loader/msdc.h can
 * only speak MSDC, so a read of MSDC0 on an address that does not decode is not a clean failure,
 * it is a bus access with no responder.
 *
 * What it does have is a UFS host, and LK brings it up before it loads anything: the link is
 * started, the gear and lanes are set, the device and unit descriptors are read, and the
 * controller is left running with LK's own transfer request list in UTRLBA. mtk/loader/ufs.h
 * takes that list over (UTRLRSR 0, UTRLBA to our own list, UTRLRSR 1) and issues READ(10) to
 * LUN 2, which is where the scatter puts everything but the two preloaders. So the loader can
 * read the flash after all, and the system image comes off it into DRAM.
 *
 * That is the whole extent of it: this gives XNU a system image via the RAMDisk node, which is
 * what XNU_SYSTEM_IMAGE 1 does. It does not give the kernel a block device. A /dev/emmc0 or
 * /dev/ufs0 needs the kernel's own driver to inherit the live controller -- ours
 * (drivers/storage/hakios.ufshc.c) is written for the Qualcomm case where UEFI's UFSDxe stays
 * resident and services a shared page, and nothing here provides that. So XNU_ROOT_EMMC stays 0
 * and the root is whatever ramdisk the payload or the system image gives it ("rd=md0").
 */
/*
 * XNU_SYSTEM_IMAGE 0 for now: there is no xnusys partition on this phone yet.
 *
 * The carve (mtk/tools/xaga-carve.sh) has not been written, so SYSTEM_PART is not in the GPT and
 * asking for it buys nothing -- but it does not cost nothing either, and that is why this is 0.
 * Finding it means gpt_find() -> board_read_bytes() -> board_read() -> ufs_read(): the one path in
 * this loader that talks to a controller nobody has watched it talk to yet (UFS_TEST has never run
 * on this board). A read that does not decode is not an error return on the MT6895, it is a bus
 * access with no responder. The panel bands went 0..5 in the run before this and stopped inside
 * run_xnu, and load_system_image() sits between band 11 and band 12 -- so this comes out of the
 * boot until UFS_TEST has been run on the phone and printed a GPT it read.
 *
 * Turn it back to 1 the moment the partition exists and the read is proven; nothing else changes.
 *
 * What 1 will do: look for the system image on the flash and hand it to XNU as md0.
 *
 * The stock xaga GPT has no xnusys partition -- it is what mtk/tools/gpt-carve.py adds, and the
 * carve has to be written to the phone before there is anything to read. Asking for it anyway
 * costs one partition lookup and one line of console output per boot ("no xnusys partition on
 * the ufs"), because load_system_image() returns 0 when gpt_find() fails and that is non-fatal:
 * sys_size stays 0, the memory-map carries the payload's ramdisk as RAMDisk (not RAMDisk1), and
 * the boot is exactly the RAM-disk boot it was with this 0. So 1 is safe before the carve, and
 * it is what makes the loader pick the image up the moment the partition exists -- no rebuild.
 *
 * What it buys: with sys_size != 0 the device tree's memory-map gets both nodes, RAMDisk = the
 * system image (md0) and RAMDisk1 = the payload's ramdisk (md1). That is the layout stage0
 * expects: the system image's /sbin/launchd is stage0, it mounts /dev/md1 on /redwood and runs
 * our init from there. With this 0 there is only md0 = our small root.apfs, whose /sbin/launchd
 * is our init -- the port boots and runs its checks, but it is the port's userland, not iOS.
 *
 * Sizing, because it decides the carve: userland/system.apfs is 2492203008 B (2.32 GiB) and
 * xaga's userdata is 3072 MiB, so --gib 2 is too small (gpt-carve.py's default is the X6826B's
 * 4) and --gib 2.5 leaves 0.5 GiB of userdata. See mtk/tools/xaga-cycle.sh for the sequence.
 */
#ifndef XNU_SYSTEM_IMAGE
#define XNU_SYSTEM_IMAGE        1
#endif

/*
 * This is a port of HakiOS to the xaga: the only thing the loader is here to start is XNU.
 * MENU_DEFAULT_XNU is 0 in the shared loader, which is right for the phones it was written for
 * -- there, the same image has to be able to come up on Android when nobody asks for Darwin.
 * Here it is wrong in a way that costs the boot: boot_choice() returns 0, mtkloader_main() takes
 * the `if (!boot_choice()) chain_android();` branch, and chain_android() is the one path on this
 * board that ends in a hang -- it hands 4 MB of chain_stub and an Android image to code that
 * expects a Qualcomm layout. The panel showed exactly that: bands 0..5 (5 = boot_choice's own
 * probes finished) and then nothing, because the next thing that ran was not run_xnu().
 *
 * So ask for XNU outright. The pmsg flag (DARWIN-REDWOOD:BOOT-XNU) still works and still means
 * the same thing; this only changes the answer when nobody has written one.
 */
#define MENU_DEFAULT_XNU        1
#ifndef XNU_ROOT_EMMC
#define XNU_ROOT_EMMC           0
#endif

/* ---- storage: the UFS host ------------------------------------------------------- */

/* ufshci@112b0000, "mediatek,mt8183-ufshci", reg 0x112b0000 + 0x2300 in the live tree. */
#ifndef UFS_BASE
#define UFS_BASE                0x112b0000ULL
#endif
/*
 * Every partition but preloader (LU0) and preloader_backup (LU1) is UFS_LU2 in the stock scatter:
 * pgpt, misc, boot_a, super, userdata, all of it. So the GPT and the system image are on LUN 2.
 */
#ifndef UFS_LUN
#define UFS_LUN                 2
#endif
/* The system image partition the loader reads for XNU, as on mt6768: made by tools/gpt-carve.py. */
#ifndef SYSTEM_PART
#define SYSTEM_PART             "xnusys"
#endif
/* No MSDC0 on this SoC: every read of the flash goes through mtk/loader/ufs.h instead. */
#ifndef BOARD_STORAGE_UFS
#define BOARD_STORAGE_UFS       1
#endif

/*
 * Use the transfer list LK is already running instead of moving UTRLBA to our own.
 *
 * This is not a preference, it is what this controller accepts, measured on this phone:
 *
 *   build 73/74 (this macro on):  gpt 4096  boot_a 1  sum 1 same 1  -- reads work, GPT and
 *                                 boot_a both come back, three reads checksum identical.
 *   build 66    (macro off):      base 0x0:0x60100000 t 1 stop 0x0  hcsStop 0x10f clr 0x10f
 *                                 tmrl 0x1->0x1  tail rsr 0x0 wrote 1
 *                                 -- the base write is accepted, the list stops cleanly, and
 *                                 then UTRLRSR refuses to latch. A stopped list never looks at
 *                                 the door bell, so every request times out and ufs_read()
 *                                 returns 0.
 *
 * build 75 was built without this macro purely by oversight while dropping UFS_TEST, and the
 * symptom was not "reads fail loudly": load_system_image() returned 0, sys_size became 0, the
 * memory-map lost "RAMDisk", md0 went back to the small root.apfs in the payload, and XNU came
 * all the way up and then said "/sbin/launchd.ios: No such file or directory". A silent storage
 * failure that looks like a userland failure is expensive to debug twice, so it lives here
 * rather than in a command line.
 */
#ifndef UFS_USE_LK_LIST
#define UFS_USE_LK_LIST         1
#endif

/*
 * What load_system_image() found, kept so the panel can show it before XNU's console buries it.
 *
 * Build 75's failure was silent in exactly this place: load_system_image() returned 0, nothing
 * printed, sys_size became 0, and the only visible evidence was XNU saying "/sbin/launchd.ios:
 * No such file or directory" minutes later -- which reads like a userland bug and sent the last
 * round of debugging in that direction. The three numbers below answer "did we read the flash,
 * and how much of it" in one glance: FOUND must be 1, BYTES must be 2492203008 (the 2.32 GiB
 * system.apfs), and SECONDS is how long it took (0 means it never started).
 */
#ifndef BOARD_REPORT_SYSTEM_IMAGE
#define BOARD_REPORT_SYSTEM_IMAGE 1
#endif

/*
 * Where the UFS host driver keeps its list, its 32 command descriptors and its 64 KB data buffer.
 * This cannot be .bss: the loader is loaded at 0x40080000 and Android's Image, which chain_stub
 * copies this program back over, starts at _start + CHAIN_OFFSET = 0x400c0000. .text .rodata
 * .data and .bss all have to end below that, and this area is 0x4a000 bytes -- 290 KB would push
 * .bss straight through the image being chained to and overwrite it before anything ran.
 *
 * 0x60100000 is inside 0x48402000-0x6fffffff (System RAM), above chain_stub's own page at
 * 0x60000000, and below the first reservation in that block (0x66f00000). Checked against
 * iomem.txt: 0x60100000-0x6014a000 has no reservation in it.
 */
#ifndef UFS_AREA_PA
/*
 * Next to LK's own transfer list rather than 256 MB away from it. LK keeps its list at
 * 0x51266400 and this controller accepts descriptors there; ours at 0x60100000 was written
 * (UTRLBA read back our value) but no request made through it ever completed -- the doorbell
 * stayed set until the poll limit. If the difference is which physical window the UFS master is
 * allowed to read, sharing LK's neighbourhood is the cheapest way to find out.
 */
#define UFS_AREA_PA             0x60100000ULL
#endif

/*
 * The touch screen is not an OmniVision TD4160 over SPI2 with firmware pushed into its TCM: the
 * kernel's input driver names it NVTCapacitiveTouchScreen (Novatek), and the live tree has no
 * address for it at all (only "touch { compatible = "mediatek,touch"; }"). mtkloader's bring-up
 * clears INFRACFG clock gates by the MT6768's offsets (0x8c, 0xa8, 0xc4, 0x84) and drives the
 * MT6768's TCM protocol -- on this SoC those offsets are different gates, so clearing them could
 * take a clock away from something that is running. Off, until a Novatek bring-up is written.
 */
#ifndef BOARD_TOUCH
#define BOARD_TOUCH             0
#endif

/*
 * When XNU comes back (loader_return_after) or the watchdog gives up, reset_board() is what the
 * shared code does next -- and on this board nothing resets it. PSCI_SYSTEM_RESET (smc 0x84000009)
 * returns without acting, and the TOPRGU restart request is ignored, both checked on the device, so
 * a run that ends unattended just sits on the last screen forever. Starting Android instead puts
 * adb back, and the log the loader wrote into the tail of xnusys is then a single pull away
 * (mtk/tools/xaga-pull-log.sh). The Android kernel is in the image at CHAIN_OFFSET because
 * build_flashed.py embeds it for chain_android(); without that, the loader says so and resets.
 */
#ifndef BOARD_RETURN_TO_ANDROID
#define BOARD_RETURN_TO_ANDROID 1
#endif

/*
 * A bare-metal USB pull-up probe, used only while the kernel side of USB is being brought up.
 * The loader takes the port from LK and drives the same registers the kernel driver will, then
 * sits polling DEVICE_CONF and COMMON_USB_INTR instead of jumping to XNU. What it answers is
 * whether the port can be attached from firmware at all, with no kernel, no caches and no
 * ordering of its own in the way: if the host enumerates after this, the electrical path is
 * fine and the kernel is the problem; if it stays silent here too, the port needs something
 * that only LK's own start-up does. Off in normal builds.
 */
#ifndef BOARD_USB_PROBE
#define BOARD_USB_PROBE         0
#endif

/*
 * Dump the overlay and DSI registers into the logbook and stop before the kernel.
 *
 * The console draws into the framebuffer LK hands over, the kernel maps that framebuffer with an
 * identity transform (fbwidth/fbheight are the panel's own 1080x2460, and the kernel's own log
 * line says "display 1080x2460 on a panel of 1080x2460"), and the picture still sits to the right
 * of the glass. Anything that could do that is in the block that scans the framebuffer out: which
 * address it reads, at what stride, with what source width and at what offset into the line.
 *
 * The Android side of this question is already answered -- the working display block was dumped
 * through debugfs and is in work-xaga/disp/ -- but those files turned out to hold the DSI and
 * dispsys halves, not the overlay, so the overlay side has only ever been guessed at. This reads
 * it from the one context that has no kernel in the way.
 */
#ifndef BOARD_DISP_PROBE
#define BOARD_DISP_PROBE        0
#endif

/*
 * Paging the kernel log across the screen is for a person watching the phone. Here the phone is
 * expected to finish on its own and start Android, and the log is already in the flash by then,
 * so paging only delays that: this board's 400 KB log at scale 3 is 172 pages held 20 seconds
 * each, which is fifty-seven minutes before chain_android() is reached. Off. show_log_tail()
 * still draws the last lines for a photograph.
 */
#ifndef BOARD_REPORT_PAGES
#define BOARD_REPORT_PAGES      0
#endif

/*
 * big_cores_clock() only acts when APMIXED + 0x20c reads back with post-divider 1 and a multiplier
 * below its target, and then writes a multiplier computed for the MT6768's A75 at 1176 MHz. The
 * MT6895's A78 clusters hang off different PLLs; on a value that happens to look alike it would
 * program a clock nobody asked for. Off.
 */
#ifndef XNU_BIG_CLOCK
#define XNU_BIG_CLOCK           0
#endif

/* ---- getting the phone back ------------------------------------------------------ */

/*
 * This phone has no recovery partition and no `fastboot boot` (lk_a registers no boot: command,
 * checked against both the phone's own lk_a.img and the lk.img of the stock ROM), so the only way
 * into Android after a bad boot image is `fastboot flash boot_a` -- and that needs a working
 * fastboot, which needs a person at the keyboard. Nothing in the loader can be recovered by
 * itself. So every way out of a failed run is wired up, and none of them needs a human:
 *
 *  1. loader_return_after=45  -- XNU hands control back 45 s after early boot, whatever it is
 *     doing (osfmk/arm64/loader_return.c, STARTUP EARLY_BOOT). xnu_returned() then takes its log
 *     off the screen, writes it to ramoops and calls reset_board() -> back to Android, with the
 *     log waiting in /sys/fs/pstore/console-ramoops-0. 0 would disable it, and leave a kernel
 *     that boots but never gets to user space sitting there for ever.
 *  2. XNU_WATCH_SECONDS 120 -- a second core spins here from the moment XNU starts. If XNU dies
 *     before it reaches EARLY_BOOT, or takes an exception nothing catches, no timer of its own is
 *     ever going to fire; this one resets the board anyway after two minutes.
 *  3. XNU_WDT_SECONDS 0 -- the hardware watchdog stays off on purpose. The MT6895's watchdog is at
 *     0x1c007000 and has the mt6589 register layout only by compatible string; TOPRGU_MODE_KEY et
 *     al. are the MT6768's values and are not verified here. A watchdog programmed wrong is worse
 *     than none: it can reset the phone in the middle of Android. (1) and (2) do the job.
 *
 * The watcher runs on core_order[XNU_CORES]. With XNU_CORES 4 and the order below, XNU gets cores
 * 0, 4, 5 and 7 (all four A78s, the fast ones) and the watcher gets core 1, an A55 -- no core is
 * asked to do both, which the _Static_assert in mtkloader.c enforces for the whole table.
 */
#ifndef XNU_RETURN_AFTER
#define XNU_RETURN_AFTER        "70"
#endif
#ifndef XNU_WATCH_SECONDS
#define XNU_WATCH_SECONDS       120
#endif
#ifndef XNU_WDT_SECONDS
#define XNU_WDT_SECONDS         0
#endif

/*
 * cpus=4: the number of cores XNU is told to use, which has to agree with XNU_CORES above and
 * with the cpus node the loader writes into the device tree. The loader's own default is
 * XNU_EXTRA_ARGS "cpus=1" (the MT6768 bring-up's first step, one core and nothing that can race),
 * and devices/xaga/configs says CONFIG_CPUS=4 -- but nothing in the build reads .config
 * into the loader, so a board that wants more than one core says so here. One core on an eight-core
 * phone is not a bring-up any more, it is a phone at an eighth of its speed.
 *
 * Four of the eight, not eight: the watcher takes its own core out of core_order (see above), and
 * the MT6895's three clusters (four A55, then three A78, then one A78 on its own) make a partial
 * set the safer choice until the scheduler has been watched on this SoC. 0, 4, 5 and 7 are the
 * A78s -- the fast ones -- so the four that do run are the ones worth running.
 */
/*
 * touchscreen=3 buttons=2: which driver brings up which part of the input -- the Novatek NT36672E
 * and the keypad plus the MT6363 (mtk/mt6895/drivers/input/). The numbers are this board's own;
 * redwood's touch screen is touchscreen=1 and the MT6768's keys are buttons=1, so a run that says
 * nothing gets no input at all rather than the wrong driver reading the wrong registers.
 *
 * keylog=2: the keypad's register layout for the MT6895 is not in any source here -- the vendor
 * tree this repository has does not carry drivers/input/keyboard/, and the PMIC is reached through
 * an arbiter whose offsets are not published. So hakios.mt6895_keys.c prints the keypad's block
 * every second while nothing changes; one boot with a key pressed on each register pins the offsets
 * down, and this word goes away.
 *
 * fbwidth=1080 fbheight=2460: the display this kernel draws into, told as the panel's own size
 * rather than left at the iPhone's 1125 x 2436.
 *
 * Without these two, MiniIOMobileFramebuffer takes its defaults (kFBDefaultWidth 1125,
 * kFBDefaultHeight 2436) and the window server lays out a 1125-wide picture that has to be scaled
 * into a 1080-wide panel. The scaling itself is even -- fFillWidth stays the panel's width, so the
 * whole 1125 maps across the whole 1080 -- but every round trip through the scale lands on a
 * different pixel of the panel, and text and hairlines come out heavier on one side than the other.
 * With the panel's real numbers the mapping is the identity: fWidth == fPanelWidth, fHeight ==
 * fPanelHeight, fFillWidth == fPanelWidth, fCropX == 0, so source pixel x is panel pixel x.
 *
 * The kernel log says which of the two it got:
 *   MiniIOMobileFramebuffer: display 1125x2436 on a panel of 1080x2460 (4352 bytes per row)
 * is the mismatch, and 1080x2460 on a panel of 1080x2460 is these two words taking effect.
 *
 * The kernel's command line has room for BOOT_LINE_LENGTH (608) characters, and the words the
 * loader adds itself (rd=md0 cpus=4 loader_return_after=900) take about 40.
 */
#ifndef XNU_EXTRA_ARGS
#define XNU_EXTRA_ARGS          "cpus=4 buttons=2 touchscreen=3 touch_raw=1 fbwidth=1080 fbheight=2460 fb_disp=1 usb=2"
#endif

/*
 * Nothing on this board is reachable through a peripheral window while the MMU is off.
 *
 * The MT6768 bring-up was written for a phone whose UART is wired to a header and whose OVL0 the
 * loader could read; xaga has neither. Two reads in particular are fatal, and both were found the
 * hard way on the real phone:
 *
 *  - UART_BASE + 0x14 (the transmit-ready bit). Nothing is soldered to the AP UART on a retail
 *    xaga, and LK clocks the block down. The loader reached the screen with one character and
 *    stopped: out() puts the character on the wire first, so the phone never printed its second.
 *  - OVL0_BASE + OVL_L0_ADDR, first thing in display_find(). The live device tree has
 *    disp_ccorr@1400b000 where OVL0_BASE points and ovl0 at 14002000, so the address is wrong
 *    before the read is even the problem -- and the read is the problem: an abort LK does not
 *    handle never comes back.
 *
 * Both are unnecessary here. The screen and the ramoops ring are the loader's two consoles (XNU's
 * own log is read out of pstore, never off a wire), and LK's framebuffer address is plain DRAM
 * whose geometry the ruler build already verified row by row.
 */
#define BOARD_NO_UART           1
#define BOARD_NO_OVL            1

/*
 * The third sink, and the one the step probes actually stopped on. With the OVL read and the UART
 * gone the loader got further -- it drew "step 1" and then not the next character -- and a build
 * that skipped only the text path ran to the end. out() reaches the screen through dbg_putc(),
 * which is plog_putc() and then fbcon_putc(); the screen half is proven (a probe drew eight
 * glyphs in a row at two scales), so the ring is the half left. It is DRAM the Android ramoops
 * driver has claimed, and on this board it is not ours to write: the first character that lands
 * in it is the last thing the loader does.
 */
#define BOARD_NO_PLOG           1

#endif /* MTK_MT6895_BOARD_H */
