# SPMI/PMIF 修复设计：B1（无锁并发）+ B3（IDLE 超时不补 ACK）

面向 MT6895 / xaga。三份独立 PMIF 实现合并成一条共享事务路径，复用项目已有的
`hakios_spmi_lock()`。只给草案，不改文件。

- 事实来源：`findings/SPMI-PMIF-SPEC.md`（下称 SPEC）、`findings/spmi-pmif.h`、
  `findings/pmif-shared.h`（本设计新增）、三份驱动的只读快照。
- 本文件里的行号全部对**冻结快照**：
  `hakios.mt6895_keys.c` md5 `cb0a64f5`、`hakios.mt6895_headset.c` md5 `7bd9751d`、
  `hakios.mali610.c` md5 `255a08d4`（2026-11-15 00:32 取自活树）。
- 未验证的一律写「未验证」，不写成结论。

---

## 0. 结论摘要

| 项 | 结论 |
|---|---|
| 串行化 | **复用** `hakios_spmi_lock()` / `hakios_spmi_unlock()`（`drivers/core/hakios.platform.c:32-44`），`lck_spin`，**不新建锁** |
| 锁粒度 | **单笔事务**。整笔 `accdet_init` 上锁会让 `delay(2000)` 变成持锁忙等 |
| 共享 API | **6 个函数**：`pmif_init` / `pmif_read8` / `pmif_write8` / `pmif_recover` / `pmif_recover_besteffort` / `pmif_last_sta`，外加 `pmif_status_name` 共 7 个符号 |
| 覆盖的调用点 | **4 处 ACC 写入点**，三份文件全部改到（脚本逐一确认，0 处遗留） |
| 最坏持锁 | **100 ms**（单笔预算，不是每段一预算）；锁的 panic 线 524 ms |
| quiesce | 停核后**不拿锁**、不等待，`pmif_recover_besteffort()`；理由见 §5 |
| B3 | 修在 `pmif_wait_idle_locked()`：FSM==6 时先 ACK 再重试一次 |
| B4 | 修在 `pmif_write8()`：写只等 IDLE，永不等 WFVLDCLR |
| 一行关掉 | `#define PMIF_LOCK_BYPASS 1`（§7）|

**必须先说明的一件事**：本设计开始后，三份目标文件被其它会话改动过两次
（00:17、00:32）。到 00:32 为止，B3、B4、B5、B9 已各自被**就地修在三个文件里**
（三份重复实现，仍然互相无锁），B1 完全未动。本方案的 diff 以 00:32 快照为基线，
它同时做两件事：把 B1 修掉，并把已经修过三遍的 B3/B4/B9 收敛成一份。

---

## 1. 三份实现的行为对照表

「现在」= 00:32 快照。表里每一格都能在括号行号上读到。

| 步骤 | keys（SID 4） | headset（SID 5） | mali（SID 5） |
|---|---|---|---|
| 事务入口 | `pmic_read_on()` :255 | `mt6368_read()` :136 / `mt6368_write()` :161 | `mali_spmi()` :3072 |
| 通道选择 | 有：`pmif_chan_known` 缓存 + `PMIF_CHAN_COUNT`=1 循环 :288-306 | 无，写死 SWINF_2 | 无，写死 SWINF_2 |
| 取锁 | **无** | **无** | **无** |
| 前置 IDLE 等待 | `pmic_wait_on(IDLE)` :266 | `pmif_wait(IDLE)` :140 | `mali_pmif_wait(IDLE)` :3081 |
| 前置 IDLE 超时 | `pmic_recover_on()` :267（FSM==6 才 ACK） | `pmif_recover()` :141（同） | `mali_pmif_recover()` :3082（同） |
| 超时是否区分错误码 | **否**，一律 `false` | **否**，一律 `false` | **否**，一律 `false` |
| ACC 写入 | `pmif_wr(acc, cmd)` :271 | `pmif_wr(PMIF_SWINF_ACC, …)` :144 / :173 | `pmif_wr(MALI_PMIF_ACC, cmd)` :3090 |
| 命令字 | 手拼 `(3<<30)\|(4<<24)\|(0<<16)\|addr` :270 | 手拼 :144 / :173-174 | 手拼 + 具名 `MALI_PMIF_WRITE_BIT` :3087-3089 |
| 读：等 WFVLDCLR | 有 :272 | 有 :145 | 有 :3097 |
| 读：RDATA 先于 ACK | 是 :276-277 | 是 :147-149 | 是 :3103-3104 |
| 写：等 WFVLDCLR | 不适用（只读） | **不等**（等 IDLE）:176-179 | **不等**（等 IDLE）:3091-3095 |
| ACK 后验 IDLE | 有 :278-283 | 有 :150-153 | 有 :3105-3108 |
| **看 `STA[18]` ERR** | **不看** | **不看** | **不看** |
| **区分 NAK(FSM==4)** | **不区分** | **不区分** | **不区分** |
| 写禁止表 | 不适用 | `0x014b`/`0x0240` :162 | `0x014b`/`0x0240` :3074 |
| 失败时 `*out` | ACK 后才写 :281（正确） | 局部 `result` 再赋值 :154（正确） | 局部 `result` 再赋值 :3109（正确） |
| 时基 | `hakios_pmif_budget`（2 ms） | 同 | 同 |
| 自旋硬顶 | `HAKIOS_PMIF_POLL_LIMIT` 20000 | 同 | 同 |

**对照表读出来的三件事**：

1. 三份在「事务顺序」上已经一致（顺序都对：RDATA 在 ACK 前、写不等 WFVLDCLR）。
   差别**不在顺序，在错误处理与串行化**：三份都不看 `STA[18]`、都不区分 NAK、
   都没有锁。顺序对不对已经不影响结论了 —— 没有锁，顺序再对也会被打断。
2. 三份各自持有一份 `pmif_wait`/`pmif_recover`，逻辑逐字相同（`:223-253` /
   `:114-134` / `:3046-3070`）。这是 B1 的**根因**：不存在一个「所有写者都会经过」的点。
3. `keys` 多一层通道搜索 —— 这是唯一的结构性差异，而 `PMIF_CHAN_COUNT`=1 使它
   等价于写死 SWINF_2。合并时直接去掉，顺带关掉 SPEC 的 B2（`chan` 参数做地址算术）。

---

## 2. 串行化选型

### 2.1 项目里已经有一把锁，而且就是为这件事造的

`drivers/core/hakios.platform.c:27-44`：

```c
static LCK_SPIN_DECLARE(spmi_lock, &hakios_platform_lock_group);
void hakios_spmi_lock(void)   { lck_spin_lock(&spmi_lock); }
void hakios_spmi_unlock(void) { lck_spin_unlock(&spmi_lock); }
```

注释原文：「One transfer at a time through the PMIC arbiter for this system, whoever asks:
with two at once the arbiter drops one, as Linux never lets happen (one lock for the bus).
A spin lock: it is taken around a transfer of some tens of microseconds.」

四个消费者已在用：`drivers/input/hakios.buttons.c:155`、`drivers/power/hakios.pmic.c:126`、
`drivers/camera/hakios.torch.c:205`、`drivers/camera/hakios.camera.c:517`；
声明在 `xnu/files/bsd/sys/hakios.input.h:70-71`。

**这一条决定了选型**：新建第二把锁与它不互斥。Qualcomm 侧的 SSC arbiter 与 MTK 侧的
PMIF 是两块不同的硬件、不同的地址，但它们**是同一根物理 SPMI 总线的两个主机口**
（未验证是否共享同一条 wire；即便不共享，新建锁也不会带来任何好处，只会多一个
「哪些写者拿哪把锁」的问题）。设计上不引入第二把锁。

### 2.2 四个候选，逐个否掉

| 候选 | 结论 | 理由 |
|---|---|---|
| **`lck_spin`（复用 `hakios_spmi_lock`）** | **采用** | 见 §2.3 |
| `lck_mtx` | 否 | ① `hakios_mt6895_headset_quiesce()` 在 `psci_smp_stop_others()` **之后**运行，而该函数返回时本核**中断已关**（`osfmk/arm64/psci_smp.c:297` `__builtin_arm_wsr("DAIFSet", DAIFSC_ALL)`）。关中断下取会睡眠的 mutex 是错的。② 与已有 `lck_spin` 不互斥，§2.1。③ `lck_mtx` 早期可用性本身没问题（`STARTUP_SUB_LOCKS` 是第 3 相位，`osfmk/kern/startup.h:62`，早于 `bsd_init()`），但这条不构成采用理由 |
| 关中断 + 自旋 | 否 | 本事务已在 `quiesce` 路径上处于关中断态，再关一次不解决问题；对 keys/mali 两个线程路径则是无谓地抬高最坏中断延迟 100 ms，而现有 `lck_spin` 已经把这件事做对了 |
| 原子标志 + 忙等（不睡眠） | 否 | 手写 `os_atomic_cmpxchg` 自旋等于重新实现 `lck_spin`，但**丢掉** panic 超时（524 ms）这个兜底，也丢掉 lock 统计。更差，不是更简单 |

### 2.3 为什么 `lck_spin` 够了 / 边界在哪

**够**：

- 临界区是「一次 ACC 写 + 至多三次 STA 读」，无睡眠、无分配、无 `printf`（见 §6）。
- 与四个既有消费者天然互斥，因为它们拿的是同一把锁。
- 在 `quiesce` 路径上也能用 —— 只要**不在停核之后拿**（§5）。

**边界（必须写进注释的三条）**：

1. **持锁期间不能 `delay()`。** `delay()` 在 `get_preemption_level() != 0` 时走
   `machine_delay_until()` 忙等（`osfmk/kern/clock.c:1585-1587`），而 `lck_spin_lock`
   正是抬 preemption level 的那一方。持锁 `delay` = 本核空转 + 另一核在
   `lock-panic` 处 panic。本实现的轮询**不用 `delay`**，只用 `mach_absolute_time()`。
2. **超时必须整笔一个预算。** 每段等待各起一个 deadline，最坏是 4×100 ms=400 ms，
   贴着 524 ms 的 panic 线（本设计第一版就是这样，已在 §6.3 修正为整笔一个）。
3. **panic 线是 524 ms，不是 250 ms。** `lck_spin_lock` 走 `hw_lock_spin_policy`
   → `lock_panic_timeout` = `0xc00000` timebase ticks ≈ 524 ms
   （`osfmk/arm/locks_arm.c:106`）。`LockTimeOut` 的 0.25 s
   （`osfmk/arm64/machine_routines.c:94`）属于 `hw_lock_spin_panic_policy`
   （`osfmk/kern/locks.c:462-470`），`lck_spin_lock` **不用**那条。两个数字差一倍，
   用错会把最坏持锁算成安全的两倍余量。

**被拒的另一种「粒度为 0」的方案**：让三份驱动各自 `pmif_try_lock()` 失败就跳过本轮。
可以，但会把「mali 抢不到锁」变成静默跳过，而 mali 的 `mali_vstack_rail()` 是一次性
初始化（`:3074-3160`），跳过就等于没做。所以是阻塞取锁，不是 try。

---

## 3. 锁粒度

### 3.1 为什么必须单笔事务

`hakios.mt6895_headset.c:236-247` 的 `accdet_init()`：

```c
if (!configure(ACCDET_CTRL, 0x02, 0x02)) return false;
delay(2000);                       /* :241 */
if (!configure(ACCDET_CTRL, 0x02, 0)) return false;
delay(1000);                       /* :245 */
return write16(0x2697, 0x800) && ...;
```

`configure()` 与 `write16()` 内部都走 PMIF。如果按「整个 `accdet_init` 上锁」做，
`:241` 的 2 ms 会**在持 `lck_spin` 的状态下 `delay`**。按 §2.3 第 1 条，那是本核
空转 2 ms 且另一核可能 panic 的写法。粗粒度不是慢一点，是错。

**结论**：锁在 `pmif_read8`/`pmif_write8` 内部取、内部放。上层（`accdet_init`、
`update_bits` 的读-改-写、`pmic_scan` 的 96 次扫描）**没有任何跨事务的原子性**，
这一点必须写在头文件里，因为 `update_bits()` 是读-改-写，理论上会被别人插进来改同一
个字节。

### 3.2 `update_bits()` 的读-改-写：已知不原子，且接受

`hakios.mt6895_headset.c:184-215` 的 `update_bits()` = 读 → 改 → 写 → 再读校验。
中间没有锁，别人（keys 读 SID 4、mali 读 SID 5）可以插进来。

**为什么接受**：三份驱动访问的**寄存器互不重叠**（keys 只读 `0x1e`/`0x2a`，headset
读写 `0x23xx`/`0x25xx`/`0x26xx`，mali 只碰 `0x240`/`0x241`/`0x24e`）。总线层面的事务
被打断是有害的（B1），但寄存器层面的交错**不会**互相覆盖。要真正原子化 RMW，需要把
锁提到 `update_bits` 外面，也就是回到 §3.1 的粗粒度问题，代价大于收益。

**未验证**：是否有 PMIC 内部状态会因为「两个不同寄存器的写交错」而出问题。
没有证据说有，也没有证据说没有。这一条留作真机验证项（§8）。

### 3.3 三份驱动的调用上下文

| 驱动 | 上下文 | 阻塞是否允许 | 依据 |
|---|---|---|---|
| keys | 内核线程，`for (unsigned tick = 1;; tick++)`，20 ms 周期 | 允许 | `hakios.mt6895_keys.c:549`、`:665`（`kernel_thread_start`） |
| headset 初始化 | **在 keys 线程里**同步调用 | 允许 | `hakios.mt6895_keys.c:443` 调 `hakios_mt6895_headset_init()` |
| headset 轮询 | 同上，`hakios_mt6895_headset_poll(poll_ms)` | 允许 | `hakios.mt6895_keys.c:663` |
| headset quiesce | `loader_return()`，停核后，**关中断** | **不允许** | `osfmk/arm64/loader_return.c:374`；`psci_smp.c:297` |
| mali | 独立内核线程，启动后 `delay(2000*1000)` | 允许 | `hakios.mali610.c:5248`、`:5088` |

**当前没有一处从中断上下文调用**。这不是运气，是结构：`kamif`/`thread_call` 都没有
出现在三份文件对 PMIF 的调用链上。但**这不构成永久保证** —— 一旦有人把
`hakios_mt6895_headset_poll()` 挂到 `thread_call` 或 EINT 上，`lck_spin` 在中断上下文
里取锁会与同核的线程路径自死锁。头文件里明确禁止（§4）。

---

## 4. 共享 API

### 4.1 放在哪里

| 文件 | 作用 |
|---|---|
| `docs/headset-wip/findings/pmif-shared.h` | 接口 + 常量 + 错误码 + 白名单谓词，**纯 C11，无 XNU 依赖** |
| `docs/headset-wip/findings/pmif-shared.c` | 实现，唯一知道 `lck_spin`/`ml_io_map`/`mach_absolute_time` 的文件 |
| 落地时 → `mtk/mt6895/drivers/spmi/hakios.mt6895_pmif.{c,h}` | 见 §9 |

`spmi-pmif.h`（已有，300 行）继续作为**常量与谓词**的来源；`pmif-shared.h` 是它的
**超集**，把 `PMIF_CMD_MAKE` 的 `len` 参数去掉（只允许单字节扩展传输），并加上函数声明。
两者不冲突，`pmif-shared.h` 落地时应直接 `#include "spmi-pmif.h"` 而不是抄一遍。

### 4.2 函数（7 个符号）

```c
pmif_status_t pmif_init(void);
pmif_status_t pmif_read8 (uint8_t sid, uint16_t addr, uint8_t *out);
pmif_status_t pmif_write8(uint8_t sid, uint16_t addr, uint8_t val);
pmif_status_t pmif_recover(void);
void          pmif_recover_besteffort(void);
void          pmif_last_sta(uint32_t *before, uint32_t *after);
const char   *pmif_status_name(pmif_status_t status);
```

| 函数 | 语义 |
|---|---|
| `pmif_init` | 映射窗口（幂等，首次调用者在锁内 `ml_io_map`）。返回 `PMIF_OK` 或 `PMIF_E_DEAD` |
| `pmif_read8` | 一笔读。成功时写 `*out`；失败时 `*out` **不动**（字节先落局部变量） |
| `pmif_write8` | 一笔写。只等 IDLE，**不等 WFVLDCLR**（B4） |
| `pmif_recover` | 只做 ACK：FSM==6 → 写 1 到 VLDCLR → 等 IDLE；FSM 是别的 → 不动，返回 `PMIF_E_BUSY` |
| `pmif_recover_besteffort` | 同上的无锁、无等待版本，只给 quiesce 用 |
| `pmif_last_sta` | 上一笔事务的 STA 前后值，锁内取，保证成对 |
| `pmif_status_name` | 错误码转字符串，永不为 NULL |

### 4.3 白名单（在形成地址之前）

```c
static inline bool pmif_sid_ok(uint32_t sid);              /* 只有 4 和 5 */
static inline bool pmif_offset_ok(uint32_t off);           /* <= 0x8fe */
static inline bool pmif_pmic_reg_writable(uint32_t addr);  /* 排除 0x014b / 0x0240 */
static inline pmif_status_t pmif_hdr_admit(uint32_t sid, uint32_t addr, bool write);
```

`pmif_hdr_admit()` 依次查 SID、可写性、命令字形状（opc==3、BC==0、SID 位没被截断）。
**在拿锁之前**调用，因为越界访问是 AXI/APB 层的错，发生在设备看到事务之前
（SPEC §6.3）；`pmif_offset_ok()` 同时守 `pmif_rd`/`pmif_wr` 的每一个偏移。

### 4.4 错误码（11 个负值 + 0）

| 码 | 含义 | 补 ACK 吗 |
|---|---|---|
| `PMIF_OK` = 0 | 成功 | — |
| `PMIF_E_SID` -1 | SID 不在 {4,5} | 否（没有事务发生） |
| `PMIF_E_OFFSET` -2 | 偏移 > 0x8fe | 否 |
| `PMIF_E_REG` -3 | 写禁止寄存器 | 否 |
| `PMIF_E_CMD` -4 | 命令字不是单字节扩展传输 | 否 |
| `PMIF_E_ARG` -5 | 空指针 / 值超 8 位 | 否 |
| `PMIF_E_BUSY` -6 | FSM 既非 IDLE 也非 WFVLDCLR | 否（别人的事务，不碰） |
| `PMIF_E_NAK` -7 | `STA[18]` 置位或 FSM==4 | 是 |
| `PMIF_E_TIMEOUT` -8 | 预算内没到 WFVLDCLR，FSM 是别的 | 是 |
| `PMIF_E_STUCK` -9 | FSM==6 且 ACK 没退掉 | 已尝试 |
| `PMIF_E_ACK` -10 | ACK 写了但没回 IDLE | 是 |
| `PMIF_E_DEAD` -11 | 窗口未映射 | 否 |

「失败时自动补 ACK」落在 `pmif_ack_locked()`：只在 FSM==6 时写 `VLDCLR=1`，
别的一律不动（SPEC §4.4 第 2 行：「别人在用 → 不要碰」）。

---

## 5. quiesce 死锁规避

### 5.1 危险在哪

```
loader_return()                       osfmk/arm64/loader_return.c:365
  └─ psci_smp_stop_others()           psci_smp.c:289
       ├─ DAIFSet DAIFSC_ALL          :297   ← 本核中断从此关闭
       └─ 让其它核 psci_smp_cpu_off() :323   ← 它们的锁永远不会放
  └─ hakios_mt6895_headset_quiesce()  loader_return.c:374
       └─ restore_configuration()     headset.c:531
```

如果 `restore_configuration()` 里拿 `lck_spin`，而该锁正被一个**已经关掉的核**持有，
`lck_spin_lock` 会自旋到 `lock-panic`（524 ms）然后 **panic**。`lck_spin_lock` 没有
「放弃」返回值（`locks_arm.c:820-824` 返回 `void`）。这就是死锁，且发生在返回 loader
的路上 —— 整轮日志会跟着丢。

`psci_smp_stop_others()` 还有一条：等待超时后**继续往下走**
（`psci_smp.c:314-334`，`PSCI_SMP_STOP_SECONDS`=3 s），所以不能假设「停核一定成功」。

### 5.2 选哪一个

两个方案：

| 方案 | 内容 | 评价 |
|---|---|---|
| A | quiesce 在**停核之前**拿锁并做完 | 需要在 `loader_return()` 里把 headset 的 restore 提到 `psci_smp_stop_others()` 之前。但 `loader_return.c` 是 XNU 自己的文件，且 A 的前提是「此刻没有别人在用 PMIF」——keys 线程还在跑，前提不成立。**否** |
| **B** | 停核之后**彻底不拿锁**，只做不依赖锁的清理 | **采用** |

**B 的实现**：`restore_configuration(false)` → `pmif_recover_besteffort()`：
一次 STA 读 + 至多一次 `VLDCLR` 写，都是本核自己的 store，不等任何人。

**B 的代价，说清楚**：如果通道被某个已停的核留在事务中间，这次 ACK 不解决问题，
后面的配置回写会全部失败，下一轮日志会说「configuration restore 未完成」。这是
**功能损失，不是数据损坏** —— accdet 的配置在下次 `headset=2` 初始化时会重新写。

**为什么这个代价可以接受**：另一个选择的失败模式是「手机回不到 loader」，
连带整轮日志一起消失。在一个还在 bring-up 的移植上，能拿到日志比耳机能响重要。

### 5.3 实现上的两条硬约束

1. `pmif_recover_besteffort()` **不得**调用 `pmif_rd`/`pmif_wr` 之外的任何东西，
   不得 `printf`（`kprintf` 内部有 `lck_ticket_lock`，见 §6.4）。
2. `hakios_mt6895_headset_quiesce()` 之后不得再有 PMIF 事务。当前
   `restore_configuration()` 末尾写 `active = false`，`hakios_mt6895_headset_poll()`
   会因此早退（`:544` `if (!active) return;`），这条已经成立。

---

## 6. 死锁风险分析

### 6.1 持锁期间所有可能阻塞的点

| # | 点 | 会不会睡 | 最坏耗时 | 依据 |
|---|---|---|---|---|
| 1 | `pmif_rd()` 一次 MMIO 读 | 否 | **未验证**。裸机、非 cache、跨 SoC 总线，量级 ns～µs | SPEC §7.2 2.2 明说没测过 |
| 2 | `pmif_wr()` 一次 MMIO 写 | 否 | 同上 + 两次 `dsb sy` | 同上 |
| 3 | `pmif_wait()` 的轮询 | 否 | 受 §6.3 两个上界约束 | 本文件 §6.3 |
| 4 | `mach_absolute_time()` | 否 | 读 counter，ns 级 | — |
| 5 | `hakios_spmi_lock()` 本身 | 否（自旋） | 524 ms 后 panic | `locks_arm.c:106` |
| 6 | `ml_io_map()`（只在 `pmif_init` 首次） | **可能会**（`kmem_alloc`） | 未验证 | `osfmk/arm/io_map.c:96`（`alloc_size = round_page(size + start_offset)`）。**见 §6.5** |

**唯一会睡的是 #6**，而它只在第一次 `pmif_init()` 时发生，且发生在任何事务之前。

### 6.2 最坏持锁时长

一笔事务内的 STA 读次数：

| 路径 | IDLE | ACC | WFVLDCLR | RDATA | ACK | IDLE | 恢复 | 最多轮询次数 |
|---|---|---|---|---|---|---|---|---|
| 读，成功 | 1 | 1 | n | 1 | 1 | m | 0 | n+m+1 |
| 读，WFVLDCLR 超时 | 1 | 1 | 预算耗尽 | 否 | 否 | 否 | 预算耗尽 | 2×预算 |
| 写，成功 | 1 | 1 | — | — | — | m | 0 | m+1 |
| 写，NAK | 1 | 1 | — | — | — | 立即（ERR 位） | 0 | 2 |

**上界由两个独立量取小**：

```
每次 pmif_wait 的上界 = min( PMIF_SPIN_HARD_CAP = 200000 次,  PMIF_POLL_BUDGET_US = 100000 µs )
```

由于预算是**整笔共享**（`struct pmif_budget` 只有一份 deadline，`polls` 累计），
一笔事务的总轮询次数上界就是 `200000` 次，总时长上界就是 **100 ms**，与路径无关。

单次 MMIO 读估值的敏感性：

| 单次读 | 200000 次 = | 与 524 ms 的关系 |
|---|---|---|
| 100 ns | 20 ms | 安全 |
| 500 ns | 100 ms | 到预算边界 |
| 2.6 µs | **524 ms** | **撞 panic 线** |

所以在「单次读 ≥ 2.6 µs」这个**未验证**的区间里，硬顶 200000 会先于预算触发，持锁
逼近 panic。**这正是为什么预算必须整笔一份**：如果每段各起 100 ms，只要单次读
≥ 1.3 µs，四段叠加就会超过 524 ms。整笔一份把这条线推到 2.6 µs。

**建议**（未验证，需真机）：把 `PMIF_SPIN_HARD_CAP` 从 200000 降到 50000
（= 100 ms / 2 µs，与预算同量级）。这样两个上界在任何单次读耗时下都不会互相矛盾。
本草案**保留 200000**，因为它是三份驱动当前的值，改动它会让「这次失败是预算还是
硬顶」在日志里不可比；降它应作为独立的一步，带自己的验证。

### 6.3 「持锁期间绝不睡眠」的论证

逐条：

1. `pmif-shared.c` 里**没有** `delay()`、`IOSleep()`、`assert_wait`、`thread_block`、
   `lck_mtx_*`、`kmem_alloc`（除了 `pmif_init` 的首次 `ml_io_map`，它在锁内 ——
   见 §6.5）、`printf`（§6.4）。可用 `grep` 验证（§8 第一条命令）。
2. 轮询是 `for(;;)` + 两个比较，唯一的外部调用是 `mach_absolute_time()`，
   它在 arm64 上是读 `CNTVCT_EL0`，不睡。
3. `lck_spin_lock` 抬 preemption level，此后任何「会睡」的调用都已经是错的，
   不依赖本文件的自觉。
4. **做不到的地方，明说**：`pmif_init()` 的 `ml_io_map()` 在 `lck_spin` 内调用。
   `kmem_alloc` 可能睡。这是本设计**唯一**违反「持锁不睡」的点。

### 6.4 `printf` 为什么在锁外

`kprintf`/`printf` 在 BSD 侧走 `bsd_log_lock()` → `lck_ticket_lock`
（`osfmk/kern/printf.c:805` 起 `bsd_log_lock()`）。持 `lck_spin` 再拿 `lck_ticket_lock` 是嵌套两类锁，
且 `printf` 的路径长度不可控。所以本设计里**所有日志都在 `hakios_spmi_unlock()` 之后**：

- 三个驱动的 `pmif_status_name()` 日志都在各自封装函数里，锁已放。
- `pmif-shared.c` 本身**一行 `printf` 都没有**。诊断靠 `pmif_last_sta()` 把
  STA 前后值交出去，由调用方在自己的上下文里打。

### 6.5 §6.2 的反例：`pmif_init()` 在锁内 `ml_io_map()`

这是本设计里唯一「持锁期间可能睡」的地方。三个选择：

| 选择 | 评价 |
|---|---|
| 现在这样（锁内 map） | 简单，`pmif_init` 只在启动时被调两次（keys 线程、mali 线程），此时没有竞争，`kmem_alloc` 几乎不会真睡 |
| 双检锁 + 锁外 map | 需要 `os_atomic` 标志，多一个状态机，而 `pmif_init` 的竞争窗口只有「keys 与 mali 同时首次调用」，实际相隔 2 s |
| 一个 `STARTUP()` 里 map | 最干净，但 `ml_io_map` 在 `STARTUP_SUB_KMEM` 之后才可用，时序要另核 |

**采用第一个**，并在代码里写明这是已知的例外。若真机上出现 `pmif_init` 卡住，
按 §7 的一行开关可以绕过锁直接定位。

### 6.6 最坏的死锁形态（回答「最大风险是什么」）

**持 `lck_spin` 的核被 `psci_smp_stop_others()` 关掉，另一个核在 `quiesce` 里等它。**

这条路径被 §5 的方案 B 切断（`quiesce` 不拿锁）。**但它只切断了一条**：如果将来有人
给 `hakios_mt6895_headset_quiesce()` 加一句 PMIF 读，或者把 `restore_configuration(true)`
接回去，死锁立刻回来，而且它发生在返回 loader 的路上，日志全丢。

第二形态：**mali 线程持锁时被 timeout 卡住 100 ms**。不是死锁，是长持锁。keys 线程
20 ms 周期会被拖到 100 ms 一次，表现为按键迟滞 —— 可见、可恢复。这就是 §7 的一行
开关要处理的那个场景。

---

## 7. 风险与回滚

### 7.1 一行关掉

`pmif-shared.c` 顶部：

```c
/*
 * 1: take no lock at all -- the pre-fix behaviour, for bisecting a hang. Every accessor
 * still does its own transaction, still acks on timeout, still checks ERR; only the
 * exclusion is gone. Boot with pmif_nolock=1 to set it.
 */
static int pmif_nolock;
TUNABLE(int, pmif_nolock, "pmif_nolock", 0);
```

三个 accessor 的内外两处 `hakios_spmi_lock()`/`hakios_spmi_unlock()` 改为
`pmif_lock()`/`pmif_unlock()`：

```c
static inline void pmif_lock(void)   { if (!pmif_nolock) hakios_spmi_lock(); }
static inline void pmif_unlock(void) { if (!pmif_nolock) hakios_spmi_unlock(); }
```

于是**不改代码、只加 boot-arg `pmif_nolock=1`** 就回到「有锁之前」的行为，同时保留
B3/B4 的修复。这是二分「这次挂死是锁引起的还是别的原因」的最短路径。

### 7.2 诊断一个疑似锁引起的挂死

| 现象 | 最可能 | 怎么确认 |
|---|---|---|
| 卡在 `hakios_spmi_lock` 里，524 ms 后 panic `hw_lock_t` 超时 | 有人持锁不还：某条 return 漏了 unlock | 数 `hakios_spmi_lock` 与 `unlock` 的调用次数是否配对；`pmif-shared.c` 里每条 return 前都有 unlock（脚本可查） |
| 卡住但**没有** panic | 不是锁，是 MMIO 读本身不回（总线挂死） | 用 `pmif_nolock=1` 复跑；若仍卡，与本设计无关 |
| 按键迟滞 5 倍 | mali 持锁 100 ms | 日志里 `mali: spmi … <status>` 出现频率；`pmif_last_sta` 的 STA 值 |
| `quiesce` 后回不到 loader | §6.6 第一形态 | 确认 `restore_configuration(false)` 没被改回 `true` |

### 7.3 回滚

| 层级 | 动作 |
|---|---|
| 只关锁 | boot-arg `pmif_nolock=1` |
| 回滚单文件 | 三份 diff 互相独立（`SPMI-FIX-DESIGN.diff` 是三个 file diff），`patch -R` 任一文件即可 |
| 全回滚 | 三个 `.c` 回到基线 md5（`cb0a64f5`/`7bd9751d`/`255a08d4`），删除 `hakios.mt6895_pmif.{c,h}` 与 conf 里两行 |

---

## 8. 验证计划

### 8.1 不需要手机

| # | 检查 | 命令 / 期望 |
|---|---|---|
| 1 | 头文件零告警 | `gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -fsyntax-only -x c - <<<'#include "pmif-shared.h"'` |
| 2 | 实现零告警（宿主 stub） | `gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Iinc -c pmif-shared.c` |
| 3 | 状态机 10 项断言 | `-DPMIF_MMIO_TEST=1` 链接设备模型，跑 §8.3 的 10 条 |
| 4 | diff 上下文存在 | `./pmif-fix-check.py` |
| 5 | 括号与条件编译配平 | 同上（脚本第 2 项） |
| 6 | **每个 ACC 写入点都被覆盖** | 同上（脚本第 3 项，逐一列出 4 处） |
| 7 | 检查器自身能失败 | `./pmif-fix-check.py --selftest`（7 条） |
| 8 | diff 可干净应用且逐字节一致 | `patch -p1 --dry-run < SPMI-FIX-DESIGN.diff`；应用后 `cmp` 与 new 一致 |
| 9 | 持锁期间无睡眠调用 | `grep -nE 'delay\(|IOSleep|assert_wait|thread_block|lck_mtx_|kmem_alloc|printf' pmif-shared.c` → 只允许 `pmif_init` 的 `ml_io_map` 一处 |
| 10 | B1 的静态证据 | 三份驱动 grep `lck_mtx\|lck_spin\|hakios_spmi_lock` 应为 0；应用 diff 后应为 `hakios_spmi_lock` 0 次（都在 `pmif-shared.c` 里） |
| 11 | 构建系统接受新子系统 | `python3 tools/drivers-check.py`（先修 `drivers/usb` 的既有报错，见 §9.3） |

### 8.2 需要手机

| # | 检查 | 期望 | 若失败说明 |
|---|---|---|---|
| 1 | `buttons=2 mali=2 headset=2`，跑 5 分钟 | 无 `hw_lock_t` panic；按键可读 | 锁的问题 |
| 2 | 同上，日志 grep `SID .* register` | 应为 0 行（没有失败） | 通道被打坏或超时太短 |
| 3 | 同上，grep `stalled`/`no WFVLDCLR` | 0 行 | B3/B4 未修好 |
| 4 | 只 `buttons=2`，不启 mali | 按键正常，且比修复前**更稳**（B3 不再让通道死掉） | B3 的修复无效 |
| 5 | `buttons=2 mali=2`，mali 启动后（2 s）连按电源键 20 次 | 20 次都注册，无丢失 | 并发仍在 |
| 6 | `pmif_nolock=1` 复跑 #1 | 若 #1 稳定而 #6 出问题，反证锁有效 | — |
| 7 | 按 §7.2 表格制造一次「mali 持锁超时」 | 按键迟滞但恢复 | 长持锁的实际影响 |
| 8 | `headset=2` 插拔 10 次 | 每次 `cable …` 都打印 | RMW 交错（§3.2）是否有实际影响 |

### 8.3 状态机 10 项（已真跑，见 §10）

`admit` 拒绝时不产生 MMIO / 一笔读的 ACC 字与 ACK / 失败不写 `*out` / B3 恢复 /
B4 写不等 WFVLDCLR / NAK 是 NAK 不是超时 / ACK 不生效是 `E_STUCK` / 白名单 /
`last_sta` 成对 / 每条路径都不漏锁。

---

## 9. 构建系统集成

### 9.1 现状（读出来的，不是猜的）

- `drivers/<subsystem>/conf` 的格式：`driver NAME` / `file PATH` / `requires A B` /
  `default y|n` / `help TEXT`（`docs/drivers-config.md`）。
- SoC 层同样有 `drivers/<subsystem>/conf`，由 `tools/hakios-config.py` 的 `layers()`
  读取（`soc`/`mtk`/`devices` 三种，`hakios-config.py:138-170`）。
- `drivers-check.py:20` 有一张写死的 `SUBSYSTEMS` 集合。**第 20 行**就是新子系统
  要改的那一行。
- `file` 必须是 `bsd/dev/hakios.*.c` 或 `iokit/Kernel/hakios.*.c(.cpp)`
  （`hakios-config.py:282-284`），所以 `.c` 一定落在 `bsd/dev/` 下。
- `.h` **不进** `drivers/map`（那张表里只有 `.c`/`.cpp`，见 `map` 的实际内容），
  头文件走 `xnu/files/` 或与 `.c` 同目录 `#include "hakios.mali610_fw.h"`
  （`hakios.mali610.c:43`）。
- `mtk/mt6895/drivers/` 目前只有 `audio/ gpu/ input/ usb/` 四个子系统，**没有 `spmi/`**。

### 9.2 建议：不新建子系统，放进 `core`

理由：

1. 这把锁本身就住在 `drivers/core/hakios.platform.c`，共享事务路径与它同层最自然。
2. 新建 `spmi/` 要改 `drivers-check.py:20` 的 `SUBSYSTEMS`，而那张表是 common 层
   的东西，为 mt6895 一个 SoC 改它不合适。
3. `core` 的 `conf` 里已有 `standard` 行，但**共享实现不该是 `standard`**：它只被
   MT6895 用，Qualcomm 的配置里不该有它。

**具体做法**（`mtk/mt6895/drivers/core/conf` 新建）：

```
# mtk/mt6895/drivers/core/conf: what this SoC's drivers share.
driver MT6895_PMIF
	file    bsd/dev/hakios.mt6895_pmif.c
	default n
	help    the one SPMI/PMIF transaction path on SWINF_2 (0x1c804000+0x880), shared by the
	help    MT6363 keys, the MT6368 accdet and the GPU's stack rail: one transfer at a time
	help    under hakios_spmi_lock(), every wait bounded, an ACK after every timeout
```

然后三份驱动的 conf 各加一行 `requires MT6895_PMIF`：

```
driver MTK6895_KEYS
	file    bsd/dev/hakios.mt6895_keys.c
	requires MT6895_PMIF
	...
```

`requires` 会被 `check_drivers()` 校验（`hakios-config.py:575-579`）：建了 keys 却没建
PMIF 会报 `CONFIG_HAKIOS_MTK6895_KEYS requires CONFIG_HAKIOS_MT6895_PMIF`。

`.h` 的位置：`xnu/files/bsd/sys/hakios.pmif.h`（与 `hakios.audio.h` 等同层），
`.c` 里 `#include <sys/hakios.pmif.h>`，`pmif-shared.h` 的内容整体搬过去。

### 9.3 一个必须先修的既有问题

`python3 tools/drivers-check.py` 在**当前**树上就已经失败：

```
drivers-check: drivers/usb is not a known subsystem directory
drivers-check: 1 problem(s)
```

`drivers/` 顶层没有 `usb/`，而 `mtk/mt6895/drivers/usb/` 存在。这是别人的在途改动，
与本设计无关，但它会掩盖本设计的构建验证。集成前先让它变绿，否则 §8.1 #11 不可用。

---

## 10. 真跑输出

### 10.1 头文件与实现编译

```
$ gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion \
      -I. -fsyntax-only -x c - <<<'#include "pmif-shared.h"'
（无输出，exit 0）

$ gcc -std=c11 -Wall -Wextra -Werror -Wpedantic -Wshadow \
      -Wstrict-prototypes -Wmissing-prototypes -Iinc -c pmif-shared.c
（无输出，exit 0）
```

### 10.2 状态机 10 项

```
1 admit: ok (no MMIO on a rejected call)
2 read: ok (ACC=0xc50026b6 ACKs=1 fsm=0)
3 read failure: ok (status=no WFVLDCLR inside the budget, out untouched)
4 B3 recovery: ok (ACKs=2, the channel was not left dead)
5 B4 write: ok (ACC=0xe5000241, wrote, no ACK expected, no timeout)
6 NAK: ok (status=the slave did not answer (NAK))
7 ack ignored: ok (status=parked at WFVLDCLR and the ack did not retire it)
8 whitelist: ok (0x240 writable=no readable=yes)
9 last_sta: ok (before=0x8040 after=0x8040)
10 lock: ok (depth 0 after every path)

all 10 PMIF state-machine checks passed
```

第 4 条是 B3 的回归：把 FSM 预置成 6（上一次读没人 ACK），`pmif_read8` 先补 ACK
再走正常流程，`ACKs=2`（一次恢复 + 一次自己的），通道没死。
第 5 条是 B4：写 `0x241` 成功，`ACKs=0`（写不产生可读字，不该 ACK），没有超时。

### 10.3 diff 检查

`pmif-fix-check.py` 在 `docs/headset-wip/`，diff 与它同级（`DEFAULT_DIFF` 指过去）。

```
$ cd docs/headset-wip && python3 pmif-fix-check.py
pmif-fix-check: 3 file(s), 20 hunk(s), 4 ACC store(s) removed, 0 kept
  - hakios.mt6895_keys.c:271  pmif_wr(acc, cmd);
  - hakios.mt6895_headset.c:144  pmif_wr(PMIF_SWINF_ACC, (PMIF_CMD_EXT_REG_LONG << 30) | (MT6368_SID << 24) | addr);
  - hakios.mt6895_headset.c:173  pmif_wr(PMIF_SWINF_ACC,
  - hakios.mali610.c:3090  pmif_wr(MALI_PMIF_ACC, cmd);
pmif-fix-check: the draft applies, the results parse, every ACC store is accounted for
```

```
$ cd docs/headset-wip && python3 pmif-fix-check.py --selftest
selftest: 7 checks, all passed
```

### 10.4 检查器自身的有效性（三种篡改，都必须被抓到）

自测只证明「解析逻辑自洽」，不证明「真 diff 上的检查有效」。所以另外做三次篡改：
把一份**真** diff 改坏，看检查器是否报出来。

| 篡改 | 内容 | 检查器输出 |
|---|---|---|
| A 漏改一处写者 | 把 keys 的 `-\tpmif_wr(acc, cmd);` 改成上下文行（假装没删） | 3 个问题，含 `hakios.mt6895_keys.c:271: still writes the command register and the diff does not remove it` |
| B 上下文漂移 | 把 `#include <sys/hakios.audio.h>` 改成 `audi0.h` | 1 个问题，指名 `hakios.mt6895_keys.c:46` 且打出「diff 要的」与「文件里有的」 |
| C 删一个右括号 | 把某 `+\t}` 改成上下文行 | 1 个问题，指名 `hakios.mt6895_keys.c:85` |

A 这一条就是本任务的核心：「漏掉一个写者」是这套修复最容易犯、也最致命的错，
检查器必须能抓到它。上面的输出证明能。

---

## 11. 与 SPEC 的两处更正

1. **SPEC §7.6 6.1「完全缺失」** 只对三份驱动本身成立。项目层面**有**一把现成的
   `hakios_spmi_lock()`（`hakios.platform.c:32`），四个驱动在用。把 6.1 读成
   「项目没有锁可用」是错的，本设计正是复用它。
2. **SPEC §9 U6「XNU 侧没有现成的跨线程互斥」不成立**，同上。

另外 SPEC §5.2「没有 SW_RST」这一条**依然成立且是本设计的基础**：唯一的恢复动作就是
补 `VLDCLR`，`pmif_recover()` 与 `pmif_recover_besteffort()` 都只做这一件事。

---

## 12. 未验证清单

| # | 项 | 为什么没验 |
|---|---|---|
| 1 | 单次 MMIO 读的实际耗时 | 没有真机测量；SPEC §7.2 2.2 也说没测过。§6.2 的敏感性表覆盖 100 ns～2.6 µs |
| 2 | `PMIF_POLL_BUDGET_US`=100 ms 是否够 | 取的是厂商的 100 ms（SPEC [V]:36）。上游用 10 ms。这块板需要哪个，未验证 |
| 3 | RMW（`update_bits`）被交错是否真的无害 | §3.2 的论证是「寄存器不重叠」，没有实测 |
| 4 | `lck_spin` 与 Qualcomm SSC arbiter 是否真的在争同一根线 | 两者地址不同（`0x1c804000` vs SSC），物理上是否同一条 SPMI wire 未验证 |
| 5 | `pmif_nolock` 的 `TUNABLE` 在 `bsd/dev` 里是否可用 | `TUNABLE(int, …)` 需要 `<kern/startup.h>`；`hakios.platform.c:15` 已经 include 了它，但 `.c` 落在 `bsd/dev/` 时是否同样可用，未验证 |
| 6 | `pmif_init` 的锁内 `ml_io_map` 是否真会睡 | `kmem_alloc` 的理论行为，没有实测 |

---

## 13. diff 草案（unified，内联）

对基线 md5 `cb0a64f5` / `7bd9751d` / `255a08d4`（2026-11-15 00:32 的活树）。整份副本另存为
`docs/headset-wip/SPMI-FIX-DESIGN.diff`，`pmif-fix-check.py` 读的就是它。

三个文件互相独立，可以逐个 `patch -p1` / `patch -R`。

```diff
--- a/mtk/mt6895/drivers/input/hakios.mt6895_keys.c
+++ b/mtk/mt6895/drivers/input/hakios.mt6895_keys.c
@@ -42,10 +42,18 @@
 #include <mach/mach_types.h>
 #include <pexpert/pexpert.h>
 #include <sys/hakios.input.h>
-#include <sys/hakios.pmif.h>
 #include <sys/hakios.audio.h>
 #include <sys/systm.h>
 
+/*
+ * The one PMIF transaction path, shared with the headset and the GPU. It replaces the
+ * hakios_pmif_budget deadline that used to live in <sys/hakios.pmif.h>: the deadline is now
+ * inside pmif_wait(), where it can also see the STA error bit and the FSM, and the lock is
+ * taken once around the whole transaction instead of never.
+ * docs/headset-wip/findings/pmif-shared.h has the interface, pmif-shared.c the state machine.
+ */
+#include <dev/hakios.pmif.h>
+
 extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
 
 #define KPD_PHYS        0x1c00e000
@@ -65,7 +73,12 @@
 #define LINE_VOLUMEUP   17
 
 static vm_offset_t kpd;
-static vm_offset_t pmif;
+/*
+ * No PMIF base of its own: the window is mapped once by pmif_init(), so a second driver
+ * cannot leave a second mapping of it behind. What stays is whether that worked, which
+ * decides whether the PMIC half of the poll runs at all.
+ */
+static bool pmif_ready;
 
 /*
  * The PMIC's own key registers, reached over SPMI rather than through the keypad block.
@@ -90,8 +103,6 @@
  *   the state     bits 3:1 hold the FSM, 0 idle and 6 "valid, clear it"; the data is in RDATA and
  *                 the read is acknowledged by writing 1 to VLD_CLR.
  */
-#define PMIF_BASE           0x1c804000
-#define PMIF_SIZE           0x08ff /* mapping may round to a page; accessors still enforce SWINF_2 */
 /*
  * Which software-interface channel is ours, and where its registers are.
  *
@@ -128,14 +139,6 @@
  * -- every transfer this driver attempted landed past the end of the region and the state machine
  * stayed at IDLE forever.
  */
-#define PMIF_LAYOUT_8195    1
-
-#define PMIF_SWINF_ACC      0x0880
-#define PMIF_SWINF_WDATA    0x0884
-#define PMIF_SWINF_RDATA    0x0894
-#define PMIF_SWINF_VLDCLR   0x08a4
-#define PMIF_SWINF_STA      0x08a8
-
 /*
  * The four software channels, as offsets from the first one's ACC. Linux picks SWINF_2 from the
  * There is exactly one channel to talk to, and walking the others is what crashed a build. The
@@ -146,11 +149,6 @@
  * rebooted into the same image three times running. So this driver addresses SWINF_2 and nothing
  * else, and PMIF_CHAN_COUNT is one so no loop can ever step outside the window again.
  */
-#define PMIF_CHAN_STRIDE    0x40
-#define PMIF_CHAN_COUNT     1               /* SWINF_2 only: everything above it is unmapped */
-
-static unsigned pmif_chan;              /* index into the four, once one is known to work */
-
 /*
  * The opcode that goes into bits 31..30 of the command word is not the SPMI opcode: the PMIF
  * translates. Vendor code maps SPMI 0x60..0x7f to PMIF_CMD_REG (1) and SPMI 0x20..0x2f /
@@ -159,11 +157,6 @@
  * PMIF_CMD_REG left the state machine at 4 instead of WFVLDCLR and made the data register read
  * back zero, which then looked like both keys being held at once.
  */
-#define PMIF_CMD_REG            1u      /* SPMI 0x60..0x7f, eight-bit register space */
-#define PMIF_CMD_EXT_REG_LONG   3u      /* SPMI 0x20..0x2f / 0x38..0x3f, what MT6363 uses */
-#define SWINF_IDLE          0x00u
-#define SWINF_WFVLDCLR      0x06u
-
 #define PMIC_SID            4               /* mt6363 is "pmic@4" on the bus */
 #define MT6363_KEYS_STATE   0x001e          /* bit 1 power, bit 3 home; clear means pressed */
 #define MT6363_KEYS_ALT     0x002a          /* TOPSTATUS on mt6359, same two bits: the vendor's pick */
@@ -188,120 +181,37 @@
 	return *(volatile uint32_t *)(kpd + at);
 }
 
-static inline uint32_t
-pmif_rd(uint32_t at)
-{
-	if (at != PMIF_SWINF_RDATA && at != PMIF_SWINF_STA) {
-		return 0xffffffffu;
-	}
-	uint32_t value = *(volatile uint32_t *)(pmif + at);
-
-	__asm__ volatile ("dsb sy" ::: "memory");
-	return value;
-}
-
-static inline void
-pmif_wr(uint32_t at, uint32_t value)
-{
-	if (at != PMIF_SWINF_ACC && at != PMIF_SWINF_VLDCLR) {
-		return;
-	}
-	__asm__ volatile ("dsb sy" ::: "memory");
-	*(volatile uint32_t *)(pmif + at) = value;
-	__asm__ volatile ("dsb sy" ::: "memory");
-}
-
 /*
- * One register of the PMIC, over the arbiter. Returns false and leaves `value` alone if the
- * transfer did not complete; nothing here retries, because a caller that is polling a key every
- * twenty milliseconds has all the retries it needs.
+ * The keys' one read, on the shared path. There is nothing of the transaction left here: the
+ * channel search, the IDLE wait, the WFVLDCLR wait, the ACK after a timeout, the STA error
+ * bits, the deadline and the lock all belong to pmif_read8. What is this driver's is the
+ * policy -- read 0x1e, and say why when it fails.
+ *
+ * The first failure of each distinct status is logged once and the count is kept: a channel
+ * that has gone deaf would otherwise print fifty lines a second, and the log the loader
+ * carries back is four megabytes shared with the kernel's own.
  */
-static unsigned pmif_chan_known;   /* zero until a channel has carried a transfer */
+static pmif_status_t pmif_last_keys_status;
+static unsigned pmif_keys_failures;
 
-/* On a failed read, acknowledge a late/stale valid word once; never reset the arbiter. */
 static bool
-pmic_wait_on(unsigned chan, uint32_t state, struct hakios_pmif_budget *budget)
-{
-	if (chan >= PMIF_CHAN_COUNT) {
-		return false;
-	}
-	uint32_t sta = PMIF_SWINF_STA + chan * PMIF_CHAN_STRIDE;
-	while (hakios_pmif_budget_poll(budget)) {
-		if (((pmif_rd(sta) >> 1) & 7u) == state) {
-			return true;
-		}
-	}
-	return false;
-}
-
-static void
-pmic_recover_on(unsigned chan, struct hakios_pmif_budget *budget)
-{
-	if (chan >= PMIF_CHAN_COUNT) {
-		return;
-	}
-	uint32_t sta = PMIF_SWINF_STA + chan * PMIF_CHAN_STRIDE;
-	if (((pmif_rd(sta) >> 1) & 7u) == SWINF_WFVLDCLR) {
-		pmif_wr(PMIF_SWINF_VLDCLR + chan * PMIF_CHAN_STRIDE, 1u);
-		(void)pmic_wait_on(chan, SWINF_IDLE, budget);
-	}
-}
-
-/*
- * One read on one channel. Split out so that the channel search can try each in turn without
- * duplicating the sequence, and so that a failure says which channel it was on.
- */
-static bool
-pmic_read_on(unsigned chan, uint8_t addr, uint8_t *value)
+pmic_read(uint8_t addr, uint8_t *value)
 {
-	if (chan >= PMIF_CHAN_COUNT || value == NULL) {
-		return false;
-	}
-	uint32_t acc = PMIF_SWINF_ACC + chan * PMIF_CHAN_STRIDE;
-	uint32_t rdata = PMIF_SWINF_RDATA + chan * PMIF_CHAN_STRIDE;
-	uint32_t vldclr = PMIF_SWINF_VLDCLR + chan * PMIF_CHAN_STRIDE;
-	uint32_t cmd;
-	struct hakios_pmif_budget budget = hakios_pmif_budget_begin();
+	pmif_status_t status;
 
-	if (!pmic_wait_on(chan, SWINF_IDLE, &budget)) {
-		pmic_recover_on(chan, &budget);
-		return false;
-	}
-	cmd = (PMIF_CMD_EXT_REG_LONG << 30) | ((uint32_t)PMIC_SID << 24) | (0u << 16) | addr;
-	pmif_wr(acc, cmd);
-	if (!pmic_wait_on(chan, SWINF_WFVLDCLR, &budget)) {
-		pmic_recover_on(chan, &budget);
-		return false;
-	}
-	uint8_t result = (uint8_t)(pmif_rd(rdata) & 0xffu);
-	pmif_wr(vldclr, 1u);
-	if (!pmic_wait_on(chan, SWINF_IDLE, &budget)) {
-		pmic_recover_on(chan, &budget);
+	if (!pmif_ready) {
 		return false;
 	}
-	*value = result;
-	return true;
-}
-
-static bool
-pmic_read(uint8_t addr, uint8_t *value)
-{
-	/*
-	 * Once a channel has carried a transfer, stay on it: a completed transfer is proof of both
-	 * reachability and correct wiring, and re-searching on every poll would cost four failed
-	 * timeouts per key check.
-	 */
-	if (pmif_chan_known) {
-		return pmic_read_on(pmif_chan, addr, value);
-	}
-	for (unsigned chan = 0; chan < PMIF_CHAN_COUNT; chan++) {
-		if (pmic_read_on(chan, addr, value)) {
-			pmif_chan = chan;
-			pmif_chan_known = true;
-			printf("mt6895-keys: the PMIC answered on channel %u (ACC %#x)\n",
-			    chan, PMIF_SWINF_ACC + chan * PMIF_CHAN_STRIDE);
-			return true;
-		}
+	status = pmif_read8(PMIC_SID, addr, value);
+	if (status == PMIF_OK) {
+		pmif_last_keys_status = PMIF_OK;
+		return true;
+	}
+	pmif_keys_failures++;
+	if (status != pmif_last_keys_status) {
+		printf("mt6895-keys: SID %u register %#04x: %s (failure %u)\n", PMIC_SID, addr,
+		    pmif_status_name(status), pmif_keys_failures);
+		pmif_last_keys_status = status;
 	}
 	return false;
 }
@@ -450,11 +360,19 @@
 		poll_ms = 20;
 	}
 	kpd = ml_io_map(KPD_PHYS, KPD_SIZE);
-	pmif = ml_io_map(PMIF_BASE, PMIF_SIZE);
-	if (pmif == 0) {
-		printf("mt6895-keys: the arbiter at %#x will not map; no key can be read\n",
-		    (unsigned)PMIF_BASE);
-		return;
+	/*
+	 * The arbiter's window is mapped once, by the shared path; the headset and the GPU find
+	 * it already up. A failure here is this driver's too: nothing of the PMIC side runs.
+	 */
+	{
+		pmif_status_t mapped = pmif_init();
+
+		pmif_ready = (mapped == PMIF_OK);
+		if (!pmif_ready) {
+			printf("mt6895-keys: the arbiter at %#x will not map (%s); "
+			    "no PMIC key can be read\n", (unsigned)PMIF_BASE,
+			    pmif_status_name(mapped));
+		}
 	}
 	/* Inherit LK's arbiter setup. Use only SWINF_2, without the old timer/gate write probes. */
 	pmic_scan("boot");
@@ -494,11 +412,13 @@
 			 * enabled, and a state machine parked outside IDLE means someone else is
 			 * using the channel.
 			 */
+			uint32_t before = 0, after = 0;
+
 			printf("mt6895-keys: the arbiter did not answer for the PMIC's key register; "
 			    "only the keypad's own line will work\n");
-			printf("mt6895-keys: SWINF_2 sta %#x (fsm %u)\n",
-			    pmif_rd(PMIF_SWINF_STA),
-			    (unsigned)((pmif_rd(PMIF_SWINF_STA) >> 1) & 0x7u));
+			pmif_last_sta(&before, &after);
+			printf("mt6895-keys: SWINF_2 sta %#x (fsm %u), last transaction %#x -> %#x\n",
+			    after, (unsigned)PMIF_STA_FSM(after), before, after);
 		}
 	}
 
--- a/mtk/mt6895/drivers/audio/hakios.mt6895_headset.c
+++ b/mtk/mt6895/drivers/audio/hakios.mt6895_headset.c
@@ -20,23 +20,16 @@
 #include <mach/mach_types.h>
 #include <pexpert/pexpert.h>
 #include <sys/hakios.audio.h>
-#include <sys/hakios.pmif.h>
 #include <sys/systm.h>
 
-extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
+/* The one PMIF transaction path, shared with the keys and the GPU (docs/headset-wip/findings/pmif-shared.h). */
+#include <dev/hakios.pmif.h>
 
-#define PMIF_BASE           0x1c804000
-#define PMIF_SIZE           0x08ff /* accessors enforce the narrower SWINF_2 register allowlist */
-#define PMIF_SWINF_ACC      0x0880
-#define PMIF_SWINF_WDATA    0x0884
-#define PMIF_SWINF_RDATA    0x0894
-#define PMIF_SWINF_VLDCLR   0x08a4
-#define PMIF_SWINF_STA      0x08a8
-#define PMIF_CMD_EXT_REG_LONG   3u
-#define SWINF_IDLE          0x00u
-#define SWINF_WFVLDCLR      0x06u
+extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
 
-#define MT6368_SID          5u
+/* The layout, the command word and the poll budget are <dev/hakios.pmif.h>'s; this file keeps
+ * only which SID and which registers the accdet is. */
+#define MT6368_SID          PMIF_SID_MT6368
 #define ACCDET_RG_LO        0x2688
 #define ACCDET_RG_HI        0x26d3
 #define AUDDEC_ANA_LO       0x2508
@@ -72,7 +65,6 @@
 enum cable_type { CABLE_NONE, CABLE_MIC, CABLE_NO_MIC };
 enum detector_state { PLUG_OUT, MIC_BIAS, HOOK_SWITCH };
 
-static vm_offset_t pmif;
 static uint32_t mode, poll_elapsed;
 static enum cable_type cable;
 static enum detector_state detector;
@@ -87,96 +79,44 @@
 } saved[48];
 static unsigned saved_count;
 
-static inline uint32_t
-pmif_rd(uint32_t at)
-{
-	if (at != PMIF_SWINF_RDATA && at != PMIF_SWINF_STA) {
-		return 0xffffffffu;
-	}
-	uint32_t value = *(volatile uint32_t *)(pmif + at);
-
-	__asm__ volatile ("dsb sy" ::: "memory");
-	return value;
-}
-
-static inline void
-pmif_wr(uint32_t at, uint32_t value)
-{
-	if (at != PMIF_SWINF_ACC && at != PMIF_SWINF_WDATA && at != PMIF_SWINF_VLDCLR) {
-		return;
-	}
-	__asm__ volatile ("dsb sy" ::: "memory");
-	*(volatile uint32_t *)(pmif + at) = value;
-	__asm__ volatile ("dsb sy" ::: "memory");
-}
+/*
+ * The accdet's two accessors, on the shared path. The transaction is pmif_read8/pmif_write8's:
+ * the IDLE wait, the WFVLDCLR wait, the RDATA read before the ACK, the ACK, the second IDLE
+ * wait, the NAK/ERR/timeout distinction, the deadline and the lock. What is this file's is
+ * `mode` -- nothing is written until headset=2 has asked for initialisation -- and the log.
+ *
+ * The status is kept rather than flattened to a bool: the four ways a write can fail (BUSY,
+ * NAK, TIMEOUT, ACK) need different things done about them, and the first of each is worth a
+ * line in the log.
+ */
+static pmif_status_t mt6368_last_status;
 
 static bool
-pmif_wait(uint32_t state, struct hakios_pmif_budget *budget)
-{
-	while (hakios_pmif_budget_poll(budget)) {
-		if (((pmif_rd(PMIF_SWINF_STA) >> 1) & 0x7u) == state) {
-			return true;
-		}
-	}
-	return false;
-}
-
-/* Recover only a valid read awaiting its W1C ACK, and still report the failed transaction. */
-static void
-pmif_recover(struct hakios_pmif_budget *budget)
-{
-	if (((pmif_rd(PMIF_SWINF_STA) >> 1) & 7u) == SWINF_WFVLDCLR) {
-		pmif_wr(PMIF_SWINF_VLDCLR, 1u);
-		(void)pmif_wait(SWINF_IDLE, budget);
-	}
-}
-
-/* Same extended-register transaction as the vendor PMIF driver, with SID 5. */
-static bool
 mt6368_read(uint16_t addr, uint8_t *value)
 {
-	struct hakios_pmif_budget budget = hakios_pmif_budget_begin();
+	pmif_status_t status = pmif_read8(MT6368_SID, addr, value);
 
-	if (!pmif_wait(SWINF_IDLE, &budget)) {
-		pmif_recover(&budget);
-		return false;
-	}
-	pmif_wr(PMIF_SWINF_ACC, (PMIF_CMD_EXT_REG_LONG << 30) | (MT6368_SID << 24) | addr);
-	if (!pmif_wait(SWINF_WFVLDCLR, &budget)) {
-		pmif_recover(&budget);
-		return false;
+	if (status != PMIF_OK && status != mt6368_last_status) {
+		printf("headset: read %#04x: %s\n", addr, pmif_status_name(status));
 	}
-	uint8_t result = (uint8_t)pmif_rd(PMIF_SWINF_RDATA);
-	/* VLDCLR is a W1C transaction ACK. Validate IDLE, not a read-back of the ACK value. */
-	pmif_wr(PMIF_SWINF_VLDCLR, 1u);
-	if (!pmif_wait(SWINF_IDLE, &budget)) {
-		pmif_recover(&budget);
-		return false;
-	}
-	*value = result;
-	return true;
+	mt6368_last_status = status;
+	return status == PMIF_OK;
 }
 
 static bool
 mt6368_write(uint16_t addr, uint8_t value)
 {
-	if (mode != 2 || addr == 0x014b || addr == 0x0240) {
-		return false;
-	}
-	struct hakios_pmif_budget budget = hakios_pmif_budget_begin();
+	pmif_status_t status;
 
-	if (!pmif_wait(SWINF_IDLE, &budget)) {
-		pmif_recover(&budget);
+	if (mode != 2) {
 		return false;
 	}
-	pmif_wr(PMIF_SWINF_WDATA, value);
-	pmif_wr(PMIF_SWINF_ACC,
-	    (PMIF_CMD_EXT_REG_LONG << 30) | (1u << 29) | (MT6368_SID << 24) | addr);
-	if (!pmif_wait(SWINF_IDLE, &budget)) {
-		pmif_recover(&budget);
-		return false;
+	status = pmif_write8(MT6368_SID, addr, value);
+	if (status != PMIF_OK && status != mt6368_last_status) {
+		printf("headset: write %#04x = %#02x: %s\n", addr, value, pmif_status_name(status));
 	}
-	return true;
+	mt6368_last_status = status;
+	return status == PMIF_OK;
 }
 
 static bool
@@ -309,20 +249,47 @@
 	return mt6368_write(AUD_IRQ_STATUS, mask == IRQ_ACCDET ? 0x02 : 0x04);
 }
 
+/*
+ * Put the accdet's registers back.
+ *
+ * can_wait separates the two contexts this runs in, and the difference is a deadlock, not a
+ * style:
+ *
+ *   true   -- from the keys worker, either when initialisation failed or when a poll did.
+ *             The channel may be holding a read whose W1C ACK never came; pmif_recover()
+ *             acks it under the shared lock and touches nothing else (there is no SW_RST in
+ *             this block, and ARB_EN / CMDISSUE_EN / INF_EN / TIMER_CTRL are global).
+ *
+ *   false  -- from hakios_mt6895_headset_quiesce(), which loader_return() calls after
+ *             psci_smp_stop_others(). That call returns with interrupts off on this core
+ *             (osfmk/arm64/psci_smp.c:297, DAIFSet DAIFSC_ALL) after telling the other cores
+ *             to switch themselves off. The shared lock is an lck_spin: lck_spin_lock spins
+ *             until lock-panic (524 ms, osfmk/arm/locks_arm.c:106) and then panics, and it
+ *             has no "give up" return. If the core that held it has been switched off, the
+ *             lock is gone for good. So on this path the lock is not taken: one STA read and
+ *             at most one VLD_CLR store, both of which are this core's alone.
+ *
+ * What false costs: a channel left mid-transaction by a stopped core stays that way, the
+ * restore does nothing, and the next boot's log says the configuration was not restored. The
+ * alternative is a phone that does not come back to its loader, and a run whose log is lost
+ * with it. A headset that does not work is the cheaper failure, and it is the visible one.
+ */
 static void
-restore_configuration(void)
+restore_configuration(bool can_wait)
 {
 	bool ok = true;
-	struct hakios_pmif_budget budget = hakios_pmif_budget_begin();
 
-	/* A stopped keys core may have left a read waiting for its W1C ACK. */
-	if (((pmif_rd(PMIF_SWINF_STA) >> 1) & 7u) == SWINF_WFVLDCLR) {
-		pmif_wr(PMIF_SWINF_VLDCLR, 1);
-	}
-	if (!pmif_wait(SWINF_IDLE, &budget)) {
-		printf("headset: PMIF not idle; configuration restore could not start\n");
-		active = false;
-		return;
+	if (can_wait) {
+		pmif_status_t status = pmif_recover();
+
+		if (status != PMIF_OK) {
+			printf("headset: PMIF not idle (%s); configuration restore could not start\n",
+			    pmif_status_name(status));
+			active = false;
+			return;
+		}
+	} else {
+		pmif_recover_besteffort();
 	}
 	for (unsigned i = saved_count; i > 0; i--) {
 		if (!update_bits(saved[i - 1].addr, saved[i - 1].mask,
@@ -525,10 +492,13 @@
 		printf("headset: unsupported mode %u\n", mode);
 		return;
 	}
-	pmif = ml_io_map(PMIF_BASE, PMIF_SIZE);
-	if (pmif == 0) {
-		printf("headset: cannot map PMIF\n");
-		return;
+	{
+		pmif_status_t mapped = pmif_init();
+
+		if (mapped != PMIF_OK) {
+			printf("headset: cannot map PMIF (%s)\n", pmif_status_name(mapped));
+			return;
+		}
 	}
 	printf("headset: mode %u on SID 5, serialised in keys worker\n", mode);
 	if (!dump_block(ACCDET_RG_LO, ACCDET_RG_HI)) {
@@ -541,7 +511,7 @@
 	first_sample = true;
 	if (!accdet_init_once() || !poll_sample()) {
 		printf("headset: initialisation failed; restoring configuration\n");
-		restore_configuration();
+		restore_configuration(true);
 		return;
 	}
 	printf("headset: mode 2 initialised, polling every 50 ms; PCM playback not implemented\n");
@@ -563,7 +533,7 @@
 	poll_elapsed = 0;
 	if (!poll_sample()) {
 		printf("headset: polling failed; restoring configuration and stopping\n");
-		restore_configuration();
+		restore_configuration(true);
 	}
 }
 
@@ -572,6 +542,7 @@
 hakios_mt6895_headset_quiesce(void)
 {
 	if (mode == 2 && saved_count) {
-		restore_configuration();
+		/* After psci_smp_stop_others(): the lock may be held by a core that is gone. */
+		restore_configuration(false);
 	}
 }
--- a/mtk/mt6895/drivers/gpu/hakios.mali610.c
+++ b/mtk/mt6895/drivers/gpu/hakios.mali610.c
@@ -38,10 +38,15 @@
 #include <mach/mach_types.h>
 #include <pexpert/pexpert.h>
 #include <sys/systm.h>
-#include <sys/hakios.pmif.h>
 #include <vm/vm_kern.h>
 
 #include "hakios.mali610_fw.h"
+/*
+ * The one PMIF transaction path, shared with the keys and the headset. The deadline it used
+ * to take from <sys/hakios.pmif.h> is inside pmif_wait() now, where it can also see the STA
+ * error bit; the lock is taken once around the whole transaction.
+ */
+#include <dev/hakios.pmif.h>
 
 extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);
 extern uint64_t kvtophys(vm_offset_t va);
@@ -3004,112 +3009,52 @@
  * selector 0x6e, 687500 uV, which is the tenth operating point, and reading the selector back
  * before writing anything is the cheap way to notice if that is not true here.
  */
-#define MALI_PMIF_BASE          0x1c804000ull
-#define MALI_PMIF_SIZE          0x08ffu /* mapping granularity does not grant access beyond SWINF_2 */
-#define MALI_PMIF_ACC           0x0880u         /* SWINF_2, the only channel inside the 0x8ff window */
-#define MALI_PMIF_WDATA         0x0884u
-#define MALI_PMIF_RDATA         0x0894u
-#define MALI_PMIF_VLDCLR        0x08a4u
-#define MALI_PMIF_STA           0x08a8u
-#define MALI_PMIF_CMD_EXT_LONG  3u              /* SPMI 0x20..0x2f / 0x38..0x3f, 16-bit registers */
-#define MALI_PMIF_WRITE_BIT     (1u << 29)      /* set on a write, clear on a read */
-#define MALI_PMIF_IDLE          0x00u
-#define MALI_PMIF_WFVLDCLR      0x06u
-
-#define MALI_MT6368_SID         5u              /* pmic@4 in the spmi node is MT6363 at 4 */
+/* The layout, the command word and the deadline are <dev/hakios.pmif.h>'s; what is here is
+ * the MT6368's VBUCK2, the only PMIC register the GPU touches. */
+#define MALI_MT6368_SID         PMIF_SID_MT6368
 #define MALI_VBUCK2_EN          0x0240u
 #define MALI_VBUCK2_EN_SET      (MALI_VBUCK2_EN + 1u)
 #define MALI_VBUCK2_EN_MASK     (1u << 2)
 #define MALI_VBUCK2_VSEL        0x024eu
 #define MALI_VBUCK2_VSEL_MASK   0xffu
 
-static vm_offset_t gPmif;
-
-static uint32_t
-pmif_rd(uint32_t at)
-{
-	if (at != MALI_PMIF_RDATA && at != MALI_PMIF_STA) {
-		return 0xffffffffu;
-	}
-	return reg_rd(gPmif, at);
-}
-
-static void
-pmif_wr(uint32_t at, uint32_t value)
-{
-	if (at == MALI_PMIF_ACC || at == MALI_PMIF_WDATA || at == MALI_PMIF_VLDCLR) {
-		reg_wr(gPmif, at, value);
-	}
-}
-
-static bool
-mali_pmif_wait(uint32_t state, struct hakios_pmif_budget *budget)
-{
-	while (hakios_pmif_budget_poll(budget)) {
-		if (((pmif_rd(MALI_PMIF_STA) >> 1) & 7u) == state) {
-			return true;
-		}
-	}
-	return false;
-}
-
-static void
-mali_pmif_recover(struct hakios_pmif_budget *budget)
-{
-	if (((pmif_rd(MALI_PMIF_STA) >> 1) & 7u) == MALI_PMIF_WFVLDCLR) {
-		pmif_wr(MALI_PMIF_VLDCLR, 1u);
-		(void)mali_pmif_wait(MALI_PMIF_IDLE, budget);
-	}
-}
-
 /*
- * One SPMI transaction. The command word is the arbiter's, not the PMIC's: bits 31..30 are the
- * PMIF's own opcode translation, bit 29 marks a write, 28..24 the bus id, 23..16 the length minus
- * one, 15..0 the register. Leaving bit 29 out is the failure mode that costs an afternoon -- the
- * transfer runs, the state machine reaches the same place, and the value never arrives.
+ * The GPU's one PMIC read and write, on the shared path. Nothing of the transaction is left
+ * here: the IDLE wait, the write path that must not wait for WFVLDCLR (a write produces no
+ * valid word), the RDATA read before the ACK, the ACK, the second IDLE wait, the deadline
+ * and the lock are pmif_read8/pmif_write8's.
+ *
+ * This is the file that had the worst of the defects: its write path waited for WFVLDCLR,
+ * which a write never reaches, so mali_vstack_rail()'s write of 0x241 always reported a
+ * failure after a full timeout while the write itself had landed -- a log that said the
+ * opposite of what the hardware did.
  */
+static pmif_status_t mali_pmif_status;
+
 static bool
 mali_spmi(uint32_t addr, uint32_t *value, bool write)
 {
-	if (value == NULL || addr > 0xffffu ||
-	    (write && (addr == 0x014bu || addr == 0x0240u || *value > 0xffu))) {
-		return false;
-	}
-	uint32_t cmd = (MALI_PMIF_CMD_EXT_LONG << 30) |
-	    (write ? MALI_PMIF_WRITE_BIT : 0u) |
-	    (MALI_MT6368_SID << 24) | addr;
-	struct hakios_pmif_budget budget = hakios_pmif_budget_begin();
+	pmif_status_t status;
 
-	if (!mali_pmif_wait(MALI_PMIF_IDLE, &budget)) {
-		mali_pmif_recover(&budget);
+	if (value == NULL || addr > 0xffffu || (write && *value > 0xffu)) {
 		return false;
 	}
 	if (write) {
-		pmif_wr(MALI_PMIF_WDATA, *value);
-	}
-	pmif_wr(MALI_PMIF_ACC, cmd);
-	/* Writes complete at IDLE and produce no valid read data or VLDCLR handshake. */
-	if (write) {
-		if (!mali_pmif_wait(MALI_PMIF_IDLE, &budget)) {
-			mali_pmif_recover(&budget);
-			return false;
+		status = pmif_write8((uint8_t)MALI_MT6368_SID, (uint16_t)addr, (uint8_t)*value);
+	} else {
+		uint8_t byte = 0;
+
+		status = pmif_read8((uint8_t)MALI_MT6368_SID, (uint16_t)addr, &byte);
+		if (status == PMIF_OK) {
+			*value = byte;
 		}
-		return true;
 	}
-	if (!mali_pmif_wait(MALI_PMIF_WFVLDCLR, &budget)) {
-		printf("mali: spmi %s %#x stalled at state %u\n", write ? "write" : "read", addr,
-		    (unsigned)((pmif_rd(MALI_PMIF_STA) >> 1) & 0x7u));
-		mali_pmif_recover(&budget);
-		return false;
-	}
-	uint32_t result = pmif_rd(MALI_PMIF_RDATA) & 0xffu;
-	pmif_wr(MALI_PMIF_VLDCLR, 1u);
-	if (!mali_pmif_wait(MALI_PMIF_IDLE, &budget)) {
-		mali_pmif_recover(&budget);
-		return false;
+	if (status != PMIF_OK && status != mali_pmif_status) {
+		printf("mali: spmi %s %#x: %s\n", write ? "write" : "read", addr,
+		    pmif_status_name(status));
 	}
-	*value = result;
-	return true;
+	mali_pmif_status = status;
+	return status == PMIF_OK;
 }
 
 /*
@@ -3125,11 +3070,14 @@
 {
 	uint32_t status = 0, vsel = 0, four = MALI_VBUCK2_EN_MASK;
 
-	gPmif = ml_io_map(MALI_PMIF_BASE, MALI_PMIF_SIZE);
-	if (gPmif == 0) {
-		printf("mali: the arbiter at %#llx will not map; the stack rail stays as it is\n",
-		    (unsigned long long)MALI_PMIF_BASE);
-		return;
+	{
+		pmif_status_t mapped = pmif_init();
+
+		if (mapped != PMIF_OK) {
+			printf("mali: the arbiter at %#x will not map (%s); the stack rail stays as it is\n",
+			    (unsigned)PMIF_BASE, pmif_status_name(mapped));
+			return;
+		}
 	}
 	if (!mali_spmi(MALI_VBUCK2_EN, &status, false)) {
 		return;
```
