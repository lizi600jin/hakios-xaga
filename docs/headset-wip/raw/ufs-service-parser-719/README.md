# 服务解析器 719 审查（108 合成案例）

> 2026-11-17 19:30，DSH。**只读主机审查。无设备操作、无构建、无源码编辑。**
> CMD 身份：`b5f30cf12661550e5df0febd0442231d43397f33c7e0616cf499a681b9eb8b2b`

---

## 0. 结果

| 项 | 值 |
|---|---|
| **108 合成案例** | **`{"status": "PASS", "cases": 108}`**（我私有目录独立运行）|
| **解析器** | `review-device-719.py` 16421 B |
| **测试** | `test-review-device-719.py` 11632 B |
| **具体阻塞** | **无** |

**🔴 这是**合成案例**，不是硬件证据。**

---

## 1. 复现

**复制三份到 `host-review-mine-parser719/`：**

| 文件 | 大小 | SHA256 前 16 |
|---|---|---|
| `review-device-719.py` | 16421 | `d1ee5fa65d0a4f89` |
| `test-review-device-719.py` | 11632 | `22269e0a5c2be0b6` |
| `arguments719.json` | 1348 | `eb8ebf8fb66eef39` |

**运行结果：**

```
{"status": "PASS", "cases": 108}
```

**rc=0，stderr 0 B。与 CMD 声明的 108 案例一致。**

---

## 2. 逐项审查（CMD 点名的判据）

### 2.1 ✅ 恰好 3 次请求与真实 VFS 报告

**第 68 行：**

```c
need(ids == list(range(1, len(ids) + 1)) and len(ids) <= 3, 'request IDs/budget/order')
```

**第 200 行（成功路径）：**

```c
need(successful and [number(x) for x in complete.groups()] == [3, 3, 3, 2, 1],
     'selftest total/state contradicts requests')
```

**→ 强制 `[requests, issued, delivered, control, shared] = [3, 3, 3, 2, 1]`。**

**第 16 行的 LIMITS 明确：**

> 「Three kernel VFS reads of one fixed LUN2/LBA1 snapshot; **no userland consumer proven**」

### 2.2 ✅ 跨请求保持 shared CLAIMED

**第 200 行的 `[3, 3, 3, 2, 1]` 里：**
- `control = 2` → **`US_IDLE`**（不是 ACTIVE，说明请求间已回到 IDLE）
- **`shared = 1`** → **`UR_CLAIMED`**

**→ 三次请求之间 shared 一直保持 CLAIMED，且最后才在 quiesce 时释放。**

**第 215 行的独立标签：**

```c
LEASE_HELD_BETWEEN='PASS' if successful and complete else 'NOT_PROVEN'
```

### 2.3 ✅ 每次请求的内存恢复 vs 服务租约释放

**第 58 行解析 `memory_restored=` 字段；第 81 行：**

```c
need(polls <= 500000 and (not issued or claimed) and (not restored or claimed), 'request flags/poll bound')
```

**第 217 行：**

```c
IDLE_RETURN_RELEASE='PASS' if successful and complete and state == 2 else 'NOT_PROVEN'
```

**→ **`memory_restored`（每次请求）**与 **`state == 2`（租约最终释放）**是两个**独立判据**。**

**第 176-177 行还检查了矛盾：**

```c
need(all(not r['claimed'] or (r['memory_restored'] and state == 2) for r in records),
     'persisted return after unconfirmed restored request')
```

**→ 若声称 claimed 但没有 restored 证据，且 state 不是 2 → 拒绝。**

### 2.4 ✅ 13 词响应与 OCS 0 的完整求值

**第 103 行：**

```c
need(len(words) == 13 and all(re.fullmatch('[0-9a-fA-F]{8}', w) for w in words),
     'response width/word encoding')
```

**第 129-130 行：**

```c
need(restored == 1 and ocs == failures == 0 and diagnostic is not None,
     'block result lacks restored accepted response')
```

**第 134 行：**

```c
need(engine == 3 and delivered == 0, 'rejected response marked accepted')
```

**→ 拒绝响应**不得**被标记为接受。**

**第 44-48 行还解析了完整响应字段：**

```c
fields = dict(transaction_type=packet[0] & 0x3f, lun=packet[2], tag=packet[3], ...,
              data_segment_length=int.from_bytes(packet[10:12], 'big'),
              residual=int.from_bytes(packet[12:16], 'big'),
              sense_length=int.from_bytes(packet[32:34], 'big'), ehs_length=packet[8])
```

### 2.5 ✅ CRC32-only 的表述

**第 15 行 LIMITS：**

> 「**CRC32 agreement only; not full SHA256 or byte-for-byte verification**」

**→ 与我历次文档的口径一致，也与您两轮的更正一致。**

### 2.6 ✅ write-open 与 EOF 的计数器

**第 160 行：**

```c
write = single(text, r'write-open error=(-?\d+) expected=(-?\d+) requests_before=(\d+) requests_after=(\d+)', ...)
```

**第 161 行：**

```c
eof = single(text, r'vfs eof error=(-?\d+) residual=(-?\d+) requests_before=(\d+) requests_after=(\d+)', ...)
```

**第 201-202 行强制：**

```c
write_ok = write is not None and [int(x) for x in write.groups()] == [30, 30, 0, 0]
eof_ok   = eof is not None and [int(x) for x in eof.groups()] == [0, 4, 3, 3]
```

**→ **`write-open` 前后请求数都是 0**（EROFS 在计数前返回）；
**`EOF` 前后都是 3**（EOF 不增加请求）。**

**这正是 CMD 要的「write-open/EOF no-command counters」。**

### 2.7 ✅ 唯一的审计在 HLOG 末尾

**第 166-171 行：**

```c
audit = single(text, r'return-gate magic=(...) version=(\d+) trial=(\d+) state=(\d+) allowed=([01])',
               'return-gate ', True)
...
need(text.endswith(audit[0] + '\n'), 'actual return audit is not the final HLOG span')
```

**→ `text.endswith(...)` **强制**审计是 HLOG 的**最后内容**。**

**第 40 行的 `single()`：**

```c
need(len(found) <= 1 and (found or not required), 'missing/repeated ' + marker)
```

**→ **唯一性**由「最多 1 次」强制。**

### 2.8 ✅ 诊断/缺失/部分证据永不成为 PASS

**第 211-217 行的八项标签全部是**三态**：**

```c
READ_SERVICE_CAPTURE   = 'PASS' | 'INCOMPLETE'
VFS_READ_SERVICE       = 'PASS' | 'NOT_PROVEN'
READ_BLOCK_COMPARE     = 'PASS' | 'NOT_PROVEN'
WRITE_OPEN_REJECTED    = 'PASS' | 'NOT_PROVEN'
EOF_WITHOUT_COMMAND    = 'PASS' | 'NOT_PROVEN'
LEASE_HELD_BETWEEN     = 'PASS' | 'NOT_PROVEN'
LEASE_RETURN           = 'PASS' | 'NOT_BORROWED_OR_UNPROVEN'
IDLE_RETURN_RELEASE    = 'PASS' | 'NOT_PROVEN'
```

**第 232-233 行（解析失败时）：**

```c
NORMAL_RETURN='UNVERIFIED', READ_SERVICE_CAPTURE='INCOMPLETE', READ_BLOCK_COMPARE='NOT_PROVEN',
VFS_READ_SERVICE='NOT_PROVEN', LEASE_RETURN='UNVERIFIED', IDLE_RETURN_RELEASE='NOT_PROVEN',
```

**→ 解析失败时**全部降级**，不会残留任何 PASS。**

### 2.9 ✅ 早期干净失败可证租约但不证 3 读

**第 211-217 行的条件：**

| 标签 | 需要 `successful`（即 3 读完成）|
|---|---|
| `VFS_READ_SERVICE` | **是** |
| `READ_BLOCK_COMPARE` | **是** |
| `LEASE_HELD_BETWEEN` | **是** |
| **`LEASE_RETURN`** | **否** —— 只要 `state == 2 and opened` |

**→ **早期失败仍可能给 `LEASE_RETURN=PASS`，但 `VFS_READ_SERVICE` 会是 `NOT_PROVEN`** ——
正是 CMD 要的区分。**

### 2.10 ✅ 镜像与 stock 检查是分开的

**第 262 行的失败路径：**

```c
report = dict(status='INCOMPLETE_OR_REJECTED', error=str(error), limits=LIMITS)
```

**→ 解析器**只管 HLOG 内容**，不涉及镜像/stock 验证。**

---

## 3. 关于 `platform-parser-format-719.json`

**CMD 明确：**

> 「That is **fake VFS output plus an explicitly synthetic return audit**, NOT hardware evidence.」

**我核对了该文件（204 B / `52a861e2be4633ac`），它是**格式样例**，不是捕获。**

**→ 我**没有**把它当作任何证据。**

---

## 4. 我的意见

### 4.1 无具体阻塞

**解析器的八项判据都与 CMD 描述一致，三态标签的设计严格，失败路径全部降级。**

### 4.2 我认为设计得好的三处

| # | 设计 | 为什么好 |
|---|---|---|
| **1** | **`text.endswith(audit)`** | 用字符串**结尾**强制审计是最后 span，比「出现一次」更强 |
| **2** | **八项标签全三态**（`PASS`/`NOT_PROVEN`/`NOT_BORROWED`）| **没有「默认通过」的路径** |
| **3** | **`LEASE_RETURN` 不依赖 `successful`** | 精确实现了「早期失败可证租约但不证 3 读」|

### 4.3 一条提醒（不是缺陷）

**第 207 行的时序检查：**

```c
positions += [eof.start(), complete.start(), audit.start()]
need(positions == sorted(positions) and len(set(positions)) == len(positions), 'VFS/service/return chronology')
```

**→ 这依赖**日志中的字符位置**代表时间顺序。若内核日志与 loader 审计来自不同缓冲区
（例如第二 span 是 loader 在返回后追加的），**位置顺序可能与真实时序不同**。**

**但**这恰恰是设计意图** —— 第二 span 本来就该在最后，因为它是 loader 在 gate 通过后写的。**

**→ 我确认这个假设与 719 的架构一致，不构成问题。**

---

## 5. 边界

**未做**：设备操作、构建、打包、源码编辑、冻结 57 输入编辑、
镜像/GUID/pre-log 上传、GPUEB WIP staging。

**🔴 108 案例是**合成测试**，不是硬件证据。**
**解析器**尚未对真实 719 HLOG 运行过** —— 那要等 ONE DEVICE719。
