# READ CAPACITY(10) 纯 codec 720 审查

> 2026-11-17 19:58，DSH。**纯主机。无内核集成、无构建、无设备操作、未复跑 719。**
> CMD 身份：`3e95684976d276b0365fd44a3bafe5a6468ebaa9ec59c6cc25612364b73e3654`

---

## 0. 结果

| 项 | 值 |
|---|---|
| **MANIFEST 7 项** | **7/7 全匹配** |
| **23295 检查** | **精确复现**（ASan+UBSan / MSan 各一次）|
| **两种 sanitizer** | **rc=0 / rc=0，stderr 0 B** |
| **freestanding ARM64** | **rc=0** |
| **具体阻塞** | **无** |

**🔴 这是纯主机 codec，**不是**真实 XNU 头构建，**不能**发命令或声明 IO 完成。**

---

## 1. 7 项身份（我逐项核过）

| 文件 | 字节 | SHA256 前 16 |
|---|---|---|
| `candidate/ufs_capacity_codec.h` | 4343 | `3f95d344d7d186ba` |
| `candidate/test-ufs-capacity-codec.c` | 4764 | `6b918386307b6bc2` |
| `candidate/capacity-arm64.c` | 440 | `d6edf3db94215792` |
| `DESIGN-CAPACITY720.md` | 3368 | `ae8292e5dc41504f` |
| `REVIEW-SURVEY-720.md` | 2288 | `2db340b0d6f82be1` |
| `run-codec-checks-720.py` | 1908 | `1253fdb3fa8c2309` |
| `host-checks/codec-v1/result.json` | 2941 | `2a99c8a3125c3c0f` |

**复制到私有目录后补齐 7/7 再复跑。**

---

## 2. 复现

```
asan build rc=0
asan run   rc=0  PASS 23295 capacity-codec checks; pure host, no IO
msan build rc=0
msan run   rc=0  PASS 23295 capacity-codec checks; pure host, no IO
freestanding ARM64 rc=0
```

**→ 与 CMD 声明的 23295 / 全 rc=0 / stderr 0 完全一致。**

---

## 3. 80 字节白名单（`uc_command_allowed`）

### 3.1 全字节比较

```c
static inline bool uc_command_allowed(const struct uc_command *actual)
{
    struct uc_command expected;
    if (!actual || !uc_make_capacity(2,&expected)) return false;
    for (size_t i=0; i<sizeof(expected); i++)
        if (((const uint8_t *)actual)[i] != ((const uint8_t *)&expected)[i]) return false;
    return true;
}
```

**→ **80 字节逐一比较**，不是抽查字段。**

### 3.2 构造的关键值

| 项 | 值 | 依据 |
|---|---|---|
| UTRD[0] | **`0x14000000`** | UFS command, data in |
| UTRD[2] | **`0x0f`** | 无效 OCS 预置 |
| UTRD[4]/[5] | **`0x60101000`** / 0 | UCD 基址 |
| UTRD[6] | **`0x00800080`** | response offset 512, length 512 |
| UTRD[7] | **`0x01000001`** | PRDT offset 1024, 1 entry |
| upiu[0]/[1]/[2]/[3] | **`0x01` / `0x40` / 2 / 0** | cmd / flags / **LUN2** / **tag0** |
| **upiu[15]** | **8** | **expected transfer length** |
| **upiu[16]** | **`0x25`** | **READ CAPACITY(10)** |
| **prd[3]** | **7** | **PRDT DBC = 8-1** |

**✅ 全部符合 CMD 要求（LUN2 / tag0 / CDB 0x25 / PMI 0 / expected 8 / PRDT DBC 7）。**

### 3.3 ✅ PMI 与 LBA 保持 0

**`uc_make_capacity` 先全结构清零，之后**只**设置上面列出的字段。
所以 CDB 的其余 8 字节（含 PMI/LBA）**全为 0** ✅**

### 3.4 ✅ 无 WRITE / RC16 / reset / retry 构造器

**全文件只有 `uc_make_capacity` **一个**构造函数，opcode 固定 `0x25`。**

### 3.5 ✅ 无效 LUN / NULL 不触碰输出

**第 20-26 行测试：**

```c
for (unsigned lun=0;lun<256;lun++) {
    memset(&command,0x66,sizeof(command));before=command;
    CHECK(uc_make_capacity(lun,&command)==(lun==2));
    if (lun!=2) CHECK(memcmp(&command,&before,sizeof(command))==0);
}
```

**→ 255 个非 2 的 LUN **全部**验证「输出未变」，NULL 与 `UINT32_MAX` 同样。**

### 3.6 ✅ 80 字节穷举变异

**第 33-42 行：**

```c
for (size_t i=0;i<sizeof(command);i++) {
    uint8_t *byte=(uint8_t *)&command;
    uint8_t saved=byte[i];
    for (unsigned value=0;value<256;value++) {
        byte[i]=(uint8_t)value;
        CHECK(uc_command_allowed(&command)==(value==saved));
    }
    byte[i]=saved;
}
```

**→ **80 字节 × 256 值 = 20480 次**，每次要求「只有原值能通过」。
**任何不同 opcode / WRITE / RC16 / 不同 LUN / 更大传输 / 不同布局都会被拒 ✅**

---

## 4. 几何解码（`uc_decode_capacity`）

### 4.1 两个大端 32 位

```c
uint32_t last=uc_be32(data), block_bytes=uc_be32(data+4);
```

**→ byte 0..3 = lastLBA，byte 4..7 = 块长，都是大端 ✅**

### 4.2 宽度加宽与乘积

```c
uint64_t count=(uint64_t)last+1;
/* last<=UINT32_MAX-1, block_bytes<=UINT32_MAX, so product fits uint64_t. */
*out=(struct uc_geometry){last,block_bytes,count,count*block_bytes};
```

**→ `count` 与 `capacity_bytes` 都用 **`uint64_t`**，**溢出注释正确**：
`last ≤ 2^32-2`、`block_bytes ≤ 2^32-1`，乘积 ≤ 约 2^64 ✅**

### 4.3 ✅ `0xffffffff` = 饱和，不是无介质

```c
if (last == UINT32_MAX) return UC_RC10_SATURATED;
```

**🔴 与您上轮更正**完全一致**：返回专用枚举，**不判无介质、不回退、不自动 RC16**。**

### 4.4 ✅ 非零块长独立观测

```c
if (!block_bytes) return UC_ZERO_BLOCK_BYTES;
```

**→ 零块长单独判为无效；**任何非零块长（含 512/520）都可解析** ✅**

### 4.5 ✅ 非 OK 不触碰输出

**测试第 9-16 行的 `unchanged()` 辅助：**

```c
struct uc_geometry before,out;
memset(&before,0x67,sizeof(before));out=before;
CHECK(uc_decode_capacity(data,bytes,&out)==expected);
CHECK(memcmp(&out,&before,sizeof(out))==0);   /* 输出必须原封不动 */
```

**→ 四种非 OK 路径（BAD_ARGUMENT / SATURATED / ZERO_BLOCK_BYTES）都验证输出未变 ✅**

### 4.6 ✅ Android 比较是独立的

```c
static inline bool uc_matches_android(const struct uc_geometry *geometry)
{
    return geometry && geometry->logical_block_bytes == UC_ANDROID_BLOCK_BYTES &&
           geometry->block_count == (uint64_t)geometry->last_lba+1 &&
           geometry->block_count == UC_ANDROID_CAPACITY_BYTES/UC_ANDROID_BLOCK_BYTES &&
           geometry->capacity_bytes == UC_ANDROID_CAPACITY_BYTES;
}
```

**→ 这是**独立谓词**，不参与 `uc_decode_capacity` 的返回。**
**不同几何仍能解析，只是 `uc_matches_android` 返回 false ✅**

### 4.7 测试验证的几何组合

**`last_values` × `block_values` = 6 × 6 = 36 组**，含：
- `last` = 0、1、31240190、**31240191**、31240192、`UINT32_MAX-1`
- `block` = 1、**512**、**520**、**4096**、8192、`UINT32_MAX`

**每组验证 `count/capacity/block_count` 与 `uc_matches_android` 的正确性 ✅**

---

## 5. 我的意见

### 5.1 无具体阻塞

**7/7 身份、23295 复现、80 字节穷举、几何解码、饱和语义全部符合 CMD 要求。**

### 5.2 三处设计得好的

| # | 设计 | 为什么好 |
|---|---|---|
| **1** | **80 字节 × 256 值穷举** | 不是抽查几个 opcode，**每一个字节位置都验过** |
| **2** | **`unchanged()` 强制非 OK 输出不变** | 防止「失败时留下半解析结果」这个经典坑 |
| **3** | **`uc_matches_android` 是独立谓词** | **几何解码与 Android 比较彻底分离**，不同几何不会变成错误 |

### 5.3 一条我必须强调的边界

**🔴 这个 8 字节解码器**不能**证明：**

| 项 | 为什么 |
|---|---|
| **响应/OCS** | codec 只解析 8 字节 payload，**不构造也不检查 UPIU 响应** |
| **DMA 可见性** | 没有任何内存屏障或 cache 操作 |
| **队列已恢复** | 没有租约或 restore 逻辑 |
| **所有权** | 没有 lease 断言 |
| **设备几何** | **没有发过任何命令** |
| **块设备** | 不注册任何设备 |

**→ 按 CMD：「Owned scratch/tail/restore must be defined and fault-tested in a new
engine candidate before any kernel or hardware work.」**

### 5.4 一条源码层面的观察

**设计文档说「Owned scratch stays 4096 bytes: future integration **may** prefill/read back
all bytes, transfer 8, verify the untransferred tail as a canary, then restore ALL 4096」。**

**✅ 这与您上轮的更正（整 4 KiB 预填并非不允许）**完全一致**。**

**但 codec **本身不实现**这些 —— 它只有构造与解码，没有 scratch 管理。**

**→ 这是**正确的范围划分**，不是缺失。**

---

## 6. 边界

**未做**：内核/loader 集成、构建、镜像、设备操作、719 复跑、
旧源码/BUILD/白名单修改、GPUEB WIP staging。

**🔴 纯主机 codec，未在真实 XNU 头下构建（freestanding ARM64 只是语法级验证）。**
**🔴 8 字节解码**不是** IO 完成证据。**
