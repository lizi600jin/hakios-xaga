# 上游 Metal、ramoops 与 MT6768 GPU 对照

2026-11-15，Codex。只读研究；未修改 GPU、loader、设备或 DSH 文档仓。

## 结论与优先级

耳机关闭/恢复复测之后继续 DAPM 供电/时钟依赖，再处理 MT6368 数字接口。Metal 枚举不是绕过 CSF 的硬件加速路线。ramoops 可作为独立的取证改进，在重新做可能挂死的 GPUEB 实验之前评估，不立即替换已经可用的 logbook。

## 来源与版本

DSH 提供的 metal.md、air.md、gpu-probe.sh、redwood_mali.c 位于 docs/headset-wip/raw/hakios-upstream/。

本轮另以 git ls-remote 核实上游 mtk 分支为 a962798418fa1ebaa06554a69955803860907c6b，并下载该提交的 loader、mtkdbg、probe 脚本和 GPU 源码。URL、完整 SHA256、大小在 upstream-manifest.json。GPU 源码 SHA256 25a61769a96ec0fc2d898c8b670394c6902708da83eb25d4fa2ea3b834ad681b，与 DSH 提供的 42131 字节文件相同。

本机 /tmp/hakios-mt6768 是较旧的 ad65e1f...，其 redwood_mali.c 哈希不同，因此最终 loader 结论以新下载的固定提交为准。MT6895 对照采用 clean705-files/bsd/dev/hakios.mali610.c（b6bf3483...），没有把当前脏树的 GPUEB 集成当作已上机基线。

## (a) Metal

metal.md:23-60 描述的是该作者对 iOS 16.5 / Metal 306.6.4 的私有设备发现与加载路径。属性和 bundle 让框架找到并实例化插件，并不实现命令队列、资源、同步、着色器编译或 GPU 提交。

同一文档 :738 起明确列出三个子系统：AIR 编译器、Metal 设备对象层、内核提交 ABI；air.md:7-9 明说后两者尚未开始。这些是该版本文档的陈述，不能泛化为任何后续提交的进度。可以实现只供枚举测试的 stub，但它没有实际渲染能力；也不能因为 MTLCreateSystemDefaultDevice 返回非 nil 就让整个系统进入未完成的驱动。完整软件 Metal 后端理论上可以不使用 Mali，但那是另一项驱动/渲染器工程，不是两个属性的改动。

若任务最终要在 G610 上执行，仍需可用的 CSF 后端；后端可由原厂 kbase 或自有内核提供，用户态插件本身不能取代它。现有 XNU 环境没有一个已证实可复用的工作后端。本条不提高 Metal 枚举的优先级。

“空列表时静默 nil”解释一种可能路径，不能独立证明本机当前无错误信息正是该原因；还需本机服务属性和插件加载取证。此轮没有逆向本机 Metal 二进制。

## (b) ramoops

固定提交 mtkloader.c:3407-3416 的 GPU_PROBE 分支直接调用 loader 内 gpu_probe()，然后 chain_android()，不调用 run_xnu()。因此 gpu-probe.sh 不能作为 XNU 日志持久化已实现的直接证据。

pmsg_take() (:2982) 消费并抹掉单次请求，只说明下一次走到 loader 时不再次执行探针。入口 :3382 调用 wdt_disable()，GPU_PROBE 分支未启动 XNU 的第二核 watcher。脚本的“只损失一次重启”不等于挂死后一定自动复位；软件超时也不能打断一笔已挂死的 MMIO。

上游 XNU 路径另外有返回时保存、重启后救日志，以及第二核 watcher (:1751) 采样再写 ramoops 的机制。我们 loader 也已有 watcher 和 logbook 救援实现；不能把“换存储格式”当作首次获得独立观察能力。更可靠的下一项是核实在风险操作之前记录进度、缓存清理、保留区和复位路径是否确实可用。

console-ramoops-0 不存在，不足以推出 console 区未注册。所读 Linux fs/pstore/ram.c:140 在没有 old_size 时直接不返回记录，即使区域对象存在也可能无文件。这里只说明一种反例，未判定本机的具体原因。Linux 官方文档也要求保留 RAM 跨重启存活：https://docs.kernel.org/admin-guide/ramoops.html 。

不能使用 README 中的 base+pmsg_size 计算 console 地址。分区顺序/record_size/ftrace/对齐/ECC/驱动版本共同决定实际布局。当前 board.h:214 定义 console=0x4811f000、pmsg=0x48160000；这只是现有板级常量，不是本轮重新实测。board.h:908 明确 BOARD_NO_PLOG=1，mtkdbg.h:274 使写入函数为空。附近注释记录曾在写入时停住，但注释不是本轮原始证据；不能直接撤掉该开关。

建议先只读取得当前 Android 的实际 ramoops 参数、reserved-memory/iomem、record/ftrace/ECC 及旧失败记录，再设计独立、最小保留标记实验。验证成功后可以增加该日志通路，保留 logbook 对照；与耳机供电实验分开。

## (c) 前四步并非完全相同

redwood_mali.c:283 的共同顺序是 PWR_ON/ON_2ND -> 确认 -> CLK_DIS 清零 -> ISO 清零 -> RST_B -> SRAM_PDN 清零并等待。MT6895 的 domain_on() :1508 基本顺序相同，但处理失败的语义不同。

| 项目 | MT6768 上游 | MT6895 705 基线 |
|---|---|---|
| 电源拓扑/状态 | 四个域；全局 0x180/184 确认；ASYNC 无 SRAM 步骤 | 十二域及可选 PDC；域控制字状态与 MT6895 专有状态寄存器 |
| 超时 | domain_on 返回 false，mali_up 立即退出 | :1607 打印 no ack 后继续，SRAM 检查失败后函数仍返回 true；上层主要拦截 MFG1 未起 |
| 时钟顺序 | :522 先只写 topckgen 选择 syspll_d3，域起来后才访问 mfgcfg/BG3D | :4370 先域，:4411 clock_26m 同时操作 topckgen 与域内 mfgcfg；后续 PLL 与独立 stacks mux |
| 总线保护 | MFG 起后清 Infracfg 的几个位 | MD0/EMISYS/RPC 等 MT6895 专有路径，域前和域后均有处理 |
| GPU 内部 | reset 后显式打开 L2/tiler/shader，再走 Bifrost job slots | L2 与 CSF 固件启动路径；不能套用 G52 的无固件 job-slot 提交 |

所以不能称“SPM 序列完全一致”，但也不能据此把上游数值或整个 clock_26m() 提前搬过来。特别是我们 clock_26m() 还写域内 mfgcfg，上游提前执行的是 topckgen 部分，两个动作不等价。

705 原始日志 :3246 等有 no ack 后继续；:3645 又读到部分域已为 c000000d。因此早期 timeout 不能直接等同固件启动时域仍未起，也不能凭 GPU_ID/L2 ready 宣称全部路径已证实。该差异支持将来整理 MT6895 的最终状态判据，但还没有形成一个由 MT6768 对照支持、能解释 GLOBAL_IF 缺失的新单变量修复。本轮不安排照搬上游时钟/SPM 的刷机实验。

另：旧 GPU 源码中的 Cortex-M7、无 GLOBAL_IF 即未取指等历史注释不能当硬件事实；本对照只引用实际分支、写入和读回行为。
