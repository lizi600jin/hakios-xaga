# trial 698（L2 状态采样）结果：写入时机正确，但仍未启动

> 2026-11-15 15:03，DSH。**实机执行，已恢复原厂。**
> 镜像 md5 `6471095a7f0ea8688da06f39c13ef621`，
> sha256 `09a7aeec9632d06fd4585ec82bc21587d9e549f9281540349b47a65a6390514c`。

---

## 0. 一句话结果

**写 `0x304` 时 L2 确实处于关闭状态（`READY=0`、`TRANS=0`、前后一致），
这是我们能做到的最理想写入时机 —— 但 MCU 仍然没有启动。**

**「写入时机不对」这个假设，正式排除。**

---

## 1. 关键读数

```
[   33.832450]: mali: protocol trial 698: features 0x1 before 0 after 0x1f wrote 1 l2 pre_t/r/t 0/0/0 post_t/r/t 0/0/0
[   33.832493]: mali: l2_config before 0x7130000; asn_hash 0 0 0; left alone
[   33.833828]: mali: l2_config after 0x7130000; asn_hash 0 0 0
[   33.836430]: mali: L2 up: trans 0 active 0 ready 0x1 spins 0
[   43.437331]: mali: MCU status 1 (raw state 1; boot unproven), job irq 0, gpu irq 0
[   43.438528]: mali: version word 0xff5aa5f0 (untouched; no firmware version published) trial 698
```

| 字段 | 值 | 含义 |
|---|---|---|
| `wrote` | `1` | 写入分支执行 |
| `after` | `0x1f` | 值落进寄存器 |
| **`pre_t/r/t`** | **`0/0/0`** | **写之前：`TRANS=0`（无过渡）、`READY=0`（L2 关闭）** |
| **`post_t/r/t`** | **`0/0/0`** | **写之后：仍然关闭，前后一致** |
| `L2 up` 的 `spins` | **`0`** | `PWRON` 之后**首次读即 `READY=1`** |
| MCU status | `1` | 与 695/696/697 相同 |
| version word | `0xff5aa5f0` | 哨兵未被覆盖 |

### 自洽性检查

`pre_ready = 0` 而 `spins = 0`（上电后首读即 1）——**说明 `PWRON` 确实翻转了 L2 状态**，
两个采样点互相印证，不是常量读错。

---

## 2. 判定：按事先定好的三种情况

6-Astra 刷机前定的判据（我采纳并原样执行）：

| 读到 | 判定 | 本次 |
|---|---|---|
| 稳定 `READY=0`、`TRANS=0` | L2 关闭 → **写入时机正确** | ✅ **命中** |
| 稳定 `READY=1`、`TRANS=0` | L2 已开 → 写入太晚 | ✗ |
| `TRANS=1` 或前后不一致 | 变化中，不能判 | ✗ |

**结论：这次写入发生在 L2 关闭期间，是最理想的时机。**

---

## 3. 这次排除掉什么

| 正式排除 | 依据 |
|---|---|
| **「写入 `0x304` 时 L2 已经上电，所以写晚了」** | `pre_ready = 0`、`TRANS = 0`、前后一致 |
| 「采样点选错了、读的是常量」 | `pre_ready 0` → `spins 0` 的翻转关系自洽 |
| 「L2 上电本身有问题」 | `spins 0`，首读即 ready |

## 4. 仍然开着什么

| 方向 | 为什么 |
|---|---|
| coherency **实际**是否按 NONE 生效 | 读回值证明配置写进去了，**不能证明事务层行为** |
| MCU 为什么不取指 | **本轮没有提供任何新证据** |
| 其余此前未排除的方向 | 本轮没动 |

**准确表述：在「配置值正确 + L2 已关闭 + 写入时机正确」这三个条件同时满足的情况下，
完整固件仍然没有启动。** 所以 coherency 这条路上**能控制的因素都已经对了**，
问题不在「怎么配」和「什么时候配」。

---

## 5. 单变量说明

这次不是纯 boot-arg 实验，**内核变了**：

| 项 | 697 | 698 |
|---|---|---|
| boot-args | `cohnone=1`, `trial=697` | `cohnone=1`, `trial=698`（**只改标签**） |
| 内核 | `18bb68da…` | **`a5719c5a…`**（+21 行 −7 行） |
| 内核改动内容 | — | 6 次只读 MMIO 采样 + 两条日志加 `spins` |

**代码改动已独立核实**：新增 `mwr`（写）**0 处**、`printf` 加 3 删 3（替换不增加）、
打印位置在采样区间之外。

**payload** `e2e6b8f4…`（因内核变化而变）。

---

## 6. 顺带修掉一个工具链 bug

`tools/make-kernel.sh` 在**中文 locale** 下会把成功的构建判成失败。

原因：它用正则过滤已知的 kext symbol-set 失败，正则写的是
`\[(Makefile:[0-9]+: )?all\] Error` —— 但 make 在中文 locale 下输出的是
`[Makefile:239：all] 错误 2`，**全角冒号且冒号后无空格**，正则失配。

**修法**：正则改成 `\[(Makefile:[0-9]+[:：][[:space:]]*)?all\][[:space:]]*(Error|错误)`。

`--judge` 重新判定原日志：`rc=0`，正确识别为「内核已构建，仅 kext symbol-set 失败」。
**构建本身一直是成功的**（`222` 个符号失败，与历史三次完全相同；
无编译错误、无链接错误）。

---

## 7. Android 卡住：第四次复现

| trial | SF 崩溃 | `boot_completed` |
|---|---|---|
| 695 | 38 | 未置位 |
| 696 | 38 | 未置位 |
| 697 | 38 | 未置位 |
| **698** | **38** | **未置位** |

**四次完全一致。** 四次覆盖了 witness 开/关、cohnone 开/关、内核有/无探针，
**症状没有任何变化**。这进一步支持「XNU 返回时 GPU 状态恢复不完整」，
且该问题与 GPU 启动实验的参数无关。

---

## 8. 恢复记录与一个操作教训

| 步骤 | 结果 |
|---|---|
| 刷 trial 698 | `OKAY`，0.892s |
| XNU 运行 | 14:51:43 → 14:55:12 返回 adb（约 3 分 29 秒） |
| 取日志 | `len 0x7424d = 475725`，`sum @64 = 0x253becf` **校验一致** |
| 进 fastboot | `adb reboot bootloader` **超时（rc=124）**，用户手动回 fastboot |
| **`getvar product`** | **始终无应答**（12 次轮询全空） |
| `lsusb` | `18d1:d00d Google Inc. fastboot` —— **USB 层是通的** |
| **`fastboot flash`** | **成功**（2.320s） |
| Android 恢复 | `boot_completed = 1`，Android 17 |
| `boot_a` 最终 | **`a63b2e35a7c2522cb24a6a10332b9bc8`**（= 原厂） |

### 🔴 重要更正：`getvar` 无应答 ≠ fastboot 不可用

交接文件里写「判据是 `getvar product`，不是 `fastboot devices`」。
**本轮实测推翻了这个判据的适用性**：

- `getvar product` **12 次全部无应答**
- 但 `fastboot flash` **一次成功**

**`getvar` 无应答可能只是该命令的问题，不代表刷写通道死了。**
以后遇到这个状态，**应该试一次真实刷写**再下结论，而不是直接判定不可用。
