# UFS 响应 718：主机审查与隔离构建

> 2026-11-17 14:47，DSH。**仅主机。未打包、未碰设备、未改候选。**
> CMD 身份：`b0303b40997a037c60796404aac1611a80fd326518b0c53bc674d2ccd47ec3cf`

**🔴 716/717 仍为 NOT_PROVEN。718 **明示修改**响应判据 —— 这是本轮审查的核心。**

---

## 0. 结果

| 项 | 值 |
|---|---|
| **MANIFEST** | **51/51 全匹配** |
| **kernel inputs** | **5210 / 5210 OK** |
| **diff vs 717** | **3 变化 + 0 新增 + 0 删除** ✅ |
| **engine** | **32513 / reads=5530 / regs=49 / writes=2908**（两 sanitizer 均 rc=0、stderr 空）|
| **make native rc** | **2** |
| **judge native rc** | **0** |
| **新内核** | **15338864 B** |
| **SHA256** | **`60bd6b195c2ab65ab0c5c1cb519fe3cf5af9ad64c351485ccbf59aa899fd7cfe`** |

---

## 1. 🔴 术语更正（用户指出，我核实后确认）

**我此前写「`byte8` = flags 高位区（含 EHS length）」—— **这是错的**。**

**`utp_upiu_header` 的实际字段顺序（Linux `scsi_bsg_ufs.h` 源码佐证）：**

| 字节 | 字段 |
|---|---|
| 0 | `transaction_code` |
| **1** | **`flags`** ← flags 在这里 |
| 2 | `lun` |
| 3 | `task_tag` |
| 4 | `iid` / `command_set_type` |
| 5 | `tm_function` / `query_function` |
| 6 | `response` |
| 7 | `status` |
| **8** | **`ehs_length`** ← **独立的 EhsLen，total EHS length** |
| 9 | `device_information` |
| 10..11 | `data_segment_length` |

**→ 候选判断 `r[8] != 0` **正是**拒绝未实现的扩展头（EHS），
而不是「flags 高位」的判断。**

**原始 717 的 `byte8 = 0`，说明当时没有 EHS。**

---

## 2. 三处 diff（我逐行核对）

### 2.1 `ufs_read_engine.h`

**新常量：**

```c
#define UR_RESPONSE_BYTES 52u
#define UR_RESPONSE_WORDS (UR_RESPONSE_BYTES / 4u)      /* = 13 */
#define UR_RESPONSE_VALID_MASK ((1u << UR_RESPONSE_WORDS) - 1u)  /* = 0x1fff */
```

**新判据 `ur_response_failures`：**

```c
uint32_t segment = (uint32_t)r[10] << 8 | r[11];
uint32_t sense   = (uint32_t)r[32] << 8 | r[33];
return ((r[0] & 0x3f) != 0x21 ? 1u : 0u) |      /* bit0 transaction */
       (r[2] != 2 ? 2u : 0u) |                   /* bit1 LUN */
       (r[3] != 0 ? 4u : 0u) |                   /* bit2 tag */
       (r[6] != 0 ? 8u : 0u) |                   /* bit3 response */
       ((r[7] & 0x3f) != 0 ? 16u : 0u) |         /* bit4 status */
       (segment != 0 && segment != 20 ? 32u : 0u) |        /* bit5 段长 */
       (ur_be32(r+12) != 0 ? 64u : 0u) |                   /* bit6 residual */
       (segment == 20 && sense > 18 ? 128u : 0u) |         /* bit7 sense 超长 */
       (r[8] != 0 ? 256u : 0u);                            /* bit8 EHS */
```

**收集改为 13 词：**

```c
for (unsigned i = 0; i < UR_RESPONSE_WORDS; i++) { ... }
```

### 2.2 `hakios.mt6895_ufs_read.c`

**打印守卫改为 `UR_RESPONSE_VALID_MASK`（`0x1fff`），循环改为 13 词。**

### 2.3 `ufs_read_contract.h`

**只改 `UR_TRIAL 717u -> 718u`。**

---

## 3. 🔴 判据正确性核对

### 3.1 常量核算

| 项 | 值 | 核对 |
|---|---|---|
| `UR_RESPONSE_BYTES` | 52 | 32 header + 2 sense_len + 18 sense = **52** ✅ |
| `UR_RESPONSE_WORDS` | **13** | 52 / 4 ✅ |
| `UR_RESPONSE_VALID_MASK` | **`0x1fff`** | (1<<13)-1 ✅ |

### 3.2 偏移与端序

| 字段 | 代码 | 语义 | 核对 |
|---|---|---|---|
| **segment** | `r[10] << 8 \| r[11]` | 大端 16 位 | ✅ |
| **sense** | `r[32] << 8 \| r[33]` | 大端 16 位 | ✅ |
| **residual** | `ur_be32(r+12)` | 大端 32 位 | ✅ |
| **EHS** | `r[8]` | 独立字段 | ✅ |

### 3.3 数组边界安全

**`response[52]`，索引 32/33 与 `ur_be32(r+12)`（读到 15）**都在界内** ✅**

### 3.4 response 区容得下

**UCD 的 response 从 offset 512 起、长 512 字节；13 词 = 52 字节，
`512+52 = 564 <= 1024`，在 UCD(1664) 内 ✅**

### 3.5 未放宽的字段（我逐项比对 717）

| 项 | 状态 |
|---|---|
| bit0..bit4（transaction/LUN/tag/response/status）| **完全未变** |
| bit6（residual）| **完全未变** |
| `OCS != 0` 仍**在掩码计算之前** `goto clean` | ✅ 不变 |
| **白名单** `ur_command_allowed` | 与 717 相同（grep 计数一致）|
| **`0x58` 只读、`ring_once` 唯一写** | ✅ 不变 |
| `max_polls` / 超时 | ✅ 不变 |
| restore / readback / lease / STOP | ✅ 不变 |

### 3.6 「无基于 data-segment 的磁盘长度调整」

**`ur_make_read` 仍固定 `blocks=1, logical_block_bytes=4096` —— 我没看到任何按 segment 调整传输长度的代码 ✅**

---

## 4. 测试覆盖（我核对了源码）

### 4.1 第 274 行的关键断言

```c
CHECK(partial_responses == UR_RESPONSE_WORDS && partial_data == UR_BLOCK_BYTES/4);
```

**→ **13 个响应词** + **1024 个 DATA 词** 的部分失败全测。**

### 4.2 构造用例（第 96-112 行）

| behavior | 注入 |
|---|---|
| `UNSUPPORTED_SEGMENT` | `response[11] = 22`（非 0/20）|
| `SENSE_EMPTY` / `SENSE_FULL` | `response[11] = 20` |
| `SENSE_FULL` | `response[33] = 18` + `response[34] = 0x70` |
| **`TOO_LONG_SENSE`** | **`response[33] = 19`**（超 18）|
| **`EXTENDED_HEADER`** | **`response[8] = 1`**（EHS 非零）|

### 4.3 断言（第 237-250 行）

```c
if (byte == 8) CHECK(ur_response_failures(mutated) == 256u);        /* EHS */
else CHECK((ur_response_failures(mutated) == 0) == old_accepted);   /* 其余位等价 */
...
CHECK(ur_response_failures(response) == (lengths[i] == 0 || lengths[i] == 20 ? 0u : 32u));
CHECK(ur_response_failures(response) == (lengths[i] <= 18 ? 0u : 128u));
```

**→ sense 长度 0/18 正例、19 反例、段长 0/20 正例、其他反例，都有断言。**

---

## 5. 独立复跑

| sanitizer | build rc | run rc | stderr | 输出 |
|---|---|---|---|---|
| **ASan+UBSan** | **0** | **0** | **0 B** | **`PASS 32513 bounded-engine checks; reads=5530 regs=49 writes=2908`** |
| **MSan** | **0** | **0** | **0 B** | 同上 |

**→ 与 CMD 声明一致。**

**未变的 atomic(40)/guard(23) 未重复跑 —— 无具体疑虑。**

---

## 6. 构建

| 项 | 值 |
|---|---|
| 命令 | `XNU_SRC=<绝对> ./xnu-build.sh -k -j8` |
| **make native rc** | **2** |
| 编译错误 | **0** |
| undefined | **0** |
| `CC loader_return.o` / `CC hakios.mt6895_ufs_read.o` | ✅ |
| **judge native rc** | **0** |

**🔴 make rc=2 与 judge rc=0 分开记录。**

---

## 7. 内核与编入

**15338864 B / `60bd6b195c2ab65ab0c5c1cb519fe3cf5af9ad64c351485ccbf59aa899fd7cfe`**

**15 条 `mt6895-ufs-read` 消息全部编入，符号 `_init` / `_quiesce` 都在。**

**🔴 字符串只证明编入，不是 IO 证据。**

---

## 8. 构建后复查与守卫

| 项 | 结果 |
|---|---|
| 5210 输入 | **rc=0，全 OK** |
| BUILD 外文件数 | 5210 |
| **51 候选/loader 文件** | **51/51 未变** ✅ |

| 守卫 | 值 |
|---|---|
| stock | `c5f8e7bd` / md5 `a63b2e35` ✅ |
| shared / wrapper | `67f9f933` / `95247bb0` ✅ |
| **716 kernel** | **`c7f2f322`** 未写 ✅ |
| **717 kernel** | **`07b016fe`** 未写 ✅ |
| 生产 headset | `8c0cf1c2` ✅ |

---

## 9. 我的意见

### 9.1 判据修改是**有依据的**，不是放宽

**依据链：**

| 环节 | 依据 |
|---|---|
| DataSeg ≠ DMA 长度 | EDK2 `UfsExecScsiCmds` 独立处理 sense/status/residual |
| 20 字节是**固定 sense 容器** | EDK2 `UTP_RESPONSE_UPIU` 对 UFS 4.0 Table 10.15 |
| 非零 sense 容器不等于 IO 失败 | EDK2 与 Linux 都如此处理 |
| **EHS 拒绝**是**新增保守限制** | 因为 EHS 之后的数据位置未实现 |

**→ 这不是「为了让 717 通过而放宽」，而是**按协议实义**重写了这一段 ——
**而且新增了 bit7/bit8 两个**更严**的限制。**

### 9.2 一条观察项（承接 717）

**`failures=0` 仍有两种含义：全部通过（`valid=0x1fff`）或根本没计算（`valid!=0x1fff`）。**

**平台打印已用 `UR_RESPONSE_VALID_MASK` 守卫，但 `failures=%#x` 本身无前缀提示。**

**→ 运行时务必同时看 `valid` 与 `failures`。**

### 9.3 一处值得记录的边界

**sense 内容**只是原样暴露**，没有解析 —— 文档明确说「Sense content is exposed raw;
SCSI status remains the command-status criterion」。**

**→ 若 718 采到 sense 数据，那 18 字节会出现在第 33/34 词的打印里，但**不做语义解释**。**

---

## 10. 边界

**未做**：打包、刷写、设备查询、生产编辑、旧 BUILD/产物覆盖、
GPUEB、clock/power/reset/UIC/watchdog/OEM expdb。

**🔴 718 的 payload 尚未在硬件上观测。CRC32 一致不等于字节相等或 SHA256。**
