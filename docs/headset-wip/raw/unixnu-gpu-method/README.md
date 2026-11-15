# UniXNU 的移植方法论（参考文本，非本仓代码）

> 2026-11-15 抓取，来源 <https://unixnu.com/>，项目 `woaphone/UniXNU`。
> 保存原因：这是目前唯一公开的「MTK SoC + XNU + GPU 加速」方法论，
> 虽然它做成的 GPU 是高通的 Adreno，流程与清单可跨 GPU 借鉴。

## 这些文件是什么

从 unixnu.com 抓取的正文文本（HTML 已剥标签）。**不是源码，是文档。**

| 文件 | 原页 | 价值 |
|---|---|---|
| `porting-gpu.txt` | `/porting/gpu` | **核心**：GPU 适配六步流程、硬件清单、内存规则 |
| `porting-gpu-diagnostics.txt` | `/porting/gpu-diagnostics` | 帧延迟捕获，与启动失败关系不大 |
| `porting-driver-development.txt` | `/porting/driver-development` | 驱动的仓库组织与接口约定 |
| `porting-log-capture.txt` | `/porting/log-capture` | **日志捕获原则**（见下） |
| `porting-power.txt` | `/porting/power` | 电源、OPP、**firmware votes**、时钟域 |
| `porting-bring-up.txt` | `/porting/bring-up` | 上电启动顺序 |
| `porting-platform.txt` | `/porting/platform` | 平台与内存 |
| `drivers-adreno740.txt` | `/drivers/adreno740` | Adreno740 实现示例（**高通，不是 Mali**） |

## 三条对我们直接有用的

### 1. GPU 适配顺序（`porting-gpu.txt`）

> Complete **platform DMA and basic display presentation** first.

**GPU 之前必须先有这两样。** 我们两样都有。

然后严格串行：匹配 GPU 世代与 ABI → 电源与固件输入 → 内存与命令提交
→ **有界工作量验证** → 调度与内存复用。

### 2. 「确认固件响应，再提交命令」（`porting-gpu.txt`）

> Confirm **GMU startup and HFI response** before submitting commands.

**这正是我们的卡点。** 我们卡在「固件没启动」，却一直在查命令提交相关的东西。
顺序是对的（先启动），但我们缺少「确认响应」这个独立判据。

### 3. 日志捕获要先做（`porting-log-capture.txt:56`）

> **Set up log capture as you bring up the board.** Start with firmware logs,
> collect kernel evidence at handoff, then add persistent logs and automated
> reboot cycles.

**这正是我们花了六次刷机才学到的教训**：内核 msgbuf 只有 128 KiB，
而 `headset` 初始化在 19.97 秒、窗口只保留最近约 24 秒，输出一直被吞掉。
把它提到 1 MiB 之后立刻就读到了 129 项寄存器。

**如果一开始就按这条原则做，前面几轮能省下大量时间。**

## 重要的边界

- **它做成的 GPU 是高通 Adreno740（SM8550）**，不是 MTK Mali。
  `porting-gpu.txt` 明确写：换 GPU 世代必须先实现对应的 kernel backend 和编译器支持。
  **Adreno 的做法不能照搬到 Mali。**
- 它的**硬件清单**可跨 GPU 用（见 `porting-gpu.txt` 第 1 步）：
  GPU/GMU 寄存器、中断、SMMU 上下文与 stream ID、固件、电源域、时钟/电压、
  **interconnect votes**、RAM 保留、scanout 布局。
- 这份文档**没有** Mali 或 panthor 的任何内容。
