# `GPU_COHERENCY_PROTOCOL (0x304)` 从未写入：上游必做、我们跳过

> 2026-11-15 14:30，DSH。**离线源码核对**，未刷机。
> 这条来自三个开发者仓库扫描的间接提示，我逐层核实到 panthor 源码。

---

## 0. 一句话

上游 panthor **在 L2 上电之前**必定写 `GPU_COHERENCY_PROTOCOL (0x304)`，
而我们的驱动在 trial 695/696 的参数下**一次都没写**，硬件保持默认 `ACE_LITE(0)`，
而 panthor 在这块非 coherent 的 SoC 上会写 **`NONE(31)`**。

---

## 1. 我们这边：条件恒假

`hakios.mali610.c:3172-3179`：

```c
uint32_t before = mrd(kCoherencyProtocol);

if (!gMaliCold || gMaliCohNone) {
	mwr(kCoherencyProtocol, kCoherencyNone);
}
printf("mali: protocol trial %u: features %#x before %#x after %#x wrote %u\n",
    gMaliTrial, mrd(kCoherencyFeatures), before, mrd(kCoherencyProtocol),
    (!gMaliCold || gMaliCohNone) ? 1u : 0u);
```

### 参数状态

| 变量 | 声明 | 默认 | boot-arg | 695/696 实值 |
|---|---|---|---|---|
| `gMaliCold` | — | — | `mali_cold` | **1** |
| `gMaliCohNone` | `:525` | **0** | `mali_cohnone`（`:5198`） | **0**（未传） |

所以：

- `!gMaliCold` = `!1` = **false**
- `gMaliCohNone` = **false**
- 条件 **`false || false` = false** → `mwr` 不执行

### 真机日志实锤

```
[   34.651773]: mali: protocol trial 696: features 0x1 before 0 after 0 wrote 0
[   34.006763]: mali: protocol trial 695: features 0x1 before 0 after 0 wrote 0
```

**`wrote 0`** —— 两次都没写。`before = after = 0` 说明读到的就是硬件默认值 `ACE_LITE(0)`。

> `features 0x1`：`GPU_COHERENCY_FEATURES(0x300)` 读到 bit0=1，即 **ACE_LITE 能力存在**。
> 但见 §2：能力存在不等于应该启用。

---

## 2. 上游 panthor：写什么值取决于**设备 DMA 属性**，不是 GPU 能力

`panthor_gpu.c:486-510` 全文：

```c
int panthor_gpu_coherency_init(struct panthor_device *ptdev)
{
	BUILD_BUG_ON(...);

	/* Start with no coherency, and update it if the device is flagged coherent. */
	ptdev->gpu_info.selected_coherency = GPU_COHERENCY_NONE;
	ptdev->coherent = device_get_dma_attr(ptdev->base.dev) == DEV_DMA_COHERENT;

	if (!ptdev->coherent)
		return 0;                    /* ← 非 coherent：直接返回，selected 保持 NONE */

	/* Check if the ACE-Lite coherency protocol is actually supported by the GPU. */
	if ((gpu_read(ptdev->gpu->iomem, GPU_COHERENCY_FEATURES) &
		      GPU_COHERENCY_PROT_BIT(ACE_LITE))) {
		ptdev->gpu_info.selected_coherency = GPU_COHERENCY_ACE_LITE;
		return 0;
	}

	drm_err(&ptdev->base, "Coherency not supported by the device");
	return -ENOTSUPP;
}
```

**关键点：`device_get_dma_attr()` 是第一道门。** 设备不 coherent 就直接返回，
`selected_coherency` **保持 `NONE`** —— 根本不去看 GPU 的 `ACE_LITE` 能力。

### 这块 SoC 是非 coherent 的，有三条独立证据

| 证据 | 内容 |
|---|---|
| 1. 驱动自己的注释 | `hakios.mali610.c:1108`：「This one reports **COHERENCY_NONE**, so the index that ends up in the PTE is **NON_CACHEABLE(5)**」 |
| 2. 页表实际选择 | `:1122` `index = (gMaliFwNc \|\| (flags & SEC_CACHE_MASK) != SEC_CACHED) ? IDX_NC : IDX_CACHE;`，共享段 `flags 0xc000001b` 的 cache mode = 3（cached coherent），被降级为 `IDX_NC` |
| 3. 真机 DT | `mt6895-xiaomi-xaga.dts` 与 `mt6895.dtsi` 里 **`dma-coherent` 零命中** |

**结论：panthor 在这块板子上会写 `NONE(31)`。**

---

## 3. 顺序：panthor 在 L2 上电**之前**写

`panthor_gpu.c:318-323`：

```c
	/* Set the desired coherency mode and L2 config before the power up of L2 */
	panthor_gpu_coherency_set(ptdev);
	panthor_gpu_l2_config_set(ptdev);

	return panthor_gpu_power_on(ptdev, L2, 1, 20000);
```

`panthor_gpu_coherency_set()` 本身（`:60-64`）：

```c
static void panthor_gpu_coherency_set(struct panthor_device *ptdev)
{
	gpu_write(ptdev->gpu->iomem, GPU_COHERENCY_PROTOCOL,
		  ptdev->gpu_info.selected_coherency);
}
```

**注释原文写得很清楚：L2 上电之前。**

---

## 4. 我们的不一致：页表说「没一致性」，寄存器说「用一致性」

| 层 | 当前值 | 含义 |
|---|---|---|
| 页表 PTE | `IDX_NC = NON_CACHEABLE(5)` | 已按 `COHERENCY_NONE` 处理，靠软件维护缓存 |
| 寄存器 `0x304` | `0` = `ACE_LITE`（硬件默认） | **仍是「走 ACE-Lite 一致性」** |

**两层互相矛盾。** 如果硬件按 `0x304 = ACE_LITE` 行事，它会尝试 ACE-Lite 一致性路径；
而这条路径在这块 SoC 上**页表已经假设不可用**（所以才用 NC）。取指可能拿不到正确数据。

> 这与 witness 的观测相容：marker 写入不可见、状态位起来但无副作用。
> **但这是解释，不是证据。** 见 §6。

---

## 5. 改法：纯 boot-arg，不动代码

```
mali_cohnone=1        （其余全部沿用 695/696）
```

`mali_cohnone` 的解析点已确证：`hakios.mali610.c:5198`
`PE_parse_boot_argn("mali_cohnone", &gMaliCohNone, sizeof(gMaliCohNone));`

传 1 后条件 `!gMaliCold || gMaliCohNone` = `false || true` = **true**，会执行 `mwr`。

### 代码层面的副作用：无

| 检查项 | 结果 |
|---|---|
| `kCoherencyNone` 值 | `31u`（`:428`） |
| `kCoherencyProtocol(0x304)` 全文写入点 | **只有 `:3175` 一处** |
| `before` 变量的用途 | 只用于 `:3178` 的打印，**无任何逻辑依赖** |
| 其他代码读 `0x304`？ | 无 |

### 硬件层面的影响：这正是要测的东西

`ACE_LITE(0)` → `NONE(31)` 改变 L2 的一致性行为。
`NONE` 模式要求**软件维护缓存**，而我们本来就在做（`mem_flush()`、`IDX_NC` 页表）。
**所以写 `NONE` 是让寄存器与页表一致，而不是引入新假设。**

---

## 6. 证据边界（必须写清）

| 层级 | 结论 |
|---|---|
| **已证明（源码）** | 我们没写 `0x304`；参数条件恒假；日志 `wrote 0` |
| **已证明（源码）** | panthor 在 L2 上电前写，且对非 coherent 设备写 `NONE` |
| **已证明（DT/驱动）** | 这块 SoC 是非 coherent |
| **推断** | 寄存器与页表不一致可能导致取指数据错误 |
| **未证明** | 写 `NONE` 后 MCU 能否启动 |
| **未证明** | 这个不一致是否就是当前卡点的原因 |

**这是目前唯一一条「上游必做、我们没做」的项，但它是候选，不是结论。**

---

## 7. 如果试，怎么做

| 项 | 值 |
|---|---|
| 编号 | trial 697（待定） |
| boot-arg 改动 | 只加 `mali_cohnone=1` |
| 其余 | 全部沿用 696（**包括 `mali_witness=0`** —— 回到完整固件） |
| 内核 | 不变（`18bb68da…`） |
| 构建 | 只重编 loader（几秒） |

**判据**：`protocol trial 697: ... wrote 1`（证明写生效）+
`after 0x1f`（证明读回 31）。若 `wrote 1` 但 `after` 不是 `0x1f`，说明寄存器不可写，
那这条线索也断。

**然后才看** `MCU status` 与 `version word` 是否有变化。
