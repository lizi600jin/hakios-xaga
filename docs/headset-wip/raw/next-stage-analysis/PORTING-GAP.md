# 移植缺口：redwood → xaga（DSH 主机侧发现）

> 2026-11-16 20:35，DSH。**仅主机读源码。未改任何文件。**

---

## 🔴 核心发现

**`bsd/dev/` 下 60 个 `hakios.*` 驱动里，23 个的注释仍写着
「POCO X5 Pro (redwood)」，只有 8 个提到 xaga/mt6895。**

**这意味着**：大部分驱动是**从 POCO X5 Pro 移植过来的底子**，
**不是为 xaga 写或验证过的**。

---

## 1. 数字

| 项 | 数量 |
|---|---|
| `bsd/dev/hakios.*` 文件总数 | **60** |
| 提到 **redwood / POCO X5 Pro** | **23** |
| 提到 **xaga** | **8**（其中 2 个同时提 redwood） |
| 两者都不提 | 约 31 |

---

## 2. 提到 redwood 的驱动（23 个）

`hakios.adreno.c`、`hakios.adsp.c`、`hakios.apfs_check.c`、`hakios.battery.c`、
`hakios.bt.c`、`hakios.buttons.c`、`hakios.camera.c`、`hakios.cpufreq.c`、
`hakios.goodix.c`、`hakios.haptic.c`、`hakios.ipa.c`、`hakios.mic.c`、
`hakios.mpss.c`、`hakios.mt6895_keys.c`、`hakios.nvt.c`、…

**含两个关键大件**：
- **`hakios.mpss.c`（250615 B，全树最大）** —— 基带
- **`hakios.cpufreq.c`** —— CPU 调频

---

## 3. 为什么这解释了「logbook 里 0 次」

**`hakios.mpss.c` 头注释原文：**

> "The modem of the **POCO X5 Pro (redwood)**, started, and asked about the network: step 1 of
> cellular. **Every address below belongs to this phone**…"

**「Every address below belongs to this phone」= 属于 redwood，不是 xaga。**

**所以**：
- 基带代码**存在**（250 KB）
- 但**地址是按 redwood 写的**
- **在 xaga 上不能直接用** —— 这也解释了为什么 logbook 里 `baseband` 0 次

**`hakios.cpufreq.c` 同理**：

> "The clock speed … on the **POCO X5 Pro (redwood)**. Only with boot-arg cpufreq=1:
> **the addresses are this phone's**."

---

## 4. 这改变了「下一阶段」的判断

**我之前的判断（见 `STATUS-20261116.md`）说「基带/CLPC/AOP 完全没代码」——
这是错的。代码在，但是 redwood 的地址。**

**正确的表述应该是：**

| 子系统 | 代码 | 地址 | xaga 上可用？ |
|---|---|---|---|
| 基带（MPSS） | ✅ 250 KB | ❌ redwood | **需重定位** |
| CPU 调频 | ✅ | ❌ redwood | **需重定位** |
| speakers / sound | ✅ | ❌ redwood | **需重定位** |
| 音频 codec（MT6368） | ✅ | ✅ **xaga** | **✅ 已验** |
| mali610 | ✅ | ✅ xaga | 部分 |
| keys / usb / nvt | ✅ | ✅ xaga | 已验 |

---

## 5. 这意味着下一阶段有两条根本不同的路

### 路 A：把 redwood 驱动重定位到 xaga

**做什么**：为 23 个 redwood 驱动逐个查 xaga 的真实地址（从设备树、`/proc/iomem`、
`lk_a.img` 的 FDT —— 与 `board.h` 用的同一套一手来源），改地址，逐个验证。

| 项 | 评估 |
|---|---|
| 工作量 | **大**（23 个驱动 × 每个几十到几百个地址） |
| 风险 | 中（地址错会挂） |
| 产出 | 基带、调频、扬声器、摄像头等**逐个活过来** |
| 方法论 | **已有成熟流程**（`BOARD.md` 记了怎么从 LK/设备树取地址） |

### 路 B：只聚焦一个子系统做深

**比如先把 `hakios.cpufreq.c` 的地址换成 xaga 的**，验证 CPU 调频能工作。

| 项 | 评估 |
|---|---|
| 工作量 | 小（一个驱动） |
| 产出 | **CPU 频率可控 —— 这对性能和功耗影响最大** |
| 价值 | 为其他驱动重定位**建立可复用的验证流程** |

---

## 6. 我的建议

**先做 `hakios.cpufreq.c`（路 B 的最小版本），因为：**

1. **它是单点可控的** —— 一个驱动、一组寄存器
2. **效果可测** —— 频率变化能从 logbook 或计时验证
3. **为其余 22 个驱动建立流程** —— 地址怎么查、怎么换、怎么验
4. **对整体性能影响最大** —— CPU 频率固定会让一切都慢

**然后再考虑基带（mpss，最大件）** —— 但那需要先解决固件加载
（modem.mdt/b00-b29 从 `vendor/firmware_mnt`）。

---

## 7. 需要 Codex 判定

1. **我数错了吗？** 23/60 这个比例对不对
2. **`hakios.cpufreq.c` 在 xaga 上需要换多少个地址？**
3. **xaga 的 EPSS 寄存器地址从哪取最可靠**（设备树？`lk_a.img`？Android `/proc/iomem`？）
4. **有没有已经被前人做过但没记录的 xaga 适配**
5. **哪条路更符合当前进度**

---

## 8. 边界

**本文只做了 `grep -l` 与文件大小统计，没有读驱动内部逻辑。**
**「redwood」出现只说明注释写着 redwood，不等于地址一定错了** ——
有些驱动可能注释没改但地址已改（`hakios.mt6895_keys.c` 同时出现在两个列表里）。
