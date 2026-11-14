# 耳机与 GPU 调研索引（2026-11-14 23:11）

> 本目录汇总 2026-11-14 全部调研产出。**先读本文件，再按需深入。**
> 注意：部分早期结论已被后来的证据推翻，本文件已标注。

---

## 0. 项目现状一句话

- **目标**：HakiOS 把 XNU 移植到 Xiaomi xaga（MT6895），做到日用
- **耳机**：检测机制已完全摸清（有厂商源码），但**从未在真机读到过任何寄存器**
- **GPU**：Mali-G610 CSF MCU 在 `MCU_CONTROL=AUTO` 后不执行，19 轮未破
- **最近四次刷机黑屏**，但真实 692 镜像已验证能进 XNU

---

## 1. 权威材料（优先级最高）

### 1.1 厂商源码（耳机任务的答案）

```
/home/lizi/IOS/work-xaga/research/raw/xaga-ecosystem/mt6368-accdet.c   3609 行
/home/lizi/IOS/work-xaga/research/raw/xaga-ecosystem/mt6368.h          5974 行，257 个位域
/home/lizi/IOS/work-xaga/research/raw/mt6368-accdet-regmap.txt         寄存器全表
```

来源：`/home/lizi/IOS/work-xaga/k-xaga`（sparse clone，`sound/` 未 checkout）
**取法**：`git cat-file -p HEAD:sound/soc/codecs/mt6368-accdet.c`

### 1.2 寄存器语义（源码确证）

| 寄存器 | 位 | 名字 | 含义 |
|---|---|---|---|
| `0x26a9` | bit0 | `ACCDET_IRQ` | **中断标志**（不是插入状态） |
| `0x26aa` | bit0 | `ACCDET_IRQ_CLR` | 中断清除（W1C） |
| `0x26b6` | bit[7:6] | `ACCDET_MEM_IN` | **cur_AB** |
| `0x26b7` | bit[2:0] | `ACCDET_STATE` | 状态机状态 |

**cur_AB**：`0` = 三段耳机，`1` = 四段耳麦，`3` = 拔出，`2` 本平台不用。
**耳机 vs LineOut 阈值**：5000 Ω（`mt6368-accdet.c:849-850`）

### 1.3 关键提醒

- `check_cable_type()` 是**有前态、有 `eint_sync_flag` 门控**的状态机，
  **不能**写成无条件 `cur_AB==0 => 三段耳机`（MIC_BIAS 下 0 表示按键按下）
- `VLDCLR` 是事务 W1C ACK，**不能**套用 codec 配置寄存器「写完读回值必须相等」的校验
- `CONFIG_TARGET_PRODUCT_XAGA=y` 会开 `USB_3_5_UNSUPPORT`，TypeC 模拟音频会冒充耳机

---

## 2. 调研报告（`research/`）

| 文件 | 行数 | 价值 | 备注 |
|---|---|---|---|
| **`BLACKSCREEN-forensics.md`** | 737 | ★★★ 最高 | `fastboot oem lkmsg/fbreason` 五个只读命令；loader 三个 bug 的精确位置 |
| `WEB-xnu-android-projects.md` | 883 | ★★ | 四家同类项目；GPUEB 线索的源头 |
| `WEB-xnu-siblings.md` | 890 | ★★ | ios-sweet/UniXNU/Bergamota/ruby；日志做法 |
| `DEEP-mt6895-mainline.md` | 2727 | ★★ | mainline 的 323 条注释经验 |
| `GPUEB-analysis.md` | 493 | ★★ | GPUEB 是 RV33 小核；与项目结论的 6 条冲突 |
| `REPO-xaga-ecosystem.md` | 1172 | ★★★ | XagaForge 源码；boot image 参数；摄像头 I2C |
| `REPO-SLNeko-XNU.md` | 1292 | ★★ | 同一项目线的另一棵树；三态标记机制 |
| `REPO-hakios-all.md` | 751 | ★ | 全部 HakiOS 仓库；MT6768 的 GPU 分层经验 |
| `REPO-xaga-skyward.md` | 739 | ★★ | recovery + 完整 vendor_boot ramdisk + 平台 DTB |
| `WEB-mt6895-mainline-linux.md` | 1168 | ★★ | 主线支持现状（**MT6895 在主线里零支持**） |
| `MTK-bootchain-rescue.md` | 待确认 | ★ | 启动链与救援 |

---

## 3. 发现记录（`findings/`）——**注意时效性**

| 文件 | 状态 | 说明 |
|---|---|---|
| `FINDING-gpueb-2026-11-14-2215.md` | ✅ 有效 | GPUEB 首次发现 |
| `FINDING-gpueb-evidence-2026-11-14-2217.md` | ⚠️ 部分修正 | IPI 消息是 **32 字节/8 个 u32**，不是 4 个 |
| `FINDING-gpueb-hardware-2026-11-14-2227.md` | ✅ 有效 | 真机 `MBOX_ISR=GIC 548` 活跃 446 次 |
| `CROSSCHECK-external-vs-ours-2026-11-14-2225.md` | ✅ 有效 | 与既有结论的交叉核对 |
| `FINDING-blackscreen-logs-2026-11-14-2239.md` | ⚠️ **部分被推翻** | 见下 §4「日志」 |
| `FINDING-pdc-early-2026-11-14-2240.md` | ✅ 已做完实验 | 结论见 §5 |
| `BREAKTHROUGH-accdet-source-2026-11-14-2255.md` | ✅ 有效 | 耳机源码突破 |
| **`HANDOFF-sol-20261114-2259.md`** | ★★★ **必读** | 前一任 codex 的完整交接，**推翻了多条早期判断** |

---

## 4. 日志问题：真因是三个 bug 叠加

**早期结论「只要设 `MARK_RUNNING` 就解决」是错的。**

| # | bug | 位置 | 后果 |
|---|---|---|---|
| 1 | `XNU_WATCH_HEARTBEAT` 取默认值 `1`（board.h 未覆盖） | `mtkloader.c:80-82`、`:2934-2940` | 120s 后满屏绿 + `for(;;) wfe`，**不复位不快照** |
| 2 | `MARK_RUNNING` 从不点亮 | `:3582-3592`（`XNU_WDT_SECONDS=0`） | `if (mark==MARK_RUNNING) rescue_log()`（`:7344`）**永不进入** |
| 3 | `rescue_log()` 走 `plog_putc()`，而 xaga 是 `BOARD_NO_PLOG=1` | `:2635` + `mtkdbg.h:274-277` | **空函数**，即使修好 2 也拿不到 |

另外：**正常返回时，完整 HLOG 被后续 `probe_flash(38u)` 覆盖成 79 字节**（`mali_power_down` 在 `:6854`）。

**改法见 `research/BLACKSCREEN-forensics.md` 方案 2**（约 5 行）。

### 🔴 一条重要的操作禁忌

**黑屏后千万别长按电源键。** PMIC 单键长按是**冷复位，DRAM 不刷新**，
ramoops 会烂掉 ~13% 位。正确动作：等 watcher 自己复位，或 `fastboot reboot`。

（附注：我们的 `wdt_reset()` 写 `MODE=KEY|0x1`，不含 `EXRST_EN` bit2，**碰巧是 warm reset**——别"顺手补上"这一位。）

---

## 5. GPU：PDC 实验已完成，不是充分原因

`mali_nopdc=1` 单变量实验结果：

| 观测 | 开 PDC（基线） | `mali_nopdc=1` |
|---|---|---|
| MFG5/6/10/12 | `0x100d` | **`0xc000000d`**（ACK 恢复） |
| `0xF3C` | `0x4` | **`0x3ffc`**（全 12 域） |
| MCU 版本哨兵 | `0xff5aa5f0` | **`0xff5aa5f0`**（仍不变） |
| GLOBAL_IF | 未升起 | 未升起 |

**结论**：早开 PDC 是真问题，**但单独跳过它不解决固件启动**。

### 关于 `0xF3C` 的判据

交接文件说它可能是 gpufreq 影子寄存器而非 SPM 状态，建议弃用。
但实验显示它从 `0x4` → `0x3ffc` 与其他域状态变化一致，**作为相对指标仍可用**。

---

## 6. 已推翻的早期判断（避免重犯）

| 早期说法 | 真相 | 推翻依据 |
|---|---|---|
| 「loader 被重写 4172 行」 | 当前源码用 692 真实 flags 重建，**loader 字节差 0** | 产物 md5 |
| 「HEAD 的 3453 行 loader 是早期版本」 | 那是 **MT6768/Infinix** 的，不兼容 xaga | 文件头 |
| 「先查 `shader_ready`」 | 它追踪固件活动，**不是电源指示**（2460 条 trace 里 8 次升降） | `hakios.mali610.c:3343` |
| 「日志要改三段式」 | `loader_return.c:36-38` **本来就是** 256KB 头 + 1MB 环 | 源码 |
| 「设 `MARK_RUNNING` 就解决日志」 | 还有 `plog_putc` 空函数等两个 bug | 交接文件 §6 |
| 「`0x13FFF000` 是 GPUEB」 | 真实是 **`0x13C40000`** | 真机 DT |

---

## 7. 安全约束（硬性）

- **不碰** Android `spmi_access` sysfs（已多次搞掉设备）
- MT6368 是 SPMI **SID 5**；SID 4 是 MT6363
- **禁写** `0x014b`（全局 shutdown）；**禁 RMW** `0x0240`
- PMIF 只碰 `0x1c804000 + 0x880..0x8a8`；`0x900` 以上有过总线 abort
- **不加 `mali_gpueb=1`，不直接读 `0x13c40000`**（门控未确认）
- 所有轮询必须有超时
- 不要全树 `stash`/`reset`（49 个未提交改动含 xaga 能启动所需内容）

---

## 8. 取证路径现状（实测）

| 日志源 | 可达性 | 备注 |
|---|---|---|
| `xnusys` 尾 8 MiB（HLOG） | ✅ 已验证可读 | 只有 loader 写 |
| `expdb`（`sdc11`，128 MiB） | ⚠️ 分区在，**六个采样点全零** | 只在 LK 异常时写 |
| `log_store @ 0x7ffbf000`（256 KiB） | ❌ Android 侧读不到（无 `/dev/mem`） | dmesg 证实有内容（`point 0x1490f`） |
| `ramoops @ 0x48090000` | ⚠️ 配置对，但 `BOARD_NO_PLOG=1` | 当前不可用 |
| `fastboot oem lkmsg/fbreason` | 未测（手机在安卓） | **零代码零风险，值得先做** |

---

## 9. 当前分工

| 角色 | 职责 |
|---|---|
| 主控（dsh） | 调度、验证链、风险把控、文档、证据审校 |
| codex（sol） | 代码实现、架构决策、难题攻克 |

codex 账号切换：`system`（99% 用尽）→ `orca:2a3f54a2`（100% 可用，当前 active）
交接机制：额度剩 5% 时停手生成交接文件 → 主控检查 → 切换或接手
