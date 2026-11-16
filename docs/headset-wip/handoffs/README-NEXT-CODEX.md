# 给下一位 Codex 的入口（DSH 补充）

---

## 🔴 当前状态入口（2026-11-16，先读这个）

**最新接续文件**：
**[`HANDOFF-CODEX-20261116-PROFILE4-HOST-READY.md`](HANDOFF-CODEX-20261116-PROFILE4-HOST-READY.md)**
（9564 B，SHA256 `a7437ff32b50a5d5eb0388f7ca55d216822f54afb35c4c637ef259de51216ec5`）

**配套证据**：
**[`../raw/verify-reader-host-02/`](../raw/verify-reader-host-02/)**
（三层主机结果：内核指令窗口模型 15 例、C 源码测试 48 例 / 287 断言、ARM64 ELF syscall 模型 60 例；
原件负对照恰好 3 例 close 错误）

**当前阶段**：**主机准备收口，设备未放行。**

| 已完成（host） | 未验证 |
|---|---|
| profile4 驱动（`headset=4`）与 host kernel `cac498da…` | **当前 kernel/module 与运行实例一致性** |
| 定点读取器修正版 ELF `4232 B / 63688efc…` | **播放时真实 trim 值** |
| 实际备份内核语义（build-id `76cc81c2…`、15 例窗口模型） | **电气结果、出声** |
| 三层分离的验证证据 | **工具是否已在手机可用** |

**🔴 这不是 hardware PASS，也不是设备放行。**
后续设备 CMD 须由 Codex 另发，且**先核验运行 kernel/module 身份与播放/元数据前提**，
再由用户在手机旁配合。

### 历史记录（保持原样，不再改写）

| 文件 | 性质 |
|---|---|
| `HANDOFF-CODEX-20261115-TRIAL705-STOP.md` | **历史记录** —— 705 时的状态与寄存器语义 |
| `../HANDOFF-2026-11-15-2200-DSH.md` | **历史记录** —— 当时的流程与设备状况 |
| 各 `trial-69x` ~ `trial-70x` 目录 | **历史试验记录**，FAIL 一律保持原判 |

**后续改动按上面那份 2026-11-16 接续文件及其链接的审定证据阅读，
不要把 705 那份当当前状态。**

**五条红线、角色分工、历史 FAIL 与 709 源链 `INSUFFICIENT` 全部保持。**
`GPUEB-INTEGRATION.patch` 的既有 WIP 不被本文件覆盖。

---

> **以下正文只讲一件事：怎么指挥 DSH。**
> 内容未改；原来的三行头部引用已移到上面的「历史记录」表里。
> 状态与寄存器语义的**当前**版本看上面的 2026-11-16 接续文件；
> `HANDOFF-CODEX-20261115-TRIAL705-STOP.md` 与 `../HANDOFF-2026-11-15-2200-DSH.md`
> 作为历史记录保留。

---

## 0. 你的角色

**你决策，DSH 执行。** 这是 2026-11-15 22:05 用户定的分工，依据是当天数据：

| DSH | 数据 |
|---|---|
| 6 份技术文档被修正 | **15/12/9/8/7/6 处** |
| 但 9 次刷机 | **零变砖** |
| 7 轮实验打包 | **零错** |
| 判据工具自检 | **全过** |

**所以：二进制/寄存器/时序/判据你定；搜集/打包/刷机/取证/归档我做。**

规则全文：`../COLLAB-MODE.md`

---

## 1. 怎么给 DSH 下指令

**方式**（Orca 终端，句柄会变，先查）：

```bash
source ~/.dsh/env.sh
orca terminal list                      # 找 DSH 那个 terminal
orca terminal send --terminal <DSH句柄> --text '[CMD] ...' --enter
```

**DSH 的句柄**：`term_67e563b9-0361-4d66-ab3b-c0e072ae63b6`（若失效用 `orca terminal list` 查）

### 指令必须含四要素

```
[CMD] 做 trial 706：<一句话说明改什么>

判据：
  - 期望日志行原文
  - 期望寄存器值
  - 什么算证据不足

边界：
  - 这个实验不能证明什么

风险：
  - 最坏情况（会不会挂死）
  - 恢复方式
```

**少了「风险」这一项，DSH 会回头问你。**

---

## 2. DSH 会做什么、不会做什么

### 会做

| 动作 | 说明 |
|---|---|
| 找资料、下源码、核验内容真实性 | 今天 9 个主线源码全部 HTTP 200 + SPDX 头 + 关键符号校验 |
| 实验打包 | 单变量保证、payload hash 链、boot-args 精确 diff |
| 刷机 + 取日志 + 恢复原厂 | 已 9 次，每次验证 md5 |
| 日志取证 | 提取、`sum` 校验、header 字段解析、grep 判据 |
| 把你给的判据写成可执行脚本 + 自检 | `judge-701.py` 自检 5/5、`accdet-check.py` 自检 4/4 |
| 归档、commit、push | |

### 不会做（即使你要求）

| 红线 | 原因 |
|---|---|
| **时钟未确认前读 `0x13c40000`** | 访问永不完成，看门狗复位板子，**无法恢复** |
| `fastboot oem expdb` | 杀 USB endpoint，只有物理插拔能救 |
| 黑屏后长按电源键 | PMIC 冷复位，DRAM 不刷新 |
| 从脏树整包构建后刷机 | 多变量混杂，失败无法归因 |
| 未报备就刷机 | 用户要有心理准备 |

**遇到这类指令，DSH 会说明风险并请求确认，不照做。**

---

## 3. 回报格式

DSH 完成后会发这样一条：

```
[DONE] trial 706
镜像 <md5>、内核 <sha256>
boot-args 唯一改动：<diff>
实测：<关键日志行原文>
判据结果：逐条对照
未验证：<边界>
设备：已恢复原厂 <md5>
```

**读它回报时的注意点**：

| 它说的 | 可信度 |
|---|---|
| 镜像 md5、内核 sha256、payload 组成 | **高**（机械可验证） |
| 日志行原文 | **高**（可直接 grep） |
| `sum` 校验、`reason` 字段 | **高** |
| 设备已恢复、md5 验证 | **高** |
| **「这说明...」类推断** | **低** —— 那是它今天的错误来源，请自己重新推导 |

---

## 4. 前置知识（跳过会踩坑）

### 4.1 必须用隔离树构建

```bash
XNU_SRC="$PWD/logs/msgbuf-1m-20261115/kernel-src" ./tools/make-kernel.sh -j8
```

**不要从当前脏树整包构建**（含 GPUEB 未成功的东西）。
**判 `rc=0` 即可** —— 222 条 `exported name not in import list` 是历史既有 kext 符号集失败。

### 4.2 logbook 判读

| 现象 | 含义 |
|---|---|
| `reason = 0xfeedface` + `len = 735` | **loader 预写，内核没回来** |
| `reason = 0x0` | **正常返回** |
| 日志以 `mtkloader:` / `boot args:` / `clock:` 开头 | loader boot note，**不是 XNU 输出** |
| 以 `cpu 0 (phys id 0x0) is a slower core` 开头 | XNU 正常输出 |

字段位置：`HLOG @ 0x9f8f8000`，`len@8`、**`reason@16`**、`sum@64`，body 在 `+4096`。

### 4.3 Android 每次都会卡住

已连续 **九次**：`surfaceflinger` 崩 38 次、`boot_completed` 未置位。
**用户手动回 fastboot 是标准流程**，不是异常。

### 4.4 代理

```bash
# 判断通道用端点，不要用 Clash 延迟测试（会误报）
curl -w "%{http_code}" https://chatgpt.com/backend-api/codex/responses
# 405 = 通
```

**断线处置**：`Reconnecting N/5` 是网络流断，不是会话失效。换节点 + 发 `ping`。

### 4.5 额度

| 账号 | 状态 |
|---|---|
| `orca:2a3f54a2` | 13%，reset 01:44 |
| **`system`** | **100%** ← 用这个 |

查看：`~/.local/bin/codex-quota`

---

## 5. 705 之后最可能的三条路

**这是 DSH 的猜测，你该自己判断：**

| # | 方向 | 依据 |
|---|---|---|
| 1 | 修 `0x260b` 判据（低五位而非全字节）后复跑 `headset=3` | 你的前一位已承认那是它的代码问题 |
| 2 | 打开 `CLKSQ`/`NCP` 后再跑，看 `0x260b` 是否仍写不进 | 705 记录了 `CLKSQ=0x00` |
| 3 | 转 AFE → I2S 路由（出声的真正门槛） | `grep mt6368 hakios.sound.c` = 0 命中 |

**GPU 线也有活**：GPUEB 时钟门控确认（读 `infracfg`/`topckgen`，不碰 GPUEB 块）。
