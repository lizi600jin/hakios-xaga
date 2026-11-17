# DEVICE718：一次守卫 READ10 —— **READ_OK**

> 2026-11-17 14:56，DSH。**一次试验，已自动恢复原厂。**
> CMD 身份：`cd8ff50165fa689248f50c40c3e7c898543ab44f2d48ce6707b2002d8b3e4726`

**🔴 这是首次 `READ_OK(5)`：`data_valid=1`、响应八字段全过、块 CRC 匹配。**

---

## 0. 六个标签 —— 全部 PASS

| # | 标签 | 结果 |
|---|---|---|
| **1** | **IMAGE_IDENTITY** | ✅ **PASS** |
| **2** | **READ_EXPERIMENT_CAPTURE** | ✅ **PASS** |
| **3** | **READ_BLOCK_COMPARE** | ✅ **PASS** |
| **4** | **LEASE_RETURN** | ✅ **PASS** |
| **5** | **NORMAL_RETURN** | ✅ **PASS** |
| **6** | **STOCK_RECOVERY** | ✅ **完成（自动）** |
| **+** | **DIAGNOSTIC_RESPONSE_CAPTURE** | ✅ **PASS**（13 词全收、OCS 0）|

---

## 1. IMAGE_IDENTITY

| 项 | 大小 | SHA256 |
|---|---|---|
| **boot.img** | **22999040 B** | **`3a3ceb1d77c9a61f289695879c077baf7ec076a8554c936aec8110afd1e61970`** |
| loader.bin | 47264 B | `dc1d273481eeab99...` |
| payload.bin | 19533320 B | `a43093c1479e34de...` |
| 嵌套 kernel | 15338864 B | `60bd6b195c2ab65a...` |

**boot MD5 `42beb68e83eabea8a281965888380417`。刷前独立复算全匹配。**

---

## 2. READ_EXPERIMENT_CAPTURE ✅

### 2.1 HLOG 原文（10 条消息）

```
[   18.622395]: mt6895-ufs-read: single READ10 lun2 lba1 bytes4096; guarded ownership required
[   18.622548]: mt6895-ufs-read: guard magic=0x554c5345 version=1 trial=718 state=0
[   18.622654]: mt6895-ufs-read: inherited utrd valid=0xff 14000000 00000000 00000000 00000000 60101000 00000000 00800080 01000001
[   18.622905]: mt6895-ufs-read: inherited upiu valid=0xff 00024001 00000000 00000000 00100000 00000028 00000100 00000001 00000000
[   18.624359]: mt6895-ufs-read: result=READ_OK(5) claimed=1 issued=1 restored=1 data_valid=1 polls=116
[   18.624428]: mt6895-ufs-read: sampled ocs=0
[   18.624562]: mt6895-ufs-read: response valid=0x1fff attempted=0x1fff failures=0 00020021 00000000 14000200 00000000 00000000 00000000 00000000 00000000 10801200 00010001 00000000 00000000 00000000
[   18.625020]: mt6895-ufs-read: diagnostic-only data crc32=0x28a2836c expected=0x28a2836c bytes=4096 accepted=1
[   18.625167]: mt6895-ufs-read: block crc32=0x28a2836c expected=0x28a2836c bytes=4096
[   18.625288]: mt6895-ufs-read: final shared state=2 loader_io_allowed=1
```

### 2.2 要素核对

| 要求 | 实测 |
|---|---|
| 228 字节完整参数 | `mali_trial=718`、`mtk_ufs_read=1` ✅ |
| read banner | ✅ |
| guard magic `0x554c5345` / version 1 / trial **718** / state **0** | ✅ |
| 一条 result 汇总 | ✅ |
| final lease state | `state=2 loader_io_allowed=1` ✅ |
| UTRD/UPIU valid-mask | **both `0xff`** ✅ |

---

## 3. 🔴 READ_BLOCK_COMPARE ✅ PASS

### 3.1 六项判据全部满足

| 判据 | 实测 | |
|---|---|---|
| **result == `READ_OK(5)`** | **`READ_OK(5)`** | ✅ |
| claimed / issued / restored | **1 / 1 / 1** | ✅ |
| **data_valid == 1** | **1** | ✅ |
| **sampled OCS == 0** | **0** | ✅ |
| **block crc32 == `0x28a2836c`** | **`0x28a2836c`** | ✅ |
| bytes == 4096 | 4096 | ✅ |

**🔴 按 CMD：这是与 Android 独立验证的 LUN2/LBA1 参考的 **CRC32 一致**，
不是完整 SHA256 或逐字节相等证明。**

### 3.2 🔴 13 词响应（我逐字段解码）

**原始 52 字节：** `21000200000000000002001400000000000000000000000000000000000000000012801001000100000000000000000000000000`

| 字节 | 字段 | 值 | 判据 |
|---|---|---|---|
| 0 | transaction | **`0x21`** | ✅ |
| 1 | flags | `0x00` | — |
| 2 | LUN | **2** | ✅ |
| 3 | tag | **0** | ✅ |
| 6 | response | **0** | ✅ |
| 7 | status | **`0x00`** | ✅ |
| **8** | **EHS** | **0** | ✅ |
| **10..11** | **segment（大端）** | **`0x0014` = 20** | ✅ **正好在 0/20 允许值** |
| 12..15 | residual（大端） | **0** | ✅ |
| **32..33** | **sense 长度（大端）** | **`0x0012` = 18** | ✅ **正好在上限 18** |
| 34..51 | sense data（18 B） | `801001000100000000000000000000000000` | **原样记录** |

**`failures=0`，且 `valid=0x1fff` 与 `OCS=0` 同时满足
—— 按 CMD 的三态判读，这是**真正被接受**的响应，不是未计算值。**

### 3.3 sense 数据

**18 字节原样：`80 10 01 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00`**

**🔴 按 CMD：sense 内容是**原始观察，不做解释**。SCSI status 0 才是命令状态判据。**

**我不推断这 18 字节的语义。**

---

## 4. LEASE_RETURN ✅ PASS

| 判据 | 实测 |
|---|---|
| 借用请求 | **是** |
| restored | **1** ✅ |
| final shared state | **2（RESTORED）** ✅ |
| loader_io_allowed | **1** ✅ |

**→ 无「声称 poisoned 却完成正常 IO」的矛盾。**

---

## 5. NORMAL_RETURN ✅ PASS

| 项 | 值 |
|---|---|
| magic | `0x474f4c48` |
| **len** | **497459** |
| **reason** | **`0x0`** |
| header sum | `0x26f45ab` |
| **完整 sum(body)** | **`0x26f45ab`** |
| 相等 | ✅ |

**🔴 这是**运行时 HLOG**，不是 boot-note-only 的 `0xfeedface`。**

**未发现 panic / data abort / guard 失败。**

---

## 6. STOCK_RECOVERY ✅（自动）

| 步 | 结果 |
|---|---|
| `adb reboot bootloader` | **rc=0 未超时** @14:54:43 |
| fastboot transport | **+6 秒** |
| stock 身份复查 | md5 `a63b2e35...` ✅ |
| **stock flash** | **rc=0**，2.25 秒，OKAY |
| `fastboot reboot` | **rc=0** @14:55:00 |
| Android 就绪 | **28 秒** |

**恢复后：slot `_a`、boot_completed 1、SELinux `Enforcing`、
**boot_a 全分区 MD5 `a63b2e35a7c2522cb24a6a10332b9bc8`**、
新 boot_id `f745b1c6-ee43-4abc-8ed2-06ee9629bf27`、SurfaceFlinger 正常。**

**→ 全程自动，未需要用户手动兜底。**

---

## 7. 时间线

| 事件 | 时刻 |
|---|---|
| **B5 reboot**（`reboot.meta`）| **14:50:43.319700** |
| **adb 可用** | **reboot + 219 s** |
| 抓 HLOG | **14:54:28.35** |

**→ 从 reboot 到抓 HLOG 约 **225 秒**，在 300 秒预算内。**

---

## 8. 原始件

| 文件 | 大小 | SHA256 |
|---|---|---|
| **`raw/trial718-logbook.bin`** | **4198400** | **`825da97d036603a4796ffafb1b4f2554fbe9d38e3a4bad37fe743d7b8d5e8719`** |
| **`raw/trial718-logbook-body.bin`** | **497459** | **`8f0d1e8a2a545201bdcff283aabbb21c048ea00becc93acfbee29c14c705918d`** |

**pre-logbook 留本地（8388608 B / `30c14202385ff633...`，旧记录 `mali_trial=717`，无 718 证据）。**

---

## 9. 我的意见

### 9.1 这次为什么通过

**716 被拒在 bit5（段长 20 非零）；717 定位了它；718 按协议接受 0 或 20，
并要求 sense ≤18、EHS 0。**

**本轮实测：segment **正好 20**、sense 长度**正好 18**（贴着上限）、EHS 0、residual 0。
**全部落在 718 的允许范围内。**

**→ 说明 716/717 的拒绝完全来自那段**被旧判据当成非法的 20 字节 sense 容器**，
而不是数据或传输本身有问题。**

### 9.2 一条必须保留的边界

**块 CRC32 一致（`0x28a2836c`）只是 **32 位校验和等价**，
**不是逐字节比较也不是 SHA256**。**

**按 CMD：这不证明完整字节相等。**

### 9.3 sense 数据我不解释

**那 18 字节 `80 10 01 00 ...` 我原样记录，**不推断语义**。**

**按 CMD：SCSI status 0 才是命令状态判据，sense 内容只是原始观察。**

### 9.4 这次通过不能推出什么

**🔴 不声称：块设备、文件系统挂载、吞吐、链路速率/gear/lane、GPU 物理状态、713 修复。**

**716/717 的 NOT_PROVEN 结论**保持原样，不因 718 通过而回溯改变。**

---

## 10. 边界

**未做**：加试、放宽白名单或谓词、改 CRC/超时/restore、MMIO 扩展、
clock/reset/UIC/watchdog/OEM expdb。

**设备已恢复原厂。**
