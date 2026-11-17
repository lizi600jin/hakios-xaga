# UFS 引擎 v3 冻结副本审查（716）

> 2026-11-17 13:40，DSH。**仅主机私有目录复跑。未改候选/生产/设备。**
> 对象：`sealed-engine-v3/`（v3 源码声明不再改）

---

## 0. 一、MANIFEST 全 hash 校验：9/9 通过

| 文件 | 字节 | SHA256 |
|---|---|---|
| `atomic-arm64.c` | 279 | `4292d4762f4de23d…` |
| `contract-arm64.c` | 398 | `5085380f21926ffb…` |
| `engine-arm64.c` | 248 | `1e2a98f463634771…` |
| `test-ufs-atomic-lease.c` | 3131 | `2149779b68eb616b…` |
| `test-ufs-read-contract.c` | 4492 | `79c355d1bf789c65…` |
| `test-ufs-read-engine.c` | 9489 | `b2200dfde734e7d5…` |
| `ufs_atomic_lease.h` | 2281 | `54cd7d32ad1d79be…` |
| `ufs_read_contract.h` | 4684 | `0012fb4f4fa84a82…` |
| **`ufs_read_engine.h`** | **9620** | **`7c2b4df4ed6df8437ad70d9545aa015d302e2b97e34822a536fd0965c9e8ca53`** |

**→ 9/9 匹配，0 不符。引擎 hash 与声明的 `7c2b4df4` 一致。**

---

## 1. 二、实际 diff：只有两处改动（我核过）

### 1.1 `ufs_read_engine.h`（`21c88fb5` → `7c2b4df4`，+313 B）

**diff 全文只有两处：**

#### 改动 A：`ur_readback` 新增 DATA/response 预填读回校验

```diff
     for (unsigned i = 0; i < 4; i++)
         if (!io->read_word(io->context, UR_UCD, 1024+i*4, &back.prd[i])) return false;
-    return ur_command_allowed(&back);
+    if (!ur_command_allowed(&back)) return false;
+    for (unsigned i = 0; i < 512/4; i++)
+        if (!io->read_word(io->context, UR_UCD, 512+i*4, &word) || word != 0) return false;
+    for (unsigned i = 0; i < UR_BLOCK_BYTES/4; i++)
+        if (!io->read_word(io->context, UR_DATA, i*4, &word) || word != 0xa5a5a5a5u) return false;
+    return true;
```

**新增两件事：**

| 检查 | 含义 |
|---|---|
| **UCD offset 512..1023（response 区）必须全 0** | 验证 `ur_prepare` 的清零真的落到了 response 区 |
| **DATA 全 4096 B 必须仍是 `0xa5a5a5a5`** | 验证数据区**没有被提前写过**（预填标记完整）|

#### 改动 B：OCS 保留值拒绝

```diff
-            if (out->ocs > 0x0f) goto poison; /* invalid/untrusted response, not completion */
+            if (out->ocs > 0x0a && out->ocs != 0x0f) goto poison; /* reserved/untrusted OCS */
```

**旧判据 `> 0x0f` 实际上永不触发**（OCS 是 8 bit，`word & 0xff` 最大 0xff，
但 UFS OCS 码 0x0b..0x0e 是保留值，旧版把它们当合法）。

**新判据正确拒绝 0x0b..0x0e，同时保留 0x0f（表示「未完成」）。**

### 1.2 `test-ufs-read-engine.c`（9275 → 9489 B，+214 B）

```diff
-                STOP_AT_FINISH, READBACK_CORRUPTED };
+                STOP_AT_FINISH, READBACK_CORRUPTED, DATA_WRITE_IGNORED, RESERVED_OCS };
```

**→ 新增的两个行为是 `DATA_WRITE_IGNORED` 与 `RESERVED_OCS`。**

**外加对应的注入与断言（`m->behavior == RESERVED_OCS` 时 `table[2] = 0x0b` 等）。**

---

## 2. 🔴 三、核对您的三条更正

### 更正 A：`STOP_AT_FINISH` 等**不是新增** ✅ 您说得对

**我先前在终端看到 Codex 的工作记录时，把 `STOP_AT_FINISH/READBACK_CORRUPTED/...`
当成了新增。实际核对：**

| 标识符 | 旧测试（9275 B）| 新测试（9489 B）|
|---|---|---|
| **`STOP_AT_FINISH`** | **4 次** | 4+ 次 |
| **`READBACK_CORRUPTED`** | **3 次** | 3+ 次 |
| `DATA_WRITE_IGNORED` | **0 次** | **新增** |
| `RESERVED_OCS` | **0 次** | **新增** |

**→ `STOP_AT_FINISH` 与 `READBACK_CORRUPTED` **旧版就有**，不是新增。**
**真正新增的是 `DATA_WRITE_IGNORED` 与 `RESERVED_OCS`。**

### 更正 B：真正新增是 **DATA/response 预填读回**与**保留 OCS 拒绝** ✅

**见 §1.1 的改动 A 与改动 B，正是这两项。**

### 更正 C：OCS 歧义**不是**单靠 DBR=0 消解 ✅ 您说得对

**🔴 我在旧文档 §6.3 里写过：**

> ~~「靠 `dbr` 位先清掉来区分（第 173 行 `if ((dbr & 1) == 0)` 才去读 OCS），
> 这个顺序是对的。」~~

**这句话不完整。实际是**三层**要求：**

| 层 | 代码 | 作用 |
|---|---|---|
| **1** | `if ((dbr & 1) == 0)` | 门铃位必须已清 |
| **2** | `if (out->ocs > 0x0a && out->ocs != 0x0f) goto poison` | **OCS 不能是保留值 0x0b..0x0e** |
| **3** | `if (out->ocs != 0x0f) { completed = true; break; }` | **OCS 必须离开 0x0f 才算完成** |

**→ 单单 DBR=0 不足：OCS 仍是 0x0f 就继续轮询（第 3 层）；
OCS 是保留值就 poison（第 2 层）。**

**🔴 追加更正（CMD-PACK-REVIEW-716 指出）：OCS 的**新鲜度**不能由 DBR0 单独建立。**

**DBR0 只说明门铃位被清了 —— 但一个**上一轮遗留**的 OCS 值（例如上次成功留下的 0）
同样可能出现在 `table[2]`，而门铃此刻也是 0。**

**真正建立新鲜度需要三者合取：**

| 要求 | 作用 |
|---|---|
| **DBR0** | 门铃位已清 |
| **OCS 离开 0x0f** | 处理器确实写了 OCS（预填是 0x0f）|
| **响应事务有效** | `ur_run` 后续校验事务类型 0x21、LUN 2、状态 0、长度与 tag |

**若只有 DBR0 而没有后两者，读到的是残留而非本次结果。**

**→ 这三条在 `ur_run` 里是**串联**的：先 DBR0，再 OCS 判据，完成后才读响应并逐字段校验。
少任一条都不构成「新鲜完成」。**

**而且完成后还要过 `ur_restore`，它首行要求 `ur_idle` —— 第四道。**

**`NO_FRESH_OCS` 这个测试行为（旧版就有）正是用来验证第 3 层：
它把 `table[2]` 保持 `0x0f`，期望 `UR_UNCERTAIN_RESULT`。**

---

## 3. 三套测试独立复跑（我的私有目录 `host-review-mine-v3/`）

| 测试 | sanitizer | build rc | run rc | stderr | 输出 |
|---|---|---|---|---|---|
| **engine** | ASan+UBSan | **0** | **0** | **0 B** | **`PASS 25502 bounded-engine checks; fault positions reads=5525 regs=49 writes=2908; no hardware IO`** |
| **engine** | MSan | **0** | **0** | **0 B** | 同上 |
| **atomic** | ASan+UBSan | **0** | **0** | **0 B** | **`PASS 40 atomic phase/publication/interleaving checks; no platform integration`** |
| **atomic** | MSan | **0** | **0** | **0 B** | 同上 |
| **contract** | ASan+UBSan | **0** | **0** | — | **`PASS 983 lease/readonly-command/range checks; no IO performed`** |

**→ 25502 / 5525 / 49 / 2908 与您给的一致；原子 40 项、契约 983 项也都过。**

### 3.1 ARM64 纯 codec 编译

| 文件 | rc | 产物 |
|---|---|---|
| `engine-arm64.c` | **0** | 4712 B |
| `contract-arm64.c` | **0** | 1560 B |
| `atomic-arm64.c` | **0** | 1184 B |

**全部 `-Wall -Wextra -Werror` 零诊断。**

---

## 4. 原子租约模型审查（`ufs_atomic_lease.h`，2281 B / `54cd7d32…`）

### 4.1 设计要点

**开篇声明（我原样引用）：**

> 「Atomic SOFTWARE phase lives in **ordinary cached kernel memory**.
> Shared-state publication is delegated to an **audited aligned-store + barrier adapter**;
> **never atomics on Device MMIO.**」

**→ 这是重要的架构选择：软件原子性在缓存内存里，MMIO 不用原子指令。**

### 4.2 三个关键操作

| 函数 | 原子操作 | 顺序 |
|---|---|---|
| `ur_atomic_claim` | `__atomic_compare_exchange_n(UNUSED → CLAIMED)` | **ACQ_REL / ACQUIRE** |
| `ur_atomic_finish(true)` | `CAS(CLAIMED → RESTORED)` | ACQ_REL / ACQUIRE |
| `ur_atomic_finish(false)` | `ur_atomic_poison` | — |
| `ur_atomic_stop` | `__atomic_fetch_or(STOP_BIT)` | ACQ_REL |

### 4.3 🔴 我核对的两个关键不变量

**① `claim` 成功后立刻检查 STOP，若已 STOP 则 poison**

```c
if (!lease->publish(lease->context, UR_CLAIMED) || ur_atomic_stopped(lease)) {
    ur_atomic_poison(lease);
    return false;
}
```

**→ 处理了「claim 与 STOP 竞态」：即使 CAS 赢了，如果 STOP 已置位，仍然 poison。**

**② `finish(true)` 失败（CAS 失败或 publish 失败）都 poison**

```c
if (!__atomic_compare_exchange_n(...)) return false;   /* ← 注意：这里直接 return false，不 poison */
if (!lease->publish(lease->context, UR_RESTORED)) {
    ur_atomic_poison(lease);
    return false;
}
```

**🔴 我发现一处不一致：CAS 失败时**直接 `return false`，不 poison**；
而 publish 失败时**会 poison**。**

**这可能是有意的（CAS 失败说明状态已被别人改成 RESTORED 或 POISONED，
不需要再 poison），但我需要指出：**

**如果 CAS 失败是因为**别人把它改成了 `UR_UNUSED`**（理论上不该发生，因为
`ur_atomic_claim` 只从 UNUSED 出发），那 `finish` 会返回 false 但不 poison，
状态会停在 `UR_UNUSED` —— 那 `ur_loader_may_use_storage` 就会允许 loader 使用存储。**

**→ 这是**理论上的**路径，需要 `ur_atomic_lease` 的使用者保证「只有 claim 者能改状态」。**
**我把它列为观察项，不是缺陷。**

### 4.4 `ur_atomic_stop` 的语义（注释很明确）

> 「STOP before claim denies future claim; **STOP after claim poisons**;
> STOP after restored is safe.」

**代码：**

```c
uint32_t old = __atomic_fetch_or(&lease->phase, UR_STOP_BIT, __ATOMIC_ACQ_REL);
if ((old & UR_PHASE_MASK) == UR_CLAIMED)
    (void)lease->publish(lease->context, UR_POISONED);
```

**→ 只有旧状态是 `CLAIMED` 时才 publish POISONED；
`RESTORED` 与 `UNUSED` 时只置 STOP_BIT。**

**这与注释一致。**

---

## 5. 我对旧文档的更正（明确记录）

**旧文档 `ufs-engine-review-716/UFS-ENGINE-REVIEW-716.md` 的 §6.3 那句需要更正：**

| 旧（不完整） | 新（正确） |
|---|---|
| ~~「靠 `dbr` 位先清掉来区分，这个顺序是对的」~~ | **三层要求：DBR 清零 + OCS 非保留值 + OCS 离开 0x0f；完成后还要过 `ur_idle`** |

**且旧文档审的是 `21c88fb5` 版本，本文件审的是 `7c2b4df4` —— 两者不能混用。**

---

## 6. 我的结论

| 项 | 判断 |
|---|---|
| **MANIFEST 9/9** | ✅ 全匹配 |
| **实际 diff 只两处** | ✅ 与您说的一致（DATA/response 预填读回 + 保留 OCS 拒绝）|
| **`STOP_AT_FINISH` 非新增** | ✅ 旧测试已有 4 次 |
| **OCS 三层判定** | ✅ 我旧文档那句已更正 |
| **三套测试** | ✅ 25502 / 40 / 983 全部独立复现 |
| **ARM64 三份** | ✅ rc=0 零诊断 |

### 我提出的一条观察

**`ur_atomic_finish(true)` 里 CAS 失败时直接 `return false` 而不 poison，
与 publish 失败时 poison 的处理不一致。**
**理论路径上若 CAS 失败因为状态被改成 `UR_UNUSED`，`finish` 返回 false 但 lease 仍是
`UR_UNUSED`，`ur_loader_may_use_storage` 会返回 true。**

**→ 需要使用者保证「只有 claim 者能改 phase」。这是使用契约，不是代码缺陷。**

### 4.5 🔴 撤回：这条「缺口」不成立（用户指出，我核实后确认）

**我原来写「CAS 失败路径既未 poison 也未覆盖」，错了。三点核实：**

#### ① 状态图无回退 `UR_UNUSED` 的 writer

**全文件只有 4 处写 `phase`：**

| 行 | 操作 | 写入值 |
|---|---|---|
| 21 | `__atomic_fetch_or` | `STOP_BIT | POISONED` |
| 28 | `compare_exchange_n` | `UNUSED → CLAIMED` |
| 41 | `compare_exchange_n` | `CLAIMED → RESTORED` |
| 55 | `__atomic_fetch_or` | `STOP_BIT` |

**`UR_UNUSED` 只出现在第 27 行的 `expected` 初值里 —— **没有任何 writer 写回它**。**

**→ 状态图是 `0 → CLAIMED → RESTORED/POISON` 加 OR STOP，**无回退路径**。**
**我假设的「CAS 失败因状态被改回 `UR_UNUSED`」在这套代码里**不可能发生**。**

**且 `hakios.mt6895_ufs_read.c` 注释明确：**

> 「Do not reset this cached software phase in init: quiesce may already have set STOP.」

**→ 平台 init 也不重置 phase。**

#### ② `ur_run` 里 `finish(true)` 失败会 `goto poison` → 再 `finish(false)`

```c
clean:
    if (!ur_restore(io, saved)) goto poison;
    if (!io->finish(io->context, true)) goto poison;   /* ← 失败即跳 poison */
    ...
poison:
    (void)io->finish(io->context, false);              /* ← 再调一次，传 false → poison */
```

**→ 即使 `finish(true)` 的 CAS 失败，紧接着 `finish(false)` 会走 `ur_atomic_poison`。**

#### ③ 40 例里 `claim→stop→finish(true)` **实际执行的就是 CAS 失败路径**

```c
init(&m); CHECK(ur_atomic_claim(&m.phase));
ur_atomic_stop(&m.phase);
CHECK(!ur_atomic_finish(&m.phase, true));
```

**实际状态追踪：**

| 步 | phase 值 | 说明 |
|---|---|---|
| `init` | `0` | UNUSED |
| `claim` | `1` | CAS 成功 → CLAIMED |
| `stop` | **`0x101`** | `fetch_or(0x100)`，旧值 `1`(CLAIMED) → `publish(POISONED)` |
| `finish(true)` | — | `expected = 1`，实际 `0x101` → **CAS 失败** |

**🔴 这条用例**确实执行了 CAS 失败分支**，且是在 phase 含 `STOP_BIT` 时。
我用 grep 找 `finish.*CAS` 零命中就断言「未覆盖」，是**方法错误** —— 覆盖不等于关键词命中。**

**→ 「缺口」这条**撤回**。该路径**有测试覆盖**，整体行为安全。**

---

### 4.6 我先前核实过的内容（保留）

**① `ur_loader_may_use_storage`（contract.h:34-37）对 `UR_UNUSED` 返回 true：**

```c
return ur_lease_valid(r) && (r->state == UR_UNUSED || r->state == UR_RESTORED);
```

**② 40 项测试里**没有**直接覆盖 `win_finish` 的 CAS 失败路径 —— 我 grep 过
`finish.*CAS` / `finish_cas` / `finish_fail`，零命中。**

**测试里 `CHECK(!ur_atomic_finish(&m.phase, true))` 的失败是**状态前提不满足**
（不是 CLAIMED）导致的，不是 CAS 竞态。**

**→ 所以这条路径**既未在代码里 poison，也未被测试覆盖**。**
**我把它列为**真实缺口**（不是理论猜测），建议要么在 CAS 失败时也 poison，
要么在测试里加一条「CAS 失败且状态变回 UNUSED」的用例。**

---

## 7. 未接入项（我确认）

| 项 | 状态 |
|---|---|
| 平台 adapter | 无 |
| **loader guard** | **尚未接入（另独立审）** |
| 真实硬件执行 | 从未 |

**`ufs_atomic_lease.h` 自述：**

> 「This is still a **host candidate, with no platform mapping or loader integration**.」

**→ 不把 v3 当作已接入，也不把旧 `21c88fb5` 审查冒充 v3。**

---

## 8. 边界

**仅主机私有目录复跑。未改候选/生产、未碰设备、未构建内核、未刷写。**

---

## 9. 平台 adapter 与 loader guard（本轮追加审查）

### 9.1 `candidate/hakios.mt6895_ufs_read.c`

| 项 | 值 |
|---|---|
| 大小 | **8858 B / 227 行** |
| SHA256 | **`bbec3f6d00500c535b6be2c118342197614c63a550a7d1aab400a72b297bc450`** |

**真实 XNU 头 ARM64 编译（我独立复跑，194 元素 argv 只改输出路径）：**

| 项 | 值 |
|---|---|
| native rc | **0** |
| stdout / stderr | **0 B / 0 B** |
| 产物 | **55440 B** / `526528606edc9dc3c00912582a9d9659…` |
| scope 原文 | 「Real XNU headers, standalone candidate compile; **not linked or installed**」|

### 9.2 四个安全属性

| # | 属性 | 证据 |
|---|---|---|
| 1 | **双重地址检查后才映射** | `ur_outside_vm` 四段 + `pmap_valid_address` 四处（含 `UR_LIST_PA`、`+31`、`UR_LEASE_PA`、`UR_DATA_PA+4095`）|
| 2 | **scratch 用 `ml_io_map_unmappable` + `VM_WIMG_WCOMB`** | 不当作普通可缓存内存 |
| 3 | **`platform_write_word` 检查 `phase == UR_CLAIMED`** | 未持租约时任何写入返回 false |
| 4 | **`platform_ring_once` 双条件** | `phase == CLAIMED` **且** `shared->state == CLAIMED` 才写门铃；**这是全文件唯一控制器 store** |

**`platform_claim` 既检查 shared DRAM 的 `state == UNUSED` 又走原子 CAS；**
**init 里 shared 不是新鲜 `UNUSED` 就直接返回不发请求 —— fail-closed。**

### 9.3 `candidate/ufs_loader_guard.h` 与测试

| 文件 | 大小 | SHA256 |
|---|---|---|
| `ufs_loader_guard.h` | **1635 B** | **`dbd626a8c2cc53ab352b5a079f19c7b34cc77862663c7bfd18fdff8ab004c445`** |
| `test-ufs-loader-guard.c` | **2184 B** | **`64b98360afa60c6e9694ba3ab95e187e265694507587f86b2fa2527e057681e1`** |

**🔴 我第一次读到的是 1103 B / 2046 B 的版本，复查时已是上述尺寸。
说明 Codex 期间改过。本文件审的是**当前**版本。**

**发布顺序正确：**

```c
r->magic = 0;              /* 先清 magic */
r->version = UR_VERSION;
r->trial = UR_TRIAL;
r->state = UR_UNUSED;
barrier(context);
r->magic = UR_MAGIC;       /* 最后写 magic —— 半写状态不会被当成有效 */
barrier(context);
```

**`ur_guard_allows_storage` 是双读比对（两次快照全等才通过）—— 能检出撕裂。**

**约束（注释原文）：**

> 「New-boot initialization only, after loader reads and before handing control to XNU.
> **Never call this to clear an outstanding/poisoned lease during return or recovery.**」

### 9.4 🔴 `logbook_boot_note` 跳转前写 flash —— 我核实了

**`logbook_boot_note()`（`mtkloader.c:1937`）在第 **2054** 行调用：**

```c
(void)save_log_to_flash((uint64_t)(uintptr_t)buf, n, 0xfeedfaceull, 0, 0, 0);
```

**调用点（第 3777 行）就在 `jump_to_kernel` **之前**：**

```c
STEP_BAND(14, 0xff00a0ffu);   /* 14: the panel is cleared, jumping to XNU */
logbook_boot_note();
out("Jumping to XNU...\n");
jump_to_kernel(kva_to_pa(entry_va), ba_pa);
```

**→ 跳转前**确实有一次 WRITE(10)**，`reason = 0xfeedface`。**

**该写走 `ufs_write` → `ufs_cmd_once` → `ufs_init()`，
而 `ufs_init` 首行 `if (ufs_ready) return 1;` —— 此时 `ufs_ready` 已为 1，不重跑初始化。**

#### 我的判断

| 项 | 判断 |
|---|---|
| 这次 WRITE 是否违反 guard | **不违反** —— 它是 loader 自己的写，发生在把控制权交给 XNU 之前 |
| 但它带来的风险 | **XNU 进入时 `UTRLDBR` / OCS 可能带这次写的残留**，而 `ur_idle()` 要求 `UTRLDBR == 0` |

**🔴 建议你重点核对：那次 `0xfeedface` 写完成后，门铃是否确定回到 0。**

**715 实测 `UTRLDBR = 0`，说明**无 guard 的配置下**那次写之后门铃是清的；
**加 guard 后要重新确认** —— 否则 `ur_idle()` 会直接失败，探针连 `prepare` 都进不去。**

---

## 10. 本轮完整复现汇总

| 项 | 结果 |
|---|---|
| engine ASan+UBSan / MSan | **25502 / 5525 / 49 / 2908** |
| atomic ASan+UBSan / MSan | **40 项** |
| contract ASan+UBSan | **983 项** |
| **adapter 真实 XNU 头 ARM64** | **rc=0，55440 B** |
| engine/contract/atomic ARM64 codec | 4712 / 1560 / 1184 B，全 rc=0 |

---

## 11. 边界

**仅主机。未改 v3、未改候选、未碰设备、未构建内核、未刷写。**
