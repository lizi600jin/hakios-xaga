# UFS 服务交付 2026-11-17（719）

> DSH。**仅主机归档。设备已验证原厂，无加试、无查询。**

---

## 结论

**719 首次实现**三次成功的内核 VFS 读取**，全部命中固定 `READ(10)`/LUN2/LBA1/4096 字节，
三次都 `READ_OK`、块 CRC32 均为 `0x28a2836c`、租约跨请求保持、返回时干净释放、
stock 自动恢复。

**🔴 范围有限**：这是**内核 VFS 调用**，**不是用户态 read(2)**，**不是持久整盘块设备**。
**固定一块、全局预算 3，用完即止。**

---

## 719 实际行为（实测）

### 服务注册

```
node registered path=/dev/mt6895-ufs-lba1 major=30 mode=0400 snapshot_bytes4096
opened baseline_verified=1 control=0x2 shared=1
```

**字符设备、权限 0400、虚拟 4096 字节快照 —— **不是** `D_DISK`/`bdevsw`。**

### 三次 VFS 读取

| 请求 | polls | result | engine | crc32 | vfs error/residual |
|---|---|---|---|---|---|
| 1 | 122 | **0** | **5** | **`0x28a2836c`** | **0 / 0** |
| 2 | 119 | **0** | **5** | **`0x28a2836c`** | **0 / 0** |
| 3 | 117 | **0** | **5** | **`0x28a2836c`** | **0 / 0** |

**每次都 `claimed=1 issued=1 memory_restored=1 delivered=1`、
`response_valid=attempted=0x1fff`、`ocs=0 failures=0`。**

### 写打开与 EOF 均不产生命令

```
write-open error=30 expected=30 requests_before=0 requests_after=0
vfs eof error=0 residual=4 requests_before=3 requests_after=3
```

**→ `EROFS 30` 拒绝写，**计数器前后都是 0**；
EOF `residual=4`，**计数器前后都是 3**。两者**都未消耗预算、未发命令**。**

### 持租与归还

```
selftest complete requests=3 issued=3 delivered=3 control=0x2 shared=1; idle close deferred to quiesce
return-gate magic=0x554c5345 version=1 trial=719 state=2 allowed=1
```

**→ 三次后仍是 `control=0x2`（缓存 IDLE）、`shared=1`（**CLAIMED**）；
quiesce 后才变为 `state=2`（RESTORED）、`allowed=1`。**

**🔴 「每次请求的内存恢复」与「外层租约归还」是**两件事**，
前者每次都为 1，后者只在 quiesce 发生。**

---

## 🔴 两处必须读准的表述

### 1. 只有 CRC32 一致，不是内容相等

**三次 4096 字节数据的 **CRC32 均为 `0x28a2836c`**；
**未逐字节或 SHA256 验证内容相等**。**

**CRC32 是 32 位校验和 —— 不同数据得到同一值在原理上可能。**

### 2. 响应词索引是零起始

**三次 `response_words` **并不相同**：**

| 零起始索引 | request=1 | request=2 | request=3 |
|---|---|---|---|
| `[2]` | `0x14000200` | `0x14000200` | `0x00000000` |
| `[8]` | `0x10801200` | `0x10801200` | `0x00000000` |
| `[9]` | `0x00010001` | `0x01000001` | `0x00000000` |

**解码：request 1/2 段长 **20**、sense 长度 **18**；request 3 段长 **0**、sense 长度 **0**。**

**🔴 我不推断 request 3 为何用段长 0 —— 没有任何证据。**

---

## 源码入口

**目录：[`raw/final-ufs-service-source-719/source/`](raw/final-ufs-service-source-719/source/)**

| 内容 | 链接 |
|---|---|
| **源快照说明与复现命令** | [`source/README.md`](raw/final-ufs-service-source-719/source/README.md) |
| **29 项清单** | [`source/SOURCE-MANIFEST.json`](raw/final-ufs-service-source-719/source/SOURCE-MANIFEST.json) |
| **29 项校验值** | [`source/SOURCE-MANIFEST.sha256`](raw/final-ufs-service-source-719/source/SOURCE-MANIFEST.sha256) |
| **719 试验结果** | [`device-ufs-service-719/RESULT-TRIAL719.md`](raw/device-ufs-service-719/RESULT-TRIAL719.md) |
| **719 设备记录** | [`device-ufs-service-719/README.md`](raw/device-ufs-service-719/README.md) |
| **主机审查** | [`ufs-service-integration-719/README.md`](raw/ufs-service-integration-719/README.md) |
| **验包** | [`pack-ufs-service-719/README.md`](raw/pack-ufs-service-719/README.md) |
| **解析器审查** | [`ufs-service-parser-719/README.md`](raw/ufs-service-parser-719/README.md) |

**32 文件 = 17 候选/测试 + 4 内核集成 + 4 loader + 4 元数据（29 清单条目）+ 3 清单文件。**

### 🔴 这不是完整的独立内核树

**29 条目是**UFS 服务相关改动集**，**不是 5214 文件的完整 XNU 树**。
完整输入清单见 `metadata/kernel-inputs.sha256`，原始工作区为
`/home/lizi/IOS/hakios-mtk/logs/ufs-service-20261117/kernel-src`。**

---

## 构建与检查规模

| 项 | 值 |
|---|---|
| **make native rc** | **2**（既有 kext symbol-set 允许项）|
| **judge native rc** | **0**（显式 `KERNEL=`）|
| 核心 8 引擎 | 34121 |
| 并发调度 | 2500 |
| 服务 shim | 16 场景 |
| 返回快照/格式 | 23481 / 36894 |
| span writer | 87 |
| **解析器合成案例** | **108** |

**全部为**主机模型或合成检查**，不是硬件证据。**

---

## 当前设备状态（已验证）

| 项 | 值 |
|---|---|
| slot | `_a` |
| boot_completed | 1 |
| SELinux | `Enforcing` |
| **boot_a 全分区 MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** |
| **boot_id** | **`61d07dda-534d-4dfb-b1a8-6cd07de3c272`** |
| SurfaceFlinger | 正常 |

**🔴 恢复方式：自动（未用手动兜底）。**

---

## 四轮演进

| 轮次 | 结果 | 意义 |
|---|---|---|
| **716** | `COMPLETED_ERROR(3)` | 字段未知（无响应词）|
| **717** | `COMPLETED_ERROR(3)` | 定位 bit5 / 段长 20 |
| **718** | **`READ_OK(5)`** | 单次成功 |
| **719** | **`READ_OK` × 3** | **服务层三次全成功** |

**前序交付：[`UFS-DELIVERY-20261117.md`](UFS-DELIVERY-20261117.md)（718 那份，保持原样）**

---

## 仍未完成

| # | 项 |
|---|---|
| 1 | **用户态 read(2)** —— 719 是内核 VFS，无用户态证据 |
| 2 | **变长 LBA / 多次/多块读取** —— 固定一块、全局预算 3 |
| 3 | **持久整盘块设备** —— 无 block device 接口 |
| 4 | **文件系统挂载** |
| 5 | 吞吐 / 链路速率 / gear / lane |
| 6 | **独占前提** —— 服务假定 UFS 由 loader 移交且无其他内核使用者 |
| 7 | **CPU / GPU 遗留问题**（713 CPU 探针、GPU 实验路径）|

---

## 边界

**未做**：生产 merge/push、镜像上传、raw GPT/GUID/reference/pre-log 上传、
ignore 规则改动、GPUEB WIP staging、旧 716/717/718 重写。
