# DEVICE720：一次 RC10 容量查询成功

> 2026-11-17 20:53，DSH。**一次试验，已自动恢复原厂。**
> CMD 身份：`5ce12826ecc29e0ed723de5a6c3b7078fcc3751602cab34db020e9d4746a4587`

---

## 0. 八项标签

| 标签 | 结果 |
|---|---|
| **IMAGE_IDENTITY** | ✅ PASS |
| **QUERY_CAPTURE** | ✅ PASS |
| **QUERY_DATA_ACCEPTED** | ✅ PASS |
| **CAPACITY_OBSERVATION** | ✅ PASS |
| **ANDROID_COMPARISON** | ✅ **MATCH** |
| **LEASE_HELD_UNTIL_IDLE** | ✅ PASS |
| **LEASE_RETURN** | ✅ PASS |
| **IDLE_RETURN_RELEASE** | ✅ PASS |
| **NORMAL_RETURN** | ✅ PASS |
| **STOCK_RECOVERY** | ✅ 完成（自动）|

**独立解析器（`review-device-720.py`）输出 `status: PASS`，与我一致。**

---

## 1. HLOG 原文（11 条，全录）

```
[18.632480] mt6895-ufs-capacity: single RC10 lun2 data_bytes8 scratch_bytes4096; guarded handoff required
[18.632640] mt6895-ufs-capacity: guard magic=0x554c5345 version=1 trial=720 state=0
[18.633282] mt6895-ufs-capacity: opened baseline_verified=1 control=0x2 shared=1
[18.634799] mt6895-ufs-capacity: result=5 claimed=1 issued=1 memory_restored=1 query_valid=1 polls=279
[18.634958] mt6895-ufs-capacity: ocs=0 response_valid=0x1fff attempted=0x1fff failures=0
[18.635032] mt6895-ufs-capacity: response_words 00020021 00000000 00000000 ... (13 词)
[18.635412] mt6895-ufs-capacity: raw valid=0x3 attempted=0x3 changed=0x3 ffafdc01 00100000
[18.635622] mt6895-ufs-capacity: tail complete=1 ok=1 checked_words=1022 first_bad=0xffffffff
[18.635876] mt6895-ufs-capacity: decode=0 last_lba=0x1dcafff block_bytes=4096 blocks=31240192 capacity_bytes=127959826432 android_match=1
[18.636022] mt6895-ufs-capacity: final control=0x2 shared=1 idle_close_deferred=1
mt6895-ufs-capacity: return-gate magic=0x554c5345 version=1 trial=720 state=2 allowed=1
```

**🔴 只有**一次** RC10：`issued=1`、`polls=279`，没有第二次查询。**

---

## 2. 几何解码（我从 raw 8 字节独立重算）

**raw 两词 `ffafdc01 00100000`（小端存储）：**

```
存储字节 01 dc af ff 00 00 10 00
lastLBA（大端 32 位）= 31240191 = 0x1dcafff
块长  （大端 32 位）= 4096
blocks   = lastLBA + 1 = 31240192
capacity = blocks × 4096 = 127959826432
```

**与打印值逐项一致 ✅**

### 2.1 🔴 关于那个十六进制

**`0x1dcafff` —— 正是我在 720 调查里**独立重算**出的值，
也是你 CMD 追加更正后的值。**先前草案的 `0x1dcbfff` 是抄写错误，本机读数确认了 `0x1dcafff`。**

---

## 3. Android 比对

| 项 | 实测 | Android 参考 | |
|---|---|---|---|
| 块长 | **4096** | 4096 | ✅ |
| capacity | **127959826432** | 127959826432 | ✅ |
| **结果** | **`MATCH`** | | |

**🔴 这是**独立观测的吻合**，不是把 Android 值代进去。**

---

## 4. 响应 13 词

| 字段 | 值 |
|---|---|
| transaction (byte0) | `0x21` |
| flags (byte1) | 0 |
| LUN (byte2) | **2** |
| tag (byte3) | **0** |
| response (byte6) / status (byte7) | **0 / 0** |
| **EHS (byte8)** | **0** |
| **segment (byte10..11)** | **0** |
| **sense 长度 (byte32..33)** | **0** |
| residual | **0** |

**→ 段长 0、sense 0 —— 与 719 的 request=3 同形态（那次也是全零）。**

**🔴 按 CMD：sense 内容保持**不解读**。**

---

## 5. 租约与归还

| 项 | 值 |
|---|---|
| 查询后 | `control=0x2`（IDLE）、`shared=1`（**CLAIMED**）|
| `idle_close_deferred` | **1** |
| **loader 审计** | **`magic=0x554c5345 version=1 trial=720 state=2 allowed=1`** |
| 审计位置 | **HLOG 末尾（唯一）** |

**🔴 每次查询的 `memory_restored=1` 不等于外层归还；外层由 quiesce 的 `us_stop` 完成。**

**🔴 审计是 loader 生成的第二 span，所以正文**不完全是内核生成**。**

---

## 6. 时间线

| 事件 | 时刻 |
|---|---|
| **reboot（`reboot.meta`）** | **20:47:27.941216** |
| **adb 可用** | **reboot + 219 s** |
| 抓 HLOG | **20:51:12** |
| 开始恢复 | **20:51:19** |
| **reboot 回 stock** | **20:51:43** |
| Android 就绪 | **+32 s** |

**→ reboot 到抓 HLOG 约 **225 秒**，在 300 秒界内。**

---

## 7. 恢复（自动，未用兜底）

| 步 | native rc |
|---|---|
| `adb reboot bootloader` | **0**，未超时 |
| fastboot transport | **+6 s** |
| stock 刷写 | **0**，2.12 s，OKAY |
| reboot | **0** |

**恢复后：slot `_a`、boot_completed 1、SELinux `Enforcing`、
**boot_a 全 64 MiB MD5 `a63b2e35a7c2522cb24a6a10332b9bc8`**、
新 boot_id `76b7316c-cf63-4be1-9e44-91de266963bf`、SurfaceFlinger 正常。**

---

## 8. 原始件

| 文件 | 大小 | SHA256 |
|---|---|---|
| `raw/trial720-logbook.bin` | 4198400 | **`b68599bcff24d894144c5fed6191b83d97bf5d04e6dc34d4fa50969982c4d117`** |
| `raw/trial720-logbook-body.bin` | 497300 | **`cba560ba20e5c41da5940ef937fe8abb3165fe9fb7cf27ff5642f653f81580fc`** |

**pre-logbook 8388608 B `edd437d5...` 留本地；旧记录 `mali_trial=719`，无 720 证据。**

**HLOG len 497300、reason `0x0`、`sum(body)=0x26f2a52` 与 header 相等 ✅**

---

## 9. 边界

**未做**：加试、重试、RC16、变量 READ、块注册、
MMIO 扩展、clock/power/reset/UIC/watchdog/OEM expdb。
**无 panic / data abort / STOP / guard 失败上下文。**

**🔴 不声称**：整盘、用户态、文件系统、吞吐、gear/lane、全局所有权、CPU 713、GPU。
**716/717 的 NOT_PROVEN 与 718/719 的 PASS 不变。**

**🔴 没有用 CRC32 声称容量字节等同 —— 只直接解码并报告那 8 字节。**
