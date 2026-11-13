# 交接 2026-11-13 17:25 · 下一条：AS0 写序对齐 Android

## 一、本轮定性（690，已完成）

**`MCU_STATUS` 是单向锁存，作为判据彻底作废。**

```
after DISABLE ctl 0 st 1 | after re-read st 1
second DISABLE ctl 0 st 1 (after 1000 us)
bounded re-read ctl 0 st 1 after 10000 polls (delay 10 us, limit 10000)   ← 100 ms 零变化
```

四种写入，status 全是 1：ENABLE(1) / AUTO(2) / doorbell / **DISABLE(0)×2**；而 AUTO 之前是 `0`。

→ 它不是常量（有 0→1），也不是状态镜像（DISABLE 清不掉）。**是单向锁存。**

**有效判据只剩三个**：witness store（`0xdeadbeef,0x22`）、`JOB_INT_STATUS` bit31、固件版本字 ≠ 哨兵。
`debug_top 0x4→0x1b` 只能说明"总线忙"，不能说明核心在跑。

详见 `out/round-690-status-verdict.md`。

## 二、下一条：`mali_assorder=1`（AS0 内部写序）

### 为什么是它

Android 冷启动实录 `hist_boot.txt` rec 0-5 的 AS0 写序是：

```
rec 0: w 0x02430 = 0x42000006     TRANSCFG_LO
rec 1: w 0x02434 = 0x00000000     TRANSCFG_HI
rec 2: w 0x02400 = 0x44b49000     TRANSTAB_LO
rec 3: w 0x02404 = 0x00000001     TRANSTAB_HI
rec 4: w 0x02408 = 0x888d8f88     MEMATTR_LO
rec 5: w 0x0240c = 0x00084c8d     MEMATTR_HI
rec 7: w 0x02418 = 0x00000001     COMMAND = UPDATE
```

**先 TRANSCFG，再 TRANSTAB，再 MEMATTR，最后 UPDATE。**

我们的 `as_enable()`（`hakios.mali610.c:2879-2883` 的 `AS_STEP` 序列）是
**TRANSTAB → MEMATTR → TRANSCFG → UPDATE**，顺序反了。

**这条从未单独试过。** trial 400 只移动了 AS0 **块**的位置（`as0first`），没改块内写序。

### 补丁形态

```c
/* 开关声明（:520 附近的开关区） */
static unsigned gMaliAsOrder;   /* mali_assorder=1 writes AS0 in the phone's order: TRANSCFG, TRANSTAB, MEMATTR */

/* 把 :2880-2883 的四行 AS_STEP 改成条件顺序 */
	if (gMaliAsOrder) {
		AS_STEP("transcfg", gpu_wr64(asReg(as, kAsTranscfg), transcfg));
		AS_STEP("transtab", gpu_wr64(asReg(as, kAsTranstab), transtab));
		AS_STEP("memattr", gpu_wr64(asReg(as, kAsMemattr), MALI_MEMATTR));
	} else {
		AS_STEP("transtab", gpu_wr64(asReg(as, kAsTranstab), transtab));
		AS_STEP("memattr", gpu_wr64(asReg(as, kAsMemattr), MALI_MEMATTR));
		AS_STEP("transcfg", gpu_wr64(asReg(as, kAsTranscfg), transcfg));
	}

/* 参数解析（:5155 附近） */
	PE_parse_boot_argn("mali_assorder", &gMaliAsOrder, sizeof(gMaliAsOrder));
```

注意：`AS_STEP` 宏每次都会清 MMU latch 并打印，所以两种顺序下 **store 数不变**（仍 82），
只是三条写的先后换位。这一点便于核对补丁真的生效：`AS0 step` 五行日志的顺序会变。

配套：`mali=5 mali_cold=1 mali_assorder=1 mali_witness=1`，其余新开关全 0。

### 判据

| 观测 | 结论 |
|---|---|
| `AS0 step` 五行顺序变成 `idle, transcfg, transtab, memattr, update`，且 witness 出现 store | **找到根因** |
| 顺序变了、`AS0 step update` 后的 `mmu irq` 仍是 `0x10000`、witness 仍哨兵 | 写序无关，这条彻底关闭 |
| 日志里 `AS0 step` 顺序没变 | 补丁没生效，查 boot-arg |

### 风险

**零。** 只改同一函数内三条写的先后顺序，不增减任何写、不碰新地址。

## 三、如果这条也失败，剩下什么

| 候选 | 说明 |
|---|---|
| `mali_skipprot=1`（跳过 `.bss` 三节映射） | 对齐 Panthor `panthor_fw.c:586-590`。**风险**：如果固件真写 `.bss`，不映射会 fault（但我们观测 fault 恒 0，能顺便区分） |
| 段加载顺序（先 shared 后 code） | 从未试过，优先级低 |
| 换固件（UK 1.38，282,624 B） | 最后手段 |

## 四、读哪几个文件

| 文件 | 读哪节 |
|---|---|
| `out/round-690-status-verdict.md` | 全文（92 行）—— 为什么 `MCU_STATUS` 作废 |
| `out/p0-patches.md` | 搜 `P1-4` / `mali_assorder` |
| `out/untried-variables.md` | **L 类（L1/L2）** |
| `/home/lizi/IOS/work-xaga/codex/evidence/hist_boot.txt` | rec 0-7（AS0 写序原文，`out/round-1-support/android-0-200.tsv` 也行） |

## 五、硬规矩（不变）

- 手机只有你能碰，一次一个变量
- 禁读 `0x13bfee00`、`0x13c40000`、`0x13F91034`
- 每轮结束恢复原厂 boot_a，MD5 = `a63b2e35a7c2522cb24a6a10332b9bc8`
- 推送：`export https_proxy=http://127.0.0.1:7897` → `cd /home/lizi/IOS/repo/hakios-mt6895 && git push origin main`
- 驱动：`/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c`
- 报告：`out/round-16.md`
- **取证**：`run-once.py` 已加完成标志检测（`log_complete` 字段），拉日志前会等 `the start loop ended`
  或 `witness: read1`，最多 150 s。**看到 `log_complete: false` 就别当结果用。**
