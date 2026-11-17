# RC10 查询引擎 720 审查

> 2026-11-17 20:10，DSH。**纯主机。无平台/内核/loader 集成、无构建、无设备操作。**
> CMD 身份：`96306c3b50df9fe886ed78e81bd542fadced65a5815bb96cb7a51e7703780688`

**codec `3f95d344` 未改；719 文件未改；support 三份是 719 的原字节。**

---

## 0. 结果

| 项 | 值 |
|---|---|
| **MANIFEST 17 项** | **17/17 全匹配** |
| **65955732 检查** | **精确复现** |
| **两种 sanitizer** | **rc=0 / rc=0，stderr 0 B** |
| **ARM64 产物** | **rc=0，7096 B** ✅ 与声明一致 |
| **具体阻塞** | **无** |

---

## 1. 身份与前置

### 1.1 MANIFEST 17/17

**`status: FROZEN_HOST_ONLY_QUERY_ENGINE_V2`**

| 类 | 文件 |
|---|---|
| **候选** | `ufs_capacity_codec.h`(4343)、`test-ufs-capacity-codec.c`(4764)、`capacity-arm64.c`(440)、`ufs_capacity_engine.h`(6640)、`test-ufs-capacity-engine.c`(8004)、`capacity-engine-arm64.c`(183) |
| **support（719 原字节）** | `ufs_read_contract.h`(4684)、`ufs_read_engine.h`(11027)、`ufs_service_model.h`(6628) |
| **设计/记录** | `DESIGN-CAPACITY720.md`、`DESIGN-ENGINE720.md`、`REVIEW-SURVEY-720.md`、`MANIFEST-CODEC720.json` |
| **runner** | `run-codec-checks-720.py`、`run-engine-checks-720-v2.py` |
| **结果** | `host-checks/codec-v1/result.json`、`host-checks/engine-v2/result.json` |

### 1.2 我按 CMD 用了两个独立目录

| 目录 | 内容 |
|---|---|
| **输入快照** | 20 文件，含完整 manifest 17 项（**17/17 复核通过**）+ CMD |
| **运行目录** | **10 文件**：`candidate/` + `support/` + `run-engine-checks-720-v2.py`，**无 host-checks** ✅ |

**→ 根的报告与快照**未被覆盖**。**

---

## 2. 复现

```
asan build rc=0
asan run   rc=0  PASS 65955732 capacity-engine checks;
                  reads=6554 regs=49 writes=2908 partial=13/2/1022; model only
msan build rc=0
msan run   rc=0  PASS 65955732 capacity-engine checks;
                  reads=6554 regs=49 writes=2908 partial=13/2/1022; model only
freestanding ARM64 rc=0   (capacity-engine-arm64.o = 7096 B)
```

**→ 与 CMD 声明逐项一致：65955732 / reads 6554 / regs 49 / writes 2908 / partial 13·2·1022 / ARM64 7096 B。**

**🔴 65955732 是**含逐 IO 断言的总检查数**，**不是独立案例数**。**
**CMD 点明的 9511 故障注入与 1022 尾部破坏是其中的维度，不能读成「9511 个独立测试」。**

---

## 3. 固定构造与白名单

### 3.1 ✅ 80 字节读回白名单

**`uc_readback`（第 25 行起）复用 codec 的 `uc_command_allowed` —— **80 字节全等比较**。**

### 3.2 ✅ 构造复用 codec，未改

```c
if (!uc_make_capacity(2,&capacity)) return;
for (size_t i=0;i<sizeof(command);i++) ((uint8_t *)&command)[i]=((const uint8_t *)&capacity)[i];
```

**→ opcode `0x25`、expected **8**、PRDT DBC **7**、LUN2、tag0、PMI 0 —— 全部来自未改的 codec ✅**

### 3.3 ✅ `ur_run` 被丢弃但**从不调用**

```c
(void)ur_run; /* imported sealed READ10 entry is intentionally never called */
```

**→ 我用 grep 核对了整个函数体：**没有任何 `ur_run(` 调用表达式**，
只有这一行 `(void)ur_run;` 的引用消除。**✅ 符合 CMD 要求。**

### 3.4 ✅ 回调集与 719 相同

**引擎调用的是 `ur_idle` / `ur_copy_from` / `ur_same` / `ur_previous_read` /
`ur_prepare` / `ur_restore` / `ur_response_failures` —— **全部来自 719 原字节的 support**，
**控制器回调 `io->*` 一个都没改**。**

---

## 4. 失败语义（我逐条核对）

| CMD 要求 | 实现 | |
|---|---|---|
| **ring 歧义** | `out->issued=true` 在 `ring_once` **之前**；失败 `goto poison` | ✅ |
| **超时** | `while (polls<max && elapsed<timeout)`，超时后 `!completed → poison` | ✅ |
| **STOP** | 循环内 `io->stopped()` 即 `poison` | ✅ |
| **保留 OCS** | `if (out->ocs>0x0a && out->ocs!=0x0f) goto poison` | ✅ |
| **不确定 DMA 不恢复** | `poison` 走 `finish(false)`，**不调 `ur_restore`** | ✅ |
| **clean 路径恢复整三块** | `clean` 先 `ur_restore(io,saved)` 再 `finish(true)` | ✅ |
| **`finish(true)` 是最后回调** | 之后只有 `out->restored=true` 与解码（纯 CPU） | ✅ |

### 4.1 ✅ 解码在 `finish(true)` 之后

```c
clean:
    if (!ur_restore(io,saved)) goto poison;
    if (!io->finish(io->context,true)) goto poison;
    out->restored=true;out->query_valid=out->result==UC_QUERY_OK;
    if (out->query_valid) {
        out->decode=uc_decode_capacity(out->raw,UC_DATA_BYTES,&out->geometry);
        ...
    }
```

**→ `uc_decode_capacity` 与 `uc_matches_android` 都是**纯内存计算**，
**在最后一个回调之后执行** ✅**

---

## 5. 响应门与掩码

### 5.1 ✅ 完整 13 词 + OCS 0

```c
for (unsigned i=0;i<UR_RESPONSE_WORDS;i++) { ... out->response_valid|=1u<<i; }
if (out->ocs!=0) goto clean;
out->response_failures=ur_response_failures(out->response);
```

### 5.2 ✅ raw 8 的三组掩码

| 掩码 | 语义 |
|---|---|
| `raw_attempted` | 尝试读过 |
| `raw_words_valid` | 成功读到 |
| **`raw_words_changed`** | **值 ≠ `0xa5a5a5a5` 预填** |

### 5.3 ✅ 全部 1022 个尾部词都采样

```c
for (unsigned i=UC_DATA_BYTES/4;i<UR_BLOCK_BYTES/4;i++) {
    if (!io->read_word(...)) goto clean;
    out->tail_checked_words++;
    if (word!=0xa5a5a5a5u && out->tail_first_bad==UINT32_MAX) out->tail_first_bad=i*4;
}
```

**`(4096-8)/4 = 1022` 个词全部采样 ✓**

### 5.4 🔴 `DATA_UNCHANGED` 是**保守假阴性**

```c
/* Conservative observation rule: a word identical to prefill is not proven fresh.
 * A legitimate equal-valued word remains raw diagnostic data, not accepted geometry. */
if (out->raw_words_changed!=3) { out->result=UC_QUERY_DATA_UNCHANGED;goto clean; }
```

**→ 若设备恰好返回 `0xa5a5a5a5` 这个值，会被判为「数据未变」而拒绝几何。**

**🔴 这是**有意设计的保守假阴性**，不是缺陷 —— 它宁可拒绝也不伪造几何 ✅**

### 5.5 ✅ 尾部 canary 的局限已写明

**CMD 要求「Tail canary is not proof against out-of-region writes or a dishonest/stale provider」。**

**源码里 `tail_first_bad` 只检测**是否被改写**，不能证明**未被越界写过** ——
**这一局限我在文档 §7 记录，源码本身无法弥补。**

---

## 6. 查询接受与解码状态分离

### 6.1 三层是独立的

| 层 | 字段 |
|---|---|
| **查询结果** | `out->result`（`UC_QUERY_OK` 等）|
| **解码状态** | `out->decode`（`UC_DECODE_OK` / `UC_RC10_SATURATED` / `UC_ZERO_BLOCK_BYTES`）|
| **Android 比较** | `out->android_reference_matches` |

### 6.2 ✅ 只有 `query_valid` 才解码

```c
out->query_valid=out->result==UC_QUERY_OK;
if (out->query_valid) {
    out->decode=uc_decode_capacity(...);
    out->android_reference_matches=out->decode==UC_DECODE_OK && uc_matches_android(&out->geometry);
}
```

**→ **被拒/部分响应/数据异常/尾部破坏都不会产出几何** ✅**

### 6.3 ✅ 不同 512 几何仍可见且 QUERY_OK

**`uc_decode_capacity` 接受任何非零块长；`android_reference_matches` 只是**独立谓词**。**

**→ `last=31240191, block=512` 会得到 `UC_QUERY_OK` + `UC_DECODE_OK` + `android_reference_matches=false` ✅**

### 6.4 ✅ 饱和/零块长保留 raw8

**`decode=UC_RC10_SATURATED` 或 `UC_ZERO_BLOCK_BYTES` 时，
`query_valid` 仍可为 true（查询成功），`raw` 八个字节**保留**，
只是**不解码出几何** ✅**

---

## 7. 我的意见

### 7.1 无具体阻塞

**17/17、65955732 复现、白色名单、失败语义、掩码与分离全部符合 CMD。**

### 7.2 三处设计得好的

| # | 设计 | 为什么好 |
|---|---|---|
| **1** | **`ur_run` 只做 `(void)ur_run;` 引用消除** | **保持 support 原字节不改**，同时明确不调用 |
| **2** | **`DATA_UNCHANGED` 保守假阴性** | 宁可拒绝也不把「恰好等于预填」当成真数据 |
| **3** | **解码放在 `finish(true)` 之后** | 最后一个硬件回调之后才是纯 CPU，边界干净 |

### 7.3 🔴 三条我仍需提醒的（不是 720 的缺陷，是范围）

| # | 项 | 说明 |
|---|---|---|
| **1** | **provider 映射不存在** | CMD 明确「Provider mappings ... are NOT present」—— 现在只是模型 |
| **2** | **support 仍是 719 的 HOST MODEL 契约** | **不能把 `ur_read_engine.h` 当成 720 运行时包** |
| **3** | **尾部 canary 不能证明「未被越界写」** | 只证明「被读的位置未被改写」 |

### 7.4 关于初版编译失败

**CMD 要求「Initial engine-v1 compile rc1 due duplicated field is retained, with old source.
Do not hide it or claim it passed.」**

**我核对了 `host-checks` 里确实保留了 v1 的证据（`run-engine-checks-720.py` 与
`candidate/` 的 v1 源），**我没有把它当成通过**。**

**当前 v2 是修正后的源，两套 sanitizer 通过。**

---

## 8. 剩余的平台义务（供下一步）

| # | 义务 |
|---|---|
| **1** | **provider 映射与缓存原子服务组合**（720 cookie、loader guard）需在平台侧实现 |
| **2** | **owned scratch 的整块预填/读回/恢复**需在真实适配器里验证 |
| **3** | **尾部 canary 的语义**需明确它只证明「读到的位置未被改写」 |
| **4** | **不调用旧 loader 的 `ufs_capacity()`** |
| **5** | **无注册、无 reset、无 RC16、无 retry** |

---

## 9. 边界

**未做**：平台/内核/loader 集成、构建、镜像、设备操作、719 复跑、
候选编辑、GPUEB WIP staging。

**🔴 纯主机模型。freestanding ARM64 只是语法级验证。**
**🔴 8 字节解码**不是** IO 完成证据；65955732 是**含逐 IO 断言的总数**，不是独立案例数。**
