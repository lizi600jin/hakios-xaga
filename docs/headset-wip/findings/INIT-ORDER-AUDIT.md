# INIT-ORDER-AUDIT — `hakios_input_init()` 初始化顺序 / 并发 / 依赖审计

**审计对象**：`/home/lizi/IOS/hakios-mtk`（只读，未修改）
**审计时间**：2026-11-15 00:50 – 01:08（Asia/Shanghai）
**审计口径**：当前工作树快照。审计期间 Codex 正在做 PMIF 重构，**树里的文件在审计过程中被改动过两次**，所以下面所有 md5 都是明确的分界点。

**每条结论都追到 `文件:行号`。行号对应下面标注的那个 md5 版本；文件被改后行号会有位移，md5 才是锚。**

---

## 0. 快照与 md5（必读）

### 0.1 三份快照

规则：审计跑三轮，每轮开头取一次 md5；文件一变就重启一轮。最终报告以 **快照 C（定稿版）** 为准，前两轮保留下来作为「变化证据」。

| 代号 | 时间 | 目录 | 用途 |
|---|---|---|---|
| A | 00:57:39 | `findings/snapshot-20261115-005739/` | 重构前（裸寄存器访问，三份驱动各自 `ml_io_map` PMIF） |
| B | 01:05:28 | `findings/snapshotB-20261115-010528/` | 共享 PMIF 驱动 `hakios.mt6895_pmif.c` 落盘 |
| **C** | **01:07:36** | （见下表，无独立目录；与 B 同一结构） | **报告定稿依据** |

### 0.2 快照 C 的 md5

| 文件 | md5 | 变化 |
|---|---|---|
| `drivers/input/hakios.input.c` | `5aff340f35a6ba4f8b9e37931985f588` | 三轮未变 |
| `.config` | `3e6c10f7fad6c180c66ea7fcee544e36` | 相对 HEAD 新增 `CONFIG_HAKIOS_MT6895_PMIF=y` |
| `mtk/mt6895/drivers/core/hakios.mt6895_pmif.c` | `f034fba1531a30a66e1860882c090b95` | **新增文件**（337 行） |
| `xnu/files/bsd/sys/hakios.pmif.h` | `55f74b29ee0f524f47bf7e3eae81470f` | 重写：加锁、预算、`pmif_*` API |
| `mtk/mt6895/drivers/input/hakios.mt6895_keys.c` | `a410817a71597ea322803f93ca2487a3` | 587 行 |
| `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c` | `36fb36d8679d414c7d2b547961c697ca` | 537 行 |
| `mtk/mt6895/drivers/gpu/hakios.mali610.c` | `28d287224150b63fc24a5a46bb536413` | 未再变 |
| `mtk/mt6895/drivers/input/hakios.nvt.c` | `32f2bbf4b54e129f3ff2c532a029018f` | 未变 |
| `mtk/mt6895/drivers/usb/hakios.mt6895_usb.c` | `fdc233fefdd0134c655e1c1b755a7aa5` | 未变 |
| `drivers/core/hakios.platform.c` | `cfad2e93ea548c9e3859fa3e30b711f8` | 未变（`hakios_spmi_lock` 仍是 `lck_spin`） |
| `drivers/core/hakios.stubs.c` | `17106470727dbaa6a94378c166c58da8` | 未变 |

快照 A / B 的 `MD5SUMS.txt` 在各自目录里，逐文件可核。

### 0.3 B1 的当前状态（一句话）

MASTER-STATE 里的 B1（三份驱动无锁并发访问 PMIF）**在快照 C 已经被结构性修掉**：三份驱动不再自己映射 PMIF，也不再自己轮询 STA，全部改调 `pmif_read8` / `pmif_write8`，而这两个函数内部取 `hakios_spmi_lock()`（`mtk/mt6895/drivers/core/hakios.mt6895_pmif.c:222`、`:266`）。

**但构建清单没有重生成**（见 §6 的 F1），所以这个修法在快照 C 还编不进内核。

### 0.4 交稿后的状态变化（01:11 复核）

审计收尾时（01:11）树又动了一次，**F1 已被修掉**：

```
xnu-src/xnu-8796.121.2/bsd/dev/hakios.drivers.h:2   → "mt6895_headset mt6895_pmif mali610 mtk6895_keys nvt mt6895_usb"
xnu-src/xnu-8796.121.2/bsd/dev/hakios.drivers.h:3   → HAKIOS_DRIVERS_COUNT 6
xnu-src/xnu-8796.121.2/bsd/conf/files:163           → bsd/dev/hakios.mt6895_pmif.c  optional hakios_mt6895_pmif
```

`mtk/mt6895/drivers/core/hakios.mt6895_pmif.c` 也改了一版（338 行，md5 `c73a99b4d96b72190749d4d8e509dabe`），锁的位置不变（`:33`/`:41`/`:222`/`:266`/`:297`/`:310`），映射仍在锁外做（`:40`）后回锁内赋值——与 §2.b 的分析一致。

**所以 §5 的 F1 在交稿时已不成立**，下文保留它是因为：
1. 它证明了「B1 的修法在审计过程中一度编不进内核」这个事实；
2. 它说明构建清单是**生成产物**，`.config` 改了必须重跑 `tools/hakios-config.py`。

交稿时的 md5（唯一权威）：

| 文件 | md5 |
|---|---|
| `.config` | `3e6c10f7fad6c180c66ea7fcee544e36` |
| `drivers/input/hakios.input.c` | `5aff340f35a6ba4f8b9e37931985f588` |
| `mtk/mt6895/drivers/core/hakios.mt6895_pmif.c` | `c73a99b4d96b72190749d4d8e509dabe` |
| `mtk/mt6895/drivers/input/hakios.mt6895_keys.c` | `a410817a71597ea322803f93ca2487a3` |
| `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c` | `36fb36d8679d414c7d2b547961c697ca` |
| `mtk/mt6895/drivers/gpu/hakios.mali610.c` | `28d287224150b63fc24a5a46bb536413` |
| `xnu-src/.../bsd/dev/hakios.drivers.h` | `8ea2d9262f29ff2e7692655b6693d933` |

**本报告 §1–§4 的所有结论不受这次改动影响**：它们只依赖 `hakios.input.c`（未变）、三份驱动的调用结构（未变）、以及 `pmif_*` 走同一把锁这件事（未变）。

---

## 1. 初始化顺序全表（核心交付）

由 `init-order-audit.py` 自动提取，逐格人工复核。`hakios_input_init()` 在 `drivers/input/hakios.input.c:221`，21 条调用在 `:229-249`。

### 1.1 先纠正一个前提

任务描述里说「20+ 个驱动的 init 串行调用」。**实际 `.config` 只编入 5 个 HAKIOS 驱动**：

```
.config:39  CONFIG_HAKIOS_MT6895_HEADSET=y
.config:40  CONFIG_HAKIOS_MT6895_PMIF=y        ← 快照 C 新增
.config:41  CONFIG_HAKIOS_MALI610=y
.config:42  CONFIG_HAKIOS_MTK6895_KEYS=y
.config:43  CONFIG_HAKIOS_NVT=y
.config:44  CONFIG_HAKIOS_MT6895_USB=y
```

其余 16 条调用由 `drivers/core/hakios.stubs.c` 提供**空函数体**（每个 `#ifndef HAKIOS_<NAME>` 一个 `{}`）。所以 21 条调用里：

- **6 条真的执行代码**：nvt、mt6895_keys、mt6895_usb、net（standard 文件）、mali610、mt6895_pmif（但后者不被 `hakios_input_init` 直接调用，是被前三个间接调用）
- **16 条是空操作**（`hakios_cpufreq_init`、`hakios_virtio_input_init`、`hakios_goodix_init`、`hakios_tcm_init`、`hakios_buttons_init`、`hakios_mtk_keys_init`、`hakios_battery_init`、`hakios_ufs_init`、`hakios_adreno_init`、`hakios_mali_init`、`hakios_mtk_connsys_init`、`hakios_mtk_scp_init`、`hakios_wifi_init`、`hakios_adsp_init`、`hakios_mpss_init`、`hakios_mtk_emmc_devfs`）

**这直接决定了「单点故障」的答案**：`hakios_cpufreq_init()` 不编入，它不可能卡住后续；真正会卡住的只有 §3 列出的那几个 worker。

### 1.2 全表

`线程` = 自己调 `kernel_thread_start`/`thread_call_allocate`/`timer_call_enter`；`PMIF` = 会访问 `0x1c804000`；
`GPU` = 访问 `0x13000000` 窗口；`阻塞(ms)` = 该 init 让**串行阶段**停住的时间。

| # | 函数 | 编入 | 起线程 | 访问 PMIF | 访问 GPU | 阻塞(ms) | 阻塞在哪 | map 的块 | 证据 |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `hakios_cpufreq_init` | ✗ stub | – | – | – | 0 | – | – | `drivers/core/hakios.stubs.c:133` |
| 2 | `hakios_virtio_input_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:39` |
| 3 | `hakios_goodix_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:43` |
| 4 | `hakios_tcm_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:52` |
| 5 | **`hakios_nvt_init`** | ✓ | **是** | 否 | 否 | **0** | worker 自己先 `delay(200000)`=200 ms 再进循环；init 本身立即返回 | GPIO `0x10005000` | `mtk/mt6895/drivers/input/hakios.nvt.c:1533`（init）、`:1542`（起线程）、`:905`（map）、`:1164`/`:1528`（200 ms）、`:1299`（`for(;;)`） |
| 6 | `hakios_buttons_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:47` |
| 7 | `hakios_mtk_keys_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:56` |
| 8 | **`hakios_mt6895_keys_init`** | ✓ | **是** | **是**（worker，1 跳） | 否 | **0** | init 立即返回；worker 里**没有**前置 delay | keypad `0x1c00e000` + PMIF（经共享层） | `mtk/mt6895/drivers/input/hakios.mt6895_keys.c:573`（init）、`:582`（起线程）、`:199`（`pmif_read8`）、`:362`（`pmif_init`）、`:461`（`for(;;)`）、`:568`（`delay(poll_ms*1000)`） |
| 9 | **`hakios_mt6895_usb_init`** | ✓ | **是** | 否 | 否 | **0** | init 里 4 次 `ml_io_map` 后起线程 | MAC `0x11201000`、IPPC `0x11203e00`、PHY `0x11e40000`、DP_SEL `0x10005600` | `mtk/mt6895/drivers/usb/hakios.mt6895_usb.c:1861`（init）、`:1870-1873`（map）、`:1880`（起线程）、`:1834`（`for(;;)`） |
| 10 | `hakios_battery_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:127` |
| 11 | `hakios_ufs_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:114` |
| 12 | **`hakios_net_init`** | ✓ standard | 否（但 `hakios_virtio_net_init()` 是 stub） | 否 | 否 | **0** | – | – | `drivers/net/hakios.net.c:423` |
| 13 | `hakios_adreno_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:138` |
| 14 | `hakios_mali_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:84` |
| 15 | **`hakios_mali610_init`** | ✓ | **是** | **是**（worker，3 跳） | **是** | **0** | init 只解析 boot-arg 后起线程；**worker 里 `delay(2000*1000)` = 2000 ms 才动 PMIF** | GPU `0x13000000`(+0x480000)、SPM、topckgen、MFGPLL、MFGCFG 等 | `mtk/mt6895/drivers/gpu/hakios.mali610.c:5043`（init）、`:5198`（起线程）、`:5033`/`:5038`（线程 + 2000 ms）、`:4247`（bring_up）、`:4391`（调 vstack_rail）、`:3069`/`:3074`（PMIF） |
| 16 | `hakios_mtk_connsys_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:105` |
| 17 | `hakios_mtk_scp_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:101` |
| 18 | `hakios_wifi_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:145` |
| 19 | `hakios_adsp_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:153` |
| 20 | `hakios_mpss_init` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:159` |
| 21 | `hakios_mtk_emmc_devfs` | ✗ stub | – | – | – | 0 | – | – | `stubs.c:109` |

### 1.3 串行阶段的总阻塞量

**0 ms。**

关键点：**真正编入的 5 个驱动，没有一个在自己的 init 体里 `delay()`**。所有 `delay()` 都在各自 `kernel_thread_start()` 起的 worker 里。`hakios_input_init()` 从 `:229` 跑到 `:249` 是纯粹的「注册 + 起线程」，实测口径下总阻塞为 0（脚本 `blocks_ms` 那一栏全 0；汇总的 `2202.0 ms` 是"worker 开始干活前的睡眠"之和，不是串行阻塞）。

这一点很重要：**当前不存在「哪个 init 卡住整条链」的现实风险**，因为串行链上根本没有任何会等待的东西（详见 §3）。

---

## 2. 竞态与顺序错误

### 2.a `hakios_mt6895_headset_init()` 的调用位置

**它在哪**：不在顶层列表里，而是在 **keys 的 worker 体内**，且位于 SID 4 读完之后：

```
mtk/mt6895/drivers/input/hakios.mt6895_keys.c:420   hakios_mt6895_headset_init();
mtk/mt6895/drivers/input/hakios.mt6895_keys.c:461   for (unsigned tick = 1;; tick++) {   ← 之后才进轮询
mtk/mt6895/drivers/input/hakios.mt6895_keys.c:567       hakios_mt6895_headset_poll(poll_ms);
```

**是有意的，且证据充分。** 三层证据：

1. **代码注释**：`hakios.mt6895_headset.c:13-15` — "Init and poll run only in the keys worker, between its complete PMIF transactions. Quiesce runs only after `loader_return()` stops the other cores. **There is no second bus worker.**"
2. **conf 文件**：`mtk/mt6895/drivers/audio/conf:4-7` — `requires MTK6895_KEYS`，并且 help 明确写 "Both run in the `buttons=2` keys worker."
3. **stub 证明**：`hakios.stubs.c:74` 给 `hakios_mt6895_headset_init` 提供空体，而它在 `hakios.input.c` 的 21 条调用里**一次都没出现**——说明它从来不是顶层调用。

**「keys 线程已经跑起来了，headset 的同步执行会不会和它并发？」→ 不会。** headset 的 init 和 poll 就在 keys 线程里同步调用，是同一个线程，不可能自并发：

- `:420` 的 `hakios_mt6895_headset_init()` 在 `:461` 的循环**之前**，
- `:567` 的 `hakios_mt6895_headset_poll()` 在循环**内部**、每次 `delay()` 之前。

**但有一个真实代价**（不是竞态，是顺序副作用）：headset init 在 `:420`，而 keypad 的 `read_state()` 在更后面。也就是说 **keys worker 卡在 accdet 上多久，三个按键就多久没有响应**。`headset=1` 时 init 要 `dump_block` 124 个寄存器（`hakios.mt6895_headset.c:492`，`ACCDET_RG_LO..HI` = `0x2688..0x26d3`），每个寄存器一次完整 SMBus 事务，单次预算 100 ms（`hakios.pmif.h:38 PMIF_POLL_BUDGET_US 100000u`）。最坏情况 124 × 100 ms ≈ **12.4 s** 按键无响应。正常情况是几十微秒一次，可忽略。

### 2.b `hakios_mali610_init()` 的 2 秒延迟 — B1 的时间线

**时间线推演**（`t=0` 是 `hakios_input_init()` 进入的时刻；串行链上无阻塞，所以五个 init 几乎同一瞬间完成）：

```
t=0         hakios_input_init() 进 :221
t≈0         :229 起   → 1..4 全是 stub，立即返回
t≈0         :233 hakios_nvt_init()          → 起 nvt_thread；worker 先 delay(200ms)     [nvt.c:1164]
t≈0         :236 hakios_mt6895_keys_init()  → 起 keys_thread                            [keys.c:573→582]
t≈0         :237 hakios_mt6895_usb_init()   → 4×ml_io_map 后起 usb_thread               [usb.c:1870→1880]
t≈0         :241..:249 其余全 stub
t≈0         hakios_input_init() 返回 → bsd_init() 继续
```

`keys_thread` 内部（`keys.c`）：

```
t≈0+ε      keys.c:362   pmif_init()            ← 映射 PMIF（取 hakios_spmi_lock）
t≈0+ε      keys.c:420   hakios_mt6895_headset_init()   ← 同步！headset=1 时 dump 124 寄存器
           ...若 headset=2，还要 accdet_init_once + 首次 poll
t≈0+ε+X    keys.c:461   进 for(;;)，每轮 pmif_read8(0x1e) + pmif_read8(0x2a) + delay(20ms)
```

`mali_thread` 内部（`mali610.c`）：

```
t=0        mali610.c:5198 起 mali_thread
t=0  →  t=2000ms   mali610.c:5038  delay(2000 * 1000)   ← 硬睡 2 秒
t=2000ms   mali610.c:4247  mali_bring_up()
t=2000ms+  若 GPU 是冷的 → :4391 mali_vstack_rail() → :3074 pmif_init() → :3043/:3047 pmif_read8/pmif_write8
```

**结论**：`t=2000ms` 时，`keys_thread` 已经在 `for(;;)` 里每 20 ms 做一次 SID 4 读。mali 此刻开始对 SID 5 的 VBUCK2 做读-改-写。**两者访问同一个 SWINF_2 通道的同一组 5 个寄存器**（ACC `0x880` / WDATA `0x884` / RDATA `0x894` / VLDCLR `0x8a4` / STA `0x8a8`），在快照 A/B 这是无锁的。

**这正是 B1 的根因，且时间窗口是"永久"而不是"瞬时"**：keys 的轮询没有终点，mali 的 VBUCK2 读-改-写落在哪里只取决于 `delay(2000)` 与 keys 的 20 ms 相位，所以**每次开机必然重叠**，且落在读-改-写的中间（`mali_spmi` 一次事务里要读 0x240、读 0x24e、写 0x241、`delay(500)`、再读 0x240，中间还夹着 500 µs 的 delay——`mali610.c:3069-3106`，`delay(500)` 在 `:3101`）。keys 在 mali 的两次事务之间插进来，会看到 mali 留下的 WFVLDCLR 状态并 `VLDCLR` 掉它，或者把自己的 ACC 写在 mali 的 ACC 之上。

**快照 C 的现状**：两个驱动都改走 `pmif_read8`/`pmif_write8`，两者内部同一把 `hakios_spmi_lock()`（`hakios.mt6895_pmif.c:222`、`:266`），**逐事务互斥**。时间线仍然重叠，但不再互相踩寄存器。

**残留风险（重要，未验证）**：锁只保证「**单次事务**」原子，不保证「**读-改-写序列**」原子。mali 的 `mali_vstack_rail()` 是 read(0x240) → read(0x24e) → write(0x241) → delay(500) → read(0x240)，**跨 4 次 `pmif_*` 调用**（`mali610.c:3069-3106`（`mali_spmi` 调用在 `:3082`/`:3085`/`:3094`/`:3102`，中间的 `delay(500)` 在 `:3101`））。`hakios.pmif.h:6-7` 自己写明了这一点：

> "Consumer RMW helpers are not atomic across calls: their PMIC register sets must remain disjoint."

在快照 C 里 VBUCK2 的寄存器组（`0x240/0x241/0x24e`）与 keys 的 `0x1e/0x2a`、accdet 的 `0x2508..0x26d3` **确实不相交**，所以当前是安全的。但这依赖「谁都不许碰别人的寄存器」这个隐式约定，没有任何机制强制。

### 2.c 还有谁访问 PMIF

对 `mtk/` 与 `drivers/` 全树 grep `0x1c804000` / `PMIF` / `SWINF` / `spmi`：

**真正访问 MT6895 PMIF（`0x1c804000`）的只有 3 处 + 1 个共享层：**

| 文件 | 角色 | 证据 |
|---|---|---|
| `mtk/mt6895/drivers/core/hakios.mt6895_pmif.c` | **共享层**，唯一持有 PMIF 映射 | `:39 ml_io_map(PMIF_BASE, ...)` |
| `mtk/mt6895/drivers/input/hakios.mt6895_keys.c` | 消费者（SID 4，`0x1e`/`0x2a`） | `:199 pmif_read8` |
| `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c` | 消费者（SID 5，accdet） | `:100 pmif_read8`、`:121 pmif_write8` |
| `mtk/mt6895/drivers/gpu/hakios.mali610.c` | 消费者（SID 5，VBUCK2） | `:3043`/`:3047` |

**清单外的另一个访问者**：`mtk/loader/mtkloader.c:5739-5776` 直接读写 `0x1c804000`，但它是 **loader（运行在 XNU 之前）**，不是内核驱动，与 `hakios_input_init()` 不同时存在。

**容易误判的一处（重要）**：`hakios_spmi_lock()` 保护的**不是** MT6895 的 PMIF。它现在的 4 个消费者访问的是**高通 PMIC arbiter**：

```
drivers/input/hakios.buttons.c:96    ARB_CORE_PHYS     0x0c440000
drivers/power/hakios.pmic.c:91       ARB_CORE_PHYS     0x0c440000
drivers/camera/hakios.torch.c:156    ARB_CORE_PHYS     0x0c440000
drivers/camera/hakios.camera.c:458   ARB_CORE_PHYS     0x0c440000
```

`0x0c440000` ≠ `0x1c804000`。`drivers/core/hakios.platform.c:27-31` 的注释写的是 "the PMIC **arbiter**"，指的是高通那块。**这 4 个消费者没有一个编入当前 `.config`**（`CONFIG_HAKIOS_BUTTONS` / `PMIC` / `TORCH` / `CAMERA` 全部 `is not set`），所以这把锁在快照 C 里**唯一的用户就是新加的 PMIF 共享层**。

这把锁仍然可用（`lck_spin`，`platform.c:32`，`#include "hakios.drivers.h"` 之后的静态声明，无初始化顺序依赖），所以复用它是对的——只是「复用一把原本为另一块硬件起的锁」这件事本身需要知道。

### 2.d `hakios_ufs_init()` 与 `hakios_mtk_emmc_devfs()`

**两者都不编入**（`CONFIG_HAKIOS_UFS` / `MTK_EMMC` 均 `is not set`，`stubs.c:114`、`:109`）。

即便编入，**也不会与 PMIC 冲突**：`hakios_ufs_init()` 读的是 loader 共享页（`ufs_shared_pa`），走 `ml_io_map_unmappable(..., VM_WIMG_WCOMB)`（`drivers/storage/hakios.ufs.c:1716`），没有任何 SPMI 访问；`hakios_mtk_emmc_devfs()` 只是 `devfs_make_node`（`mtk/mt6768/drivers/storage/hakios.mtk_emmc.c:453-461`），无 MMIO。

唯一共享的是 `drivers/core/hakios.stubs.c` 这个文件（不是运行时资源），不构成并发。

### 2.e 顺序依赖

**`hakios_cpufreq_init()` 为什么最先**：它其实是**最先被 stub 掉的**。真正的 cpufreq 驱动（`drivers/power/hakios.cpufreq.c:560`）在它自己内部就先处理 `ddr` 再判断 `cpufreq`（`:576-585`），并不依赖前面任何驱动——它只依赖 `ml_get_topology_info()`（`:562`），那是 osfmk 的核心数据，`hakios_input_init()` 时必然就绪。所以**它排第一没有硬依赖，只是历史顺序**，`stub` 之后连这点意义都没有了。

**两个 Mali 会互相干扰吗？**

- `hakios_mali_init()`（MT6768 的 G52）**不编入**（`stubs.c:84`）——它整段被 stub 掉，所以**当前不会干扰**。
- 若两者同时编入：**会，而且是硬冲突**。`mtk/mt6768/drivers/gpu/hakios.mali.c:45` 的 `MFGCFG_PHYS` 是 `0x13000000`，`mtk/mt6895/drivers/gpu/hakios.mali610.c:354` 的 `MALI_GPU_PHYS` 也是 `0x13000000`，**两个驱动映射同一个物理窗口**。它们还各自起线程（`mali.c:1143`、`mali610.c:5198`），都会写 `MFGCFG`、SPM 的 MFG 域、时钟。这是 `.config` 层面的互斥要求，不是代码能解决的。
  → `mtk/mt6895/drivers/gpu/conf:8-10` 的注释已经意识到了命名问题（"MALI610, not MALI"），但**没有防止两者同时被选**。

**`hakios_adreno_init()` 也动 GPU 吗？**

- 编入状态：`CONFIG_HAKIOS_ADRENO is not set` → `stubs.c:138` 空体。
- 它驱动的是**高通 Adreno 642L**（`drivers/gpu/conf:4`），地址空间完全不同（SC7280 的 GPU），**不碰 `0x13000000`**（脚本对它的 GPU 检测为 false）。所以即便编入也不与 Mali 冲突。
- 附带事实：`hakios.input.c:82` 的 `exit_hold_elapsed()` 无条件调 `hakios_adreno_stop()`，那在 stub 里是空函数（`stubs.c:140`）。

**一个真实的、被忽略的顺序依赖**：`hakios_mt6895_headset_init()` 依赖 `MTK6895_KEYS`（conf 里 `requires MTK6895_KEYS`，`mtk/mt6895/drivers/audio/conf:5`）。它的全部生命周期都挂在 keys worker 上——**keys 不起线程，headset 就永远不初始化**。而 keys 的启动条件是 `buttons=2`（`keys.c:578`）。也就是说：

```
buttons=2  ──是──> keys worker ──> headset init ──> headset poll
    │
    └──否──> headset 完全不初始化（即使 headset=1/2 都给了）
```

这是一个**隐式耦合**：`headset=N` 这个 boot-arg 单独存在时**什么都不做**。

---

## 3. 「挂了就全挂」的单点

先给结论：**当前没有这样的单点。** 理由和逐项检查如下。

### 3.1 为什么没有

`hakios_input_init()` 的 21 条调用里，编入的 5 个驱动**全部**是「解析 boot-arg → 起线程 → 立即返回」的结构：

| 驱动 | init 体里的最后一条语句 | 会不会等待 |
|---|---|---|
| `hakios_nvt_init` | `kernel_thread_start(...)` `nvt.c:1542` | 不会 |
| `hakios_mt6895_keys_init` | `kernel_thread_start(...)` `keys.c:582` | 不会 |
| `hakios_mt6895_usb_init` | `kernel_thread_start(...)` `usb.c:1880` | 不会 |
| `hakios_mali610_init` | `kernel_thread_start(...)` `mali610.c:5198` | 不会 |
| `hakios_net_init` | `hakios_virtio_net_init()` `net.c:427`（stub，`stubs.c:165`） | 不会 |

所以**任何一个 worker 死循环都不会阻止后面的 init**，因为后面根本没有会执行的 init。

### 3.2 逐项：无超时的循环

| 位置 | 形态 | 有无超时 | 会不会卡住串行 init |
|---|---|---|---|
| `nvt.c:1299` | `for (unsigned tick = 1;; tick++)`，每轮 `delay(200000)` | 无退出条件（设计如此） | **不会**（在 nvt_thread 里） |
| `keys.c:461` | `for (unsigned tick = 1;; tick++)`，每轮 `delay(poll_ms*1000)` | 无退出条件（设计如此） | **不会**（在 keys_thread 里） |
| `usb.c:1834` | `for (;;)`，内层 50×`delay(200)` | 无退出条件（设计如此） | **不会**（在 usb_thread 里） |
| `mali610.c` | 无 `for(;;)`；`while` 全是固定次数（`:1036` 是解析循环） | – | 不会 |

**这三个 `for(;;)` 都是轮询线程，按设计永不返回，不是缺陷。**脚本把它们记为 `worker_forever`，不是 `sync_forever`。

### 3.3 逐项：无超时的 MMIO 轮询

| 位置 | 形态 | 判定 |
|---|---|---|
| `hakios.mt6895_pmif.c`（共享层） | `pmif_poll_pause()` + 100 ms 预算 + `PMIF_SPIN_HARD_CAP 10000` | **有界**（`hakios.pmif.h:37-39`）。注释 `:114` 已点明「No software deadline can bound an MMIO access that itself never returns」——这是诚实的边界声明 |
| `usb.c:ep0_poll()` | 内层 `for (i<50) ep0_poll(); delay(200)` 有界，但 `ep0_poll` 内部的寄存器读 | **未验证**：`ep0_poll` 是纯 MMIO 读写，无轮询循环；但如果 USB 控制器掉电，读会 bus abort。内核无异常向量（`keys.c:143-147` 的注释已说明这点），abort 即死机 |
| `nvt.c` | GPIO bit-bang，`for (n < 2000000 && edges < 1000)` `:1241` | **有界**（双重上限） |

### 3.4 真正接近「单点」的东西（不是死循环，是 bus abort）

内核**没有异常向量**，一次对未上电块的 MMIO 读就是 bus abort，整机停住。这条风险链在：

```
nvt.c:905       gpio = ml_io_map(0x10005000, 0x1000)       ← GPIO 块
usb.c:1870-1873 usb_mac/ippc/phy/dp_sel = ml_io_map(...)   ← 4 个块
mali610.c:4303+ gGpu = ml_io_map(0x13000000, 0x480000)     ← GPU 窗口
```

`nvt.c:906-931` 的注释记录了真实事故：读 GPIO group 4（GPIO135，中断脚）时触发过 abort。这类失败**不是「卡住」，是「立刻死」**，而且发生在 worker 里、init 已经返回之后——所以不会「挂了就全挂初始化」，会「挂了就整机重启」。

---

## 4. 顺序依赖图

```
bsd_init()                                    [xnu-src/.../bsd/kern/bsd_init.c:988]
  │
  └─> hakios_input_init()                     [hakios.input.c:221]
        │
        │  :223-228  cdevsw_add + devfs_make_node + thread_call_allocate（全部同步、无硬件）
        │
        ├─[1..4] cpufreq / virtio_input / goodix / tcm ─────────── STUB，空返回
        │
        ├─[5] nvt_init ──────────────► nvt_thread ──delay(200ms)──► for(;;) 读 GPIO bit-bang
        │                                   │
        │                                   └─ map GPIO 0x10005000（唯一使用者）
        │
        ├─[6][7] buttons / mtk_keys ────────────────────────────── STUB，空返回
        │        （注意：buttons=2 被 [8] 抢走，见 §5.2）
        │
        ├─[8] mt6895_keys_init ──────► keys_thread
        │                                   │
        │                                   ├─ pmif_init()          [keys.c:362]
        │                                   │     └─► 共享层取 hakios_spmi_lock 建唯一映射
        │                                   ├─ pmif_read8(SID4, 0x1e/0x2a)  ← 每 20 ms
        │                                   ├─ hakios_mt6895_headset_init()  [keys.c:420] ★
        │                                   │     └─► 共享层（SID5, accdet 0x2508..0x26d3）
        │                                   │           └─ 依赖 CONFIG_HAKIOS_MT6895_HEADSET
        │                                   │           └─ 依赖 buttons=2（隐式！）
        │                                   └─ for(;;) { poll; headset_poll; delay(20ms) }
        │
        ├─[9] mt6895_usb_init ───────► usb_thread ──► for(;;) ep0_poll + delay(200)
        │        map 0x11201000 / 0x11203e00 / 0x11e40000 / 0x10005600
        │
        ├─[10][11] battery / ufs ───────────────────────────────── STUB
        │
        ├─[12] net_init ──► hakios_virtio_net_init()（STUB）── 无硬件
        │
        ├─[13][14] adreno / mali ───────────────────────────────── STUB
        │        ★ 若 MALI 与 MALI610 同时编入 → 两者都 map 0x13000000，硬冲突
        │
        ├─[15] mali610_init ─────────► mali_thread ──delay(2000ms)──► bring_up
        │                                   │                            │
        │                                   │                            ├─ mali_vstack_rail()
        │                                   │                            │     └─ pmif_init + pmif_write8(SID5, 0x241)
        │                                   │                            └─ map 0x13000000 / SPM / topckgen / MFGPLL
        │                                   └─ ★ 此线程与 keys_thread 在 t=2000ms 起长期共存
        │
        └─[16..21] connsys / scp / wifi / adsp / modbus / emmc ─── STUB
```

**必须成立的先后关系（硬依赖）**：

| 关系 | 原因 | 证据 |
|---|---|---|
| `mtk6895_keys_init` **先于** `mt6895_headset_init` | headset 跑在 keys 的线程里 | `keys.c:420` 在 keys_thread 内；`conf:5 requires MTK6895_KEYS` |
| `pmif_init()` **先于** 任何 `pmif_*8()` | 共享层要先建映射 | `hakios.mt6895_pmif.c:33` 检查 `pmif_base != 0` |
| `nvt/keys/usb/mali610` 之间 **无** 依赖 | 各自独立线程、各自窗口 | 见上表 map 列互不重叠 |
| `MALI` 与 `MALI610` **互斥**（不是先后） | 同一物理窗口 `0x13000000` | `mali.c:45` / `mali610.c:354` |
| `pmif_*` **不得** 在 `psci_smp_stop_others()` 之后调用 | 锁可能被已停的核持有 | `hakios.pmif.h:5`、`headset.c:266-271` |

---

## 5. 单点故障清单（含编号，供后续引用）

### F1（阻塞级）`hakios.mt6895_pmif.c` 没进构建清单 —— 当前编译会失败

`CONFIG_HAKIOS_MT6895_PMIF=y` 已在 `.config:40`，`mtk/mt6895/drivers/core/conf` 也声明了 `driver MT6895_PMIF`，但两份**生成产物没重生成**：

- `xnu-src/xnu-8796.121.2/bsd/conf/files` 里**没有** `hakios.mt6895_pmif.c` 这一行（grep `pmif` 零命中）
- `xnu-src/xnu-8796.121.2/bsd/dev/hakios.drivers.h:2` 仍是旧的 `"mt6895_headset mali610 mtk6895_keys nvt mt6895_usb"`，`HAKIOS_DRIVERS_COUNT` 还是 5

同时三份驱动**已经**在调 `pmif_init` / `pmif_read8` / `pmif_write8` / `pmif_recover`（全树定义数 = 0，只有新文件提供）。所以快照 C 的源码树**编译不过**（链接期 undefined symbol）。这不是审计对象的缺陷，是「重构进行中」的中间态——**但它意味着 B1 的修法此刻还没有真正生效**。

修复：重跑 `tools/hakios-config.py` 生成 `bsd/conf/files` 与 `hakios.drivers.h`。

### F2（行为级）headset 的初始化挂在 `buttons=2` 上

`headset=N` 单独给出时完全无效，因为唯一的调用点在 keys worker 内（`keys.c:420`），而 worker 的启动条件是 `buttons=2`（`keys.c:578`）。没有日志提示这一点——`headset.c:489` 的 `mode == 1` 分支甚至会在拿不到 PMIF 时静默返回。

### F3（时序级）headset init 阻塞按键最长约 12.4 s

`headset=1` 时 `:420` 要 dump 124 个寄存器，每寄存器一次事务、单次预算 100 ms。最坏 12.4 s 内三个按键完全无响应。正常（PMIC 在线）是几十 µs/次，可忽略。这是「把 accdet 放进 keys worker」的直接代价。

### F4（设计级）`pmif_*` 的读-改-写不跨调用原子

见 §2.b 末尾。当前靠「寄存器组不相交」这个**无强制**的约定成立。任何新消费者碰到 `0x240/0x241/0x24e` 或 keys/accdet 的寄存器组，就会引入新的竞态。

### F5（配置级）`MALI` 与 `MALI610` 可同时编入，争 `0x13000000`

`mtk/mt6895/drivers/gpu/conf:8-10` 的注释知道命名会撞，但**没做互斥**。当前 `.config` 只有 `MALI610=y`，所以不触发。

### F6（配置级）`buttons=2` 被两个驱动同时认领

`hakios_buttons_init()` 判 `buttons != 0` 就起线程（`drivers/input/hakios.buttons.c:524-537`），`hakios_mt6895_keys_init()` 判 `buttons == 2`（`mtk/mt6895/drivers/input/hakios.mt6895_keys.c:578`）。`buttons=2` 时**两个都会起**，读同一批 PMIC 按键、往同一个 `/dev/hakios-touch` 投两次事件。

本快照不触发，因为 `CONFIG_HAKIOS_BUTTONS is not set`（`stubs.c:47`）。但这是「配置一改就双投」的雷。`.config:71` 的 `CONFIG_BOOTARGS` 明确带 `buttons=2`。

### F7（信息级）`xaga.config` 里的 `headset=1` 没进 `.config`

| 文件 | 值 |
|---|---|
| `devices/xaga/configs/xaga.config:40` | `... mali_preas0=1 headset=1` |
| `.config:71` | `... mali_preas0=1`（**无 headset**） |

`.config:2` 写明它由 `devices/xaga/configs/xaga.config` 等片段生成，但两者不一致。`xaga.config` 的 mtime（00:46）晚于 `.config`（02:48 前一天）——**未验证**是 Codex 手工改了片段还没重生成，还是生成流程会追加。后果：当前内核 `headset` boot-arg 缺席，`hakios_mt6895_headset_init()` 在 `hakios.mt6895_headset.c:466` 直接返回。

---

## 6. 建议的正确顺序

### 6.1 结论：**顶层顺序不需要改**

理由（全部有据）：

1. 21 条调用里只有 5 条真执行，且**这 5 条互不依赖**（§4 的表）。
2. 串行阶段**零阻塞**（§1.3）——没有任何 init 会让后面的等。
3. 所有硬件访问都在各自 worker 里，进入 worker 的时刻由 `kernel_thread_start` 决定，**与顶层顺序无关**。
4. `mali610` 与 `keys` 的冲突不在「谁先谁后」，而在「两者都不加锁」。**调顺序解决不了并发**；只有锁能。锁已经加了（快照 C）。

所以任何「把 mali610 挪到 keys 前面 / 后面」的改动都是在动一个**不构成因果**的变量。这一点值得明确写下来，因为它是本轮审计最容易被误判的地方。

### 6.2 真正该改的三件事（按优先级）

**① 重生成构建清单（F1）—— 否则 B1 的修法根本没编进去。**
`tools/hakios-config.py` 从 `.config` 重新生成 `bsd/conf/files` 与 `bsd/dev/hakios.drivers.h`。

**② 让 headset 的开关自治（F2/F7）。**
两条路，选一条：

- **A（改调用点）**：把 `hakios_mt6895_headset_init()` 从 keys worker 里移出来，改成一个「注册」函数，由 `hakios_input_init()` 在顶层显式调用，内部只做参数校验和状态提交，真正的 init 仍由 keys worker 在 `:420` 执行。
  好处：`headset=N` 有独立日志；坏处：多一个跨文件状态。
- **B（改依赖声明）**：在 `mtk/mt6895/drivers/audio/conf` 里把 `requires MTK6895_KEYS` 保留，但在 keys 的 init 里加一行：`buttons=2` 未给而 `headset!=0` 时 `printf` 一条明确的话。
  好处：零结构改动；坏处：仍然无法独立工作。

**推荐 B**：成本最低，且当前真正的问题是「静默失效」而不是「架构错了」。headset 与 keys 共用 PMIF 通道这个事实决定了它们**必须**在同一个 worker 里——`headset.c:13-15` 已经把理由写清楚了（"There is no second bus worker"）。

**③ 若 `MALI` 与 `MALI610` 有朝一日同时编入，在 config 层互斥（F5）。**
`tokens/hakios-config.py` 支持 `requires` 但没有 `conflicts`。要么加一个 `conflicts` 关键字，要么在 `mtk/mt6895/drivers/gpu/conf` 与 `mtk/mt6768/drivers/gpu/conf` 的 help 里显式写明「与另一个互斥，二选一」，并让 `hakios-config.py` 在两者都 `=y` 时报错。

### 6.3 一个可选的加固（F4）

给 `mali_vstack_rail()` 的读-改-写序列加一把消费者级的 mutex（`lck_mtx`，不是 `lck_spin`——它中间有 `delay(500)`，不能在自旋锁里睡）。这样「事务原子」升级为「序列原子」，F4 从「靠约定」变成「靠机制」。

**这需要额外验证**：`pmif_init()` 在 `hakios_spmi_lock` 里调 `ml_io_map` 已经在注释里被承认会分配（`hakios.mt6895_pmif.c:38` "Mapping can allocate/sleep"），而 `lck_spin` 里睡眠是错的——那一段值得单独审（本次未深入，标记**未验证**）。

---

## 7. 脚本与产物

| 文件 | 说明 |
|---|---|
| `docs/headset-wip/init-order-audit.py` | 审计器；`--selftest` 20 条断言；`--json` 出机器可读表 |
| `docs/headset-wip/findings/init-order.json` | 21 条记录，字段见下 |
| `docs/headset-wip/findings/snapshot-20261115-005739/` | 快照 A（重构前），含 `MD5SUMS.txt` |
| `docs/headset-wip/findings/snapshotB-20261115-010528/` | 快照 B（共享层落盘），含 `MD5SUMS.txt` |

`init-order.json` 每条记录的字段：

```
order / fn / src
threads / worker / worker_forever     起线程？worker 叫什么？worker 里是无限循环吗？
pmif / pmif_where / pmif_depth / pmif_shared
                                      访问 PMIF？在 init 还是 worker？几跳？走共享层？
gpu                                  访问 0x13000000？（含宏展开）
blocks_ms / sync_forever / unbounded_poll
built / config                       编入内核？对应哪个 CONFIG
maps                                 该驱动 ml_io_map 了哪些块
note
```

### 7.1 脚本实跑输出（快照 C）

```
 #  function                       thr  PMIF   GPU  block(ms) loop    built source
----------------------------------------------------------------------------------------------------
 1  hakios_cpufreq_init            -    -      -          0.0 -       stub  drivers/power/hakios.cpufreq.c:560
 2  hakios_virtio_input_init       -    -      -          0.0 -       stub  drivers/input/hakios.virtio_input.c:343
 3  hakios_goodix_init             -    -      -          0.0 -       stub  drivers/input/hakios.goodix.c:1526
 4  hakios_tcm_init                -    -      -          0.0 -       stub  mtk/mt6768/drivers/input/hakios.tcm.c:487
 5  hakios_nvt_init                yes  -      -        202.0 worker  yes   mtk/mt6895/drivers/input/hakios.nvt.c:1533
 6  hakios_buttons_init            -    -      -          0.0 -       stub  drivers/input/hakios.buttons.c:517
 7  hakios_mtk_keys_init           -    -      -          0.0 -       stub  mtk/mt6768/drivers/input/hakios.mtk_keys.c:350
 8  hakios_mt6895_keys_init        yes  worker -          0.0 worker  yes   mtk/mt6895/drivers/input/hakios.mt6895_keys.c:573
 9  hakios_mt6895_usb_init         yes  -      -          0.0 -       yes   mtk/mt6895/drivers/usb/hakios.mt6895_usb.c:1861
10  hakios_battery_init            -    -      -          0.0 -       stub  drivers/power/hakios.battery.c:178
11  hakios_ufs_init                -    -      -          0.0 -       stub  drivers/storage/hakios.ufs.c:1710
12  hakios_net_init                -    -      -          0.0 -       yes   drivers/net/hakios.net.c:423
13  hakios_adreno_init             -    -      -          0.0 -       stub  drivers/gpu/hakios.adreno.c:3757
14  hakios_mali_init               -    -      -          0.0 -       stub  mtk/mt6768/drivers/gpu/hakios.mali.c:1133
15  hakios_mali610_init            yes  worker yes     2000.0 -       yes   mtk/mt6895/drivers/gpu/hakios.mali610.c:5043
16  hakios_mtk_connsys_init        -    -      -          0.0 -       stub  mtk/mt6768/drivers/wifi/hakios.mtk_connsys.c:729
17  hakios_mtk_scp_init            -    -      -          0.0 -       stub  mtk/mt6768/drivers/input/hakios.mtk_scp.c:524
18  hakios_wifi_init               -    -      -          0.0 -       stub  drivers/wifi/hakios.wifi.c:4314
19  hakios_adsp_init               -    -      -          0.0 -       stub  drivers/audio/hakios.adsp.c:3262
20  hakios_mpss_init               -    -      -          0.0 -       stub  drivers/modem/hakios.mpss.c:6345
21  hakios_mtk_emmc_devfs          -    -      -          0.0 -       stub  mtk/mt6768/drivers/storage/hakios.mtk_emmc.c:453

init calls                  : 21
built (not a stub)          : 5
stub (empty body)           : 16
start a thread              : 4   hakios_nvt_init, hakios_mt6895_keys_init, hakios_mt6895_usb_init, hakios_mali610_init
access PMIF/SPMI            : 2   hakios_mt6895_keys_init(worker, depth 1), hakios_mali610_init(worker, depth 3)
access GPU 0x13000000       : 1   hakios_mali610_init
init body loops forever     : 0
worker loops forever        : 3   hakios_nvt_init, hakios_mt6895_keys_init, hakios_mt6895_usb_init
worker polls w/o timeout    : 1   hakios_mt6895_usb_init
delays in the init path     : 2202.0 ms
```

### 7.2 `--selftest`（20/20）

```
PASS  A1  第一条调用是 hakios_cpufreq_init
PASS  A2  最后一条调用是 hakios_mtk_emmc_devfs
PASS  A3  hakios_mt6895_headset_init 不在顶层调用列表里
PASS  A4  hakios_mt6895_keys_init 调 kernel_thread_start
PASS  A4  hakios_mali610_init 调 kernel_thread_start
PASS  A4  hakios_nvt_init 调 kernel_thread_start
PASS  A4  hakios_mt6895_usb_init 调 kernel_thread_start
PASS  A5  PMIF 使用者恰为 keys 与 mali610
PASS  A6  两者都是在自己的 worker 里访问 PMIF（串行阶段不碰）
PASS  A7  mali610 到 PMIF 是三跳（init→thread→bring_up→vstack_rail）
PASS  A8  未编入的 cpufreq 判为 stub 且不声明任何原语
PASS  A9  stub 数量为 16（21 条调用里 5 条编入）
PASS  A10 keys 的 PMIF 轮询带 deadline（不算无超时）
PASS  A11 mali610 的 blocks_ms 覆盖 worker 里 delay(2000*1000) = 2000 ms
PASS  A12 每个 stub 都不声明线程/PMIF/GPU（空体不该有任何原语）
PASS  A13 没有任何 init 体自己在死循环（串行阶段不会卡死）
PASS  A14 GPU 窗口只被 mali610 用宏 MALI_GPU_PHYS 间接引用
PASS  A15 nvt 的 worker 在轮询前先睡 200 ms（blocks_ms == 202.0）
PASS  A16 keys 与 mali610 都经共享 pmif_* API 访问（同一把锁、同一份映射）
PASS  A17 没有 init 在串行阶段直接访问 PMIF

20/20 passed
```

---

## 8. 未验证 / 边界

| 项 | 状态 |
|---|---|
| `pmif_init()` 在 `hakios_spmi_lock`（`lck_spin`）里调 `ml_io_map` 是否违反「自旋锁内不得睡眠」 | **未验证**。代码注释自己承认 "Mapping can allocate/sleep"（`hakios.mt6895_pmif.c:38`），但映射是在 `:39` 锁外做的、`:40` 才回锁里赋值——看起来作者已经考虑到，但没有文档说明 `ml_io_map` 在 `hakios_input_init()` 相位是否真会阻塞 |
| `buttons=2` 时 `hakios_buttons_init` 与 `hakios_mt6895_keys_init` 双投 | **理论推演**，当前 `CONFIG_HAKIOS_BUTTONS` 未编入，未在真机验证 |
| `xaga.config` 的 `headset=1` 为何没进 `.config` | **未验证**（可能是生成流程时差，见 F7） |
| `ep0_poll` 在 USB 控制器掉电时的行为 | **未验证**（需真机；内核无异常向量，预期 bus abort） |
| `headset=3`（其他模式） | 不存在；`hakios.mt6895_headset.c:468` 只接受 1 和 2 |
| `mtk/mt6895/drivers/core/` 是否是 `hakios-config.py` 会扫描的目录 | **未验证**。其它 MTK 驱动都在 `mtk/mt6895/drivers/<subsystem>/`，`core` 是新加的目录名；conf 里写的是 `bsd/dev/hakios.mt6895_pmif.c`，但源文件在 `mtk/mt6895/drivers/core/`，这个路径映射是否被生成器认得，需看 F1 的重生成结果 |

---

## 9. 一句话给主控

**21 条 init，5 条真编入，4 条起线程，2 条访问 PMIF（都在各自的 worker 里，不在串行阶段），0 条在 init 体里死循环，串行阶段零阻塞。最危险的竞态不是「谁先谁后」，是 keys 每 20 ms 的 SID 4 读与 mali 在 t=2000ms 起的 SID 5 VBUCK2 读-改-写打在同一个 SWINF_2 通道上——B1。快照 C 里这三份驱动已经改走共享 `pmif_*` API 并共用 `hakios_spmi_lock`，逐事务互斥成立；但 `hakios.mt6895_pmif.c` 还没进 `bsd/conf/files` 和 `hakios.drivers.h`，所以这份修法此刻编不进内核（F1）。**
