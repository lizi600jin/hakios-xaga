# GPUEB 线索 · 完整证据链（含与历史日志的交叉核对）

> 时间：2026-11-14 22:17
> 作者：dsh 主控（独立核实，每条都有可验证路径）

---

## 0. 一句话

**我们的驱动从来没有映射过 GPUEB**（历史日志原文 `gpueb before the address spaces: not mapped`），
而 panthor 在 MT6895 上**必须**先向 GPUEB 请求 GPU 上电，否则「reads all-zero ID/features」。

这是目前最可信的根因假设，且**与我们已有的 35 条排除结论不冲突**——因为那些尝试全都在 AP 侧直接调 GPU/SPM/时钟，
从来没有走过 GPUEB 这条路径。

---

## 1. 证据一：panthor 的两处 GPUEB 调用（我抓原始文件核实的）

分支：`7.2-mt6895-xiaomi-xaga`（**默认分支，不是 main/master**）

### 1.1 `drivers/gpu/drm/panthor/panthor_fw.c:1131-1143`

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

	gpu_write(fw->iomem, MCU_CONTROL, MCU_CONTROL_AUTO);    ← 紧跟在后面
```

文件头 `:17` 有 `#include <linux/soc/mediatek/mt6895_gpueb.h>`。

### 1.2 `drivers/gpu/drm/panthor/panthor_device.c:256-269`

```c
#if IS_ENABLED(CONFIG_MTK_GPUEB)
	/* MT6895: gpueb owns GPU power. Ask it to power on before touching any
	 * GPU registers, otherwise panthor reads all-zero ID/features.
	 */
	if (of_machine_is_compatible("mediatek,mt6895") &&
	    mt6895_gpueb_available()) {
		ret = mt6895_gpueb_power_on();
		if (ret) {
			dev_err(ptdev->base.dev,
				"Failed to request GPUEB power-on before HW init: %d\n", ret);
			goto err_rpm_put;
		}
	}
#endif

	ret = panthor_hw_init(ptdev);   ← 紧接着
```

位置：`panthor_device_init()` 内，`pm_runtime_resume_and_get()` 之后、`panthor_hw_init()` 之前。

---

## 2. 证据二：API 定义（`include/linux/soc/mediatek/mt6895_gpueb.h`，全文 20 行）

```c
int mt6895_gpueb_power_control(unsigned int power_on);
int mt6895_gpueb_power_on(void);
bool mt6895_gpueb_available(void);
int mt6895_gpueb_commit(unsigned int target, unsigned int oppidx);
```

**注意 `mt6895_gpueb_available()` 的行为**：只是 `of_find_compatible_node(NULL,NULL,"mediatek,gpueb")` 非空
→ **只判断 DT 有没有节点，不判断固件是否真在跑**。

---

## 3. 证据三：GPUEB 的共享内存地址（可靠、可读、不受 0x13c40000 限制）

`raw/mt6895-xiaomi-xaga.dts:200-204`（我下载到本地的原文）：

```dts
mblock-38-me_GPUEB_SHARED {
    reg = <0x00 0x7c600000 0x00 0x200000>;
    no-map;
    compatible = "mediatek,me_GPUEB_SHARED";
};
```

`:617-623`：

```
/* GPUEB shared memory: the live FDT reserves 2MB at 0x7c600000 as
 * mblock-38-me_GPUEB_SHARED. gpueb_reserved_mem.c can use it directly
 * when gpueb_mem_addr/size are present on the gpueb node.
 */
&gpueb { gpueb_mem_addr = <0x0 0x7c600000>; gpueb_mem_size = <0x0 0x200000>; };
```

结构（子代理从 `gpufreq_ipi.h` 读的）：
- 前 `0x1000` = `MEM_ID_GPUFREQ`（4KB），其中前 `0x400` = status 区，余下是 debug
- `0x180000` = `MEM_ID_LOG`

**status 区有 32 个可读字段**：`cur_oppidx_gpu/stack`、`cur_fgpu/fstack`、`cur_vgpu/vstack`、
`power_control`、`dvfs_state`、`shader_present`、`power_count`、`avs_enable` 等。

→ **这是个现成的健康检查面板，而且 0x7c600000 不在一碰就挂的区域。**

---

## 4. 证据四：与历史日志的交叉核对（最关键）

### 4.1 我们的历史日志里，GPUEB 从未被映射

```
$ strings xaga-logbook.bin | grep -i gpueb
[ 38.768691]: mali: gpueb before the address spaces: not mapped
[ 51.445817]: mali: gpueb after the failed start: not mapped
```

两次检查都是 **not mapped**。驱动里有 `gMaliGpueb`（`mali_gpueb=1` 才映射，`:466`），
历史运行里**没有传这个参数**。

### 4.2 但电源域其实上去了

```
[ 38.676639]: mali: pwr status 0x1fc / 0x1fc (scpsys 0xF34/0xF38), gpufreq 0x4 (0xF3C); want 0x3ffc
[ 37.800506]: mali: pdc enabled on 17 registers
[ 37.800513]: mali: pdc: sc0 0x2000001 sc9 0 cg 0x1 st0 0x2000001 st4 0x2000001 st6 0
[ 38.556696]: mali: after pdc: mfg5 0x100d mfg6 0x100d mfg10 0x100d mfg12 0x100d
```

`0xF3C = 0x4` → bit2 = **MFG1 已上电**。

这与子代理查到的 commit `b2fb495073b1` 完全对得上（原文）：

> Releasing them one at a time on xaga showed **exactly one domain matters: mfg1**,
> the MFG cluster's top domain.

且该 commit 说 mfg1 关掉时 panthor 会报：
> "CSG 0 update request timedout", "Failed to stop MCU", "**AS_ACTIVE bit stuck**"

**我们 mfg1 是开的（0x4），MCU 依然不执行** → 说明 mfg1 是必要条件但**不是充分条件**。
缺的那一步正是 GPUEB 的 `CMD_POWER_CONTROL`。

### 4.3 PDC 只写进去了一部分

`pdc: sc0 0x2000001 sc9 0 cg 0x1 st0 0x2000001 st4 0x2000001 st6 0`

`sc9` 和 `st6` 读回 **0**，说明 PDC 的 17 个寄存器里至少 2 个没写进去。
驱动注释（`:1776` 起）也记了同一件事：
> all seventeen PDC stores read back zero (pdc: sc0 0 sc9 0 cg 0 st0 0 st4 0 st6 0) and 0xF3C came back 0x6 instead of 0x3FFE

---

## 5. 证据五：GPUEB 固件不是 Linux 加载的

子代理在 `gpueb_ipi.c` / `gpueb_init.c` / `gpueb_power.c` 里 grep `firmware|request_firmware|clk_get|clk_enable|reset_control`
→ **零命中**。gpueb DT 节点**没有 `clocks` 属性、没有 `power-domains`**。

→ **GPUEB 是 LK（或更早阶段）加载并启动的，Linux 只是通过 mailbox 跟它说话。**

**对我们的含义**：如果 LK 启动了 GPUEB 且它存活，那它现在应该还在跑（这正是 `gpueb_watch()` 要验的）。
但「活着」≠「已经给 GPU 上电」—— `CMD_POWER_CONTROL` 仍要发。**这两个假设必须分开验证。**

---

## 6. 三步验证方案（单变量，按顺序）

### 第 1 步：只映射 GPUEB 并读状态（不写任何东西）

验证目标：GPUEB 还活着吗，它认为自己管着 GPU 吗。

做法：
1. 打开 GPUEB 的时钟门（`0x13c40000` 在自己的门后面，没开门去读会挂死总线）
2. 映射 `0x13c40000`（0x24000）
3. 我们已有的 `gpueb_watch()` 读 `WDT_KICK(0x2061c)`——**如果它在变，说明固件在跑**
4. 映射 `0x7c600000`（2MB），读 status 区的 `power_control` / `shader_present` 字段
   —— **这是安全的，不受 0x13c40000 限制**

**判据**：
- WDT_KICK 在变 + status 有内容 → GPUEB 活着，走第 2 步
- WDT_KICK 死值 / status 全 0 → GPUEB 没跑，这条路要换方向（得在 loader 里拉起它）

### 第 2 步：只发 `CMD_POWER_CONTROL(POWER_ON)`

按 `gpufreq_ipi.h`：
- `CMD_INIT_SHARED_MEM = 0`（先发，告诉 EB 共享区在哪）
- `CMD_POWER_CONTROL = 6`
- `CMD_COMMIT = 7`（**这一步不要发**，源码明确警告 "Never commit OPP indexes from this path"）
- 消息 4 个 u32 槽，超时 `IPI_TIMEOUT_MS = 10000`
- `CMD_INIT_SHARED_MEM` 参数：`status_base = 0x7c600000`，`status_size = 0x400`，
  `debug_base = 0x7c600400`，`debug_size = 0xc00`

**判据**：`ret == IPI_ACTION_DONE`，且 `recv_msg.u.return_value` 非负。

### 第 3 步：再写 `MCU_CONTROL = AUTO`

**判据**：哨兵 `0xff5aa5f0` 被改写 / `JOB_INT_STATUS` bit31 升起。

---

## 7. 需要 sol 判断 / 落实的

1. **GPUEB 的时钟门在哪**（哪个寄存器、哪个 bit）—— 这是第 1 步的前提，必须先查到
2. **我们有没有 IPI/mailbox 的现成实现** —— `mtk-mbox` 那套在 XNU 里存在吗？还是得从零写
3. 如果 IPI 这套太重，**有没有更轻的路**：比如直接读 status 区看 GPUEB 认为 GPU 是什么状态
4. 如果 GPUEB 已经死了，**loader 能不能把它拉起来**（`mtk/loader/` 里有没有相关代码）

---

## 8. 与现有 35 条排除结论的关系

**不冲突，但需要重新审视一条。**

我们记录里有一条「MFG7..12 未上电是 PDCv2 设计，不是缺陷」。
子代理查到的 `b2fb495073b1` 说 **只保 mfg1 就够**，其余交给硬件。这条**支持**我们原来的结论。

但我们的历史日志显示 `0xF3C = 0x4`（只有 mfg1），MCU 仍然不动 ——
所以「mfg1 够了」对 **panthor + GPUEB** 成立，对 **我们手搓的路径** 不成立，
因为我们没做 GPUEB 握手。

**这正是关键差别。**

---

## 9. 未证实（不要当事实）

| 项 | 状态 |
|---|---|
| `mt6895_gpueb_power_on()` 的完整源码 | 未读（只知道 API 和行为描述） |
| `CMD_INIT_SHARED_MEM` 的确切消息布局 | 子代理转述，我未亲自核对 |
| GPUEB 的时钟门位置 | **未知**（第 1 步的阻塞点） |
| GPUEB 现在是否还活着 | **未知**（这正是第 1 步要测的） |
| 「MCU 不执行 = 缺 GPUEB 上电」 | **仍是假设**，未验证 |
| `mem_GPUEB_SHARED` 在我们设备的 DT 里是否有 | 未查（我的 dts 是从 Linux mainline 抓的，不是我们真机的 DT） |

最后一条要特别注意：**我下载的 dts 是 Linux mainline 移植者写的，不是我们设备的真实 DT。**
地址大概率一致（都来自同一条 LK），但**要以我们真机的 DT 为准**。

---

## 10. 真机 DT 核实（最终确认，2026-11-14 22:17）

前面第 9 节说「要以真机 DT 为准」，现在查了。

### 10.1 GPUEB 节点（真机 `/proc/device-tree/soc/gpueb@13c00000`）

```
compatible = mediatek,gpueb
reg = 0x13c40000 0x22000    ← gpueb_base
      0x13c5fd80 0x280      ← mbox0_base
      0x13c62004 0x4        ← mbox0_set
      0x13c62074 0x4        ← mbox0_clr
      0x13c62000 0x4        ← mbox0_send
      0x13c62078 0x4        ← mbox0_recv
reg-names = gpueb_base mbox0_base mbox0_set mbox0_clr mbox0_send mbox0_recv
```

**注意**：节点名叫 `gpueb@13c00000`，但 `reg` 第一项是 `0x13c40000`。
**节点名和基址不一致，这是联发科 DT 的常见写法，基址以 `reg` 为准。**

### 10.2 与我们驱动的对照

| 项 | 真机 DT | 我们驱动 | 一致？ |
|---|---|---|---|
| gpueb_base | `0x13c40000` | `MALI_GPUEB_PHYS 0x13c40000` (`:261`) | ✅ |
| size | `0x22000` | `MALI_GPUEB_SIZE 0x24000` (`:262`) | 略大，无害 |
| mbox0_recv | `0x13c62078` | `MALI_GPUEB_MBOX_RECV 0x22078` (`:266`) | ✅（`0x13c62078-0x13c40000 = 0x22078`） |

**我们的常量是对的。** 缺的不是地址，是**握手**。

### 10.3 GPUEB 共享内存（真机 DT）

```
/proc/device-tree/reserved-memory/mblock-38-me_GPUEB_SHARED
  reg        = 0x00000000_7c600000  0x00000000_00200000    (base 0x7c600000, 2MB)
  compatible = mediatek,me_GPUEB_SHARED
```

**与 Linux mainline 移植的 dts 逐字节一致。**

→ 这个块是**真实存在、可以直接读**的，而且**不在 `0x13c40000` 那个禁区里**。

### 10.4 结论修正

第 9 节列的最后一个「未证实」项现在**已证实**：
`mem_GPUEB_SHARED` 在我们设备的 DT 里**确实有**，地址 `0x7c600000`，大小 2MB。

**第 1 步验证可以立刻做**：映射 `0x7c600000`（不需要开任何门，不需要碰禁区），
读 status 区的字段。如果 `power_control` / `shader_present` 有非零值，
就说明 **GPUEB 活着并且自认为在管 GPU**。

这比去碰 `0x13c40000` 安全得多，而且是同一份信息的来源。
