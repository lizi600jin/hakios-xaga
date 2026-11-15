# trial 705：`headset=3` 上电全过，下电卡在 `ZCD_CON3`

> 2026-11-15 21:55，DSH。**实机执行，耳机全程插着，已恢复原厂。**
> 镜像 md5 `97dee2876770fe826704bad337335502`
> 内核 `4b1a55dac2460e78f0eb9ede3c81f6d61a618ffe7489aff9a3516c01fbc91370`
> 日志 `sha256 7bf7ddadaf1febb784c986ae8e838919af5053d11129acb561e8e3796417b3de`

---

## 0. 一句话

**MT6368 耳机的模拟上电序列在真机上第一次跑通了 —— 三个终值全部命中：
`2588=0xff`、`258a=0x03`、`2589=0x3a`。**

**但下电卡在 `0x260b`（`ZCD_CON3`）：想写 `0x9f`，读回还是 `0x1f`，差 bit7 写不进去。**

---

## 1. 树的组成（先确认干净）

| 项 | 值 |
|---|---|
| 基线 | 700 干净树（695 drivers + msgbuf 1 MiB） |
| 加入 | Codex 的四个 `headset=3` 文件 |
| **GPUEB IPI** | **零命中**（`INIT_SHARED_MEM` / `gpueb: mailbox mapped` 都是 0） |
| boot-args 改动 | 只有两处：`headset=2→3`、`mali_trial=700→705` |

**内核里确认有**：`HP sequence verified` 1 处、`HP preconditions` 1 处。

---

## 2. 插入识别（与 701 一致）

```
[   20.466945]: headset: EINT0 plug in; awaiting ACCDET classification
[   20.595525]: headset: cable headphone without mic (AB 0, detector 2)
```

---

## 3. 前置状态（Codex 要求记录的）

```
[   20.585276]: headset: HP preconditions chip=0x68 VAUD18=0x01 CKPDN=0x00
               CLKSQ=0x00 IBIAS=0x12 LDO/NV=0x00 NCP=0x00
               GAIN_SEL=0x00 LO=0x00 HS=0x00
```

**关键：`CLKSQ=0x00`（时钟方波未就绪）、`NCP=0x00`（负压泵未开）。**

---

## 4. 15 条 baseline（全部记录）

`0x2588` 到 `0x260c` 共 15 个寄存器，含三条共享位：

| 寄存器 | baseline | own mask |
|---|---|---|
| `0x259f` | `0x22` | `0x07` |
| `0x25a1` | `0x55` | `0xc3` |
| `0x25a2` | `0x10` | `0x01` |

---

## 5. 上电：全部命中

```
[   20.596525]: headset: HP normal path, low-power bias, target -40 dB; register probe only
[   20.618500]: headset: HP on readback 2588=0xff 258a=0x03 2589=0x3a 25a2=0x11 gain=0x1f/0x1f: verified
[   20.619272]: headset: HP sequence verified; supply/clock liveness and PCM playback are unverified
```

| 项 | 期望 | 实测 | 结果 |
|---|---|---|---|
| `0x2588` | `0xff` | **`0xff`** | ✅ |
| `0x258a` | `0x03` | **`0x03`** | ✅ |
| `0x2589` | `0x3a` | **`0x3a`** | ✅ |
| `0x25a2` | 仅 bit0 变 | `0x10 → 0x11` | ✅ |
| gain | `0x1f/0x1f` | **`0x1f/0x1f`** | ✅ |

**这次从 45 步并集里正确分出了「普通 HP」分支 —— Codex 提取的序列是对的。**

---

## 6. 下电：卡在 `ZCD_CON3`

```
[  169.742662]: headset: HP verify failed at 0x260b wanted 0x9f got 0x1f
[  169.768233]: headset: HP verify failed at 0x260b wanted 0x9f got 0x1f
[  169.773288]: headset: HP shutdown incomplete; analog off was NOT confirmed
[  169.779404]: headset: restored 36 configuration bytes: incomplete
```

### `0x260b` 是什么

`mt6368.h:418`：**`MT6368_ZCD_CON3`**（Zero-Cross Detection，过零检测）。

### 差异只有一位

| 项 | 值 |
|---|---|
| baseline | `0x1f` |
| shutdown 想写 | `0x9f` |
| **读回** | **`0x1f`** |
| **差异** | **bit7** |

**写了两次都是同样结果，说明不是偶发时序问题，是写入被硬件拒绝。**

---

## 7. 这恰好印证了 Codex 的边界

Codex 交付时说过：

> 单跑 HP 事件不证明 DAC 真实供电，**DAPM 的供电/时钟/trim 仍未接管**

**现在有了实测证据**：`CLKSQ=0x00`（时钟没开）、`NCP=0x00`（负压泵没开），
而 `ZCD`（过零检测）正是需要时钟的模拟模块。

**所以上电「成功」是寄存器层面的成功，不是模拟电路真的工作了。**

---

## 8. 按 `HP_REGISTER_SEQUENCE_PASS` 七阶段判定

| 阶段 | 结果 |
|---|---|
| ACCDET（C→B） | ✅ 通过 |
| 前置 | ✅ 通过（无 `HP probe refused`，15 条 baseline 齐） |
| 开启 | ✅ **通过**（三联值 + gain + 25a2 bit0） |
| 共享位 | ⚠️ 待 Codex 定（`25a1` 无全字节实测） |
| 持续插入 | ✅ 通过（六秒后 AB=3 不算失败） |
| **关闭** | ❌ **失败**（`0x260b` 卡住，没有 `HP off readback ... verified`） |
| **恢复** | ❌ **`incomplete`** |
| 错误 | ❌ **有 2 次 `verify failed` + 1 次 `incomplete`** |

**结论：本轮不是 PASS。上电成功，关闭与恢复未完成。**

---

## 9. 待 Codex 判的问题

1. `0x260b` 为什么写不进去？`CLKSQ=0x00` 是不是原因？
2. 序列里缺了开 `CLKSQ`/`NCP` 的步骤，还是这些该由 DAPM 负责、单跑事件序列本就不该写 `0x260b`？
3. 下电要不要把 `0x260b` 改成就地验证「不可写是预期的」？
4. `25a1` 最终 `baseline & 0x3c` = `0x55 & 0x3c` = `0x14`，但日志没有全字节实测，这一项能否算通过？

---

## 10. 恢复记录

| 步骤 | 结果 |
|---|---|
| 刷 trial 705 | `OKAY`，0.769s |
| XNU 运行 | 21:46:02 → 21:49:35 返回 adb（约 3 分 33 秒） |
| 取日志 | `len 0x7f6fd = 521981`，`sum 0x2895ee1` **校验通过** |
| **`reason`** | **`0x0`** —— 正常返回，不是 loader boot note |
| Android | 第七次同样卡住，用户手动回 fastboot |
| 恢复 | 刷回原厂 `a63b2e35…`，验证通过 |
