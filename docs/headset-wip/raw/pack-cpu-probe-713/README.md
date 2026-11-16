# PACK-CPU-PROBE-713：只读 CPU 探针镜像打包

> ## 🔴 本文件有一处已确认错误，更正见 [`SIGNATURE-CORRECTION.md`](SIGNATURE-CORRECTION.md)
>
> **本文写「`signature_size = 0`，此镜像无 v4 签名段」是错的。**
> **正确：`signature_size` 在 offset **1580**（不是 1584），值为 **4096**；
> stock / 713 / 714 三方签名都是 `AVB0` 且 SHA256 均为
> `692c87adcc6ad5fcf0592878a6fe56f13e0da9dfade792f1e8aa1f03e34623c5`，逐字节相同。**
>
> **「签名被保留」这个结论仍成立，但我给的理由是错的。**
> **正确表述是「签名块保留但 AVB 哈希对新内核陈旧」，不是「没有签名」。**
>
> **本文正文保留原状作为错误记录。**

> 2026-11-17 00:40，DSH。**仅主机。未刷写、未碰设备。**

---

## 0. 产物

| 项 | 值 |
|---|---|
| **boot.img** | **22994944 B** |
| **SHA256** | **127e48e9c7089e9845bfd0a3565ccacfb1b80acd3d84776b3946fa8a702b165c** |
| **MD5** | **e91d1caf11378f98553cc51f6a771a46** |
| payload.bin | 19515800 B / `871508dc8fbe6dab094aea2154e701acf1125a717794655ba90de30ee4aefdb2` |
| loader.bin | **48768 B** / `78611b0fe1bf4d3f5c84ba4a48ea55c88ab0cde0da1dda09df04a223906de669` |

**路径**：`logs/mt6895-cpu-baseline-20261116/trial713/boot.img`

---

## 1. 输入核验

| 输入 | 期望 | 实测 |
|---|---|---|
| **内核** | 15321336 B / `a80231ed…` | ✅ 一致 |
| 712 headset | `8c0cf1c2…` | ✅（树内） |
| pmif.h | `f4d9a964…` | ✅（树内） |
| stock boot | 67108864 B / `c5f8e7bd…` | ✅ |
| ramdisk | `78985f67…` | ✅ |
| kernel.ver | 48 B / `363b5257…` | ✅ |
| **loader 源** | `mtkloader.c` `60647f896d027089…` | ✅（37 文件，从 trial712 复制） |

---

## 2. 参数串（独立核算三个字节数）

**`arguments713.json` 声明的三个数，我全部独立算过：**

| 项 | 文件声明 | 我实测 | 匹配 |
|---|---|---|---|
| `base_arguments` | **479** | **479** | ✅ |
| `full_arguments` | **539** | **539** | ✅ |
| `full + NUL` | **540** | **540** | ✅ |
| `<= 608` | — | **True** | ✅ |

**一致性：**
- `base == "rd=md0 -v " + extra + " loader_return_after=150"` ✅
- `full == base + " hakios_kver=2.1.13-hakios-darwin22.5-mt6895-gdb08f63c-dirty"` ✅

### 2.1 本体定位

**`loader.bin` 里出现 3 次：**

| # | offset | 后随 | 判定 |
|---|---|---|---|
| **1** | **`0x9b4d`** | **NUL** | **`XNU_BOOT_ARGS` 本体** ✅ |
| 2 | `0x9e7a` | `\n` | printf 格式串片段 |
| 3 | `0xa58b` | `\n` | printf 格式串片段 |

**本体逐字节匹配：True。**

**`hakios_kver` 不在二进制里** —— 由 loader 运行时从独立常量拼接，
**与 trial711/712 的表现一致。**

### 2.2 关键项各 1 次

`mali_trial=713`、**`headset=0`**、**`mtk_cpu_probe=1`**、`cpus=4`、
`loader_return_after=150`、`mali_nopdc=1`

### 2.3 禁止项全 0

`mali_trial=712`、`mali_trial=711`、`headset=4`、`hp_tone`、`hp_trim`、`cpufreq=`

**→ 本轮不带任何 headset/tone/trim 参数，也不带 `cpufreq=`。**

---

## 3. native rc

| 步 | rc |
|---|---|
| `start.o` | **0** |
| `mtkloader.o` | **0** |
| `ld.lld` → `loader.bin` | **0** |
| `mkpayload.py --no-version` | **0** |
| `chainboot.py` | **0** |

```
22994944 bytes (program 48768, Image 42201364, payload 19515800, kernel gz 21113258)
```

---

## 4. 严格解包五项验收

| # | 项 | 结果 |
|---|---|---|
| 1 | header 前 4096 差异 | **仅 4 字节 `[8,9,10,11]`**（`kernel_size`）✅ |
| 2 | stock 组件 | `ramdisk_size` 1867873 相同、`header[24:40]` 同、`cmdline` 44..1580 同、`sig_size` 同 ✅ |
| 3 | 解压链 | blob 61979320；**loader 段仅 10 字节差异**（`0x12/0x13/0x28-2b/0x30-33`）、填充全零、**stock Image 逐字节相同** `12d255cd`、**payload 逐字节相同** ✅ |
| 4 | 嵌套 XNUP | kernel `a80231ed…`、ramdisk `78985f67…`、kernel.ver `363b5257…`、**24B 终止项在 `pos=19515776`**、**exact EOF** ✅ |
| 5 | loader header 补丁 | `0x10=0x3c00000`、`0x28=42201364`、`0x30=0x287f120` ✅ |

---

## 5. 探针确实编入内核

**`strings` 找到五条消息：**

```
mt6895-cpu: mtk_cpu_probe must be 0 or 1; no access
mt6895-cpu: read-only probe; LUT/request values are not measured clocks
mt6895-cpu: domain %u map failed
mt6895-cpu: domain %u base=%#x enable=%#x/%#x status=%#x/%#x request=%u/%u result=%u rows=%u request_khz=%u
mt6895-cpu: domain %u row %u raw=%#x/%#x
```

**🔴 字符串存在只证明代码被编入，不是运行结果。**

---

## 6. 既有产物守卫（全部未变）

| 项 | 值 |
|---|---|
| 710 kernel | `cac498da96406cc4…` ✅ |
| 711 kernel | `24df55bfaf7fdd66…` ✅ |
| **712 kernel** | **`a2f6db113f599603…`** ✅ |
| 711 boot.img | `3f57a80680e1eb79…` ✅ |
| 712 boot.img | `b97f96722c5c7961…` ✅ |
| shared `hakios-kernel` | `67f9f933117f9bc2…` ✅ |
| wrapper | `95247bb011c136d2…` ✅ |

---

## 7. 这个镜像做什么

**唯一行为**：`mtk_cpu_probe=1` 时读三个性能域的
`ENABLE`(`0x84`)/`STATUS`(`0x8c`)/`REQUEST`(`0x88`) 与 32 行 LUT，
**双快照 70 读/域，零控制器写。**

**`headset=0`** —— 本轮不做任何音频实验。

**`loader_return_after=150`** 保留，**watcher 与硬件看门狗都关**。

---

## 8. 边界

**未做**：刷写、碰设备、改生产树、改候选代码、用默认源树或 shared `hakios-kernel`。

**镜像 hash 只是打包产物标识，不代表它被验证过能跑。**
