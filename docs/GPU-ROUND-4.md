# Round 4：AS0-first 单次真机验证

2026-11-12，Asia/Shanghai。结果：**trial 400 已返回 Android，MCU 仍未达到可见启动里程碑。** 用户额度告警后将两次同内核对照改为只刷 as0first=1、trace=1；本轮 as0first=0 未执行。仅本次一次刷机，完成后停止。

## 改动与构建

新增 `mali_as0first`，默认0保持现行为。将原 `load_firmware()`、外层 MMU clear、AS0 更新、日志、`mfg_bus_idle()`、early-AS0 标志整块提取为同一 helper；开关1时在原 gpueb_watch/gmpu_probe 之后、as_park_unused之前执行，开关0仍在IRQ/IPA后执行。两分支保持原 eligibility：preas0=1、as0=0、level>4。后续不重复装载或UPDATE。

AS内写序、13次附加MMU clear、2次power clear、其它寄存器值及等待逻辑均未改。没有访问三个禁址，也未改固件头。源码独立复核通过；内核和payload各构建一次，make-kernel仅接受既有kext symbol-set检查失败；kernel/payload/loader/image全部产出。发布patch只有GPU全文件段改变，其余段逐字节保持。

- 镜像：`/tmp/hakios-round4-images/trial-400-as0first-1.img`，22974464 B。
- 镜像 MD5：`b19da29b3134b06d501036f24540b83c`。
- 镜像 SHA256：`6c7945824b9710254233468b38ebdff389a0e9c62d6bead4119cfa570a5c55fc`。
- 源码 SHA256：`283a45a8eab3a29becbcdcb803e8b07a7236c1575c47ddde41ead2735ce44af8`。
- 共用计划的内核 SHA256：`fb30fd7fc5db3227ab785ac4c5613e71801bf16ea5d9bf9366c0621e599eea20`（实际仅刷一次）。
- payload SHA256：`b4723dfe7ac82f310b4c68f4bfd8437be642efb147b1e464b7611ae425924578`。

实际参数：

```text
cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=1 mali_preas0=1 mali_low=1 mali_freerun=1 mali_parkattrs=1 mali_trace=1 mali_trial=400 mali_as0first=1
```

23:01:55 Sending/Writing boot_a 均OKAY；23:02:08独立fastboot reboot成功；23:05:41检查Android在线、sys.boot_completed=1、slot=_a。未使用救砖镜像。

## 写次数与差集

| 记录 | 类型 | 启动前stores |
|---|---|---:|
| trial400 / as0first=1 | 本轮新内核实测 | 82 |
| 本轮 as0first=0 | 用户取消，未执行 | 未测 |
| trial206 原顺序 | 历史实测 | 82 |
| trial236 原顺序 | 历史实测，free=1/attrs=1 | 82 |

**206不是本轮同内核对照**：206源码SHA为 `e0ae936e522ab8087fcdad621f1815679c935e095041c63a2fbde0242a53c5d3`，free-run=0、parkattrs=0；本轮两项均1。206的trace在AUTO前打印，本轮在AUTO尝试后打印，因此历史比较不能称严格单变量因果对照。

对206：offset多重集差集为空。将其AS0块移到开头后，仍有四个值差异：`0x2488/0x24c8: 0 → 0x888d8f88`，`0x248c/0x24cc: 0 → 0x00084c8d`；它们是历史parkattrs差异，不是本轮新改的寄存器值。free-run属于SoC配置，不在GPU store TSV内。

另与配置更接近的236比较：offset多重集及(offset,value)多重集差集均为空；精确序列满足下式（零基半开区间）：

```text
trial400 = trial236[65:78] + trial236[0:65] + trial236[78:82]
```

即13条AS0整块（7次AS0配置/UPDATE、6次MMU clear）移到最前；其余69条相对顺序和值全部不变。236同样不是本轮内核，且其trace仍在AUTO前打印；仅支持写操作差分判断。

## 原始关键读数

```text
33.102915 mali: interconnect before the firmware: dcm0 0xc03f->0xc03f async 0x3c00000->0x3c00000 async1 0xf7->0xf7 global 0x3281701->0x30e7701 qchannel 0x10->0x10 rpc 0->0
33.209192 mali: bus idle after the early table walk: qchannel 0x11 debug_sel 0x3 debug_top 0x4 (idle after 0 spins)
33.523353 mali: MCU status 0 before the start request
33.523444 mali: views before AUTO: gpu status 0/0 irq raw 0/0x600 mcu control 0 status 0
34.032543 mali: spin 0: status 1 job 0 mmu 0 fault 0 at 0 top 0x1b
34.038676 mali: spin 1000: status 1 job 0 mmu 0 fault 0 at 0 top 0x1b
34.734377 mali: spin 100000: status 1 job 0 mmu 0 fault 0 at 0 top 0x1b
37.720722 mali: spin 500000: status 1 job 0 mmu 0 fault 0 at 0 top 0x1b
43.093717 mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
43.094490 mali: trial 400 after AUTO (stores captured before AUTO): level 6 cold 1 low 1 preas0 1 freerun 1 parkattrs 1 trace 1 as0first 1
43.095126 mali: write trace: 82 completed GPU stores (capacity 128)
43.138036 mali: MCU status 1 (enabled), job irq 0, gpu irq 0
43.138864 mali: after the start request: gpu status 0x1, gpu irq status 0, job irq status 0, mmu irq 0, as0 status 0 faultstatus 0 faultaddress 0
43.143450 mali: version word 0xff5aa5f0 (untouched; no firmware version published)
43.144344 mali: the global interface never raised; the MCU did not boot
44.204915 mali: bus idle after the failed start: qchannel 0x11 debug_sel 0x3 debug_top 0x1b (still busy after 100000 spins)
44.209000 mali: MCU control written 0, status now 1, control reads 0
45.279657 mali: bus idle after the core was switched off: qchannel 0x11 debug_sel 0x3 debug_top 0x1b (still busy after 100000 spins)
```

AUTO前 GPU_STATUS=0/0，之后为0x1。spin 0、1000、100000、500000均为 MCU status1、job0、mmu0、fault0、address0、debug_top0x1b；1000000 spins后无GLOBAL_IF。哨兵 `0xff5aa5f0` 未改写。AS0早期table walk后debug_top=0x4（idle），AUTO后为0x1b并保持busy；DISABLE后status1/control0。enabled不等于running，哨兵不变也不构成严格PC/取指证据。

## 结论与证据

AS0-first整块移动已由82条真实store序列确认；在本次配置与测量条件下仍未修复MCU启动。两次同内核对照没有完成，不能声称已获得只差AS0顺序的两次真机因果对照。没有继续IRQ清除或其它变量实验。

完整原始logbook：4198400 B，SHA256 `bcde6c5384b0f590835f7592361316ea063ac9e17e25d5cde13743eb378a6b1c`。解析整份文件，223条Mali行，时间回退[]；唯一trial400/as0first1/trace1标记。日志路径：`/home/lizi/IOS/work-xaga/codex/out/round-4-support/trial-400-logbook.bin`。

- [实际写序列](gpu-round-4-support/trial-400-writes.tsv)
- [全部Mali行](gpu-round-4-support/trial-400-mali.txt)
- [原始关键行](gpu-round-4-support/trial-400-key.txt)
- [差集核验](gpu-round-4-support/store-comparison.json)
- [产物与真机元数据](gpu-round-4-support/trial-400.json)
- [源码diff](gpu-round-4-support/driver.diff)
- [发布源码/patch校验](gpu-round-4-support/publish-source-check.json)

补记：上一轮trial246（trace0）已经返回Android且仍失败，见本地round-3.md；历史round2的“246未刷”仅指当时交接状态。
