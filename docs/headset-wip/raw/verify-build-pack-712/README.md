# VERIFY-BUILD-PACK-712：gain31 复核、新隔离树构建与打包

> 2026-11-16 19:58，DSH。**仅主机。未碰设备、未改生产代码、未刷写。**

---

## 0. 产物

| 项 | 值 |
|---|---|
| **新内核** | **15321256 B / `a2f6db113f599603d1fe35a11e19cfefc2a4641f2eec9bdd06fbb753a956dcdc`** |
| **boot.img** | **22994944 B** |
| **image SHA256** | **b97f96722c5c7961326dadbf36d8624320ee696ac299a89a9663e1897e3379ee** |
| **image MD5** | **337fe6900a93fd80bc1965d911526da0** |
| payload | 19515720 B / `150dc130a237fa55606e98655618f65506c8309ebacea43a31598fb7f52c661e` |
| loader | 48944 B / `9f21349f0db39cb7d71603fc5f07faeaf0cf54181c4a7f6f9880059dad32ac7e` |

---

## 1. 输入与测试

| 项 | 结果 |
|---|---|
| 8 项输入 | **8/8 OK**，驱动 **71087 B / `8c0cf1c2…`** ✅ |
| profile | **rc=0**，`PASS 304 cases; up=113; close+restore=114`，trace `161/139` ✅ |
| legacy | **rc=0** ✅ |
| ASan/UBSan | **0 命中** ✅ |

---

## 2. gain31 代码审查

**改动范围极小**（`gain31-712.patch` 6784 B）：

| 位置 | 改动 |
|---|---|
| 结构体 | `uint32_t tone;` → `uint32_t tone, tone_gain;` |
| `p4_configure_arguments` | 新增 `uint32_t tone_gain = 30;` 默认值 |
| 同函数 | 新增 `PE_parse_boot_argn("hp_tone_gain", …)`，**未提供时回落 30** |
| 同函数 | **`if (tone_gain != 30 && tone_gain != 31)` → 拒绝并打印 `no PMIF access`** |
| `p4_tone_gate` | `(left & 0x1f) == 30` → `== p4.tone_gain`（右同） |
| `p4_tone_begin` | `p4_tone_gain(30)` → `p4_tone_gain((uint8_t)p4.tone_gain)` |

### 2.1 拒绝在 PMIF 之前 —— 已核实

**`p4_configure_arguments()` 在 `hakios_mt6895_headset_init()` 的 `:1714` 调用，
而 `pmif_init()` 在 `:1727`。**

**三个拒绝分支（trim 缺失/越界、`hp_tone > 1`、`hp_tone_gain` 非 30/31）
全部在 `pmif_init()` 之前 return。**

### 2.2 默认兼容

**未提供 `hp_tone_gain` 时 `tone_gain = 30`** → **行为与 trial711 完全一致**。
模型测试也覆盖了这一点（`model_tone_gain = 30; model_tone_gain_present = false;`）。

### 2.3 回读用选定值

`(left & 0x1f) == p4.tone_gain && (right & 0x1f) == p4.tone_gain`
—— **两个字段都对照选定码校验，不是硬编码 31。**

### 2.4 测试覆盖（suffix 新增）

- `bad_tone_gain[] = {0, 29, 32, UINT32_MAX}` **四个非法值各断言 `!pmic_reads && !all_writes && !status_reads`**
- gain31 全程 pattern，断言 `model_regs[0][0x260b]==31`、`0x260c==31`、`0x23c8` 交替 `0x80/0xc0`
- **七个新 gate 写逐点注入失败**（`fail_write_no = up_writes + n`，n=1..7）

---

## 3. 隔离树与构建

| 项 | 值 |
|---|---|
| 基底 | `sha256sum -c` **rc=0、5205 OK、0 FAILED** |
| 新树 | `logs/profile4-low-output-20261116/kernel-src/`，**5205 文件、创建时无 BUILD** |
| **差异** | **恰好 2 项**，新增 0、消失 0 |
| 构建 | **19:56:49 → 19:58:01，72 秒，native rc=2** |
| 编译后复算 | **5205 文件内容变化 0** ✅ |
| judge | **rc=0**，`only the kext symbol-set checks failed` |
| 源码编译错误 | **0** |

**新内核含三条新消息**：
```
headset: hp_tone_gain must be 30 or 31; no PMIF access
headset: tone experiment requested: gain%u, three 1000ms pulses; physical output unmeasured
headset: tone edge %s pulse=%u/3 sgen=%02x gain=%02x/%02x amp=%02x freq=%02x: %s
```

---

## 4. 参数串（538 字节 base）

```
rd=md0 -v … mali_trial=712 mali_nopdc=1 headset=4 hp_trim_l=3 hp_trim_r=17 hp_fine_l=0 hp_fine_r=3 hp_tone=1 hp_tone_gain=31 loader_return_after=150
```

| 检查 | 结果 |
|---|---|
| **base** | **538 字节** ✅ |
| **运行时**（+13+47） | **598** |
| **含 NUL** | **599 ≤ 608** ✅ 余量 9 |
| **本体定位** | `loader.bin` 里出现 3 次，**offset `0x9fef` 后随 NUL 是本体**，另两处后随 `\n` 属 printf 片段 |
| 本体逐字节匹配 | **True** ✅ |
| 十个关键项各 1 次 | ✅（含 `hp_tone_gain=31`） |
| 禁止项 | `mali_trial=711/710`、`headset=3/1`、**`hp_tone_gain=30`**、`gpueb` —— **全部 0 次** |
| 顺序 | `hp_tone_gain=31`(499) 在 `loader_return_after=150`(515) 之前 ✅ |

---

## 5. 打包与解包核验

**mkpayload**（`--no-version` 显式三条目）**rc=0**；**chainboot rc=0**
（`program 48944, Image 42201364, payload 19515720, kernel gz 21112854`）

| 验收 | 结果 |
|---|---|
| header 前 4096 差异 | **仅 4 字节 `[8,9,10,11]`** ✅ |
| ramdisk_size / header24-40 / cmdline / sig_size | **全部与 stock 相同** ✅ |
| loader 段差异 | **仅 10 字节**（`0x12/0x13/0x28-2b/0x30-33`）✅ |
| 填充区 | **全零** ✅ |
| stock Image | **逐字节相同** `12d255cd` ✅ |
| payload | **逐字节相同** ✅ |
| 嵌套 XNUP | kernel **`a2f6db11`**、ramdisk `78985f67`、kernel.ver `363b5257`、**24B 终止项**、**exact EOF** ✅ |
| loader header 补丁 | `0x10=0x3c00000`、`0x28=42201364`、`0x30=0x287f120` ✅ |

---

## 6. 既有产物守卫（全部未变）

| 项 | 值 |
|---|---|
| 710 kernel | `cac498da96406cc4…` ✅ |
| **711 kernel** | **`24df55bfaf7fdd66…`** ✅ |
| 709 kernel | `c17cf881745511a3…` ✅ |
| shared | `67f9f933117f9bc2…` ✅ |
| wrapper | `95247bb011c136d2…` ✅ |
| **711 boot.img / payload** | **`3f57a806…` / `51ff852c…`** ✅ |
| 710 r2 boot.img | `ca161c1bdb002540…` ✅ |
| **原始录音** | **`654c41fa…` 未动** ✅ |
| GPUEB WIP | `9cdcb0da…` ✅ |

---

## 7. 🔴 反对意见（两条）

### 7.1 反对意见一：gain30→31 只差 1 个枚举步，可能听不出差别

**硬件枚举（`mt6368.h:5678-5683`）**：
`DL_GAIN_N_22DB = 30`、`DL_GAIN_N_40DB = 0x1f = 31`。

**Codex 的意图是「更衰减的参考码」来降低响度。**

**但 30 与 31 在 5 位字段里只差 1 步。**
**TLV 的线性刻度是 1 dB/步（`DECLARE_TLV_DB_SCALE(…, -2200, 100, 0)`）——
若按那条线外推，31 只比 30 低约 1 dB。**

**同时枚举名暗示 31 是 "-40dB"（比 -22dB 低 18 dB）。**

**两者矛盾（这正是我在 PREP-711 里指出、你确认的 TLV 方向问题）。**

**🔴 所以：gain31 到底比 gain30 低 1 dB 还是低 18 dB，我没有任何独立依据判定。**
**如果只低 1 dB，这次试验在"降低响度"这个目的上可能效果很小。**

**这不是代码缺陷，是我的证据不足以预测效果。**

### 7.2 反对意见二：声源定位仍然无法靠单次录音解决

**用户方案是一耳贴麦、另一耳近耳外。**

**这比 711 好，但仍有局限：**

| 局限 | 说明 |
|---|---|
| **手机扬声器位置未变** | 手机仍在附近，扬声器若发声仍会被麦克风录到 |
| **两只耳机是同一声源** | 同一 codec 驱动，左右声道无法区分"耳机 vs 扬声器" |
| **无静音基线** | 仍无法判断底噪性质 |

**我在 712 的 docs 里建议的"方案 B（拔耳机对照）"仍然更直接** ——
但那是另一次试验，不在本 CMD 内。

**本轮不做任何建议执行，只记录这条局限。**

---

## 8. 边界

**未做**：设备操作、生产代码修改、输出电平提升、额外试验、
stash/reset/checkout/clean/gc/prune、旧源/BUILD 覆盖、GPUEB 工作。

**原始 MP3 保持在 `/home/lizi/下载/标准录音+5.mp3` 未移动、未改动。**
