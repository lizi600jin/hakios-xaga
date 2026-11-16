# DEVICE-711：Android 参考音 + 一次 trial711 短音试验 + 原厂恢复

> 2026-11-16 19:33–19:40，DSH。**DSH 独占设备。所有协议命令显式 `-s`，
> 全部用 subprocess 保存 native rc / timeout / 独立 stdout stderr。**

**🔴 声学结果待用户第二台手机录音判定。本报告不含任何出声、波形、电压或 DC offset 推断。**

---

## 六项标签

| 标签 | 结果 |
|---|---|
| **IMAGE_IDENTITY** | ✅ **PASS** |
| **REFERENCE_SOFTWARE_PLAYBACK** | ✅ **PASS** |
| **PROFILE_READBACK** | ✅ **PASS** |
| **TONE_EDGE_TIMING** | ✅ **PASS**（7/7 verified，六段在 900–1250ms；armed 后 2048ms 是设计值） |
| **NORMAL_RETURN** | ✅ **reason=0x0**，完整 sum 匹配 |
| **STOCK_RECOVERY** | ✅ **PASS**（MD5 回到 `a63b2e35…`） |

---

## 1. 基线（A1）

| 项 | 值 |
|---|---|
| slot | `_a` ✅ |
| boot_completed | `1` ✅ |
| SELinux | `Enforcing` ✅ |
| **boot_id** | **`8af533e7-e2fb-4841-a239-c224e40e030d`** ✅ 与 CMD 期望一致 |
| boot_a size | 67108864 ✅ |
| **boot_a MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** ✅ |
| 媒体音量 | 只记录：`STREAM_MUSIC Muted:false Min:0`，**未修改** |
| 耳机 | `type:0x8` 计数 1 ✅ |
| **keyguard** | **初始 true**，用 `input keyevent KEYCODE_WAKEUP` + `wm dismiss-keyguard` 正常解除 → `false` ✅ |

**未绕过任何凭据。**

---

## 2. REFERENCE_SOFTWARE_PLAYBACK

**推送**：`/data/local/tmp/profile4-reference-711.wav`（推送前确认不存在）
- 1536044 B，设备侧 SHA256 **`b994ddba0465ab54e6d33944f49373e8539b51601e7b392c7e2c2eee96408a29`** ✅ 与主机一致

**播放**：`am start -a android.intent.action.VIEW -d file:///… -t audio/x-wav`，**native rc=0**

**焦点**：`mCurrentFocus=…AudioActivity`、`isKeyguardShowing=false` ✅

**AudioFlinger 两次 dump（约 1.05 s 间隔）**：

| 项 | d1（+3.14s） | d2（+4.19s） |
|---|---|---|
| pointer | `0xb40000748bca24d0` | **同** ✅ |
| I/O handle | 21 | **同** ✅ |
| device | **0x8** | **0x8** ✅ |
| rate / channels | 48000 / 2 | 同 ✅ |
| **Frames written** | **88064** | **139264** → **+51200** |
| **Total writes** | **43** | **68** → **+25** |

**→ 同一活动线程、耳机路由、48k 立体声、帧与写计数均推进。**

**播放后停止测试播放器**（`force-stop` rc=0），**活动线程计数回到 0** ✅
**删除该临时 WAV**，确认 `No such file or directory` ✅

---

## 3. 旧 logbook 保存（B1）

| 项 | 值 |
|---|---|
| xnusys size | **2685370368** |
| 减 8388608 | **2676981760 = `0x9f8f8000`** ✅ |
| dd | `bs=4096 skip=653560 count=2048`，**native rc=0** |
| 字节 | **8388608 精确** ✅ |
| SHA256 | `2ec28bb4d1bb4985c5238b8b4ff8f14b88a92a718c737225efcf97bd42aa29a2` |
| header | `HLOG` v1、len 528685、reason `0x0` |
| **旧记录身份** | **`mali_trial=710`**，**不含 trial711 证据** ✅ 非重复试验 |

**未清区、未写 xnusys。**

---

## 4. 刷写（B2）

**刷前复查**：size 22994944、SHA256 **`3f57a806…`**、MD5 `0810f7b3…` —— 全部匹配 ✅

| 步骤 | native rc | timeout |
|---|---|---|
| `adb reboot bootloader` | **0** | False |
| fastboot 枚举（attempt 1） | **0** | — |
| **`fastboot -s … flash boot_a <trial711>`** | **0** | False |
| `fastboot -s … reboot` | **0** | False |

**flash stderr 原文**：
```
Sending 'boot_a' (22456 KB)  OKAY [ 0.597s]
Writing 'boot_a'               OKAY [ 0.092s]
Finished. Total time: 0.767s
```

**trial reboot 时间**：`2026-11-16T19:34:42`

---

## 5. HLOG 抓取（C）

**adb 在 +195.2 s 恢复**，立即抓取（未等 `boot_completed`）：

| 项 | 值 |
|---|---|
| dd | `bs=4096 skip=653560 count=1025`，**native rc=0**、timeout False |
| 字节 | **4198400 精确** ✅ |
| SHA256 | **`cb5222d7a02fdb2d733f8c0d537301f626584002f92984a7235eedc5e25303a1`** |
| header | **`HLOG`** v1、**len 531234**、**reason `0x0`**、sum `0x294e40f` |
| body | **531234 B**，SHA256 **`d8a1df1c5b590f2dd4141b99cc17731d73de9a5a3359d138f612c0e6f965ea07`** |
| **完整 sum(body)** | **`0x294e40f`** |
| **等于 header 64 位 sum** | ✅ **True** |

---

## 6. IDENTITY

| 检查 | 次数 |
|---|---|
| `mali_trial=711` | **1** ✅ |
| `headset=4` | **1** ✅ |
| **`hp_tone=1`** | **1** ✅ |
| `hp_trim_l=3` | **1** ✅ |
| `hp_trim_r=17` | **1** ✅ |
| `hp_fine_l=0` | **1** ✅ |
| `hp_fine_r=3` | **1** ✅ |

**profile4 trim 行**：
```
[19.454526]: headset: profile4 caller-supplied trim L=3/0 R=17/3; calibration quality unverified
```

**分类**：`cable headphone without mic (AB 0, detector 2)` ✅

---

## 7. TONE_EDGE_TIMING（7/7 verified）

| 时刻 | edge | pulse | sgen | gain | amp | freq | 状态 |
|---|---|---|---|---|---|---|---|
| 19.944756 | **armed** | 0/3 | **c0** | 1e/1e | 01 | 00 | **verified** |
| 21.993178 | **on** | 1/3 | **80** | 1e/1e | 01 | 00 | **verified** |
| 23.020855 | **mute** | 1/3 | **c0** | 1e/1e | 01 | 00 | **verified** |
| 24.033455 | **on** | 2/3 | **80** | 1e/1e | 01 | 00 | **verified** |
| 25.046089 | **mute** | 2/3 | **c0** | 1e/1e | 01 | 00 | **verified** |
| 26.057542 | **on** | 3/3 | **80** | 1e/1e | 01 | 00 | **verified** |
| 27.075026 | **mute** | 3/3 | **c0** | 1e/1e | 01 | 00 | **verified** |

**与 CMD 要求的序列逐项一致** ✅
**amp 恒 01、freq 恒 00、两个 gain 恒 1e/1e** ✅

### 段间实际间隔

| 从 | 到 | 实际 ms | 900–1250 |
|---|---|---|---|
| armed p0 | on p1 | **2048.4** | **设计值 2000ms**（armed 后的显式等待） |
| on p1 | mute p1 | **1027.7** | ✅ |
| mute p1 | on p2 | **1012.6** | ✅ |
| on p2 | mute p2 | **1012.6** | ✅ |
| mute p2 | on p3 | **1011.5** | ✅ |
| on p3 | mute p3 | **1017.5** | ✅ |

**六段中五段落在 900–1250ms；armed→on p1 的 2048.4ms 对应代码里的
`clock_interval_to_deadline(2000000, …)`，是设计值不是超时。**
**其余全部在窗口内，无一段被判超时。**

**`tone pattern complete` → gain31 → profile close/restore** ✅

---

## 8. PROFILE_READBACK

| 阶段 | 对数 | 唯一 |
|---|---|---|
| `on` | **38** | ✅ |
| `off` | **38** | ✅ |
| `restored` | **38** | ✅ |
| **共同** | **38** | — |

**off/restored 逐对 masked 比较失败：0 / 76** ✅

### 关键行

```
[19.891433]: headset: profile4 captured 38 field groups; muted SGEN, low-power 48k reference
[19.943262]: headset: profile4 codec configuration verified; muted SGEN, physical output unmeasured
[27.098115]: headset: profile4 HP off shape 2588=00 258a=00 2589=30 25a2=00: verified
[27.103228]: headset: profile4 off and 38 original field groups restored: verified
[27.104318]: headset: restored 36 configuration bytes: ok
```

**HP off shape 核对**：`(2589&0f)=0`、`(25a2&01)=0` ✅
**ACCDET 恢复 36 字节** ✅
**关闭一次成功**（无第二次 attempt）

---

## 9. 失败检索（全 0）

| 关键词 | 次数 |
|---|---|
| `FAILED` | **0** |
| `tone transition failed` | **0** |
| `shutdown was NOT confirmed` | **0** |
| `PMIF error` | **0** |
| `stop timed out` | **0** |

---

## 10. STOCK_RECOVERY（D）

| 步骤 | native rc | timeout |
|---|---|---|
| fastboot 确认 | **0** | — |
| **`fastboot -s … flash boot_a <stock>`** | **0** | False |
| `fastboot -s … reboot` | **0** | False |

**flash stderr**：`Sending 'boot_a' (65536 KB) OKAY [1.720s]` / `Writing 'boot_a' OKAY [0.404s]`

**Android 就绪**：**31 秒**

| 项 | 值 |
|---|---|
| slot | **`_a`** ✅ |
| boot_completed | **1** ✅ |
| **新 boot_id** | **`d9aa31a2-0d58-4cf8-bb0b-0e3de2d371c1`** |
| SELinux | **`Enforcing`** ✅ |
| **boot_a size** | **67108864** ✅ |
| **boot_a MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** ✅ **回到原厂** |
| SurfaceFlinger | 正常响应 ✅ |

---

## 11. 边界

**未做**：debugfs 挂载、PMIC getter、手工寄存器访问、stash/reset/checkout/clean/gc/prune、
共享/默认树内核输出、生产仓 push、GPUEB 扩展、第二次试验。

**没有**任何出声、时钟波形、电压、DC offset 或电气静音的推断。

**声学结果由用户第二台手机录音判定。**
