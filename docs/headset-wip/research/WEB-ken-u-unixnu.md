# Ken-u / UniXNU 仓库扫荡：对 Mali-G610 CSF 启动有用的东西

扫描时间：2026-11-15 02:03–02:25（+08:00）
扫描对象：`Ken-u` 用户全部 63 个仓库、`UniXNU` 组织全部 32 个仓库、`topic:unixnu` 155 个仓库、
`Ken-u/Mali` 的 fork（1 个）与 contributor（1 个）
本报告所有结论都追到 `文件:行号` 或 URL；不确定的标「未验证」。

---

## 0. 最重要的结论（先读这一节）

### 0.1 没有任何一条发现能解释「MCU_STATUS=1 但 GLOBAL_IF 不置位」

**这一节是负结论，但它是本报告最重要的一节。**

我把任务里假设的五条线索全部追到了源头，结论是：**它们都不能解释当前症状**。
理由逐条列在下面，每条都给了可复核的原文。

| # | 原本的假设 | 追查结果 | 证据 |
|---|---|---|---|
| 1 | 「我们没写 `CSF_CONFIG(0xF00)`，所以 MCU 不执行」 | **不成立**。`Ken-u/Mali` 全文对 `0xf00` 零命中，它也能启动 MCU。panthor 也从不写 `0xf00`（v6.12 全文零命中） | `Ken-u/Mali` grep `0xF00` = 0 命中；`panthor_fw.c/gpu.c/mmu.c` grep `0xf00` = 0 命中 |
| 2 | 「我们漏了 `hw-issues`」 | **不成立**。Ken-u 驱动全文对 `hw_issue`/`quirk`/`errata` 零命中 | `grep -i "hw_issue\|quirk\|errata" /tmp/kenmali/Mali.cpp` = 0 命中 |
| 3 | 「`ASN_HASH(0x2C0)` 没写」 | **不成立**（对 G610 而言）。上游 panthor 的 ASN hash 只对 arch ≥ 11 生效，G610 是 arch 10.8 | `tree/.../hakios.mali610.c:3196-3205` 已实测：写入后 `l2_config` bit24 仍为 0、三个 hash 字仍为 0 |
| 4 | 「`L2_CONFIG(0x48)` 没写」 | **不成立**。Ken-u 全文对 `0x48` 零命中 | `grep "0x48\b" /tmp/kenmali/Mali.cpp` = 0 命中 |
| 5 | 「PDC / MTCMOS 是 MTK 特有的缺口」 | **平台差异，不是当前症状的原因**。RK3588 的电源域是 PMU 寄存器直接控，MT6895 走 PDC/SCPMI；两者机制不同，但 `PWR_STATUS=0x3ffc` 已证明 MFG1-12 全 up | `docs/headset-wip/findings/FINDING-pdc-early-2026-11-14-2240.md`（本项目已有） |

### 0.2 找到了一条**可复核的寄存器语义差异**，但它不是「我们错、它对」

任务描述里有一处需要更正，否则会误导后续排查：

> 「`JOB_INT_STATUS(0x100c)` bit31 `JOB_INT_GLOBAL_IF` 从不置位」

`0x100c` 确实是 `JOB_INT_STAT`（掩码后），`0x1000` 是 `JOB_INT_RAWSTAT`（未掩码的锁存）。
两者都含 bit31 = `JOB_INT_GLOBAL_IF`。**关键点：`0x100c` = `RAWSTAT & ~MASK`**，
所以只要 `JOB_INT_MASK` 的 bit31 为 1，`0x100c` 的 bit31 **永远读不到**，即使 raw 已经置位。

证据链：

- `panthor_regs.h:152-157`：
  ```
  #define JOB_INT_RAWSTAT  0x1000
  #define JOB_INT_CLEAR    0x1004
  #define JOB_INT_MASK     0x1008
  #define JOB_INT_STAT     0x100c
  #define   JOB_INT_GLOBAL_IF  BIT(31)
  ```
- panthor 在开中断时把"要收的事件"写进 MASK（`panthor_device.h:333-338`，`irq_resume(mask)` → `INT_CLEAR=mask; INT_MASK=mask`），
  即 **MASK 位 = 使能位，0 = 屏蔽**。`panthor_device.h:308` 处理函数里读的是 `RAWSTAT & pirq->mask`，不是 `STAT`。
- 厂商 kbase 同向：`kbase_pm_init_hw` 先写 `0x1008=0`（`0x3b7c0`：`mov w8,wzr; str w8`），
  再写 `0x1008=0xffffffff` 开（`0x3ba50`/`0x3ba60`：`mov w23,#-1; str w23`）。
  反汇编原文见 `/tmp/kb.dis` 行 `3b7c0` 与 `3ba60`。

**对本项目的实际意义（这一条可执行）**：

`tree/mtk/mt6895/drivers/gpu/hakios.mali610.c:3132` 在 `gpu_irq_mask_prestart()` 里写：

```c
mwr(kJobIntMask, 0xffffffffu);   /* 全屏蔽 */
```

而该函数在 `:4641` **无条件**执行，早于 `:4816 start_mcu()`。于是 `start_mcu()` 的轮询
（`:3897  uint32_t stat = mrd(kJobIntStatus);` —— 读的是 **STAT**）在结构上**不可能**看到 bit31。
`:3976` 之后才写 `mwr(kJobIntMask, 0u)`，但那时轮询已经超时（`:3940` 附近的百万次自旋已跑完）。

也就是说：**当前「GLOBAL_IF 从不置位」这个观测，有一部分可能是我们自己把 MASK 关上了，
而读的是被 MASK 过滤后的 STAT。** `kJobIntRaw = 0x1000` 在整份文件里**只有定义、零次使用**
（`:364`），RAWSTAT 从未被观测过。

**这是本次扫荡里唯一一条能改变「下一步该读哪个寄存器」的发现。**
它的地位是「观测通道有缺陷」，不是「找到了根因」—— 补上 RAWSTAT 读取后，
如果 RAWSTAT 的 bit31 也是 0，才轮到「MCU 真的没执行」。

> 与项目已有记录的交叉：`docs/headset-wip/MASTER-STATE.md:130` 已经写过
> 「`kJobIntRaw = 0x1000`（`:358`）只定义、全文件零次使用 —— RAWSTAT 从未被观测」，
> `:151` 也把它列为 E1「先做」。本报告补上了**为什么 STAT 一定读不到**这一层机制，
> 并指出 MASK 全 1 是默认路径而非某次实验的偶然。

### 0.3 `startMcu()` 的实际内容只有 8 行

`Ken-u/Mali` 的 `startMcu()`（`/tmp/kenmali/Mali.cpp:540-549`）逐字如下 —— 它**没有**任何
hw-issues、CSF_CONFIG、L2_CONFIG、ASN_HASH、PDC、MTCMOS、IPI 动作：

```
540  bool startMcu() {
541    gpuWrite(kJobIntClear, 0xffffffffu);
542    gpuWrite(kMmuIntClear, 0xffffffffu);
543    gpuWrite(kJobIntMask, 0);   // polled
544    gpuWrite(kMcuControl, 2);   // MCU_CONTROL_AUTO
545    const bool booted = waitUs(1000000, [] { return (gpu(kJobIntRaw) & kJobGlobalIf) != 0; });
546    printf("mali: MCU %s (MCU status %u, job irq raw %#x)\n", booted ? "booted: the global interface raised its interrupt" : "did NOT boot", gpu(kMcuStatus), gpu(kJobIntRaw));
547    if (!booted) faultReport();
548    return booted;
549  }
```

三处值得注意：

1. `:543` 写 `kJobIntMask = 0`（**解除屏蔽**），然后 `:545` 读 `kJobIntRaw`（RAWSTAT）。
   两件事我们都做反了：mask 写全 1，读的是 STAT。
2. `:541-542` 在写 AUTO **之前**清 JOB/MMU 中断。我们在 `:3131` 清 JOB、`:3133` 清 MMU，
   时间点更早（L2 上电之前），中间隔着 L2 上电和 IPA，可能被后续事件重新置位。这是次级差异。
3. `:544` 只写 `MCU_CONTROL = AUTO`。**没有任何前置的软复位**在这个函数里 —— 但软复位在
   `mali_start()` 的 `:3097`，即 L2 上电之前就做完了。见 §2.3。

---

## 1. 扫过的仓库清单

### 1.1 `Ken-u` 用户（63 个仓库，API `?per_page=100&sort=updated`）

只有 4 个与 GPU / XNU 相关，其余是与本项目无关的个人工具仓库（AI agent、ESP32、Android ROM 等）。

| owner/repo | star | 语言 | 描述 | 相关度 |
|---|---|---|---|---|
| `Ken-u/Mali` | 0 | C++ | Mali-G610 (CSF) kernel driver for UniXNU: power, firmware, MMU, command streams | **A** |
| `Ken-u/MaliMetal` | 0 | Objective-C++ | Metal frontend for Mali on UniXNU: PanVK + metal2vulkan + backboard | **A**（降级：见 §3） |
| `Ken-u/UniXNU-rk3588-common` | 1 | C++ | Shared Rockchip RK3588 support: CPU topology, GICv3, UART, eMMC, Mali-G610 | **B** |
| `Ken-u/UniXNU-evb10` | 2 | C++ | UniXNU device repo for RK3588 EVB10 V10 (iPadOS 17.7.11) | B（板级） |
| `Ken-u/UniXNU-orangepi5` | 2 | C++ | UniXNU device repo for Orange Pi 5 (RK3588S) | C |
| `Ken-u/Rk3588Emmc` | 0 | C++ | RK3588 eMMC host (DWC MSHC) block driver | C（无关） |
| `Ken-u/uefi-evb10` / `uefi-orangepi5` | 0 | Python | UEFI platform integration for RK3588 | C |
| `Ken-u/ios-sweet` / `ios-sweet-build` | 0 | Python | XNU build for RK3588 / iOS on SM7150 | C（构建体系） |
| 其余 54 个 | 0–5 | 多 | kkagent、esp-claw、PageKit、akshare、scrcpy 等 | 无关 |

**没有第 5 个 GPU 仓库。** 没有隐藏的 Mali bring-up 文档仓库。

### 1.2 `UniXNU` 组织（32 个仓库）

| owner/repo | star | 语言 | 描述 | 相关度 |
|---|---|---|---|---|
| `UniXNU/Adreno740` | 13 | C++ | Native XNU Adreno 740 GPU driver | **B**（另一套 GPU 方案，见 §6） |
| `UniXNU/AdrenoFirmware` | 0 | C | Adreno GMU firmware and HFI kit | B（「协处理器冷启动」范式） |
| `UniXNU/AirNir` | 0 | C | AIR to NIR to IR3 compiler kit | C（见 §6.2） |
| `UniXNU/ArmCpu` | 2 | C++ | ARM64 CPU, GIC and timer driver | C（GIC 注册范式，见 §6.3） |
| `UniXNU/DarwinBoot` | 2 | C | UEFI bootloader for UniXNU | C（boot-arg 传递） |
| `UniXNU/GopFramebuffer` | 2 | C++ | Mobile display and GPU scanout hand-off | C |
| `UniXNU/ArmDTPlatform` | 1 | C++ | ARM device-tree platform expert | C |
| `UniXNU/UniXNU` | 2890 | Python | Run iOS/macOS on non-Apple ARM64 through UEFI | C（主仓，见 §6.4） |
| `UniXNU/AdrenoMetal` | 31 | Objective-C++ | Metal API frontend + metal2vulkan | C |
| `UniXNU/VerifiedRoot`、`XnuPrelink`、`RamBlock` 等 22 个 | 0–5 | 多 | 存储/HID/USB/输入/加密 | 无关 |

**UniXNU 组织下没有任何 Mali 仓库。** Mali 的全部实现只在 `Ken-u` 名下。

### 1.3 fork / contributor / topic

| 查询 | 结果 |
|---|---|
| `repos/Ken-u/Mali/forks` | **1 个**：`Hange777/Mali`，0 star，`pushed_at 2026-11-12` |
| `repos/Ken-u/Mali/contributors` | **1 个**：`Ken-u`，15 commits |
| `repos/UniXNU/UniXNU/contributors` | 未取（速率/无关，主仓是 XNU 移植框架，不含 Mali） |
| `search/repositories?q=topic:unixnu` | **155 个**。逐个看过描述，**没有第二个 Mali/CSF 实现**；GPU 相关只有 Adreno 系（Adreno740 / Adreno610 / Adreno830 / AdrenoMetal） |

`Hange777/Mali` 只比上游多 1 个 commit（`1a496b52`「round 12: mali log relay carries the owning
process name and pid」，`Mali.cpp` +9/-1），内容是给 `MaliClient` 加 `ownerPid` 用于日志署名。
**与 MCU 启动无关。**

### 1.4 本地已有副本与 revision

| 仓库 | 本地路径 | HEAD | 备注 |
|---|---|---|---|
| `Ken-u/Mali` | `/tmp/kenmali` | `7167b67ccb20d6c8a29260c0bb1ee30d487617f5` | `Mali.cpp` 3155 行，sha256 前 16 位 `5452464de47188c9` |
| `Ken-u/MaliMetal` | `/tmp/malimetal` | `ed4d549635fe993e3a24c85a63727399521c3ef6` | `docs/notes.zh-CN.md` 541 行 |
| `Ken-u/UniXNU-rk3588-common` | `/tmp/rk3588common` | `10df4ce8b58a32c80ff08017ea5cca56c83abf90` | 无 Mali 代码，只有 dts 节点 |

---

## 2. A 级发现详细摘要

### 2.1 `startMcu()` 完整流程还原

**「完整流程」需要跨三个函数拼**，因为 `startMcu()` 本身只有 8 行。
下面是 `mali=2`（M0+M1）路径上，从映射寄存器到「MCU 起来」的**全部**动作，
按 `mali_start()` 的执行顺序（`/tmp/kenmali/Mali.cpp:3084` 起）。

| # | 位置 | 动作 | 原文/值 |
|---|---|---|---|
| 1 | `:3089` | 映射 PMU `0xfd8d8000` size `0x400` | `ml_io_map(kPmuPhys, 0x400)` |
| 2 | `:3090` | 映射 CRU `0xfd7c0000` size `0x1000` | `ml_io_map(kCruPhys, 0x1000)` |
| 3 | `:3092` | `setClocks()`：写 `CLKSEL_CON(158)` 选源+分频，清 `CLKGATE_CON(66)` 门控 | `:67-69` 定义 `kCruSel158 = 0x578`、`kCruGate66 = 0x908` |
| 4 | `:3093` | `powerDomainOn()`：PMU 域 12 上电 | `:102-121`；`:109` 写 `kPmuPwr` bit16（= bit0 写 0 = 上电），`:114` 撤 idle request |
| 5 | `:3094` | 映射 GPU 寄存器基址 size `0x200000` | `kGpuMapSize = 0x200000`（`:60`，注意注释说明门铃要到 `0x100000`） |
| 6 | `:3096` | `identify()`：打印 GPU_ID / L2 / MMU / present | `:214-224` |
| 7 | **`:3097`** | **`softReset()`：`GPU_CMD(0x30) = 0x101`（soft reset），等 `INT_RAWSTAT` bit8** | `:203-212`；`:79` `kSoftReset = 1u \| (1u<<8)` |
| 8 | `:3098-3099` | 打印 "soft reset done"，再 `identify()` | |
| 9 | `:3104` | `l2Test()`：`L2_PWRON(0x1a0) = L2_PRESENT`，等 `L2_READY(0x160)` | `:282-288`；`:285` `gpuWrite(kL2PwrOn, mask)`，`:286` 等 `(L2Ready & mask) == mask`，超时 20 ms |
| 10 | `:3105` | `dumpNoc()`（RK3588 的 Arteris QoS，只读） | `:229-245` |
| 11 | `:3106` | `pokeRegisters()`（boot-arg 手动 poke，默认空） | `:249-279` |
| 12 | `:3113` | `GPU_CMD(0x30) = 0x103`（`TIME_ENABLE`，开 cycle counter）+ 读 `0x90/0x94` 测频 | `:3108` 注释原文 "GPU_COMMAND_TIME_ENABLE starts it" |
| 13 | `:2008` `m1()` | `loadFirmware()` | `:443-491` |
| 14 | `:2009` | **`asEnable(0, gMcuVm.pool.pa)`** —— AS0 使能，**在 startMcu 之前** | `:509-524` |
| 15 | `:2010` | `startMcu()` | `:540-549` |
| 16 | `:2011` | `globalInterface()` | `:567-597` |

**`loadFirmware()` 内部**（`:443-491`）：

| 步 | 位置 | 动作 |
|---|---|---|
| a | `:450` | `vmInit(gMcuVm, 64)` —— 64 页页表池（256 KiB） |
| b | `:452-486` | 遍历 entry：`type = ehdr & 0xff`、`esize = (ehdr>>8)&0xff` |
| c | `:461-462` | **`if (flags & kSecProt)` → 打印 "protected-mode only, skipped"，不映射** |
| d | `:467-468` | `memAlloc` + `memcpy`（`memAlloc` 内部 `memset(kva,0,size)`，`:325`） |
| e | `:469-474` | 逐页 `ptMap`，叶子 = `AF\|NG\|UNPRIV\|SH\|(idx<<2)`，按 flag 加 `RDONLY`/`XN` |
| f | `:475-478` | 若 `vaStart == kSharedVa(0x04000000)` 记 `gShared` |
| g | `:488-489` | `dsb sy`，打印段数与根表 PA |

**`asEnable()` 内部**（`:509-524`）：

| 步 | 位置 | 动作 |
|---|---|---|
| a | `:512` | `AS_LOCKADDR = 63`（lock 整个范围），`asCommand(LOCK)` |
| b | `:513` | `asCommand(FLUSH_MEM)` + `asReady` |
| c | `:514` | `transcfg = (2<<24) \| (1<<30) \| 6 \| ((55-vaBits)<<6)`，vaBits=48 → **`0x420001c6`** |
| d | `:515-517` | 写 `AS_TRANSTAB` / `AS_MEMATTR`（`kMemAttr = 0x4c4c4c4c9c4c9f4c`）/ `AS_TRANSCFG` |
| e | `:518` | `asCommand(UPDATE)` + `asReady` |

> **三处与本项目不同，且都是「我们更接近 panthor、它更接近 kbase」的方向**：
> 1. Ken-u 在 `asEnable` 里做 LOCK + FLUSH_MEM（`:512-513`），我们不做（`hakios.mali610.c:2885-2887` 注释明确说明为「不学 panthor」而删掉 LOCK/FLUSH_MEM）。
> 2. Ken-u 的 transcfg 是 `0x420001c6`（含 `INA_BITS(55-48)=0x1c0`），我们写 `0x42000006`。**任务描述说「两种都验证过能成功」—— 这里的「成功」指 AS0 UPDATE 返回、状态位清零，不是 MCU 起来。两者在本项目上都没有让 MCU 执行。**
> 3. Ken-u 的 `AS_MEMATTR = 0x4c4c4c4c9c4c9f4c`（panthor 的 `mair_to_memattr` 结果，索引 0=0x4c），我们写 `0x00084c8d888d8f88`（kbase 的七项表，索引 0=0x88）。两个值都在本项目的 `:883` 注释里讨论过。

### 2.2 `hw-issues` / `CSF_CONFIG(0xF00)` / `L2_CONFIG(0x48)` / `ASN_HASH(0x2C0)` 结论

对 `Ken-u/Mali` 的全文计数（`/tmp/kenmali/Mali.cpp`，3155 行）：

| 关键词 | 命中数 |
|---|---|
| `csf_config` / `CSF_CONFIG` / `0xf00` / `0xF00` | **0** |
| `asn` / `ASN_HASH` / `0x2c0` | **0** |
| `L2_CONFIG` / `0x48` | **0** |
| `hw_issue` / `hw-issue` / `quirk` / `errata` / `workaround` | **0** |
| `pdc` / `mtcmos` / `MTCMOS` / `GPUEB` / `ipi` / `IPI` / `mbox` / `MBOX` | **0** |
| `SHADER_PWRON` | **0**（它靠固件自己上电，见 M2b） |

**结论：Ken-u 驱动对一个能启动 MCU 的机器，这四组寄存器一个都没写。**

同一批寄存器在**厂商 kbase（MT6895）**里是写的，来自
`/home/lizi/IOS/hakios-mtk/work-notes-kbase-mcu-boot-sequence.md`（本项目自己的 RE 笔记）：

| 寄存器 | kbase 写点 | 值来源 | 笔记行号 |
|---|---|---|---|
| `0x0f04` SHADER_CONFIG | `0x3b92c` | `kbdev[0]`（硬件读回属性，**不是 0**） | `:60` |
| `0x0f08` TILER_CONFIG | `0x3b94c` | `kbdev[4]` | `:61` |
| `0x0f0c` L2_MMU_CONFIG | `0x3b96c` | `kbdev[8]` | `:62` |
| `0x0f00` CSF_CONFIG | `0x3b98c` | `kbdev[0xc]` | `:63` |
| `0x304` COHERENCY | `0x3b9a0` → `0x3636c` | `kbdev+0x2dec` | `:64` |
| `0x048` L2_CONFIG | `0x39358` | `read(0x48) \| (1<<24)`，**前置条件 hw_issues≠0 且 `+0x2870` bit26** | `:204` |
| `0x2c0/2c4/2c8` ASN_HASH | `0x392f8/39318/39338` | DT `l2_hash_values`，**不是常量** | `:210` |

**这构成一个真正的「两边都对、中间有平台差异」的局面**：RK3588 上不写这些能跑，
MT6895 的厂商驱动写这些。但这**不能**证明我们必须写 —— 因为 `PWR_STATUS=0x3ffc`
（MFG1-12 全 up）和 `SHADER_READY` 的状态说明供电侧已经到位，而这些寄存器控制的是
shader/tiler/L2 的配置与 ASN hash，不是 MCU 的取指。

**未验证**：MT6895 上 `0x0f00` 的硬件读回值是多少。我们的 `:4601` 有一个 `mali_hwissues=1`
实验会写 0（这是**错的方向** —— kbase 写的是读回的硬件属性，不是 0），默认关闭（`:526`）。

### 2.3 软复位：Ken-u 做、我们不做，且这是我们自己删的

| | Ken-u/Mali | 本项目 |
|---|---|---|
| 位置 | `:3097`，`mali_start()` 里，**L2 上电之前** | `hakios.mali610.c:4582-4592`，`if (!gMaliCold \|\| gMaliResetOnly)` |
| 默认 | **执行**（`mali=2` 路径必经） | **不执行**（`gMaliCold = 1`，`:522`） |
| 值 | `GPU_CMD = 0x101`（`kSoftReset = 1u \| (1u<<8)`，`:79`） | 同 |

我们没有做软复位的理由写在 `:3618-3624`：panthor 的 `panthor_gpu_soft_reset()` 只从
`panthor_gpu_suspend()` 到达，不在启动 MCU 的路径上；且软复位会把刚上电的 L2 打回默认。
这个理由是**对的**（panthor 确实不在 fw_init 里复位），但它意味着**我们和 Ken-u 在
「MCU 看到的是一个什么状态的 GPU」上不同**：Ken-u 是「软复位 + L2 上电」，我们是「原样 + L2 上电」。

**这是平台/路径差异，不是错误。** 记录在此，不作为根因。

### 2.4 `globalInterface()`：握手字段与我们的差异

`Ken-u/Mali:567-597`。要点：

| 字段 | Ken-u 值 | 位置 |
|---|---|---|
| `coreEnMask` | `gpu(kShaderPresent)` | `:584` |
| `poweroffTimer` | `235` | `:585` |
| `progressTimer` | `(5*500*1024*1024) >> 10` | `:586` |
| `ackIrqMask` | `ALLOC\|PING\|PROGRESS\|POWEROFF\|IDLE_EN\|IDLE` | `:587` |
| `req` | `(req & ~toggles) \| ((ack ^ toggles) & toggles)` | `:589` |
| 门铃 | `gpuWrite(kDoorbell0, 1)`，`kDoorbell0 = 0x80000` | `:591`、`:495` |
| 超时 | 1 s（`waitUs(1000000, ...)`） | `:592` |

本项目 `hakios.mali610.c:4156-4227` 逐字对应，**唯一实质差异**是我们多了一步
`mem_flush(&gShared->mem)`（`:4206`）—— 注释解释因为我们的共享段映射为 cacheable 而
panthor 是不 cache。这一处**我们比它更保守，是对的**。

### 2.5 `MaliMetal` 的 `docs/notes.zh-CN.md`：没有内核启动阶段的坑

`/tmp/malimetal/docs/notes.zh-CN.md`（541 行）全部是**用户态**内容：PanVK 构建、metal2vulkan、
沙盒授权、帧率、backboardd 崩溃。对「MCU 启动」零命中：

```
grep -n -i "mcu\|firmware\|power domain\|上电\|复位" docs/notes.zh-CN.md
→ 只有 1 行命中，且是 "一个客户端 = 1 个 VM + 2 个 group"（:74），与 MCU 无关
```

**结论：这个仓库不能提供 CSF bring-up 细节。** 任务里「可能有更多 bring-up 细节」的假设不成立。

### 2.6 GPUEB / IPI / mbox：两个仓库都没有

`Ken-u/Mali` 与 `Ken-u/MaliMetal` 全文对 `gpueb`、`IPI`、`mbox`、`MBOX` 全部零命中。

这一点值得注意：**本项目当前的头号怀疑（GPUEB 从没发过 IPI）在 RK3588 上根本不存在这个概念。**
RK3588 的 GPU 电源域由 AP 侧的 PMU 寄存器直接控（`Ken-u/Mali:102-121`），
没有独立的协处理器去转发命令。所以 Ken-u 的代码**在结构上无法**验证或否定 GPUEB 假设。

平台差异的对照物应该看 MTK 自己的驱动，不是 Ken-u。本项目已有
`docs/headset-wip/findings/GPUEB-IPI-PROTOCOL.md`（54 KB）与
`FINDING-gpueb-*` 三份，那些才是这块的正解来源。

---

## 3. 平台层差异清单（明确区分「平台差异」与「真正可能的原因」）

### 3.1 纯平台差异 —— **不要因为「Ken-u 这么做」就照抄**

| # | 项 | RK3588（Ken-u） | MT6895（我们） | 判据 |
|---|---|---|---|---|
| P1 | 电源域 | PMU `0xfd8d8000`，域 12 = bit0，写 `0x14c`/`0x10c` | SCPMI/PDC + `MFG_DEBUG_TOP` | `Mali.cpp:62-65`、`:102-121` vs `hakios.mali610.c:306-337` |
| P2 | 时钟 | CRU `CLKSEL_CON(158)`=`0x578`、`CLKGATE_CON(66)`=`0x908`；SCMI(id 5) 可选 | MTK TOPCKGEN/PLL，`PWR_STATUS=0x3ffc` 已锁 | `Mali.cpp:67-69` vs `hakios.mali610.c` 时钟段 |
| P3 | 中断号 | GPU 94 / MMU 93 / JOB 92（GIC SPI） | 不同 SPI | `Mali-notes.zh-CN.md:35` |
| P4 | 核数 | `SHADER_PRESENT = 0x50005`（**4 核**） | G610 **MC6**（6 核） | `Mali-notes:40` vs 任务描述 |
| P5 | L2 | 1 个 core group，`L2_PRESENT = 1` | 同（`kL2PresentFirst`） | `:283` vs `:3164` |
| P6 | coherency | 读回 0（不 coherent），映射 WC | 读回需实测 | `Mali.cpp:300` 注释 |
| P7 | 软复位 | 启动前做 `0x101` | 默认不做（理由正确） | §2.3 |
| P8 | AS0 lock/flush | `LOCK` + `FLUSH_MEM` + `UPDATE` | 只 `UPDATE` | §2.1 |
| P9 | `AS_MEMATTR` | `0x4c4c4c4c9c4c9f4c`（panthor 表） | `0x00084c8d888d8f88`（kbase 表） | §2.1 |
| P10 | transcfg | `0x420001c6` | `0x42000006` | §2.1 |
| P11 | 固件 | 274432 B，7 段 + 1 PROT 跳过 | 258048 B，10 段，3 PROT | 见 §3.3 |
| P12 | GPUEB/IPI | 无此机制 | 有 | §2.6 |

### 3.2 真正可能的原因（**只有一条，且是观测通道**）

| # | 项 | 性质 | 置信度 |
|---|---|---|---|
| **R1** | **`JOB_INT_MASK` 被写全 1，且轮询读的是被掩码的 `STAT(0x100c)` 而非 `RAWSTAT(0x1000)`** | 观测缺陷（不是硬件原因） | **高**（寄存器语义有 panthor 与 kbase 双源证据） |

**R1 的完整逻辑链**：

1. `:3132` `mwr(kJobIntMask, 0xffffffffu)` 在 `:4641` 无条件执行。
2. `:3897` `uint32_t stat = mrd(kJobIntStatus)` 读 `0x100c`。
3. `0x100c = RAWSTAT & ~MASK`（`panthor_regs.h:152-156` + `panthor_device.h:333-338` + kbase `0x3b7c0`/`0x3ba60`）。
4. 因此 bit31 在 `0x100c` 上恒为 0，与 MCU 是否执行**无关**。
5. `:3976` 才写 `mwr(kJobIntMask, 0u)`，但在轮询结束之后。

**注意这条的性质**：它解释的是「为什么我们看不到 GLOBAL_IF」，**不解释**「为什么 version 字仍是哨兵」。
`:3956-3961` 的哨兵检查（`kSentinelVersion`）是独立于中断的第二证据；如果哨兵也没变，
那么「MCU 真的没执行到写 version」仍然成立。

> **因此 R1 不推翻 GPUEB 假设，只是把 E1 从「建议做」升级为「必须先做，且已知为什么之前一定会失败」。**

### 3.3 固件段的实质差异（本项目固件已实测解包）

对 `/home/lizi/IOS/work-xaga/codex/out/round-18-support/mali_csffw.bin`
（258048 B，sha256 `de22d2fc06bc8a859968499a6e609c27fe838be062bf5c97d92ea9f904f7229a`，与任务给的
`de22d2fc…` 一致）解包：

```
sec0: flags 0x00000009 va 0x0000000-0x001000 data 2836      r--  cached
sec1: flags 0x0000000d va 0x1000000-0x1010000 data 63560    rwx  cached   <- 复位向量在这里
sec2: flags 0x8000000b va 0x2000000-0x203e000 data 180752   rw-  cached   ZERO
sec3: flags 0x8000000b va 0x203e000-0x203f000 data 0        rw-  cached   ZERO
sec4: flags 0x8000000b va 0x203f000-0x2040000 data 0        rw-  cached   ZERO
sec5: flags 0x80000009 va 0x2800000-0x2840000 data 180752   r--  cached   ZERO (与 sec2 同数据)
sec6: flags 0x0000002b va 0x3000000-0x303e000 data 0        PROT rw-        <- PROT
sec7: flags 0x0000002b va 0x303e000-0x303f000 data 0        PROT rw-        <- PROT
sec8: flags 0x0000002b va 0x303f000-0x3040000 data 0        PROT rw-        <- PROT
sec9: flags 0xc000001b va 0x4000000-0x4094000 data 8192     shared rwx    ZERO  <- 共享接口
```

向量表（VA 0）实测：

```
MSP   = 0x02040000
Reset = 0x010018ed   (bit0 = 1, Thumb)
```

**两条必须先说清楚的事实**：

1. **PROT 段的 flags = `0x0000002b`：bit0=R、bit1=W、bit3=cached、bit5=PROT。没有 bit31(ZERO)。**
   也就是说这三个段**不需要 host 清零** —— 「全零」是文件里就没有数据，不是要求我们清零。
   这与 panthor 一致（`panthor_fw.c:403` 只在 `RD_ZERO` 时 `memset`）。
2. **sec9（共享接口，`va 0x4000000`）的 flags = `0xc000001b`：含 bit30(SHARED) + bit31(ZERO)**。
   这一段才是**必须清零**的（bit31），且必须是 shared。我们的 `mem_alloc` 走
   `kmem_alloc(..., KMA_ZERO, ...)`（`:737`）所以是清零的；Ken-u 的 `memAlloc` 也
   `memset(kva, 0, size)`（`:325`）。

**从固件里直接读出的 glb_control（`0x04000000`，8 字）**：

```
version        = 0x00000000        <- 固件镜像里就是 0（所以哨兵法有效）
features       = 0x00000000
input_va       = 0x04002000
output_va      = 0x04003000
group_num      = 8
group_stride   = 0xa0
perfcnt_size   = 0
instr_features = 0x71
```

对照 Ken-u 板上实测（`Mali-notes.zh-CN.md:74`）：
`v1.1.0, features 0, 8 groups (stride 0xa0), instrumentation 0x71, input 0x4002000 output 0x4006000`。

**这里有一个值得核对的差异**：我们的固件 `output_va = 0x04003000`，Ken-u 的是 `0x4006000`。
`0x04003000 - 0x04000000 = 0x3000`；`0x4006000 - 0x4000000 = 0x6000`。
共享段大小 `0x4000000`，两者都在范围内，所以**不是错误**，是不同固件版本的布局差异。
但要注意：我们的 `globalInterface()` 用的是固件声明的 `control->output_va`（`:4173`
`output = shared_at(control->output_va)`），所以自动适配。**这一条不需要动作，仅记录。**

> 前面 §2 提到的「PROT 段被跳过」在 Ken-u 是对的（`:461-462`），**本项目实际是映射的**
> （`hakios.mali610.c:1069-1072` 只打印、不 `continue`）。任务描述里「3 条 PROT 跳过」
> 与代码不一致 —— **代码是映射的**，日志文字说 "mapped with ordinary memory"。
> 这是一个**文档/日志与代码不一致**的问题，值得在下一轮确认到底哪边是对的。
> 从固件 flag 看（`0x2b` 无 ZERO 位），kbase 会通过 `kbase_csf_protected_memory_alloc`
> 分配并映射它们，所以**映射是对的**。

---

## 4. 本项目 vs Ken-u：逐项对照表

| 项 | Ken-u/Mali | hakios.mali610.c | 判断 |
|---|---|---|---|
| MCU_CONTROL 值 | `2` (AUTO)，`:544` | `kMcuControlAuto = 2`，`:398` | 同 |
| 启动前清中断 | JOB+MMU clear，`:541-542` | 在 `:3131-3133`（更早） | **差异，次级** |
| JOB_INT_MASK | `0`（解除），`:543` | `0xffffffff`（全屏蔽），`:3132` | **R1，见 §3.2** |
| 轮询寄存器 | `RAWSTAT(0x1000)`，`:545` | `STAT(0x100c)`，`:3897` | **R1** |
| 轮询超时 | 1 s | `spins < 1000000` + `delay(1)` ≈ 1000 s | **差异**（我们长得多，不算错） |
| 门铃轮询期间 | 不敲 | 不敲（`:3881-3886` 注释说明） | 同 |
| 软复位 | 启动前做 `0x101` | 默认不做 | 平台/路径差异 |
| AS0 LOCK+FLUSH | 做，`:512-513` | 不做（注释说明为正确） | 平台差异 |
| transcfg | `0x420001c6` | `0x42000006` | 平台差异 |
| AS_MEMATTR | `0x4c4c4c4c9c4c9f4c` | `0x00084c8d888d8f88` | 平台差异 |
| 固件段循环步长 | `0x1000`（`:473`） | `PT_LEAF_GRANULE 0x1000`（`:863`） | 同（我们修过 `PAGE_SIZE=16K` 的坑） |
| PROT 段 | **跳过**，`:461-462` | **映射**，`:1069` | **不一致，需核对**（§3.3） |
| 叶子 PTE 基础位 | `AF\|NG\|UNPRIV`（`:470`） | `PTE_PAGE\|PTE_AF\|PTE_UNPRIV` + 可选 `NG` | 我们有 `PAGE_VALID` 位（修过的 bug） |
| 共享段 flush | 无（映射 NC） | 有 `mem_flush`（`:4206`） | 我们更保守，正确 |
| 哨兵法 | 无 | 有（`:3955-3961`） | 我们更好 |
| GPUEB/IPI | 无概念 | 有（本项目重点） | 平台差异 |

---

## 5. B 级 / C 级发现

### 5.1 L2 上电顺序（B 级，问题 6）

`Ken-u/Mali:282-288` 的 `l2Test()` 只有「写 `PWRON`、等 `READY`」，**不检查 `PWRTRANS`**：

```cpp
282  void l2Test() {
283    const uint32_t mask = gpu(kL2Present);
284    gpuWrite(kIntClear, 0xffffffffu);
285    gpuWrite(kL2PwrOn, mask);
286    const bool ok = waitUs(20000, [mask] { return (gpu(kL2Ready) & mask) == mask; });
287    printf("mali: L2 power-up %s (ready %#x trans %#x, irq raw %#x)\n", ...);
288  }
```

`PWRTRANS` 只被**打印**（`:287`），从不作为判据。本项目 `hakios.mali610.c:3221-3238`
检查 `PWRTRANS` 归零后再等 `READY`，更严格。

**结论：L2 上电顺序上没有可借鉴的差异。** 我们比它更严。

### 5.2 Coherency（`COHERENCY_PROTOCOL 0x304`，B 级，问题 7）

- `Ken-u/Mali`：**只读不写**。`:221` 打印 `gpu(kCoherencyFeatures)`（`0x300`），
  `:2106` 填 `i.coherency_features`，**全文没有对 `0x304` 的写**（grep `0x304` = 0 命中）。
- 本项目：`:3172-3178` 读后**写 `kCoherencyNone = 31`**（`:428`），但只在
  `if (!gMaliCold || gMaliCohNone)` 时写 —— 而 `gMaliCold = 1` 默认（`:522`），
  `gMaliCohNone = 0` 默认（`:525`），**所以默认不写**。
- 厂商 kbase（MT6895）：`0x3b9a0 → 0x3636c` 写 `0x304 = kbdev+0x2dec`（RE 笔记 `:64`）。

**结论：这是 MTK 平台差异。** Ken-u 在不写 `0x304` 的机器上能起来，说明 G610 本身
不要求 host 写它；MTK 的 kbase 写它是因为 MTK 的平台拓扑需要。

### 5.3 `GPU_CMD = 0x103`（B 级，问题 8）

- `Ken-u/Mali:3113`：`gpuWrite(kCmd, 0x103);` —— 在 `mali_start()` 的时钟探针块里，
  注释 `:3108` 写明 "GPU_COMMAND_TIME_ENABLE starts it"。**做了，但只是为了让
  cycle counter 走，与 MCU 启动无关。**
- 本项目：grep `0x103` 只命中注释与 PDC 地址，**没有写 `0x30 = 0x103`**。

**结论：纯测量用途，与症状无关。** 但如果我们想用 GPU cycle counter 判断
「MCU 是否真的在执行」（counter 在跑 = 时钟到了 MCU），补这一条有诊断价值。

### 5.4 页表（B 级，问题 9）

| 项 | Ken-u/Mali | 本项目 |
|---|---|---|
| 池大小 | 64 页 = 256 KiB（`:450`） | `pt_new` 按需 + 低窗 |
| 级数 | 4 级，4 KiB granule（`:380-405`） | 4 级，`PT_LEAF_GRANULE 0x1000`（`:863`） |
| 根表 | 池第 0 页（`:340` 注释） | `gMcuRoot` |
| 叶子 | `AF\|NG\|UNPRIV\|SH\|(idx<<2)` + 可选 RDONLY/XN | `PTE_PAGE\|PTE_AF\|PTE_UNPRIV\|(idx<<2)` + 可选 NG/SH/RDONLY/XN |
| `AS_MEMATTR` | `0x4c4c4c4c9c4c9f4c` | `0x00084c8d888d8f88` |

**关键差异只有两个**：Ken-u 的叶子**恒带 `NG`**（`:470` 无条件），我们的 `NG` 由
`gMaliPtVendor` 控制（`:1141`，默认 0 → **带 NG**）。两边默认一致。
`AS_MEMATTR` 的差异见 §2.1 note 3。

### 5.5 `AirNir` 是什么（C 级，问题 10）

`UniXNU/AirNir`，0 star，C，描述 "AIR to NIR to IR3 compiler kit"。README 原文：

> AIR shader translation support. Offline compilation supports original UI shaders.
> The runtime uses metal2vulkan and Turnip.

目录：`AirNir.h`、`BackendABI.h`、`ConstantUploads.c/h`、`Frontend.cpp`、`FusedF32.h`、
`Library.c/h`、`MesaBackend.c`、`licenses/`、`tests/`。

**它把 Metal AIR 翻成 NIR，再喂 IR3（Adreno 的后端）。** 是 Adreno 路线的产物。
对 Mali/PanVK 路线**无用** —— PanVK 需要的是 NIR → Valhall，那是 Mesa 自己的
`panfrost/compiler`，不走 AirNir。Ken-u 的选择是 metal2vulkan → SPIR-V → PanVK
（`MaliMetal-README.md`），绕开了 AirNir。

### 5.6 其他 UniXNU 设备怎么解决 GPU（C 级，问题 11）

`topic:unixnu` 155 个仓库里，GPU 相关的**全部是 Adreno**：

| 仓库 | GPU | 路线 |
|---|---|---|
| `UniXNU/Adreno740` | A740 (SM8550) | 原生内核驱动 + GMU 固件 + HFI |
| `UniXNU/AdrenoMetal` | 同上 | Metal → metal2vulkan → Turnip |
| `UniXNU/AirNir` | 同上 | AIR → NIR → IR3 |
| `UniXNU/AdrenoFirmware` | 同上 | GMU 冷启动 + HFI 协议 |
| `xVanTuring/Adreno610` | A610 (SM6225) | 同上路线 |

**没有一个非 Adreno 的 CSF 案例。** `Ken-u/Mali` 是 UniXNU 生态里**唯一**的 Mali 实现。

`AdrenoFirmware` 的 README 有一句对本项目有借鉴意义：

> GMU cold start and HFI work on sheng hardware.

GMU 是 Adreno 的协处理器，和 MTK 的 GPUEB 是同类问题（`docs/headset-wip/findings/GPUEB-IPI-PROTOCOL.md`
已经在这条线上）。但**那是高通平台，寄存器/协议完全不同**，只能借方法论，不能借代码。

---

## 6. 交叉引用：本项目内部已有的、比 Ken-u 更相关的材料

扫描过程中确认，下面这些本地文档**比 Ken-u 的仓库更贴近当前症状**，列出来避免重复劳动：

| 文件 | 内容 | 为什么比 Ken-u 相关 |
|---|---|---|
| `hakios-mtk/work-notes-kbase-mcu-boot-sequence.md` | MT6895 kbase 的**全部** CSF 启动寄存器读写，含 OTA 反汇编地址 | 同平台、同芯片 |
| `docs/headset-wip/findings/GPUEB-IPI-PROTOCOL.md`（54 KB） | GPUEB IPI 协议 | RK3588 无此机制 |
| `docs/headset-wip/findings/FINDING-pdc-early-*.md` | PDC 早启 | RK3588 走 PMU 而非 PDC |
| `docs/headset-wip/findings/MALI-MCU-FAILURE-SURFACES.md` | 失败面分类 | 直接对应症状 |
| `docs/headset-wip/findings/MMU-REASONING-REAUDIT.md` | MMU 走查复核 | 含 `0x10000 = AS0 REQ_COMPLETE` 的更正 |

`work-notes-kbase-mcu-boot-sequence.md:249` 有一条对本项目判据的直接批评，与本报告 R1 同向：

> kbase **不轮询** `JOB_INT_RAWSTAT(0x1004)`、也**不轮询** `GPU_IRQ_RAWSTAT(0x020)`。
> 它也不轮询 `glb_output.ack` / `glb_control.features`。它等的是 `kbdev+0x3020` 这个
> 内存标志（由中断路径置位后 `__wake_up` 唤醒），醒来后直接读 `glb_control.version`
> （字段偏移 0，宽度 32 位）。超时 **500 ms**。

以及 `:341`：

> `start_mcu()` 里轮询 `kJobIntRaw(0x1000) bit31` 当 booted 标志 → **判据错**。
> 应改为「读 `glb_control.version` 非 0」。

**注意这两条与 R1 的关系**：kbase 的结论是「别用中断位当判据，用 version 字」；
本报告的 R1 是「如果一定要用中断位，得读 RAWSTAT 不是 STAT，且别把 MASK 写全 1」。
两条不矛盾，**version 字仍然是最高优先级的判据**（我们的哨兵法已经实现了这一点，`:3955`）。

---

## 7. 建议的下一步（按成本排序）

| # | 动作 | 成本 | 依据 |
|---|---|---|---|
| 1 | 把 `:3897` 的 `mrd(kJobIntStatus)` 改成同时读 `mrd(kJobIntRaw)` 和 `mrd(kJobIntStatus)`，两个都打 | 极低（读寄存器） | R1，§3.2 |
| 2 | 把 `:3132` 的 `mwr(kJobIntMask, 0xffffffffu)` 改成 `0`，或在 `start_mcu()` 开头再写一次 `0` | 极低 | `Mali.cpp:543` |
| 3 | 确认 PROT 段到底是「跳过」还是「映射」，把日志文字与代码对齐 | 低（读代码） | §3.3 |
| 4 | 保持哨兵法（`:3955`）作为独立判据 —— 它不受中断通道影响 | 0（已有） | `work-notes:341` |
| 5 | 若 1+2 之后 RAWSTAT bit31 仍为 0 → 转 GPUEB/IPI 线 | 高 | 本报告不覆盖 |

**不要做**：

- 不要照抄 Ken-u 的 `0x420001c6`（我们已经验证两种都能 UPDATE 成功，它不是变量）。
- 不要在生产路径写 `CSF_CONFIG = 0`（`:4601`，那是 `mali_hwissues=1` 实验且方向与 kbase 相反）。
- 不要因为 Ken-u 不写 `0xf00`/`0x48`/`0x2c0` 就去删我们的对应代码 —— 那是 MTK 平台差异。

---

## 8. 未验证项（如实标注）

| # | 项 | 状态 |
|---|---|---|
| U1 | `JOB_INT_MASK` 的 bit31 为 1 时，`0x100c` bit31 是否**绝对**读不到 | 有 panthor + kbase 双源语义证据，但**未在 MT6895 上实测**。判据：写 mask=0 后读 `0x100c` 是否变成 `0x80000000` |
| U2 | `0x1000` RAWSTAT 当前实测值 | **从未读过**（`kJobIntRaw` 零次使用） |
| U3 | MT6895 的 `CSF_CONFIG(0xf00)` 硬件读回值 | 未读 |
| U4 | MT6895 的 `COHERENCY_PROTOCOL(0x304)` 当前值 | 未读（`:3172` 有 `before` 变量但只在 `!gMaliCold` 分支打印） |
| U5 | 我们的固件 `output_va=0x4003000` vs Ken-u 的 `0x4006000` 是否影响握手 | 未验证；`sharedAt` 有范围检查（`:563`），`0x3000` 在 `0x4000000` 段内，应无影响 |
| U6 | PROT 段映射 vs 跳过的实际取舍 | 代码是映射（`:1069`），任务描述说跳过，二者冲突，未上板验证 |
| U7 | `Ken-u/Mali` 的 `AS_MEMATTR` 与本项目 `MALI_MEMATTR` 哪个在 MT6895 上正确 | 未验证；两者都来自有出处的表（panthor vs kbase） |

---

## 附：完整扫描命令与产物

```sh
# 仓库清单
curl "https://api.github.com/users/Ken-u/repos?per_page=100&sort=updated"   # 63
curl "https://api.github.com/orgs/UniXNU/repos?per_page=100"              # 32
curl "https://api.github.com/search/repositories?q=topic:unixnu&per_page=100"  # 155
curl "https://api.github.com/repos/Ken-u/Mali/forks?per_page=100"         # 1
curl "https://api.github.com/repos/Ken-u/Mali/contributors?per_page=100"  # 1

# clone（--depth 1，全部在 /tmp）
/tmp/kenmali         Ken-u/Mali              7167b67c
/tmp/malimetal       Ken-u/MaliMetal         ed4d5496
/tmp/rk3588common    Ken-u/UniXNU-rk3588-common 10df4ce8

# 参考实现（同一 ref v6.12，避免混版本）
/tmp/pth/panthor_{fw,gpu,mmu,device}.{c,h}  panthor_regs.h

# 厂商 kbase 反汇编
/tmp/kb.dis  ← llvm-objdump -d /home/lizi/IOS/work-xaga/ko/mali_kbase_mt6895.ko
```

产物：

- 本文件
- `docs/headset-wip/findings/KEN-U-FINDINGS.json`
- `docs/headset-wip/raw/ken-u/`：7 个文本文件（Mali.cpp、Mali README/AGENTS/docs-notes、
  MaliMetal README/docs-notes、rk3588-common README）
