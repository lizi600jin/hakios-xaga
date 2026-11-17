# UFS 容量查询交付 2026-11-17（720）

> DSH。**仅主机归档。设备已验证原厂，无加试、无查询。**

---

## 结论

**720 用**恰好一条**内核 `READ CAPACITY(10)` 取到了 **LUN 报告的逻辑几何**，
与 Android 块层历史记录**吻合**，租约干净归还，设备自动恢复原厂。**

**🔴 范围有限**：单条固定命令、8 字节数据、固定 LUN2。
**没有**变量 LBA、整盘块设备、用户态接口、文件系统或性能数据。**

---

## 实测（HLOG 原文）

```
mt6895-ufs-capacity: single RC10 lun2 data_bytes8 scratch_bytes4096; guarded handoff required
mt6895-ufs-capacity: guard magic=0x554c5345 version=1 trial=720 state=0
mt6895-ufs-capacity: opened baseline_verified=1 control=0x2 shared=1
mt6895-ufs-capacity: result=5 claimed=1 issued=1 memory_restored=1 query_valid=1 polls=279
mt6895-ufs-capacity: ocs=0 response_valid=0x1fff attempted=0x1fff failures=0
mt6895-ufs-capacity: raw valid=0x3 attempted=0x3 changed=0x3 ffafdc01 00100000
mt6895-ufs-capacity: tail complete=1 ok=1 checked_words=1022 first_bad=0xffffffff
mt6895-ufs-capacity: decode=0 last_lba=0x1dcafff block_bytes=4096 blocks=31240192 capacity_bytes=127959826432 android_match=1
mt6895-ufs-capacity: final control=0x2 shared=1 idle_close_deferred=1
mt6895-ufs-capacity: return-gate magic=0x554c5345 version=1 trial=720 state=2 allowed=1
```

### 🔴 恰好一次的依据

**这个结论来自**源码结构**，不是只凭 `issued=1` 这个布尔值推次数：**

| 约束 | 位置 |
|---|---|
| **`init` 只跑一次** | 每次 `init` 前检查 `us_control(&lease) != US_NEW` 即 **直接返回**，无 IO |
| **每次 `init` 至多一次 `ring`** | `uc_run` 内只有**一处** `io->ring_once` 调用 |
| **只有一条结果行** | HLOG 里 `result=` 恰好出现 **1 次** |
| **没有任何重试路径** | 引擎无 retry / RC16 / fallback 分支 |

**HLOG 侧证：`issued=1`、`polls=279`，且全文只有一条 `result=` 行。**

**→ 所以是「一条命令、一条结果」，而不是「某个布尔说 1 所以我信」。**

---

## LUN 报告的逻辑几何（从 raw 8 字节直接解码）

| 项 | 值 |
|---|---|
| **lastLBA** | **31240191 = `0x1dcafff`**（大端 32 位）|
| **逻辑块长** | **4096** 字节（大端 32 位）|
| **块数** | **31240192** = lastLBA + 1 |
| **容量** | **127959826432** 字节 = 块数 × 块长 |

**🔴 没有用 CRC32 声称容量字节等同 —— 只直接解码并报告那 8 字节。**

---

## 与 Android 比对：MATCH

| 项 | 720 实测 | Android 块层历史 | |
|---|---|---|---|
| 块长 | **4096** | 4096 | ✅ |
| 容量 | **127959826432** | 127959826432 | ✅ |

**🔴 这是**独立观测的吻合**，不是把 Android 的值代进去。
比对与查询接受、几何解码是**三个独立标签**。**

---

## 内存与租约

| 项 | 值 |
|---|---|
| **尾部 1022 词全部采样** | `checked_words=1022`、`first_bad=0xffffffff` |
| **整块 4096 恢复** | `memory_restored=1` |
| 查询后保持 | `control=0x2`（IDLE）、`shared=1`（**CLAIMED**）|
| 归还 | `return-gate state=2 allowed=1`，**唯一位于 HLOG 末尾** |

**🔴 每次查询的 `memory_restored` **不等于**外层归还；
外层是在 quiesce 时由 `us_stop` 完成（`idle_close_deferred=1`）。**

---

## 正常返回与恢复

| 项 | 值 |
|---|---|
| **HLOG reason** | **`0x0`** |
| **body 长度** | **497300** |
| **`sum(body)`** | **`0x26f2a52`** = header ✅ |
| **恢复方式** | **自动**（未用手动兜底）|
| **boot_a 全 64 MiB MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** |

---

## 源码入口

**目录：[`raw/final-ufs-capacity-source-720/source/`](raw/final-ufs-capacity-source-720/source/)**

| 内容 | 链接 |
|---|---|
| **源快照说明与复现命令** | [`source/README.md`](raw/final-ufs-capacity-source-720/source/README.md) |
| **32 项清单** | [`source/SOURCE-MANIFEST.json`](raw/final-ufs-capacity-source-720/source/SOURCE-MANIFEST.json) |
| **720 试验结果** | [`device-ufs-capacity-720/RESULT-TRIAL720.md`](raw/device-ufs-capacity-720/RESULT-TRIAL720.md) |
| **720 设备记录** | [`device-ufs-capacity-720/README.md`](raw/device-ufs-capacity-720/README.md) |
| **codec 审查** | [`ufs-capacity-codec-720/README.md`](raw/ufs-capacity-codec-720/README.md) |
| **engine 审查** | [`ufs-capacity-engine-720/README.md`](raw/ufs-capacity-engine-720/README.md) |
| **adapter 平台** | [`ufs-capacity-platform-720/README.md`](raw/ufs-capacity-platform-720/v2/DESIGN-ADAPTER720-V2.md) |
| **集成构建** | [`ufs-capacity-integration-720/README.md`](raw/ufs-capacity-integration-720/README.md) |
| **验包** | [`pack-ufs-capacity-720/README.md`](raw/pack-ufs-capacity-720/README.md) |

**35 文件 / 32 清单条目 = 6 候选与测试 + 4 内核集成 + 4 loader + 6 元数据 + 12 运行时 + 3 清单。**

### 🔴 这不是完整的独立内核树

**32 条目是**容量查询相关改动集**，**不是 5220 文件的完整 XNU 树**。
完整输入清单：`metadata/kernel-inputs.sha256`。**原始工作区**：`/home/lizi/IOS/hakios-mtk/logs/ufs-capacity-20261117/kernel-src`。**

### 🔴 Host 与 Runtime 的 support 不可混用

**`support/` 是 **719 cookie**，仅供**主机模型**。
`runtime/support/` 是 **720 cookie**，与新 loader 匹配。
**不要把 host 头文件塞进内核。**

---

## 构建（两条 rc 分开）

| 项 | 值 |
|---|---|
| **make native rc** | **2**（config 阶段 `Private.symbolset` 的 `_uat_get_desc`）|
| **judge rc** | **0**（`tools/make-kernel.sh --judge` 判断日志）|
| **内核** | **15338680 B / `179efc781d17b41721a2d9cd5e83fb23d9b279f44c253aff27ddc00604bca19f`** |

**🔴 必须区分：**

- **make rc=2 是既有的 kext symbol-set 允许项**，**不是**编译失败。
- **judge rc=0 的含义是「日志显示内核已构建，只有 kext 符号集检查失败」**。
- **🔴 我先前用 `./xnu-build.sh judge` 调错了一次**（该脚本无 judge 子命令），
  那份失败记录保留为 `judge-WRONG-INVOCATION-*`。**那不是编译器失败。**
- **我不声称 make 成功。**

---

## 四轮演进

| 轮次 | 结果 |
|---|---|
| **716** | `COMPLETED_ERROR(3)`，字段未知 |
| **717** | `COMPLETED_ERROR(3)`，定位 bit5 / 段长 20 |
| **718** | **`READ_OK(5)`**，单次成功 |
| **719** | **`READ_OK` × 3**，服务层三次全成功 |
| **720** | **`QUERY_OK(5)` × 1**，容量查询成功并 MATCH |

**前序交付：[`UFS-DELIVERY-20261117.md`](UFS-DELIVERY-20261117.md)（718）、
[`UFS-SERVICE-DELIVERY-20261117.md`](UFS-SERVICE-DELIVERY-20261117.md)（719）—— **保持原样**。**

---

## 仍未完成

| # | 项 |
|---|---|
| 1 | **变量 LBA / 多块读取** —— 720 是单条固定命令 |
| 2 | **整盘块设备** —— 无 `bdevsw` |
| 3 | **用户态接口** —— 无设备节点 |
| 4 | **文件系统挂载** |
| 5 | **性能 / 链路速率 / gear / lane** |
| 6 | **全局独占** —— 仍是前提，未被「已知路径缺席」证明 |

---

## 边界

**未做**：额外查询、设备检查、构建、打包、源码编辑或推送、
镜像/raw GPT/GUID/pre-log/reference 上传、ignore 改动、
禁止的 git 命令、GPUEB WIP staging。

**🔴 不声称**整盘、用户态、文件系统、性能、全局所有权。
**716/717 NOT_PROVEN 与 718/719 PASS 不变。**
