# 服务核心 719 审查（sealed-core-719-v1）

> 2026-11-17 18:45，DSH。**仅主机。未改候选、未构建、未碰设备。**
> CMD 身份：`3c3aa989a7d38d786f21973bba917d079b5ae80f4800adf27817f7a47b663516`

**🔴 平台与 loader **不在**本次审查范围（Codex 独立实现中）。**

---

## 0. 结果

| 项 | 值 |
|---|---|
| **8 文件 MANIFEST** | **8/8 全匹配** |
| **service 测试** | **34121 / reads=5530 / regs=49 / writes=2908**（ASan+UBSan 与 MSan 各一次）|
| **race 测试** | **2500 schedules**，两种 sanitizer 各 32499 / 32497 checks |
| **ARM64 codec** | **rc=0**，5696 B |
| **具体阻塞** | **无** |

---

## 1. 8 个冻结文件（8/8）

| 文件 | 字节 | SHA256 前 16 |
|---|---|---|
| `ufs_service_lease.h` | 3236 | `05432b2c8c295157` |
| `ufs_service_engine.h` | 2511 | `08d3bcc94dcfff80` |
| `ufs_read_engine.h` | 11027 | `81aeee096c339fbb` |
| `ufs_read_contract.h` | 4684 | `01050e1a69ded8de` |
| `test-ufs-service.c` | 10730 | `a0fcb383e3056b01` |
| `ufs_service_model.h` | 6628 | `48e34e8395ec946a` |
| `test-ufs-service-race.c` | 3960 | `0fab989c7febb14d` |
| `service-arm64.c` | 368 | `0a3926f79c8adafe` |

**复制到私有目录后仍 8/8。**

---

## 2. 状态机审查（`ufs_service_lease.h`）

### 2.1 五态 + 三个标志位

```c
enum us_phase { US_NEW, US_OPENING, US_IDLE, US_ACTIVE, US_DRAINED };
#define US_PHASE_MASK   0xffu
#define US_STOP_BIT     0x100u
#define US_POISON_BIT   0x200u
#define US_RELEASED_BIT 0x400u
```

**🔴 关键设计：phase 与三个标志位在**同一个 32 位字**里，用 `__atomic_*` 操作 ——
所有状态转移是单个原子 RMW，不存在「先读 phase 再改标志」的窗口。**

### 2.2 状态转移全表（我逐个核对）

| 转移 | 函数 | 原子操作 | 前置条件 |
|---|---|---|---|
| `NEW → OPENING` | `us_open` | CAS | `clean_baseline == true` |
| `OPENING → IDLE` | `us_open` | CAS | publish 成功后 |
| `IDLE → ACTIVE` | `us_admit` | **CAS** | **必须精确 IDLE** |
| `ACTIVE → DRAINED` | `us_mark_drained` | **CAS** | **必须精确 ACTIVE** |
| `DRAINED → IDLE` | `us_copy_done` | **CAS** | **必须精确 DRAINED** |

**🔴 三个 CAS 都要求**精确匹配**目标 phase（`expected = US_IDLE/ACTIVE/DRAINED`），
不是掩码比较 —— **重叠准入会被 CAS 天然拒绝**。**

### 2.3 ✅ 无状态重置

**全文件搜 `__atomic_store` 写 `control` —— **零命中**。
所有写都是 `compare_exchange_n` 或 `fetch_or`，**没有任何地方把 phase 设回初值**。**

### 2.4 ✅ 一个 CLAIMED 发布者

**`publish(UR_CLAIMED)` 在**全文件只出现一次**，在 `us_open` 里。**

### 2.5 `us_stop` 的释放逻辑（最需仔细看的部分）

```c
static inline void us_stop(struct us_lease *s)
{
    uint32_t old = __atomic_fetch_or(&s->control, US_STOP_BIT, __ATOMIC_ACQ_REL);
    uint32_t phase = old & US_PHASE_MASK;
    if (old & (US_POISON_BIT | US_RELEASED_BIT)) return;      /* ① 已污染或已释放，直接返回 */
    if (phase != US_NEW && phase != US_IDLE && phase != US_DRAINED) {
        us_poison(s);                                          /* ② ACTIVE/OPENING 污染 */
        return;
    }
    old = __atomic_fetch_or(&s->control, US_RELEASED_BIT, __ATOMIC_ACQ_REL);
    if (old & US_RELEASED_BIT) return;                         /* ③ 只有一个赢家 */
    if (phase != US_NEW && !s->publish(s->context, UR_RESTORED)) us_poison(s);
}
```

**逐点核对 CMD 要求：**

| CMD 要求 | 实现 | 结果 |
|---|---|---|
| **STOP at ACTIVE/OPENING 必须 poison** | ② 分支 | ✅ |
| **STOP at DRAINED 可释放** | 通过 ① 与 ② 后到 ③ | ✅ |
| **重复/重入 STOP** | ① `US_RELEASED_BIT` 检查 + ③ 再检查 | ✅ 幂等 |
| **一个 RELEASED 标志发布者** | ③ `fetch_or` 返回旧值含该位则返回 | ✅ **fetch_or 本身保证原子性，只有一个赢家** |
| **有效 poison 后无迟到 RESTORED** | ① 先检查 `US_POISON_BIT` | ✅ |

**🔴 第 ① 步的 `if (old & (US_POISON_BIT \| US_RELEASED_BIT)) return;` 是防止迟到 RESTORED 的关键：
一旦 poison 或 release 已置位，后续 STOP 直接返回，**不会**再 publish。**

### 2.6 STOP 与 phase 的竞态：为什么不会误释放

**考虑「`us_admit` 与 `us_stop` 同时发生」：**

| 顺序 | 结果 |
|---|---|
| `admit` 赢 | phase = ACTIVE → stop 走 ② → **poison** ✅ |
| `stop` 赢 | phase = IDLE 或 NEW → 置 RELEASED → 若 phase ≠ NEW 则 publish RESTORED |
| | 此时 `admit` 的 CAS 失败（phase 已非 IDLE）→ 拒绝准入 ✅ |

**→ 两种顺序都安全。** 且 `admit` 失败后 `us_read_fixed` 会看 `US_STOP_BIT` 返回 `US_STOPPED`。**

---

## 3. 组合层审查（`ufs_service_engine.h`）

### 3.1 `us_read_fixed` 的完整流程

```c
if (bad args) return US_BAD_REQUEST;                       /* ① 无 IO、无 workspace 变更 */
if (!us_admit(s)) return STOPPED / NOT_READY / BUSY;        /* ② 重叠准入被拒 */
ur_run(io, workspace, out, ...);                            /* ③ 唯一硬件动作 */
phase = us_control(s);
if (phase & US_POISON_BIT) return US_UNCERTAIN;             /* ④ */
if (!out->restored || (phase & US_PHASE_MASK) != US_DRAINED) {
    us_poison(s); return US_UNCERTAIN;                      /* ⑤ */
}
if (phase & US_STOP_BIT) return US_STOPPED;                 /* ⑥ DRAINED STOP 安全 */
if (out->result != UR_READ_OK || !out->data_valid)
    return us_copy_done(s) ? US_COMMAND_ERROR : US_STOPPED;  /* ⑦ */
copied = deliver(destination, out->block, UR_BLOCK_BYTES);   /* ⑧ CPU 拷贝 */
if (!us_copy_done(s)) return US_STOPPED;                    /* ⑨ */
return copied ? US_READ_OK : US_COPY_ERROR;                  /* ⑩ */
```

### 3.2 逐项核对 CMD 关注点

| # | CMD 要求 | 实现 | 结果 |
|---|---|---|---|
| **1** | **无效请求不做 IO、不改 workspace** | ① 在任何 `ur_run` **之前**返回 | ✅ |
| **2** | **结果拷贝期间的重叠准入** | phase 是 `DRAINED`，`us_admit` 要求 `IDLE` → **CAS 必失败** | ✅ |
| **3** | **拷贝失败/STOP 不授权交付** | ⑩ 返回 `US_COPY_ERROR`，⑨ 返回 `US_STOPPED` | ✅ |
| **4** | **ACTIVE→DRAINED 只在完整 restore 后** | ⑤ 检查 `out->restored` **且** phase 必须是 `DRAINED` | ✅ |
| **5** | **`engine.restored` 是**每次请求**的内存证据** | 见 §3.3 | ✅ |
| **6** | **shared 保持 CLAIMED，不是释放的外层租约** | 见 §3.3 | ✅ |
| **7** | **clean closure vs 不确定 DMA** | ⑤ 与 ⑥ 分支 | ✅ |

### 3.3 🔴 「per-request restored」与「shared CLAIMED」的关系（CMD 特别点出）

**这是本轮设计的**核心概念**，我核对后确认实现正确：**

| 概念 | 谁的状态 | 语义 |
|---|---|---|
| **`out->restored`** | **引擎（718）** | **本次请求的内存是否完整恢复** |
| **`shared phase`** | **服务（719）** | **跨调用的外层租约状态** |

**`ur_run` 内部的 `finish` 回调由平台实现，
按 `ufs_service_engine.h` 顶部注释：**

> 「The platform's claim checks an already held shared lease;
> **finish(true) must perform `us_mark_drained`, not release it.**」

**→ 每次请求结束时：**
- **`out->restored = true`**（内存已恢复）
- **`shared phase = DRAINED`**（**不是 RESTORED**）

**🔴 外层 shared 租约**保持 CLAIMED**，只有 `us_stop` 才发布 `UR_RESTORED`。**

**这是正确的分层：718 的「单次用完即 RESTORED」被 719 改写为「单次用完即 DRAINED，外层仍 CLAIMED」。**

### 3.4 ✅ finish 回调后引擎不再操作硬件

**CMD 要求：「Verify that after a true finish callback the unchanged engine performs no hardware operation」。**

**我核对 718 的 `ur_run`（本包里的 `ufs_read_engine.h` 未改）：**

```c
clean:
    if (!ur_restore(io, saved)) goto poison;
    if (!io->finish(io->context, true)) goto poison;
    out->restored = true;
    out->data_valid = out->result == UR_READ_OK;
    return;                        /* ← finish 之后只有赋值，无 io->* 调用 */
```

**→ `finish(true)` 之后**只有 out 字段赋值，没有任何 `io->` 调用** ✅

**这就是「STOP at DRAINED 可安全释放」的依据：
此时引擎已经完全脱离硬件，CPU-only 拷贝可以安全完成。**

---

## 4. 测试复跑（我的私有目录）

### 4.1 service 测试

| sanitizer | build rc | run rc | stderr | 输出 |
|---|---|---|---|---|
| **ASan+UBSan** | **0** | **0** | **0 B** | **`PASS 34121 service lease/engine/copy/stop checks; reads=5530 regs=49 writes=2908; host only`** |
| **MSan** | **0** | **0** | **0 B** | 同上 |

**→ 与 CMD 声明的 34121 / 5530 / 49 / 2908 精确一致。**

### 4.2 race 测试（`-pthread`，2500 schedules）

| sanitizer | checks | schedules | allowed / denied |
|---|---|---|---|
| **ASan+UBSan** | **32499** | **2500** | 1499 / 1001 |
| **MSan** | **32497** | **2500** | 1497 / 1003 |

**🔴 两次运行的 allowed/denied **分布不同**（1499/1001 vs 1497/1003），
这正是**真实线程调度**的体现 —— CMD 明确说「Exact allowed/denied distribution can vary;
check invariants, not scheduler-count equality」。**

**不变量检查都通过（两次都是 rc=0）。**

### 4.3 强制 rendezvous

**测试用 `pthread_barrier_t start, middle`（第 11 行）强制真实线程在特定点会合：**

- `start` barrier：确保两线程同时起跑
- `middle` barrier：在 mode 3/4 时强制 **ACTIVE / DRAINED 时刻的 STOP 会合**

**→ 这是**真实的并发交错**，不是顺序枚举。**

### 4.4 ARM64 codec

**`service-arm64.c` → rc=0，5696 B，零诊断。**

---

## 5. 我的意见

### 5.1 没有具体阻塞

**状态机、组合层、测试覆盖都与 CMD 描述一致，我逐条核对未发现缺陷。**

### 5.2 三处我认为**设计得特别好**的地方

| # | 设计 | 为什么好 |
|---|---|---|
| **1** | **phase 与三个标志位挤在一个 32 位字** | 所有转移是单个原子 RMW，**不存在「读 phase 与改标志之间」的窗口** |
| **2** | **三个 CAS 都要求精确 phase** | 重叠准入、重复 drain、重复 copy_done **都被 CAS 天然拒绝**，不需要额外锁 |
| **3** | **`finish(true)` 只标 DRAINED 不释放** | 精确区分了「本次内存已恢复」与「外层租约可交还」，**这是 718 到 719 最关键的概念升级** |

### 5.3 两条我**仍需你确认**的（不是缺陷，是范围外）

| # | 项 | 为什么 |
|---|---|---|
| **1** | **`clean_baseline` 谁来验证** | `us_open` 接收一个 `bool` 参数，但**这个 bool 的真实性完全由 provider 保证**。头注释也写了「Provider must verify the inherited baseline before calling this」 |
| **2** | **`publish` 返回 false 的语义** | `us_open` 里 publish 失败会 `us_poison`，但 `us_stop` 里 publish 失败也 poison —— **这两处的 poison 是否都会让 loader 看到 `UR_POISONED`？取决于 provider 的 publish 实现** |

**→ 这两条都在平台侧，不在本次审查范围。**

### 5.4 一个术语提醒（按您的要求）

**您指出：「`save` 调用一次 ≠ 物理 `WRITE(10)` 一次」。**

**我在 719 调查文档里写过「存储写次数仍是 1 次」—— 那是**不准确的**。**

**准确表述：`save_log_spans_to_flash` **调用一次**，但内部可能产生**多次** `ufs_write`/`WRITE(10)`
（取决于 span 长度与分块）。**

**→ 我后续文档会区分这两个层次。本次审查未涉及 loader 写路径，故此处只作记录。**

---

## 6. 边界

**未做**：改候选、构建、设备命令、镜像、生产活动、旧物覆盖、GPUEB WIP staging。

**所有 `file:line` 与行为描述均来自源码与主机测试，**未在硬件上验证**。**
