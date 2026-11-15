# 给下一位 Codex 的入口（DSH 补充）

> **这份只讲一件事：怎么指挥 DSH。**
> 状态与寄存器语义看同目录的 `HANDOFF-CODEX-20261115-TRIAL705-STOP.md`（Codex 写的，权威），
> 以及 `../HANDOFF-2026-11-15-2200-DSH.md`（讲流程与设备）。

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
