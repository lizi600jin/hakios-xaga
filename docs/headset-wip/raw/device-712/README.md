# DEVICE-712：gain31 声源定位复测与原厂恢复

> 2026-11-16 20:05–20:10，DSH。**DSH 独占设备。全部协议命令显式 `-s`，
> subprocess 分离 stdout/stderr/native rc/timeout。**

---

## 五项标签（严格分开）

| 标签 | 结果 |
|---|---|
| **IDENTITY** | ✅ **PASS** |
| **PROFILE_READBACK** | ✅ **PASS** |
| **TONE_REQUEST_TIMING** | ✅ **PASS**（7/7 verified，五段在窗口内） |
| **NORMAL_RETURN** | ✅ **reason=0x0**，完整 sum 匹配 |
| **STOCK_RECOVERY** | ✅ **PASS**（MD5 回到 `a63b2e35…`） |
| **USER_CONFIRMED_HEADPHONE_AUDIBLE** | **✅ 用户现场确认耳机出声**（见 §6，独立于上面五项） |

---

## 1. 基线（A）

| 项 | 值 |
|---|---|
| slot | `_a` ✅ |
| boot_completed | `1` ✅ |
| SELinux | `Enforcing` ✅ |
| **boot_id** | **`d9aa31a2-0d58-4cf8-bb0b-0e3de2d371c1`** ✅ 与 CMD 期望一致 |
| boot_a | 67108864 B / MD5 `a63b2e35a7c2522cb24a6a10332b9bc8` ✅ |
| **Android 媒体输出** | **`Standby: no` 计数 0** —— 这是**刷写前的基线快照** |

> 🔴 **更正（FINAL-SOURCE-DELIVERY）**：原写「无混入」是过度推断。
> `Standby: no = 0` 只证明**该次采样时刻**没有活动音频线程，
> **不能证明整个录音期间持续没有 Android 媒体声音**。
> 全程无 Android 输出的结论需要连续采样或录音侧证据，本轮没有。 |

**本轮不播放参考音，不插拔，不改门控、amp、freq。**

---

## 2. 旧 logbook（B1）

| 项 | 值 |
|---|---|
| xnusys | 2685370368 |
| 减 8388608 | **2676981760 = `0x9f8f8000`** ✅ |
| dd | rc=0，**8388608 B 精确** |
| SHA256 | `9749fe471bf0113f7fa540a6f1dce6c848358060a61a843b8f36b75d2f8bdf26` |
| header | `HLOG` v1、len 531234、reason `0x0` |
| **旧记录** | **`mali_trial=711`**，**不含 712 证据** ✅ 非重复 |

**未清区、未写 xnusys。**

---

## 3. 刷写（B2）

**刷前复查**：22994944 B / **`b97f96722c5c7961326dadbf36d8624320ee696ac299a89a9663e1897e3379ee`** / md5 `337fe690…` ✅

| 步骤 | native rc | timeout |
|---|---|---|
| `adb reboot bootloader` | **0** | False |
| fastboot 枚举 | **0**（attempt 1） | — |
| **`fastboot -s … flash boot_a trial712`** | **0** | False |
| `fastboot -s … reboot` | **0** | False |

```
Sending 'boot_a' (22456 KB)  OKAY [ 0.597s]
Writing 'boot_a'               OKAY [ 0.089s]
```

**trial reboot**：`2026-11-16T20:05:36`

---

## 4. HLOG 抓取（C）

**adb 在 +132.5 s 恢复**，立即抓取（未等 `boot_completed`）：

> 🔴 **更正（FINAL-SOURCE-DELIVERY）**：`+132.5 s` 是**轮询间隔测量值**，
> 其计时起点**未经证实与实际 reboot 时刻同一**（轮询循环启动有延迟）。
> **不能用它推出「XNU 返回变快了」或其他跨轮次的时间比较。**
> 仅记录为「本轮轮询在第 132.5 秒首次观测到 adb 可用」。

| 项 | 值 |
|---|---|
| dd | rc=0，**4198400 B 精确** |
| SHA256 | **`d3434640a209afb616eabaf777c52d45fca57cf932705f2aac0408e55eea3e9b`** |
| header | **`HLOG`** v1、**len 531977**、**reason `0x0`**、sum `0x295c0e4` |
| body | **531977 B**，SHA256 **`19ab2ed6544281ebe6626ffca45809cba024288a76e6ff1a7a3aba84ce61a2e3`** |
| **完整 sum(body)** | **`0x295c0e4`** = header 64 位 sum ✅ |

---

## 5. IDENTITY 与 TONE_REQUEST_TIMING

### 5.1 身份（各 1 次）

`mali_trial=712`、`headset=4`、`hp_tone=1`、**`hp_tone_gain=31`**、
`hp_trim_l=3`、`hp_trim_r=17`、`hp_fine_l=0`、`hp_fine_r=3`

```
[19.502931]: headset: profile4 caller-supplied trim L=3/0 R=17/3; calibration quality unverified
[19.502935]: headset: tone experiment requested: gain31, three 1000ms pulses; physical output unmeasured
```

**`gain31` 在 requested 行里明确出现** ✅

### 5.2 七条 edge（全部 verified，gain 恒 1f/1f = 31）

| 时刻 | edge | pulse | sgen | gain | amp | freq |
|---|---|---|---|---|---|---|
| 19.963 | **armed** | 0/3 | **c0** | **1f/1f** | 01 | 00 |
| 22.000 | **on** | 1/3 | **80** | **1f/1f** | 01 | 00 |
| 23.043 | **mute** | 1/3 | **c0** | **1f/1f** | 01 | 00 |
| 24.070 | **on** | 2/3 | **80** | **1f/1f** | 01 | 00 |
| 25.094 | **mute** | 2/3 | **c0** | **1f/1f** | 01 | 00 |
| 26.107 | **on** | 3/3 | **80** | **1f/1f** | 01 | 00 |
| 27.124 | **mute** | 3/3 | **c0** | **1f/1f** | 01 | 00 |

**`gain=1f/1f` 而不是 `1e/1e` —— 这直接证明 gain31 已写入并回读一致。**
（trial711 是 `1e/1e` = 30。）

### 5.3 段间实际间隔

| 从 | 到 | 实际 ms | 判定 |
|---|---|---|---|
| armed p0 | on p1 | **2037.1** | **设计 2000ms** |
| on p1 | mute p1 | **1043.1** | ✅ |
| mute p1 | on p2 | **1027.0** | ✅ |
| on p2 | mute p2 | **1024.3** | ✅ |
| mute p2 | on p3 | **1012.4** | ✅ |
| on p3 | mute p3 | **1016.7** | ✅ |

**五段全在 900–1250ms 内；armed 后的 2037ms 是设计值。**

---

## 6. USER_CONFIRMED_HEADPHONE_AUDIBLE（独立登记）

**用户原文**：**「能明确确认耳机出声」**

**方法**（用户布置）：一只耳机贴录音麦、另一只耳外，被测手机拉远。

**这是现场听音证据，独立于上面的寄存器/时序 PASS。**

### 6.1 它证明了什么

| 证明 | 说明 |
|---|---|
| **耳机确实出声** | 用户在耳外那只耳机上明确听到 |

### 6.2 它没有证明什么（严格边界）

| 未证明 | 说明 |
|---|---|
| **频率** | 无频谱测量 |
| **SPL / 电压** | 无校准 |
| **DC offset** | 未测 |
| **mute gap 是否声学静音** | 用户未就位判断 gap |
| **完全排除扬声器同时发声** | 物理分离不构成隔离证明 |

**以上四条与 Codex 的 `user-acoustic-observation.json` 里的 limits 一致。**

---

## 7. PROFILE_READBACK

| 阶段 | 对数 | 唯一 |
|---|---|---|
| on / off / restored | **38 / 38 / 38** | **全部 True** ✅ |

```
[19.937075]: headset: cable headphone without mic (AB 0, detector 2)
[19.937707]: headset: profile4 captured 38 field groups; muted SGEN, low-power 48k reference
[19.962066]: headset: profile4 codec configuration verified; muted SGEN, physical output unmeasured
[27.148678]: headset: profile4 HP off shape 2588=00 258a=00 2589=30 25a2=00: verified
[27.282394]: headset: profile4 off and 38 original field groups restored: verified
[27.285904]: headset: restored 36 configuration bytes: ok
```

**关闭一次成功；ACCDET 36 字节恢复。**

---

## 8. 失败检索（全 0）

`FAILED` / `tone transition failed` / `shutdown was NOT confirmed` / `PMIF error` / `stop timed out` —— **全部 0 次**。

---

## 9. STOCK_RECOVERY（D）

**本轮由我主动 `adb reboot bootloader` 完成，无需用户手动。**

| 项 | 值 |
|---|---|
| stock 复查 | 67108864 B / `c5f8e7bd…` / md5 `a63b2e35…` ✅ |
| **flash** | **native rc=0**，2.28 s，`Sending/Writing OKAY` |
| `reboot` | **native rc=0** |
| Android 就绪 | **31 秒** |
| slot | **`_a`** ✅ |
| boot_completed | **1** ✅ |
| **新 boot_id** | **`686cb45c-3f54-481f-ba0c-5e9945f95449`** |
| SELinux | **`Enforcing`** ✅ |
| **boot_a MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** ✅ |
| SurfaceFlinger | 正常 ✅ |

---

## 10. 边界

**未做**：`spmi_access`、codec dump、tinymix、impedance getter、无关服务终止、
GPUEB 访问、PMIF 控制器改动、watchdog、OEM expdb、slot 切换、SELinux 改动、
无关文件清理、stash/reset/checkout/clean/gc/prune、生产仓 push、额外试验。

**未做 dBSPL、物理频率、保证静音或排除混合扬声器的推断。**
