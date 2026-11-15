# `mt6685-audclk.ko`：找到了，并反汇编出 `mt6685_set_dcxo` 的真实行为

> 2026-11-15 23:15，DSH。**只读取证 + 离线反汇编，未刷机，未写设备。**

---

## 0. 两件事

**一、preflight 找不到 `.ko` 的原因查清了：文件名是 `mt6685-audclk.ko`（连字符），
不是我搜的 `mt6685_audclk.ko`（下划线）；而且它在 `vendor_boot` 的 ramdisk 里，
不在 `/vendor/lib/modules`。**

**二、`mt6685_set_dcxo` 反汇编出来了：它操作自己 regmap 的 `0x79e` bit2，
延时 400–420 µs。Codex 的判断完全正确。**

---

## 1. 文件是怎么找到的

### 1.1 为什么之前找不到

| 我搜的 | 实际 |
|---|---|
| `mt6685_audclk.ko`（下划线） | **`mt6685-audclk.ko`（连字符）** |
| `/vendor/lib/modules/` | **`vendor_boot` 分区的 ramdisk：`/lib/modules/`** |
| `/vendor_dlkm/lib/modules/` | 同上 |

**我用了 `/sys/module/` 的名字（下划线），但文件名是连字符。**
**而且它来自 `vendor_boot` 的 ramdisk，被解压进内存后原文件就不在挂载点了。**

### 1.2 提取过程

```bash
# vendor_boot_a = 64 MiB，magic VNDRBOOT，header version 4
adb shell 'dd if=/dev/block/by-name/vendor_boot_a' > vendor_boot_a.img

# vendor_ramdisk 在 page 0x1000 之后，LZ4 legacy 压缩（magic 0x184c2102）
dd if=vendor_boot_a.img bs=4096 skip=1 ... # 取出 42741855 字节
lz4 -d -l vendor_ramdisk.cpio vendor_ramdisk.dec   # 解出 88036352 字节

# 是 cpio newc
cpio -idm < vendor_ramdisk.dec
```

**结果**：`lib/modules/` 下有 **198 个 `.ko`**，其中：

| 文件 | 大小 | SHA256 |
|---|---|---|
| **`mt6685-audclk.ko`** | **14496** | **`a76640b621e6f48ee1b1ed376fa61d0abeddfac8118aa9093b41769d08976893`** |
| `mt6685-core.ko` | 14064 | `e4e9d49f5d3c43a735b9c6bbdfaa4864151fe5b7e985447ebc2877d7b465668e` |
| `rtc-mt6685.ko` | 52152 | `04ab2967378c636c2aa31dc8341f36826ebda5b530ee55d07a49ebaba0aa522f` |

### 1.3 ✅ 身份验证：build-id 完全匹配

**运行中的模块**（`/sys/module/mt6685_audclk/notes/.note.gnu.build-id`）：

```
64facb6ce26148d43e953ebe97bcb7d84b128d77
```

**提取出的文件**（`readelf -n`）：

```
Build ID: 64facb6ce26148d43e953ebe97bcb7d84b128d77
```

**两者完全相同，确认是同一个模块。**

---

## 2. 🔴 `mt6685_set_dcxo` 的真实行为（反汇编）

`readelf` 给出：`mt6685_set_dcxo` 在 `.text` 偏移 **`0xf4`**，大小 **124 字节**。

`objdump -dr --start-address=0xf4 --stop-address=0x170`：

```asm
f4:  paciasp
f8:  str  x30, [x18], #8
fc:  stp  x29, x30, [sp, #-16]!
100: mov  x29, sp
104: adrp x8, 0 <__cfi_check>          ; R_AARCH64_ADR_PREL_PG_HI21  clk
108: ldr  x9, [x8]                     ; R_AARCH64_LDST64_ABS_LO12_NC  clk
10c: cbz  x9, 160                     ; clk == NULL -> 直接返回
110: mov  w8, w0                      ; w8 = 参数（enable）
114: ldr  x0, [x9, #8]                ; x0 = clk->regmap      <<< 关键
118: cbz  x0, 160                     ; regmap == NULL -> 直接返回
11c: mov  w1, #0x79e                  ; 寄存器 0x79e
120: mov  w2, #0x4                    ; 掩码 0x4（bit2）
124: tbz  w8, #0, 14c                 ; 参数 bit0 == 0 -> 走 14c（关闭）
128: mov  w3, #0x4                    ; 开启：val = 0x4
12c: mov  x4, xzr
130: mov  w5, wzr
134: mov  w6, wzr
138: bl   regmap_update_bits_base     ; update_bits(reg=0x79e, mask=0x4, val=0x4)
13c: mov  w0, #0x190                  ; 400
140: mov  w1, #0x1a4                  ; 420
144: bl   usleep_range                 ; 睡 400–420 µs
148: b    160
14c: mov  w3, wzr                     ; 关闭：val = 0
15c: bl   regmap_update_bits_base     ; update_bits(reg=0x79e, mask=0x4, val=0)
160: ldp  x29, x30, [sp], #16
164: ldr  x30, [x18, #-8]!
168: autiasp
16c: ret
```

### 结论（源码级，非推断）

| 项 | 值 |
|---|---|
| **寄存器** | **`0x79e`** |
| **掩码** | **`0x4`**（bit2） |
| **开启** | `val = 0x4`（置 bit2） |
| **关闭** | `val = 0`（清 bit2） |
| **延时** | **`usleep_range(400, 420)` µs**，**只在开启后** |
| **regmap 来源** | **`clk->regmap`（第 8 字节）** —— 即 **MT6685 自己的** |

### ✅ 这证实了 Codex 的判断

Codex 说过：

> `mt6685_set_dcxo` 实际对它自己的 regmap 操作 `0x079e` bit2，
> **不能误写到 MT6368 SID5**。

**完全正确。** `x0 = clk->regmap`，是 MT6685（SPMI **SID 9**）的 regmap，
与 MT6368（SID 5）分离。

---

## 3. 对我们的实现意味着什么

### 3.1 如果要在我们驱动里模拟 `CLK_BUF`

**需要走 SPMI SID 9**，而我们现在的 `headset=` 驱动只碰 SID 5。

**这是一个新问题**：`pmif_write8(MT6368_SID, ...)` 里的 `MT6368_SID` 是 5。
要写 MT6685 需要 `pmif_write8(9, ...)`。

**好消息**：`pmif_read8`/`pmif_write8` 本来就接受 SID 参数，机制上不需要改。

### 3.2 但要注意 `0x79e` 的语义

**`0x79e` 在 MT6685 的寄存器空间里，不是 MT6368 的。**
两个芯片的同一偏移是**完全不同的寄存器**。

### 3.3 顺序

`mt_dcxo_event` 在 `PRE_PMU` 调 `mt6685_set_dcxo(true)`，
即 **`CLK_BUF`（`SUPPLY_SEQ_CLK_BUF` = #0）是最先开启的**。

比我们刚做的 CLKSQ（#6）还早。

---

## 4. 证据边界

| 已证明 | 未证明 |
|---|---|
| 文件名是连字符、在 vendor_boot ramdisk | `0x79e` bit2 在硬件上的确切含义 |
| build-id 与运行中模块完全一致 | 写它之后 XO 时钟真的变好 |
| `mt6685_set_dcxo` 操作 `0x79e` mask `0x4` | MT6685 的 SPMI 写通路是否可用 |
| 延时 400–420 µs，只在开启后 | 我们的 PMIF 能否访问 SID 9 |
| regmap 来自 `clk->regmap`，是 MT6685 自己的 | — |

**本轮未写任何设备寄存器、未刷机。**
