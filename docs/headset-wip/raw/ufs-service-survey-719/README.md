# HOST-SURVEY-719：XNU 只读设备 API、并发与 return gate 证据路径

> 2026-11-17 18:20，DSH。**纯源码调查。未查设备、未重跑 718、未改生产、未构建。**
> CMD 身份：`c48d65bb935f72037d5dfa6226e9dfe7d023ce9c68ebac437548d711b821b177`

**基准树**：`logs/ufs-response-20261117/kernel-src`（5210 文件，718 的隔离树）

**🔴 本文严格区分**「源码事实」与「设计推断」**，每条事实给出 `file:line`。**

---

## 0. 四个问题的答案速览

| # | 问题 | 答案 |
|---|---|---|
| **1** | init 在哪跑、能否注册设备 | **`bsd_init()` 的 `#if DEVFS` 块内**，devfs 已挂载，**同块内已有 5 个 bdevsw 实例** |
| **2** | 有无并发存储调用者 | **没有** —— 唯一"真实" UFS 实现**未编译**，实际编入的是空 stub |
| **3** | return gate / HLOG 证据路径 | `loader_log_putc` **可在 quiesce 后安全追加**，无需额外存储写 |
| **4** | sleep/thread/read(2) 设施 | `msleep`/`wakeup` + `uiomove` **本树已有完整范例** |

---

## 1. 【源码事实】问题 1：init 的执行上下文

### 1.1 调用链

| 层 | 位置 | 说明 |
|---|---|---|
| **入口** | `bsd/kern/bsd_init.c:485` | `bsd_init(void)` |
| **DEVFS 块** | `bsd_init.c:981` | `#if DEVFS` |
| **devfs 挂载** | `bsd_init.c:986` | `devfs_kernel_mount("/dev")` |
| **hakios init** | `bsd_init.c:987-992` | 五个 init 依次调用 |
| **我们的调用** | **`bsd/dev/hakios.input.c:231`** | **`hakios_mt6895_ufs_read_init()`** |

**`bsd_init.c:988-992` 原文：**

```c
devfs_kernel_mount(mounthere);
hakios_version_init();         /* kern.hakios.version, and the version in the boot log */
hakios_input_init();           /* /dev/hakios-touch */
hakios_audio_init();           /* /dev/hakios-audio */
hakios_virtio_blk_init();      /* /dev/rvblk0, a disk image of the host (QEMU) */
hakios_sparse_init();          /* /dev/rsp0, part of an image too big to hold */
```

### 1.2 这个上下文能做什么（源码可证）

| 能力 | 证据 |
|---|---|
| **devfs 已挂载** | `bsd_init.c:986` 就在我们的调用之前 |
| **cdevsw 可用** | `hakios.input.c:223` 已调用 `cdevsw_add(-1, &touch_cdevsw)` 并成功 |
| **devfs_make_node 可用** | `hakios.input.c:224` |
| **bdevsw 可用** | `hakios.virtio_blk.c:549` 在**同一 init 序列**里调用 `bdevsw_add` |
| **lck_mtx 可用** | `hakios.input.c:61` `LCK_MTX_DECLARE(touch_lock, ...)` |

**🔴 结论：`hakios_input_init()` 的上下文**完全支持**注册一个真正的块/字符设备 ——
因为同一函数里已经注册了一个字符设备，同一 init 序列里已经注册了块设备。**

### 1.3 真实原型（`bsd/sys/conf.h`）

| API | 行 | 原型 |
|---|---|---|
| `struct bdevsw` | **157** | 结构定义 |
| `bdevsw_add` | **305** | `int bdevsw_add(int, const struct bdevsw *)` |
| `cdevsw_add_with_bdev` | **309** | `int cdevsw_add_with_bdev(int index, const struct cdevsw *csw, int bdev)` |

### 1.4 可工作范例：`hakios.virtio_blk.c`

**bdevsw（第 429-437 行）：**

```c
static const struct bdevsw blk_bdevsw = {
	.d_open     = blk_dev_open,
	.d_close    = blk_dev_close,
	.d_strategy = blk_dev_strategy,
	.d_ioctl    = blk_dev_ioctl,
	.d_dump     = eno_dump,
	.d_psize    = blk_dev_size,
	.d_type     = D_DISK,
};
```

**注册（第 549-564 行）：**

```c
blk_bmajor = bdevsw_add(-1, &blk_bdevsw);
blk_cmajor = cdevsw_add_with_bdev(-1, &blk_cdevsw, blk_bmajor);
devfs_make_node(makedev(blk_bmajor, 0), DEVFS_BLOCK, UID_ROOT, GID_OPERATOR, 0600, "rvblk0");
devfs_make_node(makedev(blk_cmajor, 0), DEVFS_CHAR,  UID_ROOT, GID_OPERATOR, 0600, "rrvblk0");
```

**→ 这是本树里**唯一**完整可工作的块设备注册模式，可直接作为 719 的骨架参考。**

**🔴 我未复制任何 redwood MMIO 地址，也未调用 redwood UFS 驱动。**

---

## 2. 【源码事实】问题 2：有没有并发存储调用者

### 2.1 🔴 关键事实：真实 UFS 实现**未编入**

**`bsd/conf/files` 第 148-149 行：**

```
bsd/dev/hakios.ufs.c                   optional hakios_ufs
bsd/dev/hakios.ufshc.c                 optional hakios_ufs
```

**`mt6895.config` 第 39 行：**

```
# CONFIG_HAKIOS_UFS is not set
```

**编译产物验证：**

| 文件 | 存在 |
|---|---|
| `hakios.ufs.o` | **❌ 不存在** |
| `hakios.ufshc.o` | **❌ 不存在** |
| **`hakios.stubs.o`** | **✅ 存在** |

### 2.2 实际编入的是空 stub

**`bsd/dev/hakios.stubs.c` 第 115-118 行：**

```c
void hakios_ufs_init(void) {}
void hakios_ufs_report(__unused const char *when) {}
bool hakios_ufs_dev(__unused dev_t dev) { return false; }
void hakios_ufs_quiesce(void) {}
```

**→ 这四个函数**做不了任何存储访问**。**

### 2.3 所以并发结论

| 问题 | 答案 |
|---|---|
| 有没有别的内核路径会碰 UFS？ | **没有** —— 真实实现未编译 |
| 会不会与新服务抢队列？ | **当前不会**，因为**没有竞争者** |
| 这是「独占所有权」的证据吗？ | **🔴 不是** —— 这只是「**没有**别人」，不是「**证明**别人被排除」 |

**🔴 按 CMD 要求：这是「absence」而非「exclusive ownership」的证据。**
**若将来启用 `CONFIG_HAKIOS_UFS`，`hakios.ufs.c` 的 1710 行实现会变成真正的竞争者。**

### 2.4 未编译但仍存在的文件

**`hakios.ufs.c`（1710 行有 `hakios_ufs_init`）与 `hakios.ufshc.c`（1316 行有 `hakios_ufs_quiesce`）
**留在树里但未编入** —— 这是潜在的未来冲突源，**当前不构成风险**。

---

## 3. 【源码事实】问题 3：return gate 与 HLOG 证据路径

### 3.1 `loader_return` 的实际顺序（`osfmk/arm64/loader_return.c:368-395`）

```c
psci_smp_stop_others();
hakios_mt6895_headset_quiesce();   /* software stop only: other CPUs may not all have stopped */
hakios_mt6895_ufs_read_quiesce();  /* software STOP/shared lease only; no controller access */
hakios_ufs_quiesce();              /* ← 这是空 stub */
hakios_adreno_quiesce();
... (其余外设 quiesce)
loader_log_linearize();
```

**🔴 关键观察：`hakios_ufs_quiesce()` 在 `hakios_mt6895_ufs_read_quiesce()` **之后**，
且它是空 stub —— 所以**当前没有任何存储侧动作会干扰 lease 快照。**

### 3.2 HLOG 追加通道：`loader_log_putc`

**存在且可用（`loader_return.c` 第 45 行声明，`loader_return.c:93` 定义）。实现：**

```c
loader_log_putc(char c)
{
	uint64_t daif;
	bool locked = false;
	if (c == '\r') return;
	daif = __builtin_arm_rsr64("DAIF");
	__builtin_arm_wsr("DAIFSet", DAIFSC_IRQF | DAIFSC_FIQF);   /* 关中断 */
	for (uint32_t tries = 0; !locked && tries < 10000000; tries++) {
		locked = os_atomic_cmpxchg(&loader_log_lock, 0, 1, acquire);
	}
	loader_log_put_locked(c);
	if (locked) os_atomic_store(&loader_log_lock, 0, release);
	__builtin_arm_wsr64("DAIF", daif);
}

**关键性质：**

| 性质 | 值 |
|---|---|
| **关中断** | ✅ `DAIFSet` |
| **自旋锁** | ✅ `os_atomic_cmpxchg` |
| **有界重试** | ✅ 1000 万次，超时后**仍写**（不是无限等）|
| **无存储访问** | ✅ 只写 `loader_log` 内存缓冲区 |
| **无 printf 格式化** | ✅ 逐字符 |

**HLOG 缓冲区尺寸（第 36-38 行）：**

| 常量 | 值 |
|---|---|
| `LOADER_LOG_SIZE` | 1536 KiB |
| `LOADER_LOG_HEAD` | 256 KiB |
| `LOADER_LOG_RING` | 1 MiB |

### 3.3 记录 lease 快照的最小侵入方案（**设计推断**）

**按 CMD 要求「without extra storage writes, unsafe kernel printf after SMP stop or guard bypass」：**

| 方案 | 评价 |
|---|---|
| **A. 在 `hakios_mt6895_ufs_read_quiesce()` 末尾，用 `loader_log_putc` 逐字符写一行 lease 快照** | **✅ 推荐** —— 无存储写、无 printf、无 guard 绕过 |
| B. 在 `loader_return` 里 quiesce 之后加 printf | ❌ SMP 已停，printf 路径未验证 |
| C. 写到 xnusys 额外位置 | ❌ 违反「无额外存储写」 |

**方案 A 的可行依据：**

1. `loader_log_putc` **本身就在这个文件里**，quiesce 函数可 extern 引用
2. 它**只写内存**，loader 在返回后按 `LOADER_LOG_SIZE` 读走
3. **关中断+自旋锁**保证 SMP 停止时不撕裂
4. **有界重试**保证不死锁

**🔴 但这是推断，未经验证。** 需要确认：
- `hakios_mt6895_ufs_read_quiesce()` 在 `loader_return.c` 的编译单元里能否直接调 `loader_log_putc`（跨文件符号可见性）
- `loader_log_linearize()` 在 quiesce **之后**才调用，所以**追加的内容会被包含**

---

## 4. 【源码事实】问题 4：sleep/thread/read(2) 设施

### 4.1 本树已有的完整 read(2) 范例：`touch_read`

**`bsd/dev/hakios.input.c:157-180`：**

```c
touch_read(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
	struct hakios_touch t;
	int error = 0, moved = 0;

	if (uio_resid(uio) == 0) return 0;
	if (uio_resid(uio) < (user_ssize_t)sizeof(t)) return EINVAL;

	lck_mtx_lock(&touch_lock);
	while (touch_head == touch_tail) {
		error = msleep(&touch_queue, &touch_lock, PZERO | PCATCH, "hakios-touch", NULL);
		...
	}
	...
	error = uiomove((caddr_t)&t, sizeof(t), uio);
	...
}
```

**→ 这是**串行化 4KiB 读取**可直接套用的骨架。**

### 4.2 sleep/wakeup 在树里的实际使用

| 文件 | 行 | 用法 |
|---|---|---|
| `hakios.apfs_check.c` | 704、801 | `msleep(&par_stack, &par_lock, PRIBIO, "apfs-walkers", NULL)` |
| `hakios.apfs_check.c` | 687、712、730 | `wakeup(&par_stack)` |
| `hakios.camera.c` | 3414 | `msleep(&darwin_readers, &darwin_lock, PZERO, "hakios-camera-drain", NULL)` |
| `hakios.camera.c` | 3429、3609 | `wakeup(&darwin_frames_done)` |

### 4.3 锁类型

**`hakios.input.c:61`：**

```c
static LCK_MTX_DECLARE(touch_lock, &touch_lck_grp);
```

**配合 `lck_mtx_lock` / `lck_mtx_unlock`（第 115、134、169 行）。**

### 4.4 所需头文件与错误返回（**源码可证**）

| 项 | 依据 |
|---|---|
| `struct uio` / `uio_resid` / `uiomove` | `hakios.input.c` 已在用 |
| `msleep` / `wakeup` | `hakios.apfs_check.c`、`hakios.camera.c` 已在用 |
| `LCK_MTX_DECLARE` / `lck_mtx_*` | `hakios.input.c:61` 已在用 |
| **错误返回** | `EINVAL`（第 167 行）、`0`（第 164 行）—— 本树已验证的用法 |

**🔴 我未实现 worker，也未改生产代码。**

---

## 5. 【设计推断】对 719 工作方向的评估

### 5.1 CMD 给出的方向

> 「Hold shared CLAIMED across calls; restore original table/UCD/DATA after every command.
> Cached software distinguishes active DMA/preparation, clean result-copy ownership,
> clean idle and closed/poisoned.」

### 5.2 我认为**可行**的部分

| 项 | 依据 |
|---|---|
| **设备注册** | §1.4 的 `virtio_blk` 模式已验证可用 |
| **串行化读取** | §4.1 的 `touch_read` 模式已验证可用 |
| **跨调用保持 CLAIMED** | 缓存 phase 已是静态变量，`bsd_init` 上下文可初始化 |
| **每次恢复 table/UCD/DATA** | 718 的 `ur_restore` 已实现并有测试 |
| **quiesce 只关闭 clean 状态** | §3.2 的 `loader_log_putc` 可记录证据 |

### 5.3 我认为需要**额外澄清**的点

| # | 问题 | 为什么重要 |
|---|---|---|
| **1** | 「clean result-copy ownership」与「clean idle」的区别 | CMD 说缓存要区分四态，但**「结果拷贝所有权」的判据未定义** |
| **2** | 每次请求后恢复 table/UCD/DATA，但**保持 CLAIMED** | 716-718 的引擎是**单次用完即 RESTORED**；改成跨调用保持需要**重新设计 lease 状态机** |
| **3** | 「不能让第二个请求覆盖第一个结果」 | 需要**明确的缓冲区交接协议**，不只是锁 |
| **4** | `hakios_ufs_quiesce` 当前是空 stub | 若将来启用真实 UFS，**顺序需要重排** |

### 5.4 一个我认为必须提的风险

**🔴 §2.3 的结论是「absence of competitors」而不是「exclusive ownership」。**

**当前安全是因为没有别人碰 UFS。一旦 `CONFIG_HAKIOS_UFS` 被启用，
`hakios.ufs.c`（1710 行的真实实现）就会成为**真正的并发竞争者**。**

**→ 719 的设计**不应假设**独占是永久的。建议在文档里明确「本服务假定 UFS 由 loader 移交且无其他内核使用者」，
并把这条假设作为**前置条件**写进契约。**

---

## 6. 阻塞与建议

### 6.1 没有硬阻塞

**四个问题都有源码级答案，设备注册与串行化读取都有本树内可复制的范例。**

### 6.2 建议的最小集成路径

| 步 | 内容 | 依据 |
|---|---|---|
| **1** | 用 `cdevsw_add` 注册一个字符设备（不需要 bdevsw，因为不做块层） | §1.4 简化版 |
| **2** | `.d_read = ...` 套用 `touch_read` 骨架，`msleep`/wakeup 串行化 | §4.1 |
| **3** | 每次 read 内调引擎，用 `ur_restore` 恢复三块内存 | §5.2 |
| **4** | 跨调用保持 phase CLAIMED，用锁保护 | §5.3 第 2 点需重新设计 |
| **5** | quiesce 时用 `loader_log_putc` 记一行 lease 证据 | §3.3 方案 A |

### 6.3 我不建议现在做的事

| 项 | 理由 |
|---|---|
| **扩大 LBA / 命令白名单** | CMD 明确说「Do not broaden yet」 |
| **假设独占永久成立** | §5.4 |
| **在 quiesce 里用 printf** | SMP 已停，路径未验证 |
| **直接复用 `hakios.ufs.c`** | 未编译，且含未审的 MMIO |

---

## 7. 边界

**未做**：设备命令、寄存器访问、重跑 718、构建、重测、
生产编辑或 push、旧树/BUILD 覆盖、GPUEB、clock/reset/UIC/watchdog/OEM expdb。

**当前 stock ID `f745b1c6-ee43-4abc-8ed2-06ee9629bf27` 是历史记录，不是本次观测。**

**🔴 本文所有 `file:line` 均来自 718 的隔离树，未验证运行时行为。**

---

## 8. 🔴 追加更正（用户指出三点，我核实后确认）

**以下三条是我上一节里的**错误或过度推论**，原文保留在上，此处更正。**

### 更正 A：源码里有 API 调用 ≠ 本设备运行成功注册

**我写过：**

> ~~「`hakios.input.c:223` 已成功 `cdevsw_add`」~~
> ~~「这是本树里**唯一**完整可工作的块设备注册模式」~~

**两处都不准确：**

| 我写的 | 问题 |
|---|---|
| 「**已成功** cdevsw_add」 | **源码里有调用不证明运行时成功**。`cdevsw_add` 返回负值时走 `printf` 分支，我**没有** `/dev/hakios-touch` 实际存在的证据 |
| 「**唯一**完整可工作范例」 | 我**没有**穷举全树，也没有任何 `virtio_blk` 在本设备**实际工作**的证据（它注释里写的是 QEMU）|

**准确表述：**

- `hakios.input.c:223` **有** `cdevsw_add` 调用，且**同一函数**里有 `devfs_make_node` 调用
- `hakios.virtio_blk.c:429/549` 是**本树内**的 `bdevsw` 注册**代码范例**（注释显示面向 QEMU）
- **两者在本设备的运行时成功均未被验证** —— 本轮是纯源码调查，没有任何设备观测

**→ 「能注册设备」这个结论只能降到：「源码路径上存在完整调用，运行时成功未验证」。**

### 更正 B：未编入 native UFS **只排除该已知竞争者**，不证明全局独占

**我写过：**

> ~~「→ 当前没有竞争者」~~

**问题：我只检查了 `hakios.ufs.c` / `hakios.ufshc.c` 两条路径未编入。
这不等于全树没有任何其他存储访问。**

**准确表述：**

| 事实 | 边界 |
|---|---|
| `CONFIG_HAKIOS_UFS` 未开，`hakios.ufs.c`/`ufshc.c` 未编入 | **✅ 排除了这两条已知路径** |
| 全树是否还有别的 UFS/存储访问者 | **🔴 我没有穷举验证** |
| 「独占所有权」 | **🔴 完全没有证据** |

**→ 上一节 §2.3 与 §5.4 说的「absence 而非 exclusive ownership」方向是对的，
但我写「当前没有竞争者」仍然越界 —— 应改为「排除了这两个已知竞争者」。**

### 更正 C：🔴 `loader_log_putc` **不能**用来记录 lease 快照

**我建议过：**

> ~~「在 quiesce 末尾用 `loader_log_putc` 逐字符写一行 lease 快照是最小侵入方案」~~

**我当时的理由是「有界重试保证不死锁」。** 重读实现后，这个理由**不成立**：

```c
for (uint32_t tries = 0; !locked && tries < 10000000; tries++) {
    locked = os_atomic_cmpxchg(&loader_log_lock, 0, 1, acquire);
}
loader_log_put_locked(c);          /* ← 无条件调用，不看 locked */
if (locked) {
    os_atomic_store(&loader_log_lock, 0, release);
}
```

**🔴 关键缺陷（我漏了）：**

| 情形 | 实际行为 |
|---|---|
| 拿到锁 | 正常写，然后释放 |
| **一千万次仍未拿到锁** | **`locked == false`，但 `loader_log_put_locked(c)` 照样执行** |
| 后果 | **无锁写，与其他 CPU 的写并发交错，不保证不撕裂** |

**→ 「有界」只保证**不会永远自旋**，**不保证**写入的原子性。**

**按用户指示：quiesce 继续**只用原子操作 / 对齐 shared-DRAM 发布**，**不用 `loader_log_putc`**。

### 更正 C 的替代方案（用户的建议，我核对后认为可行）

**用户提出：`save_log_spans_to_flash` 已支持多 span，
可在 loader 严格 gate 通过后，把**实际 lease 快照**作为**第二个 span**随既有 HLOG 一起写。**

**我核对了签名与实现（`mtkloader.c:1801`）：**

```c
static int save_log_spans_to_flash(const struct span *sp, int nsp, uint64_t reason,
                                   uint64_t code, uint64_t elr, uint64_t far)
{
    ...
    for (int i = 0; i < nsp; i++) {
        if ((!sp[i].p && sp[i].n) || sp[i].n > ~0ull - total) return 0;
        total += sp[i].n;
    }
    ...
    for (int i = 0; i < nsp; i++) { ... }   /* 逐 span 写入 */
```

**→ `nsp` 是**显式参数**，循环处理任意个 span，**方案可行**。**

**为什么这个方案比我的更好：**

| 维度 | `loader_log_putc`（我的） | **多 span（用户的）** |
|---|---|---|
| 内核侧并发安全 | ❌ 锁失败仍写 | **✅ 内核完全不参与** |
| 存储写次数 | 1 次（HLOG）| **1 次（同一 HLOG）** |
| 数据来源 | 内核内存缓冲区 | **loader 直接读 shared DRAM** |
| guard 影响 | 无 | **无（gate 通过后才写）** |

**→ 内核只需把快照**发布到 shared DRAM**（原子/aligned store），
**loader 在 gate 通过后自己读走并作为第二 span 写入**。

**这条路把「记录证据」的责任从内核移到 loader，避开了内核侧并发问题。**

---

## 9. 更正后的问题 3 结论

| 项 | 更正前 | **更正后** |
|---|---|---|
| quiesce 动作 | 提议用 `loader_log_putc` 写日志 | **只做原子 STOP + shared DRAM 发布** |
| 快照记录方式 | 内核写日志 | **loader 在 gate 通过后作为第二 span 写** |
| 是否需要内核 printf | 否 | **否** |
| 是否需要额外存储写 | 否 | **否（复用同一 HLOG 写入）** |

**剩余未验证项：**

- `save_log_spans_to_flash` 的 `XNU_LOGBOOK_MAX` 是否容得下两个 span（我**未算**）
- 第二 span 的**来源地址**如何传给 loader（可能需 loader 侧新增 argv 或约定固定地址）
- lease 快照在 shared DRAM 的**具体布局**（16 字节结构已有，但发布时机需与 gate 对齐）

**这些都需要你在新候选里定义，我只做源码调查。**
