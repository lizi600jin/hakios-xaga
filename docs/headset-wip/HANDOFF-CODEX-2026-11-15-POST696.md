# Codex 交接：trial 696 后收尾，证据与推断分开

生成时间：2026-11-15 14:11:47 CST。本文件是 Codex 的独立复核和判断，不是 DSH 报告的转述。

新接手者应完整读本文件。DSH 的 `HANDOFF-2026-11-15-1350-DSH.md` 有用，但其摘要、排除列表和网络经验仍有过强表述，不能不加区分地继承。这里直接列出已核事实、来源、未决项和下一步边界。

## 0. 接手即知

1. **MCU 启动问题未解决。** 696 的准确结果是“未观察到已完成的 marker store”。不能改写成“没有执行任何 str”“没有取指”或“所有内存因素已排除”。
2. **本轮未发 GPUEB CMD_INIT_SHARED_MEM / POWER IPI，未做新的共享状态探针，未读禁区。** 现在也不因 witness 阴性就授权这些动作。GPUEB 不是唯一原因，未获得新的因果证据。
3. **手机由 DSH 持有。** DSH 报告 696 后已恢复原厂 Android 17，boot_completed=1，SurfaceFlinger 正常，boot_a MD5=`a63b2e35a7c2522cb24a6a10332b9bc8`。这轮实际刷机/提取/恢复是 DSH 做的；Codex 没有对手机发 adb/fastboot 命令，不能冒称自己执行或独立读回了设备分区。
4. **构建树干净，停在 `trial/jobirq-695` / `6a51ef21564085678dc2a4fee847e56a026e3757`。** 没有 697 或后续实验，没有后台构建。696 使用同一个已构建内核，仅重编 loader 参数。
5. **公开源码尚有同步缺口。** `tree/mtk/mt6895/drivers/gpu/hakios.mali610.c` 仍等于冻结 c9 版，少本地 18 行 JOB IRQ 只读诊断。可发布补丁已生成，见 §2。日志、镜像存在不等于新 clone 能重建相同的诊断内核。
6. **原始目标耳机尚未做成功的实机验收。** 无耳机；headset=2 只是离线首轮实现及编译验证。695/696 的参数有 headset=1，但我在当前完整日志中没找到 headset dump 输出，不能声称 129 项基线已经完成。
7. 本轮在这里收工。下一步实验只是本文件的建议；**未构建、未编号、未刷机**。恢复原厂不等于以后任何试验都已获授权。

## 1. 工作区、版本、权限

- 构建树：`/home/lizi/IOS/hakios-mtk`。远端是他人上游，不向其 push。不要全树 stash/reset/clean。
- 项目发布树：`/home/lizi/IOS/repo/hakios-mt6895`，项目为 `lizi600jin/hakios-xaga`。git 提交/推送由 DSH 负责。远端配置可能含凭据，**不要把 git remote -v 原文打印、抄入报告或提交**。
- 冻结分支 `mtk`：`c9b463251d2f6c4754ed7cf2c3f8a336b41356b0`，保住原约 49 项 WIP 与 xaga 启动依赖。
- 活动分支 `trial/jobirq-695`：`6a51ef21564085678dc2a4fee847e56a026e3757`，相对 c9 仅 GPU 文件增加 18 行、零删除。
- 三个更早本地提交 ef54fa0、7df8a9d、4ebad21 已保留，不要以“领先上游”为理由删掉。
- 最新一次读取发布树 HEAD：`2bd07e8f7a843c03b49b163735a5c34fa1a2874f`；DSH 仍可能追加文档提交，接手时重新看 status/log，别覆盖他的内容。
- `COLLAB-PROTOCOL.md` 仍约定构建树由 Codex 编辑、docs/发布 git 由 DSH 编辑；`bridge/to-dsh.md` 是 Codex 可写的例外。本次交接主件放在我方目录，请 DSH 自行纳入 docs。
- 当前没有 Codex 子代理或后台工具进程；DSH 报告他的两个审计代理也已结束。并发上限 4，包括主代理；旧界面里“保持 8 个”的 TODO 是过期信息。

## 2. 必须补齐的公开源码同步

我亲自比对：

- 本地 GPU 源码 SHA256=`b6bf3483b35ae93e61d5d574a13e79506c4e89caeb57dbeab555c2197c50ef45`。
- 公开 overlay GPU 源码 SHA256=`2bac0829c0a144dfd04531ad5421b9a7f3c00899a18feaaad8bf20837c0aff04`，逐字节等于 c9 的 GPU 文件。
- 公共 PMIF 与 headset overlay 已与本地相同。loader 是上游已跟踪文件，走 `patches/0001-upstream-changes.patch`，不应因 `tree/mtk/loader/mtkloader.c` 不存在就认定漏发布。

待 DSH 发布的现成补丁：

`/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/trial-695-jobirq/export-jobirq-readonly.patch`

它由本地 c9..6a 的真实 git diff 生成。已在发布树运行下列只读检查，exit=0：

    git apply --check --directory=tree /home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/trial-695-jobirq/export-jobirq-readonly.patch

实际应用和提交归 DSH。先重新 check；如果已经应用，不要重复套。它不带固件、镜像或设备标识。公开发布基础仍是 upstream `db08f63ca513b711d562e4bd20d41fd9ab559f4e` 加 patch 和 tree，不能拿他人 MT6768 HEAD 直接覆盖本地。

22 个 intent-to-add 空 blob 曾引起误报，已在冻结提交前按真实内容入索引。原说“普通 commit 会悄悄提交为空文件”是错的；是否拒绝/跳过取决于 staged 情况，`git add <path>` 后提交真实内容。不要为此再做全树 git add -A。

## 3. 可复核产物身份

内核（三轮共用/相关关系如下）：

- 695/696 内核：15303968 B，SHA256 `18bb68da1898b0856d927f532a71e0a224cd9eac1d6ca72c6f52b44372415ea6`。
- 同一 payload：SHA256 `0cdc1c25d108195e6c6f392731fd014c2033392574968c352a5fa08e293231cb`，内核、ramdisk、kernel.ver 逐字节相同。
- 冻结 c9 内核：SHA256 `9dc40973afff3cf1551275d2da0fe80b8b4e9aa3089d32e1bcf1b26941cf191a`，不要与 695 内核混淆。

| 产物 | 完整 MD5 | 完整 SHA256 |
|---|---|---|
| 冻结 headset1 镜像（未由我刷） | `5e7c67f7cd8aeb90a86ff4098466812d` | `8859138ecad4a56bdbe71a5c3ba5f472044556023b04ef9e27eed1aea77978c2` |
| trial 695 boot.img | `2cce810f64fc8486fd90e1a70512477a` | `d2676827e9a51cb1ae20617f8721e7023c1d98b9d00f824b26880b9401b07bd8` |
| trial 696 boot.img | `b750fd86b73affbeb318377b6b8c779a` | `48c7c042917087e4c4fade613edec47f7bef589f2ceaf42cf9dcbdfe7660556a` |
| 原厂恢复镜像，64 MiB | `a63b2e35a7c2522cb24a6a10332b9bc8` | `c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc` |

路径基准：`/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/`。

- 695：`trial-695-jobirq/image/boot.img`。
- 696：`trial-696-witness/image/boot.img`。
- 原厂：`/home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img`。

我独立用 **695 的源码 hash 和原编译 flags** 隔离重建两版 loader：

- 695：48712 B，`8e33c679da54b0a21f3fcea8c5a7fc1885e69a2a13420ab2e33391825cbc99c7`。
- 696：48712 B，`9707b7e490d8fa57b12dcf55a75c7a424a9f10f0643c258ec2d6ffafb997e055`。
- 各自重建结果与保存产物逐字节一致；解包后 payload、Mach 内核、镜像嵌入的原厂 Android 内核、ramdisk、签名均独立核对。
- 脚本：`trial-696-witness/independent-verify.py`；完整结果：`independent-verification/result.json`。

两个 actual_loader_args 都是 **463 字节**，每个 loader 都可提取到三处精确完整参数。唯一行为 token 差异 `mali_witness=0→1`；另有识别标签 `mali_trial=695→696`。

我最初要求“字面替换两个 token 就应复现 loader.bin”过严，撤回这个验收条件。实际二进制差异 7963 字节，字符串布局/引用变化可导致大量差异；以**同源、同 flags、仅改参数的独立重建**为依据，而不是猜布局。695 和 696 都是三位数，根本没有缩短一字节；换 701 也不保证只差一字节。payload 相同本身不能证明 loader 代码无额外变化，因为 loader 也是执行代码；此次已通过重建补足。

全套实际参数以 696 `image/build.json:actual_loader_args` 为准，关键项：

    mali=5 mali_cold=1 mali_preas0=0 mali_low=0 mali_trace=1
    mali_witness=1 mali_ptvendor=1 mali_scclk=1 mali_scupd=1
    mali_sc26=0 mali_cacheflush=0 mali_resetonly=0 mali_cohnone=0
    mali_hwissues=0 mali_stackpwron=0 mali_mcuwin=0 mali_mcuseq=0
    mali_doorbell=0 mali_pdcsettle=0 mali_statussem=0 mali_assorder=1
    mali_trial=696 mali_nopdc=1 headset=1 loader_return_after=150

编译 flags 中 `XNU_WATCH_SECONDS=0`、`XNU_WDT_SECONDS=0`、`BOARD_MALI_POWER=1`、`BOARD_MALI_PREPOWER=0`。不要拿 .config 默认的 mali=6/cold=0 等参数代替。不要直接跑默认 xaga-cycle 生成一套不同实验。

## 4. 日志取证已打通，但我验证了哪一层要说清

设备是 KernelSU root。DSH 从 xnusys 尾部专用 logbook 区提取了 695/696，不需要再刷一次“取日志”。

- `HLOG @ 0x9f8f8000` 是 **xnusys 分区内字节偏移**，不是 DRAM PA。
- 保留区位于 xnusys 尾部 8 MiB；正文在 HLOG header 后 4096 B。
- header length 为 u64 @8；sum 为 u64 @64。sum 是各正文 byte 的加和，不是“结果只保留 8 位”。
- 仅使用 header 声明长度内的正文，验证长度/sum、再算 SHA256、查唯一当前 trial 标记。旧 4 MiB slot 的尾部可能是上轮残留，不能拼接或全盘 strings 后当本轮。
- 原始 header/设备提取由 DSH 核验；我独立重算了已提交正文的 byte 长度、sum 和 SHA256，并检查了标记。当前 docs 主要保存解码正文，不冒称我又从手机读取了原始 header。

| trial | 正文 B | byte sum | 正文 SHA256 |
|---|---:|---|---|
| 695 | 469781 | `0x24c8bcc` | `0bff18bf229a6a484e8e96b2e41cf8f5196b77a5bb5901e113f152d63d6481ab` |
| 696 | 470666 | `0x24da28c` | `068a82da755dadd204cf5da6f5a0827db9172ccb6d19c159efe7b19c94a89a6c` |

正文在发布树 `docs/headset-wip/raw/trial695-logbook/`、`trial696-logbook/`。

当前三项 loader 日志修复已包含在镜像：MARK_RUNNING 不再依赖 WDT 是否启用；rescue 路径真正写 flash HLOG；完整正常返回 HLOG 不再被后续 probe_flash(38u) 改写成 79 B。生产 decoder 是 `mtk/tools/xaga-logbook.py`，此前生产 C+decoder 正负 host cases 已验。

**不要恢复 fastboot oem expdb 路线；不要默认清空整个 slot。** 先保存有效 header/body，再做必要的定长读取。专用区外不能写，更不能清整个 xnusys。早期 full-slot 残留污染已被识别，不代表此后所有短日志都能靠拼接旧尾补齐。

## 5. 695 与 696 真正说明什么

### 5.1 JOB IRQ 只读补丁

新增 helper 按 RAW_before → MASK → STATUS → RAW_after 读取并标 raw_same；各条日志不是硬件原子快照。

- RAWSTAT `0x1000`，MASK `0x1008`，STATUS `0x100c`；GPU IRQ raw `0x020` 是另一组。
- MASK **1=使能**，vendor enable 写 ffffffff、disable 写 0；panthor enable |=bits、disable &=~bits。不能翻极性。
- 695/696 各六个采样点都是 raw=0/0、stat=0、mask=ffffffff、raw_same=1。只说明这些样本没有 GLOBAL_IF，不证明整个时间区间没有短暂置位，更不证明 MCU 零执行。
- 旧源码“RAWSTAT still zero”的注释曾没有实际 RAW 读取支撑，现已补观察；“mask=ffffffff 全屏蔽”的旧注释仍是错的，不可继承。
- HALT/FATAL 采样来自 MCU_STATUS，不经 JOB mask。MCU_STATUS=1 是 ENABLED/状态值，不是执行进度证明。

### 5.2 witness 的行为与观察

既有 32 字节 ARMv7-M/Thumb stub：设置 r0=0x04000000；依次 store 0x11、0x22，再把首字覆盖成 deadbeef；末尾 e7fe 自旋。**没有 cpsid、没有返回。** 不依赖完整固件的 MMIO 初始化，不发布 CSF interface、不置 GLOBAL_IF。

开关是一个行为变量，但同时做三件事：覆盖代码首 32 B、改 vec[1] 为 0x01000001、flush_all_sections。不能把任何差别只归因于某一条机器码。

696 原始行：

    34.827563 witness: vector was sp 0x2040000 reset 0x10018ed
    34.827594 witness: code at 0x1000000, reset vector now 0x1000001
    44.492408 witness: read1 0xff5aa5f0 0 read2 0xff5aa5f0 0 read3 0xff5aa5f0 0
    44.493581 witness: no completed first store observed (not an instruction trace)

**我认可的结论仅是：这个新供电基线上仍未观察到可见的完成 marker store。**

- CPU 读回自己的哨兵只约束 CPU 视图；不证明 MCU 的硬件 VA→PA、页权限、cache、store 或回读可见性全通。
- CPU 打印已改向量和软件 coverage，只证明 CPU 所见缓冲区/页表；不证明硬件已取到新向量。
- 无 MMU fault 只是未观察到故障报告，不排除静默翻译/访问/存储问题。项目此前已有缺 PTE_PAGE 而无 fault 地址的历史教训。
- MMU_IRQ 上半部 REQ_COMPLETE 是地址空间命令完成事件；**不是每次取指/正常 load/store 的事务计数**。启动期间为 0 不能证明没有取指。
- witness 阴性不能用来排除整类固件、段加载、共享页或所有 MMU/内存问题；它减少了完整固件逻辑的参与，但没有建立取指+store 的正向硬件基线。
- 旧 stub 没有 cpsid，中断/异常环境与真实 reset handler 不同；这项影响没有被本轮阴性结果消除，也没有被证明为根因。未来若改桩必须单独登记，不悄悄并入“同一纯 stub”对照。
- 不能把 GPUEB 升格成“只剩/唯一/最强原因”。`gpueb: not mapped` 只表明 AP 探针没映射；不是 GPUEB 没运行或没给 MCU 上电的证据。

现有失败路径在读 shared version 前有 mem_flush；read_witness 本身仅有屏障。不要只截 read_witness 一段就误报整个路径无 cache maintenance。它的阴性解释仍受可见性限制。

### 5.3 与旧实验的关系

- 604 fwnc=1、605 fwx=1：都在旧映射覆盖缺陷修复前，阴性不能排除当前情况。
- **673 已在 4 KiB 叶步进/完整 coverage 修复之后**；fwnc=1、low=1、witness=1，仍未见 marker；PDC 开启、F3C=0x4。不要把它也归到“映射修复前”。
- 691 是更接近当前参数的 witness；696 相对其 GPU boot-arg 主要新增 nopdc=1，但 PMIF/loader/观测代码和构建已有其他变化，不是同二进制 A/B。
- 695→696 才是本轮同 kernel/payload、仅 witness 行为开关变化的匹配对照。
- 未找到当前 nopdc/完整固件基线上 fwx=1 的实跑记录；fwx 同时清 XN 和只读权限，不是只动 XN。此次没有重测它，也没有新增 fwnc。

## 6. 固件来源是新的正面确认

**DSH 已从当前设备 `/vendor/firmware/mali_csffw.bin` 提取并正面对拍：与我们内嵌的字节完全相同。**

- 258048 B。
- MD5（两者）：`ddef4ef1802903d3f54259da07605229`。
- SHA256（两者）：`de22d2fc06bc8a859968499a6e609c27fe838be062bf5c97d92ea9f904f7229a`。
- 我独立从 C array 重新提取并计算了内嵌这份的完整 MD5/SHA256；设备文件的提取/两文件对拍由 DSH 执行，不冒称我发过设备命令。

“来源无法追溯/可能不是该机固件”这项疑虑在**当前设备文件匹配**这个层面已补齐。不要再把 sha256 前缀与 MD5 比较制造假差异。

另有上游 arch10.8 282624 B，SHA256 a27847ea11f8efb3136340c3ba8aab413ae25145eeb4a7f64ff5edd829a2405b，header 0.3；第三份 xaga r54 仓库文件 282624 B、9f67b2c3e4886f8d3f51ceac5170d0731bd44cb7c9ae5b094081e95f4028c7f8。同 arch 路径不等于字节相同；header 0.1/0.3 不能直接推出 r32/r54 或运行时 CSF ABI。**本轮没有换固件。**

## 7. Android 回程故障：直接原因已找到，底层因果未锁死

原始材料：`docs/headset-wip/raw/trial696-sf-crash/tombstone_04`。

我亲自读到：SurfaceFlinger，SIGABRT，abort 为 `no suitable EGLConfig found, giving up`，format=1，META-EGL；栈为 chooseEglConfig→create→RenderEngineThreaded。映射表实际有 `/vendor/lib64/egl/mt6895/libGLES_mali.so` 和 libGLES_meow.so，不能说没加载任何 Mali 库。

**正确分层：**

- Android 回程启动失败与 SurfaceFlinger/EGL 图形通路不可用是同一故障表现，不能武断当作与 GPU 完全无关的独立小问题。
- 已确认的**直接 abort 原因**是找不到满足条件的 EGLConfig。AOSP chooseEglConfig 会尝试 ES3、ES2、简化配置后才 fatal；这条栈本身不唯一证明底层 kbase 初始化失败、供电缺失或 GPUEB 问题。
- XNU 的 MCU/marker 阴性与 Android 回程故障**可能同源，也可能有不同环节**。现有 tombstone 不能证明二者同一硬件根因，也不能断言“诊断肯定无关”或“已证实 GPU 恢复不完整”。缺失败 boot 对应的 kernel dmesg、libEGL/Mali/kbase/GPUEB logcat、初始化/复位错误。
- 冷启动恢复原厂后正常，是重要对照，但还没隔离 XNU GPU bring-up、loader 自身 mali_power_down、其它平台状态交还各自的作用。

参考 AOSP（原始实现，不是本机 vendor 精确版本的替代）：
https://android.googlesource.com/platform/frameworks/native/+/716acde0fa2cd40a587ac88657c1ab48697e4a48/libs/renderengine/skia/SkiaGLRenderEngine.cpp

已向 DSH 请求已有失败 boot 的 logcat/dmesg 路径，未要求再刷一次来重现。当前只收到 tombstone；不要在交接中把缺失数据写成已收集。

## 8. 本轮推翻/收窄清单（后续不要复活）

| 旧结论/提案 | 修正后的口径 |
|---|---|
| loader 被重写 4172 行，所以当前路径无可信基线 | 真 692 产物找回；按真 flags 重建 loader 曾字节差 0。不能随意回退上游 |
| PDC 是充分原因/全部电源问题已排除 | nopdc 确实使域状态/F3C恢复，但固件/marker仍阴性；不能关闭核内所有时钟/电源/复位 |
| 7/12 ack 或 F34=1fc 表示五域没起 | vendor 终态判据为 F3C/F40、mask3ffc；695/696 F3C=3ffc。旧两位 ack 诊断是误判 |
| SHADER_READY=0 是启动异常 | 固件启动前 shader 不必 ready；固件自己请求上电 |
| SHADER_PRESENT一致证明 MCU 有电有钟 | 它是能力/寄存器可见性，不是 MCU 执行域的动态证明 |
| cycle counter 跑说明 MCU 域一定在跑 | 只证明该计数视图活动；不能外推核子块门控 |
| timestamp=0说明硬件时钟坏/从未写使能 | mfg_set_timestamp 已实现并调用；旧注释过期。新日志 timestamp 实际在动 |
| 0x103 cycle start 是纯只读零风险探针 | 包含 GPU_COMMAND 写；本轮未加，不能混进冻结只读实验 |
| MCU_STATUS=1=固件运行成功 | 只是 ENABLED/状态，不是取指、store 或 CSF启动证明 |
| STATUS只用于超时打印 | panthor 停机等待和 halt 判定也使用；只是启动成功不靠它 |
| RAW=0/fault=0就是从未执行 | 未观察到事件/报告不是 instruction trace |
| mmu irq 0 表示无取指事务 | IRQ不是正常访问计数；上半部 REQ_COMPLETE 也不是每次取指完成事件 |
| JOB mask ffffffff 全屏蔽，应该提前改0 | 1是使能，fffffff全开；改0反而屏蔽。不要做这个实验 |
| HALT/FATAL 被JOB mask屏蔽 | 来自独立 MCU_STATUS，变量不同 |
| 0x80000000 已在我们的历史日志出现 | 那条成功记录是 Ken-u 的 RK3588，不是 xaga |
| 百万次 delay(1) 等1000秒/等待太短 | delay单位us，名义约1秒加MMIO/日志，696实际约9.59秒。不要用错误单位设计新延时 |
| CPU入口字正确=MCU硬件映射正确 | 只证明CPU缓冲区/软件走查；GPU硬件取到什么仍未直接证明 |
| entry words 错位8字节 | 前两字是vec[2]/vec[3]，后两字才是code[0]/code[1] |
| INA_BITS(7)缺失就是新根因 | 当前0x42000006刻意对齐vendor，且该差异早已记录，不是新漏写实锤 |
| 固件大小/同arch就能证明Ken-u与我们相同 | 要比完整实际字节/sha；274432有真实sizeof记录，不能随口否认 |
| 当前PROT三段跳过、实载7段 | 当前代码把三段映为普通页，共10段；panthor跳过是另一策略，未证明哪种导致本机失败 |
| 无数据的PROT段必为正常启动必要BSS | 零数据不能推出正常启动必需；条件保护/恢复分支应分开看 |
| PROT恢复拷贝直接从03000000复制 | guard byte在0302bc78，直接拷贝源为03800000；两者不能混同 |
| bit7两点为0，就证明固件读时为0 | 不是同时观测。CPU零初始化支持跳过，但不是MCU实际分支轨迹 |
| 0x40022000无AS0 PTE应补映射 | 内部Device MMIO与04000000共享内存不同，不能用软件页表缺项推出不可达 |
| 0x40022000可恒等映射到SoC PA40022000 | 同数值的SoC地址是DRAM等另一视图，不能乱补；AP13022000别名也没逐寄存器验证 |
| MCU架构本项目首次确定 | 旧work-notes和witness已有Thumb/M-profile证据；本轮是复核扩证 |
| cpsid i 是Cortex-M复位固定第一条且M类独占 | 它是该固件的选择，不是架构强制；单独也不能区分所有AArch32 |
| f44f21e0=#2e0，SHCSR启用bit5/7/9 | 实为70000，MEM/BUS/USAGE fault enable在16/17/18；DIV0设置不在这里 |
| 13个PPB常量全在同一池 | 11个在2a58..2a80，另SHCSR@2824、AIRCR@2af4 |
| EF68/6c/70/74分别是DCCMVAU/DCCMVAC/DCCSW/DCCIMVAC | 应为DCCMVAC/DCCSW/DCCIMVAC/DCCISW；DCCMVAU在EF64，不在13个literal清单 |
| 这些cache地址唯一识别M7并证明Dcache存在 | CMSIS CM55也定义它们；静态代码不能证明本机cache存在/开启 |
| 自家Cortex-M7注释是独立硬件佐证 | 只是实现记录；外部依据为panthor与Icecream95研究 |
| 两份完整reset逐条相同 | 关键开头同构；上游多SCR=2，SP/代码VA等不同 |
| 第六字0x20是alignment | IFACE五个u32后是名字区；20是空格+NUL，config/shared名字能解出 |
| 所有vendor kbase都要求virtual_exe_start | k-xaga HEAD所取副本不要求；.csf-re副本要求。必须注明版本/路径 |
| 因格式像panthor就证明它由panthor打包 | 格式匹配不证明打包者/年代；原厂文件正面同字节才是来源证据 |
| 0xc000001b本身是non-cacheable | 原始cache mode=3 cached-coherent；当前按策略降为NC是加载器处理，二者分开 |
| 改695为696使字符串短一字节/701就只差一字节 | 都三位，参数均463B；后续应以源/flags重建校验 |
| payload一致就证明运行代码都一致 | loader也执行，需独立重建验证；本轮已完成 |
| witness阴性排除全部FW/映射/cache/AS0 | 阴性不能这样排除；具体观测和软件静态检查需分别记录 |
| 两个marker写MMIO=11再写RAM=22可定位 | MMIO不是marker RAM，原值是b；必须有独立RAM前标记，且阴性仍不唯一定位 |
| 跳回原reset就能保证安全退出 | 属新实验，可覆盖marker并进入完整FW，也不保证停止；本轮未加 |
| not mapped证明GPUEB没跑/没上电 | 只说明AP未映射；未验证硬件活性、门控、所有权 |
| 没有本轮INIT就必定全脏/RO字段必可靠 | 既不能保证有效，也不能一概断言从未被其他启动阶段初始化；有效性/新鲜度未知，不拿来判活性 |
| ShaderPresent是shader上电状态 | 是能力/可用核位图，不等于READY/供电完成；power_control也是使能标量，不是power-on位 |
| power_count=0说明从未上电 | 可能正常引用计数归零，Android已有0且正常的记录 |
| GPUEB host sim 4/4证明可直接真机IPI | 不证明门控/时基/内存契约。模板默认barrier只有编译器屏障、UDELAY空操作，不能原样集成 |
| /proc/gpufreqv2/gpufreq_status是纯被动读取 | vendor show→get_debug_opp_info→CMD_GET_DEBUG_OPP_INFO，会发IPI |
| Android EGL失败已唯一锁定kbase/与XNU同根因 | 直接失败点已确定，底层及同源关系仍需驱动日志 |
| marker设好就能解决所有黑屏日志 | BOARD_NO_PLOG使plog_putc空；还需rescue真写flash和防止79B覆盖 |
| watcher默认能兜底 | 本镜像watch/WDT都0，不能依赖；人工fastboot/原厂备份才是本轮兜底 |
| SG EN六条寄存器就能让耳机响 | 只是参数配置，完整模拟上电/SDM/HP路由约70笔，检测和出声解耦 |
| 无耳机也可宣称插入识别成功 | 只能做可读性/未插入基线；AB还受VAUD18供电条件影响 |
| baseline 60%匹配就通过/两份相同md5是独立采样 | 全0死总线可获约60%；原两份文件相同不能算独立采集 |
| 低延迟节点一定可用/405证明所有后端认证正常 | 服务可达和完整鉴权分开；只保留本次具体观测，不把节点后缀当永久保证 |

## 9. GPUEB 未决项与当前决定：不发 INIT

- 已有协议进展：消息32 B/8 u32；TX slot4，RX slot47；INIT中两个u64 base在前、两个u32 size在后。不能用旧4u32版本。
- 但 GPUEB 窗口门控/活性、共享缓冲区契约及恢复仍未完成验证。保持不加 `mali_gpueb=1`、不直接读 `0x13c40000`；也不能只换绝对地址就声称门控已解决。
- 名义共享区 `0x7c600000`/2 MiB 在 `/tmp/mt6895-mainline/arch/arm64/boot/dts/mediatek/mt6895-xiaomi-xaga.dts:200-203,621-623` 可见；我查看的 `dump/xaga_live.dts` 有 gpueb_mem_table，但未找到相应 live base 属性。报告把两种来源混称“真机DT已证”过强。下一步应核实际设备/启动链的 reserved memory 和传给固件的地址。
- 名义GPUFREQ块是首4 KiB，status 0x400、debug 0xc00，不是把整个2 MiB都当GPUFREQ debug。字段power_control @0x60、shader_present @0x68符合当前所读前缀结构，但仍须匹配实际FW/API版本。
- 在XNU未建立/确认INIT的情况下，只读内容可保存为**未知来源原始快照**，不能把0/非0都当作新鲜GPUEB状态或活性证据。不能为了“让只读数据有意义”就自动授权写INIT。
- `gpueb_reserved_mem.c`从设备属性获取基址并ioremap_wc；它不是GPUEB自己挑的固定地址。新INIT会修改绑定，是有状态命令。
- Android `GPUEB Support: On`主要是软件支持配置；`DBG_POWER_OFF`是debug电源引用状态，不等于GPU失效。`ShaderPresent`与XNU一致是相应读数对拍，不是MCU域已工作证明。
- Android状态接口存在，但读取debug状态会发CMD_GET_DEBUG_OPP_INFO。它可以提供受Linux驱动管理的Android侧参考，不可当作XNU被动读取共享区的等价物。
- 模板不能原样用：真实硬件barrier至少要dsb语义、真实udelay/时基与硬计数兜底、正确32B报文和ACK、超时后不盲重试叠加电源引用。只是主机编译0警告、sim4/4远不足够。
- 本轮没有新建这项驱动探针，也没改时钟寄存器。先取得有解释力的前置与错误日志，不烧一轮只会产生未知0/旧数据的试验。

## 10. 建议下一步：先回程取证，后续单变量候选与退出边界

### 10.1 第一优先：复用已有材料

找695/696那次失败Android boot的kernel dmesg、logcat（libEGL/Mali/kbase/GPUEB）、boot-id/采集记录；不要只用次数和一个abort栈判底层原因。若已留存先读，不重刷为“取日志”。当前完整XNU HLOG已经可在原厂Android下取。

### 10.2 如确需下一次设备实验，我建议先做回程隔离对照

**候选：以696的相同kernel/payload/loader宏为基线，仅把 `mali=5` 改为 `mali=0`，另用唯一新trial标签；其余参数保持。** 本轮未做、未编号、未构建。不要将候选号697当成已预留，接手时先查占用。

我核了 `hakios_mali610_init()`：mali缺省/0会在创建GPU worker前直接return。这可检验“本版XNU GPU bring-up路径是否是Android回程失败的必要参与因素”，比读无新鲜度保证的GPUEB buffer更容易解释。

但必须明确：

- 它是一个控制整个bring-up路径的开关，不是某一笔MMIO的隔离，也不能单独回答MCU为何没marker。
- **不等于全程不碰GPU**：保持的 `BOARD_MALI_POWER=1` 仍使loader返回路径执行mali_power_down。这正是要把XNU驱动路径与loader交还路径分开。
- Android若仍失败，说明XNU Mali worker的执行并非该失败的必要条件，继续查loader/平台交还和驱动初始化日志；不能把所有GPU因素都排除。
- Android若恢复正常，说明开启本版Mali bring-up与故障有配对关联；仍需日志定位是哪段状态，不能直接宣布GPUEB或某一寄存器是根因。
- 本轮witness阴性已取得；没有新前提时不要再原样重刷696，也不要悄悄改成带MMIO/返回的新stub。

退出要求：DSH完整持机独占；用户明确可人工回fastboot；先验证原厂备份和当前boot_a一致；构建并独立核验完整新哈希，刷前通告；只运行一轮。ADB回来即有界收集日志，不能等boot_completed=1才开始取证（SF失败时它可能永不为1）。150s是XNU timer尽力返回，之后loader约20s展示再链Android，不是保证；watch/WDT仍不作兜底。失败先报告，不循环主机USB复位。无论哪条退出路径，最后都刷回并验证原厂boot_a与正常Android。**链Android不会自动还原boot分区。**

### 10.3 GPUEB与时钟后续

另一条线应先离线查明GPU/GPUEB实际clock/reset/bus访问前提及可安全观察的寄存器。当前gating证据不足，别把“源码没clk_get/DT没clocks”当作永远无门控；也别把移动cycle或枚举shader当作核时钟证明。

只有地址/ABI/门控/缓冲区所有权/超时与恢复路径形成具体可审方案后，才另立受控INIT/POWER实验。需要写时就明确是写实验；不要包装成“零风险只读”。本文件不批准它，不新增禁区映射。

## 11. 耳机、PMIF和日志的既有代码进展必须保留

详细前序证据见 `HANDOFF-CODEX-20261115-0132.md`、`pmif-shared-integration/REVIEW.md` 和同目录host测试结果，不需从头重做。

### PMIF

- 已共享到 `mtk/mt6895/drivers/core/hakios.mt6895_pmif.c` / `xnu/files/bsd/sys/hakios.pmif.h`；keys/headset/GPU复用已有hakios_spmi_lock，不建第二把锁。
- 单笔事务持锁；整笔一个100 ms预算、最多10000 polls、失败间隔10 us、deadline/硬计数先到为准。不是每段各100ms，也不是遗留2ms+20000。
- 读失败遇FSM6才补VLDCLR ACK，含初始IDLE迟滞等失败点；只在完整成功后发布value。写有bit29、等IDLE，不等读专用WFVLDCLR。
- 不再接受能算到其它通道的任意chan；精确方向/offset允许列表限制SWINF2。VM映射会按页取整，**真正的0x900/0x940防护不能只靠请求映射长度**。
- map发布受同锁保护，但VM分配/释放在锁外；不在持锁期间delay(2000)或打印。
- 停核之后完全不碰PMIF，连无锁best-effort ACK也不做；停核超时不意味着其他线程一定停了。rollback需正常worker阶段执行。
- 构建依赖走既有core/conf的MT6895_PMIF和三个requires。旧drivers-check usb目录问题已修。
- 已有真实共享C代码的host状态机/并发、ASAN/UBSAN等验证；696运行不等于所有真实PMIF时序与回程恢复均已验收。

### 耳机

- MT6368是SID5；权威源码在 `/home/lizi/IOS/work-xaga/research/raw/xaga-ecosystem/mt6368-accdet.c` 和 `mt6368.h`，本地k-xaga sparse repo可git cat-file读未checkout的sound源码，不反汇编ko代替。
- headset=1是129个只读寄存器：原124+0008/0009/1cb2/230c/231d。VAUD18只观察不打开，不把AB=3当插入检测成功。
- headset=2首轮离线实现已编入内核，仍有VAUD18/时钟/纯轮询SW_EN持久性等前置待收尾；无耳机，未验证插入/按键/阻抗/出声。不能把编译通过写成硬件完成。
- check_cable_type是有前态状态机，5000Ω函数只消费阻抗、不是ADC→Ω换算；VLDCLR是W1C，不能当普通寄存器RMW后校验。
- headset=2的VAUD18启用逻辑尚未补齐；当前1cb2/230c主要在只读扩展dump内。后续按厂商序列逐项复核。
- 696 actual boot-args带headset=1，binary有mode1/2字符串，但当前HLOG未见headset:输出。这是**未验收/日志或调用链待核**，不能直接定性驱动没执行或PMIC不可达。
- 全0死总线可得约60%基线一致率，应看形状+镜像对+锚点；早先两份baseline MD5相同不是独立采样。
- 检测不自动开音频路由。SGEN六条只是参数，不是整条出声路径；不要混进只读基线。

### 构建

make-kernel可能底层exit2来自预存在kext symbolset/export检查。695 wrapper明确built，linked artifact哈希和诊断字符串都匹配，222个既有失败与旧构建相同。不能只凭raw exit2宣布没构建，也不能仅凭某个旧产物存在就认定新构建成功。

原trial695 state中的kernel_built:false是初始化状态未完成更新，已改true；现在696 review/proposal状态也已更新为“DSH完成、可见marker阴性、原厂恢复已报告核验”。没有进行新build。

## 12. 硬约束与协作通道

- 不碰Android spmi_access；SID5；禁写014b、禁RMW0240。PMIF只用1c804000+880..8a8内已经允许的方向/offset。
- 不加mali_gpueb=1、不直接读13c40000；未知门控MMIO不拿“只读”当不会挂机的保证。
- 所有poll都须有上界；不以watcher/长按电源作为默认取证恢复方案。phone已由DSH持有，非持有者只做枚举，不能adb shell/getvar/oem/flash。
- 刷前告知镜像完整哈希、预期、失败恢复；不能自动多刷。fastboot oem expdb有实测端点失效历史，不再试。
- 短消息经Orca终端，长报告/留痕写bridge。当前最近确认的DSH handle：`term_36937b00-9811-48c1-a1be-485090a65b7c`；本Codex：`term_ce8dadf1-f4ff-4f1b-a985-7b3d2dbafa1b`。
- **handle随Orca runtime重启变化，不是固定身份。** 老65478/53a1句柄已失效。用terminal list按agent/pane确认，再read screen；不要按窗口标题或凭记忆乱发。CLI：`/home/lizi/.config/orca/linux-orca-cli-shim/orca`。
- terminal send回执accepted:true仅证明接受输入，当前DSH provider不支持turn_started证明，不能声称已读，不能因静默重复发送。新命令用orca terminal send，不是orca send。
- 普通确认无需等对方回复，消息后继续自己授权的工作；重要结论同时留文件。

## 13. 节点与额度（仅本次观测，不作为永久配置规则）

DSH最新报告（13:45左右）：当前orca:2a3f54a2剩59.0%，16:49重置；system剩100%，18:52重置。本轮收工不是因为耗到5%。原用户要求额度到5%停手交接仍有效。Codex没有在收尾阶段重新调用账户接口确认这些私有额度。

DSH已处理网络，不需Codex继续改Clash：

- 韩国KR-HY2 Timeout，GitHub/kernel.org均000；
- 香港WAP-优化3延迟约34ms，GitHub/kernel.org可达，但ChatGPT后端403、HKG；
- 改台湾-优化2-GPT后GET codex/responses得到405；LA/英国-GPT也有可达记录，部分-GPT节点仍Timeout。

应保留的经验是**延迟或其它网站200不代表ChatGPT后端可用；本次普通节点被CF挡，换特定-GPT节点恢复**。不能推出所有普通节点永远失败、所有-GPT节点都能用，早先也出现过-GPT组全坏。GET405只能证明路由/方法检查可达，不能独立证明POST鉴权、配额与完整会话都正常。不要为此重启Clash或自动轮换账号；先区分链路、CF、鉴权和额度。

## 14. 关键证据索引与收工状态

构建树本轮主要目录：

- `logs/resume-sol-20261114-231152/trial-695-jobirq/`：695 image、state、只读补丁、历史开关审计、ISA/MMIO复核、待发布source diff。
- 同目录 `mcu-isa-mmio-audit.py/.json/.md`：独立从C array提取固件、按容器解析、Capstone5.0.7解入口、完整PPB扫描、上游对拍。
- `trial-696-witness/independent-verify.py`、`independent-verification/result.json`：本次最强构建复现证据。
- `trial-696-witness/CODEX-REVIEW-AND-HANDOFF.md`：刷前完整通知与退出边界；其中pending结果已被本文件和codex-review-state.json更新。
- `trial-695-jobirq/next-witness-nopdc-proposal.json`：已标为执行完696，**不再是待批准/held的未执行试验**；旧review正文是历史，可看头部更新说明。

发布树主要材料：原始695/696正文、696 tombstone、MCU/CSF-table/GPUEB文档、DSH1350handoff、bridge。文档仍需按本文件收窄其过强“已排除”列表，不以标题/commit message当硬件结论。

冻结PMIF/日志老工作详见 `docs/headset-wip/HANDOFF-CODEX-20261115-0132.md`；最初2259交接是历史背景，不能覆盖此后c9/6a与696的实际状态。

最后状态：**驱动源码未再修改；没有新增内核/镜像；没有新设备操作；没有GPUEB INIT；未安排下一轮刷机。** 已准备18行公开源同步补丁，交DSH发布；本交接主件与构建树镜像副本字节相同。后继先补证据/源码同步，再选择有因果解释力的单变量，不再从一个阴性读数推翻一整类前提。
