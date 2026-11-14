# GPUEB IPI 集成补丁草案（hakios.mali610.c）

写于 2026-11-15。目标是让 `hakios.mali610.c` 在写 `MCU_CONTROL = AUTO` 之前，先按 panthor 的顺序
把 GPU 电源交给 GPUEB：发 `CMD_INIT_SHARED_MEM` 和 `CMD_POWER_CONTROL` 两条 IPI。

本文只给补丁文本与说明，**没有改动 `/home/lizi/IOS/hakios-mtk` 的任何文件**。
所有编译验证都在 `/tmp/gp/` 下的副本上做。产物：本文、`GPUEB-INTEGRATION.patch`、
`../gpueb-patch-check.py`。

---

## 0. 结论先行

| 项 | 结论 |
|---|---|
| 补丁能否 `git apply` | **能**。`git apply --check` 通过，应用后与作者本地验证过的副本 md5 相同（`c5d41235ece6adc73e8bee1e07f0358e`） |
| 总改动量 | **11 个 hunk / 843 增 61 删**（驱动 9 hunk / header 整文件 1 hunk（552 行）/ `drivers/map` 1 hunk；`git diff --stat`：3 文件、843 insertions、61 deletions） |
| 能否编译 | **能**。用仓库真实编译命令的**完整 warning 集**（`makedefs/MakeInc.def:136-173` 的 `-Weverything … -Werror`）加 `-fsyntax-only`，退出码 0、零诊断 |
| 默认是否启用 | **不会**。需要 `mali_gpueb=1`（或 2）。不传参数时行为与现在完全一致 |
| `mali_gpueb` 是否重名 | **是，驱动里已有同名参数**（5253 版 `:5135-5141`，`PE_parse_boot_argn` 在 `:5138`）。本补丁**复用并重定义**它，详见 §C.1 |
| 那个矛盾 | **未解释**。见 §F。实测把它收窄到「2 ms 内 shader 未 ready 而 cycle counter 在跑」，但没有一条证据能证明「GPUEB gate 了 GPU」 |

改动文件：

| 文件 | 性质 |
|---|---|
| `drivers/gpu/hakios.gpueb-ipi.h` | 新增（把 `docs/headset-wip/findings/gpueb-ipi.h` 原样搬进树，改名加前缀） |
| `drivers/map` | 1 行，登记上面这个 header |
| `mtk/mt6895/drivers/gpu/hakios.mali610.c` | 9 个 hunk |

> **重要：行号基准，以及它在写作期间变了两次。**
>
> | 版本 | 行数 | md5 | 补丁 `@@` 行号对应 |
> |---|---|---|---|
> | 开工时 | 5219 | — | ✗ 已过时 |
> | 生成补丁时 | 5253 | `0608664f4fea011658b6ab65c8d1b666` | **✓ 本文所有 `@@`/行号引用都指这一版** |
> | 收尾复查时 | 5255 | `255a08d4369670d326380ffbe99f9db9` | 偏移 +1/+2 行，见下 |
>
> 两次变化都是**另一个进程在改 PMIF 部分**（5219→5253 改 `mali_pmif_*`，5253→5255 加
> `#include <sys/hakios.pmif.h>`、把 `MALI_PMIF_SPINS` 换成 `hakios_pmif_budget`），与本补丁
> 的 9 个 hunk **没有一处重叠**。
>
> 实测（§H）：补丁对 5253 版 `git apply --check` 通过；对收尾时的 5255 版**同样通过**，
> `git apply -v` 报 offset +1/+2 行（9 个 hunk 全部成功，无 fuzz），应用后
> `xnu-clang -fsyntax-only` 退出码 0。所以补丁在两个版本上都可用，行号偏移由 `git apply` 自己吸收。
>
> 本文正文的 `:NNN` 引用一律指 **5253 版**。若拿到的是 5255 版，加 1-2 即可；`git apply` 不受影响。

---

## A. 精确的 diff 文本

分三块。可以合并成一个文件 `git apply`（已验证），也可以按块单独看。

### A.1 驱动本体（9 个 hunk）

下面的文本是 `GPUEB-INTEGRATION.patch` 里驱动那一段的**逐字节复制**（去掉 `diff --git` 与 `index` 两行，
`git apply` 不需要它们）。**不要手抄**：这个文件在写作期间被别的进程改过两次，手抄一定会漂。

这段文本已单独抽出并验证：`git apply --check` 通过（只打驱动、不打 header 与 map 时也能落地，
代价是 `#include "hakios.gpueb-ipi.h"` 找不到文件——所以三块要一起用）。

```diff
--- a/mtk/mt6895/drivers/gpu/hakios.mali610.c
+++ b/mtk/mt6895/drivers/gpu/hakios.mali610.c
@@ -41,6 +41,42 @@
 #include <vm/vm_kern.h>
 
 #include "hakios.mali610_fw.h"
+/*
+ * GPUEB's mailbox, and the two commands that give it this GPU's power.
+ *
+ * GPUEB is the RV33 at 0x13c40000 that the loader starts and that owns this SoC's GPU power and
+ * clock sequencing. Until now this driver never sent it anything, and the one probe it had read
+ * four registers inside that block, which is the one thing that cannot be done safely here: a
+ * gated MTK peripheral does not fault on a read, it never answers (see gmpu_probe() below).
+ *
+ * What is safe is the mailbox, and that is what the header carries: 0x13c5fd80 for the 640-byte
+ * message SRAM, 0x13c62000 for the four control registers, 0x7c600000 for the status block GPUEB
+ * writes. None of the three is inside gpueb_base. Every address, slot and bit in it has a source
+ * line, in docs/headset-wip/findings/GPUEB-IPI-PROTOCOL.md.
+ *
+ * The header needs MMIO, a barrier and a microsecond delay, and asks for them by name so that it
+ * needs no kernel API of its own; the three below are this driver's, and match what every other
+ * block in this file does (reg_rd/reg_wr and mali_gpu_write use the same two idioms).
+ */
+#define GPUEB_MMIO_READ32(va)           (*(volatile uint32_t *)(va))
+#define GPUEB_MMIO_WRITE32(va, v)       (*(volatile uint32_t *)(va) = (v))
+#define GPUEB_BARRIER()                 __asm__ volatile ("dsb sy" ::: "memory")
+#define GPUEB_UDELAY(us)                delay((int)(us))
+#include "hakios.gpueb-ipi.h"
+/*
+ * The one field layout that has a wrong answer that still compiles.
+ *
+ * A 32-byte message is eight words, and CMD_INIT_SHARED_MEM's four numbers are two 64-bit bases
+ * followed by two 32-bit sizes -- not base/size/base/size. Written the wrong way round it is still
+ * 32 bytes and still four values, so nothing complains until GPUEB dereferences an address that
+ * is half a size. These four lines make the compiler say so instead.
+ */
+_Static_assert(sizeof(union gpufreq_ipi_msg) == 32u, "GPUFREQ_IPI_DATA_LEN is 8 words");
+_Static_assert(GPUFREQ_IPI_MSG_BYTES == 32u, "the message is 32 bytes");
+_Static_assert(offsetof(union gpufreq_ipi_msg, f.u.addr.status_base) == 8u,
+    "status_base is the first 64-bit word of the union");
+_Static_assert(offsetof(union gpufreq_ipi_msg, f.u.addr.status_size) == 24u,
+    "status_size follows the two 64-bit bases, it does not interleave with them");
 
 extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
 extern uint64_t kvtophys(vm_offset_t va);
@@ -246,25 +282,22 @@ extern void FlushPoC_DcacheRegion(vm_offset_t va, size_t length);
 #define MALI_MFG_I2M_OSTD       0x0f40u         /* 0x13FBFF40 */
 
 /*
- * GPUEB: a separate Cortex-M4-ish coprocessor at 0x13C40000 that the vendor firmware leaves in
- * charge of the GPU's own power and clock sequencing on this SoC (the DTS says
+ * GPUEB: the RV33 coprocessor at 0x13C40000 that the loader starts and that the vendor firmware
+ * leaves in charge of this SoC's GPU power and clock sequencing (the DTS says
  * `gpueb-support = <1>`, and gpufreq_mt6895.c:5166 jumps over its entire power_control path
  * because of that flag).
  *
- * Its watchdog kick register is the cheapest liveness probe there is: the firmware rewrites it
- * continuously, so two reads a millisecond apart that differ mean the coprocessor is still
- * running -- in Android or in here. What that implies for a core that will not fetch is the
- * question this whole probe exists to answer: if GPUEB is alive while XNU owns the GPU, then it
- * may be the one holding the MFG bus, and no amount of registering from the AP side would show
- * up as anything but "busy".
+ * Nothing in this file reads 0x13c40000 or any offset from it, and the seven macros that used to
+ * sit here are gone rather than unused. They were 0x2061c (WDT_KICK, 0x13c6061c), 0x20618 (WDT_CON),
+ * 0x2033c (IRQ_RAW), 0x1fd1c (GPR) and 0x22078 (MBOX_RECV, 0x13c62078 -- the one of the five that
+ * is *outside* the block, which is why the mailbox can be used and the other four cannot). The
+ * fourth probe is the one to remember: MBOX_RECV was already the absolute address of a register
+ * that works, spelled as an offset from a base that does not.
+ *
+ * gpueb_probe() below is what replaced the watchdog liveness test: the mailbox's own two status
+ * registers and GPUEB's status block in reserved DRAM. They answer "has it spoken lately", not
+ * "is it executing", and that is the honest limit of what can be read without touching the block.
  */
-#define MALI_GPUEB_PHYS         0x13c40000ULL
-#define MALI_GPUEB_SIZE         0x24000u
-#define MALI_GPUEB_WDT_KICK     0x2061cu
-#define MALI_GPUEB_WDT_CON      0x20618u
-#define MALI_GPUEB_IRQ_RAW      0x2033cu
-#define MALI_GPUEB_MBOX_RECV    0x22078u
-#define MALI_GPUEB_GPR          0x1fd1cu        /* gpr_offset from the allocator's node */
 /*
  * The MFG interconnect's own clock gates.
  *
@@ -458,12 +491,21 @@ enum : uint32_t { kAsCmdUpdate = 1, kAsCmdLock = 2, kAsCmdFlushMem = 5, kAsCmdUp
 
 /* ------------------------------------------------------------------ state */
 
-static vm_offset_t gSpm, gTopckgen, gMfgcfg, gMfgpll, gGpu, gInfracfg, gMfgrpc, gGpueb;
+static vm_offset_t gSpm, gTopckgen, gMfgcfg, gMfgpll, gGpu, gInfracfg, gMfgrpc;
+/*
+ * GPUEB's mailbox, mapped once and only when mali_gpueb asks for it. A zero field means "not
+ * mapped", which is the state every run before this one was in.
+ */
+static struct {
+	vm_offset_t sram;       /* 0x13c5fd80: 640 B of message SRAM, 160 slots of 4 B */
+	vm_offset_t ctl;        /* 0x13c62000: four 1-bit-per-channel registers in one page */
+	vm_offset_t status;     /* 0x7c600000: GPUFREQ's status block, written by GPUEB */
+} gGpueb;
 static unsigned gMaliProgress;                  /* how far bring-up got; 1 and 2 are the milestones */
 static unsigned gMaliLevel = 2;                 /* the boot-arg's number: 1 power, 2 all, 3..5 stop earlier */
 static unsigned gMaliAsnHash = 1;               /* mali_asn=0 turns off the custom ASN hash */
 static unsigned gMaliWitness;                   /* mali_witness=1 runs 32 bytes of our own code instead */
-static unsigned gMaliGpueb;                     /* mali_gpueb=1 maps a block whose clock is off */
+static unsigned gMaliGpueb;                     /* mali_gpueb: 0 off, 1 IPI then AUTO, 2 withhold AUTO */
 static unsigned gMaliRpc;                       /* mali_rpc=1 touches mfg_rpc, 2 reads it only */
 /*
  * mali_as0=1 parks address space 0 the way the other seven are parked, instead of giving it the
@@ -2310,50 +2352,198 @@ mfg_debug_top(void)
 }
 
 /*
- * Is the GPUEB coprocessor still running?
+ * GPUEB's mailbox, and the two questions that can be asked of it without touching its block.
  *
- * Two reads of its watchdog kick register, one millisecond apart. The firmware rewrites that word
- * continuously while it is alive, so a difference is proof it is still executing and a match is
- * strong evidence it is not. The interrupt raw status is read alongside because a live coprocessor
- * that is talking to the AP leaves bits set in it.
+ * Map first, once. The three windows are the message SRAM, the control page and GPUEB's status
+ * block in the phone's reserved RAM (0x7c600000, 2 MiB, outside the 4 GiB window XNU is given --
+ * board.h:123 puts the kernel's own memory at 0x100000000). None of the three is inside
+ * gpueb_base, so none of them is the read that stopped a run at 33.287 s.
  *
- * This matters here for one specific reason: if GPUEB survives the jump to XNU and keeps driving
- * the GPU's power and clock sequencing, then it owns the MFG bus. That would explain, in one
- * stroke, the two things this driver cannot otherwise reconcile -- a bus that reads non-idle from
- * the instant the MCU is enabled and never falls back, and a core that fetches nothing at all
- * while every register the AP can see looks correct.
+ * ml_io_map() is VM_WIMG_IO, i.e. device-nGnRnE and uncached, which is what the protocol asks for
+ * on the control registers and is stronger than it asks for on the SRAM (that one wants normal
+ * memory plus a barrier). Uncached is the safe direction here: the CPU's store to a slot is in
+ * DRAM when the store retires, so there is no cache to clean before GPUEB reads the message, and
+ * GPUEB's store to the status block is in DRAM when the CPU reads it back. The barrier below is
+ * still there, because ordering between the message and the doorbell is not a cache question.
+ */
+static bool
+gpueb_map(void)
+{
+	if (gGpueb.ctl != 0) {
+		return true;
+	}
+	/*
+	 * The SRAM's page starts at 0x13c5f000 and the block at 0x13c5fd80, so this maps 640 bytes
+	 * of mailbox plus the 0x180 bytes in front of it. io_map() rounds down to the page and
+	 * returns the offset, so the pointer arithmetic above is still the block's.
+	 */
+	gGpueb.sram = ml_io_map((vm_offset_t)GPUEB_MBOX_SRAM_PHYS,
+	    (vm_size_t)GPUEB_MBOX_SRAM_SIZE);
+	gGpueb.ctl = ml_io_map((vm_offset_t)GPUEB_MBOX_SEND_PHYS, (vm_size_t)0x80u);
+	gGpueb.status = ml_io_map((vm_offset_t)GPUEB_SHARED_PHYS,
+	    (vm_size_t)GPUFREQ_MEM_SIZE);
+	if (gGpueb.sram == 0 || gGpueb.ctl == 0 || gGpueb.status == 0) {
+		printf("mali: gpueb: the mailbox will not map (sram %#lx ctl %#lx status %#lx)\n",
+		    (unsigned long)gGpueb.sram, (unsigned long)gGpueb.ctl,
+		    (unsigned long)gGpueb.status);
+		gGpueb.sram = gGpueb.ctl = gGpueb.status = 0;
+		return false;
+	}
+	printf("mali: gpueb: mailbox mapped: sram %#lx ctl %#lx status %#lx\n",
+	    (unsigned long)gGpueb.sram, (unsigned long)gGpueb.ctl, (unsigned long)gGpueb.status);
+	return true;
+}
+
+/*
+ * What the mailbox and the status block say, read-only.
+ *
+ * This is what gpueb_watch() became. It cannot say whether GPUEB is executing -- only its own
+ * block could, and that is the block that must not be read -- but it can say three things that
+ * matter: whether a message is still in flight on the GPUFREQ channel (tx busy), whether GPUEB
+ * sent us one (rx pending), and what GPUEB last wrote into the status block it owns. The last is
+ * history, not liveness: on a phone that boots Android, power_control is 1 there because GPUEB
+ * put it there, and on a board where nobody has ever spoken to it the whole block reads zero.
  */
 static void
-gpueb_watch(const char *when)
+gpueb_probe(const char *when)
 {
-	uint32_t a, b, raw, con;
+	uint32_t tx, rx;
 
-	if (gGpueb == 0) {
-		printf("mali: gpueb %s: not mapped\n", when);
+	if (gMaliGpueb == 0) {
+		printf("mali: gpueb %s: off (mali_gpueb=0)\n", when);
 		return;
 	}
-	a = reg_rd(gGpueb, MALI_GPUEB_WDT_KICK);
-	delay(1000);
-	b = reg_rd(gGpueb, MALI_GPUEB_WDT_KICK);
-	raw = reg_rd(gGpueb, MALI_GPUEB_IRQ_RAW);
-	con = reg_rd(gGpueb, MALI_GPUEB_WDT_CON);
-	printf("mali: gpueb %s: kick %#x -> %#x (%s), irq raw %#x, wdt_con %#x\n",
-	    when, a, b, a == b ? "frozen" : "running", raw, con);
+	if (!gpueb_map()) {
+		return;
+	}
+	tx = GPUEB_MMIO_READ32(gGpueb.ctl + GPUEB_MBOX_CTL_TX_STS);
+	rx = GPUEB_MMIO_READ32(gGpueb.ctl + GPUEB_MBOX_CTL_RX_STS);
+	printf("mali: gpueb %s: tx_busy %u rx_pending %u (chan %u); status power_control %u"
+	    " dvfs %#x shader_present %#x power_count %u sb %#x ptp %#x\n", when,
+	    (tx >> GPUEB_CHAN_GPUFREQ) & 1u, (rx >> GPUEB_CHAN_GPUFREQ) & 1u,
+	    (unsigned)GPUEB_CHAN_GPUFREQ,
+	    gpueb_status_read(gGpueb.status, GPUSTAT_OFF_POWER_CONTROL),
+	    gpueb_status_read(gGpueb.status, GPUSTAT_OFF_DVFS_STATE),
+	    gpueb_status_read(gGpueb.status, GPUSTAT_OFF_SHADER_PRESENT),
+	    gpueb_status_read(gGpueb.status, GPUSTAT_OFF_POWER_COUNT),
+	    gpueb_status_read(gGpueb.status, GPUSTAT_OFF_SB_VERSION),
+	    gpueb_status_read(gGpueb.status, GPUSTAT_OFF_PTP_VERSION));
+}
+
+/* The name the IPI layer wants for "milliseconds now"; clock.h is already included. */
+static unsigned
+gpueb_now_ms(void)
+{
+	uint64_t abstime = 0;
+	uint64_t ns = 0;
+
+	clock_get_uptime(&abstime);
+	absolutetime_to_nanoseconds(abstime, &ns);
+	return (unsigned)(ns / 1000000ull);
+}
+
+static const char *
+gpueb_strerror(int status)
+{
+	switch (status) {
+	case GPUEB_OK:          return "ok";
+	case GPUEB_ETXSTUCK:    return "tx busy, the previous message was never taken";
+	case GPUEB_ETIMEOUT:    return "no answer inside the timeout";
+	case GPUEB_EREFUSED:    return "the firmware refused the command";
+	case GPUEB_EBADPARAM:   return "bad parameters";
+	case GPUEB_ENOTREADY:   return "the mailbox is not there";
+	}
+	return "unknown";
+}
+
+/*
+ * The two commands, in the order the vendor sends them, and nothing else.
+ *
+ * 1. CMD_INIT_SHARED_MEM (0) tells GPUEB where the block it writes lives: status_base 0x7c600000
+ *    over 0x400 bytes, debug_base 0x7c600400 over 0xc00. The two bases are 64-bit and each takes
+ *    two words, which is the one field layout that is easy to get wrong -- it is base, size, base,
+ *    size in the vendor's struct, not base/size/base/size interleaved (PROTOCOL.md section 3, and
+ *    the header's addr sub-struct with its static assertions).
+ *
+ * 2. CMD_POWER_CONTROL (6) with power_state 1. There is no separate "power on bit": the command
+ *    is the power-on, and what it does is hand the GPU's power to GPUEB (power_control in the
+ *    status block is a scalar, not a bitfield). shader_present and power_count are GPUEB's to
+ *    write; the AP only reads them.
+ *
+ * Both are polled, no interrupt is registered anywhere: the mailbox's four registers are enough
+ * to see a message out and an answer in, and this driver has no GIC line for GPUEB (SPI 516).
+ * One second per command, not the vendor's ten: a boot path that waits twenty seconds to learn
+ * that nobody is answering is worse than one that says so after two.
+ */
+static bool
+gpueb_ipi_up(void)
+{
+	union gpufreq_ipi_msg msg;
+	uint32_t shader;
+	int ack = 0;
+	int ret;
+
+	if (!gpueb_map()) {
+		return false;
+	}
 	/*
-	 * The GPR word the protected-memory-allocator node points at.
-	 *
-	 * protected-memory-allocator in the device tree (xaga_live.dts) gives gpueb_base
-	 * 0x13c40000, gpr_offset 0x1fd1c and gpr_id 6, so the word is at
-	 * 0x13c40000 + 0x1fd1c + 6*4 = 0x13c5fd34. The MTK patch that implements the allocator
-	 * computes the page table base from it as (value << PAGE_SHIFT) + gmpu_table_size, which
-	 * means a non-zero value there is proof the GPU memory protection unit has been given a
-	 * table, and zero means it never was. On xaga the table area is me_GPUmputab_PMA at
-	 * 0x78c00000, so a programmed word should read 0x78c00 or thereabouts.
+	 * Section 13, step 5: a message left unconsumed by Android occupies the channel, and the
+	 * first command would then wait on a bit nobody is going to clear. Clearing it here is the
+	 * same write the receive path makes after it has read a message.
+	 */
+	if (gpueb_rx_pending(gGpueb.ctl)) {
+		printf("mali: gpueb: channel %u had a message pending; clearing it before the first"
+		    " command\n", (unsigned)GPUEB_CHAN_GPUFREQ);
+		gpueb_irq_clear(gGpueb.ctl);
+	}
+	gpueb_build_init_shared_mem(&msg, (unsigned long long)GPUEB_SHARED_PHYS,
+	    (unsigned long long)(GPUEB_SHARED_PHYS + GPUFREQ_STATUS_MEM_SZ),
+	    (unsigned int)GPUFREQ_STATUS_MEM_SZ, (unsigned int)GPUFREQ_DEBUG_MEM_SZ);
+	printf("mali: gpueb: INIT_SHARED_MEM status %#llx size %#x, debug %#llx size %#x\n",
+	    (unsigned long long)GPUEB_SHARED_PHYS, GPUFREQ_STATUS_MEM_SZ,
+	    (unsigned long long)(GPUEB_SHARED_PHYS + GPUFREQ_STATUS_MEM_SZ),
+	    GPUFREQ_DEBUG_MEM_SZ);
+	ret = gpueb_ipi_xfer(gGpueb.sram, gGpueb.ctl, &msg, &ack, gpueb_now_ms,
+	    GPUEB_TMO_INIT_SHARED_MS);
+	if (ret != GPUEB_OK) {
+		printf("mali: gpueb: INIT_SHARED_MEM failed: %s (%d)\n", gpueb_strerror(ret), ret);
+		return false;
+	}
+	printf("mali: gpueb: INIT_SHARED_MEM answered, return %d\n", ack);
+	/*
+	 * No pointer to follow and no retry: the command only says where the memory is, so a
+	 * rejection here leaves nothing to undo and a repeat would tell GPUEB the same thing twice.
 	 */
-	printf("mali: gpueb %s: gpr[6] %#x (gmpu table %s at %#llx)\n", when,
-	    reg_rd(gGpueb, MALI_GPUEB_GPR + 6u * 4u),
-	    reg_rd(gGpueb, MALI_GPUEB_GPR + 6u * 4u) != 0 ? "programmed" : "never programmed",
-	    (unsigned long long)((uint64_t)reg_rd(gGpueb, MALI_GPUEB_GPR + 6u * 4u) << 12));
+	if (ack < 0) {
+		printf("mali: gpueb: INIT_SHARED_MEM rejected (%d); not sending POWER_CONTROL\n", ack);
+		return false;
+	}
+	ack = 0;
+	gpueb_build_power_control(&msg, 1);
+	ret = gpueb_ipi_xfer(gGpueb.sram, gGpueb.ctl, &msg, &ack, gpueb_now_ms,
+	    GPUEB_TMO_POWER_CTRL_MS);
+	if (ret != GPUEB_OK) {
+		printf("mali: gpueb: POWER_CONTROL failed: %s (%d)\n", gpueb_strerror(ret), ret);
+		return false;
+	}
+	if (ack < 0) {
+		printf("mali: gpueb: POWER_ON refused, return %d\n", ack);
+		return false;
+	}
+	printf("mali: gpueb: POWER_ON accepted, return %d\n", ack);
+	/*
+	 * Step 9: a soft check that does not block. shader_present is GPUEB's own count of the cores
+	 * it believes it has; zero after a successful power-on means it took the request but has not
+	 * enumerated anything, which is worth a line in the log and not worth skipping the MCU start
+	 * for -- the run that would tell us whether the start works is the run after this one.
+	 */
+	shader = gpueb_status_read(gGpueb.status, GPUSTAT_OFF_SHADER_PRESENT);
+	if (shader == 0) {
+		printf("mali: gpueb: warning: shader_present still 0 after POWER_ON\n");
+	} else {
+		printf("mali: gpueb: shader_present %#x after POWER_ON\n", shader);
+	}
+	return true;
 }
 
 static void
@@ -3866,6 +4056,30 @@ start_mcu(void)
 		    gMaliTrial, stable >= 8u ? "settled" : "NOT SETTLED", cur, cur & 0x3ffcu,
 		    spins < 200000u ? spins + 1u : spins, stable, changes, before);
 	}
+	/*
+	 * GPUEB first, and this is the line the whole patch is for.
+	 *
+	 * panthor does exactly this in that order: mt6895_gpueb_power_on() and then
+	 * MCU_CONTROL = AUTO (panthor_fw.c:1131-1143, and again earlier in panthor_device.c:256-268).
+	 * Nineteen runs of this driver reached AUTO with GPUEB never having been spoken to, so
+	 * "the MCU does not start" has never been tested with the GPU's power handed over.
+	 *
+	 * It goes here, above the mali_mcuseq block, rather than immediately above the AUTO write:
+	 * that block writes MCU_CONTROL = ENABLE, and "the two commands precede the first write to
+	 * MCU_CONTROL" is a stronger statement than "they precede AUTO" -- it is the one panthor
+	 * makes, and it is the one that stays true if mali_mcuseq is ever left on by default.
+	 *
+	 * mali_gpueb=1 sends the two commands and goes on to AUTO whatever the answer, which is what
+	 * keeps this run comparable with the nineteen before it. mali_gpueb=2 withholds AUTO when the
+	 * commands fail, which is what panthor does. Neither value changes the MCU start itself.
+	 */
+	if (gMaliGpueb != 0 && !gpueb_ipi_up()) {
+		printf("mali: gpueb: power-on not confirmed; MCU_CONTROL=AUTO %s\n",
+		    gMaliGpueb >= 2u ? "withheld (mali_gpueb=2)" : "still follows (mali_gpueb=1)");
+		if (gMaliGpueb >= 2u) {
+			return false;
+		}
+	}
 	/*
 	 * mali_mcuseq=1: two writes instead of one.
 	 *
@@ -4311,15 +4525,18 @@ mali_bring_up(void)
 	gInfracfg = ml_io_map(MALI_INFRACFG_PHYS, MALI_INFRACFG_SIZE);
 	gMfgrpc = ml_io_map(MALI_MFGRPC_PHYS, MALI_MFGRPC_SIZE);
 	/*
-	 * GPUEB is behind its own clock gate. The phone's gpueb driver opens it through the clock
-	 * framework; nothing in this kernel does. Reading a gated MTK peripheral does not return an
-	 * error -- the access never completes, the core waits, and the hardware watchdog resets the
-	 * board. A diagnostic that produces a boot loop is worse than no diagnostic, so the block is
-	 * only mapped when mali_gpueb=1, and only after the gate has been opened by whoever turns it
-	 * on. Left off, every read here reports "not mapped" and costs nothing.
+	 * GPUEB's block at 0x13c40000 is behind its own clock gate and is never mapped here.
+	 *
+	 * The phone's gpueb driver opens that gate through the clock framework; nothing in this
+	 * kernel does, and reading a gated MTK peripheral does not return an error -- the access never
+	 * completes, the core waits, and the hardware watchdog resets the board (gmpu_probe()'s
+	 * comment, and the run that stopped at 33.287 s). What is mapped instead, when mali_gpueb
+	 * asks for it, is the mailbox: 0x13c5fd80 and 0x13c62000 are their own iomem entries outside
+	 * the block, and 0x7c600000 is reserved DRAM. Left off, every read below reports "off" and
+	 * costs nothing.
 	 */
 	if (gMaliGpueb) {
-		gGpueb = ml_io_map(MALI_GPUEB_PHYS, MALI_GPUEB_SIZE);
+		(void)gpueb_map();
 	}
 	/*
 	 * mfgcfg is mapped here rather than inside the cold-start branch, because the interconnect
@@ -4670,7 +4887,7 @@ mali_bring_up(void)
 	 * running? Asked at the earliest point this driver touches the block, so the answer is not
 	 * contaminated by anything it does later.
 	 */
-	gpueb_watch("before the address spaces");
+	gpueb_probe("before the address spaces");
 	gmpu_probe("before the address spaces");
 	if (gMaliPreAs0 && !gMaliAs0 && gMaliLevel > 4 && gMaliAs0First) {
 		if (!mali_early_as0()) {
@@ -4969,7 +5186,7 @@ mali_bring_up(void)
 		 * be the one holding it. If GPUEB is still kicking its watchdog while the MFG channel
 		 * reads non-idle and the core has done nothing, the two facts are about each other.
 		 */
-		gpueb_watch("after the failed start");
+		gpueb_probe("after the failed start");
 		gmpu_probe("after the failed start");
 		cpe_probe();
 		/* Read fixed offsets in our allocated DRAM only. Do not follow firmware pointers
@@ -5132,11 +5349,23 @@ hakios_mali610_init(void)
 			gMaliWitness = witness ? 1u : 0u;
 		}
 	}
+	/*
+	 * mali_gpueb: 0 off, 1 send the two commands and start the MCU anyway, 2 send them and withhold
+	 * AUTO if they fail.
+	 *
+	 * The name is the one this driver already had, and its old meaning -- "map the block at
+	 * 0x13c40000" -- is gone with the block: that map is what the comment in mali_bring_up()
+	 * says must never happen, and the run that did it stopped at 33.287 s. So there is one
+	 * reader of this argument, not two, and value 1 no longer maps anything inside gpueb_base.
+	 */
 	{
 		int gpueb = 0;
 
 		if (PE_parse_boot_argn("mali_gpueb", &gpueb, sizeof(gpueb))) {
-			gMaliGpueb = gpueb ? 1u : 0u;
+			gMaliGpueb = gpueb <= 0 ? 0u : (unsigned)gpueb;
+			if (gMaliGpueb > 2u) {
+				gMaliGpueb = 2u;
+			}
 		}
 	}
 	{
```

### A.2 新增的 header（新文件，552 行）

`drivers/gpu/hakios.gpueb-ipi.h` = `docs/headset-wip/findings/gpueb-ipi.h` **逐字节复制**，只改文件名。
已用 md5 核对：补丁产出的 header = `78aa488140e71c2b5d1f95027edae8e6`，与源文件相同。

正文不重复贴这 552 行；它在 `GPUEB-INTEGRATION.patch` 里是一个完整的 `@@ -0,0 +1,552 @@` hunk。
这一节只给 hunk 头，供 reviewer 确认位置与行数：

```diff
--- /dev/null
+++ b/drivers/gpu/hakios.gpueb-ipi.h
@@ -0,0 +1,552 @@
```

要点：

- 头文件保护 `#ifndef _GPUEB_IPI_H_`（`gpueb-ipi.h:21-22`、`:552`）
- 四个可注入宏都有 `#ifndef` 兜底（`gpueb-ipi.h:29-48`：`MMIO_READ32` @29、`MMIO_WRITE32` @33、`BARRIER` @37、`UDELAY` @42），补丁在 `#include` **之前**定义，所以生效
- 全部 `static inline`，没有 `static` 变量、没有非 inline 定义 —— 这也是它可以是 header 的前提（§C.2）

### A.3 `drivers/map` 一行

```diff
--- a/drivers/map
+++ b/drivers/map
@@ -69,3 +69,4 @@ power   bsd/dev/hakios.thermal.c
 power   bsd/dev/hakios.pmic.c
 power   bsd/dev/hakios.battguard.c
 bluetooth bsd/dev/hakios.bt.c
+gpu      bsd/dev/hakios.gpueb-ipi.h
```

> 这一行**不是**给编译器用的，是给 `tools/drivers-check.py` / `xnu/apply.sh` / `xnu/sync-drivers.sh` 用的：
> `tools/hakios-config.py external-files` 只登记 `.c`，header 走 `drivers/<subsystem>/` 目录（`drivers/map:28`
> 已经用同样的方式登记 `hakios.mali610_fw.h`）。
> 但按 `drivers-check.py:68-72` 的规则，`drivers/gpu/` 下未登记的文件会报
> "is not in drivers/map, so it would never reach the kernel"。
> 若不打算修改 `drivers/map`，**另一种做法**是把 header 放在 `drivers/gpu/` 之外（例如
> `mtk/mt6895/drivers/gpu/`，和 `.c` 同目录，靠 `-I` 找到），代价是绕开仓库的驱动布局约定。两种都可行，
> 本草案选前者，因为它与 `hakios.mali610_fw.h` 的既有做法一致。

> ⚠ `tools/drivers-check.py` 当前**已经**有 1 个无关问题（`drivers/usb is not a known subsystem directory`），
> 所以这个检查在基线上就不是绿的。加这一行不会让它更差。

---

## B. 逐条改动说明

### H1 ｜ `:42-44`（hunk 头写 `-41`）注入四个宏 + `#include` + 四条 `_Static_assert`

**改什么。** 在 `#include "hakios.mali610_fw.h"` 之后插入 `GPUEB_MMIO_READ32`/`WRITE32`/`BARRIER`/`UDELAY`
四个宏、`#include "hakios.gpueb-ipi.h"`，以及四条编译期断言。

**为什么。** header 的设计约束是"零内核依赖"（`gpueb-ipi.h:1-10` 的文件头），MMIO 与延时由调用方注入。这四个
宏的顺序不能调换：`#ifndef` 兜底在 header 内部（`:36-48`），先定义才生效。
四条断言锁的是 `INIT_SHARED_MEM` 的字段布局 —— 协议 §3.2 明确说这是"最容易写错的一处"，
写成交错布局仍然 32 字节、仍然编译通过，只有 GPUEB 解引用半个 size 才会出错。

**引用。** `GPUEB-IPI-PROTOCOL.md` §3.1-§3.3（消息布局）、§13 第 1-2 步（映射语义）。

**失败时。** 编译期失败，运行期无影响。若 `offsetof` 不可用（XNU 里 `kern/assert.h` 自带
`static_assert` 宏但 `offsetof` 来自 `<stddef.h>`），编译报错而不是静默错位。已在 `xnu-clang` 下验证通过。

---

### H2 ｜ `:248-267` 删除七个 `MALI_GPUEB_*` 宏

**改什么。** 删 `MALI_GPUEB_PHYS`(`:261`)、`SIZE`(`:262`)、`WDT_KICK`(`:263`)、`WDT_CON`(`:264`)、
`IRQ_RAW`(`:265`)、`MBOX_RECV`(`:266`)、`GPR`(`:267`)，换成说明为什么不能读这段的注释。

**为什么。** 这七个宏里有五个的绝对地址落在 `0x13c60000..0x13c61fff` 与 `0x13c5fd1c`，全在禁区内。
第六个 `MALI_GPUEB_MBOX_RECV = 0x22078` 本身**是对的**（`0x13c40000 + 0x22078 = 0x13c62078`），
但它写成"相对禁区的偏移"，用它的唯一方式就是先映射禁区 —— 所以它比不用更危险：
一个看起来正确、永远不该被调用的常量。

**引用。** 协议 §14.1-§14.2（逐行核对了这七个宏的绝对地址）、§1.1（iomem 四个独立条目）、
§8.3（不读禁区的三种替代手段）。

**失败时。** 编译期：`gpueb_watch()` 里 12 处引用一并删除（H5），没有残留；`grep MALI_GPUEB` 在
打补丁后的文件里返回 0 条。运行期无影响。

---

### H3 ｜ `:461` `gGpueb` 从 `vm_offset_t` 变成三字段结构

**改什么。** `static vm_offset_t ..., gGpueb;` → 三个字段 `sram` / `ctl` / `status`。
顺带改 `gMaliGpueb` 的行尾注释（`mali_gpueb=1 maps a block whose clock is off` → `0 off, 1 IPI then AUTO, 2 withhold AUTO`）。

**为什么。** 一个 `vm_offset_t` 原本只表示"禁区基址"，本补丁要同时持三个不连续窗口的映射。
用结构而不是三个独立全局，是因为"三者为零"就是"没映射"这一个状态，`gpueb_map()` 的幂等判断只看 `ctl`。

**引用。** 协议 §13 第 1-3 步（三个窗口，三处映射，语义不同）。

**失败时。** 编译期无风险（`gGpueb` 的 12 处引用全部在本补丁内改写）。

---

### H4 ｜ `:2312-2357`（5253 版）`gpueb_watch()` → `gpueb_map()` + `gpueb_probe()` + `gpueb_now_ms()` + `gpueb_ipi_up()`

**改什么。** 删掉读禁区四个寄存器的 `gpueb_watch()`，换成四个函数：

| 函数 | 作用 |
|---|---|
| `gpueb_map()` | 幂等地映射三个窗口，任一失败则三者归零并返回 false |
| `gpueb_probe()` | 只读：`TX_STS` / `RX_STS` 的 bit1 + 状态区 6 个字段 |
| `gpueb_now_ms()` | `clock_get_uptime()` + `absolutetime_to_nanoseconds()` → ms，给 header 的 `gpueb_ipi_xfer()` 用 |
| `gpueb_ipi_up()` | 顺序发两条命令，检查 ack，读 `shader_present`（软判据） |

**为什么。** `gpueb_watch()` 存在的理由是"看门狗在跳 = 协处理器活着"，它的四个探针全在禁区，
所以它**在现状下永远只能打印 `not mapped`**（协议 §14.2 的判断，我核对 `:2331-2334` 属实）。
换掉它有两个独立收益：不再有"映射一次就违规"的代码，以及从"读禁区猜它活着"改成"读它写过的内存 + 
读它自己举的旗"。

**引用。** 协议 §8.3（三种存活判据及其可靠性）、§13 第 5 步（先清 RX 残留）、§10（轮询主流程）、
§7.3（纯轮询可行性）、§7.4（超时取值）。

**失败时。** 三种降级路径：

- 三个窗口任一 map 失败 → 打印 one line（含三个 KVA），返回 false。上层 `gpueb_ipi_up()` 也返回 false，
  按 `mali_gpueb` 的取值决定是否继续。
- TX 一直忙（上一条没被取走）→ `GPUEB_ETXSTUCK`，**未写任何东西**，无副作用。
- 写了 SRAM 但 1 s 内无应答 → `GPUEB_ETIMEOUT`，`gpueb_ipi_xfer()` 内部已写 `CLR` 回滚
  （`gpueb-ipi.h:500-503`：超时分支里先 `gpueb_irq_clear(ctl_va)` 再 `return GPUEB_ETIMEOUT`）。

---

### H5 ｜ `:4321-4323`（`mali_bring_up`）`ml_io_map(MALI_GPUEB_PHYS, ...)` → `gpueb_map()`

**改什么。** 条件分支保留（`if (gMaliGpueb)`），函数体从"映射禁区 0x24000 字节"改成"映射邮箱三个窗口"。

**为什么。** 这是本补丁唯一一处**行为改变**的既有代码：同一个 boot-arg、同一个位置，映射的物理地址
从 `0x13c40000+0x24000` 变成三个不重叠的小窗口。原代码的注释自己说得很清楚——"a diagnostic that
produces a boot loop is worse than no diagnostic"。也就是说：**`mali_gpueb=1` 在基线上是一个
会导致挂机的开关**，任何跑过 `mali_gpueb=1` 的 run 都不能作为"GPUEB 无效"的证据。

**引用。** 协议 §14.4 缺口 #2/#3/#4。

**失败时。** 映射失败只打印一行，`gGpueb` 三字段归零，后续 `gpueb_probe()` 每次都会再试一次 map
（幂等判断看 `ctl`，为 0 就重试）。这不会引入反复失败的开销问题：一个 run 里 `gpueb_probe()` 只被调两次
（`:4673`、`:4972`）。

---

### H6 ｜ `:3869`（`start_mcu`，`mali_mcuseq` 注释块之前；hunk 头写 `-3866`）插入 IPI

**改什么。** 在 `mali_mcuseq` 的 `if` 之前插入 `if (gMaliGpueb != 0 && !gpueb_ipi_up()) {...}`。

**为什么位置不是"紧贴 AUTO 上一行"。** 任务给的是"`MCU_CONTROL = AUTO` 紧前"。实际代码里
`:3893` 的 `AUTO` 之前还有一个 `:3887` 的 `mwr(kMcuControl, kMcuControlEnable)`（`mali_mcuseq=1` 时）。
如果插在两者之间，`mali_mcuseq=1` 的 run 就变成"先 ENABLE，再等 2 s IPI，再 AUTO"——
和 panthor 的顺序不同，也让那个已有开关的实验含义变了。插在 `mali_mcuseq` 之前，
"两条命令先于对 `MCU_CONTROL` 的**任何**写"这句话才成立，而这正是 panthor 的语义
（`panthor_fw.c:1131-1143`：`gpueb_power_on()` 在 `gpu_write(MCU_CONTROL, AUTO)` 之前，
中间没有任何别的 `MCU_CONTROL` 写）。

**为什么 `mali_gpueb=1` 失败后仍继续 AUTO。** 这是刻意的可对照性设计：19 轮实验都是"没有 IPI 就 AUTO"，
如果 `mali_gpueb=1` 在失败时中止，那么这一轮和之前 19 轮就没有可比性，只能得到"失败"这一个信息。
`=1` 继续、`=2` 中止，一个镜像回答两个问题。

**引用。** 协议 §13 第 6-8 步、§14.3（与 panthor 的差异）、§14.4 缺口 #1。

**失败时。** 见 H4 的三种。`=1` 时只多两行 printf 与最多 2 s 延时，然后照常 AUTO。

---

### H7 ｜ `:4673` / `:4972` 两个调用点改名（两处均核对属实）

**改什么。** `gpueb_watch(...)` → `gpueb_probe(...)`，参数不变。

**为什么。** 只改名不改位置：两处的注释（"asked at the earliest point this driver touches the block"、
"the other candidate owner of the bus"）依然成立，`gpueb_probe()` 仍然是纯读、零风险。

**引用。** 协议 §14.2 末段建议（"应改为只读 `0x13c62000` / `0x13c62078` / `0x7c600000`"）。

**失败时。** 无。`mali_gpueb=0`（默认）时 `gpueb_probe()` 立刻返回并打印 `off (mali_gpueb=0)`。

---

### H8 ｜ `:5135-5141` `mali_gpueb` 语义扩展

**改什么。** 从 `gMaliGpueb = gpueb ? 1 : 0` 改成 `clamp(gpueb, 0, 2)`，并加注释说明旧语义已废弃。

**为什么。** 见 §C.1：这个参数已经存在，且它的旧语义（"映射禁区"）在新代码里已经没有对应实现。
保留名字、扩展取值，比新增 `mali_gpueb_ipi` 更好：`configs/boot-args.lst:181` 已经有 `mali_gpueb`，
新增名字要同时改 `docs/boot-args.md` 与 `configs/symbols.tsv`，而这两个文件在基线上**已经**是 out of date
（`tools/boot-args.py --check` 报错），改它们会把一个无关的漂移混进这个补丁。

**引用。** —（工程决定，不是协议结论）。

**失败时。** 传 `mali_gpueb=7` 会被 clamp 成 2；传负数变 0。不会出现"值 3 落到未定义分支"。

---

### H9 ｜ A.3 的 `drivers/map` 一行 + A.2 的 header

**改什么。** `docs/headset-wip/findings/gpueb-ipi.h` 原样进 `drivers/gpu/`，改名 `hakios.gpueb-ipi.h`，`drivers/map` 登记。

**为什么。** 见 §C.2 的 `static inline` 分析：这个 header 的每个函数都是 `static inline`，没有 `static`
变量、没有非 inline 定义，所以放进 header 不会产生"每个 TU 一份副本"的链接问题。

**引用。** —

**失败时。** 若忘记登记 `drivers/map`，`tools/drivers-check.py` 会报 1 条
"drivers/gpu/hakios.gpueb-ipi.h is not in drivers/map"；CI 红，但编译不受影响（`-I` 由 `bsd/dev` 相对路径提供）。

---

## C. 编译期检查清单

### C.1 已存在的 `#define` 冲突（重点：`MALI_GPUEB_MBOX_RECV`）

**结论：本补丁删除了全部七个 `MALI_GPUEB_*` 宏，因此不存在同名冲突。** 但需要区分三类：

**(a) 同名宏。** header 里没有任何 `MALI_` 前缀的名字（全部 `GPUEB_` / `GPUFREQ_` / `GPUSTAT_` / `SHADER_`），
驱动里也没有任何 `GPUEB_` / `GPUFREQ_` / `GPUSTAT_` 前缀。**零重叠**（实测：对 19 个 header 符号
逐个 `grep -c` 在驱动里都是 0）。

**(b) 裸名冲突 —— 确实有，且必须处理。**

| header 符号 | 位置 | 驱动内同名 | 处理 |
|---|---|---|---|
| `POWER_ON` | `gpueb-ipi.h:210-213`（`enum gpufreq_power_state`） | `:1677` / `:1759` / `:2555` **只在注释里**，不是 token | **无冲突**（clang 验证：编译 0 诊断） |
| `POWER_OFF` | `:188` | 无 | 无冲突 |
| `CMD_NUM` / `CMD_COMMIT` 等 20 个 | `:145-167` | 无 | 无冲突 |
| `TARGET_DEFAULT` 等 4 个 | `:171-175` | 无 | 无冲突 |

`POWER_ON` 这条值得单独说明：它在 `xnu-src` 全树里唯一的**代码**用法是 `hakios.adreno.c` 的
`GDSC_POWER_ON`（不同 token），另外三处 `POWER_ON` 全在 `hakios.mali610.c` 的注释文本里。
`grep -rwn POWER_ON` 在 `osfmk`/`bsd`/`libkern`/`iokit` 里只命中那三条注释。

**(c) `MALI_GPUEB_MBOX_RECV` 的处置（任务点名要核对的）。**

基线 `:266` 是 `#define MALI_GPUEB_MBOX_RECV 0x22078u`，**全文件零引用**（`grep -c` = 1，就是定义行本身）。
它的值是相对 `0x13c40000` 的偏移，绝对地址 = `0x13c62078`，与 header 的
`GPUEB_MBOX_RECV_PHYS`（`gpueb-ipi.h:73`，值 `0x13c62078u`）指向同一个寄存器。

**本补丁的做法是删除它**，而不是保留它再 `#undef`。理由：留着它，下一个人就会在需要 recv 状态时
按名字找到它、然后为了用它去映射 `0x13c40000` —— 这正是协议 §14.1 说的陷阱。
删掉之后 `grep MALI_GPUEB` 返回 0 条（已实测），没有"看起来能用但一用就违规"的残留。

若 reviewer 更倾向保留可追溯性，替代做法是：
```c
/* 0x13c62078, spelled absolutely: this used to read 0x22078u from a base that must not be mapped. */
#define MALI_GPUEB_MBOX_RECV_ABS    GPUEB_MBOX_RECV_PHYS
```
但那样它仍然是一个**没人引用的宏**，和基线一样是死的。删除更干净。

### C.2 `#include "hakios.gpueb-ipi.h"` 需要什么额外处理

| 检查项 | 结果 |
|---|---|
| 需要额外 `typedef`？ | **不需要**。header 不自带 `size_t`/`uint32_t`，用的是 `unsigned int` / `unsigned long` / `unsigned long long`（`:63-67`、`:225-227`），全部是 C 内置类型 |
| `static inline` 冲突？ | **不会**。552 行里所有函数（`gpueb_tx_busy` … `gpueb_dump_status`）都是 `static inline`；没有一个 `static` 变量。已在 `xnu-clang -Wall -Wextra` 下验证：未被调用的 `static inline`（如 `gpueb_dump_status`，在 `#ifdef GPUEB_DEBUG` 内）不产生 `-Wunused-function` |
| 头文件保护 | `_GPUEB_IPI_H_`（`gpueb-ipi.h:21-22`、`:552`），与 XNU 的 `_*_H_` 惯例同形，未见重名 |
| `offsetof` | 由 `<libkern/libkern.h>` 带入（驱动 `:37` 已经 include）。实测：只 `#include <libkern/libkern.h>` + `_Static_assert(offsetof(...))` 编译通过；不 include 任何头时 clang 报 `call to undeclared function 'offsetof'`。所以这条依赖是**必需的**，而它已经满足 |
| `_Static_assert` 可用性 | `kern/assert.h:176-187` 定义了 `static_assert` 重载宏；直接用 C11 关键字 `_Static_assert` 亦可（实测通过）。**注意**：XNU 把 `volatile` 重定义为 `__volatile`（`-Dvolatile=__volatile`），header 里的 `*(volatile unsigned int *)` 展开后仍是合法语法 |
| 包含顺序 | 必须晚于 `#include <kern/clock.h>`（`delay()` 与 `clock_get_uptime()` 的声明）。当前插入点 `:44` 之后、`:45` 之前，满足 |
| `-Weverything -Werror` | 真实构建开的是 `-Weverything` 加 `WERROR := -Werror`（`makedefs/MakeInc.def:132`、`:136-173`），比 `-Wall -Wextra` 严格得多。**已按这一套 flags 实测**：基线 0 诊断，打补丁后 0 诊断。新增代码里 `-Wmissing-variable-declarations`（已 `-Wno-`）、`-Wswitch-enum`（已 `-Wno-`）都不构成问题 |
| `-Wdeclaration-after-statement` | 未在 `WARNFLAGS_STD` 里，`-Weverything` 也不含它。header 与新增函数仍遵守了"声明在前"，不依赖这一点 |
| 编译产物大小 | **已实测**，见 §D.1：`__text` 31588 → 32576（+988 字节 / +3.1%），`.o` 480656 → 491768 |

### C.3 命名空间前缀建议

现状：驱动 `MALI_*`，header `GPUEB_*` / `GPUFREQ_*` / `GPUSTAT_*` / `SHADER_*`。

**建议：保持现状，不做批量改名。** 理由：

1. `GPUEB_` / `GPUFREQ_` 是 header 的既有词汇，且与 MTK 源码（`gpufreq_ipi.h`、`mtk-gpueb-mailbox.c`）
   一一对应。改名会切断"这个名字来自哪份源码"的追溯链，而这个项目最贵的资产是溯源。
2. 驱动新增的**函数**用了 `gpueb_` 小写前缀（`gpueb_map` / `gpueb_probe` / `gpueb_ipi_up` /
   `gpueb_now_ms` / `gpueb_strerror`），与驱动既有风格（`reg_rd` / `mfg_bus_watch` / `gmpu_probe`）一致，
   都是 `static`，不导出。
3. 唯一有风险的一类（裸枚举常量 `POWER_ON` / `CMD_*` / `TARGET_*`）已在 C.1(b) 核实为零冲突。
   若将来要与 `mtk/mt6768` 的 `hakios.mali.c` 合编同一个 TU（现在不会，两个 `conf` 的 driver 名不同：
   `MALI610` vs `MALI`），`POWER_ON` 会是第一个撞上的名字。

**可选加固（不在本补丁里）：** 在 header 里把 `enum gpufreq_power_state` / `enum gpufreq_target` /
`enum gpufreq_ipi_cmd` 改为 `#define` 或加 `GPUEB_` 前缀。这会改动已"独立验证过"的文件，
违反"不要重新推导"的约束，所以本草案不动它，只记录风险。

---

## D. 运行时验证

### D.1 静态检查（不需要手机）

按顺序跑，前两步在 10 秒内完成：

```sh
cd /home/lizi/IOS/hakios-mtk

# 1) 补丁能否干净落地。用的是 5253 行 / md5 0608664f 的基线。
#    文件若已被别的进程改过，这里会拒绝 —— 这正是我们要的。
git apply --check /path/to/gpueb-ipi.patch

# 2) 驱动布局检查（基线已有 1 个无关问题：drivers/usb）
python3 tools/drivers-check.py

# 3) 单文件语法检查。命令取自本仓库一次真实的编译调用
#    （logs/headset-resume-20261114-220244/handoff-support/gpueb-shared-late-sample-draft-puq8gg06/syntax-command.txt），
#    把最后的源文件换成打过补丁的副本即可。已在 /tmp 验证：退出码 0，零诊断。
#    注意 -I 要包含 header 所在目录。

# 4) boot-arg 目录。加注释不改参数名，所以这一项应当与基线一致
#    （基线已经是 out of date，见 §B/H8）。
python3 tools/boot-args.py --check
```

**编译产物大小 —— 已实测。** 用同一条真实编译命令加上 `-c` 各编一次（同目录、同 flags、同一台机器）：

| | 文件字节 | `__text` 段 |
|---|---|---|
| 基线 `hakios.mali610.c` | 480656 | `0x7b64` = 31588 |
| 打补丁后 | 491768 | `0x7f40` = 32576 |
| 差 | **+11112（+2.3%）** | **+988（+3.1%）** |

`.o` 的差值远大于 `__text` 的差值，因为 `.o` 里还有 DWARF 调试信息（新增 4 个函数、552 行 header 的
行号表）。**真正占 flash 的是 `__text`：+988 字节**，相对这个驱动 31 KiB 的代码量是 +3.1%。

仓库里那个已构建的 `.o`（`DEVELOPMENT_ARM64_BCM2837/…/hakios.mali610.o`，479664 字节，
2026-11-14 23:33）与本次 `/tmp` 重编的基线（480656）差 992 字节，因为源文件在此期间被别的进程改过。
**不要拿 479664 与 491768 直接相减。**

复现：

```sh
cd xnu-src/xnu-8796.121.2/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT
<真实编译命令，把 -fsyntax-only 换成 -c，源文件换成打过补丁的副本> -o /tmp/mod.o
xnu-deps/host/bin/otool -l /tmp/mod.o | grep -A3 "sectname __text"
```

### D.2 实测（需要手机）

分四次开机，每次只多一个变量。**关键是 R1 与 R2 的对比**。

#### R0 ｜ 基线，不传 `mali_gpueb`

```
mali=6 cpus=4 touchscreen=3 buttons=2 keylog=1 headset=1
```

期望：与现有 19 轮完全一致。新增输出只有两行：

```
mali: gpueb before the address spaces: off (mali_gpueb=0)
mali: gpueb after the failed start: off (mali_gpueb=0)
```

若这两行**没有**出现，说明补丁没生效或 `mali=` 级别不够（`gpueb_probe()` 在 `mali_bring_up` 里，
`mali=1` 就会走到）。若出现别的 gpueb 行，是错误的。

#### R1 ｜ `mali_gpueb=1`（本次的主实验）

```
mali=6 mali_gpueb=1 cpus=4 ...
```

期望输出，按顺序：

1. `mali: gpueb: mailbox mapped: sram 0x... ctl 0x... status 0x...` — 三个 KVA 都非 0。
   **若这行变成 `the mailbox will not map (sram 0 ctl ...)`**，说明 `ml_io_map` 拒绝了某个物理窗口。
   这本身就是一条结论（该窗口没有 iomem 条目 / 被别的驱动占了）。
2. `mali: gpueb before the address spaces: tx_busy 0 rx_pending ? (chan 1); status power_control ? ...`
   —— `power_control` 与 `shader_present` 是**这一轮最有信息量的两个数**：
   - 全 0 → 这块 RAM 自 LK 之后从没被 GPUEB 写过（或写过但没人初始化过）
   - `power_control=1` 且 `shader_present!=0` → GPUEB **在 Android 时代**写过这块，XNU 保留了内容
   - `power_control=1` 但 `shader_present=0` → 写过但枚举没过
3. `mali: gpueb: INIT_SHARED_MEM status 0x7c600000 size 0x400, debug 0x7c600400 size 0xc00`
4. 三条之一：
   - `mali: gpueb: INIT_SHARED_MEM answered, return 0` → 继续
   - `mali: gpueb: INIT_SHARED_MEM failed: <原因> (-N)` → **快速失败**，见下
   - `mali: gpueb: INIT_SHARED_MEM rejected (N)` → GPUEB 在，但拒绝了
5. 同理 `POWER_ON accepted / refused / failed`
6. `mali: gpueb: shader_present 0x... after POWER_ON` 或 `warning: shader_present still 0`
7. 然后 `mali: MCU status ...` 一路照旧

**「快速失败」怎么区分「GPUEB 不在」与「GPUEB 拒绝」—— 这是本次设计的核心：**

| 观察 | 含义 | 置信度 |
|---|---|---|
| `tx_busy 1` 且 1 s 后 `ETXSTUCK` | 上一条消息**从未被取走**，且我们**没写任何东西**。说明有人占着通道：要么 GPUEB 死了留下半条，要么 GPUEB 活着但卡住 | 高（无副作用，可安全重跑） |
| `tx_busy 0` → 写 SRAM → 1 s 内无 `rx_pending` → `ETIMEOUT` | **消息发出去了，没人应答**。这是"GPUEB 不在"最强的证据 | 高 |
| 收到应答但 `return < 0` | **GPUEB 在**，它读了消息并拒绝（例如 `status_size` 太小，固件有 `status mem size: 0x%x < status data size` 的报错串，协议 §5.3） | 极高（能应答就是活的） |
| 收到应答 `return >= 0` | GPUEB 在且接受 | 极高 |

也就是说：**只有第三种能证明 GPUEB 活着。** 第一、二种都不能区分"GPUEB 死了"和"GPUEB 活着但没空"。
这条边界必须在报告里写清楚，不要在第 2 种结果上宣称"GPUEB 不在"。

**耗时。** 最坏 2 × 1 s = 2 s（`GPUEB_TMO_INIT_SHARED_MS` / `GPUEB_TMO_POWER_CTRL_MS` 各 1000，
`gpueb-ipi.h:328-329`），比厂商的 10 s × 2 = 20 s（`gpueb-ipi.h:324` 的 `IPI_TIMEOUT_MS_DEFAULT`）短。这个 2 s 落在 `mali_thread` 的
`delay(2000*1000)` 之后，不影响别的子系统启动。

#### R2 ｜ `mali_gpueb=2`

与 R1 相同，唯一差别：IPI 失败时**不写 AUTO**，`start_mcu()` 返回 false。

期望多出一行：
```
mali: gpueb: power-on not confirmed; MCU_CONTROL=AUTO withheld (mali_gpueb=2)
```
然后 `mali: MCU status 0 (...)` —— 因为从未写过 AUTO。

**这一轮的价值**：把"IPI 失败"与"MCU 不启动"解耦。如果 R2 的 `MCU_STATUS` 仍是 1，
说明**有别的东西在写 AUTO**（或 `MCU_CONTROL` 的读回不代表写入），那是一条独立结论。

#### R3（可选）｜ `mali_gpueb=1 mali_mcuseq=1`

验证 H6 的位置选择：ENABLE 写在 IPI 之后。期望顺序是
`INIT_SHARED_MEM` → `POWER_CONTROL` → `mcuseq: after ENABLE write` → `after AUTO write`。
若顺序反了，说明补丁插错了地方。

### D.3 不该出现的输出

- 任何 `mali: gpueb ...: gpr[6] ...` / `kick ... -> ...` / `wdt_con ...` —— 这三个来自被删的
  `gpueb_watch()`，若仍出现说明补丁没落地。
- 任何 `mali: gpueb ...: not mapped` —— 同上。新代码在 `mali_gpueb=0` 时说 `off`，非 0 时说 `mailbox mapped`
  或具体的失败原因，不再有 `not mapped` 这个字符串。

---

## E. 回滚方案

### E.1 一行关掉

```
mali_gpueb=0        # 或者干脆不传（默认 0）
```

`mali_gpueb` 默认值是 0（`:466` 的 `static unsigned gMaliGpueb;`，BSS 零初始化），
所以**不传参数 = 关闭**。关闭后：

- `gpueb_map()` 不被调用（`:4525` 的 `if (gMaliGpueb)`，`:2352` 的 `gpueb_probe()` 提前返回）
- 不映射任何 GPUEB 窗口，不发任何 IPI
- `start_mcu()` 跳过 IPI 块，直接走原有的 `mali_mcuseq` / `AUTO`
- 唯一的行为差异：两行 `off (mali_gpueb=0)` 的 printf

**注意：** 这就是与基线的**唯一**差别。也就是说"回滚"不需要重编，只要改命令行。

### E.2 如果挂机（连参数都来不及传）

`mali_gpueb` 是命令行参数，改了它就要重刷 boot image。若怀疑挂机发生在参数解析之前
（不可能：`hakios_mali610_init()` 在 `:5059`，所有 map 都在 `mali_thread` 里、参数解析之后），
或者需要"一个绝对安全的镜像"，则把 H6 的 `if` 条件改成 `if (false && ...)` 重编 —— 一个字符。

更彻底的回滚是 `git apply -R` 整个补丁，或只回滚驱动那 9 个 hunk 保留 header（header 不被 `#include` 时零影响）。

### E.3 boot-arg 命名（任务要求核对的部分）

**核对结果：`mali_gpueb` 在驱动里已经存在，且已被别处登记。**

| 位置 | 内容 |
|---|---|
| `:466` | `static unsigned gMaliGpueb;` |
| `:4321-4323` | `if (gMaliGpueb) { gGpueb = ml_io_map(MALI_GPUEB_PHYS, ...); }` |
| `:5138` | `PE_parse_boot_argn("mali_gpueb", &gpueb, sizeof(gpueb))` |
| `configs/boot-args.lst:181` | `mali_gpueb	k` |
| `docs/boot-args.md` | **没有这一条**（`grep -c mali_gpueb` = 0） |
| `configs/symbols.tsv` | 需 `tools/boot-args.py --symbols` 重生成，当前 out of date |

**结论：不能新增同名参数，只能复用。** 本补丁选择复用并扩展语义（0/1/2），
理由与代价见 H8。若 reviewer 认为"同一个名字换含义"不可接受，替代方案是新增
`mali_gpueb_ipi=0/1/2`，代价是要动 `configs/boot-args.lst`、`docs/boot-args.md`、`configs/symbols.tsv`
三个文件，而其中后两个在基线上已经不一致（`tools/boot-args.py --check` 报两条 drift），
会把无关改动混进补丁。

**另外**：`mali_gpueb` 的**旧**语义（映射禁区）本身就是危险的，这个开关在基线上从未被安全地用过
（`docs/boot-args.md` 里连文档都没有）。复用它在语义上是净改善。

---

## F. 风险评估：如果 GPUEB 真的把 GPU gate 住了，为什么 19 轮实验里 CPU 能读 GPU 寄存器？

**这个矛盾没有完全解释。** 下面是能查到的实测证据，以及它们各自能推到什么、不能推到什么。

### F.1 矛盾的确切形状

被读到的、且读数是"合理值"的寄存器（都在 `0x13000000` 窗口内）：

| 寄存器 | 读数 | 出处 |
|---|---|---|
| `GPU_ID (0x000)` | `0xa8670004` | `work-notes-ipa-and-unknowns.md:404-406`（165 次读恒同值） |
| `L2_FEATURES` | `0x7130306` | `work-notes-gpu-m1.md:9` |
| `CSF_ID` | `0x40a0412` | 同上 |
| `MMU_FEATURES` | `0x2830` | 同上 |
| `AS_PRESENT` | `0xff` | 同上 |
| `THREAD_MAX` | `0x800` | 同上 |
| `SHADER_PRESENT (0x100)` | `0x130013` | 同上；`0x130013` = 6 核的两段各 19 |
| `MCU_STATUS (0x704)` | `1`（写 AUTO 后） | `work-notes-bus-protect.md:8` |
| `GPU_IRQ_RAWSTAT` | `0x100`（RESET_COMPLETED）、`0x200`（POWER_CHANGED_SINGLE） | `work-notes-mcu-peripheral-window.md:386-388` |

`0x13000000` 窗口能读、能写（`MCU_CONTROL` 写 2 读回 2），且 `GPU_CYCLE_COUNT` 在**跑**：

```
[49.020652] mali: clock check: cycle 0xc0eaeff6 -> 0xc0f022db, timestamp 0 -> 0
```
`work-notes-mcu-peripheral-window.md:391`

两次读差 `0x532e5` = 340709 个计数。若 core 域是 300 MHz（`work-notes-mcu-clock.md:284` 实测
Android 侧 `mfg_sel_0_ck = 299999955 Hz`），340709 个计数 = **1.14 ms**。

但探针代码里两次采样之间是 `delay(1000)`（`:4117`，即 1000 µs = 1 ms）。**1.14 ms 与 1 ms 相符**，
所以 cycle counter 确实在 300 MHz 上跑，这条自洽。我一开始把它算成"1.14 ms vs 10 ms 不自洽"，
是拿 `work-notes-mcu-clock.md:389` 里另一版探针（`delay(10)`）的注释去比了，那个比较是错的。

**这条实测的含义：`GPU_CYCLE_COUNT` 以 300 MHz 递增 ⇒ core 时钟域在跳。**

**关键在于它和上一条只隔 2 ms。** 同一次 run、同一段代码（`start_mcu()` 的收尾），
`[49.018030]` 读出 `shader ready 0 tiler ready 0 l2 ready 0x1`，`[49.020652]` 读出 cycle counter
在 300 MHz 上跑。也就是说：

> **在 2 毫秒的窗口里，"shader 没 ready"与"core 时钟域在跳"是同时为真的。**

这是 §F 这个矛盾的**唯一一条硬数据**，也是它最尖锐的地方：如果 shader 没 ready 是因为
core 时钟被 gate 了，那 2 ms 之后 cycle counter 不该递增。两者同时成立只有三种可能：
(a) cycle counter 不在 core 域；(b) `SHADER_READY=0` 的原因不是时钟；(c) 两者之间的 2 ms 里
时钟被打开了。这三种我都没有证据区分。

### F.2 三条候选解释，以及各自的证据边界

**解释 A｜寄存器块有自己的常开电源/时钟，GPUEB gate 的是执行单元（shader cores）而不是寄存器块。**

- 支持：`PWR_STATUS (0xF34)` 读到 `0x1fc`，即 bit2..bit8 = 7 个域 up（`work-notes-mcu-peripheral-window.md:394-395`
  与 `work-notes-mfg8-12-rail.md` 的逐位分析）。MFG1（MPU）在其中，所以寄存器块有电。
  而同一份 logbook 里 `SHADER_READY (0x140)` 读到 **0**（`work-notes-mcu-peripheral-window.md:388`，
  `shader ready 0 tiler ready 0 l2 ready 0x1`）。
  **注意这两条的时间戳不是同一时刻**：`PWR_STATUS` 在 `[38.22]`（上电阶段），`SHADER_READY` 在
  `[49.01]`（写 AUTO 之后）。同一次 run，但相隔约 11 秒，所以只能说"这次 run 里两个量分别是这样"，
  不能说"上电完成时 shader 还没起"。这是本节最弱的一环。
- 能推到：**"寄存器块上电"与"shader 上电"在这块板子上确实是两件事**，实测已经把它们分开了。
- 不能推到：这只说明"存在一个未上电的部分"，**不能说明**那个部分是 GPUEB 挡的。
  驱动自己上电 12 个域只成功 7 个（`7 of twelve domains answered`），这个失败同样能解释它，
  而且不需要 GPUEB 参与。
- 反证：`GPU_CYCLE_COUNT` 在跑（`0xc54a5a0a -> 0xc54f8cb0`，`work-notes-mcu-fetch-gating.md:382`），
  而 cycle counter 在 **core 时钟域**（`work-notes-mcu-clock.md:298` 明确写"它是 core 时钟域的自由运行计数器"）。
  如果 GPUEB 把 core 域 gate 了，这个计数器不该动。**这一条直接削弱解释 A。**

**解释 B｜GPUEB 管的是「由谁管电源」这个所有权标志，而不是电流本身；`power_control=0` 时它不管电源，也不阻止 AP 访问。**

- 支持：协议 §6.2（`power_control` 是使能标量，不是位域）。vendor 的 `gpufreq_power_control()`
  在 `power_control == 0` 时直接返回成功、根本**不发 IPI**（协议 §6.2 引 `gpufreq_v2.c:855-858`）。
  也就是说在 `power_control == 0` 的世界里，vendor 的路径认为"AP 自己管电源"，
  GPUEB 在这件事上是**旁观者**，不是 gate。**注意这是 vendor 的推断，不是硬件的保证**：
  上游 `gpueb_power.c` 不看这个标志，无条件发 IPI（协议 §6.5），本补丁照抄的是上游。
- 能推到：如果 `0x7c600000+0x60` 在 XNU 里读到 0，那 GPUEB 从来就没被要求管过这块 GPU，
  "GPUEB gate 住 GPU"这个假设的前提就不成立。
- 不能推到：`power_control` 的值**至今没有在 XNU 下读过**（`0x7c600000` 从未被映射，
  `grep -c 7c6` 在驱动里零命中）。这一条是**可验证但未验证**的，
  而本补丁的 R1 第 2 行输出正好就是它 —— **这是补丁的第一个真实收益，与 IPI 成功与否无关。**

**解释 C｜根本矛盾：如果 MCU 是因为没电源而不跑，那 19 轮里"写 AUTO 后 status=1"这件事也不该发生。**

- `MCU_STATUS=1` 在 panthor 的语义里是 `ENABLED`（`MCU_STATUS_HALTED = bit1` 未置位）。
  `work-notes-csf-boot-others.md:18` 说得很直接：**"公开世界里检索不到任何「MCU_STATUS=1 但固件不执行」的案例"**，
  且 Tyr（Rust for Linux 的 CSF 驱动）把启动成功定义成 `MCU_STATUS==Enabled && JOB_IRQ_RAWSTAT.glb()`，
  后来从"只看 status"演进成"双条件"（`:128-138`）。
- 这条的意思是：`status=1` 可能**根本不代表"MCU 在跑"**，只代表"状态机离开了复位"。
  如果是这样，"MCU 不执行"与"寄存器能读"、"cycle counter 在跑"三者**并不矛盾** ——
  因为 MCU 从来没有承诺过 `status=1` 就等于执行。
- 但这个解释**把矛盾从"电源"转移到了"启动语义"**，它不能反过来证明 GPUEB 无罪，也不能证明有罪。

### F.3 结论（不编理由）

1. **"GPUEB gate 住了 GPU，所以 MCU 不启动"这个假设，与 `GPU_CYCLE_COUNT` 以 300 MHz 在跑不相容** ——
   前提是 cycle counter 与 MCU 同域。`work-notes-mcu-clock.md:298` 明确写"它是 core 时钟域的自由运行
   计数器"，而 MCU 就在 core 域里。两次独立的 run 都看到它在跑（`0xc0eaeff6→0xc0f022db`、
   `0xc54a5a0a→0xc54f8cb0`），且第一段的速率与 300 MHz 相符（§F.1）。
   所以：**如果 GPUEB gate 的是 core 时钟，这个计数器不该动。它动了。**
   剩下的可能是"cycle counter 不在 core 域"或"GPUEB gate 的是别的门（复位/隔离而非时钟）"——
   **这两种可能我都没有证据，只能标为待查。**
2. **"寄存器可读但执行单元未上电"有实测支持**（`PWR_STATUS=0x1fc` 而 `SHADER_READY=0`），
   但**不能归因给 GPUEB**：驱动自己上电 12 个域只成 7 个，这个失败独立存在且更直接。
3. **`power_control` 的值从未被读过**，所以"GPUEB 是否被要求管这块 GPU"这个前提**未知**。
   这是本补丁能立刻回答的第一个问题，且答案与 IPI 是否成功无关（R1 第 2 行）。
4. **"MCU_STATUS=1 是否等于执行"本身可疑**（`work-notes-csf-boot-others.md:18`：上游无此案例）。
   若不等，则"19 轮能读寄存器"根本不是矛盾 —— 矛盾在别处。

**因此：这个矛盾未解释，是下一个实验（下称 E6）的一个风险点。** 本补丁的价值不在于"证明 GPUEB 是原因"，
而在于**把三个此前未知的量变成已知**：`power_control`、`shader_present`、以及"两条命令是否有应答"。
如果 R1 的 `power_control` 读到 0，那么"GPUEB gate 住 GPU"这条假设应当**直接被放弃**，
19 轮的失败原因要回到 `PWR_STATUS 0x1fc vs 0x3ffc`（12 域只起 7 个）这条更硬的证据上。

---

## G. 未验证清单

按影响排序：

| # | 未验证项 | 怎么验证 | 影响 |
|---|---|---|---|
| 1 | `0x7c600000` 这块 RAM 在 XNU 下能否安全 map 与读 | R1 第 2 行 | 若读挂机，本补丁的 probe 部分要单独关掉（`mali_gpueb` 之外再加开关） |
| 2 | GPUFREQ 的 RX slot 是否真是 47（协议 §9 D1 列为最高优先级） | R1 收到应答时 ack 是否合理 | 若 slot 错，会读到垃圾 ack，可能误判"拒绝" |
| 3 | `0x13c62000` 页在 XNU 下读是否安全（mbox bank 时钟是否常开） | R1 第 1-2 行 | 若挂机，说明 bank 也被 gate，方案要重做 |
| 4 | ~~编译产物大小变化~~ **已实测**：`__text` +988 字节（+3.1%），`.o` +11112（见 §D.1） | — | 无 |
| 5 | `ml_io_map` 对 `0x13c5fd80`（非页对齐）返回的 VA 是否需要手工加 `& 0xfff` | 读 `osfmk/arm/io_map.c:95`（`start_offset = phys_addr - trunc_page(phys_addr)`）与 `:132`（`return start + start_offset`）。**已核对源码，不需要手工加偏移** | 已解决 |
| 6 | `GPU_CYCLE_COUNT` 与 MCU 是否同一时钟域（§F.2 的矛盾） | 读 `work-notes-mcu-clock.md:298` 的原始出处，或 R1 时同读两者 | 决定 §F 的解释 A 是否成立 |
| 7 | `tools/drivers-check.py` 基线红（`drivers/usb`）是否会掩盖本补丁引入的新问题 | 加 `drivers/map` 一行前后各跑一次，比对问题数 | 低 |

---

## H. 附：验证记录

全部在 `/tmp` 的副本上做，`/home/lizi/IOS/hakios-mtk` 未被写入。

| 项 | 命令 | 结果 |
|---|---|---|
| 补丁可应用（5253 版基线） | `git apply --check` | 通过 |
| 应用后一致性（5253 版） | `md5sum` 对比应用结果与作者验证副本 | 相同：`c5d41235ece6adc73e8bee1e07f0358e` |
| 补丁可应用（5255 版，收尾时） | `git apply --check -v` | 通过，9/9 hunk 成功，offset +1/+2，无 fuzz |
| 语法（5253 版） | `xnu-clang -fsyntax-only`（真实构建命令，替换源文件） | 退出码 0 |
| 目标文件（5253 版） | 同命令 `-c -o`，`otool -l` 读 `__text` | 基线 31588 → 32576，**+988 字节** |
| 语法（5255 版） | 同上，对应用后的当前源文件 | 退出码 0 |
| 警告（真实 warning 集） | 同上 + `makedefs/MakeInc.def:136-173` 的 `WARNFLAGS_STD`（`-Weverything … -Werror`，含 31 个 `-Wno-*`，逐条抄自该文件 `:136-173`） | 0 条诊断；基线与打补丁后都是 0，所以补丁没有引入任何新警告 |
| 警告（宽松集，对照） | 同上 + `-Wall -Wextra` | 0 条诊断 |
| header 独立编译 | `xnu-clang -std=c11 -Wall -Wextra -fsyntax-only`（只 include header）；`gcc` 同 | 两者退出码 0 |
| header 逐字节一致 | `md5sum` 补丁产出的 header vs `findings/gpueb-ipi.h` | 相同：`78aa488140e71c2b5d1f95027edae8e6` |
| header 字段布局 | `gcc -std=c11` 跑 `gpueb_build_init_shared_mem()` | `w[2]=0x7c600000 w[4]=0x7c600400 w[6]=0x400 w[7]=0xc00`；`offsetof(status_size)=24`、`sizeof=32` |
| 源文件无残留 | `grep MALI_GPUEB` / `grep gpueb_watch` | `MALI_GPUEB` 0 条；`gpueb_watch` 只剩注释里 1 处（说明它变成了什么） |
| 基线行数/md5 | `wc -l` / `md5sum` | 5253 / `0608664f4fea011658b6ab65c8d1b666`（收尾时已变成 5255 / `255a08d4…`） |
| patch 自检脚本 `--selftest` | `python3 docs/headset-wip/gpueb-patch-check.py --selftest` | **6 of 6 assertions passed**，退出码 0 |
| patch 自检脚本对真实补丁 | `cd /home/lizi/IOS/hakios-mtk && python3 …/docs/headset-wip/gpueb-patch-check.py …/GPUEB-INTEGRATION.patch` | **ok**，退出码 0；10 条 note：9 条是 offset +1/+2（文件被并发改过），1 条是 `drivers/map` 括号中性 |

patch 全文与自检脚本：

- `GPUEB-INTEGRATION.patch` — 完整 unified diff，1009 行，3 个文件、11 个 hunk
- `../gpueb-patch-check.py` — 自检脚本（在上一级目录，与其它 `*.py` 工具同处）
