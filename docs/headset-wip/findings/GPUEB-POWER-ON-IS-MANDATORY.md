# 主线在 MCU 启动前强制调用 GPUEB 上电 —— 我们一次都没发过

> 2026-11-15 17:10，DSH。**6-Astra 指出，我逐行核实全部成立。**
> 源码：`docs/headset-wip/raw/mainline-xaga-gpu/panthor_fw.c`
> 加 `raw/upstream-gpueb/drivers_gpu_mediatek_gpueb_gpueb_power.c`

---

## 0. 一句话

**主线 panthor 在 `media/mt6895` 上，写 `MCU_CONTROL=AUTO` **之前**，
强制调用 `mt6895_gpueb_power_on()`，而且失败就 `return` 不启动 MCU。**

**我们的驱动一次 IPI 都没发过，`

---

## 1. 主线的原文（`panthor_fw.c:1131-1146`）

```c
#if IS_ENABLED(CONFIG_MTK_GPUEB)
	if (of_machine_is_compatible("mediatek,mt6895") &&
	    mt6895_gpueb_available()) {
		ret = mt6895_gpueb_power_on();
		if (ret) {
			drm_err(&ptdev->base,
				"Failed to request GPUEB GPU power-on: %d\n", ret);
			return ret;               /* ← 失败就不启动 MCU */
		}
	}
#endif

	gpu_write(fw->iomem, MCU_CONTROL, MCU_CONTROL_AUTO);
```

### 三条确定事实

| # | 事实 | 依据 |
|---|---|---|
| 1 | 条件精确锁定 **`mediatek,mt6895`**，正是我们的 SoC | `:1132` |
| 2 | **GPUEB 上电在 `MCU_CONTROL=AUTO` 之前** | `:1134` vs `:1146` |
| 3 | **上电失败就不启动 MCU** | `:1138` `return ret` |

---

## 2. `mt6895_gpueb_power_on()` 内部做什么

`gpueb_power.c:198-202`：

```c
int mt6895_gpueb_power_on(void)
{
	return mt6895_gpueb_power_control(true);
}
```

`gpueb_power.c:157-192`（`mt6895_gpueb_power_control`）：

```c
	mutex_lock(&gpueb_power_lock);

	ret = gpueb_power_init_locked();      /* ← 先 INIT 共享内存 */
	if (ret)
		goto out;

	msg.cmd_id = CMD_POWER_CONTROL;       /* ← 再请求上电 */
	msg.u.power_state = power_on ? POWER_ON : POWER_OFF;

	ret = mtk_ipi_send_compl(&gpueb_ipidev, gpueb_power_channel,
				 IPI_SEND_POLLING, &msg,
				 GPUFREQ_IPI_DATA_LEN, IPI_TIMEOUT_MS);
```

**顺序：`INIT_SHARED_MEM` → `POWER_CONTROL`。** 两步都走 IPI。

---

## 3. 🔴 我们的现状：一次 IPI 都没发过

### 3.1 `mali_gpueb=1` 只映射，不发 IPI

`hakios.mali610.c:4303-4305`：

```c
if (gMaliGpueb) {
	gGpueb = ml_io_map(MALI_GPUEB_PHYS, MALI_GPUEB_SIZE);
}
```

**没有第二行。没有 IPI，没有 INIT，没有 POWER_CONTROL。**

### 3.2 而且这个开关本身是“冒风险映射”

`mali610.c:4296-4302` 的注释原文：

> GPUEB is behind its own clock gate. The phone's gpueb driver opens it through the clock
> framework; nothing in this kernel does. **Reading a gated MTK peripheral does not return
> an error -- the access never completes, the core waits, and the hardware watchdog resets
> the board.** A diagnostic that produces a boot loop is worse than no diagnostic, so the
> block is only mapped when `mali_gpueb=1`, and only after the gate has been opened by
> whoever turns it on.

**含义：`mali_gpueb=1` 不是「启用 GPUEB 上电」，是「冒险去映射一个可能没时钟的块」。**

**用它作实验 = 可能挂死板子，且仍然不会发 IPI。**

### 3.3 所以「GPUEB 是头号嫌疑」这条要改写

仓库早先写「头号嫌疑 = GPUEB 从没发过 IPI」。**这只说对了一半。**

| 之前的表述 | 准确的表述 |
|---|---|
| 「头号嫌疑，没试过」 | **主线明确要求，我们完全缺失，且现有开关不具备该能力** |

**这不是「嫌疑」，是「已知缺失的必需步骤」。**

---

## 4. 这能解释什么

`[DSH推断]` 如果 GPU 的上电/时钟真的由 GPUEB 掌握，那么：

- MCU 所在的电源域可能从未被真正唤醒
- 这与「`MCU_STATUS = 1` 但完全不执行」**完全相容**
- 也与「写 `0x304` 生效、L2 状态可读、但取指没有发生」相容
   —— 寄存器能读说明总线通，但核没被唤醒

**但这是解释，不是证据。** 见 §6。

---

## 5. 证据边界

| 层级 | 结论 |
|---|---|
| **已证明（源码）** | 主线在 mt6895 上 MCU 启动前强制调 GPUEB 上电，失败不启动 |
| **已证明（源码）** | `power_on` 内部先 INIT_SHARED_MEM 再 POWER_CONTROL，都走 IPI |
| **已证明（源码）** | 我们只映射，从不发 IPI；且该开关有挂死风险 |
| **推断** | GPUEB 未上电可能是 MCU 不执行的原因 |
| **未证明** | 发 IPI 之后 MCU 能启动 |
| **未证明** | 我们的 GPUEB 时钟门是否已开（`not mapped` 只说明没映射） |

---

## 6. 如果要试，得先解决三件事

| # | 问题 | 现状 |
|---|---|---|
| 1 | **GPUEB 的时钟门谁开** | 我们没开；vendor 通过 clock framework 开 |
| 2 | **共享内存地址** | `0x7c600000`（真机 DTS 确证），但 `INIT_SHARED_MEM` 必须把物理地址告诉 GPUEB |
| 3 | **IPI 协议实现** | `findings/GPUEB-IPI-PROTOCOL.md`（54 KB）+ `gpueb-ipi.h`（26 KB）已还原，模拟器 4/4 通过 |

**风险**：`mali610.c:4300` 的注释明确说，读一个门控未开的块会**挂死并触发硬件看门狗**。

**所以顺序必须是：先确认 GPUEB 时钟已开，再发 IPI。** 顺序错了就是一次板级挂死。
