# Codex 额度交接：MT6895 CPU probe，2026-11-16

本文按用户紧急指令优先落盘。没有为编写本文重新执行构建、测试、设备操作或校验脚本。
本次暂停只因用户明确要求先交接；不要把它写成技术失败或设备故障。

## 1. 当前阶段与第一件事

**BUILD-CPU-PROBE-713 已完成。**

- 内核：15321336 B。
- SHA256：`a80231ed9dc78a82bfddc71f34685808e66ce490d70ed059e37cd108638885c3`。
- make 原生 rc=2，显式 KERNEL judge 原生 rc=0；两者分别记录，只豁免既有 kext symbol-set 问题。
- **未打包、未刷写 trial713。** 不存在本轮 CPU 探测的真机结果。
- 用户通知文档仓已推 `c34d20b` 并远端同步；此最新提交由用户报告，Codex 尚未独立复核。
- 手机仍是此前已恢复的原厂 Android。本阶段 DSH 只采了普通 Android CPU sysfs 基线，没有调频、改设置、重启或刷机。

**额度恢复后第一件事：阅读本文件和 DSH 的三份 CPU 审查文档，核对尚未独立核验的归档/提交及反对意见，然后决定是否进入 PACK-CPU-PROBE-713。**
不要重复创建或覆盖 kernel-src，不要重复构建已经通过的内核，也不要直接刷写。
若进入打包，使用已落盘的 arguments713.json 与 review-package.py；完成严格解包与 loader 独立复现后，才能给 DSH 发单次设备 CMD。

## 2. 上下文与用户授权

P：`/home/lizi/IOS/hakios-mtk`，生产/构建工作区。
D：`/home/lizi/IOS/repo/hakios-mt6895`，文档与源码快照归档仓。
T：`P/logs/mt6895-cpu-baseline-20261116`，本阶段隔离目录。

耳机阶段已验收为“耳机确实出声，基本算半好”；采样率和位深由用户明确留待以后。
音频最终源码包已归档并通过复跑，不能用本阶段实验替换该包。

用户后来扩大范围到整个 iOS 环境，包括基带、GPU、CPU 调度，并授权 Codex 与 DSH 商讨下一阶段后直接推进，不必再次询问用户。
刷机授权之前已给出，用户在手机旁兜底；但本条最新紧急指令要求现在停止其他工作、优先交接，当前不得继续测试/构建/刷写。

分工：Codex 负责候选源码、独立复核与 CMD；DSH 独占设备及刷写/恢复，也负责独立主机审查和 D 仓归档。Codex 没有直接使用 adb/fastboot。

## 3. Codex 已独立复核的内容

以下是紧急交接指令到来前已经完成并落盘的事实，不是本文件编写时新跑的检查。

1. 阅读了现有 Qualcomm `drivers/power/hakios.cpufreq.c`、MT6895 配置、stubs 与启动入口。
   该驱动使用 EPSS 基址 0x18591000、Qualcomm LUT 编码、PERF_STATE 偏移 0x320。
   MT6895 配置明确关闭 HAKIOS_CPUFREQ 和 HAKIOS_MODEM，由 stubs 提供空入口。
2. 在 trial712 冻结树独立复算文件命中：60 个 bsd/dev/hakios.*，23 个匹配 redwood|POCO X5 Pro，6 个严格匹配 xaga，交集2。
   文件数量不是编译数量；注释命中、日志关键词为零，都不能证明某地址错了。
3. 从小米官方 xaga-s-oss 下载并冻结 mediatek-cpufreq-hw.c，读过其 LUT、请求、状态与初始化逻辑。
   本地快照与 live DT 的 compatible 都是 mediatek,cpufreq-hw。
4. 实现只读候选和宿主模型：clang ASan/UBSan、-Wall -Wextra -Werror，编译/运行原生 rc 均0，PASS130，运行 stderr 空。
   覆盖70个读失败位置、35个第二快照字变化、坏索引/坏表、默认/非法参数零映射、映射失败及只读保护页。
5. 用 XNU 真实头文件编译 ARM64 独立对象：rc0、无诊断。DSH 的 shim 对象是另一项独立检查，不能拿它的大小与真实头文件对象比较。
6. 创建全新隔离 kernel-src：基底5205项，不复制 BUILD；5个原文件变化，新增1个文件，总5206项。
7. 全内核构建完成后，Codex 独立核对5206项输入全部哈希一致，BUILD 外无新增/消失文件，冻结后源码变化0。
8. 独立计算新内核尺寸及完整 SHA256；确认实际 BUILD 中有 hakios.mt6895_cpufreq.o，内核包含 mtk_cpu_probe、read-only probe 和新入口符号。
   这证明构建与包含关系，不证明运行或硬件效果。不能从最终文件只增80字节推断新增机器码仅80字节。
9. 再核对 production-before.json 的8个生产输入，均未改变。本阶段没有修改生产树源码。
10. 已准备 trial713 参数：base479 B，运行时539 B，含NUL540 B <=608；没有打包执行它。

记录位置：
- `T/candidate-model-result.json`
- `T/cpufreq-arm64-result.json`
- `T/isolated-tree-preparation.json`
- `T/kernel-inputs.sha256`
- `T/independent-kernel-review.json`
- `T/production-before.json`

这些 JSON 是此前已存在的证据；用户禁止现在新写复杂 JSON 校验脚本，接班者不要为了本交接再次生成一套。

## 4. 尚未独立复核、尚未执行的内容

- 用户报告的文档仓最新 c34d20b、远端同步及该提交中的所有归档 blob，Codex 尚未独立核对。
- DSH 的 Android CPU sysfs 原始采样，Codex看过终端摘要，尚未逐项核验归档原件与所有频率表。
- DSH 最新三份 CPU 审查文档尚未逐字读完；其中两处限定已由 DSH 报告写入。
- 当前 Android 运行的 cpufreq 模块尚未与官方源码建立二进制身份对应。
- review-package.py 已由712验证器适配到713，但**没有本轮镜像可供运行验证**，所以不得写“713镜像PASS”。
- 没有713 loader、payload 或 boot.img；没有713 HLOG；没有本轮 CPU 寄存器快照。
- 没有在 XNU 下使能 DVFS、写性能请求、改 PLL/电压或验证实际频率变化。
- 没有频率提高、性能提升、功耗/温度或全系统 iOS 可用性的结论。

## 5. 下一阶段候选与选择

原先“把23个 redwood 驱动改地址”及“仅把 EPSS 地址换成 xaga”都不足以描述所需工作。EPSS/MPSS 是 Qualcomm 协议，需要 MTK 对应实现。

倾向第三条、分阶段执行的 CPU 路线：

1. **先完成只读探测镜像的主机打包与审查。** 使用当前已构建内核，不重建，不改变频率/电压。
2. 审查通过后，由 DSH 单次执行 trial713，采集三个性能域原始快照，并正常返回、恢复原厂。
   每个域是否可解码只是观察结果。NOT_ENABLED/NOT_READY/BAD_TABLE 不自动等于芯片故障或 Android 异常。
3. 根据快照再决定：需要补 MCUPM/cpuhvfs 初始化，还是已有固件可接受有界请求。
   任何实际调频都要另立操作与恢复设计；不能直接选最高行。

理由：CPU/调度影响全局，且已有 live DT、官方驱动和 Android 基线；只读快照能把控制器状态与推测分开，同时不引入新的供电/PLL操作。
GPU、存储、基带仍是独立方向；目前不能宣称 CPU 固定频率是所有慢的根因。GPUEB WIP 不随此任务扩大。

已有 xaga CPU 相关工作可复用：BOARD_CORE_ORDER、MPIDR映射、loader PMU/PLL观察、GenericARMPE慢核延迟启动和调度偏好。它们不等于完整 MTK DVFS 或 Apple CLPC。

## 6. 五条未证实项

以下五条保留为开放问题，不用猜测补齐：

1. **cpufreq-hw 与 cpuhvfs 谁拥有/初始化相关控制状态、两者如何协作。** 二者节点和 SRAM 资源交集存在，不代表一定互斥或“谁赢”。
2. **cpuhvfs 的 tbl-off 四组分别对应哪个域、具体条目编码。** 不可把它直接当成 cpufreq-hw 的32行 LUT。
3. **MCUPM 是否必须先初始化、由哪段启动链加载/启动固件，以及 XNU 接管时状态如何。**
4. **status bit0/bit1 由谁置位、何时可靠，以及 XNU 下能否维持。** 不能由字段名称证明固件已运行。
5. **USRAM/ESRAM 与 CSRAM 是否重叠及其实际资源关系。** 需按同一实机 DTS 的完整 reg 区间逐项计算；不能混用不同版本 DTS 的范围。

补充限定：DSH 采到 transition_latency=1000ns，不能据此独立断言实机 +0x114 寄存器读到1；运行模块与源码的身份对应尚未建立。

## 7. 关键路径与身份

### 当前内核

`T/kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837`

- 15321336 B。
- SHA256 `a80231ed9dc78a82bfddc71f34685808e66ce490d70ed059e37cd108638885c3`。
- `T/host-review/build-rc.txt`：native rc2。
- `T/host-review/judge-rc.txt`：judge rc0。
- `T/host-review/build-stdout.log`、`build-stderr.log`、`build-combined.log`、`judge-stdout.txt`。

### 候选与协议

- 候选：`T/candidate/hakios.mt6895_cpufreq.c`
  SHA256 `ef013ea9178e18a2018cf865e609f26f5ef15fdafcc680f95d38e2c165d7b6e9`。
  隔离树副本：`T/kernel-src/bsd/dev/hakios.mt6895_cpufreq.c`，同内容。
- 模型：`T/candidate/test-cpufreq.c`，当前 PASS130；不在生产树。
- 官方源码：`T/mediatek-cpufreq-hw.vendor.c`，8999 B，387行。
  SHA256 `3cc72e1f5b36e0154ab34df932a1541a59e2f1932b160a5538866df7f2d31d8f`。
  URL：https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/xaga-s-oss/drivers/cpufreq/mediatek-cpufreq-hw.c
  这是分支文件的冻结快照；不冒充已固定的 git commit 或当前运行模块身份。
- SoC配置：`P/mtk/mt6895/configs/mt6895.config`，5284 B。
  SHA256 `b8edfb815291c9d45a06a964a919c7ebaaafbf1869b0be5d98acc4367134a2de`。
- 实机 DTS：`D/docs/headset-wip/raw/dapm-preflight-20261115/xaga-live.dts`，513480 B，14212行。
  已知 SHA256 前缀 `f47d073b92b76209`。**Codex 当前上下文没有该文件的完整64位摘要，未重新读取；完整值应从 DSH 归档身份记录核对，不能把前缀伪写为完整哈希。**
  performance-controller约2714行；cpuhvfs约2753行；MCUPM约1344行，按该特定文件版本核对。
- 另有旧设备树：`/home/lizi/IOS/work-xaga/dump/xaga_live.dts`、`lk_live.dts`；不能把它们的行号/资源尺寸与上面新封存 DTS 混为一份。

### 只读设计与隔离接入

- 域基址：0x0011bc10、0x0011bd30、0x0011be50，各0x120。
- LUT偏移0..0x7c，32个32位字，低12位×1000得到kHz。
- ENABLE +0x84；REQUEST +0x88；STATUS +0x8c。
- 候选每域两次完整快照，共70次读；没有 ENABLE/REQUEST/PLL/电压写。
- 两份快照不同则拒绝解码；非零下降表；重复频率终止；索引必须小于有效条目数。
- enable严格等于1，status低两位必须都为1才报可解码；这比 vendor 在bit1超时后只警告更严格，是有意保守。
- REQUEST/LUT 是请求/查表信息，不是物理 CPU 频率测量。
- `T/prepare-isolated-tree.py`：**已执行，不得重跑覆盖现有树。**
- `T/integration-before/`：三个接入文件的修改前副本。
- 5个变化路径：bsd/conf/files、bsd/dev/hakios.input.c、bsd/dev/hakios.mt6895_headset.c、bsd/sys/hakios.input.h、bsd/sys/hakios.pmif.h。
- 1个新增：bsd/dev/hakios.mt6895_cpufreq.c。
- 当前只是隔离实验树通过 standard 接入；未向生产配置/注册表安装新驱动。

### 待用的参数与后续工具

- `T/arguments713.json`：已经落盘，不要凭记忆重写参数串。
  四核、既有 GPU 行为参数保持；mali_trial=713、headset=0、mtk_cpu_probe=1。
  移除所有 hp_* 音频实验参数；loader_return_after=150；watcher=0，硬件WDT=0。
  base479 B，runtime539 B，NUL inclusive540 B。
- `T/review-package.py`：由712严格验证器适配，尚未用于713产物。
- `T/REVIEW-AND-PLAN-713.md`：更详细的主机分析。
- `T/CMD-BUILD-CPU-PROBE-713.md`：本轮已执行的构建任务单；到此不打包、不刷写。
- 旧712打包复现参考：`P/logs/profile4-low-output-20261116/trial712/compile-commands.json`、`mkpayload-cmd.json`、`chainboot-cmd.json`及loader-src镜像。
  后续只能复制到新713目录并使用结构化argv，不能覆盖旧产物。

### 音频、PMIF、恢复与旧守卫

- 最终712音频源码：`P/logs/profile4-low-output-20261116/delivery-source/mtk/mt6895/drivers/audio/hakios.mt6895_headset.c`
  SHA256 `8c0cf1c232d0ac4f108a9ab4693559e99cb5efeecc00753c1e2bf7e96d300a02`。
- pmif.h SHA256 `f4d9a964e304ea14686359b85e75560325e9d47ee2ecb5614aae538f9e293d78`。
- PMIF core SHA256 `57a5d61dec1a3d9ec649e8c2505e0294e2dee11a34660365d35c2027d607b433`。
- 原厂恢复镜像：`P/backup/boot_a_stock_real.img`，67108864 B。
  SHA256 `c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc`。
  MD5 `a63b2e35a7c2522cb24a6a10332b9bc8`。
- 最近已知 Android boot_id `686cb45c-3f54-481f-ba0c-5e9945f95449`，slot_a、boot_completed1、SELinux Enforcing。
- 设备序列号只用于本地指定设备，不进入公开归档：`GAJZ4HIN99AQ69Y9`。DSH 归档本文件时必须脱敏此行。
- GPUEB旧WIP：`D/docs/headset-wip/findings/GPUEB-INTEGRATION.patch`
  SHA256 `9cdcb0da826e9bd561fce98aae81304789fee2742c63b10235942f028e53eb16`，未改未提交。

## 8. DSH 文档与通信

用户最新报告：D 仓 `c34d20b` 已同步。

- `D/docs/headset-wip/raw/host-cpu-baseline-01/README.md`（307行）
- 同目录 `PROTOCOL-VENDOR-CPUFREQ.md`（307行）
- `D/docs/headset-wip/raw/candidate-cpufreq-review/README.md`（302行）
- `D/docs/headset-wip/raw/build-cpu-probe-713/README.md`（231行）
- Android原始基线本地：`T/host-review/android-cpu-baseline.txt`。

DSH 摘要：online/present/possible=0-7；policy0/4/7 的 scaling_driver 全为 mtk-cpufreq-hw；related_cpus分别0-3、4-6、7。
频率表项数25/32/29；采样时cur为2000000/500000/1500000 kHz；governor为sugov_ext。
这些是一次 sysfs 采样，不是持续频率测量，也不证明 XNU 下有相同状态。

已有 Orca CLI 与终端：
- CLI：`/home/lizi/.config/orca/linux-orca-cli-shim/orca`
- DSH：`term_75f2770f-7953-46dc-ab93-dd26817d789b`
- `terminal send` 输入接受不等于已执行；读屏确认，不在沉默时重复发送。
- 接班前已使用 orca-cli skill；需要时按该 skill 的终端命令接续，不另开设备操作者。

## 9. 红线与禁令

1. 当前紧急指令生效：停止其他工作，不再构建、测试、刷写，不新写复杂JSON校验脚本；先交接与归档。
2. 不 stash/reset/checkout/clean/gc/prune；不丢弃既有未提交改动；不修整或提交 GPUEB 旧WIP。
3. 不覆盖旧隔离源树/BUILD、旧镜像、原始日志、原始录音。所有新工作用新目录。
4. 不写默认/shared hakios-kernel，不向 P 上游推生产改动；经授权的文档/快照归档推 D。
5. 不访问 GPUEB 0x13c40000；不扩大 GPU 电源/时钟实验；不访问或写 OEM expdb。
6. 不随意开启 watchdog；本轮 watcher与WDT均保持0；保留150秒返回路径。
7. 不更换slot、SELinux状态或无关设置；不以黑屏长按电源作为常规恢复方式。
8. 不进行手工 PMIF/SPMI/codec 任意寄存器访问，不调用有副作用的 PMIC getter；不把原厂Android基线扩大为任意硬件探测授权。
9. 未来如刷写，只能 DSH 在镜像身份与回退路径审查后执行，限定序列号、boot_a，必须恢复原厂并核验全分区MD5。不要根据此交接直接开始刷写。
10. HLOG：刷前保留旧区；返回后先抓原始记录，再等boot_completed；检查完整整数sum(body)==header64位sum，不能只比低字节。不得手工清 xnusys 日志区。
11. 打包必须含24字节全零XNUP终止项，严格EOF、padding、stock header/ramdisk/signature/嵌套内核与参数串检查；r1缺终止项的旧失败记录不能删。
12. shell命令不得用错误的timeout|tee管道尾rc冒充构建/刷写原生rc；参数和多行正文用结构化argv/文件，避免反引号与命令替换吃掉文档内容。
13. 不上传含环境声音的原始MP3；公开归档脱敏序列号。源码原件和历史证据保持原字节。
14. 705/707 FAIL、709源链INSUFFICIENT保持原判；不把请求寄存器/枚举/字符串存在写成硬件、声学或性能PASS。

## 10. 接续检查点

- 当前没有 Codex 发出的 PACK-CPU-PROBE-713 或 DEVICE-713 CMD。
- DSH 本轮构建任务已完成，不应自行加试。
- 本交接写完后只需通知用户/DSH路径，由其归档推送；不再执行其他检查。
