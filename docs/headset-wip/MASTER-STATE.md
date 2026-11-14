> **2026-11-15 最新入口**：DSH额度耗尽，用户授权Codex完成归档后停工。请先完整读取 [最新交接](./HANDOFF-CODEX-20261115-0132.md)。共享PMIF/日志修复已构建，129项纯读候选未刷机；源码进展已推送5606fa8，构建树本地提交c9b4632。**下文是00:20起的历史状态，不代表当前状态；GPUEB首因、设备仍半死、驱动仍在编译等说法均以新交接更正为准。**

# MASTER-STATE 2026-11-15 00:20

> 本文件是「压缩上下文 / 换 codex 账号后继续工作」的入口。**先读完，再动手。**
> 配套必读：`COLLAB-PROTOCOL.md`（双方分工与硬约束）、`bridge/README.md`（与 codex 的通信方式）。

---

## 0. 一句话现状

HakiOS（XNU → Xiaomi xaga / MT6895 / Mali-G610）。
**耳机**：厂商源码全确证，驱动 `headset=1` 已可跑，`headset=2` codex 实现中，从未在真机读到过寄存器。
**GPU**：19 轮未破。`0x10000` 被误读成 `MMU_BUS_ERROR`，已推翻（真义 = AS0 REQ_COMPLETE，走查**成功**）。头号怀疑 = GPUEB 从没发过 IPI。
**日志**：三个 bug 已定位，codex 在改。
**SPMI**：审计出 **9 个缺陷，5 个会挂死/重启，3 个默认路径就会踩**。

---

## 1. 协作状态（每轮必看）

| 角色 | 是谁 | 职责 |
|---|---|---|
| **主控** | DSH（我），tab `aa6aca61`，终端 `term_65478f6a-0846-40d4-b817-55c9be904afd` | 调度、扇出调研、独立复核、文档、设备风险 |
| **代码** | Codex 6.1 Sol，终端 `term_049b4b6b-b3fd-4afb-b8c2-797a5c7cb17d` | 生产代码、寄存器语义、构建、实机验证 |

🔴 **子代理并发上限 = 4**（2026-11-15 02:30 用户定，原为 8）。
已有的不停止、跑完自然退出；新派前先 `list_agents`，`running + 新派 ≤ 4`。
理由：8 路时出现过 6 个连续因限流失败、零产出（02:00 那轮）。

**Codex 可直接给 DSH 发消息**（已实测）：
```bash
orca terminal send --terminal term_65478f6a-0846-40d4-b817-55c9be904afd --text "[DONE] 内容" --enter
```
前缀：`[ASK]` `[DONE]` `[BLOCK]` `[OBJ]` `[DEV]`。单条 ≤500 字符，`--text` 内不嵌套双引号。

**文件归属**：`hakios-mtk/**` 只有 Codex 能改；`repo/hakios-mt6895/docs/**` 只有 DSH 能改。
**设备权限**：完整实验期间手机只归一方，非持有者只能 `adb devices`/`fastboot devices`/`lsusb`。

---

## 2. 仓库与设备

| 项 | 值 |
|---|---|
| 文档仓库 | `/home/lizi/IOS/repo/hakios-mt6895`，HEAD `be6b825` + 本轮新提交 |
| 构建树 | `/home/lizi/IOS/hakios-mtk`，`4ebad21` + **49 个未提交改动（22 A / 26 M / 1 ??）** |
| 设备 | 序列号走 `HAKIOS_SERIAL` 环境变量 |
| 🔴 设备当前 | **fastboot 协议层半死**（`devices` 列出但 `getvar` 无应答）。起因：`fastboot oem expdb`。自愈实测 ~25 min |

**`fastboot oem` 只有 `fbreason` 有输出**：`lkmsg`/`expdb`/`lpmsg`/`dump_pllk_log`/`uefilog`/`get_socid`/`lks` **全部 OKAY + 零字节**。这条路已走完，**不要再试**。

---

## 3. 耳机：已确证 + 待做

### 3.1 寄存器语义（厂商源码，别重推）

| 寄存器 | 位 | 名字 | 含义 |
|---|---|---|---|
| `0x26a9` | bit0 | `ACCDET_IRQ` | **中断标志**，不是插入状态 |
| `0x26aa` | bit0 | `ACCDET_IRQ_CLR` | W1C |
| `0x26b6` | bit[7:6] | `ACCDET_MEM_IN` | **cur_AB** |
| `0x26b7` | bit[2:0] | `ACCDET_STATE` | 状态机 |

cur_AB：0=三段耳机，1=四段耳麦，3=拔出，2 本平台不用。

### 3.2 🔴 重大纠正：ACCDET 不测阻抗

**子代理 `116e3ec7` 推翻了我原来的任务前提。** `mt6368-accdet.c` 全文 3609 行**没有** ADC→欧姆的代码，没有 `zdiff`/`Z_DET`/查找表。

两套 ADC 分开：

| | ACCDET 插孔 ADC | HP 阻抗 ADC |
|---|---|---|
| 通道 | 5 | 12（HPOFS_CAL） |
| 位宽 | 12 bit | 15 bit |
| 参考 | **1840 mV** | — |
| 单位 | **毫伏** | **欧姆** |
| 用处 | 与 DTS/efuse 的毫伏阈值比 | 喂 `mt6368_accdet_read_audio_res()` |

**欧姆公式在 codec，不在 accdet**（`mt6368.c:4981-4992`）：
```
R = (3600000 / dc) * Δsum / 7832 / 8      dc = 288 / 1440 / 6048
```
三个易错点（`adc-ohm.py --selftest` 32/32 已钉住）：
1. `3600000/dc` **先整数截断**再乘（phase2 = **595** 不是 595.238）
2. `Δsum` 是 **8 次采样之和**，最后才 `/8`
3. 饱和边界精确：**52079 Ω 可用，52080 Ω 起钳位成 5000**

**驱动永远报不出 0 Ω**：`Δ==0` 被 `mt6368.c:5110-5121` 判成「没插」直接报 5000。

**按键走毫伏不是欧姆**：xaga `headset-key-mode=<0>` → 3-key，mid/up/down = **80/220/400 mV**。
`0/70/135/235/470 Ω` 在这些文件里**一次都没出现**。

### 3.3 裸机最短路径（子代理建议）

不需要全表。在 phase0 读 8 次 `0x109a`(HPOFS_CAL) 与基线之差：
`sum >= 25060` → ≥5000 Ω；`sum <= 25059` → <5000 Ω。相对精度 1 Ω。
**裸机没有 `get_hp_dctrim_offset()`，基线要自己在 dc=0 采一次。**

### 3.4 三条实现约束（别踩）

1. `cur_AB` **不是**「已插入」布尔 —— `check_cable_type()` 有前态 + `eint_sync_flag` 门控；MIC_BIAS 下 `cur_AB==0` = 按键按下
2. `VLDCLR` 是事务 W1C ACK，**不能**套「写完读回必须相等」
3. PMIF 事务必须与 keys 驱动**串行化**（共用 SWINF_2，双方无锁）

### 3.5 待做

1. `headset=2` 实现（Codex 在做，编译中）
2. 恢复调用点：`hakios.input.c` 的 `hakios_input_init()` 里**没有** `hakios_mt6895_headset_init()` 调用（`:229-248` 那段）
3. `bootargs` 加 `headset=1`：改 `devices/xaga/configs/xaga.config` 的 `CONFIG_BOOTARGS`，然后 `make xaga_defconfig`
4. ADC 基线采集

---

## 4. GPU：`0x10000` 推翻 + GPUEB 是头号嫌疑

### 4.1 🔴 重大推翻

**之前把 MMU 中断值 `0x10000` 读成 `MMU_BUS_ERROR(0)` 是错的。**

panthor 原文 `/tmp/mt6895-mainline/drivers/gpu/drm/panthor/panthor_mmu.c:648-650`：
```c
/* Bits 16 to 31 mean REQ_COMPLETE. */
return value & GENMASK(15, 0);
```
`0x10000` = bit16 = **AS0 的 REQ_COMPLETE** = 表走查**成功完成**。
驱动里 `:473`/`:478`/`:656`/`:2906`/`:2909`/`:3447`/`:3726`/`:3930`/`:4671`/`:4767`/`:4809`/`:5143` 等处据此写的推理链**全部要重审**（子代理 `6f8b37f4` 在做）。

### 4.2 其它澄清

- `JOB_INT_MASK`（`0x1008`）= **使能集**（写 1 = 开），驱动 `:3986-3989` 注释写反了但行为对
- `kJobIntRaw = 0x1000`（`:358`）**只定义、全文件零次使用** —— `RAWSTAT` 从未被观测
- 固件内嵌 258048 字节，10 段全映射，reset vector `sp 0x2040000 pc 0x10018ed`
- `GPU_ID = 0xa8670004` = arch 10.8，与固件匹配

### 4.3 头号嫌疑：GPUEB 从没发过 IPI

**完整协议已还原**（`findings/GPUEB-IPI-PROTOCOL.md` 54 KB + `findings/gpueb-ipi.h` 26 KB，我用 `gcc -std=c11 -Wall -Wextra` 编译零警告，sim 4/4 全过）。

六条硬结论：
1. **`0x13c5fd80` 不是寄存器块，是 640 B 消息 SRAM**
2. 四寄存器全是 1bit/通道位图：`send 0x13c62000`=RO TX忙 / `set 0x13c62004`=WO 触发 / `clr 0x13c62074`=WO 清位即应答 / `recv 0x13c62078`=RO 待收
3. 消息 = `{cmd_id, target, union{24B}}` = 32 B。**`INIT` 的 base 是 u64 占 2 字**（`status@2..3 / debug@4..5 / status_size@6 / debug_size@7`），**不是交错**
4. GPUFREQ 通道 **TX = slot 4（`0x10`）、RX = slot 47（`0xbc`）**
5. `power_control` 是**使能标量**不是位域；**没有「上电位」**，上电就是命令 6
6. **纯轮询可行，零中断**。超时改 1 s 快速失败

**驱动障碍**：`:266` 的 `MALI_GPUEB_MBOX_RECV 0x22078` 从未被引用且是禁区相对偏移；`gpueb_watch()` 四个探针全在禁区内（日志 `not mapped` **其实是对的**）。
**vendor 的 `gpufreq_power_control()` 先查 `power_control`，为 0 就静默不发 IPI**（`gpufreq_v2.c:855-858`）—— 照抄上游 `gpueb_power.c`（无条件发）绕开。

### 4.4 建议实验顺序

**E1（先做）**：读 `JOB_INT_RAWSTAT`（`0x1000`）。成本 ★，只读零风险。若也 0 → 「MCU 未执行」成唯一解释，全部精力转 E6。
**E6（关键）**：发 `CMD_POWER_CONTROL`。

---

## 5. 日志：三个 bug

| # | bug | 位置 |
|---|---|---|
| 1 | `XNU_WATCH_HEARTBEAT` 取默认 `1` → watcher 满屏绿 + `wfe` 死停，**不复位不快照** | `mtkloader.c:80-82`、`:2934-2940` |
| 2 | `MARK_RUNNING` 从不点亮（`XNU_WDT_SECONDS=0`）→ `rescue_log()` 永不进入 | `:3582-3592`、`:7344` |
| 3 | `rescue_log()` 走 `plog_putc()`，`BOARD_NO_PLOG=1` → **空函数** | `:2635`、`mtkdbg.h:274-277` |

另：正常返回时完整 HLOG 被 `probe_flash(38u)` 覆盖成 79 字节（`mali_power_down`，`:6854`）。

Codex 已改：`:2998` 加 `marker_set(MARK_RUNNING)`；`save_log_to_flash` 改成 `save_log_spans_to_flash`（`XNU_LOGBOOK_RESCUE 0x52534355`）。

### 🔴 操作禁忌

- **黑屏后别长按电源键** —— PMIC 长按是**冷复位，DRAM 不刷新**，ramoops 烂 ~13%
- 恢复：① 先存证据 ② 只有持有者可限时试 `fastboot reboot` ③ 失败即停，等用户 ④ **不要等 watcher**
- **`devices` 能列出 ≠ fastboot 可用**，判据是 `getvar product`

### 取证路径实测

| 源 | 可达 |
|---|---|
| `xnusys` 尾 8 MiB | ✅ 但只有 loader 写 |
| `expdb` | ❌ `oem expdb` OKAY + 零字节 |
| `log_store @ 0x7ffbf000` | ❌ Android 无 `/dev/mem` |
| `ramoops @ 0x48090000` | ⚠️ `BOARD_NO_PLOG=1` |
| **Android 侧 ftrace/kprobe** | ⏳ **新首选，子代理 `9b39dad5` 在做** |

---

## 6. SPMI/PMIF：9 个缺陷，5 个会挂死

来源 `findings/SPMI-PMIF-SPEC.md` §7.7。我独立核过 B3。

| # | 缺陷 | 后果 | 可达性 |
|---|---|---|---|
| **B1** | mali 用 `kernel_thread_start` 起独立线程 `delay(2000ms)` 后动 PMIF，keys 是 `for(;;)` 每 20ms 轮询，**三份文件 grep `lck_mtx`/`lck_spin`/`IOLock` 零命中** | 必然重叠，互相清 ACK/覆盖 ACC | **现在就可达** |
| **B3** | IDLE 超时不补 `VLDCLR` ACK（keys`:234-236`、headset`:121-123`、mali`:3052-3053` 三处全缺） | FSM 停 6，通道**永久失效** | **现在就可达** |
| **B2** | `pmic_read_on(chan,...)` 的 `chan*0x40` 可越界到 `0x940` | 重启 | 潜伏 |
| **B6** | `restore_configuration()` 与 keys 线程并发；`psci_smp_stop_others()` 停的是 **CPU 不是线程** | 并发破坏通道 | **现在就可达** |
| **B8** | `PMIF_SIZE=0x1000` 映射整页 | `0x900` 变「合法 load」 | 潜伏 |
| B4 | mali 写路径等一个不会出现的 `WFVLDCLR` | 必然误报失败 | 结果错 |
| B5 | headset 初始化嵌在「SID 4 读成功」分支里（`keys.c:435` if → `:441`） | SID 4 一失败 headset 永不初始化 | 结果错 |
| B7 | 200000 次无时基自旋 | 0.1–1 s 不可预测 | 健壮性 |
| B9 | mali 写完 ACK 不验 IDLE | 调用方以为成功 | 结果错 |

**B1 与 B3 必须一起修**：只修 B3 通道仍被并发打坏；只修 B1 锁保护的是死通道。

### ACC 命令字（已验）
```
opc[31:30] | WR[29] | SID[27:24] | (len-1)[23:16] | ADDR[15:0]
```
**没有** USID/PID/BCUSID/A 位。读 `0x26b6` = `0xC50026B6`，写 `0x241` = `0xE5000241`（`spmi-pmif.h` 实测输出一致）。
`STA[3:1]` = FSM（0=IDLE, 6=WFVLDCLR, **4=NAK 停这里**），`[18]`=ERR、`[17]`=SBUSY、`[15]`=DONE。
**PMIF 里没有 SW_RST**，唯一安全恢复是补一次 `VLDCLR`。
`≥0x900` 是 **`spmi_pmif_mpu@1c804900`（另一设备，size 0x500）**，不是 SWINF_3。

---

## 7. 已推翻的早期判断（别重犯）

| 早期说法 | 真相 |
|---|---|
| 「loader 被重写 4172 行」 | 重建后**字节差 0** |
| 「HEAD 的 3453 行是早期版本」 | 那是 **MT6768** 的，不兼容 |
| 「先查 `shader_ready`」 | 追踪固件活动，不是电源 |
| 「日志改三段式」 | **本来就是** 256KB 头 + 1MB 环 |
| 「设 `MARK_RUNNING` 就解决日志」 | 还有两个 bug |
| 「`0x13FFF000` 是 GPUEB」 | 真实 `0x13C40000` |
| **「`0x10000` = MMU_BUS_ERROR，AS0 走查被拒绝」** | **= AS0 REQ_COMPLETE，走查成功** |
| **「ACCDET 有 ADC→欧姆换算」** | **ACCDET 只有毫伏；欧姆在 codec `mt6368.c:4981`** |
| **「按键阻抗是 0/70/135/235/470 Ω」** | **是 80/220/400 mV** |
| **「`fastboot oem lkmsg/expdb` 能取证」** | **全部 OKAY + 零字节，且会搞死会话** |
| **「USB 复位能恢复半死的 fastboot」** | **三种主机侧手段全无效，只能等设备自重启 ~25 min** |
| **「`JOB_INT_MASK=0xffffffff` 是全屏蔽」** | **是使能集，全开** |

---

## 8. 硬约束

- 🔴 不碰 Android `spmi_access` sysfs
- 🔴 MT6368 = SPMI **SID 5**（SID 4 = MT6363）；写 SID **不加 `0x` 前缀**
- 🔴 PMIF 只碰 `0x1c804000 + 0x880..0x8a8`，真实上界 `0x8fe`
- 🔴 禁写 `0x014b`；禁 RMW `0x0240`
- 🔴 **禁止启用未经验证的 WDT**（`XNU_WDT_SECONDS=0` 是正确值）
- 🔴 不加 `mali_gpueb=1`，不读 `0x13c40000`
- 🔴 不自动刷机 —— 每次刷机前告知
- 🔴 `hakios-mtk` 未提交改动**不要 stash/reset**（`git checkout .` 会造成半新半旧，只能逐个 `git checkout -- <file>`）
- 🔴 `hakios-mtk` 远端是**别人的上游**，只本地提交，永不 push
- 所有轮询必须有超时
- 脚本不写死序列号/绝对路径
- `build.sh` 必须带 `DEVICE=xaga`
- `.config` 覆盖 `board.h` 的 `XNU_EXTRA_ARGS` → 真实来源是 `devices/xaga/configs/xaga.config`，改完 `make xaga_defconfig`

---

## 9. 关键路径

```
入口（本文件）   docs/headset-wip/MASTER-STATE.md
协作协议         docs/headset-wip/COLLAB-PROTOCOL.md        ★新
通信信箱         docs/headset-wip/bridge/README.md           ★新
调研索引         docs/headset-wip/research/INDEX.md
厂商源码         raw/mt6368-accdet.c / mt6368.h
寄存器全表       raw/mt6368-accdet-regmap.txt
GPUEB 一手源码   raw/vendor-gpueb/ · raw/upstream-gpueb/     ★新
构建树           /home/lizi/IOS/hakios-mtk
GPU 驱动         mtk/mt6895/drivers/gpu/hakios.mali610.c
耳机驱动         mtk/mt6895/drivers/audio/hakios.mt6895_headset.c
keys 驱动        mtk/mt6895/drivers/input/hakios.mt6895_keys.c
loader           mtk/loader/mtkloader.c
```

### 可执行工具（全部 `--selftest` 通过，我独立复跑过）

| 脚本 | 自检 | 用途 |
|---|---|---|
| `accdet-compare.py` | 10 用例 | 寄存器对拍 |
| `adc-ohm.py` | **32/32** | ADC↔Ω 换算、按键档位 |
| `accdet-init-gen.py` | PASS | JSON → C 初始化数组 |
| `spmi-sim.py` | **9/9** | PMIF 寄存器级模拟器 |
| `gpueb-sim.py` | **4/4** | GPUEB mailbox 状态机 |
| `mali-seq-diff.py` | ok | 驱动写序列 vs panthor diff |

### 头文件（可直接 include）

| 头文件 | 编译 |
|---|---|
| `findings/gpueb-ipi.h` | `gcc -std=c11 -Wall -Wextra` 零警告 |
| `findings/spmi-pmif.h` | `gcc ... -Werror` 零告警 |

---

## 10. 下一步

**等设备恢复**后按此顺序：

1. **Codex**：`headset=2` 编译 → 日志 3 bug 构建验证 → 恢复 `hakios_input_init()` 调用点
2. **DSH**：SPMI 的 B1+B3 一起修的方案审（Codex 实现）
3. **GPU**：先 E1（读 `RAWSTAT` 零风险），再 E6（GPUEB IPI）
4. **耳机**：`headset=1` 跑一次 → **「SID 5 可读性 + 未插入基线」检查**（见下方边界说明）→ ADC 基线 → ADC 换算

### 🔴 首轮实验的真实能力边界（Codex 2026-11-15 00:22 提出，DSH 采纳）

**用户从一开始就说明没有实体耳机。** 因此首轮 `headset=1` 的 **129 项**寄存器对拍**只能验证三件事**：

1. **SID 5 寄存器读通** —— PMIF 事务能不能到 MT6368
2. **未插入时的基线对不对** —— 与 `accdet-baseline.json` 的 130 个寄存器比
3. **`0x26b6` 读回是不是 `0xFF`**（cur_AB = 3 = 拔出）

**不能证明**：耳机插入能被识别。这需要实体耳机，是硬边界。

→ 该实验的正式名称是 **「SID 5 可读性 + 未插入基线」**，**不要**叫「耳机识别验证」。

### 🔴 三条判据设计约束（2026-11-15 00:58，来自两个子代理 + Codex 的决定）

**a) 绝不能用「与基线一致率」当判据。**
全 `0x00` 的死总线读回，对 130 项基线能拿到 **78/130 = 60.0%** 一致率（基线本身有 74 个 `0x00`）。
若把一致率当判据，**一根死总线会以「60% 还行」通过**。
正确判据是三者一起看（`headsetless-check.py` 已实现）：
- **形状**：块内 distinct ≥ 12（基线 27 种）、`0xFF` 计数 ≤ 12、`0x00` 计数 ≤ 96
- **结构**：7 对镜像寄存器逐对相等
- **锚点**：`0x231d == 0x06`（stock 自检值）

**b) `headset=1` 必须保持纯读，VAUD18 只观测不启用。**
首轮目的是建立**可复现的基线**；一旦写入就改变状态，之后所有读数失去可比性。
VAUD18（`0x1cb2` bit0）的写入归 `headset=2` 单独验证。

**c) 若 `0x1cb2` bit0 == 0（VAUD18 未开），`cur_AB` 读数不能作为检测成功的证据。**
MICBIAS 可能没供电，此时 `0x26b6` 读到的 AB=3 与真空插座**同形**。
脚本会把这条保留意见直接写进输出。

**129 项的构成**：原 124 + `0x0008`/`0x0009`/`0x1cb2`/`0x230c` 四个地址 + `0x231d` 锚点。
