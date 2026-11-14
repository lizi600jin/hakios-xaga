# MASTER-STATE 2026-11-14 23:12（压缩上下文用）

> 本文件是「压缩上下文后继续工作」的唯一入口。**先读完这 60 行，再动手。**

---

## 0. 一句话现状

HakiOS（XNU → Xiaomi xaga / MT6895）。**耳机**：厂商源码已拿到，寄存器语义完全确证，但从未在真机读到过寄存器。
**GPU**：PDC 实验已排除为充分原因，MCU 仍不启动。**日志**：定位到三个 bug 叠加，未修。

---

## 1. 仓库状态

| 仓库 | HEAD | 状态 |
|---|---|---|
| `/home/lizi/IOS/repo/hakios-mt6895`（文档+tree 镜像） | `f390f17` | ✅ 已推送，与远端一致 |
| `/home/lizi/IOS/hakios-mtk`（构建树） | `4ebad21` | 本地提交（远端是别人的上游，不推）|

**hakios-mtk 有 49 个未提交改动**（22 A + 26 M + 1 ??），跨 GPU/loader/UFS/input/audio。
**不要全树 stash/reset** —— 含 xaga 能启动所需内容。

---

## 2. 设备

- 序列号 `GAJZ4HIN99AQ69Y9`（脚本用 `HAKIOS_SERIAL` 环境变量，**不要写死**）
- 当前：**原厂安卓，adb 在线**，`sys.boot_completed=1`
- 恢复镜像 `backup/boot_a_stock_real.img`，md5 `a63b2e35a7c2522cb24a6a10332b9bc8`

---

## 3. 耳机：已完成与待做

### 3.1 已确证（厂商源码）

源码：`research/raw/mt6368-accdet.c`（3609 行）、`mt6368.h`（5974 行，257 位域）
取法：`git cat-file -p HEAD:sound/soc/codecs/...`（源在 `/home/lizi/IOS/work-xaga/k-xaga`，sparse 未 checkout）

| 寄存器 | 位 | 名字 | 含义 |
|---|---|---|---|
| `0x26a9` | bit0 | `ACCDET_IRQ` | **中断标志**（不是插入状态） |
| `0x26aa` | bit0 | `ACCDET_IRQ_CLR` | 清除（W1C） |
| `0x26b6` | bit[7:6] | `ACCDET_MEM_IN` | **cur_AB** |
| `0x26b7` | bit[2:0] | `ACCDET_STATE` | 状态机 |

`cur_AB`：0=三段耳机，1=四段耳麦，3=拔出，2 本平台不用。
耳机 vs LineOut 阈值：**5000 Ω**（`mt6368-accdet.c:849-850`）

### 3.2 三条实现约束（别踩）

1. `cur_AB` **不是**「已插入」布尔 —— `check_cable_type()` 有前态 + `eint_sync_flag` 门控，
   MIC_BIAS 下 `cur_AB==0` 表示**按键按下**而非三段耳机
2. `VLDCLR` 是事务 W1C ACK，**不能**套「写完读回必须相等」
3. PMIF 事务必须与 keys 驱动**串行化**（共用 SWINF_2，双方无锁）

### 3.3 现有代码

`mtk/mt6895/drivers/audio/hakios.mt6895_headset.c`（131 行）
- `headset=1` 只读 76+48 个寄存器，格式 `headset: 0x%04x = 0x%02x`，**已实现**
- `headset=2` 初始化+轮询，**TODO**
- **`drivers/input/hakios.input.c` 里的调用已被移除**（内核含函数但不执行）

### 3.4 待做

1. 恢复调用点 + `headset=1` 跑一次 → 用 `docs/headset-wip/accdet-compare.py` 对拍 124 个寄存器
2. 实现 `headset=2`
3. ADC→Ω 换算仍缺（`read_audio_res` 只消费阻抗，不计算）

---

## 4. GPU：19 轮未破，当前候选

### 4.1 已排除

- **PDC 早开**：`mali_nopdc=1` 让 MFG5/6/10/12 从 `0x100d`→`0xc000000d`、`0xF3C` 从 `0x4`→`0x3ffc`，
  **但哨兵仍 `0xff5aa5f0`**。是真问题，不是充分原因。
- `shader_ready`：追踪固件活动，**不是电源指示**（2460 trace 里 8 次升降，`hakios.mali610.c:3343`）

### 4.2 候选

- **GPUEB IPI 上电**：panthor 在 `MCU_CONTROL=AUTO` 紧前面调 `mt6895_gpueb_power_on()`（`panthor_fw.c:1131-1143`，已核实）
  - 我们**一次 IPI 都没发过**，历史日志 `gpueb: not mapped`
  - IPI 消息 **32 字节 / 8 个 u32**（不是 4 个）
  - 协议：`CMD_INIT_SHARED_MEM(0)` → `CMD_POWER_CONTROL(6)`，`IPI_SEND_POLLING`，超时 10s
  - 共享区 `0x7c600000`（2 MiB），status 前 `0x400`，关键字段 `power_control +0x60`、`shader_present +0x68`
  - **`power_count==0` 不能证明「没上过电」**（原厂 Android 下也是 0，但系统正常）
  - **门控未确认，不碰 `0x13c40000`**

---

## 5. 日志：三个 bug 叠加

| # | bug | 位置 |
|---|---|---|
| 1 | `XNU_WATCH_HEARTBEAT` 取默认 `1`（board.h 未覆盖）→ watcher 满屏绿 + `wfe` 死停，**不复位不快照** | `mtkloader.c:80-82`、`:2934-2940` |
| 2 | `MARK_RUNNING` 从不点亮（`XNU_WDT_SECONDS=0`）→ `rescue_log()` 永不进入 | `:3582-3592`、`:7344` |
| 3 | `rescue_log()` 走 `plog_putc()`，xaga `BOARD_NO_PLOG=1` → **空函数** | `:2635`、`mtkdbg.h:274-277` |

另：正常返回时完整 HLOG 被 `probe_flash(38u)` 覆盖成 79 字节（`mali_power_down`，`:6854`）。

**改法**：`research/BLACKSCREEN-forensics.md` 方案 2（约 5 行）。

### 🔴 操作禁忌

**黑屏后别长按电源键** —— PMIC 长按是**冷复位，DRAM 不刷新**，ramoops 烂掉 ~13%。
正确：等 watcher 复位，或 `fastboot reboot`。
（我们的 `wdt_reset()` 写 `MODE=KEY|0x1` 不含 `EXRST_EN`，**碰巧是 warm**，别"补上"这一位。）

### 取证路径实测

| 源 | 可达 |
|---|---|
| `xnusys` 尾 8 MiB（HLOG） | ✅ 可读，但只有 loader 写 |
| `expdb`（`sdc11`，128 MiB） | ⚠️ 分区在，**六点采样全零**（只在 LK 异常时写） |
| `log_store @ 0x7ffbf000`（256 KiB） | ❌ Android 无 `/dev/mem`；dmesg 证实有内容（`point 0x1490f`） |
| `ramoops @ 0x48090000` | ⚠️ 配置对但 `BOARD_NO_PLOG=1` |
| **`fastboot oem lkmsg/fbreason/lpmsg`** | **未测，零代码零风险，值得先做** |

---

## 6. 已推翻的早期判断（别重犯）

| 早期说法 | 真相 |
|---|---|
| 「loader 被重写 4172 行」 | 重建后**字节差 0** |
| 「HEAD 的 3453 行是早期版本」 | 那是 **MT6768** 的，不兼容 |
| 「先查 `shader_ready`」 | 它追踪固件活动，不是电源 |
| 「日志改三段式」 | **本来就是** 256KB 头 + 1MB 环 |
| 「设 `MARK_RUNNING` 就解决日志」 | 还有两个 bug |
| 「`0x13FFF000` 是 GPUEB」 | 真实 `0x13C40000` |

---

## 7. 分工与协作

| 角色 | 职责 |
|---|---|
| **主控（我，dsh）** | 调度、验证链、风险把控、文档、证据审校。**不写业务代码** |
| **codex（sol）** | 代码实现、架构决策、难题攻克 |

**当前 codex 会话**：`term_049b4b6b-b3fd-4afb-b8c2-797a5c7cb17d`
**账号**：`orca:2a3f54a2`（blunder-inbuilt-0y@icloud.com），5h limit 100%
**旧会话**：`term_0ec0a21a`（system 账号，1%，已停手）

### 交接机制（用户要求）

```
额度剩 5% → codex 停手 + 生成交接文件 → 主控检查
  → 有额度：orca account select 切换 → 新开终端 → 过信任提示 → 发交接文件
  → 没额度：主控接手
```

**已知坑**：
- `orca account select --agent codex --account <uuid>`（必须带 `--agent`）
- 切换后**必须新开终端**（旧会话仍在旧账号）
- 新会话信任提示要用 `--text $'\r'`（不是 `"1"` + `--enter`）

---

## 8. 硬约束

- **不碰** Android `spmi_access` sysfs（多次搞掉设备）
- MT6368 = SPMI **SID 5**；SID 4 是 MT6363
- **禁写** `0x014b`；**禁 RMW** `0x0240`
- PMIF 只碰 `0x1c804000 + 0x880..0x8a8`
- **不加 `mali_gpueb=1`，不读 `0x13c40000`**
- 所有轮询必须有超时
- **不自动刷机** —— 每次刷机前明确告知
- 脚本里不写死序列号/绝对路径

---

## 9. 关键路径

```
文档+索引      /home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip/research/INDEX.md
厂商源码       .../docs/headset-wip/raw/mt6368-accdet.c  (+ mt6368.h)
寄存器全表     .../docs/headset-wip/raw/mt6368-accdet-regmap.txt
对拍工具       .../docs/headset-wip/accdet-compare.py（10 个自检用例）
基线           .../docs/headset-wip/accdet-baseline.json（130 个寄存器）
codex 交接     .../findings/HANDOFF-sol-20261114-2259.md  ★必读
构建树         /home/lizi/IOS/hakios-mtk
GPU 驱动       mtk/mt6895/drivers/gpu/hakios.mali610.c（5209 行）
耳机驱动       mtk/mt6895/drivers/audio/hakios.mt6895_headset.c（131 行）
loader         mtk/loader/mtkloader.c（7496 行）
```

---

## 10. 下一步建议顺序

1. **零成本取证**：手机进 fastboot 跑 `fastboot oem fbreason / lkmsg / lpmsg`（只读，需记录 sha256，环形缓冲会骗人）
2. **修日志**（3 个 bug，约 5 行）→ 之后所有实验都有日志
3. **耳机主线**：恢复调用点 + `headset=1` 对拍 → `headset=2`
4. **GPU**：GPUEB 门控确认后再谈 IPI
