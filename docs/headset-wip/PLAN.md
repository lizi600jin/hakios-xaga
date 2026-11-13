# xaga 3.5mm 耳机：检测与出声 · 实施方案

> 目标：XNU（HakiOS）在 xaga 上做到 ① 耳机插上能识别 ② 能解码播放音频。
> 本文只写**已验证的事实**和**可执行的步骤**。未验证的一律标「未验证」。
> 材料来源：真机探测（stock Android，2026-11-14）+ 厂商 `.ko` 反汇编 + 设备树。

---

## 0. 先说三条会直接改变方案的事实

### 0.1 耳机检测不是 GPIO，是 MT6368 内部的一个模拟检测块

xaga 的 3.5mm 走 **MT6368 PMIC 内置 accdet**（`mediatek,mt6368-accdet`）。
它挂在 **SPMI 总线 slave 5**，寄存器 `0x2688`–`0x26d3`。

- 证据：DT 节点 `spmi@1c804000/second_pmic@5/accdet`，`reg = <0x05 0x00>`（`xaga.dts:1789-1791`、`:1802-1822`）
- 证据：真机 `/sys/bus/platform/drivers/pmic-codec-accdet/` 存在，`dump_reg` 打印 `0x2688..0x26d3` 与 `0x2508..0x2537`

**这决定了实现路径**：不需要碰 GPIO、不需要 GPIO 中断。需要的是 **SPMI 读写**。

### 0.2 SPMI 读写已经在这个项目里用过，但只验证过「读」

`mtk/mt6895/drivers/input/hakios.mt6895_keys.c` 里的 PMIF 事务在真机上**读通过了**（读 MT6363 的按键寄存器）。
同一个文件里已有关键常量：

| 项 | 值 | 出处 |
|---|---|---|
| PMIF 基址 | `0x1c804000` | `hakios.mt6895_keys.c:92` |
| 通道寄存器（mt8195 布局） | ACC `0x880` / WDATA `0x884` / RDATA `0x894` / VLDCLR `0x8a4` / STA `0x8a8` | 同上 `:132-136` |
| 读命令 | `(3 << 30) \| (sid << 24) \| (0 << 16) \| addr` | `:262` |
| **写命令** | `(3 << 30) \| BIT(29) \| (sid << 24) \| (0 << 16) \| addr`，数据写 WDATA | `work-notes-vstack-pmic.md:195-219` |
| 完成判据 | STA bits[3:1] == `6`（WFVLDCLR），然后写 1 到 VLDCLR | `:163-164` |
| 记忆法 | **写命令高 4 位恒为 `0xE`，读恒为 `0xC`** | `work-notes-vstack-pmic.md:219` |

**未验证**：写事务从未在真机上跑过。`work-notes-vstack-pmic.md` 是纯离线分析产出（该文档 §0 自述「全程不接触真机」）。

→ 所以第 1 步必须是**只读探测**，不能上来就写。

### 0.3 出声有两条完全不同的路，第一步应该走不需要数字音频的那条

| 路径 | 需要什么 | 难度 | 能否证明"codec 活着" |
|---|---|---|---|
| **A. MT6368 内置 SGEN** | 只需 SPMI 写 + HPL/HPR 上电 | 低 | 能（耳机里有正弦音） |
| B. AFE → MTKAIF → MT6368 → 耳机 | SPMI 写 + AFE MMIO `0x1e100000` + MTKAIF 校准 + DMA | 高 | 能 |
| C. 完整 /dev/hakios-audio → CoreAudio | A 或 B + 现有音频栈 | 最高 | 能 |

**路径 A 的关键证据**：MT6368 有内部正弦波发生器，`DAC In Mux` 可选 `Sgen`：

- kcontrol 实测：`DAC In Mux: >Normal Path Sgen`（真机 `tinymix`）
- `mt_sgen_event` @ `snd-soc-mt6368.ko:0x9b8c` 写 `0x2396`/`0x2393`/`0x2392`/`0x23c8`/`0x23c9`/`0x23ca`

即：**不接任何数字音频源，只靠 SPMI 写寄存器，就能让耳机发出正弦音。**
这是验证"MT6368 从 XNU 里能被驱动"的最短路径，也是本方案第 2 阶段的核心。

---

## 1. 已确证的硬件拓扑

```
MT6895 SoC
 ├── AFE @ 0x1e100000 (size 0x2000, IRQ GICv3 448 "Afe_ISR_Handle")
 │    └── MTKAIF  ←→ MT6368 codec（同一颗 PMIC 内部的数字接口）
 │
 └── SPMI @ 0x1c804000 (PMIF) / 0x1c801000 (SPMIMST)
      ├── slave 4 : MT6363  (主 PMIC，按键)
      ├── slave 5 : MT6368  (codec + accdet)   ← 耳机在这里
      └── slave 9 : MT6685  (音频时钟 DCXO)
```

| 项 | 值 | 证据 |
|---|---|---|
| AFE 物理基址 | `0x1e100000`，`0x2000` 字节 | `xaga.dts:10019-10021`；真机 `/proc/iomem` |
| AFE 中断 | GICv3 **448** Level，名 `Afe_ISR_Handle` | 真机 `/proc/interrupts:240` |
| AFE audio_sram | `0x1e102000`，`0x18000` | `xaga.dts:10033-10038` |
| MT6368 SPMI SID | **5** | `xaga.dts:1791` `reg = <0x05 0x00>` |
| MT6368 accdet 寄存器 | `0x2688` – `0x26d3`（52 字节） | 真机 `dump_reg` |
| MT6368 codec 寄存器 | `0x00c7` – `0x2710`（同 SID 5） | `snd-soc-mt6368.ko` 反汇编提取 |
| MT6368 AUDDEC_ANA | `0x2508` – `0x2537` | 真机 `dump_reg` |
| MT6685 SPMI SID | **9** | `xaga.dts:1731-1732` `reg = <0x09 0x00>` |
| accdet 中断 | SPMI 子号 **97**(`ACCDET_IRQ`) / **98**(`ACCDET_EINT0`)，均 Edge | 真机 `/proc/interrupts:240-241` |
| 中断到 AP 的路径 | `mtk-eint 243` → `rcs_irq` → `spmi-pmic-irq` | 真机 `/proc/interrupts:212/214` |
| 耳机输出 mux | `HPL Mux` / `HPR Mux`，取值含 `Open`/`Playback`/`Audio Playback`/`HP Impedance` | 真机 `tinymix` |

### 1.1 耳机模拟通路的实际走向（不要照 Android 猜测）

```
数字源 ──(MTKAIF)──> MT6368 ADDA ──> DAC ──> HPL/HPR Mux ──> 3.5mm
```

- 耳机走 **MT6368 内部 ADDA**，**不走 I2S 引脚**
- 扬声器走另一条：`I2S3` → TFA98xx 功放（PA 在外部）
- DAI link 对应关系（从 `mt6895-mt6368.ko` 的 `.rela.data` 重定位解析）：
  - `ADDA` → cpu `mt6368-sound` / codec `mt6368-snd-codec-aif1`
  - `ADDA_CH34` → cpu `mt6368-sound` / codec `mt6368-snd-codec-aif2`
  - `PCM 1` / `PCM 2` → `snd-soc-dummy`（**占位，不是耳机通路，别被名字骗**）

---

## 2. 耳机检测：寄存器语义

### 2.1 核心状态寄存器

| 寄存器 | 用途 | 证据 |
|---|---|---|
| **`0x26b6` bit[7:6]** | **插入状态（work 上下文）** —— 正确的那一个 | `accdet_work_callback` @`0x4b30` 读 → `0x4b50` `ubfx #6,#2` |
| **`0x26b8` bit[7:6]** | **插入状态（IRQ 上下文）** | `0x1f6c` 读 → `0x1f84` `ubfx #6,#2` → 跳表分派 |
| `0x26a9` | EINT pending 位（bit2/bit3），**不是插入状态** | 见 ACCDET-DECODE.md 的更正节 |
| `0x26aa` | 中断清除/使能（bit0 写 1 清） | `0x1e88-0x1e94` |
| `0x26b6` | ADC 结果（高/低） | `0x1e5c` |
| `0x26b8` | ADC 结果（高/低） | `0x1e70` |
| `0x26a3` / `0x26a4` | 阈值 / mic-bias 配置 | `accdet_init` `0x920-0x968` |

### 2.2 `0x26a9` 的位语义（**已被更正，见 ACCDET-DECODE.md 末尾**）

> ⚠️ 本节早期结论有误。`0x26a9` 的 bit2/bit3 是 EINT0/EINT1 的 pending 位，
> **插入状态在 `0x26b6`/`0x26b8` 的 bit[7:6]**（字段名 `cur_AB`，未插时 = 3）。
> 下面保留原始反汇编记录以备查，但结论以 ACCDET-DECODE.md 的更正节为准。

反汇编 `accdet_irq_handle` 开头（`0x1e00-0x1e30`）：

```
w10 = *(int*)(bss+192)        ; 0x26a9 的当前值
w9  = 读到的 0x26a9
tbnz w10, #2 → w19 = (w9 >> 2) & 1
tbnz w10, #3 → w19 = (w9 >> 2) & 2
tbnz w10, #4 → w19 = (w9 >> 2) & 3
否则          → w19 = 0
```

结论：
- **bit0**（后续 `tbz w20, #0` 判断）= **插入检测结果**
- bit2 / bit3 / bit4 = 三种中断/检测模式的选择位（对应 DT 的 `accdet-mic-mode`）
- `w19` 是 2-bit 的"检测档位"，非零时直接跳走（`0x1e7c cbnz w19`）

**未确定**：bit0=1 是"已插入"还是"未插入"。真机 `state=0` 且 `0x26a9=0x00` 时耳机未插，
但 `state` 变量与 `0x26a9` 的映射关系还需在**插入状态**下实测才能定死。

→ **第 1 阶段必须做一次带耳机的探测**（只读，不改系统状态）。

### 2.3 初始化序列（`accdet_init` @ `0x7c8`，已完整解出）

```
0x268a: update_bits(mask 0x02, val 0x02)
        usleep_range(2000, 3000)
0x268a: update_bits(mask 0x02, val 0x00)
        usleep_range(1000, 1500)

0x2697 = 0x00    0x2698 = 0x08      ← cust_pwm_deb[4] = 0x800 (低/高字节)
0x2699 = 0x00    0x269a = 0x08      ← cust_pwm_deb[5] = 0x800
0x269d = 0x20    0x269e = 0x00      ← cust_pwm_deb[6] = 0x20
0x269f = 0x44    0x26a0 = 0x00      ← cust_pwm_deb[7] = 0x44

0x26a3: update_bits(mask 0xf0, val = X << 4)     ; X 来自 .bss+0xd4
（若 .bss+0x100 == 1）0x26a4: update_bits(mask 0x0f, val = .bss+0xd8)
0x26a5 = .bss+0xe0
```

`cust_pwm_deb[4..7]` 与 DT `headset-mode-setting` 下标 4/5/6/7 精确对应：
DT 值 `[0x500,0x500,0x1,0x1f0,0x800,0x800,0x20,0x44,0x4,0x1,0x5,0x3,0x3,0x5,0xe]`
→ `deb0=0x800, deb1=0x800, deb3=0x20, deb4=0x44`，与真机 dmesg 打印 `accdet_dts:deb0=0x800,deb1=0x800,deb3=0x20,deb4=0x44` **完全吻合**。

**交叉验证成功**（DT 参数 ↔ 寄存器写入值 ↔ dmesg 三方一致）。

**真机基线核对（已完成，2026-11-14）**：把上面解出的 4 组去抖值与真机 `dump_reg` 逐项比对：

| 参数 | 低字节 | 高字节 | 拼合值 | DT 期望 | 结果 |
|---|---|---|---|---|---|
| deb0 | `0x2697=0x00` | `0x2698=0x08` | `0x0800` | `0x800` | ✅ |
| deb1 | `0x2699=0x00` | `0x269a=0x08` | `0x0800` | `0x800` | ✅ |
| deb3 | `0x269d=0x20` | `0x269e=0x00` | `0x0020` | `0x20` | ✅ |
| deb4 | `0x269f=0x44` | `0x26a0=0x00` | `0x0044` | `0x44` | ✅ |

**4/4 全对。反汇编解出的写序列与真机寄存器状态完全一致**，这是本方案最硬的一块证据。

**反推的 `.bss` 变量**（从运行期值倒推，不是从代码解出，标「未证明」）：

| 变量 | 反推值 | 依据 | 与 DT 对照 |
|---|---|---|---|
| `.bss+0xd4` | `0x3` | `0x26a3=0x35`，mask `0xf0`，高 4 位 | `headset-mode-setting[11]` = `0x3` 或 `[12]` = `0x3`（两处同值，无法区分） |
| `.bss+0xd8` | `0xe` | `0x26a4=0x5e`，mask `0x0f`，低 4 位 | `headset-mode-setting[14]` = `0xe` |
| `.bss+0xe0` | `0xe` | `0x26a5=0x0e`，整字节写 | `headset-mode-setting[14]` = `0xe` |

即这三个值都来自 DT 的 `headset-mode-setting` 数组，但不是简单按下标顺序；
具体是哪几个下标由 `accdet_probe` 的解析代码决定，**该函数尚未完整解码**。
好消息是：既然运行期值已知，重实现时**可以直接硬编码 `0x3` / `0xe` / `0xe`**，不必先解出映射。

---

## 3. 出声：三条路径与推荐顺序

### 3.1 路径 A：MT6368 内置 SGEN（推荐第 2 阶段）

**为什么先做这条**：只需要 SPMI 写，不碰 AFE、不碰 DMA、不碰 MTKAIF 校准。
一旦成，就同时证明了：SPMI 写事务可用 + MT6368 上电时序对 + 耳机模拟通路通 + HPL/HPR 能开。

SGEN 相关写入（`mt_sgen_event` @ `0x9b8c`，event == 1 = 开）：

```
0x2396 = 0x06
0x2393 = 0xcb
0x2392 = 0xa1
0x2396 = 0x03
0x2396 = 0x0b
0x23c8: update_bits(mask 0xff, val 0x00)
0x23c9: update_bits(mask 0xff, val 0x01)
0x23ca: update_bits(mask 0xff, val 0x00)
```

关（event == 8）：`0x2396 = 0x00` 起的一组对称写入。

耳机上电（`mt_hp_event` @ `0x8be0`，event == 1 = 开，且 `w20 == 1` 那条分支）：

```
0x2595 = 0x10
0x25a0: update_bits(mask 0x07, val 0x02)
0x25a1: update_bits(mask 0x30, val 0x10)
0x2597 = 0x00
0x2596 = 0x01
0x2595 = 0x10
0x2595 = 0x12
0x2595 = 0x13
0x2609 = 0x08
0x260a = 0x08
0x259f: ...
0x25a1: update_bits(mask ... 0xc0)
0x25a1: update_bits(mask ... 0x03)
0x259c = 0x87
0x2591: update_bits(mask 0x30, val 0x10)
```

**注意**：`mt_hp_event` 有两套分支（`w20 == 1` 走一套，否则走另一套 `0x8d40`）。
`w20` 来自 `dapm_kcontrol_get_value()`，即 DAPM 的路径选择。
两套具体差异尚未逐条比对 → **未定**。

### 3.2 路径 B：AFE → MTKAIF → MT6368（第 3 阶段）

需要的额外工作：
1. 映射 AFE MMIO `0x1e100000`
2. AFE 上电序列（时钟：`aud_afe_clk` 等 40+ 个时钟，来自 topckgen）
3. APLL1/APLL2 配置（`aud_apll22m_clk` / `aud_apll24m_clk`）
4. MTKAIF 校准（`mt6368_mtkaif_calibration_enable` @ `snd-soc-mt6368.ko:0x560`：
   写 `0xd5`/`0xd4`/`0xd8`/`0xd7`，然后 `mt6685_set_dcxo`，再 `0x25a2`/`0x2537`/`0x230c`/`0x23d1`/`0x23d0`/`0x23d2`）
5. AFE 的 DL 通道 → ADDA 的映射（`ADDA_DL_CH1 DL2_CH1` 等 kcontrol）
6. DMA 缓冲区

**这条路的时钟链是最大风险**：MT6368 音频时钟依赖 **MT6685 的 DCXO**（`mt6685_set_dcxo`，
在 `snd-soc-mt6368.ko` 里有 8 处调用）。MT6685 在 **SPMI slave 9**，需要单独初始化。

### 3.3 路径 C：接现有 `/dev/hakios-audio`（最后）

现有音频栈 `drivers/audio/` 全部是 **redwood（POCO X5 Pro / 高通）** 的，xaga 一行都用不上。
`/dev/hakios-audio` 本身是通用的（`drivers/audio/hakios.audio.c`），但它需要"一个播放它的 sink"。
在 xaga 上这个 sink 就是路径 A 或 B 实现出来的。

---

## 4. 分阶段实施计划

### 阶段 0：只读基线对拍（**不需要耳机**）

**约束**：手头没有 3.5mm 耳机，物理插拔验证无法进行。
「插入后」的寄存器行为只能从代码推，不能实测 —— 这一点贯穿全文，凡涉及插入态一律标「未验证」。

**替代做法：用 Android 的已知值当基准，验证裸机的 SPMI 读路径。**

Android 侧已经有一份权威读数（`dump_reg`，52 + 48 字节全量）。XNU 自己读同样的寄存器，
逐字节比对 —— 对得上，就证明：

1. XNU 的 PMIF 通道/命令字/SID 全对
2. XNU 读到的是同一颗 MT6368 的同一批寄存器
3. 后续所有写操作的目标地址可信

| # | 裸机动作 | 与什么比 | 判定 |
|---|---|---|---|
| 1 | 读 SID=5 的 `0x2688`–`0x26d3` | Android `dump_reg` 的 ACCDET_RG 段 | 逐字节一致 |
| 2 | 读 SID=5 的 `0x2508`–`0x2537` | Android `dump_reg` 的 AUDDEC_ANA_RG 段 | 逐字节一致 |
| 3 | 读 SID=5 的 `0x24E`（VBUCK2 VOSEL） | 笔记记的 `0x6E` | 一致 |
| 4 | 读 SID=4 的 `0x001e` | 现有 keys 驱动已用 | 照旧通 |

**这不是替代品，是更强的验证**：Android 的 `dump_reg` 是它自己驱动初始化后的稳态，
XNU 读到同样的值，说明 XNU 是在同一状态下看同一对象。

**注意一个陷阱**：Android 的稳态是"驱动已初始化"。XNU 裸机启动后，MT6368 的 accdet 部分
**可能已经被 LK 或 Android 前一次运行初始化过**，也可能是复位态。两种情况都值得记录 ——
如果读到的值与 Android 稳态一致，说明 LK/前次运行留下了状态（这本身是重要信息）；
如果全是复位值，说明 accdet 需要完整重新初始化。

**附带产出**：`0x26a9` 在**未插入**状态下的值（Android 报 `0x00`）。这是唯一能确认的一个端点。
插入态的 `0x26a9` 值仍然未知 —— 只能从代码推。

### 阶段 1：SPMI 写事务验证（离线 + 一个只读/幂等写）

1. 在 `hakios.mt6895_keys.c` 的 `pmic_read_on()` 旁边加 `pmic_write_on()`（照读事务结构，cmd 加 `BIT(29)`）
2. **验证写事务是否工作**：选一个**写回原值**的幂等操作。
   例如读 `0x24E`（VBUCK2 VOSEL，真机 `0x6E`）再写回 `0x6E`，然后读回确认还是 `0x6E`。
   风险：即使写错地址也只影响电压选择，且写的是同一个值。
3. **更安全的选择**：accdet 的 `0x2697`–`0x26a0` 是去抖参数寄存器，
   真机值 `0x2697=0x00 0x2698=0x08 ...`，写回同样的值即可验证写通道。
4. 若写通道验证通过 → 阶段 2 才有意义

**风险控制**：绝不能写 `0x14B`（MT6368 全局 shutdown，见 `work-notes-vstack-pmic.md:163`）。
绝不能写 `0x240` 做 read-modify-write（那是状态影子，开关走 `+1`/`+2` 别名）。

### 阶段 2：路径 A —— 让耳机发出正弦音

1. **先读** MT6368 codec 侧关键寄存器，确认与 Android 下一致（`0x2591`/`0x2595`–`0x25a1`/`0x25c*` 等）
2. 按 `mt_hp_event`（event=1）的序列写耳机模拟上电
3. 按 `mt_sgen_event`（event=1）的序列开 SGEN
4. 设 `HPL Mux` / `HPR Mux` = `Audio Playback`（写 vs 走 DAPM 的 kcontrol 路径需确认）
5. **判据**：耳机里有声音

**这一步成功了，就等于证明了整条模拟链路。**

### 阶段 3：耳机检测集成

1. 新建 `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c`
2. 上电后按 `accdet_init` 序列初始化 accdet
3. **轮询 `0x26b6` bit[7:6]**（不做中断：中断需要 GIC + SPMI 中断控制器两级接线，轮询足够且现有 keys 驱动就是轮询）
4. 状态变化时 `printf` 报告 + 更新一个可被 `/dev/hakios-audio` 查询的标志
5. 可选：GIC 接 `Afe_ISR_Handle`(448) 或 SPMI 中断（后做）

### 阶段 4：数字音频播放

视阶段 2 的结果决定走 AFE/MTKAIF（路径 B），把 `/dev/hakios-audio` 的写入喂给它。

---

## 5. 需要新建的代码

| 文件 | 内容 | 依赖 |
|---|---|---|
| `mtk/mt6895/drivers/audio/hakios.mt6895_spmi.c/.h` | SPMI 读**写**事务（从 keys 驱动抽出） | 无 |
| `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c` | accdet 初始化 + 插入检测 + MT6368 上电 + SGEN | 上面那个 |
| `mtk/mt6895/drivers/audio/conf` | `driver MT6895_HEADSET` | — |
| `mtk/mt6895/configs/mt6895.config` | `CONFIG_HAKIOS_MT6895_HEADSET=y` | — |
| `devices/xaga/configs/xaga.config` | `headset=N` 启动参数 | — |

**命名规范**（照现有）：`hakios.<soc>_<thing>.c`，文件放 `bsd/dev/`，`drivers/map` 注册。

---

## 6. 风险清单

| 风险 | 后果 | 规避 |
|---|---|---|
| 写 `0x14B` | MT6368 整颗芯片关机 | 硬禁：代码里加断言 |
| 写错 SPMI 地址到 slave 4（MT6363） | 改到电源管理寄存器 | 所有地址写死 SID=5，做成常量 |
| SPMI 写时 STA 卡在非 6 | 事务不完成，可能挂总线 | 加 spin limit（现有代码是 200000），超时直接返回 |
| 访问 PMIF 窗口外（`0x1c804000+0x900` 以上） | **总线 abort，核停机，板子重启**（项目里已 3 次） | 只碰 `0x880`–`0x8a8`，`PMIF_CHAN_COUNT=1` |
| MT6685 未初始化导致 codec 无时钟 | SGEN 无输出 | 阶段 2 先试；失败再补 MT6685 |
| 耳机口物理损坏 | 所有测量为空 | 阶段 0 就能发现 |

---

## 7. 与 HiFi 工作区资料的差异（已核对）

HiFi 侧的 `work/XAGA_JACK_FOR_IOS.md`（2026-11-14 00:58 写入）§4 声称三条"真机实测"，
与本次真机探测**直接矛盾**：

| 该文档声称 | 本次真机实测 | 判定 |
|---|---|---|
| 「tinymix 在 ROM 里不存在」 | `/system/bin/tinymix` 存在，51672 字节 | **错**（该文档自己 §一 表格也写了"ROM 里不存在"，但 §四 又用 tinymix 读数，自相矛盾） |
| 「耳机插着时 `Headphone Plugged In` 回显 `On`」 | 当前 `Off`（耳机未插，无法验证插入态） | **未验证**：无原始日志，且与"从未插入"的中断计数 0 一致 |
| 「`pcm3p` RUNNING」 | `pcm3p/sub0/status` = `closed` | 当前不成立 |

该文档 §六「工作区里明确没有的东西」中列的 "xaga 的内核源码树"、"jack 检测的设备树节点"，
**本项目里都有**：`/home/lizi/IOS/work-xaga/xaga.dts`（405286 字节）+ 真机 `/proc/device-tree/`。

→ **以本项目的真机探测与 `.ko` 反汇编为准，HiFi 文档只取"设备画像"部分。**

---

## 8. 引用文件

| 路径 | 内容 |
|---|---|
| `docs/headset-wip/DEVICE-PROBE-STOCK.md` | 真机全量探测（1595 行） |
| `docs/headset-wip/HIFI-WORKSPACE-MINING.md` | HiFi 工作区挖掘（323 行） |
| `docs/headset-wip/ACCDET-DECODE.md` | accdet 驱动完整反汇编解码（进行中） |
| `docs/headset-wip/ko/*.ko` | 6 个厂商音频模块 |
| `docs/headset-wip/tinymix-a.txt` | 564 个 mixer 控件原文 |
| `/home/lizi/IOS/work-xaga/xaga.dts` | 设备树（唯一硬件事实来源） |
| `/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/input/hakios.mt6895_keys.c` | SPMI 读事务实现 |
| `/home/lizi/IOS/hakios-mtk/work-notes-vstack-pmic.md` | SPMI 写事务格式 |

---

## 9. 实现进度（2026-11-14 01:52）

### 已完成

| 项 | 状态 |
|---|---|
| accdet 寄存器语义 | 19/19 真机对拍通过 |
| 只读对拍工具 | 写好，10 个自检用例通过 |
| 驱动 `headset=1` 只读模式 | **已写，编译通过**（`CC hakios.mt6895_headset.o`，零 error） |
| 构建接入 | conf / config / 头文件 / stubs / 调用点全部就位 |

驱动路径：`mtk/mt6895/drivers/audio/hakios.mt6895_headset.c`（131 行）

安全约束（已写进代码注释）：
- 只读，无 WDATA 访问、无写命令
- 只碰 PMIF `0x880`–`0x8a8`
- 不写 `0x014b`（全局 shutdown）
- 在 keys 驱动启动前同步执行，避免同一通道无锁并发

### 一个同步陷阱（已记录）

`xnu/sync-drivers.sh` **只同步 `drivers/`，不同步 `xnu/files/` 下的头文件**。
改了 `xnu/files/bsd/sys/hakios.audio.h` 后必须手动复制到 `xnu-src/` 树，
否则构建报 `implicit declaration`（本次实际踩到）。

### 下一步（未做）

1. **上机跑 `headset=1`**：读出的值应与 `accdet-baseline.json` 的 124 个块寄存器逐字节一致
2. `headset=2`：完整初始化 + 轮询 `0x26b6` 的 bit[7:6]（不是 `0x26a9` bit0；未插 = 3，已由基线确认）
3. 耳机到手后：补 ADC→阻抗换算（当前完全缺失，阻断三态分类与按键）

### 编译验证的实际结果（精确边界）

构建日志 `/tmp/build2.log` 的三条证据：

| 证据 | 内容 |
|---|---|
| 驱动编译 | `CC hakios.mt6895_headset.o` —— 通过 |
| 内核链接 | `LD mach.development.bcm2837.unstripped` —— 通过，产物 19070192 字节 |
| 构建失败点 | `exported name not in import list: OSObject::_Dispatch(...)` → `check_all_exports` |

**所以准确的说法是：驱动编译通过、内核链接成功、最终报错在事后的符号表校验（`check_all_exports`），
且该报错与耳机驱动无关**（是 `IOUserClient::CopyClientMemoryForType_Invoke` 等
IOKit 符号的导出清单问题，属项目既有状态）。

不是"整个构建成功"，也不是"构建因耳机失败"。

### 一次被驳回的架构改动（记录）

6.1sol 为了给耳机驱动加 `drivers/map` 条目，改了四个构建工具：
`xnu/apply.sh`、`xnu/sync-drivers.sh`、`xnu/export.sh`、`tools/drivers-check.py`。

**已回滚，理由有实证**：

1. 项目里其他 mt6895 驱动（`mt6895_keys.c`、`mali610.c`、`nvt.c`、`mt6895_usb.c`）
   都**不在** `drivers/map` 里，却能正常构建 —— 它们由 `hakios-config.py external-files` 管理。
2. 本项目在 01:47 实测过：**map 里没有条目时**，`sync-drivers.sh` 照样输出
   `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c -> bsd/dev/hakios.mt6895_headset.c`，
   且构建照样产出 `CC hakios.mt6895_headset.o`。
3. 那四个改动会影响**所有** SoC 驱动的构建路径，属于项目架构，不是耳机任务的范围。

回滚后复验：`sync-drivers.sh` 报 `drivers in step`，树里的驱动与仓库 `cmp` 一致，
`conf/files` 登记在位，`drivers-check` 只剩一条**回滚前就存在**的 `drivers/usb` 问题。

**结论**：耳机驱动走既有的 external-files 路径即可，不需要改构建工具。
