# UFS 读路径交付 2026-11-17

> DSH。**仅主机归档。设备已验证原厂，无加试。**

---

## 结论

**718 首次 `READ_OK(5)`**：固定 `READ(10)` / LUN 2 / 逻辑 LBA 1 / 4096 字节，
响应完整接受，块 CRC32 与 Android 独立参考一致，租约干净归还，运行时 HLOG 正常返回，
stock 自动恢复。

**🔴 范围有限**：这是**一次单块读取**的成功观测。
**不表示**有块设备、可挂载文件系统、持续存储服务、吞吐或链路速率结论。

---

## 三轮判定表

| 轮次 | 结果 | 能断言 | **不能**断言 |
|---|---|---|---|
| **716** | `COMPLETED_ERROR(3)` | **控制器报告完成**、OCS 0、租约归还、正常返回 | **具体被拒字段未知** —— 716 **没有采到任何响应词**；数据**未**经任何比对验证 |
| **717** | `COMPLETED_ERROR(3)` | **717 自身** `failures=0x20` = bit5，段长 20 | 不能代表 716 |
| **718** | **`READ_OK(5)`** | 修订后的有界解析器接受（段长 0/20、sense ≤18、EHS 0） | 不能回溯改变 716/717 |

**🔴 716 的旧 `NOT_PROVEN` 保持不变。718 的成功不重写 716。**

---

## 🔴 响应布局更正

**我先前把 `byte8` 说成「flags 高位区（含 EHS）」—— 错的。**

**正确字段序（`utp_upiu_header`）：**

| 字节 | 字段 |
|---|---|
| 0 | transaction code |
| **1** | **flags** |
| 2 / 3 | LUN / tag |
| 6 / 7 | response / status |
| **8** | **EhsLen（独立字段，total EHS length）** |
| 10..11 | data segment length（大端） |
| 12..15 | residual |
| 32..33 | sense-data length（大端） |
| 34..51 | sense data（18 字节） |

**→ 718 判据里的 `r[8] != 0` 正是拒绝未实现的扩展头。**

---

## 主机检查规模

| 项 | 数量 |
|---|---|
| **engine 检查** | **32,513**（ASan+UBSan 与 MSan 各自）|
| contract | 983 |
| atomic | 40 |
| loader guard | 23 |
| **718 日志解析器合成案例** | **58** |
| 故障注入位置 | reads 5530 / regs 49 / writes 2908 |

**全部为**主机模型或合成检查**，不是硬件证据。**

---

## 构建判定

| 项 | 值 |
|---|---|
| **make native rc** | **2**（既有 kext symbol-set 允许项）|
| **judge native rc** | **0**（显式 `KERNEL=`）|

**🔴 两者分开记录，不合并。**

**native make 返回非零 2；内核产物已生成，显式 judge 只豁免既有 symbol-set 并返回 0。**

---

## 源码入口

**目录：[`raw/final-ufs-source-718/source/`](raw/final-ufs-source-718/source/)**

**直接入口：**

| 内容 | 链接 |
|---|---|
| **源快照说明与复现命令** | [`source/README.md`](raw/final-ufs-source-718/source/README.md) |
| **23 项清单（JSON）** | [`source/SOURCE-MANIFEST.json`](raw/final-ufs-source-718/source/SOURCE-MANIFEST.json) |
| **23 项校验值** | [`source/SOURCE-MANIFEST.sha256`](raw/final-ufs-source-718/source/SOURCE-MANIFEST.sha256) |
| **718 试验结果** | [`device-ufs-read-718/RESULT-TRIAL718.md`](raw/device-ufs-read-718/RESULT-TRIAL718.md) |
| **718 设备记录全文** | [`device-ufs-read-718/README.md`](raw/device-ufs-read-718/README.md) |
| **源码改动集** | `raw/final-ufs-source-718/source/` |

| 类 | 数 |
|---|---|
| 源文件 | **19** |
| 冻结元数据 | **4** |
| 清单 | 3（`SOURCE-MANIFEST.json` / `.sha256` / `README.md`）|
| **MANIFEST 条目** | **23** |

**复制前后各验一次 23/23 字节与 hash，提交后再验 git blob。**

### 🔴 这不是完整的独立内核树

**23 个文件是**UFS 读路径相关的改动集**，**不是 5210 文件的完整 XNU 树**。**

**完整输入清单见 `final-ufs-source-718/source/metadata/kernel-inputs.sha256`（5210 项），
原始工作区为 `/home/lizi/IOS/hakios-mtk/logs/ufs-response-20261117/kernel-src`。**

---

## 相关归档

| 阶段 | 路径 |
|---|---|
| **主机审查/构建** | `raw/ufs-response-718/` |
| **打包审查** | `raw/pack-ufs-response-718/` |
| **设备试验** | `raw/device-ufs-read-718/` |
| **前序（716/717）** | `raw/ufs-read-integration-716/`、`raw/ufs-read-diagnostic-717/`、`raw/device-ufs-read-716/`、`raw/device-ufs-read-717/` |
| **更早（引擎 v1/v3）** | `raw/ufs-engine-review-716/`、`raw/ufs-engine-review-v3-716/` |

---

## 当前设备状态（已验证）

| 项 | 值 |
|---|---|
| slot | `_a` |
| boot_completed | 1 |
| SELinux | `Enforcing` |
| **boot_a 全分区 MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** |
| **boot_id** | **`f745b1c6-ee43-4abc-8ed2-06ee9629bf27`** |
| SurfaceFlinger | 正常 |

**🔴 恢复方式：自动（未使用手动 fastboot 兜底）。**

---

## 仍未完成的工作

| # | 项 |
|---|---|
| 1 | **持久块设备** —— 当前只有一次单块读取，没有 block device 接口 |
| 2 | **文件系统挂载** —— 未实现，未验证 |
| 3 | 持续存储服务 / 队列长期所有权 |
| 4 | 链路速率 / gear / lane 结论 |
| 5 | 吞吐测量 |
| 6 | **716 的具体拒绝字段** —— 从未被观测 |
| 7 | sense 18 字节的语义 —— 本轮**原样记录不解释** |

---

## 边界

**未做**：生产 merge 或 push、镜像二进制上传、raw GPT 或磁盘 GUID 上传、
ignore 规则改动、GPUEB WIP staging、旧 BUILD/产物覆盖。

**源码快照本身不构成可直接构建的独立树。**
