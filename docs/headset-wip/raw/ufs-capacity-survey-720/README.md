# HOST-SURVEY-CAPACITY-720：容量查询源码调查

> 2026-11-17 19:50，DSH。**纯源码调查。无设备查询、无构建、无刷写、未改 718/719。**
> CMD 身份：`268b0b6080a78077e4757cab3c4bcc9950dc070efe9d641e31dff3c709804d1e`（含更正，原 2796 B / `5a101440...`）

---

## 0. 五个问题的答案速览

| # | 问题 | 答案 |
|---|---|---|
| **1** | `ufs_capacity` 是否根本没被调用 | ✅ **确认是死代码** —— 只有定义，**零调用点** |
| **2** | blocks / lastLBA 换算 | blocks = **31240192** ✅；**lastLBA = `0x1dcafff`，不是 `0x1dcbfff`** 🔴 |
| **3** | 与 Linux `sd.c` 比对 | 布局可复用；retry/fallback **不可复用** |
| **4** | 两个大端 32 位 + 哨兵 | ✅ 确认 |
| **5** | 8 字节 DMA 集成风险 | 见 §5 |

---

## 1. 🔴 问题 1：`ufs_capacity` 是死代码

### 1.1 定义与调用点

**源文件：`logs/ufs-service-20261117/independent-loader-reproduction719/loader-src/mtk/loader/ufs.h`**
**141239 B / `ef61ba81e8cbac4b5fbb0412c95211056f6f9c00296a134a2066d3298404586c`**

**全 loader 源里 `ufs_capacity` 只出现 3 次：**

| 位置 | 性质 |
|---|---|
| `ufs.h:2546` | 注释（`ufs_cap_stage` 变量说明）|
| `ufs.h:2882` | **函数定义** |
| `mtkloader.c:4486` | **注释**说「`gpt_detect()` is what runs `ufs_capacity()`」|

**🔴 没有任何一行代码调用它。**

### 1.2 预处理器输出佐证

**`logs/ufs-service-20261117/loader-candidate/preprocessed.c`（178555 B）里：**

```
2142: static int ufs_capacity(uint8_t lun, uint32_t *lbs_out)
```

**→ **只有定义，没有调用**。若是活代码，必然能看到调用表达式。**

### 1.3 那条注释是**过时的**

**`mtkloader.c:4486` 的注释声称 `gpt_detect()` 会运行 `ufs_capacity()`。
但 `gpt_detect()` 的实际实现（`:970-988`）是：**

```c
static uint32_t gpt_detect(void)
{
    static const uint32_t try[2] = { 512, 4096 };
    if (gpt_block_size) return gpt_block_size;
    for (int i = 0; i < 2; i++) {
        if (board_read_bytes(try[i], 512, sector_buf) &&
            !memcmp_n(sector_buf, "EFI PART", 8)) {
            gpt_block_size = try[i];
            ...
            return gpt_block_size;
        }
    }
    return 0;
}
```

**🔴 它只做「试 512、试 4096，看哪个能读到 `EFI PART`」——**完全没有调用 `ufs_capacity()`**。**

### 1.4 `ufs_lbs` 实际怎么来的

**预处理器输出里的全部赋值：**

| 行 | 内容 |
|---|---|
| 643 | `static uint32_t ufs_lbs = 4096u;` ← **静态初值** |
| 2266-2268 | `ufs_lbs = 4096u; ufs_lbs_known = 1; lbs = ufs_lbs;` ← **硬编码** |
| 2336-2338 | 同上（另一条路径）|

**🔴 两处都是**字面量 4096**，不是从设备读来的。**

### 1.5 结论

**🔴 `ufs_capacity()` **从未被执行过**。它内部的 READ CAPACITY(10) 逻辑（`upiu[16] = 0x25`、8 字节传输、PRDT 布局）**
**是**设计意图的残留**，不是实测记录。**

**「loader 的块长是 4096」这个事实来自**硬编码**与**Android 侧的 `blockdev --getss`**，
**不是来自 UFS 控制器的 READ CAPACITY(10)。**

**⚠️ 但 CMD 说的是从源码推 31240192 —— 那个数字来自 Android 的 `getsize64`，
所以真实容量仍需**一次真正的 READ CAPACITY(10)** 才能在内核侧确认。**

---

## 2. 🔴 问题 2：容量数字有一处错误

### 2.1 从 Android 参考推

**唯一源码证据（`logs/ufs-read-path-20261117/android-reference/B-lun-mapping.txt:51`）：**

```
getsize64 rc=0  127959826432
```

**换算：**

| 项 | 计算 | 结果 |
|---|---|---|
| **容量** | — | **127959826432 B** |
| **块长** | — | **4096 B** |
| 整除检查 | `127959826432 mod 4096` | **0** ✅ |
| **blocks** | `127959826432 / 4096` | **31240192** ✅ |
| **lastLBA** | `blocks - 1` | **31240191** |
| **lastLBA hex** | — | **`0x1dcafff`** |

### 2.2 更正记录：`0x1dcbfff` 是抄写错误

**🔴 我先独立重算发现不一致，随后 CMD 也追加了同一更正。**

#### 我的重算（在见到 CMD 更正之前完成）

| 值 | 十进制 | hex |
|---|---|---|
| **blocks** | **31240192** | **`0x1dcb000`** |
| **lastLBA（正确）** | **31240191** | **`0x01dcafff`** |
| 草案原值 | 31244287 | `0x1dcbfff`（**抄写错误**）|

**差异：`0x1dcbfff` − `0x01dcafff` = **4096** = 正好一块。**

#### CMD 追加的更正（原文照录）

> 「Root arithmetic correction before any candidate: the hex above was a **transcription error**.
> 127959826432/4096=31240192 blocks; last 31240191 is **`0x01dcafff`**, NOT `0x1dcbfff`.
> **Preserve this correction in the survey. No code or hardware used the mistyped value.**」

#### ✅ 双方结论一致

**正确值：`0x01dcafff` = 31240191。**

**🔴 最要紧的是 CMD 那句「No code or hardware used the mistyped value」——
错值**未进入任何候选或硬件**，所以**不影响 716–719 的任何已验收结论**。**

#### 我的独立验证输出

```
blocks  31240192 = 0x1dcb000
lastLBA 31240191 = 0x1dcafff
0x01dcafff == 31240191  ->  True
0x1dcbfff  == 31240191  ->  False
```

---

## 3. 问题 3：与 Linux `sd.c` 的比对

**参考：`/tmp/mt6895-mainline/drivers/scsi/sd.c`（`read_capacity_10` @ `:2799`）**

### 3.1 ✅ 可复用的**线格式**证据

**`sd.c:2849`：**

```c
memset(buffer, 0, 8);
the_result = scsi_execute_cmd(sdp, cmd, REQ_OP_DRV_IN, buffer, 8, ...);
```

**`sd.c:2860-2861`：**

```c
sector_size = get_unaligned_be32(&buffer[4]);
lba = get_unaligned_be32(&buffer[0]);
```

**`sd.c:2874`：**

```c
sdkp->capacity = lba + 1;
```

**`sd.c:2863`：**

```c
if (sdp->no_read_capacity_16 && (lba == 0xffffffff)) {
    sdkp->capacity = 0;   /* 无介质哨兵 */
```

**`scsi_proto.h:48`：**

```c
#define READ_CAPACITY 0x25
```

### 3.2 🔴 **不可复用**的部分

| 项 | 为什么不能照搬 |
|---|---|
| **retry 逻辑**（`failure_defs` / `allowed = 3`）| 我们的探针**明确禁止自动重试** |
| **`read_capacity_error()` 回退** | 会调 `sd_print_result` 与 sense 解析，超范围 |
| **`sd_try_rc16_first()` 自动升到 RC16** | **CMD 明确禁止自动发 RC16** |
| **`media_not_present()` 分支** | 需要 sense 解析，我们只做原始观察 |
| **`no_read_capacity_16` 标志** | Linux 设备模型特有，我们没有 |

### 3.3 loader 的 `ufs_capacity()` 与 Linux 的对应

**预处理器输出 `:2142` 起：**

| 元素 | loader | Linux |
|---|---|---|
| opcode | `upiu[16] = 0x25` | `READ_CAPACITY 0x25` ✅ |
| 传输长度 | `upiu[15] = 8` | `, 8, ...` ✅ |
| UTRD DW0 | `(1u << 28) \| (2u << 25)` | `REQ_OP_DRV_IN`（方向 in）✅ |
| `ufs_place(..., 8)` | **8 字节 PRD** | 8 字节 buffer ✅ |

**→ **线格式完全一致**。loader 的这段是**正确的设计**，只是**从未被调用**。**

---

## 4. 问题 4：两个大端 32 位 + 哨兵

**✅ 全部确认：**

| 项 | 值 | 依据 |
|---|---|---|
| **byte 0..3** | **lastLBA，大端 32 位** | `sd.c:2861` `get_unaligned_be32(&buffer[0])` |
| **byte 4..7** | **块长，大端 32 位** | `sd.c:2860` `get_unaligned_be32(&buffer[4])` |
| **count** | **`lastLBA + 1`** | `sd.c:2874` `sdkp->capacity = lba + 1` |
| **哨兵** | **`0xffffffff` → 容量 0（无介质）** | `sd.c:2863` |

**🔴 我**不会**在 `0xffffffff` 时静默回退到 4096 —— 那会掩盖设备状态。**

**🔴 我也**不会**自动发 RC16 / reset / retry。**

---

## 5. 问题 5：8 字节 DMA 的集成风险

### 5.1 与现有 4096 字节 scratch 的关系

**719 的 scratch 布局：**

| 区域 | 偏移 | 大小 |
|---|---|---|
| lease | 0 | 16 |
| UCD | `0x1000` | 32 × 1664 = 53248 |
| DATA | `0xe000` | 4096 |

**8 字节 payload **完全落在现有 4096 字节 DATA 区内**，**不需要新的映射或新的 scratch 区**。**

### 5.2 五个具体风险

| # | 风险 | 说明 |
|---|---|---|
| **1** | **传输长度与 PRDT** | 必须写 **8**，不是 4096。PRDT 的 `prd[3]` 也要相应改（现在是 `UR_BLOCK_BYTES - 1`）|
| **2** | **响应门** | 8 字节读取的响应谓词与 4096 不同 —— **717/718 的段长/sense 判据是针对 4096 读的**，需重新评估 |
| **3** | **预填与读回** | 719 的 prefill 是 `0xa5a5a5a5` 填满 4096 字节；**8 字节时只应填前 2 个 word** |
| **4** | **整块 restore** | 719 恢复整个 4096 字节 DATA 区。**8 字节操作后仍需恢复整块**（因为同一区域）|
| **5** | **lease 污染** | 若 8 字节传输不确定，**必须 poison**，不能假装恢复成功 |

### 5.3 一条我认为最重要的

**🔴 现有 719 的 `ur_restore` 恢复的是 `UR_BLOCK_BYTES`（4096）字节。
若容量查询只写 8 字节，**恢复仍应覆盖整块 4096** —— 因为快照是整个 4096 字节。**

**否则会留下 4088 字节的「未恢复」区域，而 lease 会错误地标为 RESTORED。**

---

## 6. 我的反对意见

### 6.1 一条实质性异议

**CMD 说「Existing Android reference ... already established LUN2, logical 4096, capacity 127959826432」。**

**我核对后确认这个数字**来自 Android 的 `blockdev --getsize64`**，
即**内核块层对该 LUN 的容量报告**。**

**🔴 但这不是 UFS 控制器 READ CAPACITY(10) 的返回值。**

**两者可能不同**（例如块层可能减去保留区、或用了不同的 LUN）。

**→ 所以「capacity 127959826432」是**Android 块层的观测**，不是**我们对控制器的实测**。
**720 的 READ CAPACITY(10) 若返回不同值，**应该记录下来而不是当成错误**。**

### 6.2 关于 `0xffffffff`

**CMD 说「Do not silently fall back to 4096」。**

**我同意。补充一条：若返回 `0xffffffff`，也应记录**这是设备明确报告「无介质」**，
而不是「查询失败」——两者含义不同。**

---

## 7. 边界

**未做**：设备查询、构建、刷写、打包、生产编辑、
718/719 修改、GPUEB WIP staging。

**🔴 本文所有 `file:line` 来自本地源码，未验证运行时行为。**
**🔴 容量数字来自 Android 块层观测，不是 UFS 控制器实测。**
