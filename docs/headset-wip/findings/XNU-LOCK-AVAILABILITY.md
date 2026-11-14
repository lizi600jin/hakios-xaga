# XNU 锁可用性：哪个启动阶段能用哪个原语

> 问题：三份驱动（`hakios.mt6895_keys.c` / `hakios.mt6895_headset.c` / `hakios.mali610.c`）无锁共用
> SPMI/PMIF 通道 SWINF_2，要加互斥。能不能用 `lck_mtx`？
>
> 结论先说：**能，而且不需要建任何 `lck_grp`**。静态声明的锁在 `STARTUP_SUB_LOCKS` 阶段就已初始化完，
> 那是 `STARTUP_SUB_ZALLOC` 之前、`bsd_init()` 之前很久。项目里已有 4 处同类用法可以直接抄。
> **但推荐用的不是 `lck_mtx`，而是项目已经建好的 `hakios_spmi_lock()`（`lck_spin`）。**
>
> 所有结论只读自源码，`文件:行号` 逐条给出。凡属读代码推断而非实测的，标「推断」或「未验证」。

- 源码树：`/home/lizi/IOS/hakios-mtk/xnu-src/xnu-8796.121.2/`（下称 `XNU/`）
- 项目补丁：`/home/lizi/IOS/hakios-mtk/xnu/files/`（下称 `PATCHED/`）
- 驱动：`/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/`、`/home/lizi/IOS/hakios-mtk/drivers/`
- 复核脚本：`docs/headset-wip/lock-probe.sh`（本文每条 grep 都在里面，可重跑）

> **行号会漂。** 本文写就期间（2026-11-15 00:06–00:22），`mtk/mt6895/drivers/` 下三份驱动被
> 并行改动过（`keys.c` 里 `pmic_read_on` 从 220 移到 244，`for(;;)` 从 548 移到 549，
> `headset_init` 调用从 472 移到 508）。本文行号已按 00:22 的版本校正。
> **XNU 源码（`XNU/`）在本轮未被改动**，那些行号稳定。
> 引用驱动行号前请先跑一遍 `lock-probe.sh`——它以实际文件为准，不依赖本文。

---

## 0. 一句话判定

| 问题 | 答案 | 依据 |
|---|---|---|
| `hakios_input_init()` 时 `lck_mtx` 能用吗 | **能** | 它在 `bsd_init()` 里（`XNU/bsd/kern/bsd_init.c:988`），`bsd_init()` 由 `kernel_bootstrap_thread()` 在 `STARTUP_SUB_LOCKDOWN` 之后调用（`XNU/osfmk/kern/startup.c:751,794`）；`LOCKS` 是第 3 号相位（`XNU/osfmk/kern/startup.h:62`） |
| 需要 `lck_grp_alloc_init()` 吗 | **不需要** | `LCK_GRP_DECLARE` 把组放进 `__startup_data` 段，由 `STARTUP_ARG(LOCKS, RANK_SECOND, lck_grp_startup_init)` 自动初始化（`XNU/osfmk/kern/lock_group.h:196-201`）——**不碰 zalloc** |
| 能传 `grp = NULL` 给 `lck_mtx_init` 吗 | **不能** | `lck_mtx_init` 无条件 `grp->lck_grp_attr_id`（`XNU/osfmk/kern/lock_mtx.c:279`）与 `grp->lck_grp_mtxcnt`（`:285`），无 NULL 检查 → 解引用 0 → 崩 |
| 能传 `grp = NULL` 给 `lck_spin_init` 吗 | **能** | `if (grp) { ... }` 保护（`XNU/osfmk/arm/locks_arm.c:777`；ticket 版 `XNU/osfmk/kern/lock_ticket.c:242`） |
| `lck_mtx` 能在中断上下文用吗 | **不能** | `ml_at_interrupt_context()` 直接 panic（`XNU/osfmk/kern/lock_mtx.c:229-233`，仅 `DEVELOPMENT\|\|DEBUG`） |
| 持 `lck_spin` 期间 `delay()` 会怎样 | **超过 524 ms 就 panic** | `hw_lock_spin_policy.hwsp_timeout_atomic = &lock_panic_timeout`（`XNU/osfmk/kern/locks.c:449-453`），`lock-panic` 默认 `0xc00000` timebase ticks ≈ 524 ms（`XNU/osfmk/arm/locks_arm.c:106-107`） |
| 项目里已有现成用法吗 | **有，且有一把就是给 PMIC 仲裁器的** | `hakios_spmi_lock()`（`/home/lizi/IOS/hakios-mtk/drivers/core/hakios.platform.c:32-44`，`LCK_SPIN_DECLARE`） |

---

## 1. 启动阶段 vs 锁可用性（时间线）

相位序号取自 `startup_subsystem_id_t` 的声明顺序（`XNU/osfmk/kern/startup.h:58-79`），
`kernel_startup_initialize_upto()` 逐相位调用登记项（`XNU/osfmk/kern/startup.c:396-421`）。

```
阶段(枚举值)            内核里发生了什么                     此刻可用的同步原语
─────────────────────────────────────────────────────────────────────────────────────
STARTUP_SUB_NONE      = 0   版本串打印、scale_setup
STARTUP_SUB_TUNABLES  = 1   boot-arg / tunables
STARTUP_SUB_TIMEOUTS  = 2   machine timeout 表
                             ↓
STARTUP_SUB_LOCKS     = 3   ★ lck_group_init (RANK_FIRST      ★ LCK_GRP_DECLARE / LCK_MTX_DECLARE /
       │                     lock_group.c:151)                  LCK_SPIN_DECLARE 的锁此刻全部 init 完
       │                     lck_grp_startup_init (RANK_2)       ← 不依赖 zalloc
       │                     lck_mtx_startup_init (RANK_4)
       │                     lck_spin_startup_init (RANK_4)
       │                     lck_init_pv (locks_arm.c:618, RANK_1)
       ↓
STARTUP_SUB_KPRINTF   = 4   kprintf
STARTUP_SUB_PMAP_STEAL= 5   pmap carveout
STARTUP_SUB_KMEM      = 6   kmem_alloc 就绪   ← 从这里起 kernel_memory_allocate 类可用
STARTUP_SUB_ZALLOC    = 7   zone_init (zalloc.c:10179),  ← 从这里起 zalloc 可用
       │                     kalloc_init (kalloc.c:1516)        → lck_*_alloc_init 可用
       │                                                        → lck_grp_alloc_init 可用
       ↓
STARTUP_SUB_KTRACE    = 8
STARTUP_SUB_PERCPU    = 9
STARTUP_SUB_EVENT     = 10
STARTUP_SUB_CODESIGN  = 11
STARTUP_SUB_OSLOG     = 12
STARTUP_SUB_MACH_IPC  = 13
STARTUP_SUB_THREAD_CALL=14
STARTUP_SUB_SYSCTL    = 15
STARTUP_SUB_EARLY_BOOT= 16  PE_init_iokit, spllo() 打开中断  ← lck_mtx 的抢占不变量检查从
       │                     (startup.c:689-705)                 startup_phase >= 此值 起生效
       ↓
STARTUP_SUB_LOCKDOWN  = ~0  OSKextRemoveKextBootstrap,
       │                     machine_lockdown, PE_lockdown_iokit
       ↓
   bsd_init()               ★ hakios_input_init() 在这里
       │                       bsd_init.c:988
       │                     → hakios_mt6895_keys_init()  :236
       │                     → hakios_mali610_init()      :243
       │                       └ kernel_thread_start → delay(2000ms)
       │                         → mali_thread → mali_bring_up
       ↓
   kernel_bootstrap_thread 之后才 vm_pageout()、pageout daemon
```

### 1.1 `hakios_input_init()` 的精确位置

调用点：`XNU/bsd/kern/bsd_init.c:988`，在 `#if DEVFS` 块里、`devfs_kernel_mount()`（`:986`）之后：

```c
	devfs_kernel_mount(mounthere);
	hakios_version_init();
	hakios_input_init();           /* /dev/hakios-touch */
	hakios_audio_init();
	...
```

`hakios_input_init()` 定义在 `/home/lizi/IOS/hakios-mtk/drivers/input/hakios.input.c:221-249`，
它同步调用 `hakios_mt6895_keys_init()`（`:236`）和 `hakios_mali610_init()`（`:243`）。

`bsd_init()` 的唯一调用点是 `XNU/osfmk/kern/startup.c:794`，位于
`kernel_startup_initialize_upto(STARTUP_SUB_LOCKDOWN)`（`:751`）之后。

> **相对锁初始化的位置：之后很多个阶段。** 全部 4 个 `lck_*_startup_init` 都在 `STARTUP_SUB_LOCKS`(3)
> 跑完，`zalloc` 在 `STARTUP_SUB_ZALLOC`(7) 才就绪，而 `hakios_input_init()` 在 `LOCKDOWN` 之后。
> **结论：静态锁与动态分配锁，在 `hakios_input_init()` 都可用。**

### 1.2 三个驱动的线程上下文（这是选原语的真正约束）

| 驱动 | 初始化调用点 | 真正跑 PMIF 的地方 | 上下文 |
|---|---|---|---|
| `hakios.mt6895_keys.c` | `hakios_input_init()` → `:236` | `keys_thread` `for (unsigned tick = 1;; tick++)`（`:549`），每圈 `delay(poll_ms * 1000)`（`:656`） | 内核线程，**可睡眠**（抢占未关） |
| `hakios.mt6895_headset.c` | keys 线程内联调用 `hakios_mt6895_headset_init()`（`keys.c:508`）、`hakios_mt6895_headset_poll()`（`keys.c:655`） | 同 keys 线程 | 内核线程，与 keys 串行 |
| `hakios.mali610.c` | `hakios_input_init()` → `:243` → `kernel_thread_start(mali_thread,...)`（`:5248`） | `mali_thread` 先 `delay(2000 * 1000)`（`:5088`）再 `mali_bring_up()` | **独立内核线程**，与 keys 线程真并发 |

关键词在 `mali610.c:5088`：`delay(2000 * 1000)` 是 2 s，注释写 "let the rest of the machine settle first"。
2 s 后 keys 线程的 `for(;;)` 早已在跑，两者**必然重叠**——这就是本任务要解决的问题。

> `hakios.mt6895_headset.c` 自己初始化时也有 `delay(2000)`（`:241`），
> 但它在 keys 线程里、两笔 PMIF 事务之间，不构成“持锁睡觉”的场景（除非加锁方式写错，见 §7）。

---

## 2. 逐原语判定表

「最早可用」一列以相位为准；「在 `hakios_input_init` 时」一列是对 `bsd_init()` 调用的判定。

### 2.1 互斥锁 `lck_mtx_t`

| 接口 | 最早可用相位 | `hakios_input_init` 时可用 | 中断上下文 | 依据 |
|---|---|---|---|---|
| `LCK_MTX_DECLARE(var, grp)` | `LOCKS`(3) | ✅ | ❌ | `XNU/osfmk/kern/lock_mtx.h:184-192`；`STARTUP_ARG(LOCKS, RANK_FOURTH, lck_mtx_startup_init)` |
| `lck_mtx_init(lck, grp, attr)` | 任意（纯内存写，**但 grp 不能 NULL**） | ✅ | ❌ | `XNU/osfmk/kern/lock_mtx.c:271-286` |
| `lck_mtx_alloc_init(grp, attr)` | `ZALLOC`(7) | ✅ | ❌ | 同文件 `:254-260`，`zalloc(KT_LCK_MTX)` 在 `:258` |
| `lck_mtx_lock` | 同上 | ✅ | ❌ **panic** | `:803` 调 `lck_mtx_check_irq`；`ml_at_interrupt_context()` → `__lck_mtx_at_irq_panic`（`:200-203, 229-233`） |
| `lck_mtx_unlock` | 同上 | ✅ | ❌ | `:307-` 快路径 `lock_cmpxchgv`，慢路径 `lck_mtx_unlock_slow` |

`lck_mtx_init` 的 `grp` 处理（**这是任务里问的重点**）：

```c
void
lck_mtx_init(lck_mtx_t *lck, lck_grp_t *grp, lck_attr_t *attr)
{
	if (attr == LCK_ATTR_NULL) {
		attr = &lck_attr_default;
	}

	*lck = (lck_mtx_t){
		.lck_mtx_type = LCK_TYPE_MUTEX,
		.lck_mtx_grp  = grp->lck_grp_attr_id,      /* ← 无 NULL 检查 */
	};
	if (attr->lck_attr_val & LCK_ATTR_DEBUG) {
		lck->lck_mtx.data |= LCK_MTX_PROFILE;
	}

	lck_grp_reference(grp, &grp->lck_grp_mtxcnt);  /* ← 又一次解引用 */
}
```

（`XNU/osfmk/kern/lock_mtx.c:270-286`）

- **`grp` 不能传 `NULL`**：`grp->lck_grp_attr_id`（`:280`）与 `grp->lck_grp_mtxcnt`（`:285`）都直接解引用。
- `attr` 可以传 `LCK_ATTR_NULL`，会被换成 `&lck_attr_default`（`:272-274`）——而这个默认值本身由
  `lck_group_init` 填（`XNU/osfmk/kern/lock_group.c:126-133`，`STARTUP(LOCKS, RANK_FIRST)`）。
  所以 `lck_mtx_init` 的**最早安全时刻是 `LOCKS` 阶段 RANK_FIRST 之后**，不是“任意时刻”。
- 对比 `lck_spin_init` 有保护：

```c
void
lck_spin_init(lck_spin_t *lck, lck_grp_t *grp, __unused lck_attr_t *attr)
{
	lck->type = LCK_SPIN_TYPE;
	hw_lock_init(&lck->hwlock);
	if (grp) {                                     /* ← 有 NULL 检查 */
		lck_grp_reference(grp, &grp->lck_grp_spincnt);
	}
}
```

（`XNU/osfmk/arm/locks_arm.c:773-780`；ticket 版同构，`XNU/osfmk/kern/lock_ticket.c:228-247`）

### 2.2 自旋锁 `lck_spin_t`

**先纠正一个容易搞错的点**：arm64 上的 `lck_spin_t` **不是** ticket lock。
`LCK_SPIN_IS_TICKET_LOCK` 在全树只出现于 `XNU/osfmk/arm/locks.h:33-34`，定义为 `0`，**没有任何地方覆盖它**
（`rg LCK_SPIN_IS_TICKET_LOCK` 全树 6 处，均为 `#if`/`#define 0`）。arm64 通过
`XNU/osfmk/machine/locks.h:34` → `#include "arm/locks.h"` 拿到这个 0，
所以 `locks_arm.c:725-919`（`!LCK_SPIN_IS_TICKET_LOCK` 分支）才是实际编译的那一套：
`lck_spin_t` = `{ type, hwlock }`，`lck_spin_lock` 直接落到 `hw_lock_lock`。

（ticket 那一套在 `locks_arm.c:623-724`，**本移植不编译**。但它仍是 `lck_ticket_t` 自己的实现，
`lck_spin` 与 `lck_ticket` 是两个类型。）

| 接口 | 最早可用 | `hakios_input_init` 时可用 | 中断上下文 | 依据 |
|---|---|---|---|---|
| `LCK_SPIN_DECLARE(var, grp)` | `LOCKS`(3) | ✅ | ✅（不会睡） | `XNU/osfmk/kern/locks.h:1255-1263`；`STARTUP_ARG(LOCKS, RANK_FOURTH, lck_spin_startup_init)` |
| `lck_spin_init(lck, grp, attr)` | `LOCKS` 之后；**`grp` 可 NULL** | ✅ | ✅ | `XNU/osfmk/arm/locks_arm.c:773-780`（`if (grp)`） |
| `lck_spin_alloc_init(grp, attr)` | `ZALLOC`(7) | ✅ | ✅ | 同文件 `:756-762`，`zalloc(KT_LCK_SPIN)` |
| `lck_spin_lock` | 同上 | ✅ | ✅ | `:820-824` → `hw_lock_lock(&lock->hwlock, LCK_GRP_NULL)` |
| `lck_spin_unlock` | 同上 | ✅ | ✅ | `:880-884` → `hw_lock_unlock` |

**抢占语义（重要）**：`hw_lock_lock` 自己关抢占，`hw_lock_unlock` 自己恢复：

```c
void
(hw_lock_lock)(hw_lock_t lock LCK_GRP_ARG(lck_grp_t *grp))
{
	thread_t thread = current_thread();
	lock_disable_preemption_for_thread(thread);       /* locks.c:696 */
	(void)hw_lock_to_internal(lock, thread, &hw_lock_spin_policy
	    LCK_GRP_ARG(grp));
}

void
(hw_lock_unlock)(hw_lock_t lock)
{
	hw_lock_unlock_internal(lock);
	lock_enable_preemption();                         /* locks.c:821 */
}
```
（`XNU/osfmk/kern/locks.c:692-699`、`:817-822`）

`_nopreempt` 变体：`hw_lock_lock_nopreempt` 先用 `__lck_require_preemption_disabled` 断言抢占已关
（`locks.c:706-713`），`hw_lock_unlock_nopreempt` 只放锁不动抢占（`:824-828`）。

> 含义：**用 `lck_spin_lock/unlock` 成对调用即可，不需要自己 `disable_preemption()`**；
> 但**不能**用 `lck_spin_lock` 配 `lck_spin_unlock_nopreempt`，那样抢占计数会泄漏。
> `lck_spin_lock` 在中断上下文是安全的（不睡眠、不查 IRQ），这是它相对 `lck_mtx` 的关键优势。

### 2.3 静态 vs 动态（任务问的「有没有不需要 zalloc 的静态初始化方式」）

**有，而且这是本项目的正解。** `LCK_*_DECLARE` 系列把锁本体放 `.data`/`__DATA,__lock_grp`，
把初始化参数放进 `__startup_data` 段，并登记到 `LOCKS` 相位：

```c
#define LCK_GRP_DECLARE_ATTR(var, name, flags) \
	__PLACE_IN_SECTION("__DATA,__lock_grp") lck_grp_t var; \
	static __startup_data struct lck_grp_spec \
	__startup_lck_grp_spec_ ## var = { &var, name, flags }; \
	STARTUP_ARG(LOCKS, STARTUP_RANK_SECOND, lck_grp_startup_init, \
	    &__startup_lck_grp_spec_ ## var)
```
（`XNU/osfmk/kern/lock_group.h:196-201`）

```c
#define LCK_MTX_DECLARE_ATTR(var, grp, attr) \
	lck_mtx_t var; \
	static __startup_data struct lck_mtx_startup_spec \
	__startup_lck_mtx_spec_ ## var = { &var, grp, attr }; \
	STARTUP_ARG(LOCKS, STARTUP_RANK_FOURTH, lck_mtx_startup_init, \
	    &__startup_lck_mtx_spec_ ## var)
```
（`XNU/osfmk/kern/lock_mtx.h:184-189`）

`lck_mtx_startup_init` 就是薄薄一层 `lck_mtx_init(sp->lck, sp->lck_grp, sp->lck_attr)`
（`XNU/osfmk/kern/locks.c:3247-3252`）。**全程无 zalloc、无 kmem。**

> **能不能「零初始化 + 直接用」？不能。** `LCK_TYPE_MUTEX = 0x22`、`LCK_TYPE_NONE = 0x00`
> （`XNU/osfmk/kern/locks_internal.h:151-156`）。bss 里的 `lck_mtx_t` type 字段是 0，`lck_mtx_lock`
> 慢路径会判 `lock->lck_mtx_type != LCK_TYPE_MUTEX` 而 `__lck_mtx_invalid_panic`（`lock_mtx.c:819-825`）。
> 快路径（无竞争）只看 owner 字段，`0` 恰好表示未持有 —— 所以**零初始化的锁在没有竞争时“看起来能用”，
> 一旦有竞争就 panic**。这是最坏的一种 bug，必须避免。（此段为读代码推断，未在机器上复现。）

### 2.4 `hw_lock_*`

| 接口 | 最早可用 | 可用性 | 中断上下文 | 依据 |
|---|---|---|---|---|
| `hw_lock_init(lock)` | 任意（`ordered_store_hw(lock, 0)`） | ✅ | ✅ | `XNU/osfmk/kern/locks.c:340-343` |
| `hw_lock_lock(lock, grp)` | 任意；`grp` 仅用于统计，可为 `LCK_GRP_NULL` | ✅ | ✅ | `XNU/osfmk/kern/locks.c:692-699`（`lock_disable_preemption_for_thread` + `hw_lock_to_internal`）；`arm/locks_arm.c:823` 的 `lck_spin_lock` 正是调它 |
| `hw_lock_try` / `hw_lock_unlock` | 同上 | ✅ | ✅ | `locks_arm.c:853`、`:883` |
| 类型 | `struct hslock { uintptr_t lock_data; }`，一个机器字 | — | — | `XNU/osfmk/kern/lock_types.h:89-92` |

`hw_lock` 是最底层、**完全无依赖**的一层（`hw_lock_init` 只写一次 `0`）。但它是**内部接口**：
项目里零使用（见 §3），也没有 `LCK_*_DECLARE` 那样的静态包装，要自己管初始化时机。
**不推荐给驱动用**——它是一个裸字，没有任何类型校验/归属检查/统计。

### 2.5 原子操作

| 接口 | 最早可用 | 可用性 | 中断上下文 | 依据 |
|---|---|---|---|---|
| `os_atomic_*`（`os_atomic_cmpxchg` / `os_atomic_inc` 等） | 任意（纯指令） | ✅ | ✅ | 项目已在用：`drivers/storage/hakios.ufshc.c:800`、`drivers/power/hakios.cpufreq.c:180` |
| `OSIncrementAtomic` / `OSCompareAndSwap`（libkern 老 API） | 任意 | ✅ | ✅ | 项目已在用：`drivers/audio/hakios.mic.c:1282`、`hakios.sound.c:834` |
| `__sync_lock_test_and_set` | 任意（编译器内建） | ✅ | ✅ | 项目未用；等价于 `os_atomic_xchg(..., acquire)`，可用 |

原子操作**没有初始化依赖**，是 §6 退路的基础。但它们只能保护单字，**不能保护一笔多寄存器的 PMIF 事务**
（ACC 写 → 轮询 STA → 读 RDATA → 写 VLDCLR 是一个序列）。做**开销标记**可以，做**互斥**不行。

### 2.6 中断/抢占控制

| 接口 | 最早可用 | 可用性 | 中断上下文 | 依据 |
|---|---|---|---|---|
| `ml_set_interrupts_enabled(enable)` | **任意**（纯 DAIF 读写） | ✅ | ✅ | `XNU/osfmk/arm/machine_routines_common.c:1037-1085`：`__builtin_arm_rsr("DAIF")` / `wsr("DAIFClr"/"DAIFSet")`，无状态依赖 |
| `ml_get_interrupts_enabled()` | 任意 | ✅ | ✅ | 同文件 `:1101`；`spl.h:35-37` 里 `splhigh()/splsched()/splclock()` 就是它的宏 |
| `disable_preemption()` / `enable_preemption()` | `PERCPU` 之后才可靠（依赖 `current_thread()`） | ✅（此时早于 `EARLY_BOOT`，per-cpu 已就绪） | ⚠️ | `XNU/osfmk/kern/cpu_data.h:48-55`；`mp_disable_preemption()` 在 `XNU/osfmk/arm/cpu_data.h:129-130` 是同一函数的别名 |
| `get_preemption_level()` | 同上 | ✅ | — | 被 `delay` 分支使用（`XNU/osfmk/kern/clock.c:1584`） |

`ml_set_interrupts_enabled` 的实现在 arm64 上是**纯寄存器操作**，这是唯一“绝对任何时刻都能用”的原语：

```c
	state = __builtin_arm_rsr("DAIF");
	...
	__builtin_arm_wsr("DAIFClr", DAIFSC_STANDARD_DISABLE);   /* enable */
	...
	__builtin_arm_wsr("DAIFSet", DAIFSC_STANDARD_DISABLE);   /* disable */
	return (state & DAIF_IRQF) == 0;
```
（`XNU/osfmk/arm/machine_routines_common.c:1046, 1074, 1076, 1085`）

> ⚠️ **关中断不能当跨 CPU 互斥用。** 它只挡本 CPU 的中断；`mali_thread` 在另一个 CPU 上照样进 PMIF。
> 裸机上还要注意 `mt6895` 是多核启动（`xnu-cpus7.img` 之类的镜像名暗示 8 核全开）。
> 要跨 CPU 互斥，必须用 `lck_spin` 这类带原子交换的原语。

### 2.7 `simple_lock`（老 API）

| 接口 | 状态 | 依据 |
|---|---|---|
| `simple_lock_init(&l, 0)` / `simple_lock(&l, grp)` | **存在，但只是 `lck_spin` 的宏** | `PATCHED/osfmk/minicrypto/mc_provider.c:54,61,78` 在用；arm64 上 `simple_lock_init` = `arm_usimple_lock_init`（`XNU/osfmk/arm/simple_lock.h:119`），`simple_lock(l,grp)` = `lck_spin_lock_grp(l,grp)`（`:122`） |
| 建议 | **不要新用**。它的名字暗示“简单”，实际语义与 `lck_spin` 相同，且 `simple_lock_init` 的第二参数（`0`）是遗留位，容易被误读 | — |

---

## 3. 项目里已有的用法（不是零命中）

任务描述担心“项目从未用过锁”。**实际上项目用得很多，而且有一把就是本任务要的。**

### 3.1 最相关：`hakios_spmi_lock()` —— 已经存在的 PMIC 仲裁器锁

`/home/lizi/IOS/hakios-mtk/drivers/core/hakios.platform.c:25-44`：

```c
static LCK_GRP_DECLARE(hakios_platform_lock_group, "hakios-platform");

/*
 * One transfer at a time through the PMIC arbiter for this system, whoever asks: with two at once the arbiter drops
 * one ("the transfer was dropped"), as Linux never lets happen (one lock for the bus). A spin lock: it is taken
 * around a transfer of some tens of microseconds.
 */
static LCK_SPIN_DECLARE(spmi_lock, &hakios_platform_lock_group);

void
hakios_spmi_lock(void)
{
	lck_spin_lock(&spmi_lock);
}

void
hakios_spmi_unlock(void)
{
	lck_spin_unlock(&spmi_lock);
}
```

声明在 `/home/lizi/IOS/hakios-mtk/xnu/files/bsd/sys/hakios.input.h:70-71`：

```c
/* one transfer at a time through the PMIC arbiter for this system (drivers/input/hakios.buttons.c) */
void hakios_spmi_lock(void);
void hakios_spmi_unlock(void);
```

**已经在用的驱动**（全部在 `drivers/` 下，`grep hakios_spmi_lock` 命中）：

| 驱动 | 位置 |
|---|---|
| `drivers/input/hakios.buttons.c` | `:155` lock / `:163` unlock |
| `drivers/power/hakios.pmic.c` | `:126` / `:134`（自己 extern 声明，`:56-57`） |
| `drivers/camera/hakios.torch.c` | `:205` / `:213` |
| `drivers/camera/hakios.camera.c` | `:517` / `:525` |

它的注释已经把问题说透：*"with two at once the arbiter drops one ('the transfer was dropped')"* ——
**这正是本任务三份驱动要做的事**。

### 3.2 `lck_mtx` 的现成模式（`drivers/input/hakios.input.c`）

```c
static LCK_GRP_DECLARE(touch_lck_grp, "hakios-touch");
static LCK_MTX_DECLARE(touch_lock, &touch_lck_grp);
```
（`/home/lizi/IOS/hakios-mtk/drivers/input/hakios.input.c:60-61`）

用法：`:115` lock / `:134` unlock；`:169` lock 后 `msleep(&touch_queue, &touch_lock, ...)`（`:171`），
错误路径先 `:173` unlock 再 `break`，`:179`/`:181` 一放一拿，`:191` 收尾 unlock。
**这就是“错误路径解锁”的正确写法**，可以照抄。

### 3.3 全项目统计（`docs/headset-wip/lock-probe.sh` §6 的实际输出）

`lck_mtx` / `lck_spin` / `IOLock` / `hw_lock` / `LCK_*_DECLARE` 命中最多的文件：

```
  84  drivers/modem/hakios.mpss.c
  65  drivers/wifi/hakios.wifi.c
  65  xnu/files/bsd/miscfs/ramfs/ramfs.c
  56  xnu/files/iokit/Kernel/MiniIOSurface.cpp
  54  drivers/camera/hakios.camera.c
  46  drivers/audio/hakios.adsp.c
  38  drivers/storage/hakios.ufs.c
   9  drivers/input/hakios.input.c
   8  drivers/core/hakios.platform.c
   0  mtk/mt6895/drivers/**            ← 见下
```

**注意最后一行**：`mtk/mt6895/drivers/` 下三份驱动所在目录，**整目录零命中**。
这印证了任务背景：“三份驱动无锁共用 SWINF_2”。（`xnu/files` 是补丁树，`drivers/` 是实际源码树，
两者内容一致；`mtk/mt6895/drivers/` 是本轮要改的地方。）

三份驱动的逐个计数（`lock-probe.sh` §7 实测，2026-11-15 00:22，在它们被并行改动之后重跑）：

```
hakios.mt6895_keys.c     : 0 lock-primitive occurrences
hakios.mt6895_headset.c  : 0 lock-primitive occurrences
hakios.mali610.c         : 0 lock-primitive occurrences
```

> 各文件的 `ml_io_map(PMIF_BASE, ...)` 与 `PMIF_SWINF_ACC 0x0880` 都还在原位
> （`keys.c:455`/`:132`、`headset.c:523`/`:29`、`mali610.c:3126`/`:3008`），
> 说明改动没有触及 PMIF 访问路径——**锁的缺口仍然存在，本报告的结论不变**。

### 3.4 旧文档的「零命中」结论需修正

`docs/headset-wip/findings/SPMI-PMIF-SPEC.md:428` 写：

> **完全缺失**。三份文件里没有任何 `lck_mtx` / `lck_spin` / `IOLock` / 关中断 | 全文 grep 零命中

**对三份驱动本身，这句是对的**（本报告 §7 复核：各 0 处）。但它容易读成“项目里没有锁可用”。
事实上项目**已经有 `hakios_spmi_lock()` 这把专为 PMIC 仲裁器建的自旋锁**，只是三份驱动没用它。
`SPMI-PMIF-SPEC.md:574` 的 U6「XNU 侧没有现成的跨线程互斥」**不成立**，应改为
“已有 `lck_spin` 可用，见 `hakios.platform.c:32`”。

---

## 4. 推荐方案

### 4.1 推荐：用现成的 `hakios_spmi_lock()`，不要新建锁

**理由（每条都可查）：**

1. **它就是为这件事建的。** 注释原文：*"One transfer at a time through the PMIC arbiter for this system, whoever asks"*
   （`hakios.platform.c:28-31`）。四个驱动已在用（§3.1），新增第五、六、七个调用者不需要任何新设施。
2. **`lck_spin` 而不是 `lck_mtx` 是正确的选择。** PMIF 一笔事务是“写 ACC → 轮询 STA → 读 RDATA → 写 VLDCLR”，
   几十微秒（`hakios.platform.c:31` 注释），**不是可睡眠区间**。用 `lck_mtx` 会有三个坏处：
   - 一旦在持锁期间 `delay()`，`lck_mtx` 的持有者会 `thread_block`，把睡眠带进 PMIF 事务；
   - `lck_mtx` 在中断上下文 panic（`lock_mtx.c:229-233`），将来若有人从 ISR 摸 PMIF 会炸；
   - `lck_mtx` 抢占语义复杂（`lck_mtx_lock` 是 `LCK_MTX_MODE_SLEEPABLE`，`lock_mtx.c:1031-1034`），
     而 `lck_spin` 的 `lock/unlock` 自己管抢占（`locks.c:692-699`、`:817-822`）。
3. **零初始化成本、零 zalloc 依赖。** `LCK_SPIN_DECLARE` 走 `LOCKS` 相位（`locks.h:1255-1263`），
   早于 `bsd_init()`，无需 `lck_grp_alloc_init`，不需要判空、不需要 destroy。
4. **超时可控。** 持自旋锁超过 ≈524 ms（`lock_panic_timeout`，`locks_arm.c:106-107`）会 panic，
   而 PMIF 事务本身有 `PMIF_SPIN_LIMIT 200000`（`hakios.mt6895_headset.c:37`）次轮询的自我保护。
   两者叠加，出错时**是可诊断的 panic，不是静默数据损坏**。

**明确不推荐：**
- **新建第二把 PMIF 锁**（哪怕名字叫 `pmif_lock`）→ 与 `spmi_lock` 不互斥，`buttons`/`pmic`/`camera`
  照样能和 keys/mali 撞车。**必须复用同一把。**
- **`lck_mtx`**：本场景是短事务，不需要可睡眠语义，反而引入 IRQ/抢占两处 panic 面。
- **原子标志 + 忙等**：作为**退路**可以（§6.2），但会让 `mali_thread` 在 2 s 后与 keys 抢锁时
  空转 CPU；且无法在 `delay()` 中让出。
- **只关中断**：挡不住另一个 CPU（§2.6）。

### 4.2 落地方式（三处改动，一行一处）

三份驱动目前各自 `ml_io_map(PMIF_BASE, PMIF_SIZE)` 拿到自己的 `pmif` 指针
（`keys.c:455`、`headset.c:523`、`mali610.c:3126`），但**指向同一段物理地址 0x1c804000**，
寄存器偏移全是 `0x0880`（`keys.c:132`、`headset.c:29`、`mali610.c:3008`）。
所以它们是**同一个硬件**的三个视图——用一把进程外的锁（`hakios_spmi_lock`）串起来即可。

把每处「PMIF 事务」包起来，**粒度是整笔事务，不是单次寄存器读写**：

```c
/* keys.c: 每一笔 SPMI 读 (pmic_read_on 的调用者，或 pmic_read 内部) */
hakios_spmi_lock();
ok = pmic_read_on(chan, addr, &v);
hakios_spmi_unlock();
```

**关键：锁必须在最外层事务函数上取，不能放在 `pmif_rd`/`pmif_wr` 里**——否则
“写 ACC → 轮询 → 读 RDATA → 写 VLDCLR”会被别的驱动插进来。现有代码里对应的事务边界是：
- `keys.c`：`pmic_read_on()`（`:244-`，含 ACC 写 / STA 轮询 / RDATA 读 / VLDCLR 写）
- `headset.c`：读是 `mt6368_read()`（`:135-156`），写是 `mt6368_write()`（`:158-176`）
- `mali610.c`：`:3040-3072` 区间内的那段（含 `:3055-3071` 的完整序列）

---

## 5. 可照抄的代码模板

### 5.1 推荐模板（复用项目现有的 `lck_spin`，零新建）

```c
#include <sys/hakios.input.h>       /* hakios_spmi_lock / hakios_spmi_unlock */

/*
 * One PMIF transaction on SWINF_2. The lock is the system's, not this file's:
 * the arbiter drops a transfer when two arrive at once (hakios.platform.c:28-31),
 * and four other drivers already take it.
 *
 * The spin is held for the whole sequence -- write ACC, poll STA, read RDATA,
 * write VLDCLR -- never for a single register access: another caller inserting
 * itself between those writes is what the lock exists to prevent.
 */
static bool
pmif_read_locked(uint8_t addr, uint8_t *value)
{
	bool ok;

	hakios_spmi_lock();
	ok = pmic_read_on(0, addr, value);
	hakios_spmi_unlock();
	return ok;
}
```

**如果要自己在文件里声明锁**（不推荐，会引入第二把锁；仅当确实需要独立的锁时）：

```c
#include <kern/locks.h>

static LCK_GRP_DECLARE(my_lock_group, "my-driver");
static LCK_SPIN_DECLARE(my_lock, &my_lock_group);   /* LOCKS 相位自动初始化，不碰 zalloc，grp 不可为 NULL */

static bool
my_pmif_read(uint8_t addr, uint8_t *value)
{
	bool ok;

	lck_spin_lock(&my_lock);        /* 自己关抢占 */
	ok = pmic_read_on(0, addr, value);
	lck_spin_unlock(&my_lock);      /* 自己恢复抢占，必须配对 */
	return ok;
}
```

### 5.2 错误路径 / 提前返回

`lck_spin` 没有“自动解锁”机制，**每条 return 前必须 unlock**。三种写法：

```c
/* (a) 单出口：推荐，最少出错面 */
static bool
pmif_write_locked(uint8_t addr, uint8_t v)
{
	bool ok;

	hakios_spmi_lock();
	ok = pmif_xfer_write(addr, v);
	hakios_spmi_unlock();
	return ok;
}

/* (b) 多出口：每个 return 前都 unlock，一个都不能漏 */
static bool
pmif_touch_locked(uint8_t addr)
{
	hakios_spmi_lock();
	if (!wait_idle()) {
		hakios_spmi_unlock();
		return false;
	}
	if (!wait_vldclr()) {
		hakios_spmi_unlock();
		return false;
	}
	hakios_spmi_unlock();
	return true;
}

/* (c) 如果中途必须 delay()：先解锁，delay 完重新加锁再重试 */
static bool
pmif_retry_locked(uint8_t addr)
{
	for (unsigned tries = 0; tries < 3; tries++) {
		hakios_spmi_lock();
		if (pmif_xfer(addr)) {
			hakios_spmi_unlock();
			return true;
		}
		hakios_spmi_unlock();        /* ★ 先放锁 */
		delay(1000);                 /* ★ 再等 1 ms */
	}
	return false;
}
```

项目里 `hakios.input.c:169-191` 已经是 (b) 的变体（`lck_mtx` + `msleep` + 错误路径 unlock），
`drivers/storage/hakios.ufshc.c:800` 展示了 `os_atomic_cmpxchg` 做单次抢占的模式。

### 5.3 反面教材（不要这样写）

```c
/* ✗ 零初始化直接用：无竞争时"能用"，一有竞争就 __lck_mtx_invalid_panic
 *   (lock_mtx.c:819-825；LCK_TYPE_MUTEX=0x22 而 bss 里是 0，locks_internal.h:151-156) */
static lck_mtx_t my_lock;

/* ✗ grp 传 NULL：lck_mtx_init 在 lock_mtx.c:279 直接解引用，必崩 */
lck_mtx_init(&my_lock, NULL, LCK_ATTR_NULL);

/* ✗ 在 startup 之前用 lck_mtx_alloc_init：ZALLOC 相位未到，zalloc 不可用 */
```

---

## 6. 如果 `lck_mtx` / `lck_spin` 都不可用时的退路

按可靠性排序。**这些都不是本项目的推荐路径**（本项目 `lck_spin` 明确可用），
仅当裸机移植后 `LOCKS` 相位被裁掉、或静态锁初始化被跳过时才启用。

### 6.1 退路 A：`ml_set_interrupts_enabled(FALSE)` + `hw_lock`（最稳）

```c
	boolean_t istate = ml_set_interrupts_enabled(FALSE);   /* 纯 DAIF，任何时刻可用 */
	/* ... 临界区 ... */
	(void) ml_set_interrupts_enabled(istate);              /* 恢复原状态，不要硬写 TRUE */
```

- 优点：**唯一无初始化依赖**的原语（`machine_routines_common.c:1037-1085`）。
- 缺点：**只挡本 CPU 中断**，不挡其他核。必须先确认 `mali_thread` 与 keys 线程不会同时在两个核上跑，
  否则**没有互斥**。裸机多核场景下这是硬缺陷。

### 6.2 退路 B：`os_atomic_cmpxchg` 自旋标志（跨 CPU，可退让）

```c
static volatile uint32_t pmif_busy;      /* bss 零初始化即可，无需任何 init */

static bool
pmif_try_claim(unsigned spins)
{
	for (unsigned i = 0; i < spins; i++) {
		if (os_atomic_cmpxchg(&pmif_busy, 0, 1, acquire)) {
			return true;
		}
		/* 让出一点，避免两个核在同一个 cacheline 上打死 */
		for (volatile unsigned n = 0; n < 64; n++) { }
	}
	return false;
}

static void
pmif_release(void)
{
	os_atomic_store(&pmif_busy, 0, release);
}
```

- **零初始化**：`0` = 空闲，bss 天然正确（这是它比零初始化 `lck_mtx` 安全的地方）。
- 跨 CPU 有效（`os_atomic_cmpxchg` 是 `ldaxr/stxr` 或 `casal`）。
- 缺点：**忙等**（持锁者若在别的核上被抢占，等待者空转）；**没有超时诊断**；
  不能用 `delay()` 做退让（见 §7 —— `delay` 在无抢占时是忙等，会占死 CPU）。
- 这正是 `osfmk/kern/locks.c` 里 `hw_lock_trylock_contended` 的做法（`locks.c:345-370`，`OS_ATOMIC_USE_LLSC` 的
  `os_atomic_rmw_loop` + `wait_for_event()`），只是把超时、统计、抢占计数都省掉了。

### 6.3 退路 C：`__sync_lock_test_and_set`（编译器内建，最不依赖头文件）

```c
	while (__sync_lock_test_and_set(&pmif_busy, 1)) {
		while (pmif_busy) { }        /* 等释放 */
	}
	/* ... */
	__sync_lock_release(&pmif_busy);
```

- 与 6.2 等价，不依赖 `os/atomic.h`。项目里目前零使用，但 gcc/clang 都支持。

---

## 7. 死锁与陷阱

### 7.1 持锁期间 `delay()` 会怎样（任务明确问的）

`delay()` 的分支在 `XNU/osfmk/kern/clock.c:1575-1601`：

```c
	if (ml_delay_should_spin(interval) ||
	    get_preemption_level() != 0 ||
	    ml_get_interrupts_enabled() == FALSE) {
		machine_delay_until(interval, deadline);      /* 忙等 */
	} else {
		assert_wait_deadline(...);
		thread_block(THREAD_CONTINUE_NULL);           /* 让出 CPU */
	}
```

**三种情况，三种后果：**

| 场景 | `delay()` 走哪条 | 后果 |
|---|---|---|
| **持 `lck_spin`** 时 `delay(2000)`（2 ms） | `get_preemption_level() != 0`（`hw_lock_lock` 已关抢占，`locks.c:696`）→ **忙等** | 本 CPU 空转 2 ms。**不 panic**（< 524 ms），但另一核上等这把锁的线程会一起空转 |
| **持 `lck_spin`** 时 `delay(2000 * 1000)`（2 s，`mali610.c:5088`） | 同上，忙等 | 2 s 内本 CPU 完全不响应调度；**任何在另一核上等这把锁的线程，在 ≈524 ms 处 panic**（`lock_panic_timeout`，`locks_arm.c:106-107`）。注意这个超时是**等待者**的预算，不是持有者的 |
| **持 `lck_mtx`** 时 `delay()` | 抢占级别为 0、中断开着 → `thread_block()` → **睡眠** | 睡着的线程**仍持有 `lck_mtx`**；别人 `lck_mtx_lock` 会阻塞。若睡眠期间有人在同一线程上下文重入 → `__lck_mtx_owned_panic`（`lock_mtx.c:818-820`）。**这才是真正的死锁** |

> **所以：绝对不要在持锁期间 `delay()`。** 需要延时，先 `unlock()`，`delay()`，再 `lock()`
> （模板 §5.2 (c)）。

**具体到本项目：** `mali_thread` 的 `delay(2000 * 1000)`（`mali610.c:5088`）在**加锁之前**，
本身没问题。但 `headset.c:241` 的 `delay(2000)`（2 ms）在 `accdet_init()` 里、**夹在两笔 PMIF 事务之间**
（`:240` configure 与 `:243` configure 之间）。如果按“把整个 `accdet_init` 包进锁”的粗粒度做法，
就会变成「持 `lck_spin` 忙等 2 ms + 再 1 ms（`:245`）」 —— **不会 panic**（3 ms ≪ 524 ms），
但**会让 mali 的每次 PMIF 事务都白等 3 ms**，且 mali 侧 `pmif_wait` 的 `PMIF_SPIN_LIMIT` 可能耗尽。
**正确做法：锁的粒度是单笔事务，不是整个初始化序列。**

### 7.2 其他陷阱清单

| 陷阱 | 后果 | 依据 |
|---|---|---|
| `lck_spin_lock` 配 `lck_spin_unlock_nopreempt` | 抢占计数泄漏，永不恢复 | `locks.c:817-822`（只有 `hw_lock_unlock` 调 `lock_enable_preemption()`；`nopreempt` 版 `:824-828` 不调） |
| 同一个锁在两处用不同变体（`lock` / `lock_nopreempt`） | 抢占计数错乱；`nopreempt` 版还会先断言抢占已关 | `locks.c:692-713` |
| `lck_mtx` 在 ISR 里加锁 | `panic("Attempt to take mutex %p in IRQ context")` | `lock_mtx.c:199-203, 229-233`（`DEVELOPMENT\|\|DEBUG` 才编译） |
| 递归加同一把 `lck_mtx` | `__lck_mtx_owned_panic` | `lock_mtx.c:818-820` |
| 递归加同一把 `lck_spin` | `panic("hwlock: thread %p is trying to lock %p recursively")` | `locks.c:550-554`（`lck_spin_lock` 落 `hw_lock_lock`） |
| 两把锁反序获取 | 经典死锁 | — |
| 在 `STARTUP_SUB_LOCKS` 之前用静态锁 | 锁字段是 0，`type != LCK_TYPE_MUTEX` → invalid panic | `locks_internal.h:151-156`、`lock_mtx.c:819-825` |
| `lck_grp_alloc_init` / `lck_mtx_alloc_init` 在 `ZALLOC` 之前 | zalloc 未就绪（**具体行为未验证**：可能 panic「zone not ready」，也可能返回未初始化内存） | `lock_group.c:175`、`lock_mtx.c:258`；zone 建立于 `zalloc.c:10179`（`STARTUP(ZALLOC, RANK_FIRST)`） |

### 7.3 关于「XNU 在某阶段会不会 panic」的证据边界

- **§7.1 表格里的 panic 路径**是**读代码**得出的（`lock_panic_timeout` 默认值、
  `hw_lock_spin_policy.hwsp_timeout_atomic` 的指向、`lck_mtx_check_irq` 的分支），
  **未在本机复现**。标注：**推断（高置信，代码路径明确）**。
- **`-no_slto_panic` boot-arg 能关掉自旋超时 panic**：`startup.c:472-475` 里
  `PE_parse_boot_argn("-no_slto_panic", ...)` 会把 `spinlock_timeout_panic = FALSE`（`locks.c:110` 初值为 `TRUE`）。
  但要注意**它管的是 `hw_spin_timeout_panic` 这一层**（`locks.c:417,506,599,621` 的 `if (!spinlock_timeout_panic)` 短路）。
  而 `hw_lock_t` 的 `hwsp_op_timeout` 是 `hw_spin_timeout_panic`（`locks.c:449-453`）→ 这个开关对 `lck_spin` **有效**。
  需要 `PE_i_can_has_debugger()` 为真（`startup.c:470`）才会解析。标注：**代码可证，运行时行为未验证**。
- **`TLockTimeOut`（0.125 s）不适用于 `lck_spin`**：它是 `lck_ticket_t` 的超时（`lock_ticket.c:63-66`、
  `machine_routines.c:97`），而 arm64 的 `lck_spin` 走 `hw_lock`（§2.2）。**别把这两个数字搞混。**
- **`hakios_input_init()` 时 `lck_mtx_alloc_init` 一定成功**：依赖 `STARTUP_SUB_ZALLOC` 已跑完。
  本文只证明了相位顺序（`startup.h:62,67` + `startup.c:751,794`），
  **没有验证 `kalloc_init` 在这个特定移植上是否被裁掉**。标注：**推断（相位顺序可证，构建产物未验证）**。

---

## 8. 复核方法

```sh
sh docs/headset-wip/lock-probe.sh
```

脚本只读，输出 8 节：调用链 / 相位顺序 / 静态声明宏 / 动态 init 与 `grp` 处理 /
运行时不变式与超时 / 项目现有用法 / 三驱动零锁复核 / 头文件声明。
路径可用环境变量覆盖：

```sh
XNU_SRC=/path/to/xnu PROJECT=/path/to/hakios-mtk sh docs/headset-wip/lock-probe.sh
```

退出码：`0` 全部探针都有命中且路径齐全；`1` 有路径缺失或某个“应当有”的 grep 落空。
（“三份驱动零锁”这类**期望为空的**探针用 `show_allow_empty`，落空不计为失败。）

---

## 9. 与本任务其他文档的关系

| 文档 | 关系 |
|---|---|
| `findings/SPMI-PMIF-SPEC.md:428,452,472,574` | 其 §6.1「完全缺失」对三份驱动成立；但 §U6「XNU 侧没有现成的跨线程互斥」**不成立**，应改为“已有 `hakios_spmi_lock()`”。本报告 §3.1、§3.4 给出修正 |
| `findings/SPMI-PMIF-SPEC.md:515-534` 的伪代码 | 其 `lock(pmif_lock)` / `unlock()` 位置正确（整笔事务），落地时映射到 `hakios_spmi_lock()` |
| `DANGER-spmi-access.md` | 与本报告无关（那是 Android 侧 `spmi_access` 的风险记录），但同样说明“并发/格式错误会崩硬件” |
