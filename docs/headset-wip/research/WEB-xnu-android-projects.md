# XNU/Darwin 移植到安卓 —— 同行调研

调研时间：2026-11-14。所有结论后括号内为出处；标「未确认」的是没找到直接证据的。
区分标注：**[代码]** = 有源码/commit diff 可直接核对；**[README]** = 项目自述；**[实测]** = 有真机日志或数字。

---

## 0. 一句话结论

**这个方向有人在做，而且不止一家，但没有任何一家走到「日用」；唯一有联发科先例的是同一个上游项目（HakiOS）自己的 MT6768 移植。**
关键判断有三条：

1. 「XNU on Android」已经是一个有 4~5 个活跃分支的小生态（ios-sweet / UniXNU / Bergamota / HakiOS），
   最好的一家（HakiOS on POCO X5 Pro）能做到锁屏、桌面、设置、Safari、Wi-Fi、声音、相机、Metal 部分可用，
   但**明确自称「research bench, not a ROM」**，且不支持蜂窝注册、蓝牙、充电
   （[HakiOS docs/index.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/index.md)）。
2. **联发科平台有先例，就是 HakiOS 自己的 `hakios-mt6768`（Infinix Hot 20 / Helio G85）**，
   已做到 GPU（Mali-G52 + panfrost + Metal 画 CoreAnimation）、Wi-Fi、蓝牙、触摸、eMMC 读盘
   （[hakios-mt6768 README](https://github.com/oosexgang/hakios-mt6768/blob/main/README.md)）。
   **但它不是 MT6895**，天玑 8100 上的 XNU 移植，公开资料里没有第二个。
   → 我们不是「完全没人走过」，是「同一条路的第二个联发科设备，且是第一个天玑系列」。
3. **GPU 卡点有了明确的外部对照物**：xaga 上有一套能跑的 mainline Linux + panthor，
   而且它的 GPUEB 上电握手代码是公开的。这不是「猜」，是可以逐行对照的实现。
   详见 §3，这是本次调研最有价值的部分。

---

## 1. 同类项目清单

| 项目 | 链接 | 设备 / SoC | 完成度 | 关键做法 |
|---|---|---|---|---|
| **HakiOS**（上游，与我们同源） | [oosexgang/hakios-xnu-kernel](https://github.com/oosexgang/hakios-xnu-kernel) | POCO X5 Pro 5G（redwood, SM7325/Snapdragon 778G）；另有 Infinix Hot 20（MT6768） | 锁屏/桌面/Control Center/原生 App、触摸、8 核、Adreno GPU(OpenGL ES)、Wi-Fi、声音、相机、`/private/var` 持久化、Sileo+SSH **[README]**；Metal 部分可用（100~104 fps 翻页，120Hz 屏没跑到 120）**[实测]** | 自己的 EFI loader + Mu-Silicium UEFI + `fastboot boot` 全内存运行 + 自己的驱动集 + Linux 式 `CONFIG_HAKIOS_*` 配置系统 |
| **ios-sweet** | [dipa-studio/ios-sweet](https://github.com/dipa-studio/ios-sweet) | Redmi Note 10 Pro（sweet, SM7150/Snapdragon 732G） | **能进 iOS 桌面并解锁**；纯 CPU 渲染 20~30 fps；无 GPU。Wi-Fi/蓝牙/蜂窝/音频/相机/传感器全无 **[README]** | 纯 `fastboot boot` shim，不刷任何 boot 分区；iPhone X（iPhone10,3）身份；HFS+ root 放在 `userdata`；**4 MiB 持久日志环** |
| **UniXNU** | [UniXNU/UniXNU](https://github.com/UniXNU/UniXNU) | Xiaomi Pad 6S Pro（SM8550）为参考；`devices.toml` 里 234 个设备条目 | 类 OpenCore 的通用 UEFI 环境；**Adreno 740 上有 Metal on Vulkan**（AdrenoMetal + metal2vulkan + Turnip）**[README]** | 把「引导层」和「设备」彻底分开：共享构建工具 + OS profile + 组件目录 + SoC 仓库 + 设备仓库 |
| **Bergamota** | [ry-diffusion/Bergamota](https://github.com/ry-diffusion/Bergamota) | Poco F1（SDM845） | Darwin 18（iOS 18 代）bring-up，ios-sweet 的 fork；无 GPU，CPU 绘制，Adreno 630 进行中 **[README]** | 自带裸机 bootloader（从 `boot` 分区起）；有 arm64e/指针认证缺口的工具 |
| **hakios-mt6768**（联发科先例） | [oosexgang/hakios-mt6768](https://github.com/oosexgang/hakios-mt6768) | Infinix Hot 20（X6826B, MT6768/Helio G85, Mali-G52 MC2） | boot/CPU/存储(只读)/显示/触摸/按键/**GPU**/传感器/Wi-Fi/蓝牙(部分)/相机(部分) **[README]** | `mtkloader` 塞进 `boot_a`，在 Android 内核前面，**只在 Android 要求时启动一次 XNU**，跑完回 Android |
| **Redmi K60 Ultra 实验** | [andgeek 文章](https://andgeek.jizhiku.net/archives/2571) | Redmi K60 Ultra | **只到显示苹果 logo**（blog 声称）**[未确认]**：无代码仓库，只有一篇二手文章 | 自研引导器，仅参考 ios-sweet 思路 |

### 1.1 有没有人做到「能开机进 iOS 桌面」？

**有，至少三家：**

- **ios-sweet**：明确写 "the phone boots to the **iOS home screen**. You can unlock it, open Settings, pull down Control Center and the notification center"
  （[ios-sweet README](https://github.com/dipa-studio/ios-sweet/blob/main/README.md)）— **[README] 声称 + 有截图**。
- **HakiOS**：锁屏、桌面两页图标、原生 App、手势 **[README]**，且给出了可复现的帧率测量 **[实测]**。
- **我们自己的 xaga**：README 写「SpringBoard 起来了，状态栏、小组件、Dock 全部渲染」（本地 `README.md`）。

所以「进桌面」不是稀缺能力。**稀缺的是「日用」**：四家全部缺蜂窝注册、蓝牙、充电，
HakiOS 明确说 Apple 服务（激活/Apple ID/iCloud/App Store）「not supported and not a goal」。

### 1.2 完成度排序（按可日用程度，非官方）

```
HakiOS(redwood)  >  hakios-mt6768  >  ios-sweet  >  Bergamota  >  UniXNU(按设备而定)
   桌面+Wi-Fi+声音     桌面+Wi-Fi+BT     桌面(纯CPU)     早期          强在 UEFI 与 GPU 栈
   +相机+Metal部分
```

### 1.3 重要：ios-sweet 的「纯内存 + 不刷机」模型

这条和我们现在的处境直接相关。ios-sweet 的启动链
（[ios-sweet README](https://github.com/dipa-studio/ios-sweet/blob/main/README.md)）：

```
ABL (stock Xiaomi bootloader, unlocked)
  -> fastboot boot shim-boot.img        (temporary; nothing is flashed to boot)
     -> shim: own MMU setup, reads the live FDT, loads payload from ramdisk
        -> XNU Mach-O + generated Apple Device Tree (ADT) + boot-args
           -> XNU -> UFS driver -> HFS+ root in userdata
```

**唯一被永久写入的是 `userdata`（放 iOS root）**，boot 分区一个字节都不动。
HakiOS 也是同一套路（`fastboot boot`，写完就重启回 Android），
默认只写 `logdump` 分区几 KB
（[HakiOS architecture.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/concepts/architecture.md#what-is-written-to-the-phone)）。

**我们的 xaga 走的是 `boot_a` 刷 mtkloader 的路线**（本地 README：`LK → boot_a 里的 mtkloader → XNU`），
这是 hakios-mt6768 的路线，不是 redwood 的路线。**这是一个结构性差异，见 §6。**

---

## 2. 驱动策略：别人怎么解决「iOS 驱动不支持安卓硬件」

**答案很一致：不用 iOS 驱动，全部自己重写，在 XNU 里写原生驱动。** 没有一家用「兼容层翻译 iOS 驱动」这条路
（因为 iOS 驱动的二进制是针对 Apple 的 DART/APCIE/AIC 写的，无法翻译）。

### 2.1 事实上的做法：每个外设写一个 XNU 驱动

HakiOS 的驱动表（[docs/reference/drivers.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/reference/drivers.md)、
porting 指南 [docs/porting/index.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/porting/index.md)）
给出了**明确的移植顺序**，这是可以直接抄的工程经验：

| 顺序 | 驱动 | 为什么是这个顺序 |
|---|---|---|
| 1 | `CPUFREQ`, `THERMAL`, `SOCINFO` | SoC 自己的，只读芯片是什么、多热，不碰「属于这台手机」的东西 |
| 2 | `BUTTONS` → 触摸屏（`GOODIX` 或自己写） | 输入，让系统能用 |
| 3 | `UFS` | 手机存储，只读；modem 需要它 |
| 4 | `ADRENO` | GPU；需要 `fw-gpu/` |
| 5 | `ADSP`, `BATTERY`, `SPEAKER`, `SOUND`, `MIC` | 音频与电量都走音频 DSP |
| 6 | `WIFI`（+`QMI`） | 需要 `fw-wifi/` |
| 7 | `HAPTIC`, `CAMERA`, `TORCH`, `MODEM`, `BT` | 其余 |

### 2.2 三条可复用的工程规则

**(a) 新设备默认「不带任何手机专属驱动」启动。**
HakiOS 的 `tools/hakios-device.py new` 生成的 `device.conf` 里，
**公共仓库里每个手机驱动都被排除**，理由是原文：

> "their addresses, pins and rails are the phone they were written for, and a driver of another phone touching yours is the last thing you want."

（[docs/porting/index.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/porting/index.md)）

**这条对我们有直接教训**：xaga 的驱动是从 redwood（高通）抄地址改过来的，如果某个地址没改对
（比如某个 rail 的 SID/offset 沿用了高通的值），**症状就是「写了不报错、但硬件不动」**。

**(b) 驱动值分三类，放三个地方。** 原文：
> "A value that is a SoC fact goes to the SoC's fragment or the kernel's defaults; a value that is the phone's goes into a boot-argument ... or into a driver of the device's own"

**(c) 状态只有三档，且必须有真机证据。** 原文：
> "Only what you have seen on the phone is `works`; what is written and not yet seen is `partial` with a note; what is not there is `missing`."

### 2.3 固件从哪来

- HakiOS：**从你自己的手机上用 `adb` 抓**（`pull-firmware.sh` → `fw-gpu/`, `fw-wifi/`, `fw-touch/`），
  仓库里一个字节都不放（[docs/index.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/index.md#what-is-in-the-repository-and-what-never-is)）。
- MT6895 mainline：GPUEB 固件**不由 Linux 加载**，由 LK 阶段带起 —— 见 §3.5。

### 2.4 一个反例警告：不要指望「iOS 驱动 + 兼容层」

[eshraqism 的技术拆解文章](https://eshraqism.com/blog/running-ios-16-5-on-android-hackios-explained) 提到
HackiOS 实现了 AIC→GIC 的中断翻译 shim、Apple 私有设备树格式的转换、MMU 适配。
但这是**内核基础设施层**的 shim，不是「外设驱动翻译」。
同一篇文章也指出瓶颈就在 GPU（Metal→Vulkan 翻译）和 SEP。
**注意：这篇文章有明显的事实错误**（说 HackiOS 用 KVM 虚拟化、说 16KB 页是现代芯片的做法），
引用时只取它描述的方向性判断，不要取具体技术细节。

---

## 3. GPU CSF/MCU 启动（重点，我们的卡点）

### 3.0 结论先行

**xaga 上有一个能跑的 panthor，它的 MCU 启动序列和上游 panthor 的差别只有一处：在写 `MCU_CONTROL=AUTO` 之前，先向 GPUEB 请求 GPU 上电。**
这一处差别是公开代码，可以逐行核对。

### 3.1 panthor 上游的 MCU 启动序列（基准）

`panthor_fw_start()`（[torvalds/linux panthor_fw.c](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/panthor/panthor_fw.c)）**[代码]**：

```c
static int panthor_fw_start(struct panthor_device *ptdev)
{
	struct panthor_fw *fw = ptdev->fw;
	bool timedout = false;

	ptdev->fw->booted = false;
	panthor_job_irq_enable_events(&ptdev->fw->irq, ~0);   // ① 先开中断
	panthor_job_irq_resume(&ptdev->fw->irq);
	gpu_write(fw->iomem, MCU_CONTROL, MCU_CONTROL_AUTO);   // ② 才写 AUTO
	                                                        // ③ 等 1000ms，等 IRQ 置 booted
	if (!wait_event_timeout(ptdev->fw->req_waitqueue, ptdev->fw->booted,
				msecs_to_jiffies(1000))) {
		if (!ptdev->fw->booted &&
		    !(gpu_read(fw->irq.iomem, INT_STAT) & JOB_INT_GLOBAL_IF))
			timedout = true;
	}
	if (timedout) {
		u32 status = gpu_read(fw->iomem, MCU_STATUS);
		drm_err(&ptdev->base, "Failed to boot MCU (status=%s)", ...);
		return -ETIMEDOUT;
	}
	return 0;
}
```

**上游的判据是 IRQ + `booted` 标志，不是轮询 `MCU_STATUS`。**
`MCU_STATUS` 只在**超时之后**被读一次，用来打印错误原因（`disabled`/`enabled`/`halt`/`fatal`）。

> ⚠️ 这对我们直接有用：状态机是 `panthor_job_irq_handler` 里
> `if (!ptdev->fw->booted && (status & JOB_INT_GLOBAL_IF)) ptdev->fw->booted = true;`
> 也就是说 **`JOB_INT_GLOBAL_IF` 这一位才是「MCU 起来了」的官方判据**。
> 我们 README 说「`JOB_INT_STATUS` bit31 不升」—— 如果那个 bit31 就是 GLOBAL_IF（按 ARM 的寄存器命名习惯很可能是），
> 那我们用的判据和上游是一致的，问题不在判据。

### 3.2 完整初始化顺序（上游 `panthor_fw_init()`）**[代码]**

```
panthor_fw_init()
  ├─ fw->iomem = ptdev->iomem + MCU_CONTROL_BASE
  ├─ platform_get_irq_byname("job")            ← 必须有 "job" 中断
  ├─ panthor_irq_request(..., "job", panthor_job_irq_threaded_handler)
  ├─ panthor_hw_l2_power_on(ptdev)             ← L2 上电（内含 coherency + l2_config）
  ├─ panthor_vm_create(... CSF_MCU_SHARED_REGION_START ...)  ← MCU 的地址空间
  ├─ panthor_fw_load(ptdev)                    ← 固件各 section 拷进内存
  ├─ panthor_vm_active(fw->vm)                 ← 把 MCU 的 AS 设为 active
  ├─ panthor_fw_start(ptdev)                   ← ★ 写 AUTO
  ├─ panthor_fw_init_ifaces(ptdev)             ← 读 GLB/CSG 接口，检查 version
  └─ panthor_fw_init_global_iface(ptdev)       ← core_en_mask / timers / 中断掩码 / doorbell
```

而 `panthor_device_init()` 的更外层顺序（[panthor_device.c](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/panthor/panthor_device.c)）**[代码]**：

```
pm_runtime_resume_and_get()
  → panthor_devfreq_init()      ← 时钟/电压
  → panthor_hw_init()
  → panthor_pwr_init()
  → panthor_gpu_init()          ← 读 GPU_ID / 各 present 位
  → panthor_gpu_coherency_init()
  → panthor_gem_shrinker_init()
  → panthor_mmu_init()
  → panthor_fw_init()           ← ★ MCU 在这里启动
  → panthor_sched_init()
```

`panthor_gpu_l2_power_on()` 里有一句关键的顺序（[panthor_gpu.c](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/panthor/panthor_gpu.c)）**[代码]**：

```c
/* Set the desired coherency mode and L2 config before the power up of L2 */
panthor_gpu_coherency_set(ptdev);     // 写 GPU_COHERENCY_PROTOCOL
panthor_gpu_l2_config_set(ptdev);
return panthor_gpu_power_on(ptdev, L2, 1, 20000);
```

> 我们 STATE-MINI 里写「下一步：L2 上电前的 COHERENCY_PROTOCOL 读回与单独配置」——
> **方向是对的，而且上游就是「先设 coherency，再上电 L2」，不是「上电后设」。**

### 3.3 ★★★ 决定性发现：xaga 的 panthor 在写 AUTO 之前先找 GPUEB 要电

这段代码在 `0e693c8cd2436b1a5995d884ef28ef3f14b649bd`
（"gpu/mediatek: add GPUEB and Panthor MT6895 power handshake"，2026-08-29）**[代码]**：

```c
// drivers/gpu/drm/panthor/panthor_fw.c  panthor_fw_start()
	ptdev->fw->booted = false;
	panthor_job_irq_enable_events(&ptdev->fw->irq, ~0);
	panthor_job_irq_resume(&ptdev->fw->irq);

#if IS_ENABLED(CONFIG_MTK_GPUEB)
	if (of_machine_is_compatible("mediatek,mt6895") &&
	    mt6895_gpueb_available()) {
		ret = mt6895_gpueb_power_on();          // ★ 这一句
		if (ret) {
			drm_err(&ptdev->base,
				"Failed to request GPUEB GPU power-on: %d\n", ret);
			return ret;
		}
	}
#endif

	gpu_write(fw->iomem, MCU_CONTROL, MCU_CONTROL_AUTO);   // 然后才 AUTO
```

同一 commit 在 `panthor_device.c:panthor_device_init()` 里还有第二处，**位置在 `panthor_hw_init()` 之前**，
注释把原因写死了 **[代码]**：

```c
#if IS_ENABLED(CONFIG_MTK_GPUEB)
	/* MT6895: gpueb owns GPU power. Ask it to power on before touching any
	 * GPU registers, otherwise panthor reads all-zero ID/features.
	 */
	if (of_machine_is_compatible("mediatek,mt6895") && mt6895_gpueb_available()) {
		ret = mt6895_gpueb_power_on();
		if (ret) { dev_err(..., "Failed to request GPUEB power-on before HW init: %d\n", ret); goto err_rpm_put; }
	}
#endif

	ret = panthor_hw_init(ptdev);
```

同一 commit 还在 `panthor_device_init()` 末尾加了 **[代码]**：

```c
#if IS_ENABLED(CONFIG_MTK_GPUEB)
	/* Bring-up workaround for MT6895: keep panthor runtime-resumed while
	 * gpueb is managing the GPU power state. Autosuspend/resume cycles can
	 * leave the EB and Mali firmware out of sync and cause CSG timeouts.
	 */
	pm_runtime_forbid(ptdev->base.dev);
	pr_info("XAGA-PANTHOR: GPU runtime PM forbidden\n");
#endif
```

> **对我们卡点的意义（这是推断，但推断链条很短）：**
> 如果 MCU 的电源域实际由 GPUEB 控制，而 AP 侧只是「写了 `MCU_CONTROL=AUTO`」但从没向 GPUEB 要过电，
> 那么 MCU 的核可能**根本没有时钟/电源**。这种情况下：
> - 写 `MCU_CONTROL` 本身**不报错**（AP 侧的 bus 是通的，寄存器写得进去）
> - 但 MCU 一条指令都不执行（**核没电/没时钟**）
> - `MCU_STATUS` 可能读到某个默认值（比如 1 = enabled，因为它就是被写进去的值）
> - 哨兵值永远不被改写
>
> **这四条和我们 19 轮观察到的现象全部吻合。**

### 3.4 GPUEB 是什么、怎么和它说话 **[代码]**

**它是谁**：MT6895 上的一颗独立协处理器，基址 `0x13c40000`，
`compatible = "mediatek,gpueb"`，`gpueb-support = <1>`，
中断 `GIC_SPI 516`
（`mt6895.dtsi:2650`，[raw/mt6895.dtsi](./raw/mt6895.dtsi) 已下载）。

**DT 节点里没有 `clocks`、没有 `power-domains`** —— 这点很重要：
说明它的时钟由 LK 开好、Linux 侧不碰。

**怎么说话**：MTK 的 mailbox/IPI。DT 里的参数（`mt6895.dtsi:2650-2716`）**[代码]**：

```dts
mbox_count = <1>;
mbox_size  = <160>;   /* slots */
slot_size  = <4>;     /* bytes */
ts_mbox    = <0>;

/* id, mbox, send_size */
send_table = <0 0 4>, <1 0 8>, <2 0 3>, <3 0 3>, <4 0 9>, <5 0 4>, <6 0 6>, <7 0 6>;
send_name_table = "IPI_ID_FAST_DVFS_EVENT", "IPI_ID_GPUFREQ", "IPI_ID_SLEEP",
                  "IPI_ID_TIMER", "IPI_ID_FHCTL", "IPI_ID_CCF",
                  "IPI_ID_GPUMPU", "IPI_ID_FAST_DVFS";

reg = <0x13c40000 0x22000>,   /* gpueb_base   */
      <0x13c5fd80 0x280>,     /* mbox0_base   */
      <0x13c62004 0x4>,       /* mbox0_set    */
      <0x13c62074 0x4>,       /* mbox0_clr    */
      <0x13c62000 0x4>,       /* mbox0_send   */
      <0x13c62078 0x4>;       /* mbox0_recv   */
interrupts = <GIC_SPI 516 IRQ_TYPE_LEVEL_HIGH 0>;
```

**协议（`gpufreq_ipi.h`）** **[代码]**：

```c
#define IPI_TIMEOUT_MS     10000U
#define GPUFREQ_STATUS_MEM_SZ  0x400
#define GPUFREQ_IPI_DATA_LEN  (sizeof(struct gpufreq_ipi_data)/sizeof(unsigned int))  // = 4 个 u32 槽

enum gpufreq_ipi_cmd {
	CMD_INIT_SHARED_MEM = 0,
	CMD_GET_FREQ_BY_IDX = 1, CMD_GET_POWER_BY_IDX = 2, CMD_GET_OPPIDX_BY_FREQ = 3,
	CMD_GET_LEAKAGE_POWER = 4, CMD_SET_LIMIT = 5,
	CMD_POWER_CONTROL = 6,          // ★ 上电用这个
	CMD_COMMIT = 7,
	CMD_GET_DEBUG_OPP_INFO = 8, ... CMD_NUM = 20,
};

enum gpufreq_target { TARGET_DEFAULT=0, TARGET_GPU=1, TARGET_STACK=2, TARGET_INVALID };

struct gpufreq_ipi_data {
	enum gpufreq_ipi_cmd cmd_id;
	unsigned int target;
	union {
		int oppidx; int return_value;
		unsigned int freq, volt, power, power_state, mode;
		struct { unsigned long long status_base, debug_base;
		         unsigned int status_size, debug_size; } addr;
		...
	} u;
};
```

**上电三步（`gpueb_power.c`）** **[代码]**：

```
1. gpueb_get_send_PIN_ID_by_name("IPI_ID_GPUFREQ")   → 通道 1
2. mtk_ipi_register(&gpueb_ipidev, ch, NULL, NULL, &recv_msg)
3. CMD_INIT_SHARED_MEM:
      msg.u.addr.status_base = MEM_ID_GPUFREQ 的 phys
      msg.u.addr.status_size = 0x400
      msg.u.addr.debug_base  = phys + 0x400
      msg.u.addr.debug_size  = 总size - 0x400
   mtk_ipi_send_compl(..., IPI_SEND_POLLING, ..., IPI_TIMEOUT_MS)
4. CMD_POWER_CONTROL:
      msg.u.power_state = POWER_ON   (1)
   mtk_ipi_send_compl(..., IPI_SEND_POLLING, ...)
5. 读 recv_msg.u.return_value，负值即失败
```

源码里有一条关键注释（决定「要不要顺便 commit OPP」）**[代码]**：

> "Downstream gpufreq_v2 sends ONLY CMD_POWER_CONTROL here when the EB owns DVFS; the firmware restores its own boot OPP internally. Never commit OPP indexes from this path"

### 3.5 共享内存地址：`0x7c600000`（可直接读，不受 0x13c40000 禁读限制）**[代码]**

dtsi 里只给了**大小**（`gpueb_mem_table = <0 0x1000>, <1 0x180000>`，名字 `"MEM_ID_GPUFREQ", "MEM_ID_LOG"`）。
**真实地址在 board dts**（`mt6895-xiaomi-xaga.dts:618-623`）**[代码]**：

```dts
/* GPUEB shared memory: the live FDT reserves 2MB at 0x7c600000 as
 * mblock-38-me_GPUEB_SHARED. gpueb_reserved_mem.c can use it directly
 * when gpueb_mem_addr/size are present on the gpueb node.
 */
&gpueb {
	gpueb_mem_addr = <0x0 0x7c600000>;
	gpueb_mem_size = <0x0 0x200000>;
};
```

保留块本体（`xaga.dts:200`）：
`mblock-38-me_GPUEB_SHARED { reg = <0x00 0x7c600000 0x00 0x200000>; compatible = "mediatek,me_GPUEB_SHARED"; }`

**布局**：
```
0x7c600000 .. 0x7c600fff   4 KiB   MEM_ID_GPUFREQ（其中前 0x400 = status，其余 = debug）
0x7c601000 .. 0x7c7fffff   ~1.5 MB MEM_ID_LOG
```

**这 32 个 u32 是现成的健康检查面板**（`gpueb_power.c:gpueb_gpufreq_status_show`）**[代码]**：
```
cur_oppidx_gpu, cur_oppidx_stack, opp_num_gpu, opp_num_stack,
cur_fgpu, cur_fstack, cur_vgpu, cur_vstack,
cur_vsram_gpu, cur_vsram_stack, cur_power_gpu, cur_power_stack,
max_power_gpu, max_power_stack, min_power_gpu, min_power_stack,
cur_ceiling_gpu, cur_floor_gpu, cur_ceiling_stack, cur_floor_stack,
cur_c_limiter_gpu, cur_f_limiter_gpu, cur_c_limiter_stack, cur_f_limiter_stack,
power_control, dvfs_state, shader_present, power_count,
aging_enable, avs_enable, sb_version, ptp_version
```
→ **在我们完全不敢碰 `0x13c40000` 的情况下，这是唯一能安全观测「GPUEB 到底认不认为 GPU 已上电」的地方。**
`power_control`、`power_count`、`dvfs_state` 这三个字段就能直接回答「有没有上过电」。

### 3.6 GPUEB 固件从哪来 —— 不由 Linux 加载 **[代码]**

在 `gpueb_ipi.c` / `gpueb_init.c` / `gpueb_power.c` 里 grep
`firmware|request_firmware|clk_get|clk_enable|reset_control` → **零命中**。
`gpueb_init.c` 的 probe 只做三件事：`gpueb_ipi_init()` + `gpueb_reserved_mem_init()` + logger。
`gpueb_ipi.c` 只做 `mtk_mbox_probe()` 和 pin table 建立。

**结论：GPUEB 固件由 LK（或更早阶段）加载并启动，Linux 只是通过 mailbox 跟它说话。**
佐证：GPUEB 的 DT 节点没有 `clocks`、没有 `power-domains`、没有 `resets`。

→ **对我们的含义**：如果 LK 已经启动了 GPUEB，那它**在我们跳进 XNU 之后大概率还活着**。
但「活着」≠「已经给 GPU 上电」。**这两个假设必须分开验证**，验证方法也现成：
读 `0x7c600000` 的 `power_count` / `power_control` / `dvfs_state`。

### 3.7 MFG2-12 到底要不要全上电 —— 有实测答案 **[代码]**

`mt6895.dtsi` 里那句点名我们症状的注释确实存在：
> "The MT6895 GPU MTCMOS is split into MFG1..MFG12. Downstream attaches all twelve domains to the GPU node and powers them through PM runtime; the PDCv2 registers then let the GPU control the finer shader/stack domains. **Without MFG2-12 the shader cores never become ready (shader_ready stays 0) and the MCU firmware hangs before writing the shared interface version.**"

**但后来的 commit 给出了实测结论**（`b2fb495073b1`，2026-09-14，"soc: mediatek: scpsys: keep only mfg1 powered on, not every boot-on domain"）**[代码]**：

> "Releasing them one at a time on xaga showed **exactly one domain matters: mfg1**, the MFG cluster's top domain. panthor owns the GPU clocks and supplies, but nothing in mainline owns this MTCMOS domain, and **with it off panthor fails its reset ("CSG 0 update request timedout", "Failed to stop MCU", "AS_ACTIVE bit stuck")** and part of the display goes with it. Mainline genpd attaches one domain per device and the GPU needs the whole MFG cluster (downstream lists MFG1..MFG12), so **keeping mfg1 on is the honest answer for now**."

同时给出的方法论也很值：
> "add `/sys/kernel/debug/scpsys/ctl` to release or restore one domain at runtime ... Releasing them one at a time on xaga showed exactly one domain matters"

**结论（有实测依据）：**
- 他们**只保 `mfg1` 一个域常开**，没有逐个上电 MFG1..MFG12
- `mfg1` 是 MFG 集群的**顶层域**；开了 mfg1 相当于整簇有电，子域由 PDCv2/硬件/GPUEB 自己管
- 所以「MFG2-12 要不要全上电」的答案是：**AP 侧只需保证 mfg1（顶层）不掉电**
- **但这不否定我们的 PDCv2 结论** —— 两者是不同层：mfg1 是 MTCMOS 顶层供电，PDCv2 是簇内细粒度电源域。
  **「只开 leaf、不开 mfg1」会精确产生我们现在的症状**（MCU 不执行 / AS_ACTIVE 卡住）。
  → **建议优先确认：我们有没有把 mfg1 打开并保持。**

### 3.8 另一条独立的坑：`clk_disable_unused()` 会关掉「看起来没人用」的门 **[代码]**

`16f1bf23e6bd` 和 `7ec259b064fc` 两个 commit 记录了两个真实事故：

- **`mfgcfg_bg3d`**（GPU 的 "coregroup" 门）："a hw-on gate with no Linux consumer, so `clk_disable_unused()` switches it off while the GPU is running and the resulting external abort surfaces as an **SError in `panthor_fw_ping_work()`**"
- **`ufs_sel`**："`clk_disable_unused()` cuts ufs_sel (taking ufs_ck with it)"

对应 DT 改动：给 GPU 加第三条时钟 `coregroup`：
```dts
clocks = <&topckgen CLK_TOP_MFG_SEL_0_CK>,   /* core */
         <&mfgcfg   CLK_MFGCFG_BG3D>,        /* coregroup */
         <&topckgen CLK_TOP_MFG_SEL_1_CK>;   /* stacks */
clock-names = "core", "coregroup", "stacks";
```
并在 `panthor_device_resume()` 里 enable、`suspend()` 里 disable。

> **对我们的意义**：我们的驱动注释里提到「MFG_ASYNC_CON 有四个门，厂商只开两个 slave 的」，
> 这和 `mfgcfg_bg3d` 是不同位置的两类门，但**失败模式完全一样**：
> **门没开 → 访问不报错 → 核等待 → 看门狗复位或永久挂住。**
> 建议把所有「hw-on 但 AP 侧没有 consumer」的门做一次清单化核对。

### 3.9 没有任何人公开记录过「MCU_CONTROL=AUTO 之后零指令执行」的根因

我搜了 `Mali CSF MCU boot`、`MCU_CONTROL AUTO`、`CSF firmware MCU_STATUS`、`panthor firmware init` 等组合，
**没有找到任何一篇记录这个具体症状（MCU 收到 AUTO 但哨兵值从不改写）的公开材料**。
能找到的最接近的是 §3.3 / §3.7 / §3.8 三条**间接**成因。
→ **这条如实记为「没找到」**。但也说明：我们不缺信息，缺的是把这三条逐一排除。

---

## 4. boot 链与「刷坏了怎么救」

### 4.1 各家怎么处理 Android bootloader「只认 Android boot image」

**答案：Android boot image 本身就是一个容器，把任意裸机程序塞进去就行。**
这不是 hack，是 Android 的既定格式。

- **ios-sweet**：shim 打成 Android `boot.img` v2，`fastboot boot` 起，**不刷任何 boot 分区**
  （[README](https://github.com/dipa-studio/ios-sweet/blob/main/README.md)）。
  `mkbootimg` 参数给了具体值：
  ```
  --base 0x3fff8000 --kernel_offset 0x8000 --pagesize 4096
  --ramdisk_offset 0x26f08000 --tags_offset 0x07c88000 --dtb_offset 0x07c88000
  --header_version 4
  ```
- **lk2nd（postmarketOS）**：同一思路的成熟实现。原文：
  > "it does not replace the stock bootloader. Instead, it is **packed into an Android boot image**, which is then loaded by the stock bootloader just like the original Android image. The real operating system can be placed in the boot partition with 512 KiB offset ... **It does not have to be Android or even Linux, any kind of kernel can be packed into an Android boot image.**"
  （[pmOS wiki: lk2nd](https://wiki.nura.eco/wiki/Lk2nd)）
- **hakios-mt6768**：`mtkloader` 塞进 `boot_a`，**在 Android 内核前面**（chainload），
  "starts XNU once when Android asks for it, and the phone comes back to Android after every run"。

### 4.2 怎么在「不破坏 Android」的前提下试跑 —— 三种模式

| 模式 | 谁在用 | 代价 | 风险 |
|---|---|---|---|
| **纯 RAM，`fastboot boot`** | ios-sweet、HakiOS(redwood)、lk2nd | 每次都要插 USB | **最低**：断电即回 Android，boot 分区零写入 |
| **塞进 `boot_a`，chainload** | hakios-mt6768、**我们 xaga** | 一次刷入，之后可脱离 USB | **中**：boot_a 被我们自己占着，刷坏了要能回来 |
| **双系统槽位 + U-Boot 菜单** | HakiOS redwood（[dual-boot.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/dual-boot.md)） | 要切分区、要 `fastboot -w` | **中高**：会擦 Android data |

HakiOS 的双系统做法（`dual-boot.md`）：
- HakiOS 的 U-Boot 放 `boot_a`，`darwin` 分区 20 GiB 在 LUN 0 末尾，**Android 在 slot b 完全不动**
- 启动时 10 秒菜单，**默认选 Android**，理由写得很实在：
  > "With no choice it starts Android: under HakiOS the phone does not charge, so a phone that restarts by itself is better off in Android."
- 选 Android 时"marks slot a unbootable and restarts; ABL makes Android's slot active"
- 要再看到菜单：`fastboot set_active a`，或 Android 里 root 跑 `hakios-menu.sh`

> **这条对我们有直接价值**：如果 xaga 也能把 mtkloader 放进 **slot b**（或一个 Android 不用的槽），
> 而让 slot a 保留原厂 Android，那「刷坏了」就退化成「`fastboot set_active a` 一条命令」。

### 4.3 「刷坏了怎么救」——各家的答案

**(a) 分级备份，每步都有门禁。**
HakiOS 的 [install-safe.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/install-safe.md) 是最完整的一份，
核心是**顺序**：

> 1. nothing is written until what cannot be made again has been copied off and proven;
> 2. the system moves to the phone's own storage **before** the boot chain does: the loader is still started from RAM (`fastboot boot`), so a wrong system image costs a reboot into Android, not a phone;
> 3. the boot chain moves last, into the slot Android does **not** run from, with the other slot left exactly as it was;
> 4. **the bootloader chain itself (`xbl`, `abl`, `tz`, `hyp` ...), the modem's and the radio's calibration, and the active slot are never written by this project. On Xiaomi phones a damaged bootloader chain can need an authorised service tool to revive; that is the one failure no step here may risk.**

备份清单（我们可以直接对照）：
> "The boot-related partitions of both slots, `vbmeta*`, `persist`, `modemst1/2`, `fsg`, the partition tables, and with `--with-bootchain` the chain. Each copy is proven by **three SHA-256s** (the phone's before, the host's, the phone's after)"

**(b) 一个真实的「手机失联」案例，值得抄的教训**（同文档 "The phone that went quiet"）：
> "Writing this, the phone vanished from USB: not adb, not fastboot, not Qualcomm's emergency mode. The last run before that ended normally and left the phone in the loader. The likeliest cause is the **battery**: nothing charges the phone while it sits in the loader (the kernel's charging path is not written), the screen was on for hours, and a long session is a flat battery."

→ 他们因此加了 **stage 0 的 battery gate**（电量 > 60%）。

> ⚠️ **这条对我们的「四次黑屏」高度相关**：
> 我们 README 说「重启后不保存任何设置」，`docs/GPU-STATE-MINI.md` 说「普通 reboot 服务会卡」。
> **如果 XNU 侧不充电，而调试会话又长，电量低会导致「黑屏、进不去、也不报错」——和症状完全一致。**
> 建议把「刷之前电量 > 60%」变成硬门禁。

**(c) 联发科特有的救援路径 —— `SP Flash Tool` + preloader**
pmOS wiki 的 xaga 页面（[wiki.nura.eco](https://wiki.nura.eco/wiki/Xiaomi_Redmi_Note_11T_Pro_(%2B)_/_POCO_X4_GT_/_Redmi_K50i_(xiaomi-xaga))）给了：
- **Fastboot**：Power + Volume Down
- **Recovery**：Power + Volume Up（仅 Android）
- **Preloader Mode**：> "Flash any recent AOSP custom ROM for xaga to get **engineering preloader**, if flashing preloader alone is risky. With leaked engineering preloader, it exposes insecure preloader port at boot, use **SP Flash Tool V6** to connect without auth."

> 这是联发科相对于高通的一个**优势**：只要有 engineering preloader + SP Flash Tool，
> 即使 `boot_a` / `preloader` 全废，也能从 BROM/Preloader 端口救回来。
> 我**没有**在本地项目文档里找到我们已经准备了这个能力的记录 —— **建议核实，这可能就是我们缺的那张底牌。**

**(d) lk2nd 的日志取法（值得抄的小技巧）**
> "If the device shows up via fastboot you can get a log file from lk2nd using `fastboot oem log && fastboot get_staged <output-file>`"

### 4.4 一个反例：不要替换 LK 的 dtb

pmOS wiki 原文（Device tree 节）：
> "We haven't ported any third-party bootloader like u-boot. **MediaTek's LK itself relies on dtb inside vendor_boot and dtbo partition, so replacing dtb there with our custom build from Linux mainline will crash LK itself.** So we are overwriting LK patched dtb with custom linked into kernel image."

→ 联发科平台上，**DTB 的归属和 Android 不同**，改错位置会让 LK 自己崩。

---

## 5. 黑屏取证的可行手段（重点）

### 5.1 结论：有 5 条成熟路径，我们目前只用了 1 条半

| # | 手段 | 谁验证过 | 是否需要串口 | 掉电后还在吗 |
|---|---|---|---|---|
| 1 | **持久日志环（自己的分区）** | ios-sweet **[README]**；**我们已有 logbook** | 否 | 是（写在 flash） |
| 2 | **ramoops / pstore** | MT6895 mainline **[代码+实测]** | 否 | **要满足 4 个条件**（见 5.3） |
| 3 | **LK 的 last boot log（`log_store` → `expdb`）** | MT6895 mainline **[代码]** | 否 | 是（LK 拷到 expdb） |
| 4 | **屏幕上的大字/色块报告** | 我们已有（`fb_rect`, `STEP_BAND`） | 否 | 否（但能定位"跑到第几步"） |
| 5 | **独立硬件：USB 串口 / 逻辑分析仪** | 通用 | 是 | — |

### 5.2 ★ ios-sweet 的 4 MiB 持久日志环 —— 和我们 logbook 几乎一样，但有两点不同

ios-sweet（[README](https://github.com/dipa-studio/ios-sweet/blob/main/README.md)）：
> "There is no serial port (the debug UART is wired as SPI on this board). The kernel draws its console into the framebuffer that ABL leaves at `0x9c000000`, and **keeps a persistent 4 MiB log ring at the end of `userdata`**. You read that log from **OrangeFox recovery** with `tools/sweet-log-pull.sh`, so you don't have to film the screen."

取日志的完整流程：
```sh
fastboot boot recovery/recovery.img        # OrangeFox, temporary
tools/sweet-log-pull.sh logs/boot.txt      # 4 MiB console ring from the end of userdata
adb reboot bootloader
```

**两点关键差异（值得我们抄）：**
1. **日志放在 `userdata` 末尾**（一个 Android 本来就在用的分区），而不是自己造的 `logdump` 分区。
   → 好处：不用改分区表；坏处：`fastboot -w` 会清掉。
2. **用临时启动的 recovery 去读**，不依赖 XNU 自己能把日志交回来。
   → **这一条是关键**：我们现在的 logbook 依赖「loader 在跳进 XNU 前/交回控制权时写头部」，
   如果内核在中间死了，头部就没写，`len` 只报几百字节 —— 这正是我们 `docs/DEBUGGING.md` 记录的坑：
   > "头部只在「loader 准备跳进 XNU」和「内核交回控制权」这两个时刻写...`len` 报 399、`reason` 报 `0xfeedface`...内核几十秒的输出一条都不在区里。"

   而 ios-sweet 的模式是：**日志环由内核持续 flush，读取由外部 recovery 完成**，两边解耦，不依赖内核正常退出。
   这正是 commit `0069 Batched persistent-log flushes` 干的事。

**ios-sweet 还做了 crash 符号化**：`tools/sweet-crash.py` symbolicates console crash reports against the shared cache
（日志里有 `CRASH name ...` 带 backtrace 的记录）。这是"黑屏也能拿到可用信息"的最后一块。

### 5.3 ★★★ ramoops/pstore 在 xaga 上的 4 个失败模式（全部有 commit 实证）

这是一份**极其宝贵的踩坑记录**，因为它是同一台机器（xaga）、同一颗 SoC。
以下全部来自 `MT6895-Mainline/linux` 的三个 commit：`d76b79da1864`、`e7481ca0abf4`、`5788fc18500a`、`7b02de532b74`。

**失败模式 1：地址选错 —— LK 会每轮重写那块内存**
> "NOT at the downstream/LK address `0x48090000`: memblock shows `0x48080000..0x4817ffff` NOMAP is LK's own `aee_debug_kinfo` + `minirdump` cluster, and **the bootloader deterministically rewrites it every boot** — the zone header came back byte-identical garbage (`0x03014200` / `0x42074240` / ...) across both a Cold_reset and an AP_WDT reset, so **ramoops zapped the zone every time and `/sys/fs/pstore` stayed empty**."

→ 这解释了一件非常重要的事：**pstore 空，不代表内核没崩，而是那块内存被 bootloader 洗了。**

**失败模式 2：窗口大小有上限 —— 2 MiB 会烂掉 25~30%**
> "Keep the size at `0xe0000` (896 KiB): **a 2 MiB region, at this or any other address, comes back with ~25-30% of its payload bytes damaged**, so the preserved window is bounded at roughly 1 MiB."

**失败模式 3：`mem-type 1`（cacheable）会 panic**
> "Do NOT set mem-type: mem-type 1 (cacheable) panics at boot with an alignment fault in `persistent_ram_new` (x9=ffff800080b00492, FSC 0x21), so the default write-combining mapping stays."

**失败模式 4：WDT 外部复位会烂掉整个窗口**
> "The WDT's external reset output is wired to the PMIC and **turns an AP_WDT timeout into a full cold reset, which is what rots the ramoops window (~13% of bits)** before pstore can be read. Keep the reset internal to the SoC so LK sees a normal warm AP_WDT reboot and the DRAM contents survive."
```dts
&watchdog { mediatek,disable-extrst; };
```

**另外两个「静默失败」的坑：**

- **`ecc-size` 必须是 16**：
  > "the 12-byte header is rewritten on every console write, so an abrupt reset can tear it; Reed-Solomon over the header runs before the signature compare and repairs it"
- **必须加 `ignore_loglevel`**：
  > "the quiet console sysctl sets `console_loglevel=0`, which suppresses *every* message on all consoles, so without it **the zone only ever held the first ~8 s of boot**."
- **64 位地址要写两格**：
  > "A bare `0x1b0000000` in a 32-bit cell silently truncates to `0xb0000000`."

**最终可用的配置（`e7481ca0abf4`，已改为从 LK cmdline 继承）** **[代码]**：
```dts
mblock-9-ramoops {
	reg = <0x00 0x48090000 0x00 0xe0000>;   /* 896 KiB @ LK 自己的窗口 */
	no-map;
	compatible = "mediatek,ramoops-reserved";   /* 故意不给 "ramoops" */
};
```
配合 `arch/arm64/kernel/setup.c` 里新增的 `xaga_capture_lk_ramoops()`：
把 LK 传进来的 `ramoops.*` 参数在重读内嵌 FDT 之前**先保存再拼回** `boot_command_line`。

> **路径依赖提醒**：这个方案是「LS 用 LK 的 cmdline」，前提是 **LK 本身就配了 ramoops**。
> 我们 xaga 的 loader 是我们自己的 mtkloader，**LK 的 `ramoops.mem_address` 参数我们可能丢了**。
> → 要么继承 LK 的 cmdline，要么照 `d76b79da1864` 的做法自己在 `0x1b0000000` 开一个（896 KiB / ecc 16 / 无 mem-type）。

### 5.4 ★ LK 的 last-boot log：`log_store` @ `0x7ffbf000` → `expdb`

xaga board dts 里的注释（`xaga.dts:72-73`）和 `mblock-6-log_store` 节点 **[代码]**：
> "Capture the dead boot with `read-lastboot-log.sh` (**XAGR ring in `log_store` at `0x7ffbf000`, copied to `expdb` by LK**)"
```dts
mblock-6-log_store {
	reg = <0x00 0x7ffbf000 0x00 0x40000>;
	compatible = "mediatek,log_store";
};
```

**这条对我们的价值可能最大**：因为我们的启动链**就是 LK → mtkloader → XNU**，
所以 **LK 自己的日志里会有「LK 到底有没有把控制权交给我们的 loader」这一条**。
四次黑屏如果是「根本没到 XNU」，LK 的 log 会显示；如果是「到了 XNU 才死」，LK 的 log 会停在跳转那条。

> 注意：`read-lastboot-log.sh` 这个脚本我在 linux / quirks / initramfs 三个仓库里都没找到
> （`scripts/`、`tools/`、根目录都试过，404）。**脚本本身不可得，但机制描述清楚，可自己实现**：
> 读 `0x7ffbf000` 的 256 KiB，或从 `expdb` 分区读。

### 5.5 我们已有的手段：logbook（评估）

我们的 `docs/DEBUGGING.md` 已经解决了「没有串口」这个前提：
> "`xaga` 的 AP UART 没有引出，ramoops 区域被 Android 占着，pstore 一直是空的。"

已有：
- **logbook**：自己分区（`xnusys` 里的 4198400 字节窗口，起点 2676981760，72 字节头 + 4 MiB 正文）
- **看门狗快照**：`SNAP_PA 0x90000100`，`SNAP_SIZE 0x100000`，魔数 `"SNAP"`，下一轮 loader 取出来
- **屏幕色块**：`STEP_BAND(n, colour)` 标「跑到第几步」
- **boot-arg 定时交回控制权**：`XNU_WATCH_SECONDS` 到期自动写 logbook 再 `chain_android()`

**三个已知弱点（都可能导致「四次黑屏拿不到日志」）：**
1. **头部只在两个时刻写**，内核中途死就没头部 → `len` 只剩几百字节（我们已踩过）
2. **「抓屏也吃缓冲」**（`DEBUGGING.md` 自己写的）
3. **`SNAP` 依赖「下一轮 loader 起来」** —— 如果黑屏是因为 loader 自己就没跑起来，快照也没人取

**→ 建议（结合 §5.2/§5.3/§5.4）**：
- 把「读日志」从「依赖内核交回控制权」解耦成「内核持续 flush + 外部读取」
- 加一条**完全独立于 XNU 的取证通道**（LK log / expdb，或临时 recovery）
- ramoops 如果要用，必须照 §5.3 的四条坑配

---

## 6. 对 HakiOS 的具体可用点（按优先级）

### P0 — 直接影响当前卡点

**① 向 GPUEB 请求 GPU 上电（`CMD_POWER_CONTROL`），再写 `MCU_CONTROL=AUTO`**
- **能用来干什么**：这是 xaga 上 panthor 能跑起来而我们的不能跑的唯一已知差异点。
  在 XNU 里实现：开 GPUEB 的门 → 找 `IPI_ID_GPUFREQ`（通道 1）→ `CMD_INIT_SHARED_MEM`（`0x7c600000`）→ `CMD_POWER_CONTROL(1)`。
- **先做的验证（零风险）**：**读 `0x7c600000` 的 32 个 u32**，看 `power_control` / `power_count` / `dvfs_state`。
  如果 `power_count == 0`，说明 XNU 下从没上过电 → 假设成立。
  这块是普通 DRAM 保留区，**不受 `0x13c40000` 禁读限制**。
- 出处：[commit 0e693c8](https://github.com/MT6895-Mainline/linux/commit/0e693c8cd2436b1a5995d884ef28ef3f14b649bd)、`gpueb_power.c`、`gpufreq_ipi.h`

**② 确认 `mfg1` MTCMOS 顶层域一直开着**
- **能用来干什么**：关掉它 → panthor 精确出现 "CSG 0 update request timedout"、"Failed to stop MCU"、"AS_ACTIVE bit stuck"、MCU 不执行。
  这和我们的症状高度重合。
- 出处：[commit b2fb4950](https://github.com/MT6895-Mainline/linux/commit/b2fb495073b1cef005090dd8d8fb086fb83936e0)

**③ 把「MCU 起来了」的判据对齐上游：`JOB_INT_GLOBAL_IF`，不是 `MCU_STATUS`**
- **能用来干什么**：避免再被 `MCU_STATUS=1` 误导（我们 STATE-MINI 已经意识到这条：
  "`MCU_STATUS=1` 不能替代 boot IRQ"）。上游实现里 `MCU_STATUS` 只在超时后读一次用于报错。
- 出处：[torvalds/linux panthor_fw.c](https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/panthor/panthor_fw.c)

**④ 核对所有「hw-on 但无 AP 侧 consumer」的门，尤其 `mfgcfg_bg3d`（coregroup）与 `MFG_ASYNC_CON` 的 master 侧**
- **能用来干什么**：这类门没开 → 访问不报错 → 核挂住 → 看门狗复位。是「零指令执行」的另一个候选成因。
- 出处：[commit 7ec259b0](https://github.com/MT6895-Mainline/linux/commit/7ec259b064fc)、[commit 16f1bf23](https://github.com/MT6895-Mainline/linux/commit/16f1bf23e6bd)

### P1 — 直接改善「黑屏取证」

**⑤ 加一条不依赖 XNU 的取证通道：LK 的 `log_store` @ `0x7ffbf000` → `expdb`**
- **能用来干什么**：区分「根本没到 XNU」和「到了 XNU 才死」。
  四次黑屏如果是前者，LK log 会停在很前面的位置；如果是后者，LK log 会显示跳转。
- 这是**成本最低、信息量最大**的一条，因为我们的启动链本来就经过 LK。
- 出处：`mt6895-xiaomi-xaga.dts:72`、`mblock-6-log_store`

**⑥ 照 §5.3 的四条坑配 ramoops**
- 地址不要用 `0x48090000`（LK 会洗）；窗口 ≤ 1 MiB（2 MiB 烂 25-30%）；
  `ecc-size=16`；不要 `mem-type 1`；`ignore_loglevel`；`&watchdog { mediatek,disable-extrst; }`
- 出处：commit `d76b79da1864` / `e7481ca0abf4` / `7b02de532b74`

**⑦ 日志「持续 flush + 外部读取」解耦**
- **能用来干什么**：解决我们 `DEBUGGING.md` 里「头部只在两个时刻写，内核中途死就一条都拿不到」的坑。
- ios-sweet 的做法：4 MiB 环在 `userdata` 末尾，用临时 recovery + `sweet-log-pull.sh` 读。
- 出处：[ios-sweet README](https://github.com/dipa-studio/ios-sweet/blob/main/README.md)

**⑧ 刷机前的硬门禁：电量 > 60%**
- **能用来干什么**：排除「黑屏其实是没电」。HakiOS 就是被这件事坑过才加的 gate。
- 出处：[install-safe.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/install-safe.md#the-phone-that-went-quiet)

### P2 — 结构性改进

**⑨ 考虑把 mtkloader 挪到 Android 不用的槽位（双槽），保留 slot a 原厂 Android**
- **能用来干什么**：把「刷坏了」降级成 `fastboot set_active a` 一条命令。
- 出处：[HakiOS dual-boot.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/dual-boot.md)

**⑩ 核实联发科救援底牌：engineering preloader + SP Flash Tool V6**
- **能用来干什么**：BROM/Preloader 级救援，`boot_a` 全废也能救。
- **这条我判断价值很高，但需要主控判断要不要碰**（涉及刷 preloader，wiki 自己说 "risky"）。
- 出处：[pmOS wiki xaga](https://wiki.nura.eco/wiki/Xiaomi_Redmi_Note_11T_Pro_(%2B)_/_POCO_X4_GT_/_Redmi_K50i_(xiaomi-xaga)) Preloader Mode 节

**⑪ 抄 hakios-mt6768 的「chainload，跑完回 Android」模型**
- 我们已经在这条路上（`chain_android()`），但可以对照它的 `xnu-cycle.sh`
  （一次完成 build → flash boot_a → 起 XNU → 取日志）。
- 出处：[hakios-mt6768 README](https://github.com/oosexgang/hakios-mt6768/blob/main/README.md)

### P3 — 中期参考

**⑫ GPU 上层可复用代码：panfrost / Mesa / AIR→NIR**
- hakios-mt6768 的 Mali-G52 是 **panfrost**（旧架构），我们是 **panthor/CSF**（新架构），
  但上层（IOKit user client 形状、Mesa 集成）可参考。
- 出处：[hakios-mt6768 kernel/README.md](https://github.com/oosexgang/hakios-mt6768/blob/main/kernel/README.md)

**⑬ Metal 的两条路径（如果将来要 GPU 加速 UI）**
- HakiOS 有两条：`hakios-metal`（直接写 Adreno PM4，100~104 fps）和 `metal2vulkan`（UniXNU 的栈，50~57 fps）。
  我们是 Mali，两条都不适用，但**测量方法和判据可以抄**（`tools/frames.py`、`metal_fps=`）。
- 出处：[docs/reference/metal.md](https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/reference/metal.md)

### 不建议采用的

- **「用 iOS 驱动 + 兼容层」**：没有一家这么做，且技术上门槛更高。见 §2.4。
- **替换 LK 的 dtb**：pmOS wiki 明确说会让 LK 自己崩。见 §4.4。
- **`mcbin` 类「全内存跑、完全不写 flash」**：我们已经在 flash 里了（mtkloader 在 boot_a），
  回退成本高，但可以**新设备**时采用。
- **直接抄 redwood 的地址/rail**：HakiOS 自己说这是 "the last thing you want"。见 §2.2(a)。

---

## 7. 所有引用的 URL

### 同类项目
1. https://github.com/oosexgang/hakios-xnu-kernel — HakiOS 上游（README）
2. https://github.com/oosexgang/hakios-xnu-kernel/blob/main/README.md — 同上
3. https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/index.md — 功能表 / 三层架构
4. https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/comparison.md — 四家横向对比（本项目写的，注意立场）
5. https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/porting/index.md — 移植顺序、驱动分层、done 判据
6. https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/concepts/architecture.md — 启动链、写入了什么
7. https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/install-safe.md — 分级安装与门禁、"the phone that went quiet"
8. https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/dual-boot.md — 双系统槽位与菜单
9. https://github.com/oosexgang/hakios-xnu-kernel/blob/main/docs/reference/metal.md — Metal 两条路径
10. https://github.com/oosexgang/hakios-xnu-kernel/tree/main/hakios-metal — Adreno Metal 实现
11. https://github.com/dipa-studio/ios-sweet — ios-sweet（README）
12. https://github.com/dipa-studio/ios-sweet/blob/main/README.md — 同上，启动链、日志环、boot-args
13. https://github.com/UniXNU/UniXNU — UniXNU（README）
14. https://github.com/UniXNU/UniXNU/blob/main/README.md
15. https://github.com/UniXNU/AdrenoMetal — Adreno 上的 Metal 前端
16. https://github.com/ry-diffusion/Bergamota — Bergamota
17. https://github.com/oosexgang/hakios-mt6768 — 联发科先例（MT6768）
18. https://github.com/oosexgang/hakios-mt6768/blob/main/README.md
19. https://github.com/oosexgang/hakios-mt6768/blob/main/device.conf — 各子系统真实状态
20. https://github.com/oosexgang/hakios-mt6768/blob/main/kernel/README.md — 驱动清单
21. https://github.com/oosexgang/hakios-mt6768/blob/main/docs/metal-x6826b-plan.md — Mali + panfrost + Metal 计划
22. https://andgeek.jizhiku.net/archives/2571 — Redmi K60 Ultra（二手，未确认）
23. https://andgeek.jizhiku.net/archives/2440 — 天玑 9000 + UniXNU（二手，未确认）
24. https://eshraqism.com/blog/running-ios-16-5-on-android-hackios-explained — 技术拆解（含明显错误，慎用）
25. https://droid.tools/run-ios-on-android-hackios-ports — 新闻报道
26. https://www.phonearena.com/news/someone-successfully-booted-ios-on-an-android-device_id184233 — 新闻报道
27. https://zendot.org/en/posts/oosexgang-hackios-xnu-kernel — HakiOS 转述

### GPU / CSF / GPUEB
28. https://github.com/MT6895-Mainline/linux — xaga mainline 内核（默认分支 `7.2-mt6895-xiaomi-xaga`）
29. https://github.com/MT6895-Mainline/linux/commit/0e693c8cd2436b1a5995d884ef28ef3f14b649bd — **GPUEB 上电握手（关键）**
30. https://github.com/MT6895-Mainline/linux/commit/4e2c2ae8a1ddcd79019444c03d03b44771cb29cc — GPUEB-backed devfreq / stacks / VSTACK
31. https://github.com/MT6895-Mainline/linux/commit/be527d1f925e47dd090fd81d1592017c97ebd578 — TILER_OOM
32. https://github.com/MT6895-Mainline/linux/commit/b2fb495073b1cef005090dd8d8fb086fb83936e0 — **只保 mfg1 常开（实测）**
33. https://github.com/MT6895-Mainline/linux/commit/7ec259b064fc — GPU/UFS 时钟与域
34. https://github.com/MT6895-Mainline/linux/commit/16f1bf23e6bd — 去掉 clk_ignore_unused 的后果
35. https://github.com/MT6895-Mainline/linux/commit/5147314052c3e09c4746c86e44f7cbd3dfec8c59 — GPU OPP + devfreq 顺序
36. https://github.com/MT6895-Mainline/linux/blob/7.2-mt6895-xiaomi-xaga/drivers/gpu/drm/panthor/panthor_fw.c — 带 GPUEB 握手的 panthor_fw
37. https://github.com/MT6895-Mainline/linux/blob/7.2-mt6895-xiaomi-xaga/drivers/gpu/drm/panthor/panthor_device.c — 第二处握手 + pm_runtime_forbid
38. https://github.com/MT6895-Mainline/linux/blob/7.2-mt6895-xiaomi-xaga/drivers/gpu/mediatek/gpueb/gpueb_power.c — 上电协议
39. https://github.com/MT6895-Mainline/linux/blob/7.2-mt6895-xiaomi-xaga/drivers/gpu/mediatek/gpueb/include/gpufreq_ipi.h — IPI 消息结构
40. https://github.com/MT6895-Mainline/linux/blob/7.2-mt6895-xiaomi-xaga/include/linux/soc/mediatek/mt6895_gpueb.h — 对外 API
41. https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/panthor/panthor_fw.c — 上游 MCU 启动
42. https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/panthor/panthor_device.c — 上游初始化顺序
43. https://github.com/torvalds/linux/blob/master/drivers/gpu/drm/panthor/panthor_gpu.c — L2 上电与 coherency 顺序
44. https://wiki.nura.eco/wiki/Xiaomi_Redmi_Note_11T_Pro_(%2B)_/_POCO_X4_GT_/_Redmi_K50i_(xiaomi-xaga) — **xaga 的 pmOS/Nura wiki（GPU 段、Preloader Mode、dtb 警告）**

### boot 链与救援
45. https://wiki.nura.eco/wiki/Lk2nd — lk2nd：Android boot image 承载任意内核；`fastboot oem log`
46. https://github.com/MT6895-Mainline/initramfs — xaga initramfs（`BOOT_PARTITION=/dev/sdc86`）
47. https://github.com/MT6895-Mainline/quirks — xaga 的 UCM/mic 切换

### 黑屏取证
48. https://github.com/MT6895-Mainline/linux/commit/d76b79da1864 — **ramoops 地址选错的教训**
49. https://github.com/MT6895-Mainline/linux/commit/e7481ca0abf4 — **改为继承 LK cmdline 的 ramoops**
50. https://github.com/MT6895-Mainline/linux/commit/5788fc18500a — mini-rdump 标记导致 LK 不再记录
51. https://github.com/MT6895-Mainline/linux/commit/7b02de532b74 — **WDT 外部复位烂掉 ramoops**
52. https://github.com/MT6895-Mainline/linux/blob/7.2-mt6895-xiaomi-xaga/arch/arm64/boot/dts/mediatek/mt6895-xiaomi-xaga.dts — `log_store` / `mblock-38-me_GPUEB_SHARED`
53. https://github.com/MT6895-Mainline/linux/blob/7.2-mt6895-xiaomi-xaga/arch/arm64/boot/dts/mediatek/mt6895.dtsi — GPUEB 节点、GPU 节点
54. 本地：`/home/lizi/IOS/repo/hakios-mt6895/docs/DEBUGGING.md` — 我们自己的 logbook 机制
55. 本地：`/home/lizi/IOS/repo/hakios-mt6895/docs/GPU-STATE-MINI.md` — 我们的 GPU 现状

---

## 附录 A：没找到的（如实记录）

1. **没有找到任何一篇公开材料记录「Mali CSF MCU 收到 `MCU_CONTROL=AUTO` 后一条指令都不执行」的根因。**
   搜过的词：`Mali CSF MCU boot`、`panthor firmware init`、`MCU_CONTROL AUTO`、`CSF firmware MCU_STATUS`、
   `Mali G610 MCU not executing`、`shader_ready stays 0`。找到的最接近的是 §3.3/§3.7/§3.8 三条间接成因。
2. **没有找到 MT6895（天玑 8100）上的第二个 XNU/Darwin 移植。**
   `hakios-mt6768` 是联发科唯一先例，且是 Helio G85（Mali-G52，panfrost 架构，不是 CSF）。
   我们用 CSF 的 G610，**是这个方向上的第一次**。
3. **没有找到 `read-lastboot-log.sh` 的源码**（linux / quirks / initramfs 三仓库均 404）。
   机制描述清楚，但脚本要自己写。
4. **没有找到 xaga 上「3.5mm 耳机 accdet」的任何外部记录。**
   pmOS wiki 只写 Audio "Works"、mic 与 3.5mm 耳机共用一条 pipe 需要 daemon 切换
   （`quirks/mic/xaga-mic-switch.c`），没有 accdet 寄存器级细节。
   → 我们「从未读到过真机寄存器」这件事，**外部没有现成答案**。
5. **没有找到 HakiOS/Bergamota 关于「黑屏怎么取证」的专门文档**（ios-sweet 和 MT6895-mainline 有）。
6. **Bergamota 的 arm64e/指针认证方案没有细看**（它针对 Darwin 18，我们是 16.5，代际不同）。
7. **未确认**：Redmi K60 Ultra 的「显示苹果 logo」只有二手文章，无仓库、无日志。
8. **未确认**：`0x13c40000` 的「禁读」是我们实测的经验，外部资料只说它在时钟门后。
   两者不矛盾，但「门在哪、怎么开」外部没有直接给出（GPUEB 节点没有 `clocks` 属性）。

## 附录 B：本地已下载的原始材料

- `raw/mt6895.dtsi`（7254 行）— SoC 级 DT
- `raw/mt6895-xiaomi-xaga.dts`（1554 行）— 板级 DT

关键行号索引：
| 内容 | 文件:行 |
|---|---|
| `gpueb@13c40000` 节点 | `mt6895.dtsi:2650` |
| `gpueb_mem_table` / 名字表 | `mt6895.dtsi:2711-2714` |
| "Without MFG2-12 ... MCU firmware hangs" 注释 | `mt6895.dtsi:2730-2732` |
| `gpu@13000000` 节点 | `mt6895.dtsi:2734` |
| "no power-domains" 注释 | `mt6895.dtsi:2745` |
| 三条时钟 core/coregroup/stacks | `mt6895.dtsi:2757-2760` |
| `&gpueb { gpueb_mem_addr = 0x7c600000 }` | `mt6895-xiaomi-xaga.dts:621-623` |
| `mblock-38-me_GPUEB_SHARED` | `mt6895-xiaomi-xaga.dts:200` |
| `mblock-6-log_store` @ `0x7ffbf000` | `mt6895-xiaomi-xaga.dts:387-390` |
| ramoops 说明与 `mblock-9-ramoops` | `mt6895-xiaomi-xaga.dts:363-380` |
