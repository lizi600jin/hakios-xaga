# DEVICE716：单次守卫 READ10 试验（六标签）

> 2026-11-17 14:08，DSH。**一次试验，已恢复原厂。**
> CMD 身份：`115e953dfb9f37a06828960a9bd161a44f8824995269233d5ad3ffa7f6eda4b2`

**🔴 结论：探针**执行了**这次读取（claimed/issued/restored 全 1），
但结果是 `COMPLETED_ERROR(3)`，**不是** READ_OK。块数据未通过校验。**

---

## 0. 六个独立标签

| # | 标签 | 结果 |
|---|---|---|
| **1** | **IMAGE_IDENTITY** | ✅ **PASS** |
| **2** | **READ_EXPERIMENT_CAPTURE** | ✅ **完成**（新运行时 HLOG，含全部要素）|
| **3** | **READ_BLOCK_COMPARE** | ❌ **未通过** —— `COMPLETED_ERROR(3)`，不是 READ_OK |
| **4** | **LEASE_RETURN** | ✅ **PASS**（借用了请求，restored=1，final state=2，loader_io_allowed=1）|
| **5** | **NORMAL_RETURN** | ✅ **PASS**（**新运行时 HLOG**，`reason=0x0`，完整 sum 匹配）|
| **6** | **STOCK_RECOVERY** | ✅ **完成**（自动，全分区 MD5 回到 `a63b2e35`）|

**🔴 第 3 标签是拒绝性的 —— 按 CMD 规定，`COMPLETED_ERROR` 是**独立的诊断结果，不是存储读 PASS**。**

---

## 1. IMAGE_IDENTITY

| 项 | 大小 | SHA256 |
|---|---|---|
| **boot.img** | **22999040 B** | **`4ca5834f555300c6616245e265923ac1897ef9d8d6d12c57638fa870afc9c057`** |
| loader.bin | 47264 B | `ca0112e2d6659c37a469fe60881f9a14d8de5e87008ee50cce8007572b800a13` |
| payload.bin | 19533288 B | `c13981d45c8d46d9336a119420a8b8490c1d8fb3801a66b65c4bf0f0ca9bba0a` |
| 嵌套 kernel | 15338824 B | `c7f2f3222f314b82885b0567975e3af759c9b172ececd6fbefe437b65ddff6aa` |

**boot MD5 `3abcb40dfe056bb152eee1fbbef9af57`。**

**刷前我独立复算三个文件与 stock，全部与批准值一致。**

---

## 2. READ_EXPERIMENT_CAPTURE ✅

### 2.1 HLOG 原文（7 条 mt6895-ufs-read）

```
[   18.938736]: mt6895-ufs-read: single READ10 lun2 lba1 bytes4096; guarded ownership required
[   18.938888]: mt6895-ufs-read: guard magic=0x554c5345 version=1 trial=716 state=0
[   18.938994]: mt6895-ufs-read: inherited utrd valid=0xff 14000000 00000000 00000000 00000000 60101000 00000000 00800080 01000001
[   18.939244]: mt6895-ufs-read: inherited upiu valid=0xff 00024001 00000000 00000000 00100000 00000028 00000100 00000001 00000000
[   18.940487]: mt6895-ufs-read: result=COMPLETED_ERROR(3) claimed=1 issued=1 restored=1 data_valid=0 polls=117
[   18.940557]: mt6895-ufs-read: sampled ocs=0
[   18.940676]: mt6895-ufs-read: final shared state=2 loader_io_allowed=1
```

### 2.2 逐步核对 CMD 的要求

| 要求 | 实测 | 结果 |
|---|---|---|
| **228 字节完整参数** | HLOG boot-args 行含 `mali_trial=716`、`mtk_ufs_read=1` | ✅ |
| **read banner** | `single READ10 lun2 lba1 bytes4096; guarded ownership required` | ✅ |
| **guard magic 0x554c5345** | **`0x554c5345`** | ✅ |
| **version 1** | **1** | ✅ |
| **trial 716** | **716** | ✅ |
| **state 0** | **state=0**（UNUSED）| ✅ |
| **一次 result 汇总** | **恰好 1 条** | ✅ |
| **final lease state** | `final shared state=2 loader_io_allowed=1` | ✅ |
| **UTRD/UPIU valid-mask 0xff** | **both 0xff** | ✅ 可解释八词 |

### 2.3 🔴 继承描述符的实际内容（这是关键证据）

**UTRD 八词**：`14000000 00000000 00000000 00000000 60101000 00000000 00800080 01000001`

| 词 | 值 | 解读 |
|---|---|---|
| [0] | `0x14000000` | UFS command, **data in** |
| [4]/[5] | `0x60101000` / `0` | **UCD 基址 = `UR_UCD_PA`** ✅ |
| [6] | `0x00800080` | response offset 512, length 512 |
| [7] | `0x01000001` | PRDT offset 1024, 1 entry |

**UPIU 八词**：`00024001 00000000 00000000 00100000 00000028 00000100 00000001 00000000`

| 词 | 值 | 解读 |
|---|---|---|
| [0] | `0x00024001` | 字节序为 `01 40 02 00` —— **UPIU cmd 0x01, flags 0x40, LUN 2** ✅ |
| [3] | `0x00100000` | **数据长度 0x1000 = 4096**（大端）✅ |
| [4] | `0x00000028` | **READ(10) opcode 0x28** ✅ |

**→ 这正是 loader 的 `ufs_read(8,8,UR_DATA_PA)` 留下的**只读 READ 描述符** ——
符合设计预期「kernel 继承的是 READ-only」。**

---

## 3. 🔴 READ_BLOCK_COMPARE ❌ 未通过

| 判据（CMD 要求）| 实测 | 通过 |
|---|---|---|
| result == `READ_OK(5)` | **`COMPLETED_ERROR(3)`** | ❌ |
| claimed == 1 | 1 | ✅ |
| issued == 1 | 1 | ✅ |
| restored == 1 | 1 | ✅ |
| **data_valid == 1** | **0** | ❌ |
| **sampled OCS == 0** | **0** | ✅ |
| block crc32 == `0x28a2836c` | **未记录**（结果非 READ_OK/REFERENCE_MISMATCH）| ❌ |
| bytes == 4096 | 4096 | ✅ |

**→ 按 CMD：这是**独立的诊断结果，不是存储读 PASS**。**

### 3.1 这个结果意味着什么

**`COMPLETED_ERROR(3)` 在引擎里的含义（我核对过 v3 源码）：**

**控制器**报告了完成**（`polls=117` 后 `dbr` 位清了，OCS 读作 0），
但**响应校验失败** —— `ur_run` 检查的八项（OCS 0、事务类型 0x21、
LUN 2、状态 0、长度、tag）里**至少一项不符**，于是 `goto clean` 而不是继续读数据。

**🔴 我没有该响应八个词的具体值** —— 引擎在失败路径上不打印它们
（打印语句只在 `READ_OK`/`REFERENCE_MISMATCH` 时输出 CRC）。

### 3.2 🔴 我必须说清的边界

**`OCS = 0` 说明控制器认为传输成功，但响应校验失败。**

**可能的原因（我**没有**证据区分）：**

| 可能 | 说明 |
|---|---|
| tag 不匹配 | loader 用的 tag 与引擎期望的不同 |
| 响应事务类型/长度不符 | 设备返回的结构与预期不同 |
| 字段偏移假设错误 | 引擎读的 response 区与设备写入的实际位置不同 |

**→ 这是**真实的诊断信息**，但它**不证明**任何一条具体原因。**

**按 CMD：** 不得据此放宽谓词或加命令。**

---

## 4. LEASE_RETURN ✅ PASS

| 判据 | 实测 |
|---|---|
| 是否借用请求 | **是**（`claimed=1 issued=1`）|
| restored | **1** ✅ |
| **final shared state** | **2（RESTORED）** ✅ |
| **loader_io_allowed** | **1** ✅ |

**→ 租约正确归还，loader 被允许使用存储。**

**🔴 这与 CMD 第 12 条的反向检查一致：
新运行时 HLOG 报告的是 `state=2 / allowed=1`，**不是** poisoned/claimed，
而 loader 确实完成了 HLOG IO —— **没有矛盾**。**

---

## 5. NORMAL_RETURN ✅ PASS

| 项 | 值 |
|---|---|
| magic | `0x474f4c48` |
| version | 1 |
| **len** | **497672**（<= 4194304）|
| **reason** | **`0x0`** |
| header sum | `0x26f7ecc` |
| **完整 sum(body)** | **`0x26f7ecc`** |
| **相等** | ✅ |

**🔴 这是**运行时 HLOG**（497672 B，含 7 条探针消息 + 完整 boot-args），
**不是** boot-note-only 的 `0xfeedface`。**

**按 CMD 第 5 条：boot-note 不算正常返回 —— 本轮**确实拿到了**正常运行时返回。**

### 5.1 失败扫描

**未发现 panic / data abort / guard 失败。**

---

## 6. STOCK_RECOVERY ✅（自动，未需手动兜底）

| 步 | 结果 |
|---|---|
| `adb reboot bootloader` | **rc=0，未超时** @14:06:40 |
| fastboot transport | **+6 秒可用**（输出 `product: xaga`）|
| stock 身份复查 | 67108864 B / `c5f8e7bd...` / md5 `a63b2e35...` ✅ |
| **stock flash** | **rc=0**，2.23 秒，`Sending/Writing OKAY` |
| `fastboot reboot` | **rc=0** @14:07:04 |
| Android 就绪 | **28 秒** |

### 6.1 恢复后核验

| 项 | 值 |
|---|---|
| slot | `_a` ✅ |
| boot_completed | 1 ✅ |
| SELinux | `Enforcing` ✅ |
| **boot_a 全分区 MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** ✅ |
| **新 boot_id** | **`396b9126-ffbd-4892-a201-2b559ebad0a0`** |
| SurfaceFlinger | 正常 ✅ |

**→ 全程自动恢复，未需要用户手动兜底。**

### 6.2 🔴 恢复证明原文（D-verify.txt）

```
  slot            _a               OK
  boot_completed  1                OK
  SELinux         Enforcing        OK
  boot_a size     67108864  OK
  boot_a MD5      a63b2e35a7c2522cb24a6a10332b9bc8
  boot_id         396b9126-ffbd-4892-a201-2b559ebad0a0  （新 id）
  SurfaceFlinger  Build configuration: [sf PRESENT_TIME_OFFSET=0 FORCE_HWC_FOR...
```

**→ slot / boot_completed / SELinux / 全 64 MiB MD5 / 新 boot_id / SurfaceFlinger 六项齐全。**

---

## 7. 时间线（按您要求：轮询起点不作 reboot 时刻）

| 事件 | 时刻 |
|---|---|
| **B5 reboot 命令**（`reboot.meta`）| **14:02:52.655427** |
| 轮询起点 | reboot 返回后立即 |
| **adb 可用** | 轮询 +201 s |
| **抓 HLOG** | **14:06:33.509541** |
| **开始恢复** | 14:06:40.497694 |

**🔴 轮询 +201 s 是「轮询起点 → adb」的观测值，**不是** reboot → adb 的精确耗时。
从 reboot 到抓 HLOG 约 **221 秒**，在 300 秒预算内。**

---

## 8. 原始件

| 文件 | 大小 | SHA256 |
|---|---|---|
| **`raw/trial716-logbook.bin`** | **4198400** | **`f8b7b52dd29f19e81a65a096989d723a088e31d8cbb700251b6fe9fcf7a9b1ea`** |
| **`raw/trial716-logbook-body.bin`** | **497672** | **`1f2a34cbde7db4433aaca5a30ae3ed8bd78460501457bd383d3c036d7efd57ea`** |

**pre-logbook 留本地（8388608 B / `7dd70d7f94ffa4f4...`，旧记录 `mali_trial=715`，无 716 证据）。**

---

## 8.1 独立解析器交叉核对

**Codex 的 `review-device-716.py`（8491 B）+ `test-review-device-716.py`（5978 B）+
`device-parser-tests-716.json`（1223 B，39 项解析测试）。**

**我独立运行到新路径 `device716/my-review/`，未覆盖 Codex 结果：**

| 标签 | 解析器输出 | 我的判断 |
|---|---|---|
| `NORMAL_RETURN` | **PASS** | 一致 |
| `READ_EXPERIMENT_CAPTURE` | **PASS** | 一致 |
| **`READ_BLOCK_COMPARE`** | **NOT_PROVEN** | **一致** |
| `LEASE_RETURN` | **PASS** | 一致 |
| `status` | **DIAGNOSTIC_ONLY** | 一致 |

**数值全部一致**：`result_code=3`、`claimed/issued/restored=1`、
`data_valid=0`、`polls=117`、`reason=0`、`full_sum_verified=true`。

### 🔴 关于解析器返回 rc=1

**脚本末尾是：**

```c
raise SystemExit(0 if result['status'] == 'PASS' else 1)
```

**本轮 `status = DIAGNOSTIC_ONLY`（因为 `READ_BLOCK_COMPARE = NOT_PROVEN`），
所以 **rc=1 是设计如此，不是解析失败**。

**判据：`status = PASS if reason==0 and READ_BLOCK_COMPARE==PASS else DIAGNOSTIC_ONLY`。**

---

## 9. 我的反对意见 / 需要下一步澄清的

**① 最关键的一条：`OCS=0` 但响应校验失败。**

**控制器报告完成且 OCS 为 0（成功），但引擎拒绝了响应。
我**没有**响应八个词的值 —— 引擎在 `COMPLETED_ERROR` 路径上不打印它们。**

**→ 如果要定位原因，需要**在不放宽白名单的前提下**，把失败路径的响应八词也打出来。
这是**诊断可见性**的改进，不是放宽谓词。**

**② `polls=117` 是正常的**（远小于 `max_polls=500000`），说明不是超时。

**③ 我没有据此推断任何硬件故障** —— 传输确实完成了，只是响应格式与引擎预期不符。

---

## 10. 边界

**未做**：加试、放宽白名单、改动 CRC/几何、mount、文件系统、吞吐、链路速率、
GPU 物理状态推断、713 事件归因。

**🔴 CRC32 一致才是块比较通过；本轮**没有**通过，所以不声称任何存储读成功。**
