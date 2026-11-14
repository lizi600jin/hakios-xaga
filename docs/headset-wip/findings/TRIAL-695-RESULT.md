# trial 695 结果：JOB IRQ 只读诊断 —— mask 假说被彻底排除

> 2026-11-15 02:47 实机运行。由 DSH 执行（Codex 在收到授权前限额，4:09 AM 才恢复）。
> **这是本轮双方停工前的最后一次设备操作。**
> 全部数据来自 `findings/trial-695/`（`key-lines.txt` 30 行、`run-full.log` 4016 行）。

---

## 0. 一句话

**`MASK = 0xffffffff`（回读确认全开）下，`RAWSTAT` 和 `STAT` 的 bit31 在 7 个采样点全部为 0，
且 `raw_same=1`（读取期间 RAW 未变）。→ 「mask 挡住了 GLOBAL_IF」这个假说被排除。
真实的 `RAWSTAT` bit31 就是 0。** 加上 `version word` 未变、`halt/fatal` 未见，
两条独立判据都指向「固件从未执行」。

---

## 1. 刷机记录

| 项 | 值 |
|---|---|
| 镜像 | `logs/resume-sol-20261114-231152/trial-695-jobirq/image/boot.img` |
| 大小 | 22986752 B |
| md5 | `2cce810f64fc8486fd90e1a70512477a` |
| sha256 | `d2676827e9a51cb1ae20617f8721e7023c1d98b9d00f824b26880b9401b07bd8` |
| 内核 | `mach.development.bcm2837`，15303968 B，sha256 `18bb68da1898b0856d927f532a71e0a224cd9eac1d6ca72c6f52b44372415ea6` |
| 源码 | 分支 `trial/jobirq-695`，提交 `6a51ef2`，基线 `c9b4632` |
| 参数 | `mali_trial=695 mali_nopdc=1 headset=1`（`argument_bytes: 463`） |
| 刷入 | `fastboot flash boot_a` → `OKAY 0.073s` |
| 恢复用原厂 | `backup/boot_a_stock_real.img`，md5 `a63b2e35a7c2522cb24a6a10332b9bc8` |
| 结果 | XNU 正常运行 **166 秒**，返回 Android，`adb` 在线 |

## 2. 🔴 核心结果：7 个采样点全部 `raw 0/0 stat 0 mask 0xffffffff`

```
[   34.710644]: mali: jobirq probe trial 695 before AUTO spin 0: raw 0/0 stat 0 mask 0xffffffff raw_same 1
[   34.712701]: mali: jobirq probe trial 695 sample spin 0: raw 0/0 stat 0 mask 0xffffffff raw_same 1
[   34.721131]: mali: jobirq probe trial 695 sample spin 1000: raw 0/0 stat 0 mask 0xffffffff raw_same 1
[   35.357387]: mali: jobirq probe trial 695 sample spin 100000: raw 0/0 stat 0 mask 0xffffffff raw_same 1
[   38.881458]: mali: jobirq probe trial 695 sample spin 500000: raw 0/0 stat 0 mask 0xffffffff raw_same 1
[   44.280742]: mali: jobirq probe trial 695 loop end before any boot-success clear spin 1000000: raw 0/0 stat 0 mask 0xffffffff raw_same 1
```

（`GLOBAL_IF observed` 那个采样点从未触发，因为 `stat & kJobGlobalIf` 一直是 0 —— 这本身也是一条证据。）

### 2.1 按收窄后的判据矩阵

| R31 / S31 / M31 | 实测 | 结论 |
|---|---|---|
| **0 / 0 / 1** | ✅ 全部 7 点 | 命中 `0/0/任意` 行 |

按 Codex 收窄的矩阵：**「本次采样未观察到 GLOBAL_IF。不能证明 MCU 零执行，
也不能证明整段时间从未置位。」**

→ **单靠 jobirq 这一项，不能说「MCU 零执行」。** 下面用独立判据补。

### 2.2 但 mask 假说被排除

关键在 `mask 0xffffffff` **是回读值**，而且 `raw_same=1`：

- `MASK` bit31 = 1 → 按 vendor/panthor 语义（`mali_kbase_pm_driver.c:2134` 注释
  "Clear all interrupts, **and unmask them all**"、`:2145` 写 `0xFFFFFFFF`；
  `panthor_device.h:608` `|=`）该位**已使能**
- `raw_same=1` → 读取 `MASK`/`STAT` 前后 `RAW` 一致，**没有边沿变化干扰**
- `RAW` 和 `STAT` 的 bit31 **同时为 0**

→ **不是 mask 挡住了，是 `RAWSTAT` 本身就是 0。** 子代理 R1 的方向（`STAT = RAWSTAT & ~MASK`）
与实测不符：若真是 `& ~MASK`，`MASK=0xffffffff` 时 `STAT` 会被清成 0 但 `RAWSTAT` 应保留 bit31；
实测 `RAWSTAT` 也是 0。

**→ R1 排除。mask 极性正确、我们的读取正确、硬件确实没置位。**

## 3. 独立判据：三条都指向「固件从未执行」

| 判据 | 实测 | 含义 |
|---|---|---|
| **`version word`** | `0xff5aa5f0`（我们写的哨兵，**未变**） | 固件从未写共享区 |
| **HALT / FATAL** | `halt seen 0, fatal seen 0` | 从未进入 HALT(2) / FATAL(3) |
| **`GPU_FAULT_STATUS`** | `0` | 没有 fault |
| `MCU_STATUS` | `1`（ENABLED） | 状态机离开了复位 |

**四条合起来：MCU 停在 ENABLED，从未取指（否则会写 version 或进 fault/fatal），
也从未写共享区。**

这与 `mt8196` 的公开失败案例（`status=fatal` + `VA 0x0` fault）**不同** ——
那个是「取指了但翻译失败」，我们是「连 fault 都没有」。

## 4. 🟢 一条推翻我自己先前判断的读数：`timestamp` 在走

```
[   44.352825]: mali: clock check: cycle 0xae473bbc -> 0xae4c7b83, timestamp 0x225d8280 -> 0x225dbcbf
```

- `cycle`：`0xae473bbc → 0xae4c7b83`，差 `0x532fc7`（在走）
- **`timestamp`：`0x225d8280 → 0x225dbcbf`，差 `0x3a3f`（在走）**

**我先前说「`timestamp 0 -> 0` 是测量缺陷」—— 那条作废。**
`timestamp` 现在明确在走，说明：
1. `mfg_set_timestamp()` 确实生效（Codex 说的「NOPDC 实测在走」成立）
2. 历史日志里那个 `timestamp 0 -> 0` 是**那次运行的特殊情况**，不是必然

另外 `:44.364731` 显示 `timestamp 0x3` —— 写入 `0x13FBF130 = 3` 的值被保留。

## 5. 其余实测（与历史一致，无变化）

| 项 | 值 | 说明 |
|---|---|---|
| `PWR_STATUS` | `0x1fc / 0x1fc`（scpsys `0xF34`/`0xF38`），**gpufreq `0x3ffc`**（`0xF3C`） | 12 域全 up（厂商判据 `0xF3C & 0x3FFC`） |
| `AS0 transcfg` | `0x42000006` | 原厂 kbase 值 |
| `AS0 readback` | `transtab 0x1a6d70000 memattr 0x84c8d888d8f88 transcfg 0x42000006` | 三写回读一致 |
| `AS0 update` | mmu irq **`0x10000`** | bit16 = REQ_COMPLETE（**不是 BUS_ERROR**） |
| `sentinel` | 写入 `0xff5aa5f0` | |
| `GPU fault` | `0 at 0` | |
| `SHADER_READY` | `0` | 启动前正常态（`Ken-u` 的 M2b 证实 FW 自己上电） |
| `L2_READY` | `0x1` | |
| `pdc` | `setup skipped (mali_nopdc=1)`；`sc0 0 sc9 0 cg 0 st0 0 st4 0 st6 0` | 本 trial 用 `mali_nopdc=1` |
| 循环 | `1000000 spins`（约 9.6 s） | |
| XNU 运行时长 | 166 s（`loader_return_after=150`） | 正常返回 Android |

## 6. 结论与下一步

### 6.1 已排除（本轮确定）

| 假说 | 排除依据 |
|---|---|
| **mask 挡住了 GLOBAL_IF** | 7 点 `raw 0/0 stat 0 mask 0xffffffff raw_same 1`；`RAWSTAT` 本身为 0 |
| **`STAT = RAWSTAT & ~MASK`** | 若如此 `RAWSTAT` 应保留 bit31，实测为 0 |
| **时钟问题** | `cycle` 与 `timestamp` 都在走，`PLL` 已锁 |
| **电源域不足** | `0xF3C = 0x3ffc`（12 域全 up，厂商判据） |
| **AS0 走查失败** | `mmu irq 0x10000` = REQ_COMPLETE（走查**成功完成**） |
| **`PDC` 缺失** | `mali_nopdc=1` 与默认路径都是同一现象；且 `pdc_report` 全 0 说明写入未落地但 MTCMOS 仍全 up |
| **`CSF_CONFIG`/`hw-issues`/`ASN_HASH`/`L2_CONFIG`** | 子代理在 `Ken-u` 全文零命中仍能启动；`ASN_HASH` 对 arch<11 不适用 |
| **`SHADER_READY=0` 异常** | 启动前正常态 |

### 6.2 剩下唯一未解释的现象

> **`MCU_STATUS = 1 (ENABLED)`，`RAWSTAT` bit31 从未置位，`version word` 未变，
> 无 HALT、无 FATAL、无 GPU fault。**

公开世界**没有**这个状态的先例（只有 `fatal` 和 `disabled`）。
`mt8196` 的 `fatal` 案例是「取指了但翻译失败」；我们是「连 fault 都没有」。

### 6.3 下一步候选（未验证，按成本排序）

| # | 实验 | 成本 | 依据 |
|---|---|---|---|
| 1 | **固件 0.1 → 0.3 单变量**（上游 `a27847ea` 或 xaga 原厂 `9f67b2c3`） | 一次刷机 | 我们的是 0.1、`Ken-u` 用 0.3；但 0.1 自身自洽，不能断言 0.1 有错 |
| 2 | **PROT 段改跳过**（对齐 panthor） | 一次刷机 | `panthor_fw.c:588-592` 明确跳过；我们映射普通页 |
| 3 | **`0x2800000` 只读影子段改 W** | 一次刷机 | 我们独有，flags `0x80000009`（无 W），数据与 `0x2000000` 逐字节相同 |
| 4 | **`0x4000000` 共享窗口 606208 B → 65536 B** | 一次刷机 | 我们声明区间比 0.3 大近 10 倍 |
| 5 | **GPUEB IPI 上电** | 一次刷机 | panthor 在 `mediatek,mt6895` 下条件调用 `mt6895_gpueb_power_on()`；我们从未发过 IPI |

**每条都必须单变量、独立 trial、完整哈希登记。**
冻结的 `5e7c67f7`（`headset=1` 首轮）**不替换**。

## 7. 未验证 / 边界

- 「MCU 零执行」是通过 `version` 未变 + 无 fault + 无 HALT/FATAL **三条独立判据**推的，
  不是 `jobirq` 单项直接证明（按收窄矩阵，`0/0` 不能单独下这个结论）
- 本 trial 用 `mali_nopdc=1`，与默认 PDC 路径的对比历史已有（现象相同）
- `timestamp` 在走，但**未测**它在 `mali_nopdc=0` 时是否也走（历史说法是「NOPDC 实测在走」）
- 固件 0.3 与本机的运行时 CSF ABI 兼容性**未验证**（`CSF_IFACE_VERSION` 要 MCU 跑起来才能读）
- 本 trial 的诊断本身有 MMIO + printf 开销（共 7 次快照，每次 4 次 MMIO 读 + 1 次 printf）


---

## 8. 🔴 刷机后 Android 未完全启动，已刷回原厂（2026-11-15 03:05）

### 8.1 现象

刷 trial 695 后 XNU 正常跑完 166 秒、日志完整拉出，但**返回 Android 后 `sys.boot_completed`
一直未置位**（等 4 分钟以上，load 稳定在 20 = CPU 满载）。

`logcat -b crash` 有 **38 条**记录，栈指向：

```
SkiaGLRenderEngine::chooseEglConfig(void*, int, bool)+600
SkiaGLRenderEngine::create(android::renderengine::RenderEngineCreationArgs const&)+156
RenderEngineThreaded::threadMain(...)+540
/system_ext/lib64/libsurfaceflinger.so
```

**`surfaceflinger` 的 Skia GL RenderEngine 创建失败并反复崩溃 → Android 图形栈起不来。**

用户观察到屏幕卡了 8 分钟以上，手动回 fastboot。

### 8.2 恢复

```sh
fastboot flash boot_a backup/boot_a_stock_real.img     # md5 a63b2e35a7c2522cb24a6a10332b9bc8
fastboot reboot
```

**结果**：`sys.boot_completed = 1`，Android **17** 正常启动。

设备侧核实（`su -c md5sum /dev/block/by-name/boot_a`）：
```
a63b2e35a7c2522cb24a6a10332b9bc8  /dev/block/by-name/boot_a
```
**与 `backup/boot_a_stock_real.img` 逐字节一致 —— 原厂镜像已完整恢复。**

### 8.3 一个此前没记录的现象（待查）

这是本项目**第一次**出现「XNU 跑完但 Android 起不来」。

历史上（`logs/headset-resume-20261114-220244/nopdc/device-final.txt:1`）同样 `mali=5 mali_nopdc=1`
的镜像返回 Android 后 `boot_completed = 1` 正常。

trial 695 与它的差异**只有 18 行只读诊断**（`jobirq_snapshot`）——**从代码上无法解释。**

**可能方向（全部未验证）**：
- 诊断的 7 次快照在 `start_mcu` 内增加了约 6 秒总时长（`34.71s → 44.28s`），
  可能改变了 XNU 返回时的 GPU/显示收尾时序
- 历史那次的 `mali_trial` 不是 695，可能有其他参数差异
- 与本 trial 无关的偶发（`surfaceflinger` 崩溃也可能由别的因素引起）

**⚠️ 结论：这条现象必须记录在案，下次刷机实验前要先确认它可复现，且恢复路径可用。**

### 8.4 屏幕截图里的重要信息

用户在手动回 fastboot 前拍到的屏幕显示：

```
mfg2 back to 0x1112
pdc down; iso_con. 0xf30 0x1701
gpu: the domains are back as Android expects them
starting Android, so that the log above can be pulled
starting Android

=== XNU log (469781 bytes) ===
=== end of XNU log ===
logbook: 458 KB at byte 0x1dc97f8
000 of xnusys
mfg1 back to 0x1112
```

**三条独立的重要事实：**

1. **`XNU log (469781 bytes)` + `logbook: 458 KB at byte 0x1dc97f8`** ——
   **日志修复真的生效了。** 历史上正常返回时 HLOG 被 `probe_flash(38u)` 覆盖成 **79 字节**，
   这次是 **458 KB 完整日志**。这是 Codex 那三个日志 bug 修复的直接证据。
2. **`gpu: the domains are back as Android expects them`** + `mfg2/mfg1 back to 0x1112` +
   `pdc down; iso_con. 0xf30 0x1701` —— **XNU 返回前把 GPU 电源域还原了**，
   这条路径是工作的。
3. **屏幕同时显示了一段 panic/backtrace**（`(no panic string)`、`TCR_EL1 0x22a51a251a`、
   `pc 0x19d6abf10 sym debugger_panic`、backtrace 里有 `_loader_jump`、`_psci_smp_stop_here`、
   `_gic_irq_handler`、`_sleh_irq`、`_machine_idle`、`_processor_idle`、`_idle_thread`、
   `_Call_continuation`）—— 这看起来是 **XNU 返回路径上的一个异常记录**，
   但标注 `(no panic string)`，且系统仍完成了「返回 Android」的流程。

   **⚠️ 这段未解读，下次接手时应优先看 `logbook` 里 `0x1dc97f8` 处的 458 KB 内容。**
