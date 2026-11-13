# 2026-11-13 · Round 13：STACK 无可见响应，完整只读快照，ENABLE→AUTO 已实测

685 的 STACK 请求未得到可见 READY 响应；686 的纯读观测目标已完成，128 个 MCU 窗口字及 SPM/PDC 两点快照齐全；687 的 ENABLE/AUTO 分别读回 control=1/2，status 均为 1，witness 仍为哨兵。固件启动问题仍未解决。685–687 均已恢复原厂并独立核验 boot_completed=1 与 stock boot_a MD5。

## 685：可选 corestack 控制路径关闭

以 681 为语义基线，仅启用默认关闭的 `mali_stackpwron=1`，其它写实验开关保持 0。分支内联于 `gpu_l2_power_on()` 成功返回之后；原有 AS park 在 L2 之前，本轮没有重排。依次读 PRESENT/READY/PWRTRANS，写 `0xe20=present`，有界轮询 READY 后继续 witness，超时不 return。

```text
32.467098 mali: stackpwron trial 685: present 0x33 ready 0 trans 0 (before)
33.036110 mali: stackpwron trial 685: present 0x33 ready 0 trans 0 (after 100000 spins, NOT READY)
41.972693 mali: witness: read1 0xff5aa5f0 0 read2 0xff5aa5f0 0 read3 0xff5aa5f0 0 trial 685
```

写轨迹 **83=82+仅 `0xe20=0x33`**，AS0 TRANSTAB PA 归一化后其余完整序列相同。PWRON store 完成不等于电源请求生效。READY 在轮询中未读到 PRESENT，TRANS 前后样本均为 0；TRANS 没有被连续采样，不能由两个零推导完整过渡态轨迹。

vendor `core_type_to_reg()` 在 `corestack_driver_control` 下确实返回 `STACK_PWRON_LO`，随后 `kbase_pm_invoke()` 执行 `kbase_reg_write`；头文件的 `(RO)` 注释不阻止这条写路径。但 `Kconfig:173–179` 明确将 `MALI_CORESTACK` 可选控制支持范围列为 t-Six/t-HEx，不能把它当作 G610 必需步骤。按用户决定关闭这个 AP 控制候选；**它没有测出 corestack 的真实供电状态**。

交接判读曾写“100 秒轮询”：实际为 100000 次 `delay(1)`，XNU `clock.c:1632–1635` 的单位为微秒，两条日志相差 **0.569012 秒**。源码证据与 SHA256 在 `stack-source.json`。本轮 root adbd 返回较慢，logbook 主机保存时间距实验 reboot 为 403.942 秒，仍在 runner 的 420 秒窗口内；随后 runner 正常完成原厂恢复。

## 686：纯读取证完成，没有新增 MMIO 写

只启用现有 `mali_mcuwin=1`，`stackpwron/hwissues/cacheflush/resetonly/cohnone` 均为 0。补丁仅加读数和打印：在 AUTO 前、AUTO 轮询完成后且 DISABLE 前各读 `0x3000..0x30fc` 的 64 字，并在同点读取五个 SPM 完整值、F3C 全 32 位、SC0..9 原值/bit0 和七个 PDC group 原值。新开关模式输出恰好两份窗口，原默认路径保留原有输出。

**启动前 GPU trace=82，与 681 的完整序列仅 AS0 root PA 不同，归一化后零差异。** 补丁没有新增 `mwr/reg_wr/gpu_wr64` 调用，原有启动写和电源路径不变。686 的目标是拿齐数据，witness 未变不构成观测任务失败。

两组窗口分别位于 38.438762s 和 48.024979s，第二组在 DISABLE 之前。非零及变化字如下：

| MCU offset | AUTO 前 | AUTO 后、DISABLE 前 |
| --- | --- | --- |
| `0x3020` | `0x600` | `0x600` |
| `0x3034` | `0` | `1` |
| `0x3090` | `0x3c58bda` | `0xae5c6ce0` |
| `0x3098` | `0x1dcc3d70` | `0x2537ccbc` |
| 其余 60 字 | 全 0 | 全 0 |

唯一变化的非计数器字为此前已见过的 `0x3034`；计数器变化不证明 MCU 完成了取指或 store。完整 64 行前后表在 [trial-686-mcu-window.tsv](round-13-support/trial-686-mcu-window.tsv)，原读数在 `trial-686-mcu.txt`。本轮没有找到新的可操作状态字；按用户决定结束这个窗口的追写方向，未改动 `0x3020`。

AUTO 前的 `0x30ac=0` 后紧接另一线程的 `process lsd…` 日志。数值 0 本身完整，解析器按十六进制字符边界提取，不依赖空白，未猜填缺项；128 个地址/值均有原文。带穿插消息的完整窗口正文保存在 `trial-686-mcu-rawblocks.txt`，JSON 保存原始 byte offsets。

SPM 两组读数完全相同，以下都是完整 32 位原值，没有先掩码：

| SPM offset（base=`0x1c001000`） | AUTO 前 | AUTO 后 |
| --- | --- | --- |
| `0xF30` | `0x1101` | `0x1101` |
| `0xF34` | `0xde0001fd` | `0xde0001fd` |
| `0xF38` | `0xde0001fc` | `0xde0001fc` |
| `0xF3C` | `0xf` | `0xf` |
| `0xF40` | `0xf` | `0xf` |

F30 的已命名 AOC bit9/bit10 均为 0，其余位不扩展解释。F34/F38 的高位不能丢弃，也不能把整个字解释为纯 MFG 域位图。

F3C 的置位集合精确为 `{0,1,2,3}`，其余 28 位为 0。vendor 掩码 `MFG_0_12=0x3ffe`、`MFG_1_12=0x3ffc` 下，`0xf & 0x3ffc=0xc`；按域顺序 bit2/3 对应 MFG1/MFG2，bit4..13（MFG3..12）为 0。bit1 处于 MFG0..12 掩码内，bit0 在该掩码外，未将它命名为“PDC 使能”。完整 32 位表在 [trial-686-F3C-bits.tsv](round-13-support/trial-686-F3C-bits.tsv)。

同一份 686 日志中，38.111310s 的旧 `gpufreq 0x4` 位于 **L2 上电前且已经掩码**；38.200286s `L2 up: ready 1` 之后，38.470831s 采到完整 `0xf`。所以二者不是相同阶段的同口径读数；多出的域状态位是 MFG2/L2 对应 bit3，另外还有掩码外低位，不能把 `0x4` 与 `0xf` 混作矛盾。

PDC 原值与使能位两点完全一致，完整逐项地址在 [trial-686-PDC-SC.tsv](round-13-support/trial-686-PDC-SC.tsv)：

| 项 | 完整原值 | bit0 |
| --- | --- | --- |
| sc0 | `0x2000001` | 1 |
| sc1 | `0x2000001` | 1 |
| sc2 | `0x2000001` | 1 |
| sc3 | `0x2000001` | 1 |
| sc4 | `0x2000001` | 1 |
| sc5 | `0x2000001` | 1 |
| sc6 | `0` | 0 |
| sc7 | `0` | 0 |
| sc8 | `0` | 0 |
| sc9 | `0` | 0 |

group 原值：`cg=0x18d0001`；`st0/st1/st4/st5=0x2000001`；`st2/st6=0`。vendor 头文件明确 `SHADER_CORE_NUM=6`；SC 使能与 MFG 域状态是不同类字段，给定源码没有提供 SC 索引与 MFG 位的一一对应表，不能把 SC6..9 的零当作漏使能。`STACK_PRESENT=0x33` 的索引 0/1/4/5 与现有四个 ST group 的非零样本相容。报告记录已知对应，不据此推导未观测的真实供电。

round-6 §9.2/§9.3 已撤回“SC9/ST6 零就是故障”和“状态必须点亮满 0x3ffc”的旧判据。[外部同型 xaga 的实机记录](https://github.com/BkaNeko/xaga-mali-r54-port/blob/1350bc1e7be56e2281bbab8b806aa34c88fb21ec/docs/GPUEB.zh-CN.md#L454-L503)也报告正常 GPU 工作时直接 MMIO 快照可为 F3C=3、MFG1..12 control=0x1112；这只说明旧样本判据不足以区分故障，不能用 `0xf > 0x3` 给设备健康排序。本轮未尝试补域、PDC 重试或未知寄存器写。

## 687：同内核单变量 ENABLE → AUTO

沿用 686 的同一驱动、内核、payload 和纯读开关，只增加 `mali_mcuseq=1` 及 trial 编号；没有修改源码。此前已有的代码执行 ENABLE、barrier、5us delay 和读回，再 AUTO、barrier、5us delay 和读回。

```text
38.278410 mali: mcuseq: after ENABLE write, control reads 0x1 status 0x1
38.280214 mali: mcuseq: after AUTO write, control reads 0x2 status 0x1 (status bits: 0=1 1=0 2=0 3=0)
47.546302 mali: witness: read1 0xff5aa5f0 0 read2 0xff5aa5f0 0 read3 0xff5aa5f0 0 trial 687
```

两步已生效到寄存器读回，status 均为 1，但没有完成可见 witness store。 现场能确认的是 status 只有 bit0=1；kbase 的 HALTED 定义为 bit1，Panthor 则将 1 命名为 ENABLED。位定义与枚举命名本身不能由此实验判为互相矛盾；无论采用何种命名，raw 1 都不是固件 boot IRQ、有效 version 或 witness 的替代证据。原始头文件和指纹在 mcu-status-source.json。MCU 窗口仍只有 3034 与计数器变化，五个 SPM 原值与 686 完全相同；没有观察到新的状态变化能解释启动停滞。

**trace 在 ENABLE/AUTO 之前已经冻结**，故 687 的启动前 trace 仍为 82，与 686 归一化后零差异，**不包含 ENABLE 和 AUTO 两写**。不得把计数相同写成“没有新增硬件动作”；新动作 ENABLE 由已到达日志、control 读回和固定源码路径相互印证。

## AUTO 前后与产物指纹

| trial | AUTO 前 MCU control/status | 请求后状态 | witness 三次 | debug_top（walk→请求后→DISABLE 后） |
| --- | --- | --- | --- | --- |
| 685 | `0/0` | status=1，JOB/GPU/MMU IRQ=0 | `ff5aa5f0,0` | `4→1b→1b` |
| 686 | `0/0` | status=1，JOB/GPU/MMU IRQ=0 | `ff5aa5f0,0` | `4→1b→1b` |
| 687 | `0/0`，另有 ENABLE 读回 `1/1` | AUTO 读回 `2/1`，JOB/GPU/MMU IRQ=0 | `ff5aa5f0,0` | `4→1b→1b` |

三轮 AS0 的 status/fault/address 和 gpu_fault 读数均为 0，coverage 完整。witness 判据仍为 `11 → 22 → deadbeef` 三次 store、最终两字 `deadbeef,22`；哨兵不变只能表述为未观察到首个 store 完成，不能证明零指令执行。

| trial | 镜像 MD5 | trace store 数（冻结范围见正文） | 驱动 SHA256 |
| --- | --- | --- | --- |
| 685 | `9d959905bbd9ca6c02d616bfa1fe558a` | 83 | `ab5b8ea264d2b81b293a81d5cbe29f4fb521cdac7e00960cdb7e0d331f5f4768` |
| 686 | `e5691810f8e41804ab1655d76ddd1140` | 82 | `953ad54126832f9149afa10317567de2c7bacceb1bb8c458b00d0bafe1ae4219` |
| 687 | `413a1d3fe1f57e4133f59697c7b5db77` | 82 | `953ad54126832f9149afa10317567de2c7bacceb1bb8c458b00d0bafe1ae4219` |

685 内核 SHA256=`9fa289c4b10d79e4d17cb7a773d53bfb1aca12cfa492fa1c4fd72813647255bf`，payload=`8b8b8cabe79ceb7599f503bc1f73cd6e54cf681c2571327f40d758d077c1c20f`。
686/687 内核 SHA256=`8b757ad908ac94fb945629d21d014b72cdd8beaf023aed563bcc14b94011590e`，payload=`3a448246fa1da0f25160c008e0e3a7f1c5c50171ba4557ac7c79e36f8f977fea`。686/687 可标为同内核单变量对照；685 与 681 是不同二进制的语义基线对照。

两次内核构建均通过 make-kernel 的既有错误分类门，只接受已知 kext symbol-set 失败；实际内核包含对应分支日志，源码与 XNU 同步源码指纹相符；各轮精确源码快照随报告保存。三轮全部实际 loader 参数按完整 token 验证各出现 3 次，证据 JSON 含三份参数原文。

## logbook 与恢复

沿用已测 runner：每轮仅备份并清理 offset=2676981760、bytes=4198400 的专用窗口，先校验 xnusys 总长2685370368并读回清零 MD5；完整原始窗口保存，解析不按 header len 截断，无外来 trial 标记。raw SHA256、预清理前一轮 SHA256、stock 恢复步骤在各 trial JSON。

685–687 runner 均正常结束，原厂 boot_a 的 MD5 和 boot_completed 已独立复核；最后一轮 687 的独立复核时间为 16:11:34 +08:00。stock MD5 统一为 `a63b2e35a7c2522cb24a6a10332b9bc8`；未启用 mali_rpc，未访问三个禁读地址。

报告为 `out/round-13.md` / `docs/GPU-ROUND-13.md`，文本证据在 `docs/round-13-support/`，raw 和镜像保留本地。round-12 的 f01f1f6 已在本轮新请求前推送；本次提交会补齐 round-13 的 685–687。用户接续指定 doorbell 实验，另记 round-14；不为点亮 0x3ffc 改上电路径。
