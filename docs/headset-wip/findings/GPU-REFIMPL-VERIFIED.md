# GPU 参照实现核实：三个「可疑差异」全部为伪

> 2026-11-15 02:05。用户提供了闭源 HakiOS 开发者的线索，顺藤摸到两个已跑通的开源实现。
> DSH 逐条核实了先前怀疑的差异，**三条全部撤回**。本文记录核实过程与真实剩余方向。
> 每条结论追到 `文件:行号`；推断标 [推断]。

---

## 0. 一句话

`Ken-u/Mali`（XNU，arch 10.8，已 device-verified）确实是重要参照，但**我们与它的差异不在于
我先前怀疑的那三处**（`AS_TRANSCFG` / 固件大小 / 章节数）。三处都是伪差异。
真实剩余方向已由我们自己早先的 `work-notes-csf-boot-others.md` 列出，其中【1】PDC 已实现，
【2】`CSF_CONFIG` 回读与【3】`GPU_CYCLE_COUNT` enable 仍未做。

---

## 1. 找到的两个参照实现

### 1.1 `Ken-u/Mali` —— Mali-G610 CSF 的 **XNU 内核驱动**（arch 10.8）

仓库描述原文（`api.github.com/repos/Ken-u/Mali`）：
> Mali-G610 (CSF) kernel driver for UniXNU: power, firmware, MMU, command streams and user client

`README.md` 原文：
> Mali-G610 (CSF, Valhall, **arch 10.8**) kernel driver for UniXNU on RK3588. It powers the GPU,
> loads the CSF firmware into the MCU address space, builds AArch64 4 KiB GPU page tables and
> command-stream groups, and exposes a Panthor-style `IOUserClient` that the PanVK-based Metal
> stack in MaliMetal drives.

本地 clone：`/tmp/kenmali`（`Mali.cpp` 177614 B，`docs/notes.zh-CN.md` 348 行）

M1 实测（`docs/notes.zh-CN.md:67-77`）：
```
firmware: header 0.3, 274432 bytes, entries end at 960
section 0..6 loaded（共 7 段 + 1 段 protected-mode 跳过），36864 B 页表，root 0x5e802000
AS0 enabled: transtab 0x5e802000 transcfg 0x420001c6 memattr 0x4c4c4c4c9c4c9f4c
MCU booted (MCU status 1, job irq raw 0x80000000)
```

**已知缺口（README 原文）**：
> there is no DVFS and **no interrupt-driven completion yet (waits poll)**

→ 它也是纯轮询。这不是差异点。

### 1.2 `bkaneko/xaga-mali-r54-port` —— **xaga 专用**（Android 侧）

`README.md` 原文：
> 把 MediaTek **Mali-G610 (MT6895)** 的 GPU 内核驱动从原厂 **r32p1** 升级到 **r54p1**，
> 并配套把 **GED** 和 **gpueb** 从 6.12 源码 backport 到 Linux 5.10 的完整记录。
> 目标设备：Redmi Note 11T Pro / POCO X4 GT（代号 **xaga**，MT6895，Mali-G610 MC6）

本地 clone：`/tmp/r54`。含**真机固件** `firmware/mali_csffw.bin`（282624 B）与
`gpueb_stock.bin`（1048576 B）。文档已拷入 `raw/xaga-mali-r54/`。

---

## 2. 三处怀疑：全部撤回

### 2.1 ❌ 撤回：`AS_TRANSCFG` 的 `0x1c0`

**我先前的怀疑**：`Ken-u` 写 `0x420001c6`，我们写 `0x42000006`，差 `0x1c0` = `INA_BITS`。

**核实结果**：两种取值都成功过，不是原因。

`work-notes-csf-boot-others.md:322` 原文：
> 两条最大的"参数分歧"都被证明无所谓：transcfg 的 INA_BITS（`0x420001c6` vs `0x42000006`）
> **双方都成功**；memattr 编码（io-pgtable MAIR 语义 vs AS_MEMATTR 索引语义）**双方都成功**。

`work-notes-csf-boot-others.md:310` 的完整对照：
| 实现 | 值 |
|---|---|
| `Ken-u/Mali` | `0x420001c6`（含 `(55 - vaBits) << 6`） |
| panthor | `0x420001c6`（`AS_TRANSCFG_INA_BITS(55 - va_bits)`，`panthor_mmu.c:788-795`） |
| kbase（MTK 原厂） | `0x42000006`（**kbase 全树无 `INA_BITS` 定义**） |
| 我们 | `0x42000006` |

**位域换算已核实**（`panthor_mmu_regs.h:49-66`）：
```c
#define AS_TRANSCFG_ADRMODE_AARCH64_4K  (6 << 0)      /* 0x00000006 */
#define AS_TRANSCFG_INA_BITS(x)         ((x) << 6)    /* (7<<6) = 0x000001c0 */
#define AS_TRANSCFG_PTW_MEMATTR_WB      (2 << 24)     /* 0x02000000 */
#define AS_TRANSCFG_PTW_RA              BIT(30)       /* 0x40000000 */
```
合计 = `0x420001c6`。`INA_BITS(7)` 对应 `va_bits = 48`（`panthor_mmu.c:782` 的 `55 - va_bits`）。

**这是一条真实的技术差异，但不是失败原因** —— 因为原厂 kbase 用 `0x42000006` 也能跑通。

### 2.2 ❌ 撤回：固件大小差异

**我先前的怀疑**：我们 258048、`Ken-u` 274432、xaga 原厂 282624，三份全不同 → 我们的固件可能不对。

**核实结果**：我们和 `Ken-u` 的固件**字节完全一致**。

`work-notes-csf-boot-others.md:332` 原文：
> 我方固件 `hakios.mali610_fw.h`（258048 B，sha256 `de22d2fc…`，
> **与 `/tmp/kmali/mali_csffw.inc` 字节完全一致**）

所以 `Ken-u` README 里的 `274432` 不是文件大小（它加载的固件与我们同一份），
很可能是它 `MMU_FW_LENGTH` 之类的内存布局数字，或另一分支版本。**「274432」的含义未核实。**

xaga 原厂 282624（`bkaneko` 从 `/vendor/firmware/` 拉的，UK 1.38）确实与我们不同，
但那是 **r54p1 用户态配套的固件**，与我们的 r32p1 时代固件不是同一版本 —— 版本不同不等于错。

### 2.3 ❌ 撤回：章节数差异

**我先前的怀疑**：`Ken-u` 「7 段 + 1 段 protected 跳过」，我们「10 段全映射」。

**核实结果**：我们的实际行为与它一致 —— 10 条 type=0 里 3 条 PROT 被跳过，**实加载 7 段**。

`work-notes-csf-boot-others.md:355` 原文：
> **⇒ 10 条 type=0，3 条 PROT 被跳过，实加载 7 段。** 与你给的"7 个 iface section"吻合。

我们的固件容器实测（同文件 `:340-352`）：
| # | va 区间 | flags | 语义 |
|---|---|---|---|
| 1 | `0x0`–`0x1000` | `0x09` | RD\|CACHED（**无 EX**）`config` |
| 2 | `0x1000000`–`0x1010000` | `0x0d` | RD\|EX\|CACHED |
| 3 | `0x2000000`–`0x203e000` | `0x8000000b` | ZERO\|RD\|WR\|CACHED |
| 4,5 | 栈区 | `0x8000000b` | ZERO\|RD\|WR |
| 6 | `0x2800000`–`0x2840000` | `0x80000009` | ZERO\|RD\|EX |
| 7,8,9 | `0x3000000`–`0x3040000` | `0x2b` | RD\|WR\|CACHED\|**PROT** |
| 10 | `0x4000000`–`0x4094000` | `0xc000001b` | SHARED\|ZERO\|RD\|WR\|coherent `shared` |

所以「10 段全映射」是我对日志的误读。**实际是 7 段加载 + 3 段 SKIP。**

---

## 3. 核实中确认的四条硬事实

### 3.1 取指入口 = AS0 的 VA 0，没有任何寄存器决定它

`work-notes-csf-boot-others.md:330`：
> **没有任何寄存器决定入口地址。MCU 复位后的 PC 由 Cortex-M7 的硬复位行为决定：
> 从 AS0 的虚拟地址 0 取 MSP，从 VA 4 取 Reset 向量。**

VA 0 的向量表实测（`:412-420`）：
```
00000402  ed180001  b1190001 ...
```
- `0x02040000` = initial MSP（= section #5 顶端）
- `0x010018ed` = Reset，奇数 ⇒ Thumb 位 ⇒ 真实地址 `0x010018ec`，**落在 section #2（有 EX 位）内** ✅

**完全对应我们日志里的 `reset vector sp 0x2040000 pc 0x10018ed`。** 所以我们的固件布局与它就绪状态一致。

### 3.2 `status=enabled` 是公开世界里不存在的失败状态

`work-notes-csf-boot-others.md:520-527` 原文：
> 全世界公开可见的 CSF 启动失败，只有三种状态，**没有一种是 `enabled`**：
> | 状态 | 案例 | 根因 |
> | `status=fatal` + `VA 0x0` fault | mt8196 / Mali-G925 | L2 的 `ASN_HASH` 未按 SoC 配置 |
> | `status=fatal` | suspend/resume 保留 `MCU_CONTROL=AUTO` | 缺完整上电/复位序列 |
> | `status=disabled` | NXP i.MX95 + Mali-G310 | 未查明 |

**我们是 `enabled`。** 这条要么说明我们的观察有误，要么说明我们遇到的是一个没有公开先例的状态。

### 3.3 「静默翻译失败」是这个症状类的已确认成因

`hakios.mali610.c:610-618` 注释（我们自己的树）：
> PTE_PAGE 曾整段 M1 缺失，所有叶描述符被 MCU 读成 invalid，"**a translation failure that
> raises no MMU interrupt and writes no fault address** -- exactly the silence this driver spent
> days chasing. The firmware's own vector table at VA 0 was unmapped in that sense, so **the reset
> fetch never happened and no instruction ever ran**."

**该 bug 已修**（`PTE_PAGE` 在 `:621`，叶描述符 `bits[1:0]=0b11`）。
但它证明：**「零 fault + 零执行 + 哨兵不变」确实可以由「MCU 尝试取指但被静默拒绝」造成。**
所以不能假设「MCU 根本没尝试」。

### 3.4 所有实现都不给 VA 0 的 config 段 EX 权限，但都成功

`work-notes-csf-boot-others.md:480-492` 原文：
> 所有成功实现都把我们这个 section #1（`va 0x0-0x1000`，flags `0x9`，无 EX 位）
> 映射成"不可执行"：panthor 加 `NOEXEC`、kbase 加 `GPU_NX`、`Ken-u` 加 `kPteXn`、我们加 `PTE_XN`。
> **然而它们都成功了。** ⇒ [推断] MCU 通过 AS0 取指时走地址翻译，但**不检查 PTE 的 XN 位**。

→ 排除「VA 0 页被标 XN」这个怀疑方向。

---

## 4. 我先前一个过期结论的更正

`work-notes-csf-boot-others.md:640` 说：
> 【1】PDCv2 的 33 位 `active_pwrctl_en` 授权从未写过 —— 最可疑的空白

**这条已过期。** 当前驱动**已经实现**：
- `hakios.mali610.c:330-337`：`MALI_PDC_CORES 10u`、`MALI_PDC_SC_BASE 0x400u`、
  `MALI_PDC_SC_STEP 0x18u`、`MALI_PDC_SC_EN (1u << 0)`、`MALI_PDC_SC_RSV_BIT (1u << 31)`
- `:1867-1869`：循环写 `MALI_PDC_SC_BASE + i * MALI_PDC_SC_STEP`，置 `MALI_PDC_SC_EN`
- `:1877`：写 `MALI_PDC_GROUP_EN`
- `:1886-1891`：`pdc_report()` 回读 sc0 和 sc9
- `mali_pdcauto=1` 开关（`:557`、`:5160`）
- `mali_nopdc=1` 跳过（`:553`）

而 `mali_nopdc=1` 的实验结论（记录在 `findings/FINDING-pdc-early-2026-11-14-2240.md`）：
让 MFG5/6/10/12 从 `0x100d` → `0xc000000d`、`0xF3C` 从 `0x4` → `0x3ffc`，**但哨兵仍 `0xff5aa5f0`**。
→ PDC 是真问题，不是充分原因。

---

## 5. 真实剩余方向（2026-11-15 02:10 逐条核实后的最终结论）

### 5.1 【2】已排除：厂商冷启动 trace 里这四个寄存器就是写 0

我先前的怀疑（「我们写 0，没确认硬件默认值」）**不成立**。

`hakios.mali610.c:3500-3505` 原文，引用手机自己的冷启动 trace：
```
The phone's cold-start trace has the same four writes back to back, all zero,
at records 269-272 -- after the soft reset and before the address space is programmed:

	269 w 0x0f04 = 0x00000000	SHADER_CONFIG
	270 w 0x0f08 = 0x00000000	TILER_CONFIG
	271 w 0x0f0c = 0x00000000	L2_MMU_CONFIG
	272 w 0x0f00 = 0x00000000	CSF_CONFIG
```

同文件 `:3506-3507`：这四条**只出现在复位路径**，bootloader 已经把块配好了。

→ **厂商也写 0。** 与 `work-notes-kbase-fwload.md:606`「真机 debugfs `quirks_gpu = 0`」
一致。`work-notes-gpu-status.md:96` 的「没回读确认过」这个疑虑到此消除。

### 5.1b 🔴 三条更正（Codex 2026-11-15 02:14 指出，DSH 核实源码后全部接受）

| 我先前说 | 真相 | 依据 |
|---|---|---|
| 「timestamp 使能没实现」 | **已实现**：`mfg_set_timestamp()` 在 `:2665` 定义、`:4497` 调用，写 `0x13FBF130 = 0x00000003` | 我只 grep 了字面量没读函数，误判 |
| 「3 条 PROT 段跳过、实加载 7 段」 | **未跳过**：`:1069-1072` 只打印警告，`:1073` 继续映射 | 那是 `work-notes` 旧结论，与当前代码不符 |
| 「274432 是固件文件大小」 | **是 `sizeof`** | 我本来就标了「未核实」 |

**→ 5.3 里「timestamp 0 -> 0 是测量缺陷」这条也随之作废。** 那个函数存在，历史日志那行是在加它之前。

### 5.1c 新发现：PROT 段三种实现全不同

核实 PROT 时发现的对比（`panthor_fw.c:588-592` 原文）：

```c
if (hdr.flags & CSF_FW_BINARY_IFACE_ENTRY_PROT) {
    drm_warn(&ptdev->base,
         "Firmware protected mode entry is not supported, ignoring");
    return 0;
}
```

| 实现 | 对 PROT 段的做法 |
|---|---|
| **panthor** | **明确跳过**（`return 0`，不映射）—— 且它成功了 |
| kbase | 用 `kbase_csf_protected_memory_alloc` 分配并映射 |
| 我们 | 用**普通页**映射（`:1069-1072` 打印 "mapped with ordinary memory"） |

我们 `:1057-1067` 的注释推理是：
> leaving them unmapped means the first store to a global variable lands on nothing.
> That is a plausible reason for a core that reports ENABLED, never writes its version word
> and never faults.

**但 panthor 证明不映射也能起来。** 可能的原因是固件差异（panthor 的固件是 RK3588 的，
我们的是 MTK 的，PROT 段用途可能不同）。

我们固件的 PROT 段：VA `0x3000000-0x3040000`，共 **256 KB**，镜像里无数据，注释说是 `.bss`。
如果 MCU 起来后第一件事是写某个全局变量、而它落在这 256 KB 里，那么映射与内存属性就有意义。

→ **标「未验证」**，不作为结论。

### 5.2 【3】确认存在：`kGpuCmdCycleCountStart` 宏定义了，但**零引用**

`work-notes-mcu-clock.md:330-336` 明确列出三件要做的事，其中②是：
> 按 panthor / DDK 的做法显式启动 cycle counter（`GPU_COMMAND = CYCLE_COUNT_START`）

宏也确实定义了（`hakios.mali610.c:335-336`）：
```c
#define kGpuCmdCycleCountStart  0x0103u   /* GPU_CMD_DEF(CODE_TIME=3, TIME_ENABLE=1) */
#define kGpuStatusCcActive      (1u << 6)
```

**但 `grep -n "kGpuCmdCycleCountStart\|kGpuStatusCcActive"` 全文件零命中** —— 两个宏从未被引用。

我们实际只写：
- `:3480` `mwr(kGpuCmd, kGpuCmdSoftReset)` = `0x101`
- `:3583` / `:3774` `mwr(kGpuCmd, kGpuCmdFlushCaches)` = `0x3304`

**从来没有 `0x103`，也从来没检查过 `GPU_STATUS(0x034)` bit6 `CYCLE_COUNT_ACTIVE`。**

`work-notes-mcu-clock.md:268-271` 原文：
> panthor / kbase 都是先写 `GPU_COMMAND(0x030) = 0x0103`
> （`GPU_CMD_DEF(CODE_TIME=3, TIME_ENABLE=1)`，`mali_kbase_gpu_regmap_csf.h:216`），
> 再读 `GPU_STATUS(0x034) bit6 CYCLE_COUNT_ACTIVE` 确认生效，才去读 `0x090/0x094`。
> **我们跳过这两步直接读。**

**后果**：我们「`GPU_CYCLE_COUNT` 在跑 ⇒ 时钟已锁」这个结论**可能不成立**。
未 enable 时计数器是否照常递增，取决于硬件设计——这一点**没有核实过**。

→ 这是目前唯一一条「计划做了、文档写了、代码没接」的缺口。成本：2 行写 + 1 行读。

### 5.3 其余项

| # | 项 | 状态 |
|---|---|---|
| 【4】 | `GPU_L2_CONFIG(0x48)` / `ASN_HASH(0x2C0)`：mt8196 的失败根因，但那段代码对 arch<11 直接 return（我们 10.8） | 不适用 |
| 【5】 | 固件在物理内存上是否真接在 MCU 取指路径 | `work-notes` 说已做 |
| 【6】 | ~~`GPU_TIMESTAMP` 使能没写~~ | **已实现**（`mfg_set_timestamp()`，见 §5.1b）。这条撤回 |

→ **§5.3 原先把 `timestamp 0 -> 0` 判为「测量缺陷」也作废** —— 使能函数是存在的，
历史日志那行是加它之前的读数。Codex 说 NOPDC 实测 timestamp 在走。

---

## 6. GPUEB 一手资料（`bkaneko/firmware/README.md`）

这是我们从没拿到过的 xaga GPUEB 硬事实，原文：
> GPUEB 是 MT6895 上的 **RISC-V (RV33)** 协处理器，**GPU 的频率、电压、电源域全部由它控制**。
> AP 只能通过 IPI 发请求，固件决定实际频率。

固件头（**未加密，无 hash 无签名**）：
```
0x00: 88 16 88 58      magic
0x04: 2c ef 01 00      size
0x08: "tinysys-gpueb-RV33_A"
```
实际数据约 96 KB（`0x0–0x17000`），其余 `0xFF` 填充。

GPU OPP 表在偏移 `0x15d60`，6 项 × 24 字节（电压单位 0.1 mV）：

| idx | freq (kHz) | volt | vsram | posdiv | 说明 |
|---|---|---|---|---|---|
| 0 | 950000 | 78750 | 78750 | 2 | 固件永不选 |
| 1 | 880000 | 78750 | 75000 | 2 | 实际最高 |
| 2 | 800000 | 72500 | 75000 | 2 | |
| 3 | 610000 | 65000 | 75000 | 2 | |
| 4 | 430000 | 60000 | 75000 | 2 | |
| 5 | 350000 | 57500 | 75000 | 3 | |

→ **坐实「GPUEB 独占控制 GPU 电源域」**。但注意：这是 **Android/Linux** 侧的控制模型
（AP 不 attach genpd，由 GPUEB 管），**我们的裸机是自己上电 12 域**，两者模型不同，不能直接套。

---

## 7. 待扫的开发者（子代理失败，未完成）

用户提供：`mytiantian001`、`bkaneko`、`zhaoqing412`、`woaphone`、`Ken-u`

已确认存在但**未读**的仓库：
- `bkaneko/panvk-mtk-driver` (10★) —— panvk kbase (MTK Mali) Android Vulkan
- `mytiantian001/Mu-Silicium` —— **Project Mu UEFI Ports for MediaTek MT6895 Family**
- `zhaoqing412/xaga-mt6895-k6.12` —— xaga 的 ACK 6.12
- `zhaoqing412/fenrir` —— 控制 MTK bootchain
- `Ken-u/UniXNU-evb10` —— RK3588 设备仓库（Mali 合成 50 fps 实测）
- `UniXNU` 组织 —— 官方组件目录

（派出的两个子代理 `bda216ff`、`81edd5ba` 均因限流失败，无产出。此清单待续。）
