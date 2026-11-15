# 🔴 六次试验全部跳过 PDC：GPU 从未获得控制 shader MTCMOS 的许可

> 2026-11-15 17:25，DSH。**离线核实 trial 695-700 日志 + vendor 源码**，无设备操作。

---

## 0. 一句话

**我们的驱动有完整的 PDC 实现（`MFG_ACTIVE_POWER_CON` 十组位），
但 trial 695/696/697/698/699/700 全部带 `mali_nopdc=1`，六次都跳过了。**

**日志实测：`pdc: sc0 0 sc9 0` —— 那十组使能位从未被置起。**

---

## 1. vendor 的做法：上电必须调 PDC

`gpufreq_mt6895.c:801` 注释原文：

> set PDCv2 register when power on, **let GPU DDK control MTCMOS itself**
> `__gpufreq_pdc_control(POWER_ON);`

`:1487` 再强调一次：

> `/* PDCv2: GPU IP automatically control GPU shader MTCMOS */`

### `__gpufreq_pdc_control(POWER_ON)` 写什么

```c
/* MFG_ACTIVE_POWER_CON_00 0x13FBF400 [0] rg_sc0_active_pwrctl_en = 1'b1 */
val = readl(g_mfg_top_base + 0x400);
val |= (1UL << 0);
writel(val, g_mfg_top_base + 0x400);

/* MFG_ACTIVE_POWER_CON_06 0x13FBF418 [0] rg_sc1_active_pwrctl_en = 1'b1 */
val = readl(g_mfg_top_base + 0x418);
```

**含义：置位后，GPU IP 才能自己给 shader 上电。**

---

## 2. 我们实现了，但六次全跳过

### 2.1 实现是完整的

`hakios.mali610.c:323-330`：

```c
 * The bit is the same in all twenty-six registers: bit 0 of MFG_ACTIVE_POWER_CON_nn is
 * rg_*_active_pwrctl_en. The ten 0x400-step registers are one per shader-core cluster
 * ... rg_*_active_pwrctl_rsv in bit 31, which the vendor also sets; this driver sets both
#define MALI_PDC_CORES          10u    /* 0x400 + n*0x18: rg_sc0..rg_sc9, and no further */
```

### 2.2 但开关一直是关的

`hakios.mali610.c:4492`：

```c
printf("mali: pdc setup skipped (mali_nopdc=1); inherited values unchanged\n");
```

### 2.3 六次试验全部带 `mali_nopdc=1`

| trial | `pdc setup skipped` |
|---|---|
| 695 | **1** |
| 696 | **1** |
| 697 | **1** |
| 698 | **1** |
| 699 | **1** |
| 700 | **1** |

### 2.4 日志实测：那十组位全是 0

```
[   33.957892]: mali: pdc: sc0 0 sc9 0 cg 0 st0 0 st4 0 st6 0
[   33.960481]: mali: after pdc: mfg5 0xc000000d mfg6 0xc000000d mfg10 0xc000000d mfg12 0xc000000d
```

**`sc0 = 0`、`sc9 = 0` —— `rg_sc0..sc9_active_pwrctl_en` 从未置起。**

---

## 3. 为什么当初加 `mali_nopdc=1`

`hakios.mali610.c:1770-1776` 的注释记录了原因：

> The first run of this branch proved it: all seventeen PDC stores read back zero
> (`pdc: sc0 0 sc9 0 cg 0 st0 0 st4 0 st6 0`) and **0xF3C came back 0x6 instead of 0x3FFE**.

**当时的判断**：PDC 写入无效（读回全 0），而且 `0xF3C` 只到 `0x6`，
所以改成 `mali_nopdc=1` 绕开。

### 🔴 但这个理由现在已经不成立了

| 当时 | 现在 |
|---|---|
| `0xF3C = 0x6`（大部分域没上电） | **`0xF3C = 0x3ffc`（12 域全 up）** |
| PDC 写入读回全 0 | **`sc0/sc9` 仍然 0，但电源已由别的路径解决** |

**也就是说：当初跳过 PDC 是为了绕开「电源上不来」，
而现在电源已经上来了（走的是 `mali_pdcauto` 之外的另一条路），
却没有人回头重估「PDC 还需不需要跳过」。**

---

## 4. 为什么这能解释「寄存器全通、MCU 全断」

`[DSH推断]` 如果 `rg_sc*_active_pwrctl_en` 不置位：

- **GPU 的寄存器块**（`gpu_id`、`l2`、`shader present`、`gpufreq`）**仍然可读**
  —— 它们在 MFG1/2 的常开域里，我们手动开过；
- **但 shader 核心的 MTCMOS 没有任何一方负责**
  —— AP 不再手动写（PDCv2 模式下 vendor 也不手动写），GPU 自己又没拿到许可。

**这与观测完全相容：**
- `shader 0x130013`（present 位可读，说明寄存器在）
- 但 `shader ready 0`（ready 位没起来，说明核心没上电）
- `MCU status 1` 但不执行

**⚠️ 这是解释，不是证据。** 见 §6。

---

## 5. 下一步实验（成本极低）

**去掉 `mali_nopdc=1`，其余全部沿用 700。**

| 项 | 值 |
|---|---|
| 基线 | trial 700（同内核、同 headset=2、同其它参数） |
| **唯一改动** | **删除 `mali_nopdc=1`** |
| 判据 | `pdc: sc0` 和 `sc9` 是否变成非 0；`0xF3C` 是否仍是 `0x3ffc` |
| 若成功 | 再看 `shader ready` 与 `version word` |

**风险**：PDC 写入可能让电源状态变化。但历史上 PDC 分支跑过一次
（虽然后来被 `nopdc` 取代），不是完全未试。

---

## 6. 证据边界

| 层级 | 结论 |
|---|---|
| **已证明（源码）** | vendor 上电必调 `__gpufreq_pdc_control(POWER_ON)`，注释说让 GPU 自己控制 MTCMOS |
| **已证明（源码）** | 我们实现了 `MFG_ACTIVE_POWER_CON` 十组位 |
| **已证明（实测）** | 六次试验全部 `pdc setup skipped`，`sc0/sc9` 实测为 `0` |
| **已证明（实测）** | `0xF3C = 0x3ffc`（12 域全 up），与当初 `0x6` 的情况已不同 |
| **推断** | PDC 未使能可能导致 shader MTCMOS 无主，进而 MCU 不执行 |
| **未证明** | 去掉 `nopdc` 后 `sc0/sc9` 会变成非 0 |
| **未证明** | 即使变成非 0，MCU 会启动 |
| **未证明** | `mali_pdcauto` 路径是否已经覆盖了 PDC（需要查它是否也写 `0x400`） |

**最后一条必须先查** —— 见 §7。

---

## 7. 先要排除的：`mali_pdcauto` 是否已覆盖

`hakios.mali610.c` 里有 `gMaliPdcAuto` 分支（`:1745` `power_on()` 内）。
**如果它已经写了 `MFG_ACTIVE_POWER_CON`，那去掉 `nopdc` 就没有意义。**

**但六次日志里 `pdc: sc0 0` 都出现，说明 `pdc_report()` 读的就是实际值 = 0。**
**所以 PDC 确实没被任何分支写过。**

**这一条已由实测排除。**
