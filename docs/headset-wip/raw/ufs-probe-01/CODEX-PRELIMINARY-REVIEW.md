# UFS 候选初审与接续点（2026-11-16）

Codex。读取了CPU交接、UFS候选及其部分模型、指定Linux提交的相关函数、XNU映射实现和现有loader相关代码。
额度自查仍为3%，尚未恢复；本次没有编译、测试、打包、刷写或设备操作。
这是初审，不是候选通过或允许设备试验的结论。没有重新计算候选hash或核验84970a9归档。
用户报告候选hash为35c9c297a0df1f49736bbe158ea10b8377789676faa11c86602c2c4d8e2f6929；后续核对当前文件身份再修改。

## 结论与推进顺序

先完成已构建CPU713的打包审查与单次只读试验；UFS继续主机修正与审查，暂不接入或刷写。
理由：CPU内核及输入已独立核验，UFS候选仍有下面的诊断可靠性问题。
不把两个首次硬件探测合并成一轮，不重建或覆盖CPU713冻结树。
此处只是额度恢复后的建议；本次没有发出PACK或DEVICE CMD。

## 候选需要修正/补核

1. 当前读到的入口声明 struct ufs_snapshot first, second，未初始化。
   ufs_observe读失败时可能只填了一部分甚至完全没有填，之后入口仍打印second及两份原始字。
   需要初始化输出，最好同时记录有效字/失败位置，未读字标为未采集，不能用零值冒充已读硬件零。
   ASan/UBSan通过不证明没有未初始化读取，且入口模型的日志替身没有格式化这些参数。
2. 当前判定允许九个字全为0xffffffff时进入OBSERVED：HCE、DP、UTRLRDY均为1。
   应区分“捕获了快照”与“可信控制器状态”，加入明显无效响应/身份一致性处理及负对照。
   未知VER保留原始值，不要随意宣称支持或把不熟悉版本一概当硬件坏。
3. read回调接口没有write方法，只能证明接口没有暴露写操作，不能在类型层面保证回调实现不写内存/MMIO。
   零控制器写需要审查实际实现、调用链和适当模型；真实MMIO data abort也不会自动变成bool false。
4. 双快照不同是UNSTABLE观察，不是设备故障。IS等状态可能自行变化，不要据此写“链路坏”。
   CAP.NUTRS是transfer request slots，UTRL不是task management request list；注释中的task应改正。

## 对五个问题的初步判断

1. 九个寄存器足够做首次状态快照。暂不加UICCMD/UICARG1；它们是命令上下文，不能替代电源、时钟或链路证据。
   UCRDY仅报告合理；实际接管发命令前还要处理门铃、run/stop、队列所有权和DMA，OBSERVED不能作为直接发SCSI的许可。
2. HCE=0不能单独归因时钟。先比对loader完成初始化/跳XNU前与XNU探针时刻的同组寄存器，
   再追现有clock/power控制及loader行为。CAP/VER不可信时，HCE本身也不能当有效状态。
3. 先只读确认继承状态，再决定接管还是补clock/power。当前两条写入路线都没有批准；不盲目开clock，也不直接复用旧链表发SCSI。
4. 当前XNU machine_routines.c:1645附近的ml_io_map明确调用io_map(..., VM_WIMG_IO, VM_PROT_DEFAULT, false)。
   首次寄存器读使用该IO映射，没有证据要求额外自选属性；不要用wcomb或普通可缓存DRAM映射。
   这不保证物理总线读一定成功，也不解决以后DMA缓冲区一致性问题。
5. 首轮不同时读ufscfg_ao 0x112b8000或ufscfg_pdn 0x112bc000。
   仅有syscon基址不等于知道安全偏移、字段意义或该块当前可读；需要先建立来源和调用链。

## Linux结论应收窄

本地 /tmp/mt6895-mainline HEAD 已读取为25fd36e67958d96086b8542340477948ab40c2a2。
指定节点未提供phys/reset、相关接口容许NULL，支持“这份配置不靠这些可选句柄执行PHY/reset操作”。
不能由此推出整个驱动只做三件事、完全没有reset或仅被动接管。

同提交ufs-mediatek.c仍包含：
- ufs_mtk_setup_clocks -> ufs_mtk_pwr_ctrl -> ref-clock处理；
- hce_enable_notify中的variant处理；
- pre_link/post_link及DME设置；
- 单独的ufs_mtk_device_reset，通过平台控制路径复位设备，不依赖上述reset_control句柄。
这些分支在xaga上的可达性及capabilities需要逐项追踪，不能仅因函数存在就断言本次都执行。

源码链接：
https://github.com/MT6895-Mainline/linux/blob/25fd36e67958d96086b8542340477948ab40c2a2/drivers/ufs/host/ufs-mediatek.c

此外这是MT6895-Mainline的移植分支，不应把其特定设备行为笼统称作全部upstream Linux已证明。

## 现有loader不可漏掉

P/mtk/loader/mtkloader.c约4180行打印LK状态，随后代码已有taking the list over、
fDeviceInit、LUN选择和NOP相关流程。board.h也描述了自有UFS接管实现。
下一轮要追完整调用图、时序和已有日志，明确XNU继承的是哪一阶段状态；不能把路径简化成LK直接交给XNU。
本次没有完整追完loader初始化调用图，也没有确认中间所有clock/power写入者。

## 额度恢复后第一步

读取本文件和CPU交接，核验UFS候选当前hash、84970a9提交及相关审查文档。
修正以上诊断问题并做有意义的失败路径/无效响应测试，再完成真实XNU头文件的对象编译与独立审查。
CPU713沿用现有a80231ed内核，未打包未刷写的状态不变。
保持DSH独占设备、原厂回退、禁止覆盖旧树/BUILD、不stash/reset/checkout/clean/gc/prune、
不碰GPUEB/OEM expdb、不启用watchdog、不扩大硬件寄存器探测范围。
