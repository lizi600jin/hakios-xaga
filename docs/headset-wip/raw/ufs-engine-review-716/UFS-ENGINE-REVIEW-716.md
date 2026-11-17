# UFS 读引擎候选审查（716）

> 2026-11-17 13:30，DSH。**仅主机。未改候选/生产/设备。**
> 对象：`candidate/ufs_read_engine.h`、`ufs_read_contract.h` 及两套测试。

---

## 0. 身份与复跑

| 文件 | 大小 | SHA256 |
|---|---|---|
| **`ufs_read_engine.h`** | **9307 B / 210 行** | **`21c88fb5c2405af1903fc82d18c76d73642feaaa44a4750849895e80b9b41cf5`** |
| `ufs_read_contract.h` | 4684 B / 123 行 | `0012fb4f4fa84a82a9616cf3f61c61f10d6268ac13262bbb37e740219f45f3ce` |
| `test-ufs-read-engine.c` | 9275 B | — |
| `test-ufs-read-contract.c` | 4492 B | — |

### 独立复跑结果（我自己的目录 `host-review-mine/`）

| sanitizer | build rc | run rc | stderr | 输出 |
|---|---|---|---|---|
| **ASan+UBSan** | **0** | **0** | **0 B** | `PASS 22039 bounded-engine checks; fault positions reads=4373 regs=49 writes=2908; no hardware IO` |
| **MSan** | **0** | **0** | **0 B** | 同上 |

**→ 与 `host-checks/engine-result.json` 的 22039 / 4373 / 49 / 2908 精确一致。**

---

## 1. 🔴 ring 失败是否按「可能已提交」处理 —— **是**

### 1.1 代码证据

**`ur_io` 的注释（第 26 行）：**

```c
bool (*ring_once)(void *); /* exactly UTRLDBR=1; false is ambiguous, not "no write" */
```

**`ur_run` 第 168 行：**

```c
out->issued = true; /* failure may mean the MMIO store was already posted */
if (!io->ring_once(io->context)) goto poison;
```

**🔴 关键：`out->issued = true` 在 `ring_once` **调用之前**设置，
而不是看它的返回值。**

### 1.2 判断

| 项 | 结果 |
|---|---|
| 是否假定 ring 失败 = 没写 | **❌ 没有** |
| ring 失败后走哪条路 | **`goto poison`**（`UR_UNCERTAIN_RESULT`）|
| 是否尝试恢复 | **不恢复** —— 直接 `finish(false)` |

**→ 这是保守且正确的处理：既然可能已提交，就不能假装没发生。**

---

## 2. 🔴 超时 / STOP 后是否禁止恢复活 DMA 描述符 —— **是**

### 2.1 三条进入 `poison` 的路径

| 条件 | 行 | 结果 |
|---|---|---|
| **`io->stopped()`** | 157、171（轮询内）| **`goto poison`** |
| **轮询超时**（`max_polls` 用尽或 `timeout_us` 到期）| 170 | **循环退出后 `if (!completed) goto poison`** |
| **`dbr` 读失败** | 172 | `goto poison` |
| **OCS > 0x0f（无效响应）** | 176 | **`goto poison`** |

### 2.2 `poison` 分支做什么

```c
poison:
    (void)io->finish(io->context, false);
    out->result = UR_UNCERTAIN_RESULT;
    out->data_valid = false;
```

**🔴 `finish(ctx, false)` —— 第二参数是 `false`，
按 `ur_io` 的注释（第 18 行）：**

```c
bool (*finish)(void *, bool); /* true only if clean restore won against STOP */
```

**→ `false` 表示「不是干净恢复」。**

### 2.3 判断

| 项 | 结果 |
|---|---|
| 超时后是否尝试 restore 那些描述符 | **❌ 没有** |
| STOP 后是否尝试 restore | **❌ 没有** |
| 是否把这种状态标为不确定 | **✅ `UR_UNCERTAIN_RESULT`** |
| `data_valid` | **`false`** |

**→ 正确。活着的 DMA 描述符不会被假装恢复。**

### 2.4 一个我认为值得指出的细节

**`ur_restore` 第一行是 `if (!ur_idle(io)) return false;`。**

**`ur_idle` 要求 `UTRLDBR` 位为 0（`w[6] == 0`）。**

**→ 如果还有在途请求，`ur_restore` 会**拒绝**执行 —— 这是第二道保险。**

---

## 3. 🔴 每个已提交写再失败的回滚 —— **逐项检查**

### 3.1 `ur_prepare` 的写入序列（第 96-108 行）

| # | 写什么 | 失败时 |
|---|---|---|
| 1 | **`UR_UCD` 全 1664 B 清零**（416 个 word）| `return false` → `goto clean` |
| 2 | `UR_UCD` 前 32 B 写 UPIU | 同上 |
| 3 | `UR_UCD` 偏移 1024 写 4 个 PRD word | 同上 |
| 4 | **`UR_DATA` 全 4096 B 填 `0xa5a5a5a5`**（1024 个 word）| 同上 |
| 5 | `UR_TABLE` 8 个 word（UTRD）| 同上 |

### 3.2 关键：**`clean` 标签在 `ur_prepare` 失败时也会执行**

```c
out->result = UR_PREPARE_FAILED;
if (!ur_prepare(io, &command)) goto clean;      /* ← 半途失败也走 clean */
```

**而 `clean:` 里是：**

```c
clean:
    if (!ur_restore(io, saved)) goto poison;
```

### 3.3 🔴 这里有一个我必须指出的**依赖**

**`ur_restore` 回滚的是 `saved` —— 而 `saved` 是 `ur_prepare` **之前**抓的快照。**

**问题：如果 `ur_prepare` 在第 1 步（UCD 清零）就失败，
那 UCD 可能只清了一部分 —— 而 `ur_restore` 会把**整个** UCD 写回 `saved`。**

**这实际上是**更强的**恢复：它不区分「写到哪一步」，直接整块写回原值。**

**→ 所以这个设计对「每个已提交写再失败」是**充分的**：
不需要逐步回滚，因为最后统一整块恢复。**

**但有一个前提成立才有效：**

| 前提 | 是否成立 |
|---|---|
| `saved` 是完整的原始快照 | ✅ `ur_copy_from` 抓了 UCD(416 word) + DATA(1024 word) + TABLE(8 word) |
| 快照在 prepare 之前抓 | ✅ 第 151-153 行，早于 `claim` |
| 快照经过一致性验证 | ✅ `ur_same` 三次 + `ur_idle` 两次 |

**→ 我认为这条**设计上是满足的**，但有边界（见 §6）。**

---

## 4. 🔴 只读白名单 —— **逐字节全等，非常严格**

### 4.1 `ur_command_allowed` 的实现（contract.h:113-121）

```c
static inline bool
ur_command_allowed(const struct ur_command *actual)
{
    const struct ur_request request = { 2, 1, 1, UR_BLOCK_BYTES };
    struct ur_command expected;
    if (!actual || !ur_make_read(&request, &expected)) return false;
    for (size_t i = 0; i < sizeof(expected); i++)
        if (((const uint8_t *)actual)[i] != ((const uint8_t *)&expected)[i]) return false;
    return true;
}
```

**→ 不是「检查几个关键字段」，而是**整个结构逐字节比对**。**

### 4.2 构造的请求是固定的

**`ur_make_read`（contract.h:85-105）：**

| 字段 | 值 |
|---|---|
| **`lun`** | **必须是 2** |
| **`lba`** | **必须是 1** |
| **`blocks`** | **必须是 1** |
| **`logical_block_bytes`** | **必须是 4096** |
| `utrd[0]` | `0x14000000`（UFS command, **data in**）|
| `upiu[0]` | `0x01` |
| `upiu[1]` | `0x40` |

**声明（contract.h:82）：**

> 「**No WRITE, QUERY, task management, reset, clock or queue-base programming is constructed.**」

### 4.3 写入范围核算

| 区域 | 最大写偏移 | 边界 | 结果 |
|---|---|---|---|
| UCD | **1660** | < 1664 | ✅ |
| DATA | **4092** | < 4096 | ✅ |
| TABLE | **28** | 8 word | ✅ |
| PRD（在 UCD 内）| **1024+12=1036** | < 1664 | ✅ |

**→ 无越界。**

### 4.4 `ur_readback` 是第二道保险

**`ur_prepare` 之后立刻 `ur_readback`（第 165 行），它把写进去的内容读回来，
再过一遍 `ur_command_allowed`。**

**→ 「写进去的」和「控制器会看到的」必须都符合白名单。**

---

## 5. 🔴 恢复完才许可 loader —— **是**

### 5.1 顺序

```c
clean:
    if (!ur_restore(io, saved)) goto poison;
    if (!io->finish(io->context, true)) goto poison;   /* ← 只有 restore 成功才 true */
    out->restored = true;
```

**→ `finish(ctx, true)` **只在 `ur_restore` 返回 true 之后**才调用。**

### 5.2 `ur_restore` 自己检查什么

```c
if (!ur_idle(io)) return false;                      /* 无在途请求 */
... 写回 UCD / DATA / TABLE ...
io->barrier(io->context);
return ur_same(...UCD...) && ur_same(...DATA...) && ur_same(...TABLE...) && ur_idle(io);
```

**→ 回滚后要**重新校验三块内容逐 word 相等**，再确认 `idle`。**

### 5.3 contract 层的配合

**`ur_loader_may_use_storage`（contract.h:38-42）：**

```c
return ur_lease_valid(r) && (r->state == UR_UNUSED || r->state == UR_RESTORED);
```

**→ loader 只有看到 `UR_UNUSED` 或 `UR_RESTORED` 才被允许使用存储。**

### 5.4 判断

| 项 | 结果 |
|---|---|
| loader 许可是否依赖 restore 完成 | **✅ 是**（双条件：`finish(true)` + lease `UR_RESTORED`）|
| restore 失败会怎样 | **`goto poison` → `UR_UNCERTAIN_RESULT`，lease 不给 RESTORED** |

---

## 6. 🔴 我认为需要指出的三条

### 6.1 `saved` 快照本身依赖 `ur_copy_from` 成功 —— 而这个函数不校验内容

**`ur_copy_from`（第 59-65 行）只是逐个 `read_word` 填入。**

**如果底层 `read_word` 在某次读时返回了**陈旧或伪造**的值但仍返回 true，
`saved` 就是错的，而后续所有 restore 都以它为准。**

**→ 这不是代码缺陷，是**provider 的责任**（注释里也写了「callbacks returning false
do not model a hardware abort; integration must fail closed there」）。**

**但值得明确：`saved` 的正确性完全依赖于 adapter 的诚实性。**

### 6.2 `ur_idle` 对 `CAP` 的检查是 `(w[0] & 31) == 31`

**第 55 行：`(w[0] & 31) == 31` —— 要求 `CAP[4:0] == 31`，即 **NUTRS = 32**。**

**715 实测 `CAP = 0x3587071f`，`0x1f & 0x1f = 0x1f` ✅ 满足。**

**`VER` 要求 `== 0x200`，715 实测 `0x200` ✅。**

**→ 这条硬编码与 715 实测一致，但**如果将来遇到 NUTRS 不是 32 的控制器，`ur_idle` 会一直 false**。**
**这是有意的保守（只在已知硬件上跑），但应记明。**

### 6.3 `ocs > 0x0f` 判为 poison，`ocs == 0x0f` 表示「还在进行」

**第 176 行：`if (out->ocs > 0x0f) goto poison;`**
**第 177 行：`if (out->ocs != 0x0f) { completed = true; break; }`**

**→ 逻辑是：OCS 值 > 0x0f 属无效；== 0x0f 表示尚未完成；其余表示已完成（成功或失败）。**

**这与 `ur_make_read` 里 `c->utrd[2] = 0x0f; /* invalid OCS before submission */` 一致。**

**但注意：`utrd[2]` 是**预先写成 0x0f 的**，所以「读到 0x0f」既可能是「未完成」，
也可能是「根本没被控制器改写」。**

**→ 靠 `dbr` 位先清掉来区分（第 173 行 `if ((dbr & 1) == 0)` 才去读 OCS），
这个顺序是对的。**

---

## 7. 已知未接入项（Codex 声明 + 我确认）

| 项 | 状态 |
|---|---|
| **缓存软件原子状态 + shared 发布的竞态模型** | **Codex 正在实现** |
| **loader guard** | **尚未接入** |
| 平台 adapter | 无（`ur_io` 是纯函数指针，无实现）|
| 真实硬件执行 | **从未** |

**`ufs_read_engine.h` 第 1 行自述：**

> 「Bounded single-request engine candidate. **No platform adapter or production integration.**」

**`ur_restore` 前的注释（第 138 行）：**

> 「Provider must establish exclusive lease/mapping/return guard separately.
> This engine never turns FLAG observations into ownership.
> **Workspace must be persistent, not kernel stack.**
> callbacks returning false do not model a hardware abort; integration must fail closed there.」

---

## 8. 我的结论

| 审查要点 | 判断 |
|---|---|
| **ring 失败按可能已提交处理** | ✅ **是**（`issued=true` 在调用前，失败即 poison）|
| **超时/STOP 后禁止恢复活 DMA 描述符** | ✅ **是**（`finish(false)`，不 restore；`ur_restore` 还先要求 idle）|
| **每个已提交写再失败的回滚** | ✅ **充分**（统一整块 restore，非逐步；前提是 `saved` 可信）|
| **只读白名单** | ✅ **逐字节全等**，且写入范围无越界 |
| **恢复完才许可 loader** | ✅ **是**（`finish(true)` 在 restore 成功后；lease 需 `UR_RESTORED`）|

**22039 项检查我独立复跑通过，与声明一致。**

**三条我提出的观察（§6.1 `saved` 依赖 adapter 诚实、§6.2 NUTRS 硬编码 32、
§6.3 OCS 0x0f 的歧义靠 dbr 顺序消解）—— 都是**已知边界**，不是缺陷。**

---

## 9. 边界

**未做**：改候选/生产、接入 XNU 或 loader、碰设备、构建内核、刷写。

**这套引擎**从未在真实硬件上执行**，22039 项全是主机模型。
