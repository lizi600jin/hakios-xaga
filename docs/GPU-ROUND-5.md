# Round 5：诊断 MMU clear 与 power IRQ clear 两组独立实验

2026-11-12（Asia/Shanghai）。按用户指令只做 trial401、trial402，每组一次刷机，不做第三组。

## 实验设计与唯一源码改动

两开关默认0，默认路径保持round4行为。`mali_noapclear=1`只跳过park循环6次、AS_STEP宏5次、early-AS0外层1次、start_mcu 1次，共13次诊断MMU_IRQ_CLEAR。IRQ初始化六写（GPU clear/mask、JOB clear/mask、MMU clear/mask）完整保留，初始化函数逐字节未改。

`mali_nopwrclear=1`只跳过gpu_l2_power_on中两次GPU_IRQ_CLEAR=0x600。20000-spin循环、判断、delay及其余语句保持原样；**这会改变等待语义与实际时序**：首个clear不执行时，已有ALL锁存可以满足退出条件，原本清掉ALL后等待新事件的逻辑不再相同。没有添加补偿延时。不能把402解释为“只有少两次写、等待时间相同”。

两组共同参数：`mali=6 mali_trace=1 mali_cold=1 mali_low=1 mali_preas0=1 mali_as0first=0 mali_freerun=1 mali_parkattrs=1`。401为noapclear=1/nopwrclear=0；402为noapclear=0/nopwrclear=1。每组相对两个新开关都为0的定义基线只启用一个变量，401与402不是彼此的单开关对照。本轮未额外刷默认基线。

内核和payload各构建一次，两镜像共用同一产物；仅loader boot-args按组切换。内核构建由make-kernel判定通过，只余既有kext symbol-set检查失败。未改供电/时钟/AS内写序、固件头或其它驱动。三个禁址均未访问。

共同驱动源码 SHA256：`81db05720a2b6587fa121f2aa573f726946a0fe8cf72d43bd68c039304778c9a`。

内核 SHA256：`cc7e15998d98feb6e9ab15f72741dba0cf293dac70218e49d7bed74e1bb84254`。

payload SHA256：`3832e803bc4cb1ccb6b109e8d35c6880133a290224c21894c331bfef0bc157c6`。

## 差分基线与证据边界

历史236为82条、AS0在IRQ/IPA后、free-run=1/parkattrs=1。历史400也是82条，但AS0-first=1；其顺序满足 `400 = 236[65:78] + 236[0:65] + 236[78:82]`（零基半开区间）。因此比较本轮对400必须先恢复AS0位置，不能只报删除次数而漏掉顺序差异。

历史236的trace在AUTO前打印，本轮与400均在AUTO尝试后打印；历史内核源码也不同。本轮真实trace可精确证实写操作差集，历史比较不构成同内核同打印时序的严格配对试验。

## Trial 401：只去13次诊断MMU clear

镜像 MD5：`e0807148aa0a4c4357cdb0e8572323ca`；22974464 B。

驱动源码 SHA256：`81db05720a2b6587fa121f2aa573f726946a0fe8cf72d43bd68c039304778c9a`。

```text
cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=1 mali_preas0=1 mali_low=1 mali_freerun=1 mali_parkattrs=1 mali_trace=1 mali_trial=401 mali_as0first=0 mali_noapclear=1 mali_nopwrclear=0
```

实测 **69 stores**，期望69；与236删除指定写后的完整序列一致：**True**；IRQ初始化六写保留：**True**。

相对236删除零基索引：`[0, 8, 16, 24, 32, 40, 65, 66, 67, 70, 73, 76, 81]`，剩余项(offset,value)及相对顺序全部不变。

相对400：先恢复AS0整块位置，再删上述目标写；被删项在原400中的索引为 `[13, 21, 29, 37, 45, 53, 0, 1, 2, 5, 8, 11, 81]`。

(offset,value)多重集差集：删除 `[{'offset': '0x2004', 'value': '0xffffffff', 'count': 13}]`；新增 `[]`。

Android检查时间：`2026-11-12T23:17:22+08:00`；`sys.boot_completed=1` 已确认：**True**。

原始关键行（spin中的status是MCU_STATUS，GPU_STATUS见views/after行）：

```text
33.553459 mali: interconnect before the firmware: dcm0 0xc03f->0xc03f async 0x3c00000->0x3c00000 async1 0xf7->0xf7 global 0x3281701->0x30e7701 qchannel 0x10->0x10 rpc 0->0
33.692654 mali: bus idle after the early table walk: qchannel 0x11 debug_sel 0x3 debug_top 0x4 (idle after 0 spins)
33.698602 mali: L2 up: trans 0 active 0 ready 0x1
33.816511 mali: power settle: found 0x600 (all 1 single 1), now raw 0 status 0
33.832085 mali: MCU status 0 before the start request
33.845321 mali: views before AUTO: gpu status 0/0 irq raw 0/0x600 mcu control 0 status 0
33.855816 mali: spin 0: status 1 job 0 mmu 0x10000 fault 0 at 0 top 0x1b
33.863153 mali: spin 1000: status 1 job 0 mmu 0x10000 fault 0 at 0 top 0x1b
34.697104 mali: spin 100000: status 1 job 0 mmu 0x10000 fault 0 at 0 top 0x1b
37.763478 mali: spin 500000: status 1 job 0 mmu 0x10000 fault 0 at 0 top 0x1b
43.195504 mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
43.197920 mali: trial 401 after AUTO (stores captured before AUTO): level 6 cold 1 low 1 preas0 1 freerun 1 parkattrs 1 trace 1 as0first 0 noapclear 1 nopwrclear 0
43.199031 mali: write trace: 69 completed GPU stores (capacity 128)
43.236886 mali: MCU status 1 (enabled), job irq 0, gpu irq 0
43.238168 mali: after the start request: gpu status 0x1, gpu irq status 0, job irq status 0, mmu irq 0x10000, as0 status 0 faultstatus 0 faultaddress 0
43.243239 mali: version word 0xff5aa5f0 (untouched; no firmware version published)
43.244082 mali: the global interface never raised; the MCU did not boot
44.298353 mali: bus idle after the failed start: qchannel 0x11 debug_sel 0x3 debug_top 0x1b (still busy after 100000 spins)
44.301843 mali: MCU control written 0, status now 1, control reads 0
45.356287 mali: bus idle after the core was switched off: qchannel 0x11 debug_sel 0x3 debug_top 0x1b (still busy after 100000 spins)
```

noapclear=1时，旧日志文字“after the clear / freshly cleared”只是保留的标签，不代表执行了clear。MMU高16位按REQ_COMPLETE解释，必须与AS0 faultstatus/address分开；不把未清的完成回执误判为新bus error。

结论：无GLOBAL_IF，MCU未达到可见启动里程碑；本组指定删写不足以修复本次启动。哨兵未改的直接日志证据：True。

完整logbook 4198400 B，SHA256 `14eb202140f24bba62ee9e833395ef9195204f7618677c17bb51dc308380602c`；210条Mali行，时间回退[]。原始二进制：`/home/lizi/IOS/work-xaga/codex/out/round-5-support/trial-401-logbook.bin`。

- [完整(offset,value)序列](round-5-support/trial-401-writes.tsv)
- [全Mali行](round-5-support/trial-401-mali.txt)
- [顺序/多重集差集](round-5-support/trial-401-comparison.json)
- [镜像与真机元数据](round-5-support/trial-401.json)
- [MCU窗口续行](round-5-support/trial-401-mcu-windows.txt)

## Trial 402：只去两次power IRQ clear

镜像 MD5：`f06222d75b6f3e2acc592164bf377b2d`；22974464 B。

驱动源码 SHA256：`81db05720a2b6587fa121f2aa573f726946a0fe8cf72d43bd68c039304778c9a`。

```text
cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=1 mali_preas0=1 mali_low=1 mali_freerun=1 mali_parkattrs=1 mali_trace=1 mali_trial=402 mali_as0first=0 mali_noapclear=0 mali_nopwrclear=1
```

**402 由 DSH 主控完成取日志（astra 额度耗尽后交接）。**

刷入：23:18:29 独立 reboot，Sending/Writing 均 OKAY。
回 Android：23:22:20 确认 `GAJZ4HIN99AQ69Y9 device`、`sys.boot_completed=1`、`slot_suffix=_a`。

实测 **80 stores**，期望 80。相对 236 的完整差集只有一项：删掉索引 79、80 的两次 `0x00024=0x600`，其余 80 条 `(offset,value)` 与顺序逐项不变（`sequence_identical_after_removal: true`）。多重集差：`{0x00024:0x600 → 2}`，无新增。IRQ 初始化六写完整保留。

关键原始行：

```text
33.687356 mali: power settle: found 0x600 (all 1 single 1), now raw 0x600 status 0x400
33.688640 mali: mmu irq 0 with the latch freshly cleared, just before the start request
33.688649 mali: spin 0: status 1 job 0 mmu 0 fault 0 at 0 top 0x1b
34.439015 mali: spin 100000: status 1 job 0 mmu 0 fault 0 at 0 top 0x1b
37.356244 mali: spin 500000: status 1 job 0 mmu 0 fault 0 at 0 top 0x1b
42.819277 mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
42.867237 mali: after the start request: gpu status 0x1, gpu irq status 0x400, job irq status 0, mmu irq 0, as0 status 0 faultstatus 0 faultaddress 0
42.871856 mali: version word 0xff5aa5f0 (untouched; no firmware version published)
43.921580 mali: bus idle after the failed start: qchannel 0x11 debug_sel 0x3 debug_top 0x1b (still busy after 100000 spins)
43.925274 mali: MCU window after the core was switched off:
  0x3020=0x600 0x3034=0x1 0x3090=0xb7147964 0x3098=0x2209791b
45.022955 mali: gmpu after the failed start: 0x1c:0x11f00 0x48:0x1000 0x4c:0xff020f00 0x60:0x21800000 0xc0:0x30
45.030986 mali: cpe: 0 0 0 0 (ctrlen never programmed)
```

**结论：去掉两次 power IRQ clear 不足以修复启动。** 无 GLOBAL_IF、哨兵未改、`debug_top` 四个采样点恒 `0x1b`、`gpu status` AUTO 前 `0` 后 `1`。

**证据边界（必须带着读）**：`nopwrclear=1` 同时改变了等待语义。`power settle` 那行显示 `now raw 0x600`（第一次 clear 没做，原有的 ALL 锁存仍在），
所以循环是被**已有锁存**满足而立即退出的，不是在等一个新的 ALL 事件。因此 402 **不是**"只少两次写、其它完全相同"的干净对照，
它混入了"少等一次 power 事件"这个时序变量。要单独量化这一项，需要补偿一个等长延时再跑第三组 —— 本轮按指令未做。

## 收尾

原始日志从完整4198400 B文件解析，不按HLOG len截断。源码diff、构建日志与共同产物哈希保存在round-5-support。
402 的日志、mali 行、writes.tsv、comparison.json 由 DSH 主控在额度耗尽交接后补齐（见 trial-402.json 的 `retrieved_by`）。
发布时只同步GPU源文件及对应patch段，其它patch段逐字节保持。完成指定两组后停止，不执行组合删写或第三个实验。
