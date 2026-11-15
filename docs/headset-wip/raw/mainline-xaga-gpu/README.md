# 主线 Linux 的 xaga GPU 参考源码

> 2026-11-15 抓取，来源
> <https://github.com/MT6895-Mainline/linux/tree/7.2-mt6895-xiaomi-xaga>
> 分支 `7.2-mt6895-xiaomi-xaga`。全部 HTTP 200，内容已校验（SPDX 头 + 关键符号命中）。
>
> **保存原因**：这是目前唯一公开的、在 xaga 上真的用 panthor 跑 Mali-G610 的内核源码。
> 它踩过 GPU 的坑并修好了，而且它的 GPUEB 上电路径是我们完全缺失的一步。

## 文件清单

### 设备树（硬件描述，权威）

| 文件 | 字节 | 内容 |
|---|---|---|
| `mt6895.dtsi` | 244187 | **SoC 定义，含完整 GPU 节点（`:2734`）** |
| `mt6895-xiaomi-xaga.dts` | 43951 | 板级，含 bootargs 与 GPU 时钟注释（`:2743-2757`） |

### 时钟（验证 stacks 时钟的依据）

| 文件 | 字节 | 内容 |
|---|---|---|
| `clk-mt6895.c` | 106839 | **时钟父表**：`mfgsc_ref_parents`（`:1256`）|
| `mt6895-clk.h` | 27461 | 时钟 ID 定义 |

**`mfgsc_ref_parents` 原文：**

```c
static const char * const mfgsc_ref_parents[] = {
	"tck_26m_mx9_ck",     /* 0 */
	"univpll_d6",         /* 1 */
	"mainpll_d5_d2"       /* 2 = 218 MHz */
};
```

**我们驱动设的 `parent = 2` 正是 `mainpll_d5_d2`，正确。**

### panthor（启动序列与判据）

| 文件 | 字节 | 内容 |
|---|---|---|
| `panthor_fw.c` | 45112 | **启动序列**：`:1131-1146` GPUEB→AUTO，`:1132` 起 |
| `panthor_gpu.c` | 13692 | coherency（`:459`）、L2 上电（`:296`） |
| `panthor_device.c` | 17354 | 设备初始化与电源 |
| `panthor_mmu.c` | 92716 | MMU、地址空间 |
| `panthor_hw.c` | 8528 | 硬件抽象层 |
| `panthor_fw_regs.h` | 767 | MCU 寄存器 |
| `panthor_gpu_regs.h` | 3738 | GPU 寄存器 |
| `panthor_mmu_regs.h` | 2723 | MMU 寄存器 |

### GPUEB（我们缺失的那一步）

| 文件 | 字节 | 内容 |
|---|---|---|
| **`gpueb_power.c`** | 5988 | **主线自己的 GPUEB 上电实现** |
| **`mt6895_gpueb.h`** | 597 | 极简 API：`power_on` / `power_control` / `available` |

### 官方 binding

| 文件 | 字节 |
|---|---|
| `arm,mali-valhall-csf.yaml` | 4970 |

---

## 三条最重要的引用

### 1. GPUEB 上电在 MCU 启动之前，且失败就不启动

`panthor_fw.c:1131-1146`：

```c
#if IS_ENABLED(CONFIG_MTK_GPUEB)
	if (of_machine_is_compatible("mediatek,mt6895") &&
	    mt6895_gpueb_available()) {
		ret = mt6895_gpueb_power_on();
		if (ret) {
			drm_err(&ptdev->base,
				"Failed to request GPUEB GPU power-on: %d\n", ret);
			return ret;
		}
	}
#endif

	gpu_write(fw->iomem, MCU_CONTROL, MCU_CONTROL_AUTO);
```

### 2. `power_on` 内部：先 INIT_SHARED_MEM，再 POWER_CONTROL

`gpueb_power.c:70-112`（`gpueb_power_init_locked`）：

```c
gpueb_power_channel = gpueb_get_send_PIN_ID_by_name("IPI_ID_GPUFREQ");
if (gpueb_power_channel < 0)
	...
ret = mtk_ipi_register(&gpueb_ipidev, gpueb_power_channel, NULL, NULL,
		       &gpueb_power_recv_msg);
...
msg.cmd_id = CMD_INIT_SHARED_MEM;
ret = mtk_ipi_send_compl(&gpueb_ipidev, gpueb_power_channel,
			 IPI_SEND_POLLING, &msg,
			 GPUFREQ_IPI_DATA_LEN, IPI_TIMEOUT_MS);
```

**注意 `IPI_SEND_POLLING` —— 纯轮询，与我们的裸机可行性一致。**

### 3. BG3D 时钟坑

`mt6895.dtsi:2743-2757` 注释原文：

> `coregroup` is the **MFGCFG BG3D gate**: a **hw-on gate with no Linux consumer**, so
> `clk_disable_unused()` switches it off while the GPU is running and the resulting
> external abort surfaces as **an SError in `panthor_fw_ping_work()`**.

**它的失败点是「固件已经起来之后」，比我们晚。**

---

## 边界

- 这些是**参考源码**，不是本仓的代码。许可证见各文件 SPDX 头（GPL-2.0 / MIT / BSD）。
- 分支是 `7.2-mt6895-xiaomi-xaga`，**不是 upstream mainline**，含作者自己的移植改动。
- **没有核实**：主线当前是否真能让 GPU 出画面（找不到状态说明）。
- **不能推论**：「主线已解决 xaga GPU」不成立 —— 它只说明修了一个 SError。
