# trial 700：`headset=2` 未插入基线

> 2026-11-15 16:02，DSH。**实机执行（未插耳机），已恢复原厂。**
> 镜像 md5 `5474817444eaa89b9b4c93d017feb805`
> 日志 `sha256 b4a633561bd6df80636942d60e9913730627328f2c7b29144534edb33987fef5`

---

## 0. 一句话

**`headset=2` 初始化成功，检测轮询启动，空插座状态稳定在 `AB=3 / EINT0=3 / FSM=0`。**

**这是 `mode 2` 第一次在真机上跑通。**

---

## 1. 关键读数

```
[   19.960443]: headset: mode 2 on SID 5, serialised in keys worker
[   20.145439]: headset: sample IRQ 0x00 AB 3 EINT0 3 FSM 0 sync 0
[   20.145693]: headset: mode 2 initialised, polling every 50 ms; PCM playback not implemented
```

| 项 | 值 | 含义 |
|---|---|---|
| `mode 2` 启动 | ✅ | 走的是初始化路径，不是只读 dump |
| `accdet_init_once()` | **成功** | 约 30 次寄存器写全部通过 |
| 首次采样 | `IRQ 0x00 AB 3 EINT0 3 FSM 0 sync 0` | **未插入，符合预期** |
| `mode 2 initialised` | ✅ | 轮询已启动（每 50 ms） |
| 初始化失败标记 | **无** | 没有 `initialisation failed`，没有 `restore` |

---

## 2. 为什么只打印一次采样

`poll_sample()`（`hakios.mt6895_headset.c`）的打印条件：

```c
if (first_sample || ab != last_ab || eint != last_eint || fsm != last_fsm) {
    printf("headset: sample IRQ ...");
    ...
}
```

**只在状态变化时打印。** 空插座下 `AB=3`、`EINT0=3`、`FSM=0` 恒定不变，
所以只打印首次那一条。

**这是正确行为，不是轮询停了。** 判据：`mode 2 initialised` 出现即证明进入轮询。

---

## 3. 与 trial 699 的寄存器对比：**零差异**

用 `tools/accdet-check.py` 的数据对拍 699（`mode 1`）与 700（`mode 2`）：

| 项 | 结果 |
|---|---|
| 共同寄存器 | **129 个** |
| **有差异的寄存器** | **0 个** |

**这不是异常，是设计决定的**：`dump_block()` 在 `accdet_init_once()` **之前**执行
（源码 `:492-497` → `:500`），所以两次 dump 都反映**初始化前**的同一状态。

**含义**：`mode 2` 的初始化效果不能从 dump 对比中看出，只能看 `sample` 行。
**这一点必须在以后的对拍中记住，否则会误判「初始化无效」。**

---

## 4. 未插入基线的完整值（可作 701 的对照）

| 寄存器 | 值 | 含义 |
|---|---|---|
| `0x26b6` | `0xff` | `cur_AB = 3` = 拔出 |
| `0x26b8` | `0xff` | `EINT0_MEM_IN = 3` = 拔出 |
| `0x26b7` | `0x00` | `ACCDET_STATE = 0` |
| `0x26a9` | `0x00` | 无 ACCDET IRQ |
| `0x1cb2` | `0x01` | VAUD18 已开 |

全量 129 项见 `raw/trial700-logbook/trial700-full.log`。

---

## 5. 这次证明了什么

| 已证明 | 依据 |
|---|---|
| `mode 2` 初始化序列在真机上能跑通 | `accdet_init_once` 成功，无失败标记 |
| 检测轮询已启动 | `mode 2 initialised, polling every 50 ms` |
| 空插座读数稳定且符合预期 | `AB=3 / EINT0=3 / FSM=0`，且不抖动 |
| 初始化不会破坏未插入状态 | 采样值与 699（未初始化）在关键寄存器上一致 |

## 6. 这次没证明的

| 未证明 | 为什么 |
|---|---|
| **插入耳机能否被识别** | 这次没插耳机 |
| 按键 / 麦克风 / 出声 | `PCM playback not implemented` |
| 锚点 `0x231d` 为何是 `0x00` | 芯片被复位，未解决 |

---

## 7. 下一步：trial 701（插耳机）

**同一镜像，同一内核，唯一差别是物理上插着耳机。**

| 项 | 值 |
|---|---|
| 镜像 | **与 700 完全相同**（md5 `5474817444eaa89b9b4c93d017feb805` 只需改 trial 标签） |
| 动作 | **插着实体 3.5mm 耳机刷机并运行** |
| 关键判据 | `AB` 从 3 变为 **0**（三段）或 **1**（四段）；或 `0x26a9` bit0 置位；或出现 `EINT0 plug in` |
| 预期日志行 | `headset: EINT0 plug in; awaiting ACCDET classification` 或 `headset: cable ...` |

**成功率最高的一条**：只要 `poll_sample` 打印出 `AB != 3`，插入检测就是通的。
