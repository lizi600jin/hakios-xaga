# GPUEB 线索 · 真机证据（从推测升级为实证）

> 时间：2026-11-14 22:27
> 方法：真机 `/proc/interrupts` + `/proc/device-tree/` 实测，不是引用外部资料

---

## 0. 三条真机证据

### 证据 A：GPUEB 的 mailbox 中断在 Android 下是活跃的

真机 `/proc/interrupts`：

```
114:  446  0 0 0 0 0 0 0   GICv3 548 Level   MBOX_ISR
228: 2456  0 0 0 0 0 0 0   GICv3 544 Level   13000000.mali
229:    0  0 0 0 0 0 0 0   GICv3 545 Level   13000000.mali
230: 3881  0 0 0 0 0 0 0   GICv3 546 Level   13000000.mali
```

GIC INTID 与 SPI 号的关系是 `SPI = INTID - 32`：

| INTID | SPI | 名字 | 计数 |
|---|---|---|---|
| 548 | **516** | **MBOX_ISR**（GPUEB mailbox） | **446** |
| 544 | 512 | `13000000.mali` (gpu) | 2456 |
| 545 | 513 | `13000000.mali` (mmu) | 0 |
| 546 | 514 | `13000000.mali` (job) | 3881 |

**结论**：
1. GPUEB 的 mailbox 中断 **在 Android 下跑了 446 次** → **GPUEB 固件是活的、在通信**
2. `mt6895.dtsi` 说 GPUEB 的中断是 `GIC_SPI 516` → **与 Mali 的 PWR 中断同号**（SPI 516 = INTID 548）
3. 真机上 Mali 只注册了 **3 个**中断（gpu/mmu/job），**没有 EVENT(515) 和 PWR(516)** —
   因为厂商驱动把 PWR 中断归给了 GPUEB（同一个 INTID），而不是 Mali

**含义**：Android 下 GPU 和 GPUEB 在同一个中断号上协作。我们的驱动如果只接管 Mali 的 3 个中断，
**完全没有处理 516 这个 mailbox 中断** → GPUEB 的请求没人应答。

### 证据 B：`headset-use-ap-eint = 0`（真机实测）

```sh
$ adb shell "cat /proc/device-tree/.../accdet/headset-use-ap-eint | od -An -tx1"
 00 00 00 00          → 0
$ adb shell "cat /proc/device-tree/.../accdet/headset-eint-num | od -An -tx1"
 00 00 00 00          → 0
```

**耳机检测走 PMIC 内部中断，不用 AP 侧 EINT/GPIO。**

→ 这对耳机任务很重要：如果我们想用 GPIO 中断来做耳机检测，**方向本身就错了**。
必须走 SPMI 的 `ACCDET_IRQ`(子号 97) / `ACCDET_EINT0`(98)。

### 证据 C：GPUEB 与 Mali 的地址（真机 DT，前一份文档已列，这里备查）

```
gpueb@13c00000  (节点名)  reg = 0x13c40000 0x22000   ← 基址以 reg 为准
  mbox0_base = 0x13c5fd80  0x280
  mbox0_set  = 0x13c62004  0x4
  mbox0_clr  = 0x13c62074  0x4
  mbox0_send = 0x13c62000  0x4
  mbox0_recv = 0x13c62078  0x4

MBOX_ISR = GIC INTID 548 = SPI 516
```

---

## 1. 这三条证据合起来意味着什么

**Android 下 GPU 是这么工作的：**

```
Mali 硬件 ──中断(544/545/546)──> Mali 内核驱动
                                      ↕
                              GPUEB（独立 MCU，固件由 LK 加载）
                                      ↕
                          MBOX_ISR (548) ──> mtk-mbox 驱动 ──> gpueb 驱动
```

**我们 XNU 下的状态：**

```
Mali 硬件 ──中断?──> 我们的驱动（只注册/处理了 3 个）
                          ✗ 没有 GPUEB 握手
                          ✗ 没有 MBOX_ISR 处理
GPUEB（如果还活着）──MBOX_ISR(548)──> 没人接
```

**最自洽的解释**：
GPU 的电源/频率由 GPUEB 掌管，我们的驱动从来没有向它请求过。
写 `MCU_CONTROL=AUTO` 是"告诉 MCU 开始"，但 **MCU 所在域的电/时钟可能由 GPUEB 控制**，
没要过电 → 寄存器写得进去（AP 总线通）→ 但核不执行 → 哨兵不变。

---

## 2. 需要 sol 决策的三件事

### 2.1 先做零风险验证（我强烈建议）

**读 `0x7c600000` 的 GPUEB 共享状态区（4 KB）**，看这些字段：
- `power_control`：GPUEB 认为 GPU 电源是什么状态
- `power_count`：上电请求计数
- `dvfs_state`
- `shader_present`

**为什么这个最安全**：
- 它是普通 DRAM 保留区（`mblock-38-me_GPUEB_SHARED`，真机 DT 已确认 `0x7c600000` 2MB）
- **不在 `0x13c40000` 那个一碰就挂的禁区**
- 不需要开任何时钟门
- 纯读

**如果 `power_count == 0`** → 直接证实「XNU 下从没给 GPU 上过电」。

### 2.2 `MBOX_ISR` 这个中断我们有没有接管

真机 GPUEB 用 INTID 548。我们的驱动：
- 有没有注册 GIC 548？
- 如果没注册，GPUEB 就算发了请求也没人应答

**这条要查**：`grep -n "548\|516" hakios.mali610.c`

### 2.3 如果确认 GPUEB 是必需的，最小实现路径

按优先级：
1. **读共享内存**（零风险，先做）
2. **打开 GPUEB 时钟门**（要查到门在哪）
3. **实现 mailbox 发送**：`mbox0_send` / `mbox0_set` / `mbox0_recv` / `mbox0_clr` 四个寄存器，不是队列
4. **发 `CMD_POWER_CONTROL`**（先 `CMD_INIT_SHARED_MEM` 告知共享区地址）

---

## 3. 与我之前判断的差异（需要更正）

我之前在 `FINDING-gpueb-evidence` 里说「真机 DT 确认 GPUEB 基址 0x13c40000，与我们驱动一致」——这条对。

但我**没有**注意到：**GPUEB 的中断是 SPI 516，而 Mali 的 PWR 中断也是 SPI 516**。
这是一条独立的信息，而且可能是关键：
如果厂商驱动把 PWR 中断让给了 GPUEB，那「Mali 的中断处理」和「GPUEB 的通信」在硬件上纠缠在一起。

我们的驱动只有 3 条中断记录（512/513/514），**缺 515(EVENT) 和 516(PWR)**。
其中 516 很可能就是这一条。

---

## 4. 一条要提醒的边界

`mt6895.dtsi` 是 **Linux mainline 移植者写的**，不是我们设备的原始 DT。
但本次三条证据里，A（interrupts）和 B（accdet 参数）都是**从真机直接读的**，
只有「GPUEB 节点地址」那条需要以真机 DT 为准（已核对一致）。

所以证据等级：**A、B 是硬证据，C 是已核对的辅助证据。**

---

## 5. 一个可能的替代解释（不要忽略）

**也许 GPUEB 不是必需的，而是厂商驱动的实现选择。**

支持这个解释的事实：
- `b2fb495073b1` 说「只有 mfg1 会破坏东西」，说明电源域层面 mfg1 就够
- 我们的 mfg1 是开的（`0xF3C = 0x4`）

但这个解释无法说明：**为什么 panthor 要专门写那段 GPUEB 握手代码。**
作者不会是闲着没事加这个。

→ 两种解释都保留，**用 §2.1 的那个读数来裁决**。

---

## 6. 补充核实（22:28）：我们是纯轮询驱动，零中断

```sh
$ grep -c "delay(" hakios.mali610.c        → 47
$ grep -c "gic_v3_spi_attach" hakios.mali610.c → 0
$ grep -n "548\|516\|MBOX" hakios.mali610.c
  266: #define MALI_GPUEB_MBOX_RECV    0x22078u      ← 只有偏移常量
  （没有中断注册，没有 MBOX_ISR 处理）
```

**我们的驱动：47 处 `delay()` 轮询，0 处中断注册。**

### 这不完全是缺点

轮询 `JOB_INT_STATUS` 和中断读的是**同一个寄存器位**，信息量一样。
之前项目里也确认过：「现在卡住 GPU 的是 MCU 拿到 AUTO 后一条指令都不执行，不是没人报告中断」。

### 但有一个例外可能是致命的

如果 **GPUEB 在等 Mali 侧 ack 它的 PWR 中断（SPI 516）**，
而我们既没注册中断、也没在轮询路径里读 `IRQ_RAW(0x2033c)`，
那 GPUEB 会一直等下去 → 它可能因此不发出 GPU 电源使能。

**这条可以低成本验证**：
我们的驱动里已经定义了 `MALI_GPUEB_IRQ_RAW 0x2033cu`。
在 `mali_gpueb=1` 时读一次这个寄存器，看有没有 pending 位。
如果有 → 证实 GPUEB 在等我们 ack。

### 所以第 1 步验证应该扩成两项

**1a. 读 GPUEB 共享内存 `0x7c600000`** → 看 `power_control` / `power_count`
**1b. 读 GPUEB 的 `IRQ_RAW(0x2033c)`** → 看有没有等待应答的中断

这两项都是纯读，但如果 1b 要访问 `0x13c40000` 区域，
**必须先确认那些门的时钟已开**（否则会挂死板子，项目里已发生 3 次）。

→ 所以**先做 1a（安全），1b 等确认门控之后再做**。

### 一个保守的替代

如果连 GPUEB 的门都不敢碰，还有一条纯 DRAM 的路：
**`0x7c600000` 的共享区里有 `power_count` 字段**。如果 GPUEB 活着并且在等我们，
它可能已经把「我想给 GPU 上电」写在这个区里了。

这就是为什么 1a 是**唯一零风险且信息量最大**的观测点。
