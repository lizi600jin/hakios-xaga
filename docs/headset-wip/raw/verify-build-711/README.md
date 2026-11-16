# VERIFY-BUILD-711：短音状态机审查、测试重跑与新隔离树构建

> 2026-11-16 19:23，DSH。**仅主机。未碰手机、未改生产代码、未打包刷写。**

---

## 0. 结果

| 项 | 结果 |
|---|---|
| 8 项输入核验 | ✅ **8/8** |
| 驱动身份 | ✅ **70626 B / `9e423bb9…`** 精确匹配 |
| profile 测试 | ✅ **292 cases / 113 / 114 / 161 / 139**，五项全中 |
| legacy 测试 | ✅ **native rc=0** |
| ASan/UBSan | ✅ **零命中** |
| 5205 基底核验 | ✅ **`sha256sum -c` rc=0，5205 OK，0 FAILED** |
| 新树差异 | ✅ **恰好 2 项**，新增 0、消失 0 |
| 构建 | ✅ **native rc=2**（既有 symbolset）、73 秒 |
| 编译后输入 | ✅ **零变化** |
| judge（显式 KERNEL） | ✅ **rc=0** |
| 新内核 | ✅ **15321256 B / `24df55bf…`** |

**🔴 两条反对意见见 §5，请在打包前判定。**

---

## 1. 输入核验

来源：`logs/profile4-tone-20261116/production-inputs.sha256`

| 文件 | 字节 | SHA256 |
|---|---|---|
| **`hakios.mt6895_headset.c`** | **70626** | **`9e423bb9c1e30c8375057f5d7f8fbb2707a326f18e8e399723a42153f59adf73`** |
| `hakios.pmif.h` | 3356 | `f4d9a964e304ea14686359b85e75560325e9d47ee2ecb5614aae538f9e293d78` |
| `test-headset.py` | 21122 | `2228331ba8a9b6f175b46df6d8a778891fbda82b3da96378542a25a5d10f5ccb` |
| `test-headset-profile.py` | 7783 | `afb21db55cbfc2147988ad4fec0e65eb1bf0ea7e38356ec712250948fab050a0` |
| `mt6368-profile4-prefix.c` | 5848 | `7c2e99b9ae3301242cbf658faf487a1037e610e05d345074728196568a2c4cdd` |
| `mt6368-profile4-suffix.c` | 13842 | `53b4f5c199d3506f1bce612ed68fdf684992e025ac5f4a61f4c71f07313197ff` |
| `mt6368-profile4-vendor.json` | 70412 | `81a836bf6777b881f691b6d62400ffc4849dc71384908f2a8b7c41d0775ce508` |
| `mt6368-hp-normal.json` | 14273 | `a8858f97cf8952b1a46793bd58c71cf263053aede482cf74c0255850189378e9` |

**失败项 0。**

**环境**：Python **3.14.4**、clang **21.1.8**、LLD **21.1.8**。

---

## 2. 状态机审查（独立读源码）

### 2.1 参数门禁

```c
if (PE_parse_boot_argn("hp_tone", &tone, sizeof(tone)) && tone > 1) {
    printf("headset: hp_tone must be 0 or 1; no PMIF access\n");
```

**`hp_tone > 1` 在任何 PMIF 访问之前拒绝。** ✅ 与实现文档一致。

### 2.2 状态机（`P4_TONE_IDLE/WAIT/ON/GAP/DONE`）

| 阶段 | 动作 | 延时 |
|---|---|---|
| `begin` | 先 `p4_apply(0x23c8, mask 0x40, 0x40)` 确认数字静音 → `p4_tone_gain(30)` → `p4_tone_gate(false,"armed")` | — |
| `WAIT` | → `pulse++`、`gate(true,"on")` | **2000 ms** |
| `ON` | → `gate(false,"mute")`；第 3 次则 `p4_tone_gain(31)` 并 `DONE` | **1000 ms** |
| `GAP` | → 同 `WAIT` 分支 | **1000 ms** |

**总时长：2000 + 3×1000 + 2×1000 = 7000 ms = 7.0 s。**

### 2.3 `p4_tone_gate` 的写入/回读自洽性

```c
p4_apply(5, 0x23c8, 0x40, audible ? 0 : 0x40)     /* 只动 bit6 = C_MUTE_SW_CTL */
(sgen & 0xfc) == (audible ? 0x80 : 0xc0)           /* 但校验 bit2..bit7 六位 */
```

| 状态 | `sgen & 0xfc` | bit7 `C_DAC_EN_CTL` | bit6 `C_MUTE_SW_CTL` | bit5:2 四个 SDM_MUTE |
|---|---|---|---|---|
| audible | **`0x80`** | **1** | **0** | **0** |
| mute | **`0xc0`** | **1** | **1** | **0** |

**与 `mt6368.h:1565-1584` 的字段定义完全一致。**
**校验比写入更严格（写 1 位、校 6 位），是好事。**

**trial710 实测 `0x23c8=0x80` → 正好满足 audible 期望** ✅

### 2.4 `p4_tone_gain` 与 `p4_apply` 兼容性

**`p4_registers` 里 `{0x260b, 5, 0x1f, 0x1f, 0x1f, 0x1f, 0}` → `own = 0x1f`。**

`p4_tone_gain` 传 `mask = 0x1f`：
- `mask & ~own = 0x1f & 0x00 = 0` ✅ 通过
- `keep = defined & ~own = 0` → 不查 shared field ✅

**与 `headset_volume_ramp` 的写法一致（都只动低 5 位）。**
**`0x260b`/`0x260c` 是独立寄存器，tone 分别写两个，与 OBJ 判定一致。**

### 2.5 清理路径

| 情形 | 处理 |
|---|---|
| `p4_tone_poll` 返回 false（transition 失败） | `printf("tone transition failed…")` → **`restore_configuration()`** |
| `tone_phase == P4_TONE_DONE` | `printf("tone pattern complete…")` → **`restore_configuration()`** |
| `HP_STOP_REQUESTED` / `quiesced` | `p4_tone_poll` 直接 `return true`（不动硬件），由外层 stop 处理 |
| `stop` | `__atomic_fetch_or(HP_STOP_REQUESTED)`，有界等待 2000×1000µs，超时打印 **"shutdown was NOT confirmed"** |
| `quiesce` | **只 `atomic_store(quiesced, true)`** —— 无锁、无 PMIF、无 printf ✅ |

**与之前审查过的设计一致。**

### 2.6 兼容性

**无 `hp_tone` 或 `hp_tone=0` 时：`p4.tone` 为 0 → `p4_enable` 末尾 `return !p4.tone || p4_tone_begin()` 直接 true，
`p4_tone_poll` 首行 `if (!p4.tone …) return true`，worker 里 `if (mode == 4 && p4.tone && p4.on)` 不进。**

**→ 原 vendor up/down trace 完全不变（161/139 已由测试证实）。** ✅

---

## 3. 测试重跑

### 3.1 profile

```
python3 mtk/tools/test-headset-profile.py --log-file <V>/profile.log --result-file <V>/profile-result.json
native rc = 0
```

| 项 | 期望 | 实测 |
|---|---|---|
| cases | 292 | **292** ✅ |
| up bus writes | 113 | **113** ✅ |
| close+restore | 114 | **114** ✅ |
| oracle up | 161 | **161** ✅ |
| oracle down | 139 | **139** ✅ |

**`model_summary` 原文**：`PASS 292 cases; up bus writes=113; close+restore bus writes=114`

### 3.2 legacy

```
python3 mtk/tools/test-headset.py --log-file <V>/legacy.log
native rc = 0    stderr 空
```

**stdout 覆盖 mode1/2/3、vendor HP trace、5-bit gain readback、bounded stereo retries、
shared-field corruption、repeat insertion、fault rollback、worker stop、zero-access post-stop。**

### 3.3 ASan/UBSan

**三个文件 × 四种模式（AddressSanitizer / UndefinedBehaviorSanitizer / LeakSanitizer / runtime error）全部 0 命中。**

---

## 4. 新隔离树与构建

### 4.1 基底核验

`logs/build-reference-710-20261116-135551/inputs` → **`sha256sum -c` rc=0、5205 OK、0 FAILED**

### 4.2 新树

| 项 | 值 |
|---|---|
| 路径 | **`logs/profile4-tone-20261116/kernel-src/`** |
| 文件数 | **5205** |
| **BUILD** | **创建时不存在** ✅ |
| **新增 / 消失** | **0 / 0** |
| **内容差异** | **恰好 2 项** |

### 4.3 编译前冻结

**关键文件 hash 见 `tree-frozen-inputs.txt`**：
`MASTER f8767550`、`MASTER.hakios dfe75d28`、`pmif.c 57a5d61d`、
`stubs dd0fc309`、`keys 1992fdb7`、`loader_return 82811c10`、`mali610 b6bf3483`、`audio.h 17631a9f`

### 4.4 构建

```
XNU_SRC=<新树> ./xnu-build.sh -k -j8
19:21:31 → 19:22:44   73 秒
native rc = 2
stdout 124870 B / stderr 76191 B
```

**编译后复算**：5205 文件、**内容变化 0**、新增/消失 0 ✅

### 4.5 judge（显式 KERNEL）

```
KERNEL=<新树>/BUILD/obj/.../mach.development.bcm2837 \
  ./tools/make-kernel.sh --judge <build-combined.log>
judge rc = 0
make-kernel: the log shows a built kernel; only the kext symbol-set checks failed
```

**源码编译错误 `*.c:line:col: error:` 计数 = 0。**
**`make rc=2` 与 `judge rc=0` 分开记录，未混淆。**

### 4.6 新内核

| 项 | 值 |
|---|---|
| **路径** | **`logs/profile4-tone-20261116/kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837`** |
| **大小** | **15321256 B** |
| **SHA256** | **`24df55bfaf7fdd665b575730b2012b0de9dac5ed71e59543133244adcfd7fa4f`** |

**与旧 profile4 内核（`cac498da`，15321160 B）差 +96 字节。**

### 4.7 tone 消息存在性

**新内核里有 4 条：**

```
headset: tone armed: 2000ms wait, three 1000ms pulses separated by 1000ms digital mute
headset: tone edge %s pulse=%u/3 sgen=%02x gain=%02x/%02x amp=%02x freq=%02x: %s
headset: tone transition failed; closing and restoring configuration
headset: tone pattern complete; closing and restoring configuration
```

**旧内核（`cac498da`）里 0 条** ✅
**`hp_tone` 参数名字符串存在** ✅

---

## 5. 🔴 反对意见（请在打包前判定）

### 5.1 反对意见一：tone 期间 gain 一直是 30，gap 靠数字静音 —— 需确认这是否是设计意图

**实现文档写：**

> "SGEN digital mute, rather than gain31 alone, defines the gaps between the proposed acoustic intervals."

**代码事实：**

- `p4_tone_begin` 里 `p4_tone_gain(30)` **只调用一次**
- 之后每个 `ON`/`mute` **只改 `0x23c8` bit6**，**gain 保持 30**
- **只在第 3 个 pulse 结束后**才 `p4_tone_gain(31)`

**所以三个 pulse 期间 gain 恒为 30，gap 完全靠 `C_MUTE_SW_CTL=1` 切断。**

**我认可这个设计自洽，但有一点我没把握：**

**`C_MUTE_SW_CTL`（`0x23c8` bit6）在 `mt6368.h` 里叫 "MUTE_SW_CTL"，
但它与四个 `SDM_MUTE_L/R/_2ND` 位是并列的独立字段。
我没有依据判定 bit6 到底切断哪一级（SDM 输入？DAC 输出？），
也不知道它与 `SDM_MUTE` 位的关系。**

**若 bit6 只静音而不切断 SGEN 本身，则"数字静音"能否形成干净的 gap 需要实测。**

**这不是代码缺陷，是**我的证据不足以确认效果**。标为待判定。**

### 5.2 反对意见二：tone 完成会**提前**触发关闭，导致后 123 秒空闲

**trial710（无 tone）实测时间线：**

```
[20.06s] codec configuration verified
[169.4s] HP off shape          ← 间隔 149.3s，正是 loader_return_after=150 的等待
[169.7s] restored 38 groups
```

**加入 tone 后：**

```
[~20.1s] verified → 立刻 p4_tone_begin()
[~27.1s] tone pattern complete → restore_configuration()
```

**即关闭从 169.4s 提前到约 27.1s，之后到 150s 窗口结束还有约 123 秒无输出空闲。**

**这可能完全正常（就是要早关），也可能不是设计者本意（也许期望 tone 接近窗口末尾才触发，
好让波形落在采集窗口中央）。**

**我不判定哪个对，但请确认：**
**tone 在 verified 后立刻开始，是否就是设计意图？**

### 5.3 一个次要观察

**`p4_tone_poll` 里 `p4.pulse++` 在 `p4_tone_gate(true,"on")` **之前**。**
**若 gate 失败，`pulse` 已经加过 —— 日志会显示 `pulse=1/3` 但实际没成功。**
**这只影响日志可读性，不影响状态机正确性（失败即 return false 走清理）。**

---

## 6. 既有产物守卫（全部未变）

| 项 | 实测 |
|---|---|
| old profile4 kernel | **`cac498da96406cc4…`** ✅ |
| old 709 kernel | **`c17cf881745511a3…`** ✅ |
| shared `hakios-kernel` | **`67f9f933117f9bc2…`** ✅ |
| wrapper | **`95247bb011c136d2…`** ✅ |
| GPUEB WIP | **`9cdcb0da826e9bd5…`** ✅ |
| 8 项生产输入 | **不一致 0** ✅ |

---

## 7. 边界

**本轮没有**：设备操作、生产代码修改、共享/默认树重建、打包、刷写、
GPUEB/音频范围扩展、TLV 修复、生产仓 push、stash/reset/checkout/clean/gc/prune、
封存证据覆盖。

**未在设备上运行过这段新代码。**

**等 Codex 判定 §5 两条反对意见后，再发打包/设备 CMD。**
