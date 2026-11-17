# DEVICE717：守卫诊断 READ10 试验（六标签 + 诊断字段）

> 2026-11-17 14:36，DSH。**一次试验，已自动恢复原厂。**
> CMD 身份：`7da774a6a257b650f6dc2c698e429476618e6ef7ae89c9a4f5f0a4291a6ce261`

**🔴 诊断目标达成：失败位精确定位到 `failures=0x20`（bit5），
`byte11 = 0x14` 是唯一不符项。
**但 `accepted=0`，READ_BLOCK_COMPARE 仍为 NOT_PROVEN。**

---

## 0. 六标签 + 一个额外标签

| # | 标签 | 结果 |
|---|---|---|
| **1** | **IMAGE_IDENTITY** | ✅ **PASS** |
| **2** | **READ_EXPERIMENT_CAPTURE** | ✅ **完成** |
| **3** | **READ_BLOCK_COMPARE** | ❌ **NOT_PROVEN** —— `COMPLETED_ERROR(3)`，`accepted=0` |
| **4** | **LEASE_RETURN** | ✅ **PASS**（restored=1、state=2、allowed=1）|
| **5** | **NORMAL_RETURN** | ✅ **PASS**（新运行时 HLOG，`reason=0x0`，sum 匹配）|
| **6** | **STOCK_RECOVERY** | ✅ **完成**（自动）|
| **+** | **DIAGNOSTIC_RESPONSE_CAPTURE** | ✅ **完整** —— `valid=0xff`，八词全收，OCS 0 |

---

## 1. IMAGE_IDENTITY

| 项 | 大小 | SHA256 |
|---|---|---|
| **boot.img** | **22999040 B** | **`c275625ff431ee93b8fdc87284c1336bd631d7721af13415deba124392496699`** |
| loader.bin | 47264 B | `3c88cd069e000b48...` |
| payload.bin | 19533320 B | `bb40824042997ba9...` |
| 嵌套 kernel | 15338864 B | `07b016feb866c434...` |

**boot MD5 `69dea280d97bdca4267c114f1ca537b8`。刷前独立复算全匹配。**

---

## 2. 🔴 新诊断字段（本轮核心）

### 2.1 HLOG 原文

```
[   18.634762]: mt6895-ufs-read: response valid=0xff attempted=0xff failures=0x20 00020021 00000000 14000200 00000000 00000000 00000000 00000000 00000000
[   18.635122]: mt6895-ufs-read: diagnostic-only data crc32=0x28a2836c expected=0x28a2836c bytes=4096 accepted=0
```

### 2.2 掩码

| 字段 | 值 | 含义 |
|---|---|---|
| **`valid`** | **`0xff`** | **八词全部读到** ✅ |
| **`attempted`** | **`0xff`** | 八词全部尝试 ✅ |
| **`failures`** | **`0x20`** | **只有 bit5 置位** |

### 2.3 八词按 716 谓词逐项核对（我手工解码）

**响应 32 字节：** `21000200 00000000 00020014 00000000 ...`

| Bit | 条件 | 实际 | 结果 |
|---|---|---|---|
| 0 | `byte0 & 0x3f == 0x21` | `0x21` | ✅ |
| 1 | `byte2 == 2`（LUN）| `0x02` | ✅ |
| 2 | `byte3 == 0`（tag）| `0x00` | ✅ |
| 3 | `byte6 == 0`（response）| `0x00` | ✅ |
| 4 | `byte7 & 0x3f == 0`（status）| `0x00` | ✅ |
| **5** | **`byte10 \|\| byte11`** | **`byte10=0x00, byte11=0x14`** | **🔴 唯一不符** |
| 6 | `be32(bytes12..15) == 0` | `0x00000000` | ✅ |

**🔴 所以 716 那次被拒的原因**精确定位到 bit5**：
**`byte11 = 0x14 = 20` 非零。**

**按 CMD 的定义，bit5 是「data-segment-length」字段。**

### 2.4 诊断数据 CRC

| 字段 | 值 |
|---|---|
| **`diagnostic-only data crc32`** | **`0x28a2836c`** |
| **`expected`** | **`0x28a2836c`** |
| bytes | 4096 |
| **`accepted`** | **0** |

**🔴 诊断 CRC 与 Android 参考**的 CRC32 相同**，但 `accepted=0`。**

**按 CMD 明确：CRC 相同但 accepted 0 仍**不是** READ_BLOCK_COMPARE PASS。**

---

## 3. READ_BLOCK_COMPARE ❌ NOT_PROVEN

| 判据 | 实测 | 通过 |
|---|---|---|
| result == `READ_OK(5)` | **`COMPLETED_ERROR(3)`** | ❌ |
| claimed / issued / restored | 1 / 1 / 1 | ✅ |
| **data_valid** | **0** | ❌ |
| sampled OCS | **0** | ✅ |
| block crc32 == 0x28a2836c | **未打印 block crc32 行**（结果非 READ_OK/MISMATCH）| ❌ |
| bytes | 4096 | ✅ |

### 3.1 🔴 这里有一个必须说清的关系

**诊断 CRC（`0x28a2836c`）与期望的 **CRC32 相同**。**

**🔴 这不等于「内容是对的」** —— **CRC32 一致**只是 32 位校验和的碰撞级等价，
**不是逐字节比较，也不是 SHA256**。

**准确表述：那 4096 字节的 CRC32 与 Android 参考一致；其**完整内容未被逐字节验证**。**

**但结果仍不是 READ_OK，因为**响应谓词**拒绝了该包（bit5）。**

**→ 「数据对」与「传输被接受」是**两件事**。
**引擎的设计正是把这两件事分开：`data_valid` 只由「响应谓词通过 + CRC 匹配」共同决定。**

---

## 4. LEASE_RETURN ✅ PASS

| 判据 | 实测 |
|---|---|
| 借用请求 | **是**（claimed=1 issued=1）|
| restored | **1** ✅ |
| final shared state | **2（RESTORED）** ✅ |
| loader_io_allowed | **1** ✅ |

**→ 与 CMD 第 12 条的反向检查一致：没有「声称 poisoned 却完成正常 IO」的矛盾。**

---

## 5. NORMAL_RETURN ✅ PASS

| 项 | 值 |
|---|---|
| magic | `0x474f4c48` |
| version | 1 |
| **len** | **496731**（<= 4194304）|
| **reason** | **`0x0`** |
| header sum | `0x26e44bc` |
| **完整 sum(body)** | **`0x26e44bc`** |
| 相等 | ✅ |

**🔴 这是**运行时 HLOG**（9 条消息），**不是** boot-note-only 的 `0xfeedface`。**

**未发现 panic / data abort / guard 失败。**

---

## 6. STOCK_RECOVERY ✅（自动）

| 步 | 结果 |
|---|---|
| `adb reboot bootloader` | **rc=0，未超时** @14:34:48 |
| fastboot transport | **+6 秒** |
| stock 身份复查 | md5 `a63b2e35...` ✅ |
| **stock flash** | **rc=0**，2.14 秒，OKAY |
| `fastboot reboot` | **rc=0** @14:35:12 |
| Android 就绪 | **41 秒** |

**恢复后：slot `_a`、boot_completed 1、SELinux `Enforcing`、
**boot_a 全分区 MD5 `a63b2e35a7c2522cb24a6a10332b9bc8`**、
新 boot_id `77b01ffe-6fc8-4c84-b0e8-0782fc8d6243`、SurfaceFlinger 正常。**

**→ 全程自动，未需要用户手动兜底。**

---

## 7. 时间线

| 事件 | 时刻 |
|---|---|
| **B5 reboot**（`reboot.meta`）| **14:31:07.007904** |
| **adb 可用** | **reboot + 208 s** |
| 抓 HLOG | **14:34:40** |

**→ 从 reboot 到抓 HLOG 约 **214 秒**，在 300 秒预算内。**

---

## 8. 原始件

| 文件 | 大小 | SHA256 |
|---|---|---|
| **`raw/trial717-logbook.bin`** | **4198400** | **`e3c87f1a5836791c918da899b953197f861781c36a88ecd138038572f99da19a`** |
| **`raw/trial717-logbook-body.bin`** | **496731** | **`234c8b7284805420daf43ea4b0a6d813c223bb05454bdf5acdc897e389f10b9a`** |

**pre-logbook 留本地（8388608 B / `03b21da17877fa55...`，旧记录 `mali_trial=716`，无 717 证据）。**

---

## 9. 我的意见

### 9.1 诊断目标达成 ✅

**716 那次被拒的具体原因现在**有证据**了：**

**`failures=0x20` → bit5 → `byte11 = 0x14`（20）非零。**

**七项里六项通过，只有这一项不符。**

### 9.2 🔴 但我不做因果断言

**按 CMD：「only subsequent source/protocol review can establish a cause or propose a NEW revision」。**

**我知道 `byte11 = 0x14`，但**不知道**它为什么是 20，也不知道：
- 这个字段在响应里应该是多少
- 20 是否可能是合法的（比如某种长度/残留语义）
- 是否需要改动谓词（**本轮绝对不改**）**

**→ 这是**下一步源码/协议审查**的输入，不是结论。**

### 9.3 一条值得注意的观察

**诊断 CRC 与期望精确相同（`0x28a2836c`），说明**那 4096 字节是对的**。**

**换句话说：控制器**确实按 READ(10) 把 LBA1 的正确内容搬到了 scratch**，
只是响应包的 bit5 字段不符合 716 的谓词。**

**这个组合（数据对 + 响应被拒）是很有价值的信息，但**仍不足以**称存储读成功。**

---

## 9.4 🔴 与协议参考的对照（718 前的线索）

**`PROTOCOL-REFERENCE-PRE717.md` 已归档在本目录。**

**按 EDK2 `UTP_RESPONSE_UPIU` 定义（对 UFS 4.0 Table 10.15）：**

| 字节 | 字段 |
|---|---|
| 0..3 | transaction type / flags / LUN / tag |
| 6 / 7 | response / status |
| **10..11** | **data segment length** |
| 12..15 | residual |
| 16..31 | reserved |
| **32 起** | **sense-data length 与 sense data** |

**→ 717 采到的 `byte11 = 0x14`（20）在 **DataSeg length** 字段里，
表示 32 字节 header **之后**还有 20 字节数据。**

**🔴 那 20 字节 717 **没有采集** —— 探针只读 32 字节 response header。**

**这不构成结论，只是 718 的输入：若要判断这 20 字节是什么，
需要新的候选扩大观测宽度，并经过新的审查与编号试验。**

**按协议参考的明确要求：「Any change to interpretation or observation width needs
new candidate/tests/review and a new numbered trial.」**

---

## 10. 边界

**🔴 不声称**：块设备、文件系统挂载、吞吐、链路速率/gear/lane、GPU 物理状态、
713 修复、或「READ_BLOCK_COMPARE 通过」。

**未做**：加试、放宽白名单或谓词、改 CRC/超时/restore、MMIO 扩展、
clock/reset/UIC/watchdog/OEM expdb。

**CRC32 相同但 accepted=0 —— 仍为 NOT_PROVEN。**
