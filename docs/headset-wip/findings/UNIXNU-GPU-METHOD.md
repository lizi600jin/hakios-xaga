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
| **interconnect votes** | ⚠️ 有 `interconnect before the firmware` 日志，但**未核对是否足够** |
| RAM 保留 | ✅ 共享区 `0x7c600000` |
| scanout 布局 | ✅ 显示已通 |

**最值得注意的两项**：**中断**（我们一个都没注册）和 **interconnect votes**。

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
