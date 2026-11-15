# trial 706：`HP_REGISTER_SEQUENCE_PASS` 达成

> 2026-11-15 22:30，DSH。**实机执行，耳机全程插着、未戴在耳上，已恢复原厂。**
> 镜像 md5 `9a79c2a9bc6a85ba89db131a317c2c15`
> 内核 sha256 `2a25b0ba44c4b72d5c87e6dcbb59afc5fdcaf50bdd218f3a358f611a8a46ed35`
> 日志 sha256 `a65e9e15beb5ea0b1c84f2e8c0f4e92efa5067b5dd835c339c5502f81530e736`

---

## 0. 一句话

**`0x260b` 有效增益字段的核验修好后，trial 706 全判据通过 ——
上电、共享位、关闭、恢复四段都 `verified`，零错误，只 attempt 1。**

**705 保持原 FAIL/INCOMPLETE，不追溯改判。**

---

## 1. 输入与构建（按 [CMD] 逐条核验）

| 项 | 指令值 | 实测 | 结果 |
|---|---|---|---|
| 补丁 SHA256 | `1934b598…` | **`1934b598…`** | ✅ |
| 目标文件修改前 | `928db869…` | **`928db869…`** | ✅ |
| dry-run | 应先跑 | `rc=0` | ✅ |
| **修改后 SHA256** | `0fb48748…` | **`0fb48748…`** | ✅ |
| 其余五个冻结文件 | 保持原 SHA | `mali610 b6bf3483`、`keys 1992fdb7`、`audio.h 17631a9f`、`loader_return 82811c10` | ✅ |
| GPUEB IPI 在内核 | 零命中 | **0** | ✅ |
| boot-args 改动 | 只改 `mali_trial` | **只有 `705→706`** | ✅ |

**构建**：`XNU_SRC=logs/msgbuf-1m-20261115/kernel-src ./tools/make-kernel.sh -j8` → **`rc=0`**。

**没有从脏树整包构建。**

---

## 2. 上电：全部命中

```
[20.616464] HP gain write reg=0x260b raw=0x1e read=0x1e mask=0x1f expected=0x1e actual=0x1e: verified
[20.617240] HP gain write reg=0x260c raw=0x1e read=0x1e mask=0x1f expected=0x1e actual=0x1e: verified
[20.640035] HP on readback 2588=0xff 258a=0x03 2589=0x3a 25a2=0x11 gain=0x1f/0x1f gain_mask=0x1f gain_fields=0x1f/0x1f: verified
```

| 判据 | 要求 | 实测 | 结果 |
|---|---|---|---|
| `0x2588` | `0xff` | **`0xff`** | ✅ |
| `0x258a` | `0x03` | **`0x03`** | ✅ |
| `0x2589` | `0x3a` | **`0x3a`** | ✅ |
| `0x25a2` | `(baseline & 0xfe) \| 0x01` | baseline `0x10` → **`0x11`** | ✅ |
| `gain_mask` | `0x1f` | **`0x1f`** | ✅ |
| `gain_fields` | `0x1f/0x1f` | **`0x1f/0x1f`** | ✅ |

---

## 3. 共享位 on：三条独立 verified

```
[20.644650] HP shared on reg=0x259f baseline=0x22 read=0x21 own=0x07 keep=0xf8 own_expected=0x01 own_read=0x01 keep_expected=0x20 keep_read=0x20: verified
[20.649448] HP shared on reg=0x25a1 baseline=0x55 read=0x14 own=0xc3 keep=0x3c own_expected=0x00 own_read=0x00 keep_expected=0x14 keep_read=0x14: verified
[20.654408] HP shared on reg=0x25a2 baseline=0x10 read=0x11 own=0x01 keep=0xfe own_expected=0x01 own_read=0x01 keep_expected=0x10 keep_read=0x10: verified
```

| 地址 | baseline | read | own/keep | on 目标 owned | keep 保持 | 判决 |
|---|---|---|---|---|---|---|
| `0x259f` | `0x22` | **`0x21`** | `0x07`/`0xf8` | `0x01` | `0x20` | ✅ |
| `0x25a1` | `0x55` | **`0x14`** | `0xc3`/`0x3c` | `0x00` | `0x14` | ✅ |
| `0x25a2` | `0x10` | **`0x11`** | `0x01`/`0xfe` | `0x01` | `0x10` | ✅ |

**指令预测的「on 全字节必须 `21`/`14`/`11`」完全命中。**

---

## 4. 关闭：attempt 1 一次通过

```
[169.761986] HP gain write reg=0x260b raw=0x9f read=0x1f mask=0x1f expected=0x1f actual=0x1f: verified
[169.767236] HP gain write reg=0x260c raw=0x9f read=0x1f mask=0x1f expected=0x1f actual=0x1f: verified
[169.772612] HP off readback 2588=0x00 258a=0x00 2589=0x30 25a2=0x10 gain=0x1f/0x1f gain_mask=0x1f gain_fields=0x1f/0x1f: verified
[169.777780] HP shutdown attempt 1/2 sequence=ok off_readback=ok: verified
```

| 判据 | 要求 | 实测 | 结果 |
|---|---|---|---|
| `0x2588` | `0x00` | **`0x00`** | ✅ |
| `0x258a` | `0x00` | **`0x00`** | ✅ |
| `0x2589` 低四位 | `0` | **`0x30`** | ✅ |
| `0x25a2` bit0 | `0` | **`0x10`** | ✅ |
| 左右 gain 字段 | `0x1f` | **`0x1f`/`0x1f`** | ✅ |
| attempt 编号 | 只 1 | **`attempt 1/2`** | ✅ |

**关键对照 705**：705 在 `0x260b` 写 `0x9f` 读回 `0x1f` 时判 FAIL（要求全字节相等）；
**706 按 `read & 0x1f == 0x1f` 判，正确通过。**

---

## 5. 恢复：三条 shared restored 全等于 baseline

```
[169.788608] HP shared restored reg=0x259f baseline=0x22 read=0x22 own=0x07 keep=0xf8 own_expected=0x02 own_read=0x02 keep_expected=0x20 keep_read=0x20: verified
[169.799098] HP shared restored reg=0x25a1 baseline=0x55 read=0x55 own=0xc3 keep=0x3c own_expected=0x41 own_read=0x41 keep_expected=0x14 keep_read=0x14: verified
[169.809520] HP shared restored reg=0x25a2 baseline=0x10 read=0x10 own=0x01 keep=0xfe own_expected=0x00 own_read=0x00 keep_expected=0x10 keep_read=0x10: verified
[169.814725] HP off and original configuration restored
[169.821028] restored 36 configuration bytes: ok
```

| 判据 | 实测 | 结果 |
|---|---|---|
| 三条 read 等于 baseline（`22`/`55`/`10`） | **全部相等** | ✅ |
| own/keep 两个比较都 verified | **全部 verified** | ✅ |
| `HP off and original configuration restored` | **原文出现** | ✅ |
| `restored 36 configuration bytes: ok` | **原文出现** | ✅ |

---

## 6. 零错误与单次执行

| 检查 | 要求 | 实测 |
|---|---|---|
| `HP verify failed` | 0 | **0** |
| `FAILED` | 0 | **0** |
| `incomplete` | 0 | **0** |
| `NOT confirmed` | 0 | **0** |
| `stop timed out` | 0 | **0** |
| `HP restore incomplete` | 0 | **0** |
| `HP shutdown attempt` 条数 | 1 | **1** |
| `HP on readback` 条数 | 1 | **1** |
| `plug out` | 0 | **0** |

---

## 7. 判定

**`HP_REGISTER_SEQUENCE_PASS`——全部判据同时满足。**

| 阶段 | 结果 |
|---|---|
| C→B 三段无麦分类 | ✅ |
| mode 3 初始化 | ✅ |
| on（三联值 + gain + 共享位） | ✅ |
| off（关闭 + 只 attempt 1） | ✅ |
| 恢复（三条 restored + 36 字节 ok） | ✅ |
| 零错误 | ✅ |

---

## 8. 边界（严格按指令，不越界）

**本轮只验证寄存器序列与回收。以下全部未证明：**

| 未证明 | 原因 |
|---|---|
| **DAC 真实供电** | 没有打开 `CLKSQ`/`NCP`/`AUDGLB`/`LDO` |
| **音频时钟活性** | 同上 |
| **PCM 路由** | 本轮不做 |
| **出声** | 本轮不做 |
| **`0x260b` 高位属于哪种硬件行为** | 未验证 |

**705 保持原 FAIL/INCOMPLETE 记录，不追溯改判。**

**GPU / ramoops / Metal 未进入本轮实验变量。**

---

## 9. 一个不影响本轮但必须记录的隐患

**705 编译时实际用的 `bsd/dev/hakios.stubs.c` 是 `479f0881`，缺 `hakios_mt6895_headset_stop` 桩；
外层树那份 `dd0fc309` 才有。**

**705/706 都能编过**，因为 `HAKIOS_MT6895_HEADSET` 已定义，该段被 `#ifndef` 跳过。

**但一旦关掉 headset 驱动就会链接失败。** 本轮按指令未动它（指令要求五个冻结文件保持原 SHA）。

---

## 10. 恢复记录

| 步骤 | 结果 |
|---|---|
| 刷 trial 706 | `OKAY`，0.777s |
| XNU 运行 | 22:22:05 → 22:25:37 返回 adb（约 3 分 32 秒） |
| 取日志 | `len 0x800d8 = 524504`，`sum 0x28c8e24` **校验通过** |
| **`reason`** | **`0x0`** —— 正常返回 |
| Android | 第十次同样卡住，用户手动回 fastboot |
| 恢复 | 刷回原厂 `a63b2e35…`，验证通过 |
| **耳机** | 恢复后仍在（`type:0x8 (headphone)`、`DEVICE_STATE_AVAILABLE`） |
| crash 日志 | **0** |
