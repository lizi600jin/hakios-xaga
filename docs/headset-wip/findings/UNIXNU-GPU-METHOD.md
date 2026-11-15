# UniXNU 的 GPU 移植方法论（Adreno740 为例，可跨 GPU 复用）

> 2026-11-15 17:00，DSH。**线上核实**。
> 来源：<https://unixnu.com/porting/gpu>（GPU and Metal adaptation）
> 项目：`woaphone/UniXNU`，成员含 `mytiantian001`、`Yukino1314520`、`Eason206`、`limibeer0`
> （组织 `yukino-xnu`，公开仓库 0 个，`yukino-xnu` 本体是私有的）

---

## 0. 一句话

**UniXNU 是目前唯一有「GPU 驱动移植方法论」的 MTK/XNU 项目 —— 但它做成的是高通的 Adreno，
不是 MTK 的 Mali。它明确写了「换 GPU 世代要先实现对应的 kernel backend」。**

---

## 1. 它做到了什么

`UniXNU` README 原文：

> **🎉 News:** Adreno740 and AdrenoMetal provide **GPU acceleration on sheng**
> (Xiaomi Pad 6S Pro, SM8550).

**运行时路径：**

```
Metal → AdrenoMetal → metal2vulkan → Turnip → Darwin KMD → Adreno740 → A740
```

**这是完整的一条 GPU 加速链**，从 Metal 一直到硬件。

## 2. 但它不是 Mali

`/porting/gpu` 里明确写着：

> **For another GPU generation, implement the matching kernel backend and compiler
> support before selecting its frontend.**

**含义：换 GPU 世代 = 重写 kernel backend + 编译器支持。**
Mali 与 Adreno 是两套完全不同的硬件接口，**不能照搬**。

---

## 3. 它的移植流程（可借鉴的部分）

文档给的顺序是**严格串行**的：

| # | 阶段 | 关键内容 |
|---|---|---|
| 0 | **先完成平台 DMA 与基础显示** | **GPU 之前必须先有这两样** |
| 1 | 匹配 GPU 世代与 OS ABI | 寄存器、固件、packet、内存接口逐项对比 |
| 2 | 适配电源与固件输入 | **OPP、带宽策略、固件三件套** |
| 3 | 建立内存与命令提交 | SMMU 上下文、stream ID、地址宽度、cache 维护 |
| 4 | 有界工作量验证 | **渲染一张已知图像并检查结果，再上 UI 负载** |
| 5 | 调度与内存复用 | 优先级、ring、fence |

### 3.1 对我们的直接启示

**第 2 步「确认 GMU 启动和 HFI 响应，再提交命令」** —— 这正是我们的卡点。

它的做法是：**先把固件启动与响应确认掉，才碰命令提交。**

**我们现在的状态**：固件没启动（没有 version word、没有 GLOBAL_IF），
却已经在查命令提交相关的一切。**顺序上我们是对的（先启动），但没有「确认响应」这个独立环节。**

### 3.2 它的验证判据值得抄

> **Expected result: commands complete, fences advance and the known image is correct.**
> **Record GPU results independently from desktop presentation.**

**两条：**
1. **用一个已知图像做验证**，不靠 UI；
2. **GPU 结果与显示结果分开记录** —— 显示出来了不等于 GPU 工作了。

**我们的 witness 实验其实已经在做第 1 条**（写 marker 而不是跑固件）。

---

## 4. 它列的硬件清单（第 1 步，可直接当我们的检查表）

> collect **GPU/GMU registers, interrupts, SMMU context and stream IDs, firmware,
> power domains, clock/voltage levels, interconnect votes, RAM reservations and
> scanout layout**. Check them against the selected firmware.

**逐项对照我们的状态：**

| 项 | 我们的状态 |
|---|---|
| GPU 寄存器 | ✅ 已知（`0x13000000`，与主线 DTS 一致） |
| **中断** | ❌ **我们完全没有用中断，纯轮询** |
| SMMU 上下文 / stream ID | ⚠️ 有 AS0 页表，但**没有 stream ID 概念** |
| 固件 | ✅ 与厂商字节一致 |
| 电源域 | ✅ `0xF3C = 0x3ffc` 全 up |
| 时钟 / 电压 | ✅ 已核对（stacks = `mainpll_d5_d2`） |
| **interconnect votes** | ✅ **已核，见下方修正** |
| `iommus`（SMMU stream ID） | ⚠️ GPU 节点**没有** `iommus` 属性，见下方修正 |
| RAM 保留 | ✅ 共享区 `0x7c600000` |
| scanout 布局 | ✅ 显示已通 |

### 🔴 修正（6-Astra 2026-11-15 复核，我独立复算确认）

**我原来把「interconnect votes 缺」列成我们的缺口。这是错的。**

#### 修正一：那组日志不是 bandwidth vote

`mfg_master_gates()` 操作的 `MFG_GLOBAL_CON`（`0x13fbf0b0`）是 **MFG 时钟/DCM/握手配置**，
**没有 avg_bw/peak_bw 单位**，不存在「把 global 数字调大 = 带宽更高」的关系。

**五组读回用掩码核对，全部通过**（我独立复算，5/5 一致）：

| 项 | 掩码 | 读回 & 掩码 | 期望 | 结果 |
|---|---|---|---|---|
| `DCM_CON_0` | `0x807f` | `0x803f` | `0x803f` | ✅ |
| `ASYNC_CON` | `0x3c00000` | `0x3c00000` | `0x3c00000` | ✅ |
| `ASYNC_CON_1` | `0x1` | `0x1` | `0x1` | ✅ |
| `GLOBAL_CON` | `0x266500` | `0x66000` | `0x66000` | ✅ |
| `QCHANNEL_CON` | `0x10` | `0x10` | `0x10` | ✅ |

`global` 的精确变换也对上了：
`(0x03281701 & ~0x00200500) | 0x00066000 = 0x030e7201`，与日志完全相等。

#### 修正二：主线 GPU 节点根本没有 `interconnects`

`mt6895.dtsi:2734-2768` 的 GPU 节点**没有** `interconnects`/`interconnect-names`；
对 `drivers/gpu/drm/panthor` 与 `drivers/gpu/mediatek/gpueb` 搜索
**没有 `icc_set_bw`/`of_icc_get`** 这类请求调用。

**所以不能从 Adreno 的清单反推出「这份 MT6895 启动路径必缺一笔 ICC 带宽请求」。**

#### 修正三：SMMU stream ID 也不适用

GPU 节点**没有 `iommus` 属性** —— Panthor 用的是**GPU 内部 MMU**（`0x2000` 块的
`AS_TRANSTAB` 那套），不是平台 SMMU。

**不能因为我们没有 Adreno 式的 stream-ID 配置就认定缺步骤。**

#### 修正四：`mali_gpueb=1` 不能被当成「试一次 GPUEB 上电」

它只让 `:4303-4304` 映射窗口，随后 `:2333-2362` 读 watchdog/IRQ/GPR 探针；
**不发上电 IPI、不建立共享区、不打开已确认的访问前提。**

**所以：中断注册也不该列为首要修复** —— `RAW/STATUS` 都是 0 时，
缺 CPU IRQ handler 解释不了设备侧全零。

---

## 5. 它的内存规则（细节，可对照我们的页表）

> **Allocation** BOs come from **system memory**: a pageable `IOBufferMemoryDescriptor`
> with no task and the **write-combined cache mode**, wired by `prepare()`.
> **The VM sets the page attribute in one batch and restores it on free.**
> Only a caller that asks for contiguous memory gets a contiguous kernel allocation.

**我们的 `mem_alloc_low()` 用的正是 `MALI_WIMG_WCOMB`** —— 与这条一致。

> **Mapping handles are indexed**, and unmap invalidates the affected translations.

**它的 unmap 有明确的 TLB 策略**（512 页以内逐页 `TLBIVAL`，更大则 invalidate ASID）。
**我们的驱动在 unmap 上的处理需要核对。**

---

## 6. 证据边界

| 已核实 | 未核实 |
|---|---|
| UniXNU 有 GPU 加速（Adreno740，SM8550） | **是否有 Mali/MTK 的 GPU 实现** |
| 组织 `yukino-xnu` 存在，成员 6 人 | 组织里的私有仓库内容 |
| 方法论文档完整可读 | 这套方法在 Mali 上是否适用 |

**关键判断：UniXNU 证明「MTK SoC + XNU + GPU 加速」这条路走得通，但它的 GPU 是高通的。
Mali 这一侧，全世界仍然没有公开先例。**
