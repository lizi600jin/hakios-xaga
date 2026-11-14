# ★ 可能推翻 19 轮判断：PDC 早开会精确产生我们的症状

> 时间：2026-11-14 22:40
> 证据来源：`MT6895-Mainline/linux` 的 `clk-mt6895-mfgcfg.c` 原始注释（我亲自核实）
> 交叉核对：我们自己的历史日志

---

## 0. 一句话

`MT6895-Mainline` 的作者**故意注释掉了 PDC 使能**，理由逐字描述了我们 19 轮看到的症状。
而我们的驱动在默认路径里**无条件开了 PDC**。

---

## 1. mainline 的原始注释（`clk-mt6895-mfgcfg.c:209-214`）

```c
	/* XAGA EXPERIMENT: do NOT enable PDCv2 here. With PDC enabled, the GPU
	 * takes over MFG4-12 and powers them off because no shader power request
	 * is active yet, leaving the domains stuck in 0x100d (requested but no
	 * PWR_ACK). Keep them software-powered via scpsys instead.
	 */
	/* mt6895_mfg_pdc_enable(base); */
	mt6895_mfg_hwdcm_enable(base);
	mt6895_mfg_acp_enable(base);
	mt6895_mfg_gpm_enable(base);
```

以及 `:232` 起的下一段注释：

```c
/* Enable PDCv2 after the shader power request has been asserted, so the GPU
 * power state machine can complete the shader MTCMOS handshake without the
 * ...
```

→ **PDC 不是不能用，而是不能"早"用**：必须在 shader 电源请求已经置起之后。

## 2. 我们的日志（`xaga-logbook.bin`）

```
[ 37.800506]: mali: pdc enabled on 17 registers
[ 37.800513]: mali: pdc: sc0 0x2000001 sc9 0 cg 0x1 st0 0x2000001 st4 0x2000001 st6 0
[ 38.556696]: mali: after pdc: mfg5 0x100d mfg6 0x100d mfg10 0x100d mfg12 0x100d
[ 38.676639]: mali: pwr status 0x1fc / 0x1fc (scpsys 0xF34/0xF38), gpufreq 0x4 (0xF3C); want 0x3ffc
```

**`0x100d` —— 与 mainline 注释里的 `0x100d` 逐字相同。**

`0x100d` 的位含义（按 mainline 描述）：bit0=PWR_ON 已请求，bit2/bit3+ = **没有 PWR_ACK**。

## 3. 我们的驱动做了什么

`hakios.mali610.c:4472`（在 `clock_26m()` 之后、**默认路径**里）：

```c
printf("mali: pdc enabled on %u registers\n", pdc_enable());
```

`pdc_enable()` 定义在 `:1854`，它写 **17 个 PDC 寄存器**（日志里"pdc enabled on 17 registers"）。

**注意**：`mali_pdcauto` 开关（`:551`，默认 0）只控制 `:1776` 那条"只开 mfg1"的分支。
`:4472` 这个 `pdc_enable()` 调用是**无条件**的。

→ **我们的默认路径就是"早开 PDC"，正是 mainline 说会坏事的那条。**

## 4. 因果链

```
我们：clock_26m() 之后 → 无条件 pdc_enable()（写 17 个寄存器）
      ↓
GPU 的电源状态机接管 MFG4-12
      ↓
此刻 shader 电源请求还没置起
      ↓
PDC 认为没人要电 → 把 MFG4-12 关掉
      ↓
域卡在 0x100d（请求发了但无 ACK）
      ↓
shader core 永远 ready 不了
      ↓
MCU 固件在写共享接口版本字之前挂住
      ↓
哨兵 0xff5aa5f0 从不被改写、JOB_INT_STATUS bit31 从不起、MCU_STATUS 恒 1
```

**四条症状全部解释。**

## 5. 为什么这条之前没被发现

项目记录里有一条「MFG7..12 未上电是 PDCv2 设计，不是缺陷」，把责任推给了 PDC 的"设计"。

但从 mainline 的实测注释看：**PDC 确实会关掉它们，但那是"没人请求"时的正常行为；
在 shader 请求还没置起时就开 PDC，是时机错误。**

区别在于：
- 如果 PDC 在**正确时机**开（shader 请求已置起）→ 它应该把域**上电**，而不是关掉
- 我们在**错误时机**开（请求未置起）→ 它把域关掉，然后卡死

## 6. 建议的验证（单变量，一次刷机）

### 最直接的实验：默认路径不调 `pdc_enable()`

改法：把 `:4472` 那行改为受开关控制，默认关：

```c
if (!gMaliNoPdc) {
    printf("mali: pdc enabled on %u registers\n", pdc_enable());
    pdc_report();
}
```

新增 boot-arg `mali_nopdc=1` 来跳过。

**这正是 mainline 当前的状态**（`mt6895_mfg_pdc_enable(base);` 被注释掉），
而他们的 GPU 是**能跑的**。

### 预期结果

- **`mfg5/6/10/12` 不再是 `0x100d`**（要么上电、要么保持某个确定的未上电状态）
- `SHADER_READY` 可能变
- **如果哨兵被改写了 → 直接突破**

### 如果这条不通

说明还有别的前置条件（GPUEB 的 IPI）。两条可以叠加：
1. 关掉早开的 PDC
2. 向 GPUEB 发 `CMD_POWER_CONTROL`

## 7. 这条与 GPUEB 线索的关系

**两者不矛盾，可能是同一件事的两个面**：

- GPUEB 是电源状态机的**仲裁者**（子代理结论）
- PDC 是 GPU 侧的**状态机**
- 如果我们自己开了 PDC，又没跟 GPUEB 打招呼 → **两个状态机互相不知道对方**
- mainline 的做法：**不用 GPU 侧 PDC，让 scpsys 软件管**，同时通过 GPUEB 要电

所以最可能的完整解法是：
```
关掉早开的 PDC（让 scpsys 软件管）
  + 向 GPUEB 发 CMD_POWER_CONTROL（要电）
  + 在正确时机（如果需要）再开 PDC
```

## 8. 需要注意的边界

- 这份 `clk-mt6895-mfgcfg.c` 注释来自 mainline 移植者，**不是厂商文档**
- 但它有**"XAGA EXPERIMENT"标注 + 精确的 `0x100d` 数字**，说明是**实测得出**的
- 而且它与我们的日志 `0x100d` **逐字吻合**，这种巧合概率很低

## 9. 另外两条 mainline 给的具体数值（可能有用）

### 9.1 MFG PLL 的 VCO 下限必须是 2000 MHz

`clk-mt6895.c:2721-2727`（子代理转述，我未亲自核）：
通用 1500 MHz 下限会让 852 MHz 算出 postdiv=2（VCO=1704M）→ **PLL 锁不住**；正确是 postdiv=4 → VCO=3408M。

我们的驱动 `pll_300mhz()` 目标 300 MHz，是否踩这个坑要算一下。

### 9.2 耳机走 ADDA_DL_CH3/CH4，不是 CH1/CH2

`quirks/ucm/mt6895-mt6368/HiFi.conf:45-54`（子代理转述）：
可照抄的 mixer 序列是 `RCV Mux=Open` + `HPL/HPR Mux='Audio Playback'` + `Headset Volume 20,20` + **`ADDA_DL_CH1..CH4` 四条全开**。

原因：codec 的 AIF 数据输入焊在 `AUD_DAT_MOSI2` pad 上（即 ADDA_CH34 通道）。

→ 这条要与我们 `PLAYBACK-SEQUENCE.md` 反汇编的结论对照。
