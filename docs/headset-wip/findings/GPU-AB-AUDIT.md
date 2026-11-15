# A'/B' 审计：PTE 与电源层面都没有错

> 2026-11-15 17:20，DSH。**离线分析 trial 700 日志**，无设备操作。
> 按 6-Astra 建议的两条方向：A'（GPUEB 责任链）、B'（内存可见性对到精确对象）。

---

## 0. 一句话

**B' 做完了：四页的 PTE 属性、backing PA、XN 位、缓存索引全部正确。**
**A' 给出一条关键对照：`gpu_id` 在上电前读 0、上电后读真值 —— 电源已就绪。**

**两条都没有发现错误。问题不在这两层。**

---

## A'. GPUEB / 电源责任链

### A.1 主线有两处 GPUEB 调用，第一处更早

`panthor_device.c:257-271`（**在 HW init 之前**）：

```c
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
```

第二处是 `panthor_fw.c:1131-1146`（MCU 启动前），已在
`GPUEB-POWER-ON-IS-MANDATORY.md` 里记过。

### A.2 🔴 但我们读到了真 ID —— 与主线的「全零」预言不符

**主线的理由**：没有 `power_on`，panthor 会读到**全零** ID/features。

**我们的实测**（trial 700）：

| 时刻 | `gpu_id` |
|---|---|
| `21.842646`（loader 留下的状态） | **`0`** |
| `33.965658`（我们自己的上电之后） | **`0xa8670004`** ✅ |

**所以「全零」这个前提在我们这里不成立 —— 我们自己的上电已经让 ID 可读了。**

### A.3 但这不能证明 MCU 能被唤醒

**必须严格区分两层：**

| 层 | 状态 | 证据 |
|---|---|---|
| **寄存器可读** | ✅ **全通** | `gpu_id 0xa8670004`、`l2 0x7130306`、`shader 0x130013`、`pwr status 0x1fc`、`gpufreq 0x3ffc` |
| **MCU 执行** | ❌ **全断** | `MCU status 1`、`version word 0xff5aa5f0`、`the MCU did not boot` |

**寄存器可读 ≠ 核在跑。**

### A.4 所以 A' 的剩余问题

**我们还不知道**：
- GPU 的**执行域**（MCU 所在）是否与**寄存器域**分开供电
- 如果是分开的，那 `gpu_id` 可读不能说明 MCU 域已上电

**这是 A' 没有回答的部分，需要硬件文档或 vendor 电源表。**

---

## B'. 内存可见性：四页对到精确对象

### B.1 四页的 walk（trial 700 实测）

```
walk 0:          L0 0x19ec5c403 L1 0x19ec60403 L2 0x19ec64403 L3 0x4000019ec184c3 page
walk 0x1000000:  L0 0x19ec5c403 L1 0x19ec60403 L2 0x19ec38403 L3 0x19ed344c3    page
walk 0x2000000:  L0 0x19ec5c403 L1 0x19ec60403 L2 0x19ecbc403 L3 0x4000019ec78443 page
walk 0x4000000:  L0 0x19ec5c403 L1 0x19ec60403 L2 0x19f12c403 L3 0x4000019f0a8457 page
```

### B.2 PTE 解码（我独立解码，不是转述）

| 页 | L3 leaf | 低 12 位 | **bit54 XN** | attr idx | backing PA |
|---|---|---|---|---|---|
| **`VA 0`** 向量表 | `0x4000019ec184c3` | `0x4c3` | **1（不可执行）** | **0 (CACHED)** | `0x19ec18000` |
| **`VA 0x1000000`** 代码 | `0x19ed344c3` | `0x4c3` | **0（可执行）** | **0 (CACHED)** | `0x19ed34000` |
| **`VA 0x2000000`** 栈 | `0x4000019ec78443` | `0x443` | 1 | 0 (CACHED) | `0x19ec78000` |
| **`VA 0x4000000`** 共享 | `0x4000019f0a8457` | `0x457` | 1 | **5 (NON-CACHEABLE)** | `0x19f0a8000` |

### B.3 四项检查全部通过

| 检查 | 结果 |
|---|---|
| **XN 位正确** | ✅ 只有代码页 `bit54 = 0`（可执行），其余三页都是 1 |
| **backing PA 无别名** | ✅ 四个 PA 各不相同（`0x19ec18000` / `0x19ed34000` / `0x19ec78000` / `0x19f0a8000`） |
| **缓存属性正确** | ✅ 代码/栈/向量表用 `idx 0`(CACHED)，共享页用 `idx 5`(NC) |
| **AP 位正确** | ✅ 向量页与代码页 `AP=11`(只读)，栈与共享页 `AP=01`(读写) |

> 注意：`VA 0` 与 `VA 0x1000000` 的低 12 位都是 `0x4c3`，但**它们不是同一属性** ——
> 区别在 **bit54（XN）**，不在低 12 位里。这是解码时容易误判的陷阱。

### B.4 B' 的结论

**PTE 层面没有发现任何错误。** 四页的地址、权限、可执行性、缓存属性都对。

**所以「页表映射错」这一类解释可以排除。**

---

## 汇总：A' 和 B' 都没找到错

| 方向 | 结论 |
|---|---|
| **A' 电源** | 寄存器域已上电（ID 可读）；**MCU 执行域是否分开供电未知** |
| **B' 页表** | **四页全部正确**，PA/XN/缓存/AP 都对 |

**这两层是干净的。** 问题在：
- MCU 执行域的供电（A' 未回答的部分）
- 或取指路径上更深的环节

---

## 下一步能做什么

| # | 动作 | 前提 |
|---|---|---|
| 1 | 查 vendor 电源表，看 MCU 域与寄存器域是否分开 | 需要 vendor gpufreq 源码里的域列表 |
| 2 | 若分开，需要找到唤醒 MCU 域的路径（可能就是 GPUEB） | 依赖 1 |
| 3 | 用 `0x13FBF0B0` 之外的方式判断 MCU 是否真的在跑 | 目前 `MCU_STATUS` 不可信 |
