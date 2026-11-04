/*
 * UFS reads for the mtkloader, the way msdc.h does them for eMMC.
 *
 * xaga has no eMMC at all: iomem has no 1123xxxx (MSDC0) window, only 112b0000-112b22ff, the
 * UFS host controller. So the loader cannot read the system image off the phone with msdc_read,
 * and everything msdc.h does is dead weight here.
 *
 * What we inherit: LK has already brought the link up (DME link startup, UniPro, the gear and
 * lane settings), has already read the device descriptor and the unit descriptors, and has left
 * the controller running with its own transfer request list in UTRLBA. That is the same
 * situation msdc.h is in with the eMMC bus: the hard, timing-sensitive part is done, and what is
 * left is to issue commands and poll.
 *
 * What we therefore do *not* do: reset the controller, touch the PHY, run UIC commands, change
 * the power mode, or reprogram the gear. Any of those would drop the link and there is no driver
 * here to bring it back up.
 *
 * What we do: keep LK's list address, stop the list (UTRLRSR = 0), point UTRLBA/UTRLBAU at our
 * own list in our own memory, start it again (UTRLRSR = 1), and from then on issue one request at
 * a time and poll the doorbell until the controller clears it. The descriptor and command
 * descriptor layout is the one JESD223 defines and the one drivers/storage/hakios.ufshc.c already
 * implements for the kernel: UTRD is 8 dwords, the UCD holds the UPIU, the response and the PRD
 * table, and the whole thing is 32-bit register pokes.
 *
 * The MMU is off in the loader (start.S: "MMU and caches off"), so every access here is device
 * memory: naturally aligned 32-bit accesses only, no SIMD, no memset over the descriptor area
 * with anything wider than a word. DMA coherency needs no cache maintenance for the same reason.
 */
#ifndef MTK_UFS_H
#define MTK_UFS_H

#include <stdint.h>

/* ufshci@112b0000, "mediatek,mt8183-ufshci", in xaga's device tree. board.h overrides this. */
#ifndef UFS_BASE
#define UFS_BASE             0x112b0000ULL
#endif

/* The logical unit the GPT and every system partition live on (scatter: everything but the two
 * preloaders is UFS_LU2). board.h overrides this too. */
#ifndef UFS_LUN
#define UFS_LUN              2
#endif
/*
 * The LUN that answers. LK leaves UFS_LUN 2 selected and that is what the device tree says, but a
 * unit that does not exist answers TEST UNIT READY with status 0x7 (INVALID LUN) rather than not
 * answering at all, so the number has to be found rather than assumed. ufs_pick_lun() walks 0..7
 * and keeps the first one that answers 0.
 */
#ifndef UFS_LUN_MAX
#define UFS_LUN_MAX          8
#endif
/* How many times one unit may be asked before its answer is believed. */
#ifndef UFS_LUN_TRIES
#define UFS_LUN_TRIES        4
#endif

/* fDeviceInit polling: Linux gives it FDEVICEINIT_COMPL_TIMEOUT (1 s) in 500 us steps. */
#ifndef UFS_FDEVINIT_TRIES
#define UFS_FDEVINIT_TRIES   64
#endif

/*
 * A hang inside this file used to be invisible: the screen showed the last line printed before
 * the call and nothing told the difference between "stuck in the doorbell loop" and "stuck
 * cleaning a cache line". ufs_trace is assigned by the caller once its own output routine
 * exists (this header is included before it does), and every step below reports through it.
 */
static void (*ufs_trace)(const char *) = 0;
static void (*ufs_trace_hex_fn)(uint64_t) = 0;

#define UFS_TRACE(s)    do { if (ufs_trace) { ufs_trace(s); } } while (0)
#define UFS_TRACE_HEX(v) do { if (ufs_trace_hex_fn) { ufs_trace_hex_fn(v); } } while (0)

/* JESD223 host controller registers, offsets from UFS_BASE. */
#define UFS_CAP              0x00
#define UFS_VER              0x08
#define UFS_HCDDID           0x10
#define UFS_HCPMID           0x14
#define UFS_AHIT             0x18
#define UFS_IS               0x20
#define UFS_IE               0x24
#define UFS_HCS              0x30
#define UFS_HCE              0x34
#define UFS_UTRIACR          0x4c
/*
 * UniPro/M-PHY error codes, one per layer (ufshci.h): a command that comes back
 * OCS_FATAL_ERROR without a response UPIU says the controller gave up, and these say at which
 * layer. Zero everywhere means the controller never saw a link error and the fault is above it.
 */
#define UFS_UIC_ERR_PHY      0x38
#define UFS_UIC_ERR_DL       0x3c
#define UFS_UIC_ERR_NET      0x40
#define UFS_UIC_ERR_TRANSPORT 0x44
#define UFS_UIC_ERR_DME      0x48
#define UFS_UTRLBA           0x50
#define UFS_UTRLBAU          0x54
#define UFS_UTRLDBR          0x58
#define UFS_UTRLCLR          0x5c
#define UFS_UTRLRSR          0x60
#define UFS_UTLRCNR          0x64
/*
 * The task management request list. Linux programs both lists in
 * ufshcd_make_hba_operational(): transfer base low/high and task base low/high, because HCS bit 2
 * (UTP_TASK_REQ_LIST_READY) is asserted by hardware and the controller expects a base behind it.
 * UTLRCNR reading 0x40000000 -- bit 30, which no public definition covers -- is the only sign
 * this side was left unset. Derived from the JESD223 layout, which puts the task list registers
 * after their transfer counterparts:
 *
 *   0x50/0x54 UTRLBA/UTRLBAU   0x68/0x6c UTMRLBA/UTMRLBAU
 *   0x58      UTRLDBR          0x70      UTMRLDBR
 *   0x5c      UTRLCLR          0x74      UTMRLCLR
 *   0x60      UTRLRSR          0x78      UTMRLRSR
 */
#define UFS_UTMRLBA          0x70
#define UFS_UTMRLBAU         0x74
#define UFS_UTMRLDBR         0x78
#define UFS_UTMRLCLR         0x7c
#define UFS_UTMRLRSR         0x80
/*
 * MediaTek's own registers, outside the UFSHCI block (ufs-mediatek.h):
 *   0x140 REG_UFS_XOUFS_CTRL   bit 7 = CLK_CG
 *   0x144 REG_UFS_REFCLK_CTRL  bit 0 = REFCLK_REQUEST, bit 1 = REFCLK_ACK
 *   0x160 REG_UFS_MMIO_OPT_CTRL_0
 * The vendor driver sets CLK_CG before every attempt at enabling the controller, with the
 * comment "to bypass abnormal ERR_CHK signal to prevent host hang issue". Without it the HCE
 * write is simply lost -- which is exactly what this board did: wrote 1, read back 0, forever.
 */
#define UFS_MTK_XOUFS_CTRL   0x140
#define UFS_MTK_XOUFS_CLKCG  (1u << 7)
#define UFS_MTK_REFCLK_CTRL  0x144
#define UFS_MTK_REFCLK_REQ   (1u << 0)
#define UFS_MTK_REFCLK_ACK   (1u << 1)
#define UFS_MTK_REFCLK_TRIES 20000u

#define UFS_UIC_COMMAND      0x90
#define UFS_UIC_ARG1         0x94
#define UFS_UIC_ARG2         0x98
#define UFS_UIC_ARG3         0x9c
/* MASK_UIC_COMMAND_RESULT, include/ufs/ufshci.h: the UIC outcome lives in the low byte of ARG2 */
#define UFS_UIC_RESULT_MASK  0xffu

#define UFS_CAP_NUTRS(c)     (((c) & 0x1fu) + 1u)
#define UFS_CAP_64AS         (1u << 24)
#define UFS_HCS_DP           (1u << 0)          /* the device is present */
#define UFS_HCS_UTRLRDY      (1u << 1)          /* the transfer list is ready */
#define UFS_HCS_UICRDY       (1u << 3)          /* the UIC command register takes a command */

#define UFS_IS_UIC_HIBER_EXIT (1u << 5)
#define UFS_IS_UIC_CMD_COMPL  (1u << 10)
#define UFS_CMD_DME_RESET 0x14u
#define UFS_CMD_DME_ENABLE 0x12u
#define UFS_CMD_DME_SET 0x02u
#define UFS_CMD_DME_GET 0x01u
#define UFS_CMD_DME_LINK_STARTUP 0x16u
#define UFS_IS_UIC_LINK_STARTUP (1u << 8)

/*
 * The three UniPro attributes MTK's ufs_mtk_pre_link() writes before every link startup.
 *
 * This is not optional vendor polish: the first readout of a link that was started without them
 * came back with UIC_LINK_LOST on the very next register read, and the device then latched
 * DEVICE_FATAL_ERROR -- a device that had its link started while the host still had LPM and TX
 * lane clock gating enabled is entitled to do that. The values and their meaning:
 *
 *   VS_UNIPROPOWERDOWNCONTROL 0xD0A8 sel 0 = 0   leave LPM before the link comes up
 *   PA_LOCAL_TX_LCC_ENABLE    0x155E sel 0 = 0   host TX lane clock gating off, both ends
 *   VS_SAVEPOWERCONTROL       0xD0A6       &= ~(1 << 6)   "disable deep stall"
 *
 * (drivers/ufs/host/ufs-mediatek.c ufs_mtk_pre_link, and unipro.h / ufs-mediatek.h for the ids.)
 */
#define UFS_MIB_VS_UNIPROPOWERDOWNCONTROL 0xD0A8u
#define UFS_MIB_PA_LOCAL_TX_LCC_ENABLE    0x155Eu
#define UFS_MIB_VS_SAVEPOWERCONTROL       0xD0A6u
/*
 * Never write all ones to IS.
 *
 * IS is write-one-to-clear, and bits 16, 17 and 18 are CONTROLLER_FATAL_ERROR,
 * SYSTEM_BUS_FATAL_ERROR and CRYPTO_ENGINE_FATAL_ERROR. They are conditions the controller latches
 * and cannot recover from by being told to clear them; writing them sends the whole register file
 * into that error state, and every read after it comes back zero -- HCS 0x101f becomes HCS 0x0 on
 * the very next line, exactly as the syscon reset did. That is what "IS = 0xffffffff" was doing
 * here for many builds. Clear only the summary bits below, all of which mean "look here", not
 * "you are broken".
 *
 *   0x00001 UTP_TRANSFER_REQ_COMPL      0x00080 UIC_LINK_LOST
 *   0x00002 UIC_DME_END_PT_RESET        0x00100 UIC_LINK_STARTUP
 *   0x00004 UIC_ERROR                   0x00200 UTP_TASK_REQ_COMPL
 *   0x00008 UIC_TEST_MODE               0x00400 UIC_COMMAND_COMPL
 *   0x00010 UIC_POWER_MODE              0x00800 DEVICE_FATAL_ERROR
 *   0x00020 UIC_HIBERNATE_EXIT          0x01000 UTP_ERROR
 *   0x00040 UIC_HIBERNATE_ENTER
 */
#define UFS_IS_CLEARABLE      0x000007ffu        /* NOT 0x800: that is DEVICE_FATAL_ERROR */
#define UFS_CMD_DME_HIBER_EXIT 0x18u
#define UFS_CMD_DME_PEER_GET   0x03u
#define UFS_CMD_DME_PEER_SET   0x04u

/*
 * UniPro PA attributes and power-mode values, from include/ufs/unipro.h.
 *
 * These were never used in this file: the link sat at whatever LK left it, PWM-G1, and the
 * whole 2376 MB image took 2615 s to read (~0.9 MB/s). The same image over a negotiated
 * HS-G4 link is a few seconds. Nothing here changes the controller's list handling; it only
 * asks the peer to move to a faster gear after the link is up.
 */
#define UFS_PA_ACTIVETXDATALANES    0x1560u
#define UFS_PA_CONNECTEDTXDATALANES 0x1561u
#define UFS_PA_TXGEAR               0x1568u
#define UFS_PA_TXTERMINATION        0x1569u
#define UFS_PA_HSSERIES             0x156Au
#define UFS_PA_PWRMODE              0x1571u
#define UFS_PA_ACTIVERXDATALANES    0x1580u
#define UFS_PA_CONNECTEDRXDATALANES 0x1581u
#define UFS_PA_RXGEAR               0x1583u
#define UFS_PA_RXTERMINATION        0x1584u
#define UFS_PA_MAXRXPWMGEAR         0x1586u
#define UFS_PA_MAXRXHSGEAR          0x1587u
#define UFS_PA_PWRMODEUSERDATA0     0x15B0u

#define UFS_FAST_MODE     1u
#define UFS_SLOW_MODE     2u
#define UFS_FASTAUTO_MODE 4u
#define UFS_SLOWAUTO_MODE 5u
#define UFS_HS_MODE_A     1u
#define UFS_HS_MODE_B     2u

/* UPMCRS lives in HCS bits 10:8 and is the only success signal for DME_SET(PA_PWRMODE). */
#define UFS_UPMCRS_SHIFT     8u
#define UFS_UPMCRS_MASK      0x7u
#define UFS_PWR_OK           0u
#define UFS_PWR_LOCAL        1u
#define UFS_PWR_REMOTE       2u
#define UFS_PWR_BUSY         3u
#define UFS_PWR_ERROR_CAP    4u
#define UFS_PWR_FATAL_ERROR  5u

/* UniPro default link timeouts, set before the gear change like Linux does. */
#define UFS_DL_FC0_TIMEOUT   0xD041u
#define UFS_DL_TC0_TIMEOUT   0xD042u
#define UFS_DL_AFC0_TIMEOUT  0xD043u

/*
 * DW0 of a request descriptor, from the interface header the Linux driver compiles against
 * (/usr/src/linux-headers-<version>/include/ufs/ufshci.h). Its first dword is a bitfield struct, and on
 * little endian the fields land in the *top* byte, not the bottom one:
 *
 *   struct request_desc_header {
 *       u8 cci; u8 ehs_length; u8 reserved2:7; u8 enable_crypto:1;      // bytes 0..2
 *       u8 interrupt:1; u8 data_direction:2; u8 reserved1:1; u8 command_type:4;   // byte 3
 *   };
 *
 * so command_type is DW0[31:28], data_direction DW0[26:25], interrupt DW0[24]. An intermediate
 * pass in this file moved them to bits 8:11 / 5:4 after misreading a descriptor word, and every
 * transfer after that was refused for a reason unrelated to the real fault.
 */
/*
 * These four bits are where measurement and the Linux header disagree, and measurement wins.
 *
 * The Linux interface header puts command_type in DW0[31:28] and data_direction in DW0[26:25].
 * Written that way, not one request on this controller ever completed. Written the other way --
 * command_type DW0[8:11], data_direction DW0[4:5] -- a TEST UNIT READY was answered on all eight
 * units ("LUN scan: 0x0 0x0 0x0 0x0 0x0 0x0 0x0 0x0", "TEST UNIT READY on LUN 0: answered"), on
 * this board, through a list this loader built itself. The earlier "correction" to the header's
 * layout is what turned working transfers into refusals.
 *
 * LK's own leftover descriptor at 0x51266400 reads 0x0d1600e0 as its first word, which is not a
 * UTRD at all (it is a PRD table), so it cannot arbitrate; the answered TURs can.
 */
/*
 * DW0 is `struct request_desc_header`, and the interface header spells it out byte by byte:
 *
 *     u8 cci;
 *     u8 ehs_length;
 *     u8 reserved2:7; u8 enable_crypto:1;
 *     u8 interrupt:1; u8 data_direction:2; u8 reserved1:1; u8 command_type:4;   <- little endian
 *     __le32 dunl;
 *
 * That fourth byte is the most significant byte of the little-endian dword, so
 * command_type is bits 31:28, data_direction bits 26:25 and interrupt bit 24. The kernel driver
 * and Linux both write UTRD_CT_UFS as (1u << 28) and data direction as (2u << 25); an earlier
 * build here "corrected" them to bits 8:11 after a variant scan showed the (1u << 28) variants
 * never ringing the door bell -- but those two variants ran after two others had already left
 * HOST_ERROR_INDICATOR set, so the silence was a contaminated controller, not evidence about
 * the field position. Put them back where the header says they are.
 */
#define UTRD_CT_MASK         0xf0000000u
#define UTRD_CT_UFS          (1u << 28)          /* UTP_CMD_TYPE_UFS_STORAGE */
#define UTRD_DD_MASK         (3u << 25)
#define UTRD_DD_NONE         (0u << 25)          /* UTP_NO_DATA_TRANSFER */
#define UTRD_DD_OUT          (1u << 25)          /* UTP_HOST_TO_DEVICE */
#define UTRD_DD_IN           (2u << 25)          /* UTP_DEVICE_TO_HOST */
#define UTRD_INT             (1u << 24)

/* UTRD word 2: the overall command status the controller writes back. */
#define UTRD_OCS_MASK        0xffu
#define OCS_SUCCESS          0x0u
#define OCS_INVALID          0xfu

#define UPIU_COMMAND         0x01
#define UPIU_TRANSACTION_NOP_OUT 0x00
#define UPIU_TRANSACTION_QUERY_REQ 0x16
/* Query opcodes and the one flag that matters: see UFS spec B-0/B-1. */
#define UPIU_QUERY_OPCODE_READ_FLAG 0x05
#define UPIU_QUERY_OPCODE_SET_FLAG  0x06
#define QUERY_FLAG_IDN_FDEVICEINIT  0x01
#define UPIU_QUERY_FUNC_READ        0x01
#define UPIU_QUERY_FUNC_WRITE       0x81
#define UPIU_RESPONSE        0x21
#define UPIU_FLAG_READ       0x40
#define UPIU_FLAG_WRITE      0x20      /* the same byte, the other direction (JESD223 UPIU flags) */

/*
 * Where the three parts of a UCD sit, taken from the layout every other UFS host driver uses:
 * each UPIU gets an aligned slot of its own rather than 32 bytes and then padding to 512. The
 * 512/1024 this file used came from reading the offsets as if they were arbitrary; they are not,
 * the controller has them built in, which is why a command written at 0 and a response expected
 * at 512 both came back as zeros while the doorbell was still consumed.
 */
/*
 * Offsets inside the command descriptor, from the interface header the Linux driver compiles
 * against (/usr/src/linux-headers-<version>/include/ufs/ufshci.h):
 *
 *   struct utp_transfer_cmd_desc { u8 command_upiu[512]; u8 response_upiu[512]; u8 prd_table[]; };
 *   ALIGNED_UPIU_SIZE = 512
 *
 * The 0/512/1024 split this file started with was right. An intermediate pass narrowed it to
 * 0/128/256 while guessing, and every transfer after that was rejected for a reason that had
 * nothing to do with the guess.
 */
#define UCD_UPIU             0               /* the command UPIU we write */
#define UCD_UPIU_SLOT        512             /* command UPIU slot, padded to this */
#define UCD_RESPONSE         512             /* where the response UPIU comes back */
#define UCD_PRDT             1024            /* the physical region descriptor table */
/*
 * The response slot is the whole aligned UPIU buffer, and the descriptor counts it in dwords:
 * ALIGNED_UPIU_SIZE >> 2. The driver writes exactly that (ufshcd.c, ufshcd_uic_hibern8_exit /
 * the utrd initialiser: response_upiu_length = ALIGNED_UPIU_SIZE >> 2). 32 was the size of the
 * UPIU we *send*, not of the slot the device answers into.
 */
/*
 * The length is in dwords and the original 32 was right: it is the response UPIU's own size
 * (32 bytes), which the controller or the device checks the returned response against. Set to
 * 128 while correcting the UTRD against the Linux header, every TUR that used to be answered was
 * rejected instead. 32 is what the TURs were answered with.
 */
#define UCD_RESP_LEN         (UCD_RESPONSE / 4)
/*
 * One command descriptor: the command UPIU, the response area, the PRD table. JESD223 wants each
 * UCD 128-byte aligned and sized, so this is rounded up to a multiple of 128 (1152, not 1056).
 */
#define UCD_SIZE_RAW         (UCD_PRDT + 16 * (UFS_SLOT_BUFFER / UFS_PRD_MAX + 1))
#define UCD_SIZE             ((UCD_SIZE_RAW + 127u) & ~127u)

#define UFS_PRD_MAX          (256u << 10)    /* one PRD covers 256 KB */
/*
 * One request may move eight megabytes. JESD223 caps a single PRD entry at 256 KiB, so this costs
 * 32 PRD entries out of the 64 the 1 KiB PRD table holds -- measured, not assumed: UCD_PRDT is
 * 1024 bytes and each entry is 16, so 32 entries end at byte 512 of a 1024-byte table.
 *
 * The reason to raise this is the fixed cost per request. With 1 MiB per request the 2.3 GiB
 * image is 2376 round trips; on the phone each one is a descriptor rebuild, a door bell and a
 * completion poll, and build 80 sat at "5%" for minutes on that overhead alone. 8 MiB makes it
 * 297 round trips.
 *
 * 8 MiB is also 2048 logical blocks, still well inside READ(10)'s 16-bit block count.
 */
#define UFS_SLOT_BUFFER      (8u << 20)     /* the data buffer one request may use */
#define UFS_BLOCK            4096u           /* the logical block size of the unit we read */
#define UFS_SLOTS            32
#define UFS_TIMEOUT_MS       10000u
#define UFS_SYSCON_PDN       0x112bc000ULL      /* "mediatek,mt6895-ufs_pdn_cfg" */
#define UFS_SYSCON_AO        0x112b8000ULL      /* "mediatek,mt6895-ufs_ao_config" */
#define UFS_RESET_DELAY      20000u             /* busy-wait long enough to cover 100us */
/*
 * ti,syscon-reset hands out two pulse registers per line: a write of the line's bit to the
 * assert offset takes it into reset, a write of the same bit to the deassert offset releases it.
 * Neither register holds a value, so reading either one back always gives 0 -- the readings were
 * never evidence that a write had failed.
 */
#define UFS_RESET_ASSERT     0x44u
#define UFS_RESET_DEASSERT   0x48u
/*
 * Backup bound for the doorbell wait: see ufs_run() for why a count is needed at all.
 *
 * The number is not arbitrary any more. Build 81 measured the wait directly (2.2 million polls
 * for a 2 MB request, printed as `2165207p`), so the cost tracks the number of bytes moved, not
 * the number of requests: roughly one poll per byte on this link. 1 << 22 was therefore only
 * ever enough for about 4 MB, and build 82 -- the first version that really handed 8 MB to one
 * request -- ran out mid-transfer and reported "doorbell timeout (no timebase: counted polls)".
 * ufs_run() now scales the bound from s->bytes; this is only the floor for a request that
 * declares no data.
 */
#define UFS_MAX_POLLS        (1u << 24)
/* the halt in ufs_init() is a register handshake, not a transfer: a far smaller bound is plenty */
#define UFS_HALT_POLLS       (1u << 16)
/*
 * The UPIU task tag. The kernel driver uses the slot number for it, and so do we: one request in
 * flight, in slot 0. A tag of our own choosing would have to stay inside the device's tag budget.
 */
#define UFS_SLOT0_TAG        0u

struct ufs_err {
	const char *what;
	uint32_t status;
};

static struct ufs_err ufs_last;

/*
 * Our list: 32 UTRDs of 32 bytes, then one command descriptor per slot. The controller requires
 * the list to be 4 KiB aligned and the descriptors 128-byte aligned, hence the padding.
 */
struct ufs_slot {
	volatile uint32_t *utrd;        /* 8 dwords, in the list at UFS_AREA_LIST */
	volatile uint32_t *ucd;         /* UCD_SIZE bytes, at UFS_AREA_UCD + n * UCD_SIZE */
	uint64_t ucd_pa;
	uint32_t bytes;                 /* how much of UFS_AREA_BUF this request moves */
};

/*
 * The working memory lives at an absolute address the board header names, not in .bss. The loader
 * is loaded at 0x40080000 and the Android image it chains back to sits at _start + CHAIN_OFFSET =
 * 0x400c0000, so the loader's own image -- .text, .rodata, .data and .bss together -- has to end
 * below that. A 64 KB data buffer plus 32 command descriptors is 290 KB, which would push .bss
 * over the chained image and overwrite it before anything ever ran.
 */
#define UFS_AREA_LIST   ((volatile uint32_t *)(uintptr_t)UFS_AREA_PA)
#define UFS_AREA_UCD    ((uint8_t *)(uintptr_t)(UFS_AREA_PA + UFS_UCD_OFFSET))
#define UFS_AREA_BUF    ((uint8_t *)(uintptr_t)(UFS_AREA_PA + UFS_BUF_OFFSET))

/* board.h names the area and says why it is not .bss; this is the fallback for a build without one. */
#ifndef UFS_AREA_PA
#define UFS_AREA_PA           0x60100000ULL
#endif
#define UFS_UCD_OFFSET        0x1000u               /* the list is 1 KB; descriptors start a page in */
#define UFS_BUF_OFFSET        (UFS_UCD_OFFSET + UFS_SLOTS * UCD_SIZE)
#define UFS_AREA_SIZE         (UFS_BUF_OFFSET + UFS_SLOT_BUFFER)

static struct ufs_slot ufs_slots[UFS_SLOTS];

/*
 * The unit's own logical block size, which is not necessarily the 4 KiB this file used to assume:
 * READ(10) counts blocks in whatever the device says, and the scatter file cannot answer it (its
 * block_size is a transfer unit, not the logical one). READ CAPACITY(10) reports it, so ask once
 * and believe the answer when it is one a UFS device is allowed to give.
 */
static uint32_t ufs_lbs = UFS_BLOCK;
static int ufs_lbs_known;

static uint64_t ufs_base = UFS_BASE;
static uint32_t ufs_nutrs;
static int ufs_reset_done;
static uint32_t ufs_hce_after_reset, ufs_hce_enabled, ufs_hcs_after_enable;

/*
 * HCS read at each stage of ufs_init(). HCS is the cheapest way to tell "the controller is alive
 * and still owns the device" from "the register block has gone dead": bit 0 is DEVICE_PRESENT,
 * bit 1 transfer-list-ready, bit 3 UIC-command-ready, bit 4 the sticky host error. Printing one
 * value at the end cannot say which of six writes killed it, so record one per stage.
 */
static uint32_t ufs_hcs_trace[10];
static const char *ufs_hcs_where[10];
/*
 * A second, finer trace for the window between "ufs_init returned with HCS 0x10f" and the ring.
 *
 * Build 58 settled the coarse question: every one of the ten samples taken inside ufs_init,
 * including the one after the run/stop write in the common tail, reads 0x10f. Yet the same boot
 * reads `ring 0x0` and `hcs 0x501` at the ring. So the READY bits go away in a window that
 * contains no register write at all -- unless one of the descriptor writes or the cache
 * maintenance is what does it. Four samples inside ufs_run say which.
 */
static uint32_t ufs_ring_trace[5];
static const char *ufs_ring_where[5];
static uint32_t ufs_tail_wrote_rsr, ufs_tail_rsr_before;
static uint32_t ufs_hiber_exit_ok;
/*
 * The FIRST request is the only one whose numbers mean anything.
 *
 * Build 59 sampled HCS five times inside ufs_run and every sample read 0x501 -- but the trace
 * array is shared, so what the report shows is whatever the LAST of the probe's seven variants
 * left behind, not the first attempt. The value that matters is the one from request number
 * one: if the controller is already 0x501 there, the READY bits were never lost and something
 * earlier is wrong. If request one sees 0x10f and only later ones see 0x501, then the first
 * request itself is what takes the controller down -- and IS 0x815 says why: bit 11
 * DEVICE_FATAL_ERROR plus bit 10 UIC_POWER_MODE plus bit 2 UIC_ERROR. A device that declares a
 * fatal error makes the host tear its lists down, and from then on nothing is serviced.
 */
static uint32_t ufs_run_calls;
static uint32_t ufs_first_hcs, ufs_first_rsr, ufs_first_is_before, ufs_first_is_after;
static uint32_t ufs_first_dbr_before, ufs_first_dbr_after, ufs_first_ocs, ufs_first_exit;
static uint8_t ufs_first_resp[8];
static uint32_t ufs_stale_clears;
/*
 * What the wait loop last saw. A timeout that reports only "doorbell timeout" cannot be told
 * apart from a controller that is answering something else entirely, and the board has to be
 * rebooted by hand to see the next attempt, so record both before giving up.
 */
static uint32_t ufs_mtk_xoufs_before, ufs_mtk_xoufs_after;
static uint32_t ufs_mtk_refclk_before, ufs_mtk_refclk_after;
static uint32_t ufs_mtk_refclk_ack;
static uint32_t ufs_sip_ref_pre;    /* x0 of REF_CLK_NOTIFICATION(on=1, PRE_CHANGE) */
static uint32_t ufs_sip_ref_post;   /* x0 of REF_CLK_NOTIFICATION(on=1, POST_CHANGE) */
static uint32_t ufs_sip_mtcmos_off; /* x0 of MTCMOS_CTRL(0) */
static uint32_t ufs_sip_mtcmos_on;  /* x0 of MTCMOS_CTRL(1) */
static uint32_t ufs_hce_init;        /* REG_CONTROLLER_ENABLE as LK handed it over */
static uint32_t ufs_hce_after_set;   /* ... and after we ask for it back */
static uint32_t ufs_hce_at_ring;     /* ... and at the moment a request is rung */
static uint32_t ufs_post_link_rsr;  /* UTRLRSR right after DME_LINK_STARTUP */
static uint32_t ufs_post_link_dbr;  /* UTRLDBR right after DME_LINK_STARTUP */
static uint32_t ufs_last_is;
static uint32_t ufs_last_dbr;
static uint32_t ufs_pdn_before, ufs_pdn_asserted, ufs_pdn_after;
static uint32_t ufs_ao_before, ufs_ao_asserted, ufs_ao_after;              /* slots the controller has */
static uint32_t ufs_lk_list_lo, ufs_lk_list_hi;   /* LK's list, so a report can name it */
static uint32_t ufs_lk_cold_lo, ufs_lk_cold_hi;   /* ... as read before HCE was set (unclocked) */
static uint32_t ufs_lk_utrd[8];                   /* LK's own descriptor, captured once */
static int ufs_lk_saved;
static uint32_t ufs_moved_lo, ufs_moved_hi;       /* what UTRLBA read back after the move */
static uint32_t ufs_base_tries_used;              /* how many attempts the base write needed */
static uint32_t ufs_base_hi_wanted;               /* the upper half we asked for (0 on this board) */
static uint32_t ufs_hcs_after_stop, ufs_hcs_after_clr;  /* HCS around the stop-then-clear sequence */
static uint32_t ufs_utmrl_rsr_at_stop, ufs_utmrl_rsr_after; /* task-list run/stop around the same point */
static int ufs_64as;                    /* CAP says the list base has an upper half */
static uint32_t ufs_hce_before, ufs_hce_after;   /* HCE as LK left it, and after the move */
static uint32_t ufs_uic_status;
static uint32_t ufs_uic_step;
static uint32_t ufs_uic_hcs;
static uint32_t ufs_is_before, ufs_door_before;  /* interrupt status and doorbell before we touch it */
static uint32_t ufs_run_after_stop, ufs_door_after_clear;  /* did the list actually stop */
static uint32_t ufs_restart_rsr;                 /* historical: builds 46/47 wrote UTRLRSR here; always 0 now */
static uint32_t ufs_rsr_at_ring, ufs_rsr_after_ring;
static uint32_t ufs_rsr_restarted;
static uint32_t ufs_hcs_at_ring;          /* controller status at the ring, after any restart */
static uint32_t ufs_dbr_at_ring;          /* bell state at the ring, before we press it */
static uint32_t ufs_exit_reason;          /* 1 = IS completion, 2 = bell cleared, 3 = fatal, 4 = timeout */
static uint32_t ufs_is_before_ring;       /* IS as it stands at the ring, never written */
static uint32_t ufs_door_after_ring;      /* did the controller latch the bell at all */
static uint32_t ufs_link_ran;             /* 1 if ufs_link_startup() was actually issued this boot */
static uint32_t ufs_link_skipped;         /* 1 if it was deliberately skipped because the link was live */
static uint32_t ufs_nop_ocs, ufs_nop_resp0, ufs_nop_resp6;   /* NOP OUT probe result */
static uint32_t ufs_query_ocs, ufs_query_resp, ufs_query_rsp0;  /* last QUERY REQUEST result */
static uint32_t ufs_last_resp6, ufs_last_status7;   /* response@6 and SCSI status@7 of the last reply */
static uint8_t ufs_tur_resp[16];                    /* the TUR's reply, verbatim, for the report */
static uint32_t ufs_tur_resp_seen;
static uint32_t ufs_fdevinit_stage, ufs_fdevinit_flag, ufs_fdevinit_tries;
static uint32_t ufs_io_stage, ufs_io_fdev;
/* High-speed negotiation: how far ufs_hs_negotiate() got and what the two ends offered. */
static uint32_t ufs_hs_stage, ufs_hs_ok, ufs_hs_upmcrs, ufs_hs_res;
static uint32_t ufs_hs_gear_rx, ufs_hs_gear_tx, ufs_hs_lane_rx, ufs_hs_lane_tx;   /* the read path's own bring-up: how far it got */
/* How many polls the last completed request took. Zero means it completed without being polled. */
static uint32_t ufs_run_polls[1];
static uint32_t ufs_sip_va09_ok, ufs_sip_devpwr_ok, ufs_sip_rst_lo, ufs_sip_rst_hi;  /* device bring-up SMCs */
static uint32_t ufs_device_woke;   /* the wake sequence ran, so any earlier link is gone */
static uint32_t ufs_tur_ok;               /* the last TEST UNIT READY verdict, for the big report */  /* 1 if the list had to be restarted just before ringing */
#define UFS_BASE_TRIES 8u               /* the base write is not always taken on the first try */
#define UFS_BRINGUP_POLLS 200000u       /* polls, not ticks: this path has no dependable timebase */
#define UFS_SETTLE_N   64u               /* reads after a write, before trusting its readback */
static uint32_t ufs_list_lo, ufs_list_hi;   /* the transfer list we ended up driving */
static int ufs_list_is_lk;                  /* ... and whether it is still LK's, at address 0 */
static int ufs_ready;                   /* our list is in and running */
static int ufs_failed;                  /* a request failed; do not try again */

static int ufs_fail(const char *what, uint32_t status)
{
	ufs_last.what = what;
	ufs_last.status = status;
	return 0;
}

static inline uint32_t ufs_rd(uint32_t off)
{
	return *(volatile uint32_t *)(uintptr_t)(ufs_base + off);
}

static inline void ufs_wr(uint32_t off, uint32_t v)
{
	__asm__ volatile("dsb sy" ::: "memory");
	*(volatile uint32_t *)(uintptr_t)(ufs_base + off) = v;
	__asm__ volatile("dsb sy" ::: "memory");
}

/*
 * The UFS power domain and its reference clock do not belong to the AP: secure firmware
 * (TFA/SPM) owns them, and the vendor driver asks for them with an SMC rather than by
 * poking registers. Writing REG_UFS_REFCLK_CTRL on our own is therefore a request nobody
 * is listening to, which is exactly what this board showed -- REFCLK_ACK never came back
 * and the controller's master enable never latched.
 *
 * The function id is MTK_SIP_UFS_CONTROL, and MTK_SIP_SMC_CMD(0x276) expands to
 * fast-call | 64-bit | owner SIP | 0x276 = 0xC2000276. Sub-commands are single bits; the
 * two this loader needs are REF_CLK_NOTIFICATION (bit 3) and MTCMOS_CTRL (bit 9).
 *
 * Returns x0..x3: x0 is the status in SIP_SVC_E_* form -- 0 success, -1 not supported,
 * -4 permission denied -- and x3 is the result pointer the vendor driver passes in.
 */
#define UFS_SIP_FN_ID            0xC2000276ULL
#define UFS_SIP_REF_CLK_NOTIFY   (1ULL << 3)
#define UFS_SIP_MTCMOS_CTRL      (1ULL << 9)
#define UFS_SIP_DEVICE_RESET     (1ULL << 1)
#define UFS_SIP_VA09_PWR_CTRL    (1ULL << 0)
#define UFS_SIP_DEVICE_PWR_CTRL  (1ULL << 7)
#define UFS_SIP_PRE_CHANGE       0ULL
#define UFS_SIP_POST_CHANGE      1ULL

struct ufs_smc_ret { uint64_t a0, a1, a2, a3; };

static struct ufs_smc_ret ufs_smc(uint64_t fn, uint64_t a1, uint64_t a2,
                                  uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6)
{
	register uint64_t x0 __asm__("x0") = fn;
	register uint64_t x1 __asm__("x1") = a1;
	register uint64_t x2 __asm__("x2") = a2;
	register uint64_t x3 __asm__("x3") = a3;
	register uint64_t x4 __asm__("x4") = a4;
	register uint64_t x5 __asm__("x5") = a5;
	register uint64_t x6 __asm__("x6") = a6;
	__asm__ volatile("smc #0"
	                 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
	                 : "r"(x4), "r"(x5), "r"(x6)
	                 : "memory");
	struct ufs_smc_ret r;
	r.a0 = x0; r.a1 = x1; r.a2 = x2; r.a3 = x3;
	return r;
}

/* UFS_MTK_SIP_REF_CLK_NOTIFICATION: on=1/0, stage=PRE_CHANGE/POST_CHANGE. */
static struct ufs_smc_ret ufs_mtk_ref_clk_notify(uint64_t on, uint64_t stage)
{
	return ufs_smc(UFS_SIP_FN_ID, UFS_SIP_REF_CLK_NOTIFY, on, stage, 0, 0, 0);
}

/* UFS_MTK_SIP_MTCMOS_CTRL: op=1 powers the domain on, op=0 restores a backup state. */
static struct ufs_smc_ret ufs_mtk_mtcmos_ctrl(uint64_t on)
{
	return ufs_smc(UFS_SIP_FN_ID, UFS_SIP_MTCMOS_CTRL, on, 0, 0, 0, 0);
}

/* UFS_MTK_SIP_DEVICE_RESET: drive the device's own RST_n line (high = released). */
static struct ufs_smc_ret ufs_mtk_device_reset_ctrl(uint64_t high)
{
	return ufs_smc(UFS_SIP_FN_ID, UFS_SIP_DEVICE_RESET, high, 0, 0, 0, 0);
}

/* UFS_MTK_SIP_VA09_PWR_CTRL: the UFS PHY's analogue rail (0.9 V). */
static struct ufs_smc_ret ufs_mtk_va09_pwr_ctrl(uint64_t on)
{
	return ufs_smc(UFS_SIP_FN_ID, UFS_SIP_VA09_PWR_CTRL, on, 0, 0, 0, 0);
}

/* UFS_MTK_SIP_DEVICE_PWR_CTRL: the device's own supply, ufs_version 0x2 for UFS 2.x. */
static struct ufs_smc_ret ufs_mtk_device_pwr_ctrl(uint64_t on, uint64_t ufs_version)
{
	return ufs_smc(UFS_SIP_FN_ID, UFS_SIP_DEVICE_PWR_CTRL, on, ufs_version, 0, 0, 0);
}

static void ufs_put(void *to, const void *from, uint32_t bytes)
{
	volatile uint32_t *d = (volatile uint32_t *)to;
	const uint8_t *s = (const uint8_t *)from;

	for (uint32_t i = 0; i < bytes / 4; i++) {
		uint32_t w;

		__builtin_memcpy(&w, s + 4 * i, 4);
		d[i] = w;
	}
}

static void ufs_zero(void *to, uint32_t bytes)
{
	volatile uint32_t *d = (volatile uint32_t *)to;

	for (uint32_t i = 0; i < bytes / 4; i++) {
		d[i] = 0;
	}
}

/*
 * Fill in one command descriptor: the UPIU at the front, the PRD table after the response area,
 * and the UTRD that points at both.
 */
/* Diagnostic state written by ufs_place(), read by the test report. */
static uint64_t ufs_place_pa;
/*
 * Where the controller should deposit the data. Zero means the loader's own slot buffer, which
 * then has to be copied out. A non-zero value is a physical address in main memory and is used
 * directly, which is what makes loading a 2.3 GiB image take seconds instead of minutes: the
 * copy out of the slot buffer runs a word at a time through device memory, because the MMU is
 * off and every access is Device-nGnRnE.
 */
static uint64_t ufs_prd_dst;           /* the UCD address handed to the controller */
static uint32_t ufs_place_bytes;        /* bytes asked for, so a zero looks like a zero */
static uint32_t ufs_canary_back;        /* 0xc0de1234 if the UCD is real writable memory */

/*
 * The command descriptor's field placement is not the one JESD223 draws, and the evidence is
 * the controller itself: after a successful doorbell handshake it wrote overall status 0x7 into
 * UTRD word 2, and left our words 3 and 4 -- where JESD223 puts the UCD base address -- at zero.
 * LK's own in-flight descriptor on this board reads 0x....288 0x61 <address> 0x72 ..., i.e. it
 * keeps the address in word 2 too. So the layout is tried rather than assumed: four candidate
 * placements, one boot, and the report says which one the device actually answered on.
 */
/*
 * Clean and invalidate the given range out of every data cache level to the point of coherency.
 *
 * This is not decoration. The controller fetches the request descriptor and the command UPIU by
 * DMA, straight out of DRAM; the CPU has just written them through its own data cache. If the
 * dirty lines are still in the cache when the doorbell rings, the controller fetches whatever
 * DRAM held before -- zeros -- sees a descriptor whose command type is 0, rejects it, and writes
 * an error status back into a descriptor this CPU still has cached.
 *
 * The readable symptom is exactly what the address-word scan showed: every candidate placement
 * fails identically, the status dword shows an error code the CPU never wrote, the command UPIU
 * reads back as we left it, and the response area stays zero. All of those are the CPU looking at
 * its own cache while the device looked at DRAM.
 *
 * The same range is written by the device when the transfer completes, so it is cleaned and
 * invalidated again on the way out, or the CPU reads stale lines and calls an answered request a
 * failure.
 */
static void ufs_cache_sync(volatile void *p, uint32_t len)
{
	uintptr_t a = (uintptr_t)p & ~(uintptr_t)63u;
	uintptr_t end = ((uintptr_t)p + len + 63u) & ~(uintptr_t)63u;

	__asm__ volatile("dsb sy" ::: "memory");
	for (; a < end; a += 64u) {
		__asm__ volatile("dc civac, %0" : : "r"(a) : "memory");
	}
	__asm__ volatile("dsb sy" ::: "memory");
}


/*
 * Where the controller may take its descriptors from, and where it reads command_type, are the
 * two things still open. Both are answerable in one boot, so answer them here instead of one
 * guess per flash.
 *
 * The four-variant probe reported: command_type in DW0[8:11] completes the doorbell at once with
 * OCS_FATAL_ERROR, command_type in DW0[31:28] never completes it. Read one way that says the
 * header is wrong; read the other way it says the header is right and the controller hangs while
 * fetching a descriptor it was told about but cannot reach. The second reading explains more,
 * because an immediate OCS_FATAL_ERROR is what a controller writes when it rejects the command
 * type outright -- which is what command_type=0 is -- and a doorbell that never clears is what a
 * controller does when it is waiting on a bus that will not answer.
 *
 * So: hold command_type valid under both readings at once (both bits written), then vary only the
 * address the descriptors live at. 0x60100000 is memory this loader picked; 0x51266400 is the
 * list LK itself was using for UFS on this very board, so the controller demonstrably can reach
 * it. If the doorbell clears at LK's address and not at ours, the fault is reachability, not
 * layout, and no further descriptor work matters until an area the controller can reach is found.
 */
static uint8_t ufs_lun = UFS_LUN;
static uint32_t ufs_probe_ocs[6];
static uint32_t ufs_probe_resp[6];
static uint32_t ufs_probe_door[6];
static uint32_t ufs_probe_is[6];
static uint64_t ufs_probe_base[6];

static int ufs_place_which(struct ufs_slot *s, const uint8_t *upiu, uint32_t dw0, uint32_t bytes,
			   int which)
{
	uint32_t prds = 0;

	/*
	 * Prove the UCD is real, writable memory before the controller is asked to read it. A silent
	 * write drop (DEVAPC, a shadowed window) looks exactly like a wrong base address afterwards:
	 * the controller runs an empty command and still consumes the doorbell.
	 */
	ufs_place_pa = s->ucd_pa;
	ufs_place_bytes = bytes;
	s->bytes = bytes;
	*(volatile uint32_t *)((uint8_t *)s->ucd + UCD_RESPONSE + 60) = 0xc0de1234u;
	__asm__ volatile("dsb sy" ::: "memory");
	ufs_canary_back = *(volatile uint32_t *)((uint8_t *)s->ucd + UCD_RESPONSE + 60);

	ufs_put((void *)s->ucd + UCD_UPIU, upiu, 32);
	ufs_zero((void *)s->ucd + UCD_UPIU + 32, UCD_PRDT - 32);

	for (uint32_t at = 0; at < bytes; at += UFS_PRD_MAX, prds++) {
		volatile uint32_t *prd = (volatile uint32_t *)((uint8_t *)s->ucd + UCD_PRDT + 16 * prds);
		uint64_t pa = (ufs_prd_dst ? ufs_prd_dst : (uint64_t)(uintptr_t)UFS_AREA_BUF) + at;
		uint32_t len = bytes - at < UFS_PRD_MAX ? bytes - at : UFS_PRD_MAX;

		prd[0] = (uint32_t)pa;
		prd[1] = (uint32_t)(pa >> 32);
		prd[2] = 0;
		prd[3] = len - 1u;
	}

	/*
	 * The request descriptor, exactly as the interface header defines it:
	 *
	 *   DW0..DW3  request_desc_header: cci, ehs_length, crypto, then a byte holding
	 *             interrupt:1 | data_direction:2 | reserved:1 | command_type:4; then dunl,
	 *             then ocs, cds, ldbc, dunu
	 *   DW4..DW5  command_desc_base_addr (64-bit, low half first)
	 *   DW6       response_upiu_length[15:0] | response_upiu_offset[31:16]
	 *   DW7       prd_table_length[15:0] | prd_table_offset[31:16]
	 *
	 * Both offsets and both lengths are counted in dwords, not bytes.
	 */
	for (uint32_t i = 0; i < 8; i++) {
		s->utrd[i] = 0;
	}
	s->utrd[0] = dw0;
	s->utrd[1] = 0;                                          /* dunl */
	s->utrd[2] = 0x0fu;                                      /* ocs=invalid | cds | ldbc | dunu */
	/*
	 * Preset OCS to INVALID (0xf), the way ios-sweet's shim/ufs.c does (`utrl[0].dw2 = 0x0f`).
	 *
	 * When this was zero, a descriptor the controller never touched read back as
	 * "OCS_SUCCESS with an empty response", and that fake success was accepted three separate
	 * times in this project's history. A slot that was never executed now says so.
	 */
	s->utrd[3] = 0;
	s->utrd[4] = (uint32_t)s->ucd_pa;
	s->utrd[5] = (uint32_t)(s->ucd_pa >> 32);
	/*
	 * Both lengths are counts, not byte sizes, and both live in the low half of their dword:
	 *   response_upiu_length = ALIGNED_UPIU_SIZE >> 2 = 128 dwords
	 *   prd_table_length     = the number of entries, not the number of bytes
	 * (ufshcd.c ufshcd_sgl_to_prdt writes sg_entries; the utrd initialiser writes
	 * ALIGNED_UPIU_SIZE >> 2.) Writing 32 and prds * 16 made every transfer fail with
	 * OCS_FATAL_ERROR while the descriptor itself looked correct.
	 */
	s->utrd[6] = (UCD_RESPONSE / 4) << 16 | (UCD_RESPONSE / 4);  /* 0x808000, as it answered */
	s->utrd[7] = (UCD_PRDT / 4) << 16 | prds;
	__asm__ volatile("dsb sy" ::: "memory");
	return 1;
}

/* Which placement the controller actually reads. Set by ufs_pick_layout() before any real I/O. */
static int ufs_layout = 0;
static uint32_t ufs_layout_tried[4];    /* response UPIU byte 0 seen for each candidate */

static int ufs_place(struct ufs_slot *s, const uint8_t *upiu, uint32_t dw0, uint32_t bytes)
{
	return ufs_place_which(s, upiu, dw0, bytes, ufs_layout);
}

/*
 * Ring the doorbell for slot 0 and wait for the controller to clear it. The controller writes the
 * overall status into UTRD word 2 and the response UPIU into the UCD, both of which we read back
 * through the same uncached path we wrote them.
 */
static int ufs_run(uint32_t *ocs_out)
{
	struct ufs_slot *s = &ufs_slots[0];
	uint64_t deadline = dbg_ticks() + dbg_freq() / 1000u * UFS_TIMEOUT_MS;
	uint32_t waited = 0;
	uint32_t is;
	/*
	 * The deadline above is only a deadline if the counter it is measured against actually
	 * advances. On a board where CNTPCT_EL0 is not running, `dbg_ticks() >= deadline` is false
	 * for ever and this loop is the whole system: the loader would sit here with the doorbell
	 * set and never say a word. So the wait is bounded a second time, by counted polls. A poll
	 * that is fast enough to finish in well under ten seconds on any real timebase still exits
	 * on the deadline first; this only decides what happens when there is no timebase at all.
	 */
	uint32_t polls = UFS_MAX_POLLS;
	/*
	 * Scale the budget with the request. Measured on the phone: ~1.1 polls per byte moved, and
	 * the old fixed bound cut an 8 MB request off halfway. Two polls per byte leaves the bound
	 * comfortably above what the hardware has actually taken.
	 */
	if (s->bytes != 0u) {
		uint64_t need = (uint64_t)s->bytes * 2u;
		if (need > (uint64_t)polls) {
			polls = need > 0xffffffffull ? 0xffffffffu : (uint32_t)need;
		}
	}

	/* the controller reads the descriptor and the command by DMA, not through this cache */
	/*
	 * A door-bell bit left standing by an earlier request that never completed is not harmless.
	 * Setting the bit again is a no-op when it is already set, so the controller keeps treating
	 * this slot as pending and the wait below never ends.
	 */
	/*
	 * NOTHING IS WRITTEN TO UTRLCLR.
	 *
	 * Build 50 (the first one whose numbers were actually legible) settles it: `rsr 0x1 ring 0x0`
	 * -- the same boot that read the run/stop register as 1 right after link startup read it as 0
	 * at the moment of the first ring, and the only code that ran in between was this block.
	 * Writing the clear register is what halts the list; with the list halted the controller
	 * never looks at the door bell, and every request times out exactly as the photographs show.
	 *
	 * The polarity question that consumed build 49 is moot: whether the bits mean "clear" or
	 * "keep", the write itself stops the engine. Linux can write it because it owns the list --
	 * it stops the list first, clears, and restarts. This loader inherits LK's running list and
	 * has no business stopping it, so the only safe move is to leave both registers alone and
	 * ring a bell the controller is still watching.
	 */
	ufs_run_calls++;
	ufs_ring_where[0] = "run:start"; ufs_ring_trace[0] = ufs_rd(UFS_HCS);
	if (ufs_run_calls == 1) {
		ufs_first_hcs = ufs_ring_trace[0];
		ufs_first_is_before = ufs_rd(UFS_IS);
		ufs_first_dbr_before = ufs_rd(UFS_UTRLDBR);
	}
	ufs_cache_sync(s->utrd, 32);
	ufs_cache_sync(s->ucd, UCD_PRDT + 16u * (UFS_SLOT_BUFFER / UFS_PRD_MAX + 1));
	/*
	 * Only the bytes this request actually moves. A TEST UNIT READY moves none, and cleaning
	 * and invalidating 256 KB of buffer for it is work the controller never asked for -- which
	 * is worth avoiding on a path that runs before the watchdog is even armed.
	 */
	if (s->bytes) {
		ufs_cache_sync(ufs_prd_dst ? (volatile void *)(uintptr_t)ufs_prd_dst : UFS_AREA_BUF,
			       s->bytes);
	}
	/*
	 * Nothing is written to IS here. An earlier build cleared the clearable summary bits before
	 * every ring as a guard against a stale fatal bit, and the very next boot stopped clearing
	 * the door bell at all -- two `r:bell` lines and a hang. Whatever that write disturbs, it is
	 * worse than the stale bit it was meant to remove; the mask in the loop already excludes
	 * every bit a stale request could leave behind.
	 */
	/*
	 * NOTHING IS WRITTEN TO IS EITHER.
	 *
	 * Build 51 is the measurement that closes this: its screen read `clr 1 dbr 0x0` and
	 * `rsr 0x1 ring 0x0`. The clear fired exactly once, and by the time the ring happened the
	 * run/stop register had gone from 1 to 0. The only write in between was this one. So on this
	 * controller writing IS does not merely acknowledge a status bit -- it stops the request
	 * list, and a stopped list ignores the door bell. That is the same failure the very first
	 * wide-mask version produced, and it turns out the width was never the point.
	 *
	 * So the leftover completion bit is handled by the wait loop instead: it looks at IS bit 0
	 * AND at the door bell, and only treats the transfer as done when the bell has actually
	 * dropped. A stale bit alone cannot fake completion any more, because the descriptor is
	 * written fresh and the bell is the thing the controller itself clears.
	 */
	ufs_ring_where[1] = "run:cache"; ufs_ring_trace[1] = ufs_rd(UFS_HCS);
	ufs_is_before_ring = ufs_rd(UFS_IS);
	ufs_hce_at_ring = ufs_rd(UFS_HCE);
	/*
	 * Read the list state here, at the last moment before ringing, and do nothing with it.
	 *
	 * Build 46 wrote this register when it read 0 and spent a whole photo cycle chasing a
	 * stopped list that was never stopped: the 0 was an uninitialised logging variable, and the
	 * live read on build 47's screen says `ring rsr 0x1`. Builds 30 to 33 had already shown what
	 * the write costs -- the door bell never clears after it. So this line is a readout only.
	 */
	ufs_ring_where[2] = "run:prersr"; ufs_ring_trace[2] = ufs_rd(UFS_HCS);
	ufs_rsr_at_ring = ufs_rd(UFS_UTRLRSR);
	/*
	 * Restart the list if it is not running, because a stopped list ignores the door bell
	 * entirely -- that is the single fact build 50 pinned down (`ring 0x0` on a boot where
	 * post-link had read `0x1`). Earlier builds feared this write because the door bell stopped
	 * clearing after it, but in those builds the clear register was being written in the same
	 * breath; with UTRLCLR left alone the write is the normal operating state, and it is what LK
	 * itself does before it reads flash.
	 */
	ufs_rsr_restarted = 0;
	if ((ufs_rsr_at_ring & 1u) == 0) {
		ufs_wr(UFS_UTRLRSR, 1u);
		__asm__ volatile("dsb sy" ::: "memory");
		for (uint32_t n = 0; n < 10000u; n++) {
			if ((ufs_rd(UFS_UTRLRSR) & 1u) != 0) { break; }
		}
		ufs_rsr_restarted = 1;
	}
	ufs_rsr_after_ring = ufs_rd(UFS_UTRLRSR);
	ufs_hcs_at_ring = ufs_rd(UFS_HCS);
	ufs_dbr_at_ring = ufs_rd(UFS_UTRLDBR);
	__asm__ volatile("dsb sy" ::: "memory");
	ufs_ring_where[3] = "run:prebell"; ufs_ring_trace[3] = ufs_rd(UFS_HCS);
	if (ufs_run_calls == 1) {
		ufs_first_rsr = ufs_rsr_at_ring;
		ufs_first_dbr_after = ufs_door_after_ring;
	}
	UFS_TRACE("r:bell\n");
	ufs_wr(UFS_UTRLDBR, 1u);
	ufs_ring_where[4] = "run:postbell"; ufs_ring_trace[4] = ufs_rd(UFS_HCS);
	/*
	 * Make sure the doorbell actually took before waiting on it. If the write is dropped the
	 * bit reads back 0, the loop below would break on its first look, and utrd[2] would still
	 * hold OCS_INVALID - a request that was never sent would be reported as one that failed.
	 * Writing it again is harmless when the first write did take.
	 */
	ufs_door_after_ring = ufs_rd(UFS_UTRLDBR);
	if ((ufs_door_after_ring & 1u) == 0) {
		ufs_wr(UFS_UTRLDBR, 1u);
		__asm__ volatile("dsb sy" ::: "memory");
		ufs_door_after_ring = ufs_rd(UFS_UTRLDBR);
	}

	for (;;) {
		/*
		 * The door bell clearing means the controller finished with this descriptor, and the
		 * status it wrote into DW2 is then the only thing that says whether the request worked.
		 * Checking IS for fatal bits first throws that away: on this board a completed TUR comes
		 * back with OCS_SUCCESS and DW2 zero while IS still has DEVICE_FATAL_ERROR (0x800) set
		 * from the link's earlier life, so the transfer looked failed even though the descriptor
		 * says it ran. Read the door bell first, always.
		 */
		/*
		 * Wait for completion, which is IS bit 0 (UTP_TRANSFER_REQ_COMPL), not for the door bell
		 * to clear. The door bell is cleared when the controller *accepts* the request; the
		 * request then runs and announces itself by setting this bit. Polling the door bell
		 * therefore waits on a state that is already gone, and the loop burns its whole poll
		 * budget while the answer sits in IS -- exactly the readings on the last boot: IS 0x5
		 * (completion + UIC event), door bell 0, OCS written, and a report of "timeout".
		 * The door-bell branch stays as a fallback for a controller that clears the bit late.
		 */
		is = ufs_rd(UFS_IS);
		/*
		 * IS bit 0 only means "this request finished" if it was clear when the bell rang.
		 *
		 * Build 52's screen: `bel 0x0 0x1` -- the bell latched, which is the first time that has
		 * ever happened -- and `isr 0x815`, i.e. UTP_TRANSFER_REQ_COMPL was ALREADY standing from
		 * the link's earlier life before a single byte was requested. The loop took one look at
		 * it, called the transfer done, and reported ocs 0 with an empty response again.
		 *
		 * Writing IS to retire the stale bit is not available to us: the same build proves that
		 * any write there stops the request list. So the bit is left standing and the test
		 * becomes a transition test -- completion is the bit ARRIVING, which cannot be faked by
		 * the leftovers of a previous link.
		 */
		if ((is & 0x1u) != 0 && (ufs_is_before_ring & 0x1u) == 0) {
			ufs_exit_reason = 1;
			break;
		}
		/*
		 * The bell going clear only means "finished" if it was ever seen set. Build 51 ended
		 * with ocs 0 on a request nobody ran: the bell write was never latched, this branch
		 * fired on its first look, and the descriptor still read zero because we were the last
		 * ones to write it. So the fallback now requires the latch that ufs_door_after_ring
		 * captured -- without it a clear bell proves nothing and the loop keeps waiting until
		 * the poll budget runs out, which at least reports honestly.
		 */
		if ((ufs_door_after_ring & 1u) != 0 && (ufs_rd(UFS_UTRLDBR) & 1u) == 0) {
			ufs_exit_reason = 2;
			break;
		}
		/*
		 * Only the two bits that mean "this host controller is gone" stop the wait:
		 * CONTROLLER_FATAL_ERROR (bit 16) and SYSTEM_BUS_FATAL_ERROR (bit 17). When either is
		 * set nothing will ever complete, so waiting out the full budget buys nothing.
		 *
		 * Everything else has to be left to complete. DEVICE_FATAL_ERROR (bit 11) is about the
		 * device, not about this transfer, and on this board it is set from early in the boot
		 * (the link's UECPA latched a LINERESET before we ever rang a door bell); a TUR that has
		 * already run still reports OCS_SUCCESS with it standing. UTP_ERROR (bit 12) is likewise
		 * sticky. UIC_ERROR (bit 2) was in this set before and never belonged: it is a plain
		 * clearable event. Every one of those made a finished request look like a failed one --
		 * the door bell was clear, DW2 read zero, and this test threw the answer away.
		 */
		if ((is & ((1u << 16) | (1u << 17))) != 0) {
			/*
			 * Acknowledge and give up -- but acknowledge only the bits that are safe to
			 * write. IS is write-one-to-clear, so writing the fatal bits themselves back
			 * latches them: CONTROLLER_FATAL_ERROR and SYSTEM_BUS_FATAL_ERROR are states the
			 * controller cannot leave by being told to, and the next read of HCS comes back
			 * zero. Mask first, always.
			 */
			/* Not even here. Build 51 showed any write to IS stops the request list, and
			 * the two bits in question are states the controller cannot be talked out of
			 * anyway -- acknowledging them buys nothing and costs the list. */
			ufs_exit_reason = 3;
			return ufs_fail("controller error", is);
		}
		ufs_last_is = is;
		waited++;
		if (dbg_ticks() >= deadline) {
			ufs_last_dbr = ufs_rd(UFS_UTRLDBR);
			ufs_exit_reason = 4;
			return ufs_fail("doorbell timeout", ufs_last_dbr);
		}
		if (--polls == 0) {
			ufs_last_dbr = ufs_rd(UFS_UTRLDBR);
			ufs_exit_reason = 4;
			return ufs_fail("doorbell timeout (no timebase: counted polls)", ufs_last_dbr);
		}
	}

	/*
	 * The device wrote the data, the status and the response UPIU behind this cache: DC IVAC
	 * covers all three, because on this core a DMA write leaves the CPU's copy of the line
	 * untouched. Reading the buffer without this step returns whatever the CPU happened to
	 * hold, which changes from one read to the next as unrelated traffic evicts lines -- three
	 * reads of the same block came back with three different checksums before this call was
	 * here.
	 */
	ufs_run_polls[0] = waited;
	ufs_cache_sync(s->utrd, 32);
	ufs_cache_sync(s->ucd, UCD_PRDT + 16u * (UFS_SLOT_BUFFER / UFS_PRD_MAX + 1));
	if (s->bytes) {
		/*
		 * Sync where the controller actually wrote, which is not always UFS_AREA_BUF: when
		 * ufs_read() takes the zero-copy path the PRDs point straight at the caller's buffer.
		 * Syncing UFS_AREA_BUF there invalidates 8 MB of memory nobody wrote to and leaves the
		 * 8 MB the device filled untouched.
		 */
		ufs_cache_sync(ufs_prd_dst ? (volatile void *)(uintptr_t)ufs_prd_dst : UFS_AREA_BUF,
			       s->bytes);
	}
	/*
	 * The overall command status sits in byte 8 of the descriptor header, which on little
	 * endian is the low byte of DW2 -- include/ufs/ufshci.h puts `u8 ocs` right after `__le32
	 * dunl`. Reading it from DW0 bits 16:23 instead reported OCS_INVALID for every request.
	 */
	*ocs_out = s->utrd[2] & UTRD_OCS_MASK;
	if (ufs_run_calls == 1) {
		ufs_first_is_after = ufs_last_is;
		ufs_first_exit = ufs_exit_reason;
		ufs_first_ocs = *ocs_out;
		for (uint32_t b = 0; b < 8u; b++) {
			ufs_first_resp[b] = *(volatile const uint8_t *)((uint8_t *)s->ucd + UCD_RESPONSE + b);
		}
	}
	return 1;
}

/*
 * The response UPIU's status byte, at offset 6 of the response: 0 means the device did what was
 * asked. Byte 5 would be the query function, 7 the flags and EHS length; the kernel driver reads
 * the same two bytes when it reports one (hakios.ufshc.c hc_complete).
 */
static uint8_t ufs_response_status(struct ufs_slot *s)
{
	volatile const uint8_t *r = (volatile const uint8_t *)s->ucd + UCD_RESPONSE;

	return r[6];
}

/*
 * Byte 7 is the SCSI status, which is a different thing from byte 6.
 *
 * uapi/scsi/scsi_bsg_ufs.h spells out the response header: transaction_code@0, flags@1, lun@2,
 * task_tag@3, iid|command_set_type@4, tm_function/query_function@5, **response@6**, **status@7**.
 * `response` is the UPIU-level verdict (0 worked, 1 the target refused), `status` is the SCSI
 * status (0 GOOD, 2 CHECK CONDITION, 8...). A reply is only usable when both are clean, and
 * mixing them up makes a TUR look like "target failure" when it may have been a plain
 * CHECK CONDITION carrying a sense key -- which is what a freshly reset unit answers.
 */
static uint8_t ufs_scsi_status(struct ufs_slot *s)
{
	volatile const uint8_t *r = (volatile const uint8_t *)s->ucd + UCD_RESPONSE;

	return r[7];
}

static int ufs_reply_ok(struct ufs_slot *s)
{
	uint8_t resp = ufs_response_status(s);
	uint8_t st = ufs_scsi_status(s);

	ufs_last_resp6 = resp;
	ufs_last_status7 = st;
	return resp == 0 && (st & 0x3fu) == 0;
}

/*
 * Take the controller over: stop LK's list, point it at ours, start it. Called once, on the first
 * read. If the controller is not where we expect it, say so and leave everything alone.
 */

/*
 * Leave UniPro hibernate (H8).
 *
 * This is the one thing the loader never did, and it explains a symptom nothing else did: TEST
 * UNIT READY answered, the controller reported every list bit ready, and yet a real READ came
 * back with OCS_FATAL_ERROR (0x7) and a response UPIU of zeros. A link left in H8 by LK still
 * carries the short UIC-level traffic TUR needs, so readiness checks all pass -- but a data
 * transfer cannot start until the link is out of hibernate.
 *
 * The host does this with a UIC command, not a transfer: write the arguments, write the opcode to
 * REG_UIC_COMMAND, and wait for the controller to report it done. drivers/ufs/core/ufshcd.c does
 * exactly this in ufshcd_uic_hibern8_exit() with UIC_CMD_DME_HIBER_EXIT, and that opcode is 0x18
 * in include/ufs/ufshci.h. DME_HIBER_EXIT needs no arguments.
 *
 * Two status bits can say it finished: IS.UIC_HIBERNATE_EXIT (the link actually came out) and
 * IS.UIC_COMMAND_COMPL (the command was accepted). A controller that was never in H8 may raise
 * neither, which is not a failure -- so a timeout here is reported and stepped over, not treated
 * as fatal: the transfer path below is the real test.
 */
/*
 * DME_LINK_STARTUP (0x16) over the UIC channel, with a counted poll rather than dbg_ticks: the
 * timebase is not trustworthy in this loader (BOARD_NO_TIMEBASE exists for that reason), and a
 * loop bounded by a counter cannot hang the way a loop bounded by a clock that never advances
 * can. Returns 1 if the UIC command reported completion.
 */
static uint32_t ufs_reset_ok, ufs_enable_ok;
static uint32_t ufs_reset_hcs, ufs_enable_hcs;
static uint32_t ufs_link_uecpa, ufs_link_uecpa_after;
static uint32_t ufs_link_hcs_before, ufs_link_hcs_after;
static uint32_t ufs_link_is, ufs_link_ok;
/*
 * The UIC result code, which this loader had never read.
 *
 * drivers/ufs/core/ufshcd.c ufshcd_wait_for_uic_cmd() does not judge a UIC command by the
 * interrupt summary. It takes the code the controller leaves in REG_UIC_COMMAND_ARG_2 and masks
 * it with MASK_UIC_COMMAND_RESULT (0xFF, include/ufs/ufshci.h): 0 means the UniPro stack carried
 * the command out, anything else is the failure reason. IS only says the command was *accepted*.
 *
 * That distinction is the whole reason every build so far printed `ok 1` next to a link that was
 * not up: UIC_COMMAND_COMPL is set whether the command succeeded or failed, so the return value
 * was reporting "the controller answered" and the caller was reading it as "the link is up".
 */
static uint32_t ufs_link_result;           /* REG_UIC_COMMAND_ARG_2 & 0xFF after LINK_STARTUP */
static uint32_t ufs_uic_result;            /* same, for the generic helper */
/* What the three ufs_mtk_pre_link() DME_SETs reported: command ok, then the stack's result code,
 * then the value a following DME_GET read back (so the screen distinguishes "write refused" from
 * "write accepted and read back the same"). */
static uint32_t ufs_link_already_up;       /* HCE was already set: the link is LK's, not ours */
static uint32_t ufs_pre_lpm_ok, ufs_pre_lpm_res;
static uint32_t ufs_pre_txlcc_ok, ufs_pre_txlcc_res;
static uint32_t ufs_pre_stall_ok, ufs_pre_stall_res, ufs_pre_stall_val;


/*
 * One UIC command, bounded by a counter. Every UIC command in this controller has the same shape:
 * three argument registers, then the opcode in REG_UIC_COMMAND, then completion via IS. Nothing
 * here trusts the timebase (BOARD_NO_TIMEBASE exists because it can be dead), so a loop that runs
 * out of counts is the only way out -- a UIC command that never completes must not become a hang.
 */
static uint32_t ufs_uic_status, ufs_uic_hcs_after, ufs_uic_is_after;

static int ufs_uic_cmd4(uint32_t command, uint32_t arg1, uint32_t arg2, uint32_t arg3)
{
	uint32_t loops = 200000;
	uint32_t is;

	if ((ufs_rd(UFS_HCS) & UFS_HCS_UICRDY) == 0) {
		return 0;
	}
	ufs_wr(UFS_IS, UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL |
	               UFS_IS_UIC_LINK_STARTUP | 0x1u);
	ufs_wr(UFS_UIC_ARG1, arg1);
	ufs_wr(UFS_UIC_ARG2, arg2);
	ufs_wr(UFS_UIC_ARG3, arg3);
	__asm__ volatile("dsb sy" ::: "memory");
	ufs_wr(UFS_UIC_COMMAND, command);
	__asm__ volatile("dsb sy" ::: "memory");

	for (;;) {
		is = ufs_rd(UFS_IS);
		if ((is & (UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL |
		           UFS_IS_UIC_LINK_STARTUP)) != 0) {
			break;
		}
		if (--loops == 0) {
			break;
		}
	}
	ufs_uic_status = is;
	ufs_uic_hcs_after = ufs_rd(UFS_HCS);
	ufs_uic_is_after = is;
	ufs_uic_result = ufs_rd(UFS_UIC_ARG2) & UFS_UIC_RESULT_MASK;
	ufs_wr(UFS_IS, UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL |
	              UFS_IS_UIC_LINK_STARTUP);
	__asm__ volatile("dsb sy" ::: "memory");
	/*
	 * Accept the command only if the stack said it worked. Completion alone is not success --
	 * see the note on ufs_link_result above.
	 */
	if ((is & (UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL |
	           UFS_IS_UIC_LINK_STARTUP)) == 0) {
		return 0;
	}
	return ufs_uic_result == 0;
}

static int ufs_uic_cmd(uint32_t command, uint32_t arg1)
{
	return ufs_uic_cmd4(command, arg1, 0, 0);
}

/*
 * One DME_SET.
 *
 * Same three registers as every other UIC command, but the encoding matters and is easy to get
 * wrong in a way that silently does nothing:
 *
 *   ARG1 = UIC_ARG_MIB_SEL(attr, sel) = ((attr & 0xFFFF) << 16) | (sel & 0xFFFF)
 *   ARG2 = UIC_ARG_ATTR_TYPE(attr_set) = ((attr_set & 0xFF) << 16)   -- 0 is ATTR_SET_NOR
 *   ARG3 = the value
 *
 * (ufshci.h UIC_ARG_MIB_SEL / UIC_ARG_ATTR_TYPE; ufshcd.c ufshcd_dme_set_attr.) The attribute id
 * is a 16-bit MIB number that already includes its own selector byte on this stack, so the value
 * goes in the high half of ARG1 and zero in the low half.
 *
 * Returns 1 only when both the command completed and the stack's result code is zero -- the same
 * two-condition rule ufs_link_startup() uses, and for the same reason.
 */
static int ufs_dme_set(uint32_t attr, uint32_t value, uint32_t *out_result)
{
	int ok = ufs_uic_cmd4(UFS_CMD_DME_SET, attr << 16, 0, value);
	if (out_result) {
		*out_result = ufs_uic_result;
	}
	return ok;
}

/* DME_GET: same encoding, and the value the stack reports comes back in ARG3. */
static int ufs_dme_get(uint32_t attr, uint32_t *out_value, uint32_t *out_result)
{
	int ok = ufs_uic_cmd4(UFS_CMD_DME_GET, attr << 16, 0, 0);
	if (out_value) {
		*out_value = ufs_rd(UFS_UIC_ARG3);
	}
	if (out_result) {
		*out_result = ufs_uic_result;
	}
	return ok;
}

/* DME_PEER_GET: the same encoding as DME_GET, but addressed to the peer's attribute bank. */
static int ufs_dme_peer_get(uint32_t attr, uint32_t *out_value, uint32_t *out_result)
{
	int ok = ufs_uic_cmd4(UFS_CMD_DME_PEER_GET, attr << 16, 0, 0);
	if (out_value) {
		*out_value = ufs_rd(UFS_UIC_ARG3);
	}
	if (out_result) {
		*out_result = ufs_uic_result;
	}
	return ok;
}

/*
 * DME_SET(PA_PWRMODE) is the one UIC command whose result is not in ARG2: it completes with
 * UIC_COMMAND_COMPL whether the gear change worked or not, and the real answer is UPMCRS in
 * HCS bits 10:8. Linux waits for the completion and then requires UPMCRS == PWR_LOCAL
 * (ufshcd_uic_pwr_ctrl, ufshcd.c:4341). Checking only the completion bit is how a failed
 * negotiation would look like a success here.
 */
static int ufs_pwr_mode_set(uint32_t mode, uint32_t *out_upmcrs)
{
	uint32_t hcs = 0;
	uint32_t loops = 200000u;

	ufs_wr(UFS_IS, UFS_IS_UIC_CMD_COMPL);
	ufs_wr(UFS_UIC_ARG1, UFS_PA_PWRMODE << 16);
	ufs_wr(UFS_UIC_ARG2, 0);
	ufs_wr(UFS_UIC_ARG3, mode);
	__asm__ volatile("dsb sy" ::: "memory");
	ufs_wr(UFS_UIC_COMMAND, UFS_CMD_DME_SET);
	__asm__ volatile("dsb sy" ::: "memory");

	while (loops-- != 0u) {
		hcs = ufs_rd(UFS_HCS);
		if (((hcs >> UFS_UPMCRS_SHIFT) & UFS_UPMCRS_MASK) == UFS_PWR_LOCAL) {
			break;
		}
		if ((ufs_rd(UFS_IS) & UFS_IS_UIC_CMD_COMPL) != 0) {
			/* Completed: whatever UPMCRS says now is final. */
			hcs = ufs_rd(UFS_HCS);
			break;
		}
	}
	hcs = ufs_rd(UFS_HCS);
	ufs_wr(UFS_IS, UFS_IS_UIC_CMD_COMPL);
	__asm__ volatile("dsb sy" ::: "memory");
	if (out_upmcrs) {
		*out_upmcrs = (hcs >> UFS_UPMCRS_SHIFT) & UFS_UPMCRS_MASK;
	}
	return ((hcs >> UFS_UPMCRS_SHIFT) & UFS_UPMCRS_MASK) == UFS_PWR_LOCAL;
}

/*
 * Ask for the fastest gear both ends advertise, i.e. what ufshcd_dme_change_power_mode()
 * does (ufshcd.c:4691): write the gear/lane/termination attributes first, then commit with
 * PA_PWRMODE. Order matters -- committing before the attributes are set asks the peer to
 * switch to a gear it has not been told about yet.
 *
 * Everything is best effort. A refusal leaves the link at PWM-G1, which is exactly the state
 * this loader booted in for a dozen builds, so failing costs nothing but the attempt.
 */
static int ufs_hs_negotiate(void)
{
	uint32_t gear_rx = 0, gear_tx = 0, lane_rx = 0, lane_tx = 0;
	uint32_t res = 0, upmcrs = 0;
	uint32_t fast;

	ufs_hs_stage = 1;
	if (!ufs_dme_get(UFS_PA_MAXRXHSGEAR, &gear_rx, &res) || gear_rx == 0u) {
		ufs_hs_stage = 2;                       /* no HS gear: stay in PWM */
		ufs_hs_gear_rx = gear_rx; ufs_hs_res = res;
		return 0;
	}
	ufs_dme_peer_get(UFS_PA_MAXRXHSGEAR, &gear_tx, &res);
	if (gear_tx == 0u) {
		ufs_hs_stage = 3;
		ufs_hs_gear_rx = gear_rx; ufs_hs_res = res;
		return 0;
	}
	ufs_dme_get(UFS_PA_CONNECTEDRXDATALANES, &lane_rx, &res);
	ufs_dme_get(UFS_PA_CONNECTEDTXDATALANES, &lane_tx, &res);
	if (lane_rx == 0u || lane_tx == 0u) {
		ufs_hs_stage = 4;
		ufs_hs_gear_rx = gear_rx; ufs_hs_gear_tx = gear_tx;
		ufs_hs_lane_rx = lane_rx; ufs_hs_lane_tx = lane_tx; ufs_hs_res = res;
		return 0;
	}
	ufs_hs_gear_rx = gear_rx; ufs_hs_gear_tx = gear_tx;
	ufs_hs_lane_rx = lane_rx; ufs_hs_lane_tx = lane_tx;
	ufs_hs_stage = 5;

	/* HS mode A is the only one PA_HSSERIES accepts on UFS 3.x links without rate B support. */
	fast = 1u;

	ufs_dme_set(UFS_PA_RXGEAR, gear_rx, &res);
	ufs_dme_set(UFS_PA_ACTIVERXDATALANES, lane_rx, &res);
	ufs_dme_set(UFS_PA_RXTERMINATION, fast, &res);
	ufs_dme_set(UFS_PA_TXGEAR, gear_tx, &res);
	ufs_dme_set(UFS_PA_ACTIVETXDATALANES, lane_tx, &res);
	ufs_dme_set(UFS_PA_TXTERMINATION, fast, &res);
	ufs_dme_set(UFS_PA_HSSERIES, UFS_HS_MODE_A, &res);

	/* The peer's UniPro replay timers: without these the first long read stalls. */
	ufs_dme_set(UFS_PA_PWRMODEUSERDATA0, 0u, &res);
	ufs_dme_set(UFS_PA_PWRMODEUSERDATA0 + 1u, 0u, &res);
	ufs_dme_set(UFS_PA_PWRMODEUSERDATA0 + 2u, 0u, &res);
	ufs_dme_set(UFS_PA_PWRMODEUSERDATA0 + 3u, 0u, &res);
	ufs_dme_set(UFS_PA_PWRMODEUSERDATA0 + 4u, 0u, &res);
	ufs_dme_set(UFS_PA_PWRMODEUSERDATA0 + 5u, 0u, &res);
	ufs_dme_set(UFS_DL_FC0_TIMEOUT, 8191u, &res);
	ufs_dme_set(UFS_DL_TC0_TIMEOUT, 65535u, &res);
	ufs_dme_set(UFS_DL_AFC0_TIMEOUT, 32767u, &res);

	ufs_hs_stage = 6;
	ufs_hs_ok = (uint32_t)ufs_pwr_mode_set((gear_rx << 4) | gear_tx, &upmcrs);
	ufs_hs_upmcrs = upmcrs;
	ufs_hs_res = res;
	ufs_hs_stage = ufs_hs_ok ? 7u : 8u;
	return (int)ufs_hs_ok;
}

static int ufs_link_startup(void)
{
	uint32_t hcs = ufs_rd(UFS_HCS);
	uint32_t loops = 200000;
	uint32_t is;

	ufs_link_ok = 0;
	ufs_link_is = 0xffffffffu;
	if ((hcs & UFS_HCS_UICRDY) == 0) {
		return 0;                       /* no UIC channel to ask */
	}
	ufs_wr(UFS_IS, UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL |
	              0x1u /* UTP_TRANSFER_REQ_COMPL */);
	ufs_wr(UFS_UIC_ARG1, 0);
	ufs_wr(UFS_UIC_ARG2, 0);
	ufs_wr(UFS_UIC_ARG3, 0);
	__asm__ volatile("dsb sy" ::: "memory");
	ufs_wr(UFS_UIC_COMMAND, UFS_CMD_DME_LINK_STARTUP);
	__asm__ volatile("dsb sy" ::: "memory");

	for (;;) {
		is = ufs_rd(UFS_IS);
		if ((is & (UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL |
		           UFS_IS_UIC_LINK_STARTUP)) != 0) {
			break;
		}
		if (--loops == 0) {
			break;
		}
	}
	ufs_link_is = is;
	ufs_link_result = ufs_rd(UFS_UIC_ARG2) & UFS_UIC_RESULT_MASK;
	/*
	 * Two conditions, not one. IS says the command was answered; ARG2 says the UniPro stack
	 * actually carried it out. Every build before this one tested only the first, which is why
	 * the screen printed `ok 1` beside a link that had just been torn down -- UECPA 0x80000010
	 * and IS 0x404 (command complete plus UIC_ERROR) on the same line.
	 */
	ufs_link_ok = (is & (UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL |
	                     UFS_IS_UIC_LINK_STARTUP)) != 0 && ufs_link_result == 0;
	ufs_wr(UFS_IS, UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL |
	              UFS_IS_UIC_LINK_STARTUP);
	__asm__ volatile("dsb sy" ::: "memory");
	return ufs_link_ok;
}

static int ufs_uic_hibernate_exit(void)
{
	uint32_t hcs = ufs_rd(UFS_HCS);
	uint32_t deadline_loops = 20000;
	uint32_t is;

	if ((hcs & UFS_HCS_UICRDY) == 0) {
		ufs_uic_status = 0xffffffffu;
		return 0;                       /* no UIC channel: nothing to send */
	}

	ufs_uic_step = 1;
	ufs_wr(UFS_IS, UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL);
	ufs_wr(UFS_UIC_ARG1, 0);
	ufs_wr(UFS_UIC_ARG2, 0);
	ufs_wr(UFS_UIC_ARG3, 0);
	__asm__ volatile("dsb sy" ::: "memory");
	ufs_uic_step = 2;
	ufs_wr(UFS_UIC_COMMAND, UFS_CMD_DME_HIBER_EXIT);
	__asm__ volatile("dsb sy" ::: "memory");
	ufs_uic_step = 3;

	for (;;) {
		is = ufs_rd(UFS_IS);
		if ((is & (UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL)) != 0) {
			break;
		}
		if (--deadline_loops == 0) {
			break;
		}
	}
	ufs_uic_step = 4;

	ufs_uic_status = is & (UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL);
	ufs_wr(UFS_IS, UFS_IS_UIC_HIBER_EXIT | UFS_IS_UIC_CMD_COMPL);
	ufs_uic_hcs = hcs;
	return 1;
}

/*
 * The three resets this board wires to the UFS controller, from the device tree:
 *
 *   syscon@112bc000  "mediatek,mt6895-ufs_pdn_cfg"  reset-controller
 *                    ti,reset-bits = <0x44 0x00 0x48 0x00 0 0 0x1c  0x44 0x01 0x48 0x01 0 0 0x1c>
 *                    => crypto_rst = 0x44 bit 0, hci_rst = 0x44 bit 1, state read back at 0x48
 *   syscon@112b8000  "mediatek,mt6895-ufs_ao_config" reset-controller
 *                    ti,reset-bits = <0x44 0x01 0x48 0x01 0 0 0x1c>
 *                    => unipro_rst = 0x44 bit 0, state read back at 0x48
 *
 * There is no mphy_rst in this board's tree, so three resets is the whole list.
 *
 * This is the one piece of bring-up that has never been done here, and every symptom points at
 * it. Linux does it in ufs_mtk_host_reset() before it touches anything else:
 *
 *     assert hci, assert crypto, assert unipro, usleep(100)
 *     deassert unipro, deassert crypto, deassert hci
 *
 * Assert is the bit being set, deassert is the bit being clear (ti,syscon-reset writes the bit
 * to the offset for assert and clears it for deassert). Without this the controller carries
 * whatever UniPro/PHY state the boot loader left behind: HCS reports a host fatal error that no
 * amount of UIC commands will retire, a door bell made against a 32-bit address clears once and
 * then never again, and requests end in OCS_PEER_COMM_FAILURE -- the host finished and no peer
 * answered. A link that was never physically reset has no peer to answer.
 */
static void ufs_mtk_host_reset(void)
{
	/*
	 * ti,syscon-reset gives each line seven cells:
	 *
	 *     <assert_offset assert_bit deassert_offset deassert_bit status_offset status_bit flags>
	 *
	 * and for this board the device tree spells them out:
	 *
	 *     ufs_ao_config (0x112b8000): <0x44 0x01 0x48 0x01 0x00 0x00 0x1c>          unipro_rst
	 *     ufs_pdn_cfg   (0x112bc000): <0x44 0x00 0x48 0x00 0x00 0x00 0x1c           crypto_rst
	 *                                 <0x44 0x01 0x48 0x01 0x00 0x00 0x1c>          hci_rst
	 *
	 * Assert is a write of the bit to 0x44, deassert is a write of the bit to 0x48. They are
	 * separate pulse registers, not one read/write pair -- 0x48 is not a status register, it is
	 * the deassert trigger, and reading either one back always gives 0 because neither holds a
	 * value.
	 *
	 * The first version of this wrote the assert register in both directions (it cleared bits in
	 * 0x44 to "deassert"), so the controller was asserted and never released. Build 23 is what
	 * that looks like: HCS 0x101f before the calls, 0x0 after, and the controller never came back
	 * for the rest of the boot. The register readings of 0x0 were consistent with that and were
	 * misread as "the writes did not land".
	 */
	ufs_pdn_before = ufs_rd(UFS_SYSCON_PDN + 0x44);
	ufs_ao_before = ufs_rd(UFS_SYSCON_AO + 0x44);

	/* assert, in the order the kernel uses: hci, crypto, then unipro */
	ufs_wr(UFS_SYSCON_PDN + UFS_RESET_ASSERT, 0x2u);        /* hci    */
	ufs_wr(UFS_SYSCON_PDN + UFS_RESET_ASSERT, 0x1u);        /* crypto */
	ufs_wr(UFS_SYSCON_AO + UFS_RESET_ASSERT, 0x1u);         /* unipro */
	__asm__ volatile("dsb sy" ::: "memory");
	for (volatile uint32_t n = 0; n < UFS_RESET_DELAY; n++) { }

	/* deassert, reverse order: unipro, crypto, hci */
	ufs_wr(UFS_SYSCON_AO + UFS_RESET_DEASSERT, 0x1u);
	ufs_wr(UFS_SYSCON_PDN + UFS_RESET_DEASSERT, 0x1u);
	ufs_wr(UFS_SYSCON_PDN + UFS_RESET_DEASSERT, 0x2u);
	__asm__ volatile("dsb sy" ::: "memory");
	for (volatile uint32_t n = 0; n < UFS_RESET_DELAY * 10u; n++) { }

	ufs_pdn_after = ufs_rd(UFS_SYSCON_PDN + 0x44);
	ufs_ao_after = ufs_rd(UFS_SYSCON_AO + 0x44);
}

/*
 * Bring the controller up after a reset.
 *
 * ufs_mtk_host_reset() leaves the controller held in reset, which is what the photograph of build
 * 23 shows: HCS reads 0 and ufs_init() gives up at its first check, before it ever rings a door
 * bell. That is correct behaviour for a controller nobody has switched on yet, and switching it
 * on is the step that has been missing all along -- Linux does it in ufshcd_hba_enable() right
 * after the same reset:
 *
 *     write HCE = CONTROLLER_ENABLE
 *     wait for HCS.UTP_TRANSFER_REQ_LIST_READY   (ufshcd_wait_for_register, 1s budget)
 *     UIC: DME_RESET, DME_ENABLE, LINK_STARTUP
 *     make_hba_operational(): programme UTRLBA/UTMRLBA, set UTRLRSR
 *
 * HCE=1 here is not the write that bricked the controller earlier. That write was HCE=1 issued
 * against a live controller after the clock request had already been dropped; this one is the
 * first thing to touch HCE after a hardware reset put it back in a defined state. If this still
 * reads back 0 the controller never came out of reset and that is worth knowing on its own.
 */
static void ufs_mtk_host_enable(void)
{
	uint32_t polls;

	ufs_hce_after_reset = ufs_rd(UFS_HCE);
	ufs_wr(UFS_HCE, 1u);
	__asm__ volatile("dsb sy" ::: "memory");
	for (polls = 0; polls < UFS_BRINGUP_POLLS; polls++) {
		if ((ufs_rd(UFS_HCE) & 1u) != 0) {
			break;
		}
	}
	ufs_hce_enabled = ufs_rd(UFS_HCE);

	for (polls = 0; polls < UFS_BRINGUP_POLLS; polls++) {
		if ((ufs_rd(UFS_HCS) & UFS_HCS_UTRLRDY) != 0) {
			break;
		}
	}
	ufs_hcs_after_enable = ufs_rd(UFS_HCS);

	/*
	 * Only now does the UIC sequence make sense: before this the stack had nothing to reset and
	 * the commands could not complete, which is the other half of why the earlier builds that
	 * sent them first came back with UECPA latched and UIC_ERROR set.
	 */
	ufs_reset_ok = ufs_uic_cmd(UFS_CMD_DME_RESET, 0);
	ufs_reset_hcs = ufs_rd(UFS_HCS);
	ufs_enable_ok = ufs_uic_cmd(UFS_CMD_DME_ENABLE, 0);
	ufs_enable_hcs = ufs_rd(UFS_HCS);
	ufs_link_startup();
	ufs_link_uecpa_after = ufs_rd(UFS_UIC_ERR_PHY);
	ufs_link_hcs_after = ufs_rd(UFS_HCS);
}

static void __attribute__((unused)) ufs_device_wake(void);

static int ufs_init(void)
{
	ufs_last.what = 0;
	ufs_last.status = 0;
	uint32_t cap, hcs, nutrs;

	if (ufs_ready) {
		return 1;
	}
	if (ufs_failed) {
		return 0;
	}

	/*
	 * Reset the controller's three resets before anything is read. This is the only hardware
	 * initialisation this path has ever skipped, and it runs before the first register access on
	 * purpose: after a reset the controller starts from a known state instead of from whatever
	 * UniPro left behind, and every reading below is then a reading of a controller we own.
	 */
#ifdef UFS_HOST_RESET
	if (!ufs_reset_done) {
		ufs_mtk_host_reset();
		ufs_mtk_host_enable();
		ufs_reset_done = 1;
	}
#else
	/*
	 * The reset above stays off, and three builds are why.
	 *
	 * It was added on the theory that UniPro had never been physically reset, so the peer could
	 * never answer. The screen says the reset itself is what breaks the controller: HCS reads
	 * 0x101f (device present, both list-ready bits, the UIC channel, and the sticky host error)
	 * right up to the call, and 0x0 immediately after it, after which HCE will not take a 1 and
	 * the whole register block reads back zero for the rest of the boot. Deassert was wired to
	 * the wrong offset at first, but build 26 corrected that to a write of the bit to 0x48 and
	 * the outcome did not change -- so the cause is not the deassert spelling.
	 *
	 * The likely cause is clocks. UFS on this SoC needs ten of them (see the clock-names in the
	 * device tree: phy_tx_sym, phy_rx_sym0/1, unipro_sys, unipro_sap, phy_sap, ufs_ck,
	 * ufs_fde_ck, ufs_fde, ufs), and LK turned them on; this loader has no clock driver at all.
	 * Taking the controller into reset while its clocks are not under our control leaves it in a
	 * state nothing here can bring back, which is exactly what the register reads show.
	 *
	 * So the controller stays as LK left it -- the state in which it booted Android -- and the
	 * question narrows to the one the door bell already answered: the descriptor is read, the
	 * request runs, and the device reports a fatal error of its own.
	 */
#endif

	cap = ufs_rd(UFS_CAP);
	hcs = ufs_rd(UFS_HCS);
	nutrs = UFS_CAP_NUTRS(cap);
	ufs_last.status = hcs;
	if ((hcs & UFS_HCS_DP) == 0 || (hcs & UFS_HCS_UTRLRDY) == 0) {
		ufs_last.what = "controller not ready";
		ufs_failed = 1;
		return 0;
	}
	/* We only use slot 0; a unit that reports fewer slots than that is not one we can drive. */
	if (nutrs < 1) {
		ufs_failed = 1;
		return 0;
	}
	ufs_nutrs = nutrs;

	ufs_link_uecpa = ufs_rd(UFS_UIC_ERR_PHY);
	ufs_link_hcs_before = ufs_rd(UFS_HCS);
	/*
	 * The order matters and this code had only the last step of it. Linux brings UniPro up as
	 * DME_RESET (restart the stack), then DME_ENABLE (let the stack carry traffic), and only then
	 * LINK_STARTUP (negotiate lanes with the device) -- see ufshcd_hba_enable(), which does
	 * exactly this pair when HCE cannot be toggled, which is this board's situation.
	 *
	 * Skipping the first two is consistent with what the controller has been reporting: the door
	 * bell clears (so the descriptor was read and the request ran) but the request ends with
	 * OCS_PEER_COMM_FAILURE and the IS bit for a device fatal error is set, i.e. the host side
	 * finished its half and the peer never answered. There is no peer to answer a link that was
	 * never reset and never enabled.
	 */
	ufs_link_uecpa_after = ufs_rd(UFS_UIC_ERR_PHY);
	ufs_link_hcs_after = ufs_rd(UFS_HCS);

	ufs_hcs_where[0] = "entries"; ufs_hcs_trace[0] = ufs_rd(UFS_HCS);
	ufs_lk_list_lo = ufs_rd(UFS_UTRLBA);
	ufs_lk_list_hi = ufs_rd(UFS_UTRLBAU);
	ufs_lk_cold_lo = ufs_lk_list_lo;
	ufs_lk_cold_hi = ufs_lk_list_hi;

	/*
	 * Copy LK's own descriptor out on the very first call, before any write of ours can reach
	 * that memory. It is the only authoritative statement about where this controller expects
	 * each field: it is a descriptor LK filled in and this same controller accepted. Reading it
	 * later is worthless -- a write probe moves UTRLBA and the list under us.
	 */
	if (!ufs_lk_saved && ufs_lk_list_lo) {
		const volatile uint32_t *lk = (const volatile uint32_t *)(uintptr_t)ufs_lk_list_lo;
		for (uint32_t i = 0; i < 8; i++) {
			ufs_lk_utrd[i] = lk[i];
		}
		ufs_lk_saved = 1;
	}

	for (uint32_t i = 0; i < UFS_SLOTS; i++) {
		ufs_slots[i].utrd = (volatile uint32_t *)((uint8_t *)UFS_AREA_LIST + i * 32);
		ufs_slots[i].ucd = (volatile uint32_t *)(UFS_AREA_UCD + i * UCD_SIZE);
		ufs_slots[i].ucd_pa = (uint64_t)(uintptr_t)(UFS_AREA_UCD + i * UCD_SIZE);
	}

	/*
	 * JESD223 says UTRLBA may only be written while the host controller is disabled, so the
	 * first thing tried here was to clear HCE, move the base and set HCE again. That cleared
	 * HCE and never got it back: a write of 1 to HCE read back 0, and from then on every
	 * register in the block read 0. IE was still writable at that point (a probe wrote
	 * 0x00010000 and read it back), so this is not a bus or permission problem -- it is that
	 * halting the controller drops its clock request, and on this SoC an unclocked UFS block is
	 * powered down by SPM. From there nothing can bring it back.
	 *
	 * So do not halt it. What it does accept, measured rather than read out of JESD223:
	 *
	 *   UTRLBA writable while HCE=1 and the list running .... yes (a probe wrote 0xdeadb000
	 *                                                          and read it back)
	 *   UTRLBA writable with UTRLCLR/UTRLRSR=0 first ......... no  (the write was dropped and
	 *                                                          the old base came back)
	 *
	 * That is the opposite of the order the standard describes, and the standard's order was
	 * what this code did. So write the base first, with the list left exactly as LK had it,
	 * and only then clear the doorbell and start the list again.
	 *
	 * IS is write-one-to-clear; the fatal bits from an earlier attempt would otherwise make
	 * the first request look like a controller error of its own making.
	 */
	ufs_hce_after = ufs_rd(UFS_HCE);
	/*
	 * The write itself works -- a probe put 0xdeadb000 there and read it back -- but the write
	 * of our own base was dropped when it was made in the same breath as a write-one-to-clear
	 * of the whole of IS. Clearing IS makes the controller work through its error state, and
	 * the register file does not take a new base until that is over. So: write the base first,
	 * clear IS afterwards, and re-try the base a few times rather than trust one attempt.
	 */
ufs_hcs_where[1] = "pre-branch"; ufs_hcs_trace[1] = ufs_rd(UFS_HCS);
	/* ===== power-up + link startup: shared by BOTH list branches ===== */
	/*
	 * Ask secure firmware for the UFS power domain first. The vendor driver does this in
	 * ufs_mtk_vops_init(), before it touches any clock or the controller: restore the
	 * previous MTCMOS state, then power the domain on. Both calls go through the same SiP
	 * function so the status is recorded and can be read off the screen.
	 */
	{
		struct ufs_smc_ret mr = ufs_mtk_mtcmos_ctrl(0);
		ufs_sip_mtcmos_off = (uint32_t)(mr.a0 & 0xffffffffu);
		mr = ufs_mtk_mtcmos_ctrl(1);
		ufs_sip_mtcmos_on = (uint32_t)(mr.a0 & 0xffffffffu);
	}
	__asm__ volatile("dsb sy" ::: "memory");

	ufs_mtk_xoufs_before = ufs_rd(UFS_MTK_XOUFS_CTRL);
	ufs_wr(UFS_MTK_XOUFS_CTRL, ufs_mtk_xoufs_before | UFS_MTK_XOUFS_CLKCG);
	__asm__ volatile("dsb sy" ::: "memory");
	ufs_mtk_xoufs_after = ufs_rd(UFS_MTK_XOUFS_CTRL);

	/*
	 * Tell TFA/SPM that the reference clock is about to be requested. The vendor driver
	 * brackets the register write with PRE_CHANGE and POST_CHANGE notifications; skipping
	 * them leaves the request unanswered, because the gate is not the register's to open.
	 */
	{
		struct ufs_smc_ret rr = ufs_mtk_ref_clk_notify(1, UFS_SIP_PRE_CHANGE);
		ufs_sip_ref_pre = (uint32_t)(rr.a0 & 0xffffffffu);
	}

	ufs_mtk_refclk_before = ufs_rd(UFS_MTK_REFCLK_CTRL);
	ufs_wr(UFS_MTK_REFCLK_CTRL, UFS_MTK_REFCLK_REQ);
	for (uint32_t n = 0; n < UFS_MTK_REFCLK_TRIES; n++) {
		uint32_t v = ufs_rd(UFS_MTK_REFCLK_CTRL);
		if ((v & UFS_MTK_REFCLK_ACK) != 0) { ufs_mtk_refclk_ack = 1; break; }
	}
	ufs_mtk_refclk_after = ufs_rd(UFS_MTK_REFCLK_CTRL);

	{
		struct ufs_smc_ret rr = ufs_mtk_ref_clk_notify(1, UFS_SIP_POST_CHANGE);
		ufs_sip_ref_post = (uint32_t)(rr.a0 & 0xffffffffu);
	}

	/*
	 * Wake the device BEFORE the controller is enabled and before the link starts.
	 *
	 * Build 68 proved the three SMCs themselves work (`va09 0x0`, `rst 0x0/0x0`) but got
	 * nowhere, and the reason is order: the reset was issued after LINK_STARTUP had already
	 * brought the link up, so it threw away a working link and left the peer dead with
	 * nothing to rebuild it. IS carried DEVICE_FATAL_ERROR (0x800) from before the first
	 * request, which is what a device says when its RST_n was pulsed mid-session.
	 *
	 * The vendor order is: analogue rail on, device supply on, RST_n pulse, then
	 * HBA enable, then link startup. That is the order used here.
	 */
	ufs_device_wake();

	ufs_hce_init = ufs_rd(UFS_HCE);
	if ((ufs_hce_init & 1u) == 0) {
		/*
		 * HCE goes 1 -> 0 -> 1 while the controller runs its own init sequence, so the
		 * write must be followed by a real delay before the first poll; reading back
		 * immediately sees the 0 of the transition and concludes failure. The vendor
		 * driver asks for 600us here.
		 */
		ufs_wr(UFS_HCE, ufs_hce_init | 1u);
		__asm__ volatile("dsb sy" ::: "memory");
		for (volatile uint32_t d = 0; d < 200000u; d++) { __asm__ volatile("nop"); }
		for (uint32_t n = 0; n < UFS_HALT_POLLS; n++) {
			if ((ufs_rd(UFS_HCE) & 1u) != 0) { break; }
		}
	}
	ufs_hce_after_set = ufs_rd(UFS_HCE);
	/*
	 * Did the controller arrive already enabled? Build 48 said no (init 0x0), build 50 said yes
	 * (init 0x1), and the two boots behaved completely differently. The flag decides whether this
	 * loader is allowed to start the link at all -- see the gate at pre-link.
	 */
	/*
	 * ...but the gate below exists to avoid restarting a link that LK built and Android is
	 * running on. After ufs_device_wake() pulsed RST_n there is no such link left: the peer
	 * was reset, so whatever LK negotiated is gone by definition and LINK_STARTUP is not
	 * "restarting" anything, it is the only way back. So when the wake sequence actually
	 * ran, the gate must not apply.
	 */
	ufs_link_already_up = ((ufs_hce_init & 1u) != 0) && !ufs_device_woke;

	/*
	 * Bring the UniPro link up before the first request.
	 *
	 * Nothing in this loader has ever sent DME_LINK_STARTUP: it lives in ufs_mtk_host_enable(),
	 * which sits behind the syscon-reset switch that is off. The device agrees -- it answers a
	 * transfer with IS 0x800, DEVICE_FATAL_ERROR, and no response UPIU at all, which is what a
	 * well-behaved device does when a UPIU arrives over a link that was never started. UECPA
	 * had 0x80000010 latched, the LINERESET indication, from wherever LK left the link.
	 *
	 * Only the startup command is sent, not DME_RESET. BUILD 19/20 showed DME_RESET makes the
	 * controller worse -- after it, UECPA could no longer be cleared and IS grew a UIC_ERROR
	 * that was not there before -- because a protocol-layer reset without the matching PHY
	 * bring-up leaves the two sides further apart, not closer.
	 */
	ufs_hcs_where[6] = "pre-link"; ufs_hcs_trace[6] = ufs_rd(UFS_HCS);
	/*
	 * Only start the link if this loader is the one that powered the controller up.
	 *
	 * Build 48: `hce: init 0x0` -- LK left the controller off, we turned it on, LINK_STARTUP came
	 * back `is 0x400 res 0 ok 1`, a clean result. Build 50: `hce: init 0x1` -- LK had left it
	 * running, the link was already up (Android boots over it), and the same LINK_STARTUP came
	 * back `res 1 ok 0`, with IS growing UIC_ERROR and UIC_POWER_MODE and the list ending up
	 * stopped (`rsr ... ring 0x0`). Starting a link that is already running is not idempotent on
	 * this controller; it tears down what was there. Linux never does it either -- it calls
	 * ufshcd_link_startup() once, right after its own hba_start(), never on a live link.
	 *
	 * So: start the link only when this loader is the one that just powered the controller up.
	 * When HCE arrived already set the link is LK's, it works (Android boots over it), and the
	 * correct action is to leave it exactly as it is and go straight to ringing the door bell.
	 */
	if (!ufs_link_already_up) {
	ufs_link_ran = 1;
	ufs_link_uecpa = ufs_rd(UFS_UIC_ERR_PHY);
	ufs_link_hcs_before = ufs_rd(UFS_HCS);

	/*
	 * ufs_mtk_pre_link(): the three UniPro attributes MTK writes before every link startup.
	 *
	 * The ISP / camera-style "vendor polish" reading of this was wrong. A link started with LPM
	 * still requested and host TX lane clock gating still enabled comes up and then goes away:
	 * build 45 started the link and printed a clean UECPA, and the next read of IS already had
	 * UIC_LINK_LOST in it. The device had been told DEVICE_FATAL_ERROR in the same window. These
	 * three writes are what stops it tearing the link down again, and they have to complete
	 * successfully BEFORE the startup command, which is why they go here and not after.
	 *
	 * The third one is a read-modify-write: clear bit 6 "deep stall" and leave everything else.
	 * If the read fails, skip the write rather than clobber the register with a zero we invented.
	 */
#ifdef UFS_PRE_LINK
	/*
	 * OFF BY DEFAULT. Build 51 measures the cost: with these three writes in front of it,
	 * LINK_STARTUP came back `res 1` -- rejected. Build 48, which started the link with nothing
	 * in front of it, came back `res 0`. The writes themselves succeed (`pre 1/0 1/0 1/0`), so
	 * what breaks is not the command but the state they put the link in: LPM cleared and TX lane
	 * clock gating disabled are things the vendor does *after* the PHY is under its control,
	 * and this loader has no PHY driver. Turned off until the link can be started cleanly.
	 */
	ufs_pre_lpm_ok = ufs_dme_set(UFS_MIB_VS_UNIPROPOWERDOWNCONTROL, 0, &ufs_pre_lpm_res);
	ufs_pre_txlcc_ok = ufs_dme_set(UFS_MIB_PA_LOCAL_TX_LCC_ENABLE, 0, &ufs_pre_txlcc_res);
	{
		uint32_t stall_val = 0, stall_res = 0;
		int got = ufs_dme_get(UFS_MIB_VS_SAVEPOWERCONTROL, &stall_val, &stall_res);
		if (got) {
			ufs_pre_stall_val = stall_val & ~(1u << 6);
			ufs_pre_stall_ok = ufs_dme_set(UFS_MIB_VS_SAVEPOWERCONTROL,
			                               ufs_pre_stall_val, &ufs_pre_stall_res);
		} else {
			ufs_pre_stall_val = stall_val;
			ufs_pre_stall_ok = 0;
			ufs_pre_stall_res = stall_res;
		}
	}
#endif
	ufs_link_uecpa = ufs_rd(UFS_UIC_ERR_PHY);
#if defined(UFS_UIC_BRINGUP) || 1
	/*
	 * DME_RESET -> DME_ENABLE -> LINK_STARTUP, i.e. what Linux runs in ufshcd_hba_enable().
	 *
	 * This block was compiled out for a dozen builds, and correctly so at the time: builds 45 and
	 * 47 measured that running it against a link LK had already negotiated tears that link down
	 * (`uecpa 0x0 -> 0x80000010  is 0x404`, then UIC_LINK_LOST) with nothing here able to rebuild
	 * it.
	 *
	 * Build 69 changed the premise. ufs_device_wake() now pulses the device's own RST_n before
	 * the controller is enabled, so by the time this code runs there IS no negotiated link left --
	 * the peer's UniPro stack was reset too. And the photo says exactly that: LINK_STARTUP alone
	 * came back `is 0x400 res 1 ok 0`, refused, `uecpa 0x0 -> 0x0`. A refused startup on a link
	 * that no longer exists is the one situation where DME_RESET is not destructive but necessary:
	 * it restarts the local stack so the subsequent startup has something to negotiate with.
	 */
	ufs_reset_ok = ufs_uic_cmd(UFS_CMD_DME_RESET, 0);
	ufs_reset_hcs = ufs_rd(UFS_HCS);
	ufs_enable_ok = ufs_uic_cmd(UFS_CMD_DME_ENABLE, 0);
	ufs_enable_hcs = ufs_rd(UFS_HCS);
	ufs_link_startup();
#else
	ufs_link_startup();
#endif
	}
	/*
	 * The link is up, but at whatever speed it came up at: PWM-G1, 0.9 MB/s measured over
	 * 2376 MB. Ask for the HS gear now, before anything is read. Best effort -- if the peer
	 * refuses, the read path below still works, just at PWM speed.
	 */
	/*
	 * Not gated on ufs_link_ok: build 84 reported `up 1 ran 1 link 0 res 0` -- the startup
	 * command was accepted with result 0 yet the completion bits were not seen, and reads
	 * worked anyway. A working link is a working link; ask for the gear regardless.
	 */
	ufs_hs_negotiate();
	ufs_link_uecpa_after = ufs_rd(UFS_UIC_ERR_PHY);
	ufs_link_hcs_after = ufs_rd(UFS_HCS);
	ufs_hcs_where[7] = "post-link"; ufs_hcs_trace[7] = ufs_rd(UFS_HCS);
	ufs_link_skipped = ufs_link_ran ? 0u : 1u;
	/* ===== end of shared power/link block ===== */
#ifdef UFS_USE_LK_LIST
	/*
	 * Write into the list LK was running instead of moving the base.
	 *
	 * Everything else in this file has been fighting the move: the base takes when written with
	 * the list running and is dropped when written after stopping the list, which is the reverse
	 * of what JESD223 says, and the readback of our own descriptor came back shifted by one
	 * dword against what was written. Rather than keep guessing at the controller's idea of the
	 * list, use the list it is already walking: LK left UTRLRSR=1 with the base at 0x51266400,
	 * the controller reads descriptors from there for Android, and we can put ours in slot 0 of
	 * exactly that array. Nothing about the controller's configuration changes; only the 32
	 * bytes it reads next.
	 *
	 * The UCD and the data buffer stay where they are (0x60100000+), because the area matrix
	 * proved the controller can reach that base: ringing the door bell there cleared it.
	 */
	/*
	 * The controller has a master enable (REG_CONTROLLER_ENABLE, 0x34) separate from the
	 * per-list RUN_STOP bits. With it clear the host accepts no transfer at all: the door bell
	 * keeps its bit set forever and the summary never grows a completion, which is exactly the
	 * DBR 0x1 / IS without bit 0 signature this build shows. LK leaves it set on the boot path
	 * that starts Android, so read it rather than assume, and put it back if it is clear.
	 */

	/*
	 * Re-read the list base now that the controller is clocked.
	 *
	 * The base captured back at ufs_hcs_where[0] was read with HCE = 0 -- the photo of build 48
	 * says so outright: `hce: init 0x0 set 0x1 at ring 0x1`. While HCE is clear this controller
	 * is not clocked, and a value read out of an unclocked block cannot be assumed to still hold
	 * once the block comes alive. Linux never faces the question because it is the one that
	 * writes the base: ufshcd_make_hba_operational() runs AFTER ufshcd_hba_start(), i.e. with
	 * HCE already set, and only then writes UTRLBA/UTRLBAU and sets UTRLRSR.
	 *
	 * So take the base again, from the live controller, and use that one. If it differs from what
	 * was read cold, the cold value is what we have been pointing descriptors at for a dozen
	 * builds -- a plausible reason every door bell rang into nothing.
	 */
	ufs_lk_list_lo = ufs_rd(UFS_UTRLBA);
	ufs_lk_list_hi = ufs_rd(UFS_UTRLBAU);
	ufs_hcs_where[2] = "after clr"; ufs_hcs_trace[2] = ufs_rd(UFS_HCS);
	for (uint32_t n = 0; n < 10000u; n++) {
		if ((ufs_rd(UFS_UTRLDBR) & 1u) == 0) { break; }
	}
	ufs_list_lo = ufs_lk_list_lo;
	ufs_list_hi = ufs_lk_list_hi;
	ufs_list_is_lk = 1;
	ufs_moved_lo = ufs_list_lo;
	ufs_moved_hi = ufs_list_hi;
	for (uint32_t i = 0; i < UFS_SLOTS; i++) {
		uint64_t base = ((uint64_t)ufs_list_hi << 32) | ufs_list_lo;
		ufs_slots[i].utrd = (volatile uint32_t *)(uintptr_t)(base + i * 32);
	}
	/*
	 * NOTHING IS WRITTEN TO IS HERE, and this line is the one that cost the most.
	 *
	 * Build 39 wrote the whole 0x7ff clearable mask -- the door bell stopped clearing. Build 51
	 * narrowed it to bit 0 alone and the door bell still stopped clearing, so the width was
	 * never the point. Build 54 finally measured what the write does to the controller: its
	 * screen reads `hcs 0x501` at ring time, against `0x10f` right after link startup. Those
	 * three vanished bits are UTP_TRANSFER_REQ_LIST_READY, UTP_TASK_REQ_LIST_READY and
	 * UIC_COMMAND_READY. The controller had just told us both of its lists and its UIC channel
	 * were operational, and one write to the status register took all three away. With the
	 * request list no longer READY, UTRLRSR can never latch (`aft 0x0`) and the door bell we so
	 * carefully latch is read by nobody.
	 *
	 * So the leftover completion bit stays where it is. The wait loop tests for the bit
	 * ARRIVING (it was clear at the ring, set now), which is immune to leftovers -- a stale bit
	 * cannot impersonate a new completion. Nothing here needs it cleared.
	 */
	ufs_hcs_where[3] = "after is"; ufs_hcs_trace[3] = ufs_rd(UFS_HCS);
	ufs_is_before = ufs_rd(UFS_IS);
	ufs_door_before = ufs_rd(UFS_UTRLDBR);
	ufs_door_after_clear = ufs_rd(UFS_UTRLDBR);

	/*
	 * Start the list. LK does not always hand it over running: on some boots it leaves
	 * UTRLRSR bit 0 clear after its own UFS work, and a stopped list never looks at the door
	 * bell -- every request then sits there until the poll budget runs out, which is exactly
	 * the "r:bell" timeouts this build showed. Setting the bit is the normal operating state
	 * and is what LK itself does before reading from flash.
	 */
	/*
	 * Leave UTRLRSR alone.
	 *
	 * LK hands the list over running (the boot line above reads run 0x1), and every attempt to
	 * "make sure" by writing 1 to it here made things worse: the write reads back 1 once and 0
	 * on the next read, and from then on the door bell never clears -- the `r:bell` cascades in
	 * builds 30 to 33. Build 28, which did not write this register at all, cleared the door bell
	 * on the first try. The register is not sticky on this controller; the only correct move is
	 * to not touch it and to use the list exactly as it arrives.
	 */
	ufs_run_after_stop = ufs_rd(UFS_UTRLRSR);
	ufs_hcs_where[4] = "after rsr"; ufs_hcs_trace[4] = ufs_rd(UFS_HCS);
	ufs_hcs_where[5] = "branch end"; ufs_hcs_trace[5] = ufs_rd(UFS_HCS);

	/*
	 * Did LINK_STARTUP disturb the request list? Record it, but do not repair it here.
	 *
	 * The repair belongs in ufs_run(), immediately before the ring, because that is the only
	 * moment where the answer matters and the only place the value has been observed to change:
	 * build 50 printed `rsr 0x1 ring 0x0` -- running right after link startup, stopped by the
	 * time the first request was about to be rung, with the UTRLCLR write the only thing in
	 * between. Fixing the list at ring time covers both causes (link startup and any clear) with
	 * one readout instead of two guesses.
	 */
	ufs_post_link_rsr = ufs_rd(UFS_UTRLRSR);
	ufs_post_link_dbr = ufs_rd(UFS_UTRLDBR);
	ufs_hcs_where[8] = "post-rsr"; ufs_hcs_trace[8] = ufs_rd(UFS_HCS);
#else
	ufs_64as = (cap & UFS_CAP_64AS) != 0;
	/*
	 * ORDER IS THE WHOLE POINT HERE, and build 63 had it backwards.
	 *
	 * JESD223 lets host software write UTRLCLR only while the transfer list is stopped
	 * (UTRLRSR bit 0 clear). Build 63 wrote the clear register first and stopped the list
	 * afterwards -- it cleared a list the controller still owned. The controller did not ignore
	 * it: the next reads show HCS 0x501, whose 0x500 is the power-mode-change status field
	 * (PWR_FATAL_ERROR), and neither list reports READY any more. From that state UTRLRSR cannot
	 * be set at all (`tail rsr 0x0 wrote 1`, `aft 0x0`), so no request can ever run. That is
	 * exactly what builds 54 through 63 were chasing.
	 *
	 * So: stop the list, WAIT until it says it stopped, and only then clear. The clear value
	 * stays zero, because UTRLCLR is bit-significant with 0 meaning "this slot is cleared", so
	 * writing zero clears every slot -- that is ufshcd_utrl_clear(~mask) with mask all ones.
	 */
	ufs_wr(UFS_UTRLRSR, 0);
	for (uint32_t n = 0; n < UFS_HALT_POLLS; n++) {
		if ((ufs_rd(UFS_UTRLRSR) & 1u) == 0) {
			break;
		}
	}
	ufs_run_after_stop = ufs_rd(UFS_UTRLRSR);
	if ((ufs_run_after_stop & 1u) != 0) {
		/* still running: writing the clear register now is what broke build 63 */
		ufs_failed = 1;
		return ufs_fail("list did not stop before clear", ufs_run_after_stop);
	}
	ufs_hcs_after_stop = ufs_rd(UFS_HCS);
	ufs_wr(UFS_UTRLCLR, 0u);
	__asm__ volatile("dsb sy" ::: "memory");
	ufs_hcs_after_clr = ufs_rd(UFS_HCS);
	/*
	 * The task-management list has its own run/stop register, and it is not a bystander: the
	 * controller only reports HCS.UTMRLRDY while that list runs, and a controller without all
	 * three READY bits refuses to latch UTRLRSR. Linux always writes BOTH registers in
	 * ufshcd_enable_run_stop_reg() -- task list first, transfer list second. Builds 63 and 64
	 * only ever wrote the transfer one, which is why `clr 0x10f` (both lists ready) turned into
	 * `tail rsr 0x0 wrote 1` (refused) one write later.
	 *
	 * So restore whatever the task list was doing, and put it into run state explicitly, before
	 * asking the transfer list to run. Both values are recorded so the next photograph can tell a
	 * refused latch apart from a list that was never restarted.
	 */
	ufs_utmrl_rsr_at_stop = ufs_rd(UFS_UTMRLRSR);
	ufs_wr(UFS_UTMRLRSR, 1u);
	__asm__ volatile("dsb sy" ::: "memory");
	ufs_utmrl_rsr_after = ufs_rd(UFS_UTMRLRSR);

	/*
	 * Move the base, then PROVE the controller took it -- both halves.
	 *
	 * ios-sweet's shim/ufs.c:215-226 does exactly this and refuses to ring at all if the
	 * readback disagrees: "Never ring a doorbell unless the host really uses our list:
	 * otherwise it would execute whatever ABL left in its own slot 0 (maybe a write)."
	 * The kernel driver in this same tree checks it the same way (hakios.ufshc.c:926-935):
	 * ok = lo == expected && hi == expected && (rsr & 1).
	 *
	 * The earlier version of this loop only compared the low half. A high half that never
	 * took would leave the controller fetching descriptors from 0x00000000_60100000 while we
	 * filled 0x60100000 and rang -- every request then ends as OCS_PEER_COMM_FAILURE with the
	 * door bell never latching, which is precisely what build 60/61 measured.
	 */
	ufs_moved_lo = 0;
	ufs_moved_hi = 0;
	for (uint32_t t = 0; t < UFS_BASE_TRIES; t++) {
		ufs_wr(UFS_UTRLBA, (uint32_t)(uint64_t)UFS_AREA_PA);
		if (ufs_64as) {
			ufs_wr(UFS_UTRLBAU, (uint32_t)(UFS_AREA_PA >> 32));
		}
		for (uint32_t n = 0; n < UFS_SETTLE_N; n++) {
			__asm__ volatile("dsb sy" ::: "memory");
		}
		ufs_moved_lo = ufs_rd(UFS_UTRLBA);
		ufs_moved_hi = ufs_64as ? ufs_rd(UFS_UTRLBAU) : 0;
		if (ufs_moved_lo == (uint32_t)(uint64_t)UFS_AREA_PA && ufs_moved_hi == 0u) {
			ufs_base_tries_used = t + 1u;
			break;
		}
		ufs_base_tries_used = t + 1u;
	}
	ufs_base_hi_wanted = 0u;
	if (ufs_moved_lo != (uint32_t)(uint64_t)UFS_AREA_PA) {
		ufs_failed = 1;
		return ufs_fail("list base did not take", ufs_moved_lo);
	}
	if (ufs_moved_hi != 0u) {
		ufs_failed = 1;
		return ufs_fail("list base hi did not take", ufs_moved_hi);
	}
#endif
	ufs_zero((void *)UFS_AREA_LIST, UFS_SLOTS * 32);
	__asm__ volatile("dsb sy" ::: "memory");
	/*
	 * NOTHING IS WRITTEN TO IS -- and this is the site that mattered, not the two before it.
	 *
	 * This line sits in the common tail, after the #endif, so both branches run it. Build 54
	 * removed the write from ufs_run(), build 55 removed the one in the LK branch, and the
	 * screen came back identical both times (`hcs 0x501`, `aft 0x0`, `ex 4`) because the killer
	 * was here all along.
	 *
	 * What it does, measured: right after link startup HCS reads 0x10f -- both request lists
	 * ready, UIC ready. One write of the 0x7ff clearable mask takes all three away (0x501).
	 * A controller with no READY request list will not latch UTRLRSR (hence `aft 0x0`), and a
	 * list that is not running never looks at the door bell -- which is why every request since
	 * build 39 has timed out with the bell still standing at 0x1.
	 *
	 * Clearing the summary mask buys nothing here. The wait loop now tests for the completion
	 * bit ARRIVING rather than being set, so leftovers cannot impersonate a fresh completion,
	 * and nothing else in this loader consumes the summary.
	 */
	ufs_is_before = ufs_rd(UFS_IS);
	ufs_door_before = ufs_rd(UFS_UTRLDBR);
	ufs_door_after_clear = ufs_rd(UFS_UTRLDBR);
	ufs_list_lo = ufs_moved_lo;
	ufs_list_hi = ufs_moved_hi;
	/* remember whose list it is, before the assignment below forgets */
	const int ufs_list_inherited = ufs_list_is_lk;
	ufs_list_is_lk = 0;
	for (uint32_t i = 0; i < UFS_SLOTS; i++) {
		uint64_t base = ((uint64_t)ufs_list_hi << 32) | ufs_list_lo;
		ufs_slots[i].utrd = (volatile uint32_t *)(uintptr_t)(base + i * 32);
	}
	/*
	 * Start the list -- but only if it is OUR list, or only if it is actually stopped.
	 *
	 * This write is unconditional in the older code and it is the one that costs the READY bits.
	 * Build 57 measured it: the last sample taken inside the LK branch reads `post-rsr 0x10f`,
	 * i.e. request list ready, task list ready, UIC ready. The very next observation, at the
	 * moment of the first ring, is `hcs 0x501` -- those three bits gone, `aft 0x0`, and the
	 * door bell standing unserviced. Between the two samples the only register write is this
	 * one.
	 *
	 * That matches what builds 30 to 33 saw from the outside ("write UTRLRSR and the bell never
	 * clears again") without knowing why. A run/stop register is not a set-only latch: writing
	 * 1 into a controller that already has its list up restarts the run/stop state machine,
	 * which deasserts the list READY handshake, and a list without READY is one the controller
	 * will not fetch descriptors from.
	 *
	 * Linux never does this -- ufshcd_make_hba_operational() writes the base and then sets
	 * UTRLRSR exactly once, right after hba_start(), and never again on a live controller.
	 * LK's list is already running when we inherit it, so the only correct move is to leave the
	 * register alone. When we built the list ourselves (the #else branch) it is stopped by then
	 * and this write is required, so keep it for that case.
	 */
	ufs_tail_rsr_before = ufs_rd(UFS_UTRLRSR);
	ufs_tail_wrote_rsr = 0;
	if (!ufs_list_inherited || (ufs_tail_rsr_before & 1u) == 0) {
		ufs_wr(UFS_UTRLRSR, 1);
		ufs_tail_wrote_rsr = 1;
	}

	if ((ufs_rd(UFS_UTRLRSR) & 1u) == 0) {
		ufs_failed = 1;
		return ufs_fail("list did not start", ufs_rd(UFS_UTRLRSR));
	}
	ufs_hcs_where[9] = "tail-rsr"; ufs_hcs_trace[9] = ufs_rd(UFS_HCS);

	/*
	 * Not calling ufs_uic_hibernate_exit() yet, on purpose. It is written and it is probably
	 * right -- a link left in H8 by LK answers TUR but refuses to move data -- but sending a
	 * UIC command is the one operation here that can hang the controller outright, and the
	 * buffer cache maintenance below is a fault that is already proven by the three different
	 * checksums of three identical reads. One change at a time: if the reads start returning
	 * stable data now, H8 was never the problem; if they still do not, the next build turns the
	 * UIC exit on and this comment goes away.
	 */

	/*
	 * Wake the link before the first request, not after a failure. LK hands the controller over
	 * in an unknown UniPro power state; if it parked the link in Hibernate, a TUR is still
	 * acknowledged (it needs no data movement) while every fetched command comes back
	 * OCS_FATAL_ERROR -- which is exactly what the descriptor variants report now that the
	 * command_type field is in the position this controller reads it from. One UIC command,
	 * bounded by a counted poll, issued once, before anything else.
	 */
	/*
	 * The link is not operational, and the two registers say so before any request is made.
	 *
	 * HCS reads 0x10df: bit 4 (HOST_ERROR_INDICATOR) is set, which the spec defines as "the host
	 * controller has a fatal error"; UECPA reads 0x80000010, i.e. the error flag plus bit 4
	 * (GENERIC_ERROR), which is the LINERESET indication. A link that has just been reset is not
	 * a link that can carry a command, and that is consistent with every fetched descriptor
	 * coming back OCS_FATAL_ERROR with no response UPIU behind it.
	 *
	 * The kernel driver treats exactly this state by redoing link startup and then reading UECPA
	 * once, "because LINERESET has happened during LINK_STARTUP" (ufshcd.c, ufshcd_link_startup).
	 * Do the same, in that order: link startup over UIC, then read UECPA to retire the latched
	 * error, then look at HCS again. ufs_link_state records what came back so one photograph
	 * answers whether this controller will even carry a UIC command.
	 */
	/*
	 * Wake the link out of Hibernate before the first request.
	 *
	 * This is the one call that has been written since the beginning and never made, and build
	 * 60 is the measurement that says it is the missing step. For the first time the transport
	 * reported a real, specific failure instead of a fake success or an invalid descriptor:
	 *
	 *    1st hcs 0x10f          controller fine, both lists ready, UIC channel ready
	 *    1st rsr 0x1  dbr 0x0 -> 0x0   list running, door bell accepted
	 *    1st is 0x0 -> 0x14     UIC_POWER_MODE | UIC_ERROR
	 *    1st ocs 0x5            OCS_PEER_COMM_FAILURE
	 *
	 * PEER_COMM_FAILURE means the host committed the UPIU and the device never answered -- the
	 * host side is healthy, the peer is asleep. A UniPro link parked in Hibernate (H8) accepts
	 * nothing but a HIBER_EXIT, which is exactly the state LK can leave behind, and the comment
	 * that has been sitting above this call since the first version says so: "a link left in H8
	 * by LK answers TUR but refuses to move data".
	 *
	 * Sent once, before anything else, bounded by a counted poll. ufs_uic_hibernate_exit() only
	 * touches ARG1/2/3 and UIC_COMMAND, never the list registers, and it is a no-op when the
	 * controller has no UIC channel or when the link never went into H8 at all.
	 */
#ifndef UFS_NO_HIBER_EXIT
	ufs_hiber_exit_ok = ufs_uic_hibernate_exit();
#endif

	ufs_ready = 1;
	return 1;
}

/* TEST UNIT READY: does the unit answer at all, before we ask it for data. */
static int __attribute__((unused)) ufs_tur(uint8_t lun)
{
	uint8_t upiu[32];
	uint32_t ocs;

	if (!ufs_init()) {
		return 0;
	}
	ufs_zero(upiu, sizeof(upiu));
	upiu[0] = UPIU_COMMAND;
	upiu[2] = lun;
	upiu[3] = UFS_SLOT0_TAG;
	upiu[16] = 0x00;

	if (!ufs_place(&ufs_slots[0], upiu, UTRD_CT_UFS | UTRD_DD_NONE, 0)) {
		return 0;
	}
	if (!ufs_run(&ocs)) {
		ufs_failed = 1;
		return 0;
	}
	for (uint32_t b = 0; b < 16u; b++) {
		ufs_tur_resp[b] = *(volatile const uint8_t *)((uint8_t *)ufs_slots[0].ucd + UCD_RESPONSE + b);
	}
	ufs_tur_resp_seen = 1;
	if (ocs != OCS_SUCCESS) {
		ufs_failed = 1;
		return ufs_fail("TEST UNIT READY", ocs);
	}
	if (!ufs_reply_ok(&ufs_slots[0])) {
		ufs_failed = 1;
		return ufs_fail("TEST UNIT READY status", ufs_last_status7);
	}
	return 1;
}

/* NOP OUT: Linux uses this (ufshcd_verify_dev_init) to find out whether the
 * device-side UTP layer answers at all -- "If the UTP layer at the device side
 * is not initialized, it may not respond with NOP IN UPIU within timeout".
 * A SCSI TUR only tells us the transfer never completed; a NOP OUT tells us
 * whether the peer is alive, and its answer (NOP IN, 0x20) is unmistakable.
 * Linux sends it as a device-management command: command_type = UFS_STORAGE,
 * no data stage, interrupt bit set, ucd response buffer zeroed first. */
/*
 * Wake the device itself up.
 *
 * Everything else has been ruled out by measurement: the list runs (tail rsr 0x1, 1st rsr 0x1),
 * the controller is healthy when the first request is rung (1st hcs 0x10f), the link reports
 * LINK_STARTUP success (res 0, UECPA clean), yet neither a NOP OUT nor a TUR ever comes back --
 * 1st dbr 0x0 -> 0x0, the door bell is never consumed. A controller that is awake, a list that
 * runs and a peer that never answers add up to a peer that was never switched on.
 *
 * The vendor driver's ufs_mtk_mphy_power_on() (ufs-mediatek.c:507) enables the PHY analogue rail
 * VA09 and then tells the firmware through SMC; ufs_mtk_device_reset() (:1631) drives the device's
 * own RST_n low/high with a 10 us pulse and a 15 ms settle. This loader has never sent any of the
 * three. They are all SMC requests, so they do not disturb the controller state the list lives in.
 */
static void __attribute__((unused)) ufs_device_wake(void)
{
	ufs_device_woke = 1;
	/* VA09 first, then 200 us to stabilise, exactly as the vendor driver does. */
	struct ufs_smc_ret r = ufs_mtk_va09_pwr_ctrl(1);
	ufs_sip_va09_ok = (uint32_t)r.a0;
	for (volatile uint32_t n = 0; n < 20000u; n++) { }

	/* The device's own supply. ufs_version 0 means "let the firmware decide". */
	r = ufs_mtk_device_pwr_ctrl(1, 0);
	ufs_sip_devpwr_ok = (uint32_t)r.a0;

	/* RST_n is active low; the spec asks for at least 1 us, the vendor keeps 10 us. */
	r = ufs_mtk_device_reset_ctrl(0);
	ufs_sip_rst_lo = (uint32_t)r.a0;
	for (volatile uint32_t n = 0; n < 2000u; n++) { }
	r = ufs_mtk_device_reset_ctrl(1);
	ufs_sip_rst_hi = (uint32_t)r.a0;

	/* "Some devices may need time to respond to rst_n" -- vendor waits 10-15 ms. */
	for (volatile uint32_t n = 0; n < 200000u; n++) { }
}

static int __attribute__((unused)) ufs_nop(void)
{
	uint8_t upiu[32];
	uint32_t ocs;

	if (!ufs_init()) {
		return 0;
	}
	ufs_zero(upiu, sizeof(upiu));
	upiu[0] = UPIU_TRANSACTION_NOP_OUT;
	upiu[3] = UFS_SLOT0_TAG;

	if (!ufs_place(&ufs_slots[0], upiu, UTRD_CT_UFS | UTRD_DD_NONE | UTRD_INT, 0)) {
		return 0;
	}
	if (!ufs_run(&ocs)) {
		ufs_nop_ocs = 0xffffffffu;
		return 0;
	}
	ufs_nop_ocs = ocs;
	ufs_nop_resp0 = *(volatile const uint8_t *)((uint8_t *)ufs_slots[0].ucd + UCD_RESPONSE);
	ufs_nop_resp6 = *(volatile const uint8_t *)((uint8_t *)ufs_slots[0].ucd + UCD_RESPONSE + 6);
	if (ocs != OCS_SUCCESS) {
		return 0;
	}
	return 1;
}

/*
 * QUERY REQUEST: the step the device needs after it has been power cycled.
 *
 * Measured (build 71): NOP OUT answers (NOP IN, 0x20), a SCSI TUR completes with OCS_SUCCESS and
 * a RESPONSE UPIU (0x21), and yet the UPIU's `response` byte is 1 -- TARGET FAILURE. So the
 * transport works and the device refuses to execute SCSI commands. That is precisely what a UFS
 * device does before its own initialisation is finished.
 *
 * Linux handles it in ufshcd_complete_dev_init() (core/ufshcd.c:4839-4876): SET_FLAG fDeviceInit,
 * then poll READ_FLAG fDeviceInit until the device clears it, and only then issue any SCSI
 * command. Nothing else in the bring-up sequence substitutes for it, which is why every read so
 * far came back empty: gpt_detect() got no data because the very first READ(10) was refused.
 *
 * UPIU layout (uapi/scsi/scsi_bsg_ufs.h, struct utp_upiu_header + struct utp_upiu_query):
 *   [0]  transaction code 0x16      [5]  query function: 0x01 read / 0x81 write
 *   [2]  lun                        [6]  response  (device writes)   [7] status (device writes)
 *   [3]  task tag                   [12] opcode   [13] idn   [14] index   [15] selector
 *   [18..19] length (BE)            [20..23] value (BE)
 */
#define UFS_QUERY_LUN 0u

static int __attribute__((unused)) ufs_query_flag(uint8_t opcode, uint8_t idn, uint32_t *value)
{
	uint8_t upiu[32];
	uint32_t ocs;
	volatile const uint8_t *r;

	if (!ufs_init()) {
		return 0;
	}
	ufs_zero(upiu, sizeof(upiu));
	upiu[0] = UPIU_TRANSACTION_QUERY_REQ;
	upiu[2] = UFS_QUERY_LUN;
	upiu[3] = UFS_SLOT0_TAG;
	upiu[5] = (opcode == UPIU_QUERY_OPCODE_READ_FLAG) ? UPIU_QUERY_FUNC_READ
							 : UPIU_QUERY_FUNC_WRITE;
	upiu[12] = opcode;
	upiu[13] = idn;
	upiu[14] = 0;                   /* index: which unit, 0 for device-wide flags */
	upiu[15] = 0;                   /* selector */

	if (!ufs_place(&ufs_slots[0], upiu, UTRD_CT_UFS | UTRD_DD_NONE | UTRD_INT, 0)) {
		return 0;
	}
	if (!ufs_run(&ocs)) {
		ufs_query_ocs = 0xffffffffu;
		return 0;
	}
	ufs_query_ocs = ocs;
	r = (volatile const uint8_t *)ufs_slots[0].ucd + UCD_RESPONSE;
	ufs_query_resp = r[6];
	ufs_query_rsp0 = r[0];
	if (value) {
		*value = ((uint32_t)r[20] << 24) | ((uint32_t)r[21] << 16) |
			 ((uint32_t)r[22] << 8) | (uint32_t)r[23];
	}
	if (ocs != OCS_SUCCESS) {
		return 0;
	}
	return r[6] == 0;
}

/*
 * The whole handshake, once: set fDeviceInit, then wait for the device to clear it.
 * The device clears the flag when it has finished its own start-up; a device that never clears
 * it is one that cannot serve reads, and saying so on screen is better than a silent empty disk.
 */
#ifndef UFS_NO_FDEVICEINIT
static int __attribute__((unused)) ufs_fdevice_init(void)
{
	uint32_t tries;

	if (!ufs_query_flag(UPIU_QUERY_OPCODE_SET_FLAG, QUERY_FLAG_IDN_FDEVICEINIT, 0)) {
		ufs_fdevinit_stage = 1;         /* the set itself failed */
		return 0;
	}
	ufs_fdevinit_stage = 2;
	for (tries = 0; tries < UFS_FDEVINIT_TRIES; tries++) {
		uint32_t flag = 0;

		if (!ufs_query_flag(UPIU_QUERY_OPCODE_READ_FLAG, QUERY_FLAG_IDN_FDEVICEINIT, &flag)) {
			ufs_fdevinit_stage = 3;     /* the read failed */
			return 0;
		}
		ufs_fdevinit_flag = flag;
		if (flag == 0) {
			ufs_fdevinit_stage = 4;     /* done, the device cleared it */
			ufs_fdevinit_tries = tries + 1u;
			return 1;
		}
		for (volatile uint32_t n = 0; n < 20000u; n++) { }
	}
	ufs_fdevinit_tries = tries;
	ufs_fdevinit_stage = 5;                 /* never cleared */
	return 0;
}
#endif

/*
 * Find the LUN that answers. A unit that is not there reports INVALID LUN (status 0x7) in the
 * response UPIU, not a transport error, so probing is cheap and safe: every reply is a real one.
 * The kernel driver takes its LUNs from the device tree; here there is no device tree, so the
 * only honest source is the device itself.
 */
/* ufs_lun is declared above, before the probe that also needs it. */
static uint32_t ufs_lun_status[UFS_LUN_MAX];    /* what each unit said, for the report */
static uint32_t ufs_lun_ocs[UFS_LUN_MAX];       /* the overall status of the last try */
static const char *ufs_lun_why[UFS_LUN_MAX];    /* and what the transport said, if it failed */
/* The raw descriptor and response of the last request, so a report can show what really came back. */
static uint32_t ufs_last_utrd[8];
static uint8_t ufs_last_resp[32];
static uint32_t ufs_place_probe[4];     /* the descriptor as written, before the controller runs */
static uint32_t ufs_place_probe_addr;   /* where the controller says the list is at that moment */
static uint8_t ufs_cap_raw[16];         /* READ CAPACITY(10)'s data area, as it came back */
static uint8_t ufs_cap_resp[16];        /* its response UPIU: byte 8 is status, and it moved data */
static uint8_t ufs_cap_cmd[16];         /* the command UPIU, to prove it is still ours */
static uint8_t ufs_cap_at128[16];       /* ucd+128: where a response lands if the offset is bytes */
static uint8_t ufs_cap_at1024[16];      /* ucd+1024: where it lands if the PRD offset is bytes */
static uint32_t ufs_cap_utrd0, ufs_cap_utrd7;
static uint32_t ufs_cap_stage;          /* how far ufs_capacity() got before it gave up */
static uint32_t ufs_cap_last, ufs_cap_lbs;
static int ufs_lun_found;                       /* a unit answered 0 */

/*
 * Work out where the controller expects the command descriptor's fields.
 *
 * One unit-agnostic TUR per candidate, and the tell is the response UPIU: a real answer starts
 * with 0x21 (UPIU_RESPONSE) at the response offset we asked for. If nothing lands there, that
 * placement is wrong and the next one is tried. Nothing is written to the device, so trying all
 * four is free.
 */
/*
 * Where does this controller read the command descriptor address from?
 *
 * JESD223 puts it in words 3:4 and the OpenSSD/Linux layout puts it in word 2; the descriptor LK
 * left behind is the only authority, and photographing it proved unreliable. So ask the hardware
 * instead: put the same UCD address in one word position at a time and submit a TEST UNIT READY.
 * A placement the controller understands makes it fetch our command, execute it and write a
 * response UPIU back into the UCD. A placement it does not understand leaves the UCD untouched.
 *
 * TUR writes nothing to the device, so eight candidates cost nothing and cannot damage anything.
 */
static uint32_t ufs_pw_ocs0[8];         /* overall status as this controller puts it in DW0 */
static uint32_t ufs_pw_ocs2[8];         /* and in DW2, if it uses that instead */
static uint32_t ufs_pw_resp[8];         /* first byte of the response UPIU where we asked for it */
static uint32_t ufs_pw_at[8];           /* offset in the UCD where a 0x21 turned up, or 0xffff */
static int ufs_pw_best = -1;            /* the word position that produced a real answer */

static int __attribute__((unused)) ufs_probe_addr_word(void)
{
	ufs_pw_best = -1;
	for (int w = 1; w < 8; w++) {
		uint8_t upiu[32];
		uint32_t ocs;

		ufs_failed = 0;
		if (!ufs_init()) {
			return 0;
		}
		ufs_zero((void *)ufs_slots[0].utrd, 32);         /* zero the descriptor first */
		ufs_zero(upiu, sizeof(upiu));
		upiu[0] = UPIU_COMMAND;
		upiu[2] = ufs_lun;
		upiu[3] = UFS_SLOT0_TAG;
		upiu[16] = 0x00;
		ufs_put((void *)ufs_slots[0].ucd + UCD_UPIU, upiu, 32);
		ufs_zero((void *)ufs_slots[0].ucd + UCD_RESPONSE, 64);

		/* command type and direction only; the address goes in exactly one place */
		ufs_slots[0].utrd[0] = UTRD_CT_UFS | UTRD_DD_NONE;
		ufs_slots[0].utrd[w] = (uint32_t)ufs_slots[0].ucd_pa;
		if (w < 7) {
			ufs_slots[0].utrd[w + 1] = (uint32_t)(ufs_slots[0].ucd_pa >> 32);
		}
		/* the response and PRD fields, wherever they are, as dword offsets from the UCD */
		ufs_slots[0].utrd[5] = (UCD_RESP_LEN << 16) | (UCD_RESPONSE / 4);   /* dwords both sides */
		ufs_slots[0].utrd[6] = (0u << 16) | (UCD_PRDT / 4);
		__asm__ volatile("dsb sy" ::: "memory");

		ufs_pw_ocs0[w] = 0;
		ufs_pw_ocs2[w] = 0;
		ufs_pw_resp[w] = 0;
		ufs_pw_at[w] = 0xffffu;
		if (!ufs_run(&ocs)) {
			ufs_pw_ocs0[w] = 0xffffffffu;
			continue;
		}
		ufs_pw_ocs0[w] = ufs_slots[0].utrd[0] >> 24;
		ufs_pw_ocs2[w] = ufs_slots[0].utrd[2];
		ufs_pw_resp[w] =
			*(volatile const uint8_t *)((uint8_t *)ufs_slots[0].ucd + UCD_RESPONSE);
		{
			volatile const uint8_t *u = (volatile const uint8_t *)ufs_slots[0].ucd;
			for (uint32_t at = 0; at + 1 < 512u; at += 4) {
				if (u[at] == UPIU_RESPONSE) { ufs_pw_at[w] = at; break; }
			}
		}
		if (ufs_pw_resp[w] == UPIU_RESPONSE && ufs_pw_best < 0) {
			ufs_pw_best = w;
		}
	}
	return ufs_pw_best > 0;
}

static int __attribute__((unused)) ufs_pick_layout(void)
{
	for (int which = 0; which < 4; which++) {
		uint8_t upiu[32];
		uint32_t ocs;

		ufs_layout = which;
		ufs_failed = 0;
		if (!ufs_init()) {
			return 0;
		}
		ufs_zero(upiu, sizeof(upiu));
		upiu[0] = UPIU_COMMAND;
		upiu[2] = ufs_lun;
		upiu[3] = UFS_SLOT0_TAG;
		upiu[16] = 0x00;
		if (!ufs_place(&ufs_slots[0], upiu, UTRD_CT_UFS | UTRD_DD_NONE, 0)) {
			return 0;
		}
		if (!ufs_run(&ocs)) {
			ufs_layout_tried[which] = 0xffffffffu;
			continue;
		}
		ufs_layout_tried[which] =
			*(volatile const uint8_t *)((uint8_t *)ufs_slots[0].ucd + UCD_RESPONSE);
		if (ufs_layout_tried[which] == UPIU_RESPONSE) {
			ufs_layout = which;
			return 1;
		}
	}
	return 0;       /* none of them answered with a response UPIU */
}

/*
 * Unit 0 is the one this board answers on -- "TEST UNIT READY on LUN 0: answered", and a scan that
 * reported 0x0 for all eight units before the descriptor layout was corrected. Walking the rest
 * anyway cost a hang: with the layout now fixed, unit 0 completed four attempts and unit 1 stopped
 * the doorbell completing at all, which left the screen on "lun 0x1 / r:bell" with no way forward.
 * Nothing needs the scan, so it does not run: try unit 0, and only unit 0.
 */

/*
 * Four descriptor variants, one TEST UNIT READY each, in one boot. The controller fetches the
 * descriptor (ocs lands in DW2) but always reports OCS_FATAL_ERROR and writes no response UPIU,
 * so something in the descriptor it will not take is still unknown. Rather than one variable per
 * flash on a phone that needs a battery pull between builds, try the four arrangements the two
 * candidate layouts give and report the status word of each:
 *
 *   0  command_type DW0[8:11],  response length 32 dwords   (what this file has now)
 *   1  command_type DW0[8:11],  response length 128 dwords  (length as Linux counts it)
 *   2  command_type DW0[31:28], response length 128 dwords  (Linux header exactly)
 *   3  command_type DW0[31:28], response length 32 dwords
 */
static uint32_t ufs_ab_ocs[4];
static uint32_t ufs_ab_dw2[4];
static uint32_t ufs_err_phy, ufs_err_dl, ufs_err_net, ufs_err_tr, ufs_err_dme;
static uint32_t ufs_fail_is, ufs_fail_hcs, ufs_fail_cnr, ufs_fail_resp0;

static void ufs_capture_failure(void)
{
	ufs_err_phy = ufs_rd(UFS_UIC_ERR_PHY);
	ufs_err_dl  = ufs_rd(UFS_UIC_ERR_DL);
	ufs_err_net = ufs_rd(UFS_UIC_ERR_NET);
	ufs_err_tr  = ufs_rd(UFS_UIC_ERR_TRANSPORT);
	ufs_err_dme = ufs_rd(UFS_UIC_ERR_DME);
	ufs_fail_is = ufs_rd(UFS_IS);
	ufs_fail_hcs = ufs_rd(UFS_HCS);
	ufs_fail_cnr = ufs_rd(UFS_UTLRCNR);
	ufs_fail_resp0 = *(volatile const uint32_t *)((uint8_t *)ufs_slots[0].ucd + UCD_RESPONSE);
}

static void ufs_probe_variants(void)
{
	for (int v = 0; v < 4; v++) {
		uint8_t upiu[32];
		uint32_t ocs = 0xffffffffu;
		uint32_t ct = (v < 2) ? 0x00000100u : (1u << 28);
		uint32_t rl = (v == 1 || v == 2) ? 128u : 32u;

		ufs_failed = 0;
		ufs_ab_ocs[v] = 0xffffffffu;
		ufs_ab_dw2[v] = 0xffffffffu;
		if (!ufs_init()) {
			continue;
		}
		ufs_zero(upiu, sizeof(upiu));
		upiu[0] = UPIU_COMMAND;
		upiu[2] = ufs_lun;
		upiu[3] = UFS_SLOT0_TAG;
		upiu[16] = 0x00;
		if (!ufs_place(&ufs_slots[0], upiu, ct | UTRD_DD_NONE, 0)) {
			continue;
		}
		ufs_slots[0].utrd[6] = (UCD_RESPONSE / 4) << 16 | rl;
		ufs_cache_sync(ufs_slots[0].utrd, 32);
		if (!ufs_run(&ocs)) {
			ufs_ab_dw2[v] = ufs_slots[0].utrd[2];
			if (v == 0) { ufs_capture_failure(); }
			continue;
		}
		ufs_ab_ocs[v] = ocs;
		ufs_ab_dw2[v] = ufs_slots[0].utrd[2];
		if (v == 0) { ufs_capture_failure(); }
	}
}

static int __attribute__((unused)) ufs_pick_lun(void)
{
	if (ufs_lun_found) {
		return 1;
	}
	for (uint32_t l = 0; l < UFS_LUN_MAX; l++) {
		ufs_lun_status[l] = 0xffu;      /* nothing heard yet */
		/*
		 * Retry: the first commands after taking the list over fail on this controller while
		 * LK's leftover doorbell state drains, and then answer normally. Retrying is cheaper
		 * than explaining it, and the report below shows how many tries each unit took.
		 */
		for (uint32_t t = 0; t < UFS_LUN_TRIES; t++) {
			uint8_t upiu[32];
			uint32_t ocs;

			UFS_TRACE("lun ");
			UFS_TRACE_HEX(l);
			UFS_TRACE("\n");
			ufs_failed = 0;         /* each attempt gets its own chance */
			if (!ufs_init()) {
				return 0;
			}
			ufs_zero(upiu, sizeof(upiu));
			upiu[0] = UPIU_COMMAND;
			upiu[2] = (uint8_t)l;
			upiu[3] = UFS_SLOT0_TAG;
			upiu[16] = 0x00;
			if (!ufs_place(&ufs_slots[0], upiu, UTRD_CT_UFS | UTRD_DD_NONE, 0)) {
				return 0;
			}
			/* what the descriptor looks like right after we wrote it */
			ufs_place_probe[0] = ufs_slots[0].utrd[0];
			ufs_place_probe[1] = ufs_slots[0].utrd[4];
			ufs_place_probe[2] = ufs_slots[0].utrd[7];
			ufs_place_probe[3] = *(volatile uint32_t *)((uint8_t *)ufs_slots[0].ucd + UCD_UPIU);
			ufs_place_probe_addr = ufs_rd(UFS_UTRLBA);
			{
				int ran = ufs_run(&ocs);

				for (uint32_t i = 0; i < 8; i++) { ufs_last_utrd[i] = ufs_slots[0].utrd[i]; }
				for (uint32_t i = 0; i < 16; i++) {
					ufs_last_resp[i] = *(volatile const uint8_t *)((uint8_t *)ufs_slots[0].ucd + UCD_RESPONSE + i);
				}
				if (!ran) {
					ufs_lun_status[l] = 0xfeu;
					ufs_lun_ocs[l] = 0u;
					ufs_lun_why[l] = ufs_last.what;
					continue;
				}
			}
			if (0) {
				ufs_lun_ocs[l] = 0u;
				ufs_lun_why[l] = ufs_last.what;
				continue;
			}
			if (ocs != OCS_SUCCESS) {
				ufs_lun_status[l] = 0xfdu;      /* the controller answered: status, not transport */
				ufs_lun_ocs[l] = ocs;
				ufs_lun_why[l] = "ocs";
				continue;
			}
			ufs_lun_ocs[l] = 0u;
			if (ufs_reply_ok(&ufs_slots[0])) {
				ufs_lun_why[l] = "ok";
				ufs_lun_status[l] = 0u;
			} else {
				ufs_lun_why[l] = "scsi";
				ufs_lun_status[l] = (ufs_last_resp6 << 8) | ufs_last_status7;
			}
			break;
		}
		if (ufs_lun_status[l] == 0) {
			ufs_lun = (uint8_t)l;
			ufs_lun_found = 1;
			return 1;
		}
	}
	return 0;
}

/*
 * Read up to UFS_SLOT_BUFFER bytes in one request. The caller loops for anything bigger.
 * READ(10) counts blocks in 16 bits, so 64 blocks of 4 KiB is far inside its range.
 * lba and blocks are in the unit's own logical blocks, which is ufs_lbs, not always 4 KiB.
 */
static int ufs_cmd_once(uint8_t lun, uint32_t lba, uint32_t blocks, uint32_t flags, uint32_t dw0)
{
	uint8_t upiu[32];
	uint32_t ocs, bytes = blocks * ufs_lbs;

	if (!ufs_init()) {
		return 0;
	}

	ufs_zero(upiu, sizeof(upiu));
	upiu[0] = UPIU_COMMAND;
	upiu[1] = (uint8_t)flags;
	upiu[2] = lun;
	upiu[3] = UFS_SLOT0_TAG;
	/* data transfer length, big endian */
	upiu[12] = (uint8_t)(bytes >> 24);
	upiu[13] = (uint8_t)(bytes >> 16);
	upiu[14] = (uint8_t)(bytes >> 8);
	upiu[15] = (uint8_t)bytes;
	/*
	 * The CDB opcode has to follow the direction: READ(10) is 0x28, WRITE(10) is 0x2a. Getting
	 * this wrong sends a write command in the read direction -- the unit then either refuses it or
	 * does something surprising, and either way the data stays where it was.
	 */
	upiu[16] = (uint8_t)((dw0 & UTRD_DD_OUT) ? 0x2a : 0x28);
	upiu[18] = (uint8_t)(lba >> 24);
	upiu[19] = (uint8_t)(lba >> 16);
	upiu[20] = (uint8_t)(lba >> 8);
	upiu[21] = (uint8_t)lba;
	upiu[22] = 0;
	upiu[23] = (uint8_t)(blocks >> 8);
	upiu[24] = (uint8_t)blocks;
	upiu[25] = 0;

	if (!ufs_place(&ufs_slots[0], upiu, dw0, bytes)) {
		return 0;
	}
	if (!ufs_run(&ocs)) {
		ufs_failed = 1;
		return 0;
	}
	if (ocs != OCS_SUCCESS) {
		ufs_failed = 1;
		return ufs_fail((dw0 & UTRD_DD_OUT) ? "WRITE(10)" : "READ(10)", ocs);
	}
	if (!ufs_reply_ok(&ufs_slots[0])) {
		ufs_failed = 1;
		return ufs_fail((dw0 & UTRD_DD_OUT) ? "WRITE(10) status" : "READ(10) status", ufs_last_status7);
	}
	return 1;
}

/*
 * READ CAPACITY(10): the last logical block number and the block length, both big endian in the
 * data area (SPC: 4 bytes, then 4 bytes). This is what tells us whether the unit counts in 512 or
 * 4096: the scatter file's block_size is the transfer unit and says nothing about it, and both
 * answers exist in the field. Ask once, and fall back to 4096 if the answer is not one the spec
 * allows.
 */
static int ufs_capacity(uint8_t lun, uint32_t *lbs_out)
{
	uint8_t upiu[32];
	uint32_t ocs, last, lbs;

	ufs_cap_stage = 1;
	if (!ufs_init()) {
		ufs_cap_stage = 2;
		return 0;
	}
	ufs_cap_stage = 3;
	ufs_zero(upiu, sizeof(upiu));
	upiu[0] = UPIU_COMMAND;
	upiu[1] = UPIU_FLAG_READ;
	upiu[2] = lun;
	upiu[3] = UFS_SLOT0_TAG;
	upiu[12] = 0;
	upiu[13] = 0;
	upiu[14] = 0;
	upiu[15] = 8;                   /* READ CAPACITY(10) returns eight bytes */
	upiu[16] = 0x25;                /* READ CAPACITY(10) */

	ufs_cap_stage = 4;
	if (!ufs_place(&ufs_slots[0], upiu, UTRD_CT_UFS | UTRD_DD_IN, 8)) {
		ufs_cap_stage = 5;
		return 0;
	}
	ufs_cap_stage = 6;
	if (!ufs_run(&ocs)) {
		ufs_cap_stage = 7;
		ufs_failed = 1;
		return 0;
	}
	ufs_cap_stage = 8;
	if (ocs != OCS_SUCCESS) {
		ufs_cap_stage = 9;
		ufs_failed = 1;
		return ufs_fail("READ CAPACITY(10)", ocs);
	}
	ufs_cap_stage = 10;
	if (ufs_response_status(&ufs_slots[0]) != 0) {
		ufs_failed = 1;
		return ufs_fail("READ CAPACITY(10) status", ufs_response_status(&ufs_slots[0]));
	}
	/* device memory: read the two words one at a time, big endian */
	{
		volatile const uint8_t *d = (volatile const uint8_t *)UFS_AREA_BUF;

		last = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
		lbs = ((uint32_t)d[4] << 24) | ((uint32_t)d[5] << 16) | ((uint32_t)d[6] << 8) | d[7];
	}
	/* keep the raw state, so a wrong answer can be explained from one photograph */
	{
		volatile const uint8_t *d = (volatile const uint8_t *)UFS_AREA_BUF;
		volatile const uint8_t *r = (volatile const uint8_t *)ufs_slots[0].ucd + UCD_RESPONSE;
		for (uint32_t i = 0; i < 16; i++) { ufs_cap_raw[i] = d[i]; }
		for (uint32_t i = 0; i < 16; i++) { ufs_cap_resp[i] = r[i]; }
		{
			volatile const uint8_t *c = (volatile const uint8_t *)ufs_slots[0].ucd;
			for (uint32_t i = 0; i < 16; i++) { ufs_cap_cmd[i] = c[i]; }
			for (uint32_t i = 0; i < 16; i++) { ufs_cap_at128[i] = c[128 + i]; }
			for (uint32_t i = 0; i < 16; i++) { ufs_cap_at1024[i] = c[1024 + i]; }
		}
		ufs_cap_utrd0 = ufs_slots[0].utrd[0];
		ufs_cap_utrd7 = ufs_slots[0].utrd[7];
		ufs_cap_last = last;
		ufs_cap_lbs = lbs;
	}
	if (lbs != 512u && lbs != 4096u) {
		return ufs_fail("logical block size is neither 512 nor 4096", lbs);
	}
	ufs_cap_stage |= 0x1000u;               /* the probe answered with a usable size */
	dbg_puts("ufs: unit "); dbg_hex(lun, 2); dbg_puts(": "); dbg_dec((uint64_t)last + 1u);
	dbg_puts(" blocks of "); dbg_dec(lbs); dbg_puts(" bytes\n");
	*lbs_out = lbs;
	return 1;
}

/*
 * ufs_read(lba, blocks, dst): the shape msdc_read has, so gpt_find and load_system_image can use
 * either one -- 512-byte sectors, straight into memory. The unit counts in ufs_lbs, which is not
 * always 512, so a request is first split into whole device blocks and copied through the slot
 * buffer when the alignment does not line up. Reads go through UFS_AREA_BUF and are copied out.
 */
/*
 * The handshake the disk needs before it will serve a single read, done once.
 *
 * Build 73/74 read the GPT and boot_a, but they were built with UFS_TEST, and the only thing
 * that had ever put these two calls on the path was the self test's own body:
 *
 *     sel_fdev_ok = ufs_fdevice_init();
 *     ufs_pick_lun();
 *
 * Build 75 (the first one that reads a real system image) dropped UFS_TEST, so neither call ran
 * and every READ(10) after that came back refused -- the same `response 0x1` build 71 found,
 * which is what a unit says when its own start-up has not been completed. The panel showed it as
 * `found 0  bytes 0  secs 0`: load_system_image() returned before it read anything.
 *
 * So the handshake lives here now, inside the read path, instead of in a test that a production
 * build does not compile in.
 */
static int ufs_io_ready(void)
{
	static int state;               /* 0: not tried, 1: ready, 2: gave up */

	if (state == 1) {
		return 1;
	}
	if (state == 2) {
		return 0;
	}
	if (!ufs_init()) {
		state = 2;
		ufs_io_stage = 1;
		return 0;
	}
	ufs_io_stage = 2;
#ifndef UFS_NO_FDEVICEINIT
	ufs_io_fdev = (uint32_t)ufs_fdevice_init();
	if (!ufs_io_fdev) {
		/*
		 * Not fatal: an already-initialised unit reports the flag clear, and the set is then
		 * refused. Carry on to the LUN probe, which is the real question.
		 */
		ufs_io_stage = 3;
	}
#endif
	ufs_pick_lun();
	ufs_io_stage = 4;
	state = 1;
	return 1;
}

/* Not every board reads through UFS: one with an eMMC (the MT6768) uses msdc.h and this is unused. */
static int __attribute__((unused)) ufs_read(uint64_t lba, uint32_t blocks, void *dst)
{
	uint8_t *d = (uint8_t *)dst;
	uint64_t byte_at, bytes;
	uint32_t lbs, per;

	if (ufs_failed) {
		return 0;
	}
	if (!ufs_io_ready()) {
		return 0;
	}
	/*
	 * The unit counts in its own blocks, and the caller counts in 512-byte sectors the way
	 * msdc_read does. Ask the device once what its block is, then work in bytes and round every
	 * request out to whole device blocks; the extra bytes are read but not copied out.
	 */
	/*
	 * The logical block size is a fixed 4 KiB here, and asking the unit for it was a mistake
	 * worth recording: it answers TEST UNIT READY, then returns a READ CAPACITY(10) whose
	 * block-length word is zero while its last-LBA word (0x1a0a503) only makes sense at 4096.
	 * A wrong answer is worse than no answer, so the size is not taken from the unit at all -
	 * gpt_detect() below reads both 512 and 4096 and keeps whichever shows the signature,
	 * which is a test of the actual content rather than of the unit's self-description.
	 */
	ufs_lbs = UFS_BLOCK;
	ufs_lbs_known = 1;
	lbs = ufs_lbs;
	per = UFS_SLOT_BUFFER / lbs;
	byte_at = lba * 512ull;
	bytes = (uint64_t)blocks * 512ull;

	while (bytes) {
		uint64_t blk = byte_at / lbs;
		uint32_t off = (uint32_t)(byte_at % lbs);
		uint32_t want = bytes < UFS_SLOT_BUFFER ? (uint32_t)bytes : UFS_SLOT_BUFFER;
		uint32_t nblk = (off + want + lbs - 1u) / lbs;
		uint32_t avail, take;
		/*
		 * The controller deposits the data wherever the PRD points. When the caller's own
		 * destination is a 64-byte aligned physical address and the request starts on a block
		 * boundary, point the PRD straight at it and skip the copy out altogether.
		 *
		 * This is not an optimisation of a fast path, it is the difference between working and
		 * not: with the MMU off every load and store is Device-nGnRnE, so copying out of the
		 * slot buffer runs one 4-byte bus transaction at a time. A 2.3 GiB image at that rate
		 * looks exactly like a hang -- build 78 sat at "reading 2376 MB" without printing a
		 * single percentage, because the first 10% alone (237 MB) is tens of millions of
		 * uncoalesced word accesses.
		 */
		if (off == 0 && (((uint64_t)(uintptr_t)d & 63ull) == 0)) {
			if (nblk > per) {
				nblk = per;
			}
			if (blk > 0xffffffffull) {
				ufs_failed = 1;
				ufs_prd_dst = 0;
				return ufs_fail("lba out of READ(10) range", (uint32_t)blk);
			}
			ufs_prd_dst = (uint64_t)(uintptr_t)d;
			if (!ufs_cmd_once(ufs_lun, (uint32_t)blk, nblk, UPIU_FLAG_READ, UTRD_CT_UFS | UTRD_DD_IN)) {
				ufs_prd_dst = 0;
				return 0;
			}
			ufs_prd_dst = 0;
			avail = nblk * lbs;
			take = want < avail ? want : avail;
			d += take;
			bytes -= take;
			byte_at += take;
			continue;
		}
		ufs_prd_dst = 0;
		if (nblk > per) {
			nblk = per;
		}
		if (blk > 0xffffffffull) {
			ufs_failed = 1;
			return ufs_fail("lba out of READ(10) range", (uint32_t)blk);
		}
		if (!ufs_cmd_once(ufs_lun, (uint32_t)blk, nblk, UPIU_FLAG_READ, UTRD_CT_UFS | UTRD_DD_IN)) {
			return 0;
		}
		/* device memory on both sides: copy a word at a time */
		avail = nblk * lbs - off;
		take = want < avail ? want : avail;
		ufs_put(d, (const void *)(UFS_AREA_BUF + off), take);
		d += take;
		bytes -= take;
		byte_at += take;
	}
	return 1;
}

/*
 * ufs_write(lba, blocks, src): the same geometry as ufs_read, the other direction. It exists so
 * that a log taken on a board with no UART, no ramoops and no pstore has somewhere to go that
 * survives the reset -- into a partition of our own, which Android never mounts.
 *
 * Writes go straight out of the caller's buffer through the PRD when it is 64-byte aligned (the
 * same zero-copy rule as the read path, for the same reason: with the MMU off a copy is one bus
 * transaction per word). The cache is cleaned before the doorbell, not invalidated, because the
 * controller must see what the CPU wrote.
 */
static int __attribute__((unused)) ufs_write(uint64_t lba, uint32_t blocks, const void *src)
{
	const uint8_t *s8 = (const uint8_t *)src;
	uint64_t byte_at, bytes;
	uint32_t lbs, per;

	if (ufs_failed) {
		return 0;
	}
	if (!ufs_io_ready()) {
		return 0;
	}
	ufs_lbs = UFS_BLOCK;
	ufs_lbs_known = 1;
	lbs = ufs_lbs;
	per = UFS_SLOT_BUFFER / lbs;
	byte_at = lba * 512ull;
	bytes = (uint64_t)blocks * 512ull;

	while (bytes) {
		uint64_t blk = byte_at / lbs;
		uint32_t off = (uint32_t)(byte_at % lbs);
		uint32_t want = bytes < UFS_SLOT_BUFFER ? (uint32_t)bytes : UFS_SLOT_BUFFER;
		uint32_t nblk, avail, take;

		if (off == 0 && (((uint64_t)(uintptr_t)s8 & 63ull) == 0)) {
			nblk = (want + lbs - 1u) / lbs;
			if (nblk > per) {
				nblk = per;
			}
			if (blk > 0xffffffffull) {
				ufs_failed = 1;
				ufs_prd_dst = 0;
				return ufs_fail("lba out of WRITE(10) range", (uint32_t)blk);
			}
			ufs_prd_dst = (uint64_t)(uintptr_t)s8;
			if (!ufs_cmd_once(ufs_lun, (uint32_t)blk, nblk, UPIU_FLAG_WRITE,
					  UTRD_CT_UFS | UTRD_DD_OUT)) {
				ufs_prd_dst = 0;
				return 0;
			}
			ufs_prd_dst = 0;
			avail = nblk * lbs;
			take = want < avail ? want : avail;
			s8 += take;
			bytes -= take;
			byte_at += take;
			continue;
		}
		/* an unaligned head or tail goes through the slot buffer */
		nblk = (off + want + lbs - 1u) / lbs;
		if (nblk > per) {
			nblk = per;
		}
		ufs_zero(UFS_AREA_BUF, nblk * lbs);
		ufs_put(UFS_AREA_BUF + off, s8, want);
		if (!ufs_cmd_once(ufs_lun, (uint32_t)blk, nblk, UPIU_FLAG_WRITE,
				  UTRD_CT_UFS | UTRD_DD_OUT)) {
			return 0;
		}
		avail = nblk * lbs - off;
		take = want < avail ? want : avail;
		s8 += take;
		bytes -= take;
		byte_at += take;
	}
	return 1;
}

/* What the controller looks like as LK leaves it, and what we did with its list. */
static void __attribute__((unused)) ufs_report(void)
{
	uint32_t cap = ufs_rd(UFS_CAP);

	ufs_last.what = "not tried";
	ufs_last.status = 0;
	ufs_last.what = "controller";
	ufs_last.status = ufs_rd(UFS_HCS);
	ufs_nutrs = UFS_CAP_NUTRS(cap);
}

#endif /* MTK_UFS_H */
