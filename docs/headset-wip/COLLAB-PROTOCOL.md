# 协作协议 v2 — DSH 主控 × Codex 6.1 Sol

> **状态：✅ 双方已确认定稿（2026-11-15 00:12）。**
> v1 由 DSH 起草（2026-11-14 23:36）→ Codex 提 5 条修改 → DSH 全接受 + 追加 4 条 → Codex 对其中的第 4 条再改写 → DSH 采纳，定稿。
> **每个新 codex 会话启动后：① 读本文件 ② 读 `MASTER-STATE.md`。** 换账号不改变分工。

---

## 0. 一句话

**按职责分工，不按能力评级。** DSH 负责调度、扇出调研、独立复核、文档、仓库；Codex 负责生产代码、寄存器语义推理、架构决策、构建与实机验证。

---

## 1. 职责分工

| 职责 | 主责 | 说明 |
|---|---|---|
| 任务拆解、优先级、风险把控 | **DSH** | |
| 并发调研（多路扇出） | **DSH** | 并发数按任务需要，不设固定值 |
| 独立复核（重跑构建/重读寄存器/跑对拍） | **DSH** | 不可跳过，见 §2 |
| 文档、索引、仓库提交与推送 | **DSH** | |
| 设备状态跟踪、恢复方案 | **DSH** | 掌握风险清单与历史事故 |
| 生产代码实现 | **Codex** | |
| 寄存器语义推理、位域分析 | **Codex** | |
| 架构决策 | **Codex** | 与 DSH 讨论后定 |
| 构建与实机验证 | **Codex** | |
| 源码核查、调研、证据记录 | **双方** | 为实现所必需时，Codex 可自行核查源码与调研，不必等 DSH |

**不设能力评级。** 双方各有擅长的场景，写死评级会导致错误决策。

### 1.1 文件归属（防止编辑冲突）

| 范围 | 谁能改 |
|---|---|
| `/home/lizi/IOS/hakios-mtk/**`（构建树，含 49 个未提交改动） | **只有 Codex** |
| `/home/lizi/IOS/repo/hakios-mt6895/docs/**`（文档仓库） | **只有 DSH** |
| `/home/lizi/IOS/repo/hakios-mt6895/` 的 git 提交与推送 | **只有 DSH** |

一方需要改对方范围的文件时，**发请求**，不要直接改。理由：`hakios-mtk` 有 49 个未提交改动，两人同时编辑会产生无法归因的冲突。

---

## 2. 标准工作流

```
① DSH 拆解 → 写任务包
        ↓
② 调研（DSH 扇出 或 Codex 自行核查，按需要选）
        ↓
③ 独立复核（谁调研，另一方抽查；关键结论必须真跑一遍）
        ↓
④ 把「已确证事实 + 待决策点」交给 Codex
        ↓
⑤ Codex 实现 + 构建 + 报告
        ↓
⑥ DSH 独立复核（重跑构建 / 重读寄存器 / 跑对拍脚本）
        ↓
⑦ DSH 决定：进仓库 or 打回 ⑤
```

**③ 和 ⑥ 不可跳过。** 任何一方的汇报都**不等于事实**。

### 2.1 任务包必含 5 项

1. **目标**（一句话）
2. **约束**（禁读/禁写地址、风险边界）
3. **源码版本**（commit hash 或 `git cat-file` 取法；sparse clone 必须写明取法）
4. **验收标准**（可执行的命令 + 期望输出，例如 `python3 x.py --selftest` 全过、`sha256` 匹配）
5. **设备操作范围**（是否碰手机、碰哪些命令、谁持有）

---

## 3. 双向请求通道

### 3.0 🔴 主通道：Codex 直接 `orca terminal send` 给 DSH

**Codex 可以主动给 DSH 发消息，不需要等 DSH 轮询。** 已实测可用（2026-11-15 00:08）。

DSH 的终端 handle **固定为**：

```
term_65478f6a-0846-40d4-b817-55c9be904afd
```

（tab `aa6aca61-3efd-47a7-8478-f0eb9d3d3812`，标题「🐋 DSH 主控 v2」）

Codex 在自己的 shell 里执行：

```bash
orca terminal send \
  --terminal term_65478f6a-0846-40d4-b817-55c9be904afd \
  --text "你的消息（纯 ASCII 双引号内，不要嵌套引号）" \
  --enter
```

**这条消息会直接出现在 DSH 的输入框里并被提交，DSH 会立刻看到。**

#### 使用规范

| 项 | 规定 |
|---|---|
| 长度 | 单条 ≤ 500 字符。长的分段发 |
| 引用 | `--text` 里**不能有嵌套的双引号**；要引用代码用单引号 `'...'` 或反引号 |
| 频率 | 需要 DSH 立刻行动时才用。**纯记录用文件信箱** |
| 阻塞 | 发完继续干自己的活，**不要等**；DSH 会在下一轮回复 |
| 前置 | `orca` 已在 PATH（`/home/lizi/.local/bin/orca`），codex 进程已有 `ORCA_*` 环境变量 |

#### 发给 DSH 时要带的前缀

```
[ASK]    要一个决策/答案
[DONE]   一件事做完了，附证据位置
[BLOCK]  卡住了
[OBJ]    反对 DSH 的某个结论
[DEV]    要手机
```

例：
```bash
orca terminal send --terminal term_65478f6a-0846-40d4-b817-55c9be904afd \
  --text "[DONE] 耳机 headset=2 编译通过，产物 sha256 见 logs/xxx, 请复核" --enter
```

### 3.1 DSH → Codex（深挖请求）

DSH 用 `orca terminal send --terminal <codex 的 handle> --text "..." --enter`。
**codex 的 handle 每次换账号都会变**，由 DSH 从 `orca terminal list` 取。

```
[DEEP] <一句话问题>
已知事实：<3 行以内，带文件:行号>
已排除：<避免重复劳动>
期望产出：<文件路径 或 明确结论>
证据边界：<哪些能读、哪些禁读>
```

### 3.2 备用通道：文件信箱（长文本、需留痕、DSH 正忙时）

`docs/headset-wip/bridge/` 下：`to-dsh.md`（Codex → DSH）、`to-codex.md`（DSH → Codex）。
DSH 后台轮询。详见 `bridge/README.md`。

**什么时候必须用文件信箱而不是 `orca terminal send`**：
- 内容 > 500 字符
- 需要 DSH 事后复查（代码片段、寄存器表、长报告）
- DSH 正在长时间任务中，打断代价大

### 3.3 Codex 的轮询义务

Codex **每个回合开始时**读一次 `bridge/to-codex.md` 的末尾，检查有无 `状态: open` 的新段落。


---

## 4. 设备权限：按完整实验独占

🔴 **一个完整实验期间，手机只归一方。持有者负责刷写、取证、恢复全流程；另一方不得穿插任何协议层命令。**

这条是 DSH 在 2026-11-14 23:22 违反后加的：DSH 在 Codex 准备实验时插了一条 `fastboot oem expdb`，该命令把 fastboot 会话留在半死状态（`fastboot devices` 仍列出，但 `getvar`/`reboot` 全部 `usb_read failed`），需要 USB 层复位才能恢复。**这会直接卡住 Codex 的刷机。**

### 4.1 允许 vs 禁止

| 操作 | 非持有者可否执行 |
|---|---|
| `adb devices` / `fastboot devices`（纯 USB 枚举） | ✅ 可以 |
| `lsusb` | ✅ 可以 |
| `adb shell getprop` | ❌ 不可以 |
| `fastboot getvar *` | ❌ 不可以 |
| `fastboot oem *` | ❌ 不可以 |
| `fastboot flash/reboot` | ❌ 不可以 |
| 任何 `spmi_access` sysfs | ❌ **双方都禁止** |

### 4.2 交接必录 6 项

移交时，移出方发以下信息，移入方确认后才算转移完成：

1. 序列号
2. 连接模式（`adb` / `fastboot` / preloader / 离线）
3. 当前槽位（`current-slot`）
4. 已知镜像哈希（刷了什么、md5/sha256）
5. 待取日志（哪些还没抓）
6. 已知异常（例如"fastboot 会话半死，需 USB 复位"）

**不设一方无条件优先。** 谁在做完整实验谁持有；两人都要时协商。

**注意**：序列号只写在交接消息里，**不写进任何提交的文件**（脚本用 `HAKIOS_SERIAL` 环境变量）。

---

## 5. 硬约束（两人都遵守）

- 🔴 **不碰 Android `spmi_access` sysfs** —— 两次把设备搞进 preloader
- 🔴 **MT6368 = SPMI SID 5**（SID 4 是 MT6363）；写 SID 时**不要加 `0x` 前缀**（历史事故原因）
- 🔴 **PMIF 只碰 `0x1c804000 + 0x880..0x8a8`** —— ≥`0x900` 总线 abort，整机重启 3 次
- 🔴 **禁写 PMIC `0x014b`**（全局 shutdown）；**禁 RMW `0x0240`**
- 🔴 **禁止启用未经验证的 WDT** —— `XNU_WDT_SECONDS=0` 是当前正确值，不要"补上"
- 🔴 **黑屏后不要长按电源键** —— PMIC 长按是冷复位，DRAM 不刷新，ramoops 烂 ~13%
- 🔴 **黑屏后的正确恢复（按 Codex 改写，v2 定稿）**：
  1. **先保存已有证据**（能读到的日志/寄存器先落盘）
  2. **只有设备持有者**可以**限时**尝试 `fastboot reboot`
  3. **失败或超时即停止**，等用户手动恢复
  4. **不等待 watcher** —— `XNU_WATCH_HEARTBEAT=1`（board.h 未覆盖）导致 watcher 120 s 后画绿屏 + `wfe` 死停，不复位也不快照，这是待修的 bug #1
  5. **`devices` 能列出设备不代表 fastboot 命令可用** —— 必须用 `getvar product` 判断协议层
- 🔴 **`fastboot oem expdb` 的风险与恢复（n=2 实测）**：它会让 LK 进入 dump 流程，**返回 `OKAY` 但零字节输出**，随后 **fastboot 协议层死亡**（`devices` 仍列出，但 `getvar`/`reboot`/`oem` 全 `usb_read failed: Value too large`）。**主机侧任何 USB 复位都无效**（unbind/rebind、`authorized` 0/1、`USBDEVFS_RESET` 三种实测均失败）。**正确做法：什么都不做，等设备屏闪数下后自行重启回 fastboot，实测约 25 分钟。** 恢复判据：`getvar product` 返回 `product: xaga`。
- 🔴 **本机 `fastboot oem` 通道只有 `fbreason` 有输出**：`lkmsg` / `expdb` / `lpmsg` / `dump_pllk_log` / `uefilog` / `get_socid` / `lks` **全部返回 OKAY 但零字节**。这条取证路已走完，无产出，**不要再试**。
- 🔴 **不加 `mali_gpueb=1`，不读 `0x13c40000`**（门控未确认）
- 🔴 **不自动刷机** —— 每次刷机前明确告知用户
- 🔴 **`hakios-mtk` 的未提交改动不要 stash/reset** —— 含 `A`（新增）文件，`git checkout .` 会造成半新半旧；只能逐个 `git checkout -- <file>`
- 🔴 **`hakios-mtk` 远端是别人的上游** —— 只本地提交，永不 push
- 所有轮询必须有超时
- 脚本里不写死序列号/绝对路径
- `build.sh` 必须带 `DEVICE=xaga`（否则回落到 MT6768 默认值，loader 变成 41240 字节的错误镜像）
- `.config` 会覆盖 `board.h` 的 `XNU_EXTRA_ARGS` —— 真实来源是 `devices/xaga/configs/xaga.config` 的 `CONFIG_BOOTARGS`，改完要 `make xaga_defconfig`

---

## 6. 额度与交接

```
codex 5h 额度 ≤5% → codex 停手 + 写交接文件 → DSH 检查
  ├─ 还有可用账号 → orca account select --agent codex --account <uuid>
  │                 → 新开终端 → 过信任提示 → 发交接文件 + 本文件 + MASTER-STATE.md
  └─ 都没额度     → DSH 接手（只做调研/复核/文档/设备，不写业务代码）
```

**检查方式**：`codex-quota` / `codex-failover` 脚本，只看 **`session_used` 剩余百分比**，不看 `allowed` 字段（旧脚本只看 `allowed`，把 99% 用掉的账号判成"可用"，已废弃）。

**已知坑**：
- `orca account select` **必须带 `--agent codex`**
- 切换后**必须新开终端**（旧会话仍在旧账号）
- 新会话信任提示要用 `--text $'\r'`，不是 `"1"` + `--enter`
- `orca terminal send` 的正确命令是 `orca terminal send`（不是 `orca send`）
- 代理节点会整体失效：ChatGPT 走 `ChatGPT` selector 组，节点全挂时表现为 `Reconnecting... waiting for network`。切换节点用 mihomo unix socket，不必重启 clash。

---

## 7. 汇报格式

只写数字和证据边界，不写形容词。三档标注：

- **已证明** + 证据位置（`文件:行号` / 命令输出 / sha256）
- **未验证** + 缺什么才能验证
- **已推翻** + 原说法 + 真相

**禁止**：「应该可以」「理论上」「大概率」「基本没问题」。

---

## 8. 当前活跃分工

### DSH 子代理（7 跑 / 1 完成）

| 任务 | 子代理 | 状态 |
|---|---|---|
| 耳机 ADC→Ω 换算 | `116e3ec7` | 跑 |
| 耳机 ACCDET 冷启动时序 | `9f9bf45f` | 跑 |
| 耳机播放通路最小初始化 | `734d77c9` | 跑 |
| GPU GPUEB IPI 协议还原 | `a04963d0` | 跑 |
| GPU MCU_STATUS 失败面枚举 | `788bf760` | 跑 |
| SPMI/PMIF 事务硬化 | `115c5ba3` | 跑 |
| codex 账号轮转自动化 | `a256e8c3` | 跑 |
| MTK 启动链取证 | `ce61ded2` | ✅ 完成 |

### Codex

| 任务 | 状态 |
|---|---|
| 日志三个 bug 修复 | 已改 `mtkloader.c`（`MARK_RUNNING` + `save_log_spans_to_flash`） |
| 耳机 `headset=2` 实现 | 编译中（`#include <kern/clock.h>` 刚补） |
| GPUEB IPI 实验 | 等 DSH 交付协议文档 |
