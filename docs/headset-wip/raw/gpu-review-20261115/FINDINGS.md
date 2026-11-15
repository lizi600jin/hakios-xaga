# GPU 三条外部线索：Codex 独立复核，2026-11-15

范围：回答 DSH 的 (a) 启动阶段、(b) interconnect votes、(c) 可用线索。本轮只读核查代码和既有日志；未改生产代码、未构建、未操作设备。

已读 1513 Codex 交接、1700 DSH 代写交接、POST696 的相关边界、协作协议和本次用户修正。1655 文件不存在。以本次用户说明为准：699/700 使用隔离构建的内核，不含当前工作区备用诊断打印；不能把源码中出现的打印当成已实跑。

证据基准：

- 构建树 `/home/lizi/IOS/hakios-mtk`：`trial/jobirq-695`，HEAD `6a51ef21564085678dc2a4fee847e56a026e3757`，既有 5 个修改保留。
- 所称“主线”具体是 `MT6895-Mainline/linux` 的 MT6895 移植分支，不是说所有相关补丁均已合入 Torvalds 上游。
- `/tmp/mt6895-mainline`：HEAD `25fd36e67958d96086b8542340477948ab40c2a2`、工作区干净。本轮 `git ls-remote` 核对远端 `7.2-mt6895-xiaomi-xaga` 同为该 SHA。下述上游行号以此本地源码为准（网页渲染会折叠空行）。
- 原始样本：`/home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip/raw/trial700-logbook/trial700-full.log`。

## (a) 能定位到首次可观察握手之前，不能锁定第一条取指

【源码】`panthor_fw.c:1545,1549,1553` 的正常初始化顺序是 `panthor_fw_start()` 成功、`panthor_fw_init_ifaces()` 成功，才调用 `panthor_fw_init_global_iface()`。后者在 `:1088` 排程 ping。启动函数在 `:1145-1150` 等待 booted，并在超时后复查 `INT_STAT & JOB_INT_GLOBAL_IF`；IRQ handler 在 `:1102-1103` 依据该事件置 booted。

因此，作者所述 `panthor_fw_ping_work()` 处 SError 对应的正常路径已经越过首次启动判据。它支持“我们未达到他们出错时的阶段”，不支持把我们直接定案为“第一条取指没发生”。SError 的报告点本身也不能精确定位最初出问题的总线访问。

【既有实测，本轮重读】700 在 `:3510-3513,3545,3657,3739` 的 JOB RAW/STATUS 均为 0、MASK 为 `0xffffffff`；`:3825` MCU status=1；`:3830` version 仍为哨兵 `0xff5aa5f0`。准确描述为：未观察到首次 GLOBAL_IF 或有效 version 发布。取指、初期固件初始化/等待、写回路径和 CPU 可见性仍需分别辨别。

【边界】POST696 已明确：witness 阴性只说明没有观察到完成的 marker store，不是指令追踪。MMU_IRQ 高 16 位 REQ_COMPLETE 是地址空间命令完成事件，不是正常取指或 load/store 的计数。

## 中断：应有完整驱动支持，但不是当前已证明的阻塞点

【源码】主机 GIC 接收、GPU 事件 RAWSTAT、GPU INT_MASK/INT_STAT 是不同环节。当前纯轮询已读取设备侧状态。只缺 CPU IRQ handler，不能直接解释 RAWSTAT 也为 0。Panthor 启动超时后的 INT_STAT 复查本身也说明该事件可被轮询观察。

建议：保留后续实现 GPU/job/MMU IRQ 的工程任务；在当前证据下，不把“注册中断”排成一次首要修复试刷。若未来 RAW/STAT 非零而 CPU 不接收，再针对 GIC 路由查证。设备侧清位顺序或漏处理故障仍可另行审计，不能用这句话排除所有中断相关逻辑。

## (b) 已有 interconnect 日志不是 bandwidth vote

【源码】`hakios.mali610.c:2479-2552` 的 `mfg_master_gates()` 操作 MFG 时钟/DCM/握手配置；`global` 是 `MFG_GLOBAL_CON`，地址 `0x13fbf0b0`。厂商 `gpufreq_mt6895.c:3112-3159` 的 `__gpufreq_hw_dcm_control()` 给出位名和相同 global 写法。移植分支 `clk-mt6895-mfgcfg.c:79-114` 也复制了此流程。

这组值没有 avg_bw/peak_bw 单位，也不存在“把整个 global 数字调大就是带宽更高”的关系。Linux ICC 的 bandwidth 请求是对明确路径提出 avg/peak 等资源要求，见 [Linux interconnect 文档](https://docs.kernel.org/driver-api/interconnect.html)。

700 的配置读回可以按下面的掩码检查，而不是按整数大小比较：

| 项 | 检查 | 700 的读回 | 能证明什么 |
|---|---|---|---|
| DCM_CON_0 | `(v & 0x807f) == 0x803f` | `0xc03f`，符合 | debounce/DCM 配置位读回 |
| ASYNC_CON | `(v & 0x03c00000) == 0x03c00000` | `0x03c00000`，符合 | 我们要求设置的四位读回 |
| ASYNC_CON_1 | `(v & 1) == 1` | `0xf7`，符合 | FAXI_SOC_IN_EN 位读回 |
| GLOBAL_CON | `(v & 0x00266500) == 0x00066000` | `0x030e7201`，符合 | 下表七个位的配置读回 |
| QCHANNEL_CON | `(v & 0x10) == 0x10` | `0x10`，符合 | QCHANNEL_EN 位读回 |

GLOBAL_CON 七个位：

| 位 | 厂商名称 | 期望 |
|---|---|---:|
| 8 | GPU_SOCIF_MST_FREE_RUN | 0 |
| 10 | GPU_CLK_FREE_RUN | 0 |
| 13、14、17、18 | 对应 m0/m1 GALS slpprot_idle_en | 1 |
| 21 | dvfs_hint_cg_en | 0 |

本轮实际运算核对：

```text
clear_mask = 0x00200500
set_mask   = 0x00066000
(0x03281701 & ~clear_mask) | set_mask = 0x030e7201
```

与日志完全相等。此处配置写入/读回没有发现错误；不能由此证明真实总线流量、所有门控状态或 DDR 服务水平。

两处日志解释陷阱：

1. `global` 使用保存过的 `glob_before`，是真正的前后值。`dcm/async/async1/qchannel` 的左边却是已经修改的局部变量，即“拟写值→读回值”，不能证明写前已经相同。
2. 默认 `mali_rpc=0` 时 `rpc 0->0` 的两个 0 都不能当作实际寄存器读取；`:2508-2510,2552` 明确跳过读取。移植分支会访问 `0x13f91034`，我们默认避开，有历史挂机记录；这是一项尚未建立等价性的差异，不据此重新读危险地址。

【语义边界】MEM0/1_MST_CG_ENABLE 位名及写法来自另一型号 mt6873；mt6895 厂商 HWDCM 函数只显式设置 bit23/25。可以说我们没有漏写当前实现要求的 bit22/24，但不能仅靠跨型号注释证明“厂商漏了两个门”。CG/DCM enable 配置也不等于对运行时所有时钟的直接测量。

### 真正的 vote 路径查到什么

【源码】固定 SHA 下，`mt6895.dtsi:2734-2768` 的 GPU 节点没有 `interconnects`/`interconnect-names`；对该树 `drivers/gpu/drm/panthor` 与 `drivers/gpu/mediatek/gpueb` 搜索没有 `icc_set_bw`/`of_icc_get` 这类请求调用。不能从 Adreno 清单反推出这份 MT6895 启动路径必缺一笔 ICC 带宽请求。

【源码】厂商 MT6895 AP 侧确有另一种 vote：`gpufreq_mt6895.c:3603-3604` 在上电时用 `regulator_set_voltage(reg_dvfsrc, cur_volt * 10, INT_MAX)` 请求 VGPU/VCORE 电压，关电时 `:3628-3629` 请求 level 0；头文件 `gpufreq_mt6895.h:29` 启用该配置。这是电压请求，不是上述 MFG_GLOBAL_CON，也不是 DDR avg/peak 带宽。GPUEB 模式下还须追实际命令路径，不能把 AP 分支一定执行当成事实。

【源码】厂商 `ged_gpu_bm.c:54-90` 有经 SSPM 共享区建立 GPU QoS 数据与帧/作业更新的路径，也不同于 MFG_GLOBAL_CON。当前没有取得证据证明它是首次 MCU 启动所需的非零带宽门槛。

判断：值得做平台路径审计；这次已经完成“这些日志究竟是什么、直接 ICC 调用是否存在”的最小审计。没有依据给出“至少 X MB/s”或建议盲写新 vote。进一步核查应取得实际请求方、请求字段/单位、提供方的生效状态、同 OPP 下的成功基线；不能只读一个名字相近的寄存器。

## (c) 更有价值的是比较成功平台的完整启动责任链

### 1. GPUEB 上电握手是源码中明确存在的差异

【源码】该移植分支 `arch/arm64/configs/xaga.config:114` 启用 `CONFIG_MTK_GPUEB`。`panthor_device.c:257-268` 在硬件初始化前调用 `mt6895_gpueb_power_on()`；`panthor_fw.c:1131-1142` 在写 MCU AUTO 前再次走这个接口。调用受配置和 GPUEB 节点存在条件控制，不能脱离条件说每个构建都执行。

【源码】`gpueb_power.c:70-113` 首次建立共享内存绑定，发 `CMD_INIT_SHARED_MEM`；`:162-196` 发 `CMD_POWER_CONTROL`，使用 `IPI_SEND_POLLING` 并检查返回。这说明 GPUEB 这项协议不要求 CPU 中断接收才能完成。`panthor_devfreq.c` 的 GPUEB 分支还通过 OPP 索引请求频率/电压联动，不能只比几个最终时钟寄存器就视为同一状态机。

【源码】我们当前 GPU 文件及 loader 搜索未发现上述 INIT/POWER IPI 实现；当前流程有 AP 直接上电、时钟、PDC 等操作。因此“同一 SoC 的电源设计归 GPUEB”成立，不等于“两份软件已经采用相同的启动流程”。这是已有交接中的未决差异，本轮重新核实，不能把它包装成刚发现或已证根因。

尤其 `mali_gpueb=1` 目前仅让 `hakios.mali610.c:4303-4304` 映射窗口，随后 `:2333-2362` 读 watchdog/IRQ/GPR 等探针；不发上电 IPI、不建立共享区、不打开已确认的访问前提。**不要把此参数作为“试一次 GPUEB 上电”的实现。**

接下来的离线工作应针对访问前提、实际保留内存契约、传输与业务 ACK、完整失败/恢复边界建立对照。POST696 已指出 `0x7c600000` 不是未经核对即可视为当前固件新鲜状态的固定锚点。本轮未读该区、未发 INIT、未新增设备探针。

### 2. 把内存链路对照到精确对象

比较 reset-vector 页、reset-PC 指令页、栈页、GLB shared 页的实际 backing PA、GPU PTE、CPU 映射属性及缓存维护；把它们与成功路径的同一阶段关联。已有 witness 和软件页表检查是证据的一部分，不能变成“所有 DMA/可见性已排除”。

UniXNU 的 SMMU stream ID 属于对应 Adreno 平台。此 MT6895 GPU 节点也没有 `iommus` 属性，Panthor 使用 GPU 内部 MMU；不能因为我们没有 Adreno 式 stream-ID 配置就认定缺步骤。CPU 都使用 write-combined 也不能单独证明 DMA 可见性等价。

### 3. 另外两条线索的使用阶段

- `panvk-mtk-driver` 的 AFBC modifier、buffer 布局和 kick 竞态修复，在固件启动且能提交任务后有参考价值；它依赖现成 kbase，不能补当前 MCU 启动。参见 [该项目 README](https://github.com/mytiantian001/panvk-mtk-driver)。
- [UniXNU GPU 移植页](https://unixnu.com/porting/gpu) 的“有界任务完成、fence 前进、结果正确”可用于将来的验收。Adreno740/SM8550 的成功是高通平台经验，不能写成 MTK+XNU GPU 已验证；未找到公开 Mali 实例，也不等于证明世界上不存在。
- 移植分支的源码和故障注释能给出可检验的流程。若要把它当作硬件正向对照，还需要匹配 commit/config/固件哈希的实际成功日志；本轮没有在设备上重现它的成功。

## 建议优先级

1. 文档把 interconnect 日志归到 MFG HWDCM/握手配置；把“vote 是否足够”留作尚未找到具体请求对象的待证问题。上述掩码核查已完成。
2. 继续同型号启动路径差分，重点是实际 GPUEB/电源所有者流程和内存可见性；先填完前置条件与成功基线，不直接试 `mali_gpueb=1`。
3. 中断注册作为驱动后续完整性工作。当前 RAW/STATUS 阴性没有给它首要修复优先级。
4. 耳机 701 仍按 700 同内核和既定判据等待实体耳机；本轮没有改变其准备状态。

固定版本的一手源码链接：

- [Panthor 启动与 ping](https://github.com/MT6895-Mainline/linux/blob/25fd36e67958d96086b8542340477948ab40c2a2/drivers/gpu/drm/panthor/panthor_fw.c)
- [GPU DTS](https://github.com/MT6895-Mainline/linux/blob/25fd36e67958d96086b8542340477948ab40c2a2/arch/arm64/boot/dts/mediatek/mt6895.dtsi)
- [MFG 时钟/DCM 配置](https://github.com/MT6895-Mainline/linux/blob/25fd36e67958d96086b8542340477948ab40c2a2/drivers/clk/mediatek/clk-mt6895-mfgcfg.c)
- [Panthor 平台初始化](https://github.com/MT6895-Mainline/linux/blob/25fd36e67958d96086b8542340477948ab40c2a2/drivers/gpu/drm/panthor/panthor_device.c)
- [GPUEB 握手](https://github.com/MT6895-Mainline/linux/blob/25fd36e67958d96086b8542340477948ab40c2a2/drivers/gpu/mediatek/gpueb/gpueb_power.c)
