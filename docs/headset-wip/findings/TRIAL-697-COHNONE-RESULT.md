# trial 697（`mali_cohnone=1`）结果

> 2026-11-15 14:35，DSH。**实机执行，已恢复原厂。**
> 镜像 md5 `62f252051516ba9c997db136612e2e3e`，
> sha256 `fb1e161a48b9414d8c3ce7e5be994a94fbc4ec077e9fcc1ff49bc95b25cdc690`。

---

## 0. 一句话结果

**`0x304` 写进去了、读回也对（`0x1f`），但下游一切照旧。**

MCU 仍卡 `status 1`，version word 仍是哨兵。**这条路排除了「写不进去」，
但没能让固件启动。**

---

## 1. 关键读数

```
[   33.894908]: mali: protocol trial 697: features 0x1 before 0 after 0x1f wrote 1
[   34.096960]: mali: sentinel 0xff5aa5f0 written to the version word
[   34.100156]: mali: MCU status 0 before the start request
[   43.539541]: mali: MCU status 1 (raw state 1; boot unproven), job irq 0, gpu irq 0
[   43.547209]: mali: version word 0xff5aa5f0 (untouched; no firmware version published) trial 697
```

| 项 | 结果 |
|---|---|
| `wrote` | **1** —— 写入分支确实执行了 |
| `before` | `0`（`ACE_LITE`） |
| `after` | **`0x1f`（31 = `COHERENCY_NONE`）** —— **寄存器可写，值落进去了** |
| MCU status | **1**（与 695/696 相同） |
| version word | **`0xff5aa5f0`**（哨兵未被覆盖） |
| job irq / gpu irq | `0` / `0` |

---

## 2. 这个结果能说什么、不能说什么

按 6-Astra 在刷机前定的口径（我采纳）：

| 能说 | 不能说 |
|---|---|
| `0x304` **是可写的**，且写 `31` 能读回 `31` | 硬件**实际**按 `NONE` 模式工作 |
| 这次修改**没有**让完整固件启动 | coherency **被排除** |
| version word 仍未被固件发布 | 固件没有执行（只是没观察到发布） |

### 为什么不能说「coherency 被排除」

6-Astra 在刷前明确提过第三种可能，我原样保留：

> 「配置读回正确，但实际访存行为没有改变。`after=0x1f` 证明配置值读回成功，
> 不能独立证明事务已按 NONE 工作；当前代码只保证写在 L2 上电请求前，
> **没有确认写入时 L2 确实已关闭**。」

**我们从未确认写入时 L2 处于关闭状态。** 所以这一条仍然开着：
**写入时机是否有效**，与本轮测的「写入是否生效」是两件事。

---

## 3. 与 695/696 的对比

| trial | 关键参数 | `0x304` 写了吗 | MCU status | version word |
|---|---|---|---|---|
| 695 | `cohnone=0`, `witness=0` | ❌ `wrote 0` | 1 | 哨兵 |
| 696 | `cohnone=0`, `witness=1` | ❌ `wrote 0` | 1 | 哨兵 |
| **697** | **`cohnone=1`**, `witness=0` | ✅ **`wrote 1`, `after 0x1f`** | **1** | **哨兵** |

**三次结果在 MCU 侧完全一致。** 唯一的差别是 697 确实改了寄存器值。

---

## 4. 单变量证明

| 项 | 结果 |
|---|---|
| 基线 | trial 695（**不是 696** —— 696 开了 witness，会换掉复位向量） |
| token 数 | 35 / 35，**只差 2 个** |
| 差异 | `mali_cohnone=0→1`、`mali_trial=695→697` |
| 参数长度 | 两版**都是 463 B**（等长，无位移） |
| `mali_witness` | **`0`**（确认未被 696 污染） |
| kernel sha256 | `18bb68da…` 与 695 相同 |
| payload | **逐字节相同** |
| 构建确定性 | 两次编译同哈希 |

> 特别检查过：脚本里有断言 `assert 'mali_witness=0' in mid2.split()`，
> 以及 `assert len(actual_args) == len(a695)` 防止参数长度变化。

---

## 5. 这次排除掉了什么

**正式排除**：

| 方向 | 依据 |
|---|---|
| 「`0x304` 硬件不存在或只读」 | `before 0` → `after 0x1f`，值确实变了 |
| 「写入代码路径没执行」 | `wrote 1` |
| 「条件判断写错了」（`mali_cold=1` 下真的不写） | 确认：`cohnone=1` 能覆盖 `cold=1` |

**没有排除**（仍然开着）：

| 方向 | 为什么 |
|---|---|
| coherency 影响启动 | 硬件是否真按 `NONE` 工作未证；写入时 L2 状态未证 |
| 写入时机 | 我们只保证了「在 L2 上电请求之前」，没保证「写入时 L2 已关闭」 |
| 一切此前未排除的方向 | 本轮没有改变它们 |

---

## 6. Android 卡住：第三次复现

| trial | surfaceflinger 崩溃次数 | `boot_completed` |
|---|---|---|
| 695 | 38 | 未置位 |
| 696 | 38 | 未置位 |
| **697** | **38** | **未置位** |

**三次完全一致。** 三次的共同点是「XNU 跑完返回 Android」，与具体 GPU 参数无关。
这进一步支持「XNU 返回时 GPU 状态恢复不完整」这个方向，而不是某个试验参数导致的。

> 照片证据：屏幕停在 loader 打完 `starting Android` 之后，`XNU log (472881 bytes)`、
> `logbook: 461 KB at byte 0x1dc97f8`、`mfg1 back to 0x1112`。
> loader 已交接，是 Android 图形栈起不来。

---

## 7. 恢复记录

| 步骤 | 结果 |
|---|---|
| 刷 trial 697 | `OKAY`，0.803s |
| XNU 运行 | 14:25:52 → 14:29:33 返回 adb（约 3 分 41 秒） |
| 取日志 | `HLOG @ 0x9f8f8000`，`len 0x73731 = 472881`，`sum @64 = 0x2506433` **校验一致** |
| 首次 `adb reboot bootloader` | **超时未生效**（进程挂住，60s 后转后台） |
| 重试 | 第二次成功，`fastboot getvar product` → `product: xaga`（连续三次应答） |
| 刷回原厂 | `backup/boot_a_stock_real.img`，`OKAY`，2.152s |
| Android 恢复 | `boot_completed = 1`，Android 17 |
| `boot_a` 最终 | **`a63b2e35a7c2522cb24a6a10332b9bc8`**（= 原厂） |

### 一个操作教训

第一次 `adb reboot bootloader` **返回 0 但没有生效**，adb 后来还短暂在线。
**`adb reboot` 的成功返回不代表设备真的进了目标模式**，必须用
`fastboot getvar product` 确认。这与交接文件里「`fastboot devices` 会骗人」是同一条。

---

## 8. 下一步

这条路已经走到头（写入生效、下游无变化）。剩两个方向：

| # | 方向 | 说明 |
|---|---|---|
| A | **确认写入时机** | 检查写入 `0x304` 时 L2 是否真的处于关闭状态；若否，可能需要把写入挪到更早 |
| B | 回到耳机线 | 今晚有实体耳机，且需要先解决「日志只从 28 秒开始」的问题 |

**A 的代价比 B 高**（要改代码时序），**B 的时间窗口是今晚。**
