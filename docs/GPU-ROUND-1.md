# Round 1：冷启动逐写对账与可验证假设

日期：2026-11-12（Asia/Shanghai）。分析先于本轮任何驱动修改、构建和刷入。手机后由用户明确授权交给 Codex；本文的原始 logbook 是接手前的证据，不是本轮实验结果。发布仓库使用 `/home/lizi/IOS/repo/hakios-mt6895`，Orca 工作区仅作参考。

## 1. 结论先行

**“启动前寄存器已与 Android 冷路径逐条对齐”不成立。** 冷启动 record 0..135 共 **67 次写**。对提供的 logbook 所选择的源码路径重建，共 **94 次 GPU aperture 写**：61 次与 Android 同值，6 次同 offset 不同值，另有 27 次附加写。6 次值差异里，2 次是 AS0 物理分配地址差异，另外 **4 次是漏掉的 AS2、AS3 MEMATTR 非零值**。

另有三处必须先更正文档的证据错误：

1. **提供的 logbook 跑的是 `mali_cold=0`。** 原始字节中存在完整启动参数 `mali=6 mali_cold=0 mali_preas0=1`，并且 `soft reset done`、`power key written`、`asn_hash ... wrote them`、`caches flushed` 四组行互相印证。不能拿这份日志证明“冷路径也失败”或“这次 cold=0 没有到 MCU 启动”。此前另一轮 cold=0 不回 Android 的历史结论仍按禁用项保留，本轮不重试。
2. **record 9..17 是 AS2，不是 AS1。** 地址计算是 `(0x24b0-0x2430)/0x40 = 2`。整个 prologue 为 AS0、AS2、AS3、AS4、AS5、AS6、AS7；AS1 没有出现。`as_park_unused()` 从 2 开始本身正确，错在 AS2/AS3 的 MEMATTR 被全清零。
3. **Android 冷启动轨迹没有读过 `GPU_STATUS(0x34)`。** 不能从“没有读”推导“恒 0”。三份 reset 轨迹各三次的 0 读数出现在启动前的 reset/诊断阶段，不能与本机 AUTO 之后的 1 作同阶段比较。原始 logbook 还明确显示 `soft reset done: status 0`。因此现有证据不足以建立“GPU_STATUS 在 AUTO 前为 1，所以拒绝 AUTO”的因果链。

**优先级：建立有明确 boot-args、写操作轨迹和返回 Android 结果的纯冷路径基线 → 验证 free-run → 单独追平 AS2/AS3 属性。** 不能据现有差分宣称已找到 GPU 根因，也不能再把 SoC 门/保护当作唯一可能。

## 2. 证据来源、恢复与对账边界

最初 `codex/evidence/` 实际只有三个 txt 与 logbook，缺少 `hist_boot.txt`。接管手机后，只读发现手机已保存 `/data/local/tmp/hist_before.txt`，首行为 `Last 2460 register accesses of 2460 total:`。复制恢复为本地 `evidence/hist_boot.txt`；未触发 Android GPU reset，未覆盖其它原始证据。

恢复文件满足全部锚点：rec0=AS0 TRANSCFG、rec98=L2_PWRON、rec136=MCU_CONTROL=2、rec163=JOB_INT_STATUS bit31、rec174=doorbell。三份 reset 轨迹的 first AUTO 分别为 680、648、500 条文件内未出现；不能以 reset prologue 替代 cold trace。

| 文件 | SHA-256 |
|---|---|
| 恢复的 hist_boot.txt，115663 B / 2460 records | `882197bd7362fc6bc80aa0e0f858d421bb9dcbdc796c896b13e18d8705db7831` |
| xaga-logbook.bin，4198400 B | `c62dae75ab5000e5ab60e26680034d89dd36bc7692e0285560b0791ef313ae05` |
| 分析时驱动源码，193594 B / 4217 lines | `76745840ef6e9ae8fb1740a87d57a7438db21de1852302d40a099f88c2893566` |

发布仓库副本、Orca patch 提取出的驱动和主控源码的上述 SHA-256 完全相同。下面源码行号固定指这个快照，后续修改会移动行号。

解析按指定正则处理整份二进制，不按 72 B header 截断，找到 155 条带时间戳的 mali 行（22.445541..43.831804，时间单调）。另保留两段不带 `mali:` 前缀的 MCU 窗口续行；只提取正则匹配行会把这两个关键读数漏掉。更早 SoC 上电日志被 ring 覆盖的部分不补造。

**重要可观测性边界：原 logbook 不是逐 MMIO 写 trace。** 它打印阶段、读回值和 boot-args，不打印每次 store。下表的 Android 列是直接原始记录；“本机 Sxx”是用相同哈希源码、实际 boot-args 与阶段日志重建的写序号，不是设备实录 record。源码中 `gpu_hw_issues()` 当前只有读取，虽然日志说 hardware issues，也不能据此虚构四次 `0xf00..0xf0c` 写。下一轮会加只记录实际 GPU store 的小缓存，补齐动态对账。

对账范围是 Android `regs_history` 覆盖的 Mali GPU aperture。SPM、MFGCFG、PLL 等 SoC 写不在这份 Android history 的观测范围内；不能因为没记录就判 Android 没写。DRAM 页表、哨兵和 GLB_REQ 也不算寄存器写。

## 3. record 0..135 每一次 Android 写与本机路径逐条对应

基址严格由 `rec0.addr - 0x2430` 推出：`0xffffffc015800000`。`AS(n)=0x2400+n*0x40`。`S01..S94` 为本机源码重建的执行顺序。表中每个 Android record 都单独列出，不合并高低字，不把读操作算成写。

| Android record | 寄存器 / offset | Android 写值 | 本机写值 | 本机重建序号 | 判定 |
|---:|---|---|---|---:|---|
| 0 | `AS0.TRANSCFG_LO / 0x02430` | `0x42000006` | `0x42000006` | S78 | 相同 |
| 1 | `AS0.TRANSCFG_HI / 0x02434` | `0x00000000` | `0x00000000` | S79 | 相同 |
| 2 | `AS0.TRANSTAB_LO / 0x02400` | `0x44b49000` | `0x50000000` | S72 | 物理分配差异（不能抄地址） |
| 3 | `AS0.TRANSTAB_HI / 0x02404` | `0x00000001` | `0x00000000` | S73 | 物理分配差异（不能抄地址） |
| 4 | `AS0.MEMATTR_LO / 0x02408` | `0x888d8f88` | `0x888d8f88` | S75 | 相同 |
| 5 | `AS0.MEMATTR_HI / 0x0240c` | `0x00084c8d` | `0x00084c8d` | S76 | 相同 |
| 7 | `AS0.COMMAND / 0x02418` | `0x00000001` | `0x00000001` | S81 | 相同 |
| 9 | `AS2.TRANSCFG_LO / 0x024b0` | `0x42000001` | `0x42000001` | S09 | 相同 |
| 10 | `AS2.TRANSCFG_HI / 0x024b4` | `0x00000000` | `0x00000000` | S10 | 相同 |
| 11 | `AS2.TRANSTAB_LO / 0x02480` | `0x00000000` | `0x00000000` | S05 | 相同 |
| 12 | `AS2.TRANSTAB_HI / 0x02484` | `0x00000000` | `0x00000000` | S06 | 相同 |
| 13 | `AS2.MEMATTR_LO / 0x02488` | `0x888d8f88` | `0x00000000` | S07 | 缺失的冷路径写值；本机写成 0 |
| 14 | `AS2.MEMATTR_HI / 0x0248c` | `0x00084c8d` | `0x00000000` | S08 | 缺失的冷路径写值；本机写成 0 |
| 16 | `AS2.COMMAND / 0x02498` | `0x00000001` | `0x00000001` | S11 | 相同 |
| 18 | `AS3.TRANSCFG_LO / 0x024f0` | `0x42000001` | `0x42000001` | S17 | 相同 |
| 19 | `AS3.TRANSCFG_HI / 0x024f4` | `0x00000000` | `0x00000000` | S18 | 相同 |
| 20 | `AS3.TRANSTAB_LO / 0x024c0` | `0x00000000` | `0x00000000` | S13 | 相同 |
| 21 | `AS3.TRANSTAB_HI / 0x024c4` | `0x00000000` | `0x00000000` | S14 | 相同 |
| 22 | `AS3.MEMATTR_LO / 0x024c8` | `0x888d8f88` | `0x00000000` | S15 | 缺失的冷路径写值；本机写成 0 |
| 23 | `AS3.MEMATTR_HI / 0x024cc` | `0x00084c8d` | `0x00000000` | S16 | 缺失的冷路径写值；本机写成 0 |
| 25 | `AS3.COMMAND / 0x024d8` | `0x00000001` | `0x00000001` | S19 | 相同 |
| 27 | `AS4.TRANSCFG_LO / 0x02530` | `0x42000001` | `0x42000001` | S25 | 相同 |
| 28 | `AS4.TRANSCFG_HI / 0x02534` | `0x00000000` | `0x00000000` | S26 | 相同 |
| 29 | `AS4.TRANSTAB_LO / 0x02500` | `0x00000000` | `0x00000000` | S21 | 相同 |
| 30 | `AS4.TRANSTAB_HI / 0x02504` | `0x00000000` | `0x00000000` | S22 | 相同 |
| 31 | `AS4.MEMATTR_LO / 0x02508` | `0x00000000` | `0x00000000` | S23 | 相同 |
| 32 | `AS4.MEMATTR_HI / 0x0250c` | `0x00000000` | `0x00000000` | S24 | 相同 |
| 34 | `AS4.COMMAND / 0x02518` | `0x00000001` | `0x00000001` | S27 | 相同 |
| 36 | `AS5.TRANSCFG_LO / 0x02570` | `0x42000001` | `0x42000001` | S33 | 相同 |
| 37 | `AS5.TRANSCFG_HI / 0x02574` | `0x00000000` | `0x00000000` | S34 | 相同 |
| 38 | `AS5.TRANSTAB_LO / 0x02540` | `0x00000000` | `0x00000000` | S29 | 相同 |
| 39 | `AS5.TRANSTAB_HI / 0x02544` | `0x00000000` | `0x00000000` | S30 | 相同 |
| 40 | `AS5.MEMATTR_LO / 0x02548` | `0x00000000` | `0x00000000` | S31 | 相同 |
| 41 | `AS5.MEMATTR_HI / 0x0254c` | `0x00000000` | `0x00000000` | S32 | 相同 |
| 43 | `AS5.COMMAND / 0x02558` | `0x00000001` | `0x00000001` | S35 | 相同 |
| 45 | `AS6.TRANSCFG_LO / 0x025b0` | `0x42000001` | `0x42000001` | S41 | 相同 |
| 46 | `AS6.TRANSCFG_HI / 0x025b4` | `0x00000000` | `0x00000000` | S42 | 相同 |
| 47 | `AS6.TRANSTAB_LO / 0x02580` | `0x00000000` | `0x00000000` | S37 | 相同 |
| 48 | `AS6.TRANSTAB_HI / 0x02584` | `0x00000000` | `0x00000000` | S38 | 相同 |
| 49 | `AS6.MEMATTR_LO / 0x02588` | `0x00000000` | `0x00000000` | S39 | 相同 |
| 50 | `AS6.MEMATTR_HI / 0x0258c` | `0x00000000` | `0x00000000` | S40 | 相同 |
| 52 | `AS6.COMMAND / 0x02598` | `0x00000001` | `0x00000001` | S43 | 相同 |
| 54 | `AS7.TRANSCFG_LO / 0x025f0` | `0x42000001` | `0x42000001` | S49 | 相同 |
| 55 | `AS7.TRANSCFG_HI / 0x025f4` | `0x00000000` | `0x00000000` | S50 | 相同 |
| 56 | `AS7.TRANSTAB_LO / 0x025c0` | `0x00000000` | `0x00000000` | S45 | 相同 |
| 57 | `AS7.TRANSTAB_HI / 0x025c4` | `0x00000000` | `0x00000000` | S46 | 相同 |
| 58 | `AS7.MEMATTR_LO / 0x025c8` | `0x00000000` | `0x00000000` | S47 | 相同 |
| 59 | `AS7.MEMATTR_HI / 0x025cc` | `0x00000000` | `0x00000000` | S48 | 相同 |
| 61 | `AS7.COMMAND / 0x025d8` | `0x00000001` | `0x00000001` | S51 | 相同 |
| 63 | `GPU_IRQ_CLEAR / 0x00024` | `0x00080503` | `0x00080503` | S52 | 相同 |
| 64 | `GPU_IRQ_MASK / 0x00028` | `0x00080503` | `0x00080503` | S53 | 相同 |
| 65 | `JOB_IRQ_CLEAR / 0x01004` | `0xffffffff` | `0xffffffff` | S54 | 相同 |
| 66 | `JOB_IRQ_MASK / 0x01008` | `0xffffffff` | `0xffffffff` | S55 | 相同 |
| 67 | `MMU_IRQ_CLEAR / 0x02004` | `0xffffffff` | `0xffffffff` | S56 | 相同 |
| 68 | `MMU_IRQ_MASK / 0x02008` | `0x0000ffff` | `0x0000ffff` | S57 | 相同 |
| 69 | `IPA_SELECT / 0x40010` | `0x20181004` | `0x20181004` | S58 | 相同 |
| 70 | `IPA_SELECT / 0x40014` | `0x00000000` | `0x00000000` | S59 | 相同 |
| 71 | `IPA_SELECT / 0x40018` | `0x00000000` | `0x00000000` | S60 | 相同 |
| 72 | `IPA_SELECT / 0x4001c` | `0x00000000` | `0x00000000` | S61 | 相同 |
| 73 | `IPA_SELECT / 0x40020` | `0x00000000` | `0x00000000` | S62 | 相同 |
| 74 | `IPA_SELECT / 0x40024` | `0x00000000` | `0x00000000` | S63 | 相同 |
| 75 | `IPA_SELECT / 0x40028` | `0x00000000` | `0x00000000` | S64 | 相同 |
| 76 | `IPA_SELECT / 0x4002c` | `0x00000000` | `0x00000000` | S65 | 相同 |
| 78 | `IPA_COMMAND / 0x40000` | `0x00000001` | `0x00000001` | S66 | 相同 |
| 80 | `IPA_COMMAND / 0x40000` | `0x00000003` | `0x00000003` | S67 | 相同 |
| 82 | `IPA_TIMER / 0x40008` | `0x00000000` | `0x00000000` | S68 | 相同 |
| 98 | `L2_PWRON / 0x001a0` | `0x00000001` | `0x00000001` | S88 | 相同 |

Android 全部 67 条写操作都找到同 offset 的本机操作；61 条值相同，6 条值不同（4 条 MEMATTR、2 条 PA 字）。除此之外，本机多出下列 27 次写操作。

| 本机重建序号 | offset / 寄存器 | 写值 | 源码行 | 归类、冷启动必要性 |
|---:|---|---|---:|---|
| S01 | `0x00024 / GPU_IRQ_CLEAR` | `0x00000100` | 2908 | 本机 reset 分支遗留；冷启动没有，不能搬进冷路径 |
| S02 | `0x00030 / GPU_COMMAND` | `0x00000101` | 2909 | 本机 reset 分支遗留；冷启动没有，不能搬进冷路径 |
| S03 | `0x00024 / GPU_IRQ_CLEAR` | `0x00000100` | 2921 | 本机 reset 分支遗留；冷启动没有，不能搬进冷路径 |
| S04 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 2874 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |
| S12 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 2874 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |
| S20 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 2874 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |
| S28 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 2874 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |
| S36 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 2874 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |
| S44 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 2874 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |
| S69 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 3841 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |
| S70 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 2352 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |
| S71 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 2353 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |
| S74 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 2354 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |
| S77 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 2355 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |
| S80 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 2368 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |
| S82 | `0x00050 / PWR_KEY` | `0x2968a819` | 2585 | 本机 reset 分支遗留；本次三份 Android reset 轨迹也未写，非 vendor 必需 |
| S83 | `0x00304 / COHERENCY_PROTOCOL` | `0x0000001f` | 2607 | 本机 reset 分支遗留；冷启动没有，不能搬进冷路径 |
| S84 | `0x002c0 / ASN_HASH` | `0x0000000b` | 2634 | 本机 reset 分支遗留；本次三份 Android reset 轨迹也未写，非 vendor 必需 |
| S85 | `0x002c4 / ASN_HASH` | `0x0000000e` | 2634 | 本机 reset 分支遗留；本次三份 Android reset 轨迹也未写，非 vendor 必需 |
| S86 | `0x002c8 / ASN_HASH` | `0x00000000` | 2634 | 本机 reset 分支遗留；本次三份 Android reset 轨迹也未写，非 vendor 必需 |
| S87 | `0x00048 / L2_CONFIG` | `0x07130000` | 2636 | 本机 reset 分支遗留；本次三份 Android reset 轨迹也未写，非 vendor 必需 |
| S89 | `0x00024 / GPU_IRQ_CLEAR` | `0x00000600` | 2725 | 调试附加清电源 IRQ；冷启动 AUTO 前没有，非启动必需 |
| S90 | `0x00024 / GPU_IRQ_CLEAR` | `0x00000600` | 2742 | 调试附加清电源 IRQ；冷启动 AUTO 前没有，非启动必需 |
| S91 | `0x00024 / GPU_IRQ_CLEAR` | `0x00020000` | 3011 | 本机 reset 分支遗留；冷启动没有，不能搬进冷路径 |
| S92 | `0x00030 / GPU_COMMAND` | `0x00003304` | 3012 | 本机 reset 分支遗留；冷启动没有，不能搬进冷路径 |
| S93 | `0x00024 / GPU_IRQ_CLEAR` | `0x00020000` | 3024 | 本机 reset 分支遗留；冷启动没有，不能搬进冷路径 |
| S94 | `0x02004 / MMU_IRQ_CLEAR` | `0xffffffff` | 3144 | 调试附加清锁存；冷启动无对应写，非 reset 专属，非启动必需 |


### 3.1 差集的解释与必要性判断

**Android 有而本机没有的四个 `(offset,value)`：**

| record | offset | Android | 本机 | 判据 |
|---:|---|---|---|---|
| 13 | `0x2488` AS2.MEMATTR_LO | `0x888d8f88` | `0` | 在真正冷启动 prologue 内，非 reset 残留 |
| 14 | `0x248c` AS2.MEMATTR_HI | `0x00084c8d` | `0` | 同上 |
| 22 | `0x24c8` AS3.MEMATTR_LO | `0x888d8f88` | `0` | 同上 |
| 23 | `0x24cc` AS3.MEMATTR_HI | `0x00084c8d` | `0` | 同上 |

它们是**确实漏掉的冷路径写值**，但“冷启动出现”不等于“启动因果上不可缺”。AS2/AS3 的 ADRMODE 同时为 UNMAPPED，正常不承载 MCU AS0 取指；所以不能仅凭这四个差异宣称根因。适合单独补齐做一轮对照。AS4..AS7 原本就是 MEMATTR=0，不能把所有 parked AS 一概改成非零。

**另外两个值差异是物理地址：** Android 写 AS0 TRANSTAB_LO=`0x44b49000`、HI=`1`，合起来是 **`0x0000000144b49000`**，不是“低端 DRAM 0x44b49000”。本机为 LO=`0x50000000`、HI=`0`。地址应指向各自真实分配的根表；保持已验证的本机 root，不能抄 Android 的地址。

**本机多出的 27 次写：** 上表已经逐次枚举。13 次是重复的 `MMU_IRQ_CLEAR=0xffffffff`，2 次是 `GPU_IRQ_CLEAR=0x600`，均为调试/settle 逻辑，不能硬归为 vendor reset 必需步骤；其余 12 次来自本机 `!gMaliCold` 分支。它们不是新增的“待补冷初始化”：改用显式 `mali_cold=1` 即移除 reset、PWR_KEY、coherency、ASN 和 cache flush 等操作。PWR_KEY、ASN、L2_CONFIG 甚至在本次三份 Android reset 证据里也没有对应写，不要称作厂商 reset 必需。

二分成“必需冷启动”与“reset 残留”不足以覆盖全部差集，所以明确保留“调试附加”这一类，避免给重复 IRQ 清除虚构因果。冷路径已出现的差异仍只有上述四个 MEMATTR 字；PA 差异是配置实例差异。

### 3.2 顺序差异也是差异

Android 为 `AS0 → AS2..7 → IRQ → IPA → L2_PWRON → AUTO`；本机提供日志对应 `soft reset → AS2..7 → IRQ → IPA → load firmware/AS0 → PWR_KEY/coherency/ASN → L2 → clear/flush → AUTO`。

每个 AS 内，Android 写 `TRANSCFG → TRANSTAB → MEMATTR → UPDATE`；本机写 `TRANSTAB → MEMATTR → TRANSCFG → UPDATE`。本机 AS0 虽然 `mali_preas0=1`，仍在 IRQ/IPA **之后**，不是整条 prologue 的第一个 AS。没有证据说明这些顺序差异会致命，但也不能再写“逐条相同”。UPDATE 前三项最终值一致时，一般只在 UPDATE 消费；是否存在实现相关副作用需实录验证，优先级低于先获得正确冷路径基线。

## 4. 寄存器现值与同阶段快照

| 项 | Android | 提供的本机 logbook | 含义/边界 |
|---|---|---|---|
| GPU_ID | `0xa8670004`，rec95 | `0xa8670004` | 一致 |
| AS0 TRANSCFG | `0x42000006`，rec0 | 回读 `0x42000006`，32.932335 | 一致 |
| AS0 MEMATTR | `0x00084c8d888d8f88` | 同值回读 | 一致 |
| AS0 TRANSTAB | `0x144b49000` | `0x50000000` | 各自真实 PA，非可直接复制常量 |
| AS2/3 MEMATTR | 非零，rec13/14/22/23 | 源码写 0；日志只证明 parked UPDATE 完成 | 未对齐；下一轮应直接打印写值 |
| AS1 | rec0..135 完全未访问 | 完全不配置 AS1 | 一致，不能补“AS1 parking” |
| IRQ 三个 mask | `0x80503 / 0xffffffff / 0xffff` | 同值路径写入 | mask 配置相同 |
| L2_READY | rec119=1（此时 PWRTRANS 仍 1），rec122 PWRTRANS 才 0 | L2 up: trans0 active0 ready1 | 稳态一致；BRIEF 用“先 TRANS↓再 READY↑”概括并不精确 |
| GPU_STATUS before AUTO | 冷轨迹未采；reset 文件只在 reset 前后读 0 | soft reset 后 0；AUTO 前无独立 0x34 采样 | 不能建立启动前 1 vs 0 的对照 |
| MCU_STATUS before AUTO | 冷轨迹未采 | 33.115867=0 | 无异常前置状态证据 |
| AUTO → GLOBAL_IF | rec136 → rec163，27 条访问距离 | 百万 spins 从无 bit31 | 确定启动握手失败 |
| MCU_STATUS after failed AUTO | 冷轨迹未采同阶段值 | 1，写 DISABLE 后仍 1 | 控制请求未完成退回 |
| `0x3020` | 四份 history 全无访问 | before/after 均 `0x600` | 不是已知“漏写值” |
| `0x3090` | 四份 history 全无访问 | `0x03a4e70b → 0xaf834a6f` | 约 299.8 MHz 的模 2^32 增量 |
| `0x3098` | 四份 history 全无访问 | `0x19aa3e88 → 0x211dbe18` | 约 13.0 MHz 的模 2^32 增量 |
| MFG_DEBUG_TOP | Android history 不覆盖 | 早期 idle4，AUTO 后 busy1b | 反映有未完成活动，不直接给出事务类型或堵塞位置 |

补充：`JOB_IRQ_MASK=0xffffffff` 能看到 bit31，足以排除这次启动前 mask 取值不同；**不能由此推导 STATUS 不受 MASK 影响**。冷轨迹里它在 rec386 等处又写 0，并非“全程从未解开”。标准 Mali interrupt mask 的 1 是使能位，不能把全 1 解释成“全部屏蔽”。可参照 [Panthor 的 FW 启动实现](https://raw.githubusercontent.com/torvalds/linux/v6.16/drivers/gpu/drm/panthor/panthor_fw.c)：启动前以 `~0` resume JOB IRQ，之后才写 AUTO。

`AS UPDATE` 的上半 MMU IRQ 是 REQ_COMPLETE，本轮沿用已确认结论，不再当 bus error。哨兵未改加无 GLOBAL_IF 可以证明“未到可见固件启动里程碑”；若无独立 PC/取指跟踪，仅凭哨兵不能严格证明**一条指令也没执行**。`debug_top=0x1b` 同样不能单独证明“已发出取指且一定被 EMI 拒绝”。这不是改查已排除项，而是限制因果断言的强度。

## 5. 三个 MCU 窗口读数：假设与可证伪判据

公开 kbase 头定义 `GPU_CONTROL_MCU_BASE=0x3000`，而常规 GPU 控制块的 IRQ RAWSTAT、GPU_STATUS、CYCLE_COUNT、TIMESTAMP 偏移分别为 `0x20 / 0x34 / 0x90 / 0x98`。来源：[Arm kbase CSF 寄存器头（Google 发布副本）](https://nest-open-source.googlesource.com/manifest_repos/mali-driver/+/0f8397eced2de6bc649a9cc32d0fae77a1dc34dc/bifrost/r44p0/kernel/drivers/gpu/arm/midgard/gpu/backend/mali_kbase_gpu_regmap_csf.h)、[Linux Panthor GPU 寄存器头](https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/panthor/panthor_gpu_regs.h)。**公开符号只证明基址和常规偏移，下面“MCU 视图映射”是推断，尚非已取得 G610 TRM 的定义。**

### H1：0x3020 是 MCU 视图的 GPU_IRQ_RAWSTAT

`0x600=BIT(9)|BIT(10)` 正好是 POWER_CHANGED_SINGLE 与 POWER_CHANGED_ALL。主 CPU 的 `0x20` 被本机 settle 逻辑清零后，`0x3020` 仍保留 0x600，因此即使地址布局对应，也可能是**独立中断目的端锁存**，不是同一存储单元的简单 alias。此时不能靠“清 AP IRQ 没清 MCU 视图”就判成故障；已挂起电源变化 IRQ 也可能是正常状态。

验证：仅在已知可读 GPU 窗口内，对现有电源变化清除操作前后，同一行打印 `0x20 / 0x24对应已有写值 / 0x3020 / 0x34 / 0x3034`；不新增对未知 MCU 窗口的写。若 `0x20` 随现有 clear 清零而 `0x3020` 保持，支持独立 latch；若二者总同时变化，支持共享视图。**不建议盲写 0x3024 或 0x3020=0x600**，首先没有因果证据，其次会改变 MCU 自身中断状态。

### H2：0x3090 是 GPU cycle counter 的 MCU 视图

两次窗口 dump 的时间差为 9.616247 s，低字增量 `(0xaf834a6f-0x03a4e70b) mod 2^32 = 2883478372`，平均约 **299.85 MHz**，与 MFGPLL 约 300 MHz 相符。计数器在 AUTO 之前已经非零，足以否定“只有固件执行才会动”的解释；它并不是 MCU PC。

验证：一次采样按 `0x90_before → 0x3090 → 0x90_after` 顺序读取，判断 MCU 值是否落在 AP 两读包围的模 2^32 区间；间隔 1 ms 再做一次，并在 MCU_CONTROL=0 的基线阶段重复。数值被包围且斜率一致支持视图假设；稳定偏移或明显不同斜率则否定简单 alias，但仍可属于另一时钟计数器。跨低字回卷要按无符号差计算。

### H3：0x3098 是系统 timestamp 的 MCU 视图

同样时间差内增量 `0x07737f90=125009808`，约 **13.00 MHz**，符合系统定时器源而非 300 MHz GPU 时钟。与 H2 组合，可以解释“MCU 看起来活体”的全部跳动，而无需假设固件已执行。

验证：`0x98_before → 0x3098 → 0x98_after` 的包围读与 1 ms 斜率采样；另外保持 MCU 未启动时测量，确认计数仍推进。当前四份 Android history 均未访问 0x3000..30ff，不能给出任何 Android 的这些寄存器“对照值”。如需 Android 侧读数，应在 AP 驱动保证 GPU runtime-active 的条件下有界采样，不能在已下电状态直接 devmem。

## 6. 下一轮具体改动与结果判据

### E0：先固定纯冷路径基线，顺便补实际写轨迹

先用 `mali=3 mali_cold=1 mali_low=1 mali_preas0=1`，其余沿用已知可回 Android 的配置。必须显式传入这些 boot-args，不能依赖旧 payload 或 loader 默认。根据源码，mali=3 会在 L2_READY 与 firmware loaded 后返回，**不会发送 AS0 UPDATE 或 AUTO**；“mali=3”不是整个 Mali 块零 MMIO 的路径。

唯一驱动文件里在 `mwr`（原 :530）旁加入固定容量的小写记录缓存，记真正的 offset/value；AUTO 前打印一次，不添加 MMIO 操作；启动后冻结记录。打印实验编号与解析后的关键 boot-args，给出产物 MD5 与 driver SHA-256。把 0x90/3090、0x98/3098 的包围读加入现有可读采样窗口；打印前后 GPU_STATUS，避免“ACTIVE 原因/结果”混淆。

期望：Android `sys.boot_completed=1`，日志明确 `mali_cold=1`、`firmware loaded; stopping here because mali=3`，无 soft reset/PWR_KEY/ASN/cache flush。失败只说明基线/构建/交接仍需修复，不判 free-run 或固件假设。

然后同一内核、同一电源配置把 `mali=3` 改成 `mali=6`，取得**完整纯冷基线 C0**。不要把 “mali=3 能回 Android”当作 AUTO 必然能回 Android。若 C0 仍 `GLOBAL_IF=0`、sentinel 未改且返回 Android，才构成可与下一项对照的同阶段失败基线。

### E1：只验证 MFG_GLOBAL_CON free-run

在 `mfg_master_gates()` 原 :2026..2030 的 `glob` 写之前，加 boot-arg 控制的：

```c
if (gMaliFreeRun) {
    glob |= (1u << 8) | (1u << 9) | (1u << 10) | (1u << 19);
}
```

保持已有 `QCHANNEL_CON |= BIT(4)`，打印 GLOBAL_CON 的**原始读值、目标写值、实际回读**（现代码打印的 before 其实已被改过）。以原始 `0x030e7201` 为例，目标应为 `0x030e7701`：bit19 原本已经是 1，这次实际新增的是 bit8/9/10。不要再声称“bit19 一直未开”。该 bit 名来自旧 MTK 平台注释，MT6895 仍需要真机验证，不保证语义完全相同。

期望：从 reset-free C0 的对照中，只改变这组位，观察 `debug_top` 能否回 idle、JOB RAWSTAT/STATUS bit31 是否升起、sentinel 是否变成版本值。若三个里程碑同时成功，可继续检查 global interface。若回读确认到位、仍 0x1b/无 GLOBAL_IF，则否定“仅打开这四位即可解决”的假设，不能泛化为所有 GALS/clock 路径均已排除。仅 debug_top 变化不代表固件已启动。

### E2：只追平 AS2/AS3 的四个 MEMATTR 字

在 `as_park_unused()` 原 :2877，把：

```c
gpu_wr64(asReg(as, kAsMemattr), 0);
```

改成单独 boot-arg 开关的：

```c
gpu_wr64(asReg(as, kAsMemattr),
    (gMaliParkAttrs && as <= 3u) ? MALI_MEMATTR : 0);
```

循环仍从 AS2 开始，不碰 AS1、不改 AS0 root、不同时重排 AS0。对照 E1 关/开或 C0 时保持另一项固定。若四字实录追平而失败读数完全相同，则可把这四字从当前阻塞候选中划掉；若成功，反向恢复开关再复现，以区分属性与实验状态漂移。

### 本轮不重复的路线

不试 `mali_cold=0`、`mali_low=0`、`BOARD_MALI_POWER=0`，不把 clock_26m 提到域上电之前；不读 GPUEB、brisket、0x13f91034 禁区；不复测 firmware 一致性、页表编码、MMU REQ_COMPLETE、HALT、I2M、VSTACK 等已排除项。PHONE-OPS 提议的 0xCA0/0xCAC 读取在原始日志中已经存在（33.114307，EN=0、RDY=0xffffffef），无须再花一轮把同样读数当新发现。

## 7. 本轮交付与后续边界

`round-1-support/` 保留解析脚本、rec0..200 标准化 TSV、本机 94 次重建写序列、分析前驱动快照、完整 155 条 mali 行与 MCU 窗口续行。恢复的冷轨迹放回 `evidence/hist_boot.txt`。此报告没有冒称新增真机启动结果；后续刷机逐轮另记 `round-2.md` 等，记录镜像 MD5、实际 boot-args、原始日志及是否返回 Android。

最终结论：**已经找到可复现的证据错位和四条真正漏对齐的写值，但尚未找到 GPU 启动失败根因。** 用明确冷路径、真实写记录、同阶段寄存器快照继续推进，比在当前错位对照上推断唯一 SoC 原因更有判别力。
