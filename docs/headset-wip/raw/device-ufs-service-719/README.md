# DEVICE719：守卫字符快照服务 —— 三次读取全 PASS

> 2026-11-17 19:37，DSH。**一次试验，已自动恢复原厂。**
> CMD 身份：`fba23a58fc440e3efaf9baccd3fd01cf92bbaaebb580b1f16df0e3b025329dec`

---

## 0. 十项标签全部 PASS

| 标签 | 结果 |
|---|---|
| **IMAGE_IDENTITY** | ✅ PASS |
| **READ_SERVICE_CAPTURE** | ✅ PASS |
| **VFS_READ_SERVICE** | ✅ PASS（3 次 `vn_rdwr`，`error=0 residual=0`）|
| **READ_BLOCK_COMPARE** | ✅ PASS（三次全 `engine=5`、`crc32=0x28a2836c`）|
| **WRITE_OPEN_REJECTED** | ✅ PASS（`EROFS 30`，`0/0` 计数器）|
| **EOF_WITHOUT_COMMAND** | ✅ PASS（`residual=4`，`3/3` 计数器）|
| **LEASE_HELD_BETWEEN** | ✅ PASS（三次后 `control=0x2 shared=1`）|
| **LEASE_RETURN** | ✅ PASS |
| **IDLE_RETURN_RELEASE** | ✅ PASS（`return-gate state=2 allowed=1`）|
| **NORMAL_RETURN** | ✅ PASS（`reason=0`，sum 匹配）|
| **STOCK_RECOVERY** | ✅ 完成（自动）|

**独立解析器（`review-device-719.py`）输出 `status: PASS`，十项标签与我一致。**

---

## 1. HLOG 原文（27 条 service 消息的要点）

```
[18.668142] bounded readonly LUN2 LBA1 bytes4096 budget3; guarded handoff required
[18.668300] guard magic=0x554c5345 version=1 trial=719 state=0
[18.668940] opened baseline_verified=1 control=0x2 shared=1
[18.669156] node registered path=/dev/mt6895-ufs-lba1 major=30 mode=0400 snapshot_bytes4096
[20.670300] vnode lookup error=0
[20.670318] write-open error=30 expected=30 requests_before=0 requests_after=0
[20.671271] request=1 result=0 engine=5 claimed=1 issued=1 memory_restored=1 delivered=1 control=0x2 shared=1 polls=122
[22.673322] request=2 ... polls=119
[24.675641] request=3 ... polls=117
[24.675816] vfs eof error=0 residual=4 requests_before=3 requests_after=3
[24.675826] selftest complete requests=3 issued=3 delivered=3 control=0x2 shared=1; idle close deferred to quiesce
mt6895-ufs-service: return-gate magic=0x554c5345 version=1 trial=719 state=2 allowed=1
```

---

## 2. 🔴 一个必须如实记录的事实：三次响应**不相同**

**我从 HLOG 里逐词对比了三次的 `response_words`：**

| 词 | request=1 | request=2 | request=3 | 同 |
|---|---|---|---|---|
| [0] | `00020021` | `00020021` | `00020021` | ✅ |
| [1]-[7] | 全 0 | 全 0 | 全 0 | ✅ |
| **[2]** | **`14000200`** | **`14000200`** | **`00000000`** | **🔴 不同** |
| **[8]** | **`10801200`** | **`10801200`** | **`00000000`** | **🔴 不同** |
| **[9]** | **`00010001`** | **`01000001`** | **`00000000`** | **🔴 不同** |
| [10]-[12] | 全 0 | 全 0 | 全 0 | ✅ |

**解码后：**

| 请求 | segment | sense 长度 | EHS | residual |
|---|---|---|---|---|
| 1 | **20** | **18** | 0 | 0 |
| 2 | **20** | **18** | 0 | 0 |
| **3** | **0** | **0** | 0 | 0 |

**🔴 三次的块 CRC32 全部是 `0x28a2836c`，但响应**并不同**。**

**我不会声称它们「相同」。这是个如实观察：前两次带 20 字节 sense 容器（sense 长 18），
第三次段长为 0。**

**→ 这**不改变**任何标签判定（三次都满足 718 修订后的判据：段长 0 或 20、sense ≤18、EHS 0），
**但**响应内容确实随请求变化**，值得记录。**

**🔴 我不推断其语义 —— 那 18 字节 sense 内容我没有解码，按 CMD 保持为原始观察。**

---

## 3. 逐项核对

### 3.1 IMAGE_IDENTITY ✅

| 项 | 值 |
|---|---|
| boot.img | 23003136 B / `fd6e19ac...` |
| loader | 47952 B / `9d2c5fb9...` |
| payload | 19534232 B / `c6e9b51e...` |
| kernel | 15339768 B / `5a85ac7f...` |

**刷前独立复算全匹配。`fastboot flash` rc=0，`Sending/Writing OKAY`，reboot rc=0。**

### 3.2 READ_SERVICE_CAPTURE ✅

**231 字节运行时参数**（含 `mtk_ufs_service=1`、`mali_trial=719`）。
**service banner**、**fresh 719 guard**（`magic=0x554c5345 version=1 trial=719 state=0`）。

**🔴 不是 boot-note-only —— 是真实运行时服务捕获。**

### 3.3 VFS_READ_SERVICE ✅

| 项 | 实测 |
|---|---|
| 节点 | `/dev/mt6895-ufs-lba1 major=30 mode=0400` ✅ |
| vnode lookup | **error=0** ✅ |
| **读次数** | **恰好 3** ✅ |
| 每次 error/residual | **0 / 0** ✅ |
| 每次 crc32 | **`0x28a2836c`** ✅ |

**🔴 这是**内核 VFS 调用**，不是用户态 read(2)**，**也不是持久整盘块设备。**

### 3.4 READ_BLOCK_COMPARE ✅

**三次全部：**

| 项 | 值 |
|---|---|
| service result | **0** ✅ |
| engine | **READ_OK 5** ✅ |
| claimed / issued / memory_restored / delivered | **1 / 1 / 1 / 1** ✅ |
| **response_valid / attempted** | **`0x1fff` / `0x1fff`** ✅ |
| **OCS / failures** | **0 / 0** ✅ |
| diagnostic / block CRC | **`0x28a2836c`** ✅ |

**🔴 这是 **CRC32 一致**，不是 SHA256 或数学字节相等。sense 18 字节语义未验证。**

### 3.5 WRITE_OPEN_REJECTED ✅

`write-open error=30 expected=30 requests_before=0 requests_after=0`

**→ `EROFS` 且**计数器前后都是 0**，写打开**未消耗任何请求预算**，也没有发出命令。**

### 3.6 EOF_WITHOUT_COMMAND ✅

`vfs eof error=0 residual=4 requests_before=3 requests_after=3`

**→ EOF 时 `residual=4`（请求 4 字节，得 0），计数器**前后都是 3**，EOF **未产生新命令**。**

### 3.7 LEASE_HELD_BETWEEN ✅

**三次请求后 `control=0x2`（IDLE）、`shared=1`（CLAIMED）。**
**`selftest complete` 确认 `requests=3 issued=3 delivered=3 control=0x2 shared=1`。**

**🔴 每次请求的内存恢复（`memory_restored=1`）**不等于**全局租约归还 ——
`control` 是**缓存 IDLE**，`shared` 仍是 **CLAIMED**。**

### 3.8 IDLE_RETURN_RELEASE ✅

**HLOG **末尾**唯一的 loader 审计（第二 span）：**

```
mt6895-ufs-service: return-gate magic=0x554c5345 version=1 trial=719 state=2 allowed=1
```

**→ `state=2`（RESTORED）、`allowed=1`。**

**🔴 按 CMD：审计是 **loader 生成的第二 span**，所以 **HLOG 正文不完全是内核生成的**。
且**一次 save 调用可能产生多次物理 WRITE(10)**。**

### 3.9 NORMAL_RETURN ✅

| 项 | 值 |
|---|---|
| len | **496764** |
| **reason** | **`0x0`** |
| header sum | `0x26e4525` |
| **完整 sum(body)** | **`0x26e4525`** |
| 相等 | ✅ |

### 3.10 STOCK_RECOVERY ✅（自动）

| 步 | 结果 |
|---|---|
| `adb reboot bootloader` | rc=0 未超时 |
| fastboot transport | **+6 秒** |
| stock 身份复查 | md5 `a63b2e35...` ✅ |
| **stock flash** | **rc=0，2.23 秒，OKAY** |
| reboot | rc=0 |
| Android 就绪 | **28 秒** |

**恢复后：slot `_a`、boot_completed 1、SELinux `Enforcing`、
**boot_a 全分区 MD5 `a63b2e35a7c2522cb24a6a10332b9bc8`**、
新 boot_id `61d07dda-534d-4dfb-b1a8-6cd07de3c272`、SurfaceFlinger 正常。**

**→ 全程自动，未需要用户手动兜底。**

---

## 4. 时间线

| 事件 | 时刻 |
|---|---|
| **reboot**（`reboot.meta`）| **19:31:49.153192** |
| **adb 可用** | **reboot + 218 s** |
| 抓 HLOG | **19:35:33** |
| 开始恢复 | **19:35:39** |

**→ 从 reboot 到抓 HLOG 约 **224 秒**，在 300 秒预算内。**

---

## 5. 原始件

| 文件 | 大小 | SHA256 |
|---|---|---|
| **`raw/trial719-logbook.bin`** | **4198400** | **`70c459167ba308d0fd28e0400292ec25029f8e9156299ca56810b7fa6403d9bb`** |
| **`raw/trial719-logbook-body.bin`** | **496764** | **`db4e7caf86a3a1452139d3406db062a246e6d533de7368ecfec412cd6d85e568`** |

**pre-logbook 留本地（8388608 B / `30ffe3335a2c58c8...`，旧记录 `mali_trial=718`，无 719 证据）。**

---

## 6. 我的意见

### 6.1 十项标签全 PASS，无阻塞

**这是 716→717→718→719 四轮里第一次**全部标签 PASS + 三次请求全成功 + 服务层完整走通**。**

### 6.2 🔴 我必须强调的一条

**三次响应的 `response_words` **不同**（段长 20/20/0，sense 18/18/0），
但三次块 CRC32 **都是 `0x28a2836c`**。**

**→ 这说明：**
- **数据内容一致**（CRC32 相同）
- **但响应包结构随请求变化**

**我不推断为什么第三次没有 sense —— 可能是设备缓存了某种状态，
也可能是别的原因。**没有证据**。**

**按 CMD，我只记录原始观察，不解释。**

### 6.3 这次通过不能推出什么

**🔴 不声称**：整盘块设备、文件系统、吞吐、链路速率/gear/lane、全局队列所有权、713 修复。

**716/717 的 NOT_PROVEN 保持原样；718 的 PASS 不变。**

### 6.4 一个边界提醒

**CMD 明确「These are kernel VFS calls, **NOT userland read(2) evidence** or persistent
whole-disk block support」—— 我据此不声称任何用户态可访问性。**

---

## 7. 边界

**未做**：加试、改判据/白名单/几何/超时、MMIO 扩展、
clock/reset/UIC/watchdog/OEM expdb、用户态 VFS 额外调用。

**设备已恢复原厂。**
