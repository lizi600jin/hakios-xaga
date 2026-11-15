# DAPM-PREFLIGHT：原厂 Android 只读取证

> 2026-11-15 22:40，DSH。**只读，未刷机，未占 trial 号，未写任何 sysfs/control。**
> 设备：原厂 Android 17，`boot_a = a63b2e35…`（未变），耳机插着（未戴在耳上）。

---

## 0. 一句话

**DT 与 sysfs 两条独立来源都确认 `mt6685` 在 SPMI SID 9（不是 MT6368 的 SID 5）。
但 dmesg 已滚过 probe 阶段，两条判据行都没命中 —— 记为证据不足，不推断校准未做。**

---

## 1. 采集到的文件

| 文件 | 大小 | 说明 |
|---|---|---|
| `dmesg.txt` | 2204353 B | **完整 dmesg，未清环** |
| `modules.txt` | 26312 B | `/proc/modules`（385 行） |
| `asound-cards.txt` | 88 B | `/proc/asound/cards` |
| **`xaga-live.fdt`** | **408337 B** | **`/sys/firmware/fdt` 原始文件**，sha256 `c81fe0b1…` |
| `xaga-live.dts` | 513480 B | 由上面反编译（`dtc -I dtb -O dts`），14212 行 |

**FDT magic 校验**：`0xd00dfeed`，`totalsize=408337`，`version=17` ✅

---

## 2. 判据 1/2：dmesg 两条目标行 —— **未命中**

| 判据 | 命中数 |
|---|---|
| `mt6685_audclk_probe done` | **0** |
| `get_hp_trim_offset` / `after trim_code R:(…) L:(…)` | **0** |
| `hp_trim_code` / `finetrim` | **0** |
| AUXADC / 校准错误 | **0** |
| `Linux version`（启动标记） | **0** |

### 🔴 为什么没命中：dmesg 只覆盖最近一段

**`dmesg.txt` 第一行是 `[   23.581700]`，没有 `Linux version` 启动标记。**

说明环形缓冲**已滚过内核启动阶段**，probe 发生在更早，记录已被覆盖。

**按指令：记为「证据不足」，不推断校准未做或 trim=0。**

**这不是权限问题**（`dmesg` 读取 `rc=0`，2204353 字节完整取出，stderr 为空）。

---

## 3. 模块状态（`/proc/modules` 与 `/sys/module`）

### 3.1 依赖链

```
mt6895_mt6368 61440 1 -            Live
  └─ snd_soc_mt6368 176128 2 mt6895_mt6368  Live
       ├─ mt6368_accdet 61440 2 mt6895_mt6368,snd_soc_mt6368  Live
       └─ mt6685_audclk 16384 1 snd_soc_mt6368  Live     ← 靠 codec 撑引用计数
mt6685_core 16384 0 -              Live
```

**`mt6685_audclk` 的 refcnt = 1，被 `snd_soc_mt6368` 持有。**

### 3.2 声卡已注册

```
/proc/asound/cards:
 0 [mt6895mt6368   ]: mt6895-mt6368 - mt6895-mt6368
```

---

## 4. 🔴 MT6685 的 SPMI 定位（两条独立来源，互相印证）

### 4.1 来自设备树

```
/spmi@1c804000/mt6685_mfd {
    compatible = "mediatek,mt6685";
    reg = <0x09 0x00>;          ← SID 9
};
```

父节点：

```
spmi@1c804000 {
    compatible = "mediatek,mt6895-spmi";
    reg = <0x00 0x1c804000 0x00 0x8ff      /* pmif    */
           0x00 0x1c801000 0x00 0x100>;    /* spmimst */
    reg-names = "pmif", "spmimst";
    swinf_ch_start = <0x0a>;
    ap_swinf_no = <0x02>;
};
```

### 4.2 来自 sysfs 绑定路径

```
/sys/bus/platform/drivers/mt6685-audclk/mt6685-audclk
  -> ../../../../devices/platform/1c804000.spmi/spmi-0/0-09/mt6685-audclk
                                                        ^^^^ SID 9
```

### 4.3 SPMI 上的全部设备（`/sys/bus/spmi/devices/`）

| 名称 | SID | 备注 |
|---|---|---|
| `0-04` | **4** | `pmic@4` = MT6363 |
| **`0-05`** | **5** | **MT6368（耳机/音频 codec）** |
| `0-06` | 6 | |
| `0-07` | 7 | |
| **`0-09`** | **9** | **MT6685（音频时钟 + RTC + consys）** |
| `spmi-0` | — | 控制器本身 |

### 4.4 ✅ 这正是指令警告的那一点

**`mt6685_set_dcxo` 操作的是 SID 9 上它自己的 regmap（`0x079e` bit2），
与 MT6368 的 SID 5 完全分离。**

**我们的 `headset=` 驱动只碰 SID 5。两条路径在 SPMI 层面就分开了，不可能误写。**

---

## 5. 模块文件：`snd-soc-mt6368.ko` 拿到，`mt6685_audclk.ko` **找不到**

### 5.1 拿到的

| 文件 | 路径 | 大小 | SHA256 |
|---|---|---|---|
| **`snd-soc-mt6368.ko`** | `/vendor/lib/modules/` | 277608 | **`4ce5cab06d3d29aa3e2ccfbd8044d68d691b27f3b4e336332bfb41ee6e479475`** |
| `mt6368-accdet.ko` | `/vendor/lib/modules/` | 120232 | `c276ab387d3cea52fbb92f864c22e2f8948b10d8bdf9e64ec78bda154addac1d` |

**两者与仓库已有的 `docs/headset-wip/ko/` 副本哈希完全一致**，可复用，
校验依据即「同一 SHA256」。

### 5.2 🔴 `mt6685_audclk.ko` 取不到

**`mt6685_audclk` 确实在运行**（`/proc/modules` 有、`/sys/module/mt6685_audclk/sections/` 存在、
`coresize=16384`），**但它的 `.ko` 文件不在下列任何位置**：

| 查过的位置 | 结果 |
|---|---|
| `/vendor/lib/modules/`（222 个 .ko） | 无 `*6685*`、无 `*audclk*` |
| `/vendor_dlkm/lib/modules/`（222 个 .ko） | 同上 |
| `modules.load` | 无 `mt6685_audclk` |
| `modules.dep` | 无 |
| `grep -il 6685_audclk /vendor/lib/modules/*` | 无 |

**我无法解释这个矛盾**（模块在跑但文件找不到）。
可能它在更早的启动阶段从别处加载后被删除，或来自 initramfs。
**这是未解决的问题，不是「模块不存在」。**

### 5.3 可用的替代

**仓库里已有 `docs/headset-wip/ko/snd-soc-mt6368.ko` 等六个 `.ko`**，
其中 `snd-soc-mt6368.ko` 与本次从设备拉取的哈希一致。

**`mt6685_audclk` 的实现只能通过反汇编正在运行的模块或找 vendor 源码。
但 `.ko` 文件不存在，反汇编这条路暂时走不通。**

---

## 6. 边界（严格按指令）

**本轮未做、也不证明：**

| 未做/未证明 | 说明 |
|---|---|
| 时钟实际振荡 | 只读了 DT 和模块状态 |
| 模拟电压 | 未测量 |
| DAC 出声 | 未播放 |
| `mt6685_set_dcxo` 的实际行为 | 未读该模块代码（文件不存在） |

**未执行的操作**（按指令禁止）：

- ❌ 未读 Audio HP ImpeDance Setting 控件（vendor getter 会跑硬件测量）
- ❌ 未做 `tinymix` 全值转储
- ❌ 未做 codec debugfs 全寄存器扫描
- ❌ 未做任何 sysfs/control 写操作
- ❌ 未播放测试音
- ❌ 未清 dmesg 环

---

## 7. 给下一步的三条结论

| # | 结论 |
|---|---|
| 1 | **SID 定位已确证**：MT6685 = SID 9，MT6368 = SID 5，DT 与 sysfs 双源一致 |
| 2 | **判据未命中但原因是 dmesg 滚过**，不是校准缺失。**要拿到 trim 行需要更早的日志**（`console-ramoops`？或重启后立刻取） |
| 3 | **`mt6685_audclk.ko` 文件缺失**是新的待查项。若能取得，可反汇编确认 `mt6685_set_dcxo` 的实际 regmap 操作 |
