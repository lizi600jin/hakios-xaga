# MT6895 CPU baseline and read-only candidate (2026-11-16)

Codex. This begins the next phase after the accepted headphone delivery.
Scope: CPU control and scheduling evidence for xaga. Sample rate and bit depth remain deferred.

## Decisions on the five questions

1. The frozen trial712 tree has 60 matching bsd/dev/hakios.* files. The exact
   case-sensitive redwood|POCO X5 Pro pattern matches 23; xaga matches 6, not 8.
   Files, configuration choices, compiled objects and runtime activity are separate facts.
2. A count of replacement addresses in the Qualcomm driver would not describe
   this port. That driver reads Qualcomm-specific LUT words and writes offset
   0x320. Its three domains start at 0x18591000. MT6895 uses a different compatible,
   LUT encoding and register layout. A separate adapter is appropriate.
3. Use the running phone's DT resources and CPU performance-domain bindings,
   paired with its matching driver implementation. The existing live DT gives
   0x11bc10, 0x11bd30, 0x11be50, each size 0x120. Vendor source explains the protocol;
   iomem is corroboration and may omit firmware SRAM. The DT alone does not
   establish firmware readiness or valid voltage/frequency transitions.
4. Prior xaga CPU work exists. board.h supplies MPIDRs and core order
   {0,4,5,7,1,2,3,6}; the loader contains a PMU/PLL observation path; GenericARMPE
   and hakios.patch implement deferred slow-core startup and scheduler preference.
   Trial712 also logs the slow-core relationship. The existing CPUFREQ and MODEM
   configurations are explicitly disabled, with empty stub entry points.
   A missing log keyword cannot identify the cause of a missing subsystem.
5. Choose a staged CPU baseline and MT6895 adapter. Begin with a bounded,
   read-only snapshot; do not infer that fixed CPU frequency caused every observed
   performance limit. Frequency control, scheduling, GPU acceleration, storage
   and modem support need separate evidence. MPSS and EPSS are Qualcomm protocols.

## Primary protocol source

MiCode official xaga-s-oss:
https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/xaga-s-oss/drivers/cpufreq/mediatek-cpufreq-hw.c

Local snapshot: mediatek-cpufreq-hw.vendor.c, 8999 bytes, 387 lines,
SHA256 3cc72e1f5b36e0154ab34df932a1541a59e2f1932b160a5538866df7f2d31d8f.
This is a captured branch file, not a claim that the running module is byte-identical.

The driver uses 32 LUT words, with the low 12 bits multiplied by 1000 for kHz.
It ends a table at a repeated frequency. Per-domain offsets: LUT 0,
ENABLE 0x84, requested row 0x88, status 0x8c. It waits for status bits 0 and 1.
The vendor initialization may continue after a timeout with bit0 set and bit1
clear; the candidate conservatively declines to label that state ready.
The vendor reads transition latency at 0x114; the candidate never accesses it.
A sysfs latency value alone does not prove the current binary or physical word.

The separate cpuhvfs and MCUPM nodes describe additional firmware/control paths.
The absence of their names from this one driver does not establish independence
from firmware running elsewhere. No firmware initialization or regulator control
has been ported in this candidate.

## Candidate

candidate/hakios.mt6895_cpufreq.c:
SHA256 ef013ea9178e18a2018cf865e609f26f5ef15fdafcc680f95d38e2c165d7b6e9.

Only mtk_cpu_probe=1 enters the path. Missing/zero arguments return without
mapping; every other value is rejected before mapping. Three fixed DT resources
are mapped; there are no voltage, frequency-request, enable or PLL writes.

Each domain gets two full snapshots of ENABLE, STATUS, REQUEST and all 32 LUT
words, 70 reads total. The candidate checks that both copies agree, then checks
enable, readiness, descending nonzero frequencies and the request's index.
All raw words are logged even when decoding is declined.
No frequency is labeled an actual measured CPU clock.

Result values:
0 observed; 1 reader failure; 2 snapshots differ; 3 not enabled;
4 readiness flags incomplete; 5 table rejected; 6 requested index outside table.

The failure callback tests are host failure-injection coverage. They do not
implement recovery from a live MMIO data abort.

## Verification completed before kernel build

130 cases passed with clang, -Wall -Wextra -Werror and ASan/UBSan.
Coverage includes all 70 read failure positions, all 35 second-snapshot word
changes, out-of-range indices, empty/increasing tables, inactive controllers,
no-access argument cases and failed mappings. The actual MMIO adapter was also
run against host mappings protected read-only with mprotect.

Native build and run rc are both 0; run stderr is empty:
candidate-model-result.json. DSH independently reproduced the 130 cases.

An ARM64 object compiled against XNU headers with native rc 0 and no diagnostics:
cpufreq-arm64-result.json. This proves object compilation, not a kernel boot.

## Isolated kernel integration

prepare-isolated-tree.py created kernel-src from the 5205-file frozen reference.
It did not copy BUILD. No production file or prior build tree was changed.

Five original paths changed:
- bsd/dev/hakios.mt6895_headset.c and bsd/sys/hakios.pmif.h retain the final712 inputs.
- bsd/dev/hakios.input.c calls the new observation entry point.
- bsd/sys/hakios.input.h declares it.
- bsd/conf/files includes the experimental candidate as standard.

One new path: bsd/dev/hakios.mt6895_cpufreq.c.
This experiment does not yet install a production driver/configuration choice.
The 5206 input hashes are frozen in kernel-inputs.sha256.
CMD-BUILD-CPU-PROBE-713.md gives DSH the build/review/archive work.

## Planned trial713 behavior

arguments713.json preserves the existing four-core and GPU settings,
changes the trial identity, sets headset=0 and mtk_cpu_probe=1, and removes
headphone trim/tone arguments. It retains loader_return_after=150,
the disabled watcher and the disabled hardware watchdog.
Base args: 479 bytes; runtime args: 539; with NUL: 540 <= 608.

Packaging and device execution require completion of the internal kernel and
image review. No additional user permission is needed under the existing scope.
Until those internal reviews finish, the phone remains on stock Android.

## Handoff

### A. 状态快照

BUILD-CPU-PROBE-713 已完成：内核15321336 B，SHA256
a80231ed9dc78a82bfddc71f34685808e66ce490d70ed059e37cd108638885c3。
make 原生rc=2，显式KERNEL judge原生rc=0，只豁免既有symbol-set问题。
713尚未打包、尚未刷写，没有713真机结果。没有发出PACK或DEVICE CMD。

设备最近确认仍为原厂Android，boot_a全分区MD5
a63b2e35a7c2522cb24a6a10332b9bc8，slot_a。
文档仓为 /home/lizi/IOS/repo/hakios-mt6895；用户报告c34d20b已推且远端同步。
生产工作区为 /home/lizi/IOS/hakios-mtk；本阶段未改生产源码。
当前用户要求先交接并停止：不再测试、构建、打包或刷写。

### B. 已独立复核与尚未复核

已独立复核：据同目录independent-kernel-review.json，内核完整hash与尺寸匹配，
5206项冻结输入全部一致，BUILD外无新增/消失文件，源码冻结后变化0；
make rc2与judge rc0分别记录。实际CPU probe对象、新入口符号和提示字符串存在。
production-before.json中8项生产输入未变。
候选模型编译/运行rc0、PASS130、ASan/UBSan运行stderr空；真实XNU头文件下ARM64对象rc0。
这些均是紧急交接指令之前完成的检查，本次追加没有重新执行验证。

尚未独立复核：c34d20b最新提交、远端及其全部归档blob；DSH最新审查文档全文；
Android CPU sysfs归档原件与完整频率表；当前运行模块与官方源码的二进制身份对应。
713镜像尚不存在，review-package.py虽已适配但尚无本轮镜像可验证。
没有XNU下真实LUT/status快照、MCUPM就绪证据、实际时钟/电压测量、调频或性能改善结论。
不能以字符串存在、sysfs请求频率或transition_latency=1000ns替代上述证据。

### C. 五条未证实项与判断

1. cpufreq-hw与cpuhvfs谁拥有/初始化相关状态、如何协作：
   不阻塞限定范围的只读快照；可写调频前必须明确控制路径和所有权，当前阻塞写入。
2. cpuhvfs的tbl-off四组对应哪个域及条目格式：
   不阻塞按已知cpufreq-hw布局读三域LUT；若选择hybrid接口写入则阻塞。
   不能把两种表直接当成同一种。若最终确认仅用hw接口且依赖已满足，不必强行先移植hybrid表。
3. MCUPM是否需先初始化，以及XNU接管时固件是否运行：
   不阻塞只读观察；固件/电压控制就绪尚未建立，当前阻塞可写调频。
4. status位由谁、何时置位：
   可以先采原始值；不能仅凭两位为1就宣称固件、供电和调频可靠。
   当前缺口阻塞把status当作写入许可；不意味着必须找到某条置位源码才能做任何后续验证。
   候选对bit1比vendor更严格，是故意保守；NOT_READY不自动等于设备故障。
5. USRAM/ESRAM与CSRAM是否重叠及实际资源关系：
   不阻塞当前已限定的performance-controller只读范围；写共享SRAM、加载固件或接hybrid路径前，
   必须按同一实机DTS核清完整区间与所有权，不能混用不同版本设备树。

选择仍是分阶段CPU路线：先取得只读基线，再判断是否补固件初始化，最后才设计有界写入与恢复。
不直接改Qualcomm EPSS基址，不直接选最高频，不把CPU频率推测成全部性能问题的根因。

### D. 额度恢复后第一件事

先读本节、同目录HANDOFF-CODEX-20261116-EOL.md，以及DSH在文档仓raw目录下的
host-cpu-baseline-01、candidate-cpufreq-review、build-cpu-probe-713三份审查。
核对尚未复核的最新提交、归档和反对意见后，再进入PACK-CPU-PROBE-713。
沿用已验证内核；使用同目录arguments713.json和review-package.py，不重复创建/覆盖源码树或重建。
镜像须完成严格解包、XNUP终止项、参数及loader复现审查；随后才可由DSH单次试验并恢复原厂。
Codex不接管设备。既有禁令继续：不stash/reset/checkout/clean/gc/prune，不覆盖旧证据或BUILD，
不写shared内核、不推生产仓、不触碰GPUEB 0x13c40000/OEM expdb，不启用watchdog，不扩大硬件范围。
采样率/位深仍留待以后。现在写完即停。
