# 主线 Linux 在 xaga 上跑 panthor：它踩过的坑与我们不同

> 2026-11-15 16:50，DSH。**线上核实**，来源：
> <https://github.com/MT6895-Mainline/linux/tree/7.2-mt6895-xiaomi-xaga>
> 分支 `7.2-mt6895-xiaomi-xaga`，组织 `MT6895-Mainline`。

---

## 0. 一句话

**主线 Linux（内核 7.2）已经支持 xaga，并且用 panthor 跑 Mali-G610。
它踩过一个 GPU 故障并修好了 —— 但那个故障表现为 SError，我们的完全没有声音。**

**这不能直接给我们答案，但它给了三条硬事实。**

---

## 1. 组织与仓库

`MT6895-Mainline` 下有 11 个仓库，是一个完整项目：

| 仓库 | 用途 |
|---|---|
| `linux` | **内核，分支 `7.2-mt6895-xiaomi-xaga`** |
| `initramfs` / `rootfs` | 启动与根文件系统 |
| `quirks` | 兼容性处理 |
| `libcamera` / `vaapi-mtk-vcp` | 相机、视频加速 |
| `mtk-mod-tee-nogz` | TEE |
| `modemmanager-mtk-soc` / `mtk-ccci-userspace` | 基带 |
| `fpc1540-ta` / `libfprint` | 指纹 |

### 设备树确实存在

`arch/arm64/boot/dts/mediatek/`：

- `mt6895.dtsi`
- `mt6895-xiaomi-xaga.dts`
- `mt6895-xaga-usb-tcpm.dtsi`

`mt6895-xiaomi-xaga.dts` 开头：

```dts
/*
 * MT6895 (MediaTek Dimensity 8100) Xiaomi Redmi Note 11T Pro+ board
 *
 * Codename: xaga
 */
```

**compatible = "xiaomi,xaga", "mediatek,mt6895"** —— 就是这台机器。

---

## 2. GPU 节点（`mt6895.dtsi:2734-2765`）

```dts
gpu: gpu@13000000 {
    compatible = "arm,mali-valhall-csf";
    reg = <0x13000000 0x480000>;
    interrupts = <GIC_SPI 514 ...>, <GIC_SPI 513 ...>, <GIC_SPI 512 ...>;
    interrupt-names = "job", "mmu", "gpu";
    clocks = <&topckgen CLK_TOP_MFG_SEL_0_CK>,
             <&mfgcfg CLK_MFGCFG_BG3D>,
             <&topckgen CLK_TOP_MFG_SEL_1_CK>;
    clock-names = "core", "coregroup", "stacks";
    vstack-supply = <&mt6368_vbuck2>;
    operating-points-v2 = <&gpu_opp_table>;
    mali-supply = <&vdd_gpu>;
    #cooling-cells = <2>;
};
```

**`reg = <0x13000000 0x480000>` 与我们驱动的 `MALI_GPU_PHYS` + `MALI_GPU_SIZE` 一致。**

**注意：`/* Real power management stays with gpueb; no power-domains. */`
—— 主线也没有用 `power-domains`，电源归 GPUEB。**

---

## 3. 🔴 它踩过的坑：BG3D 时钟被内核关掉

`mt6895.dtsi:2743-2757` 的注释原文（**这是最重要的一段**）：

> `coregroup` is the **MFGCFG BG3D gate** ("subsys_mfg_cg" / gpufreq's MFG subsystem
> clock gating downstream): **a hw-on gate with no Linux consumer, so
> `clk_disable_unused()` switches it off while the GPU is running** and the resulting
> external abort surfaces as **an SError in `panthor_fw_ping_work()`**.
> panthor enables it in `panthor_device_resume()` and cuts it again in
> `panthor_device_suspend()`; the binding fixes the name order at core, coregroup, stacks.

### 拆开说

| 事实 | 内容 |
|---|---|
| 故障点 | **`panthor_fw_ping_work()`** —— 固件起来后的心跳 |
| 现象 | **SError**（外部中止） |
| 根因 | `clk_disable_unused()` 把 BG3D 关掉，因为**没有驱动声明使用它** |
| 修法 | 在 DTS 里把 `CLK_MFGCFG_BG3D` 声明为 `coregroup` 时钟，让 panthor 自己管 |

### 同一段注释还提到另一个 casualty

> Wiring the drivers to own their clocks (mt6895.dtsi: ufsci clocks, **gpu coregroup**)
> fixed the two casualties that pass caused directly — without them `clk_disable_unused()`
> cuts `ufs_sel` (taking `ufs_ck` with it) and **`mfgcfg_bg3d`**, so UFS never enumerates
> and panthor's FW ping takes an SError.

---

## 4. 这告诉我们什么、不告诉我们什么

### 告诉我们

| # | 结论 |
|---|---|
| 1 | **xaga 的 G610 在 panthor 下是能走到 FW ping 的** —— 说明固件在那套配置下会启动 |
| 2 | BG3D 是一条真实存在的坑，**主线已经踩过并修好** |
| 3 | 主线的电源同样交给 GPUEB，**没有用 power-domains** |

### 不告诉我们

| # | 因为 |
|---|---|
| 1 | **它的失败是 SError，我们是完全静默** —— 现象不同，不能直接套用 |
| 2 | 它跑在 Linux 上，有完整的 clk/regulator/PM 框架；我们是裸机，没有这些抽象 |
| 3 | **它没有说「GPU 已完全跑通」** —— 注释里说的是修好了这个 SError，没说能渲染 |

---

## 5. 我们的驱动在 BG3D 上做了什么（已核对）

| 位置 | 代码 | 含义 |
|---|---|---|
| `:214` | 注释：`mfgcfg: the block's own gate. mfgcfg_bg3d, the only gate in it` | 已知 |
| `:218` | `#define MALI_MFGCFG_CLR 0x0008u` | CLR 寄存器 |
| `:2171` | `reg_wr(gMfgcfg, MALI_MFGCFG_CLR, MALI_MFGCFG_BG3D);` | **写 CLR 清 BG3D 位 = 打开门控** |
| `:4483` | 注释：`clock_26m() is what opens it` | 与上一致 |

**所以我们是「显式打开 BG3D」，不存在主线那个「被内核关掉」的问题。**

> 注：最初我误以为 `CLR` 是关闭，实际 gate 语义是 `1 = gated off`，
> 写 CLR 位等于打开。已更正。

---

## 6. 可以立刻做的一件事（待 6-Astra 判断）

**主线的 DTS 给出了一份权威的 xaga GPU 硬件描述**，其中几项我们可能没对齐：

| 项 | 主线值 | 我们需要核对 |
|---|---|---|
| GPU 寄存器窗口 | `0x13000000 + 0x480000` | 我们的 `MALI_GPU_PHYS/SIZE` |
| 中断 | job=514、mmu=513、gpu=512（GIC_SPI） | 我们是轮询，没有中断 |
| core 时钟 | `CLK_TOP_MFG_SEL_0_CK`（MFGPLL / 231M ref） | 我们 `clock_26m()` 用手写寄存器 |
| coregroup | `CLK_MFGCFG_BG3D` | 我们已打开 |
| stacks | `CLK_TOP_MFG_SEL_1_CK`（MFGSCPLL / 218M ref） | 我们 `pll_300mhz()` 设了 |
| vstack | `mt6368_vbuck2` | 我们日志有 `v buck2 status 0x6a` |

**其中「stacks 时钟来自 MFGSCPLL / 218M」这一条值得核对** ——
我们日志里 `sc mux trial 695: update20 1 parent 2 gate 0 cfg5 0x1010102`，
`parent 2` 对应什么父时钟需要确认。

---

## 7. 证据边界

- **已核实**：仓库存在、分支存在、xaga DTS 存在、GPU 节点存在、注释原文如上。
- **未核实**：主线当前是否真的能让 GPU 出画面（没找到状态说明）。
- **不成立**：'主线已经解决 xaga GPU' —— 它的注释只说明修了一个 SError。
