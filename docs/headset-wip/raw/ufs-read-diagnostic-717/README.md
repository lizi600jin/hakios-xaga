# UFS 诊断 717：主机审查与隔离构建

> 2026-11-17 14:27，DSH。**仅主机。未打包、未碰设备、未改候选。**
> CMD 身份：`8f3bad27ad4e473ac90a8da421e1eeac740b466100836260b4f6e7efb2373adb`

**🔴 716 仍为 NOT_PROVEN。本轮诊断**不改变准入**。**

---

## 0. 结果

| 项 | 值 |
|---|---|
| **MANIFEST** | **51/51 全匹配** |
| **kernel inputs** | **5210 / 5210 OK，0 FAILED** |
| **diff vs 716** | **3 变化 + 0 新增 + 0 删除** ✅ 与声明一致 |
| **engine 检查** | **32384**（ASan+UBSan / MSan 均 rc=0、stderr 空）|
| **make native rc** | **2** |
| **judge native rc** | **0** |
| **新内核** | **15338864 B** |
| **SHA256** | **`07b016feb866c4347d72a3915ef2dce15045bfa2d0628d3d1f9eb5ff385506d6`** |

---

## 1. 三个文件的 diff（我逐行核对）

### 1.1 `ufs_read_engine.h`

**新增输出字段：**

```c
uint32_t response_attempted, response_valid, response_failures;
bool diagnostic_valid;
uint32_t diagnostic_crc32;
```

**新增 `ur_response_failures()` —— 七位掩码，逐项对应 716 谓词：**

| Bit | 条件 |
|---|---|
| 0 | `r[0] & 0x3f != 0x21` |
| 1 | `r[2] != 2`（LUN）|
| 2 | `r[3] != 0`（tag）|
| 3 | `r[6] != 0` |
| 4 | `r[7] & 0x3f != 0`（status）|
| 5 | `r[10] || r[11]` |
| 6 | `ur_be32(r+12) != 0` |

**响应读取改为记账式：**

```c
out->response_attempted |= 1u << i;
if (!io->read_word(..., &word)) goto clean;
ur_bytes(word, out->response+i*4);
out->response_valid |= 1u << i;
```

**完成后加诊断采样：**

```c
out->diagnostic_crc32 = ur_crc32(out->block, UR_BLOCK_BYTES);
out->diagnostic_valid = true;
if (out->response_failures != 0) goto clean;
out->crc32 = out->diagnostic_crc32;
out->result = out->crc32 == expected_crc32 ? UR_READ_OK : UR_REFERENCE_MISMATCH;
```

### 1.2 `hakios.mt6895_ufs_read.c`

**只加两处打印（其余不变）：**

```c
printf("mt6895-ufs-read: response valid=%#x attempted=%#x failures=%#x", ...);
if (outcome.response_valid == 0xff)
    for (unsigned i = 0; i < 8; i++) printf(" %08x", ur_le32(outcome.response+i*4));
printf("\n");
if (outcome.diagnostic_valid)
    printf("mt6895-ufs-read: diagnostic-only data crc32=%#x expected=%#x bytes=%u accepted=%u", ...);
```

**🔴 注意 `accepted=%u` 这个字段 —— 它显式打印 `data_valid`，
所以「诊断 CRC 有值但 accepted=0」是可读的。**

### 1.3 `ufs_read_contract.h`

**只改一行：`#define UR_TRIAL 716u` → `717u`。**

---

## 2. 🔴 准入是否真的没变（我逐点验证）

### 2.1 `data_valid` 仍只在 `READ_OK` 时为真

```c
out->data_valid = out->result == UR_READ_OK;   /* clean 路径 */
out->data_valid = false;                        /* poison 路径 */
```

**→ 诊断 CRC 不会让它变真。**

### 2.2 `OCS != 0` 仍走 clean，不进入掩码

```c
if (out->ocs != 0) goto clean;                  /* ← 在掩码计算之前 */
out->response_failures = ur_response_failures(out->response);
```

**→ 不会把「OCS 非 0」误读为「掩码 0 = 通过」。**

### 2.3 `diagnostic_valid` 只在收齐 1024 词后置位

**它在 `for (i < UR_BLOCK_BYTES/4)` 循环**之后**才置 true。**

### 2.4 部分响应读失败时 `failures` 保持 0

**若第 i 个词读失败：`goto clean`，**掩码不计算**，保持初始化值 0。**

**🔴 这是我在审查里要指出的一个**可误读点**：**

| 情形 | `response_valid` | `response_failures` |
|---|---|---|
| 全部读完且全通过 | `0xff` | **0** |
| **部分读失败** | **≠ `0xff`** | **0**（未计算）|

**→ `failures=0` **只在** `response_valid == 0xff` 时才意味着「全部通过」。**

**设计文档已承认这点（「Mask0 is interpretable only with a full response and OCS0」），
平台的打印也用 `response_valid == 0xff` 守卫八词输出。**

**我把它记为**观察项**，建议运行时报告务必同时看 valid 与 failures 两个字段。**

---

## 3. 测试覆盖（我核对了源码）

### 3.1 差分变异测试（第 216-226 行）

```c
uint8_t response[32] = {0}; response[0] = 0x21; response[2] = 2;
CHECK(ur_response_failures(response) == 0);
for (unsigned byte = 0; byte < 32; byte++) {
    for (unsigned bit = 0; bit < 8; bit++) {
        uint8_t mutated[32]; memcpy(mutated, response, 32); mutated[byte] ^= 1u << bit;
        bool old_accepted = /* 716 的完整谓词 */;
        CHECK((ur_response_failures(mutated) == 0) == old_accepted);
    }
}
```

**→ **256 个单 bit 变异**，逐一验证「新掩码 0」等价于「716 接受」。**
**这是本轮最强的等价性证据。**

### 3.2 部分失败覆盖断言（第 250 行）

```c
CHECK(partial_responses == 8 && partial_data == UR_BLOCK_BYTES/4);
```

**→ **恰好 8 处响应词失败** + **1024 处 DATA 失败**（`4096/4`）。**

### 3.3 回调注入

**第 55 行：`if (++m->mem_reads == m->fail_read) { *out = 0xdeadbeef; return false; }`**

**→ 失败回调会先写入垃圾值再返回 false，测试验证它不被复制。**

---

## 4. 独立复跑

| sanitizer | build rc | run rc | stderr | 输出 |
|---|---|---|---|---|
| **ASan+UBSan** | **0** | **0** | **0 B** | **`PASS 32384 bounded-engine checks; fault positions reads=5525 regs=49 writes=2908; no hardware IO`** |
| **MSan** | **0** | **0** | **0 B** | 同上 |

**→ 32384 与 CMD 声明一致。**

**CMD 说未变的 atomic(40)/guard(23) 我没有重复跑 —— 它们与 716 完全相同，无具体疑虑。**

---

## 5. 构建

| 项 | 值 |
|---|---|
| 命令 | `XNU_SRC=<绝对> ./xnu-build.sh -k -j8` |
| 时间 | 14:25:20 → 14:26:36 |
| **make native rc** | **2** |
| 编译错误 | **0** |
| undefined | **0** |
| `CC loader_return.o` / `CC hakios.mt6895_ufs_read.o` | ✅ 都出现 |
| **judge native rc** | **0** |

**🔴 make rc=2 与 judge rc=0 分开记录。**

---

## 6. 编入证明

**15 条 `mt6895-ufs-read` 消息，含两条**新的**：**

```
mt6895-ufs-read: response valid=%#x attempted=%#x failures=%#x
mt6895-ufs-read: diagnostic-only data crc32=%#x expected=%#x bytes=%u accepted=%u
```

**符号 `hakios_mt6895_ufs_read_init` / `_quiesce` 都在。**

**🔴 字符串存在只证明编入，不是执行结果。**

---

## 7. 构建后复查与守卫

| 项 | 结果 |
|---|---|
| 5210 输入 | **rc=0，5210 OK，0 FAILED** |
| BUILD 外文件数 | 5210（未变）|
| **51 候选/loader 文件** | **51/51 未变** ✅ |

| 守卫 | 值 |
|---|---|
| stock | `c5f8e7bd...` / md5 `a63b2e35...` ✅ |
| shared | `67f9f933...` ✅ |
| wrapper | `95247bb0...` ✅ |
| **716 kernel** | **`c7f2f322...`**（未写）✅ |
| **716 boot.img** | **`4ca5834f...`**（未写）✅ |
| 生产 headset | `8c0cf1c2...` ✅ |

---

## 8. 我的意见

### 8.1 准入确实未变 ✅

**四项验证：`data_valid` 只在 READ_OK 时为真、`OCS!=0` 不进掩码、
`diagnostic_valid` 只在收齐后置位、256 个 bit 变异的等价性测试。**

**716 的 NOT_PROVEN 结论不受影响。**

### 8.2 一条观察项（不是阻塞）

**`failures=0` 有两种含义**：全部通过（`valid=0xff`）或**根本没计算**（`valid!=0xff`）。

**平台的八词打印有 `response_valid == 0xff` 守卫，但 `failures=%#x` 本身没有前缀提示。**

**→ 运行时读日志时必须同时看 `valid` 与 `failures` 两个字段，不能只看 `failures=0`。**

### 8.3 关于诊断 CRC

**`diagnostic-only data crc32=... accepted=...` 这个设计很好 ——
`accepted=0` 明确表示「这不是被接受的结果」。**

**但要注意：若 `diagnostic_valid` 为 false（部分 DATA 读失败），这行根本不打印，
所以「没有 diagnostic crc 行」= 诊断采样不完整，不是「CRC 为 0」。**

---

## 9. 边界

**未做**：打包、刷写、设备查询、生产编辑、旧 BUILD/产物覆盖、
GPUEB、MMIO 扩展、reset/clock/UIC/watchdog/OEM expdb。

**原始 GPT 未上传。GPUEB WIP 未 stage。等独立 package/device CMD。**
