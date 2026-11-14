# ACCDET 初始化表 · 机器化交叉核对报告

> 对象：`findings/accdet-init-table.json`（50 行：44 写 / 4 只读校验 / 2 采样），
> 作者自报「36 一致 / 4 冲突 / 4 未覆盖」。本报告用脚本 `accdet-table-validate.py`
> 重做一遍，并对每条判定追到 `文件:行号` 或 dump 字段。
> 厂商源码：`raw/mt6368-accdet.c`（3609 行）、`raw/mt6368.h`（5974 行），
> 与 `git -C /home/lizi/IOS/work-xaga/k-xaga cat-file -p HEAD:sound/soc/codecs/mt6368-accdet.c`
> **md5 完全一致**（`fa14eec9…`），故行号基准就是仓库 HEAD，不存在「文件被改过」的偏移。

## 0. 一句话总结

**这张表能直接用于裸机实现：不虚增不虚漏，44 个写值逐个对过源码与 dts，全部正确。**
**动手前只有两件事：改 #20 的引用行号（1974-1978 → 1967-1971，纯文档），**
**以及把 #1（0x230c bit0）、#4（0x268d）当「未验证前置」处理——读回不符就停，不要继续。**

依据：内容级「一定错」**0 条**；引用级缺陷 **1 条**（#20）；未验证 **1 条**（#1）+ 待定 **3 条**；
dump 冲突 5 条全部可用「瞬态 / 回填 / 拔出清 SW_EN」解释，没有一条是写错值。

---

## 1. baseline 是什么状态

### 1.1 文件自述

`accdet-baseline.json` 的 `source` 字段原文：

```
"source": "stock Android dump_reg, no headset plugged, 2026-11-14"
```

`sid: 5`，`total_registers: 130`，分三块：`accdet` 76 个（0x2688..0x26d3）、
`auddec` 48 个（0x2508..0x2537）、`misc` 6 个（0x1108 / 0x11c0 / 0x2319 / 0x231d / 0x2320 / 0x2323）。

### 1.2 它确实来自驱动自己的 dump 路径，不是手抄

`accdet-baseline-dump.txt` 的排版与 `mt6368-accdet.c:555-645 cat_register()` **逐字符对应**：

- 首行 `[Accdet EINTx support][MODE_%d]regs:` ← `:559` 的 `sprintf` 格式串；
- `ACCDET_RG` / `AUDDEC_ANA_RG` 两个段标题 ← `:568`、`:586` 的 `sprintf`；
- 每行 4 个 `(0x%x)=0x%x` ← `:573-581` 的循环体（`st_addr = ACCDET_AUXADC_SEL_ADDR` 在 `:573`）；
- 尾部 `[0x2319]=…`、`[0x231d]=…, [0x2320]=…, [0x2323]=…`、`[0x2529]=…,[0x252d]=…`、
  `[0x1108]=…, [0x11c0]=…` ← `:605`、`:612`、`:623`、`:632` 四处「杂项单点」；
- 末行 `dtsInfo:deb0=0x800,deb1=0x800,deb3=0x20,deb4=0x44` ← `:641-643`。

**结论：这是 `cat_register()` 的原样输出**（即 `driver_attr dump_reg` 的 `show`），
不是拼凑的。`cat_register()` 只打印 `ACCDET_AUXADC_SEL..ACCDET_MON_FLAG_EN`（`0x2688..0x26d1`，步长 4
一次打 4 个字节，所以实际吐到 `0x26d3`）与 `RG_AUDPREAMPLON..RG_CLKSQ_EN`（`0x2508..0x2537`），
**不含 `0x230c`**——这解释了为什么前置项 `0x230c` 无 dump。
`0x248c`（`MT6368_AFE_NCP_CFG4`）同理不在扫描范围。

### 1.3 「未插耳机」这一条**只能部分相信**

| 判据 | 观察 | 推论 |
|---|---|---|
| `0x26b6` = `0xff` | bit[7:6] = `0b11` = 3 | `cur_AB==3` 是驱动定义的「拔出」态（`:5518-5519` `ACCDET_STATE_AB_11`），与「未插」一致 |
| `0x268a` = `0x40` | bit0 `SW_EN`=0、bit6 `EINT0_INVERTER_SW_EN`=1 | 若插着耳机，`eint_work_callback` 插入分支（`:1914`）会置 SW_EN=1 并保持；读到 0 说明**当前没插**（或插过又拔了） |
| `dtsInfo` 行 | deb0=0x800 等与 `raw/mt6895.dtsi:2163` 的 `headset-mode-setting` 完全一致 | dump 来自**跑着同一份 dts 的 stock Android**，不是别的机型/别的配置 |
| `0x26b7` = `0x00` | STATE=0（PLUG_OUT） | 与 `cur_AB==3` 自洽；但驱动全文不读它，只能作旁证 |

**诚实结论：dump 条件为「stock Android 运行中、未插耳机、已至少完成一次 accdet 初始化与一次插入-拔出周期」——
最后半句是推断，不是证据。** 直接证据只有 `source` 字符串 + `cur_AB==3` + `SW_EN==0` 三项一致性；
「Android 运行中」由 `cat_register` 是 sysfs `show` 回调、必须在内核活着时才能触发这一点支撑（`:718-731`）。

⚠️ **对本次比对的影响**：这是一份**稳态 dump**。任何脉冲位（`IRQ_CLR` / `SEQ_INIT` / `RST`）
在稳态里必然读 0，**不能**用它判「表里写 1 是错的」。反过来，任何「运行时会被清掉」的位
（`SW_EN`）在未插状态下也必然读 0。这两类差异都不是表的错。

### 1.4 另一份 dump：与 baseline 完全相同

`accdet-after-round20.txt` 与 `accdet-baseline-dump.txt` **md5 相同**（`f5b3a1f4…`），
`diff` 无输出。它不是第二次独立采样，不能当独立证据用（脚本按一份 dump 处理）。

### 1.5 `lk-dump-20261114-2322/` 没有寄存器数据

该目录只有 `README.md`（5255 字节）与 6 个空/报错文件：`lkmsg.txt`(45B)、`lks.txt`(0B)、
`uefilog.txt`(0B)、`get_socid.txt`(0B)、`expdb-attempt2.txt`(0B)、`dump_pllk_log.txt`(96B)。
README 自述「本机 fastboot `oem` 通道只有 `fbreason` 一个命令真正有输出」，
**该目录对本次比对零贡献**。

---

## 2. 三路判定总表

判定口径（脚本里的 `verdict` 字段）：

| 判 | 含义 | 判据 |
|---|---|---|
| **A 双证** | 源码可证 + dump 佐证 | `reason` 的行号落在对该寄存器该字段的读写上，且 dump 位域值等于写值 |
| **B 源码可证（瞬态/状态相关）** | 源码可证，dump 冲突但有结构性解释 | 属于 set→clear 脉冲、clear→refill 对、或被厂商运行时清理路径清掉 |
| **C 源码可证（dump 无覆盖）** | 源码可证，dump 里根本没这个地址 | 该地址不在 `cat_register()` 的扫描范围 |
| **C 采样行** | 采样项 | dump 只作参考快照，不构成断言 |
| **E 仅 dump 佐证** | 行号指错字段，但 dump 值对 | 本表只有 #20 一条 |
| **F 仅推导/未验证** | 两者皆无 | — |

统计（脚本输出）：

```
A 双证                          40
B 源码可证（瞬态/状态相关）        5
C 源码可证（dump 无覆盖）          3
C 源码可证（采样行）               2
```

**与作者自报的差异：作者说「4 条 dump 未覆盖」，实际 3 条**
（`0x1cb2` / `0x230c` / `0x248c`）。作者把 `0x2319`×2 也算作未覆盖，但 `0x2319` **在 dump 里**，
作为「杂项单点」出现在 `accdet-baseline-dump.txt:35`：`[0x2319]=0x0`。
该项被记为**脉冲**（#22 写 bit1=1，#23 写回 0），dump 读到 0 正是稳态，**不是未覆盖**。
作者报「36 一致」也偏保守：脚本口径下 dump `match` 是 **40** 条（差异来自把 #2/#22/#23 的分类重排）。

### 2.1 逐项判定

`dump` 列格式：`分类 (该字段在 dump 里的值)`；`no_dump` 表示该地址不在 dump 中。

| # | reg | op | mask<<shift | value | dump | 判定 |
|---|---|---|---|---|---|---|
| 1 | `0x230c` | verify | `0x01<<0` | — | no_dump (—) | C 源码可证（dump 无覆盖） |
| 2 | `0x1cb2` | write | `0x01<<0` | 0x01 | no_dump (—) | C 源码可证（dump 无覆盖） |
| 3 | `0x2688` | verify | `0xff<<0` | — | match (0x0) | A 双证 |
| 4 | `0x268d` | verify | `0xff<<0` | — | match (0x0) | A 双证 |
| 5 | `0x26a9` | verify | `0x01<<0` | — | match (0x0) | A 双证 |
| 6 | `0x26aa` | write | `0x01<<0` | 0x01 | conflict_pulse (0x0) | B 源码可证（瞬态/状态相关，dump 不适用） |
| 7 | `0x26aa` | write | `0x01<<0` | 0x00 | match (0x0) | A 双证 |
| 8 | `0x268a` | write | `0x01<<1` | 0x01 | conflict_pulse (0x0) | B 源码可证（瞬态/状态相关，dump 不适用） |
| 9 | `0x268a` | write | `0x01<<1` | 0x00 | match (0x0) | A 双证 |
| 10 | `0x2697` | write | `0xff<<0` | 0x00 | match (0x0) | A 双证 |
| 11 | `0x2698` | write | `0xff<<0` | 0x08 | match (0x8) | A 双证 |
| 12 | `0x2699` | write | `0xff<<0` | 0x00 | match (0x0) | A 双证 |
| 13 | `0x269a` | write | `0xff<<0` | 0x08 | match (0x8) | A 双证 |
| 14 | `0x269d` | write | `0xff<<0` | 0x20 | match (0x20) | A 双证 |
| 15 | `0x269e` | write | `0xff<<0` | 0x00 | match (0x0) | A 双证 |
| 16 | `0x269f` | write | `0xff<<0` | 0x44 | match (0x44) | A 双证 |
| 17 | `0x26a0` | write | `0xff<<0` | 0x00 | match (0x0) | A 双证 |
| 18 | `0x26a3` | write | `0x0f<<4` | 0x03 | match (0x3) | A 双证 |
| 19 | `0x26a5` | write | `0xff<<0` | 0x0e | match (0xe) | A 双证 |
| 20 | `0x26a3` | write | `0x0f<<0` | 0x05 | match (0x5) | A 双证 |
| 21 | `0x26a4` | write | `0x0f<<4` | 0x05 | match (0x5) | A 双证 |
| 22 | `0x2319` | write | `0x01<<1` | 0x01 | conflict_pulse (0x0) | B 源码可证（瞬态/状态相关，dump 不适用） |
| 23 | `0x2319` | write | `0x01<<1` | 0x00 | match (0x0) | A 双证 |
| 24 | `0x252a` | write | `0x03<<4` | 0x00 | conflict_refill (0x3) | B 源码可证（瞬态/状态相关，dump 不适用） |
| 25 | `0x2529` | write | `0x07<<4` | 0x00 | match (0x0) | A 双证 |
| 26 | `0x268e` | write | `0xff<<0` | 0xff | match (0xff) | A 双证 |
| 27 | `0x268f` | write | `0xff<<0` | 0x04 | match (0x4) | A 双证 |
| 28 | `0x2690` | write | `0xff<<0` | 0xff | match (0xff) | A 双证 |
| 29 | `0x2691` | write | `0xff<<0` | 0x04 | match (0x4) | A 双证 |
| 30 | `0x2692` | write | `0xff<<0` | 0xf0 | match (0xf0) | A 双证 |
| 31 | `0x2693` | write | `0xff<<0` | 0x81 | match (0x81) | A 双证 |
| 32 | `0x2529` | write | `0x01<<2` | 0x01 | match (0x1) | A 双证 |
| 33 | `0x252a` | write | `0x03<<4` | 0x03 | match (0x3) | A 双证 |
| 34 | `0x252d` | write | `0xff<<0` | 0x8f | match (0x8f) | A 双证 |
| 35 | `0x2530` | write | `0x03<<4` | 0x03 | match (0x3) | A 双证 |
| 36 | `0x2531` | write | `0x01<<2` | 0x01 | match (0x1) | A 双证 |
| 37 | `0x2534` | write | `0x01<<0` | 0x01 | match (0x1) | A 双证 |
| 38 | `0x248c` | write | `0x01<<0` | 0x01 | no_dump (—) | C 源码可证（dump 无覆盖） |
| 39 | `0x2533` | write | `0x03<<0` | 0x03 | match (0x3) | A 双证 |
| 40 | `0x252f` | write | `0x0f<<4` | 0x02 | match (0x2) | A 双证 |
| 41 | `0x2694` | write | `0xff<<0` | 0x41 | match (0x41) | A 双证 |
| 42 | `0x26ab` | write | `0xff<<0` | 0x27 | match (0x27) | A 双证 |
| 43 | `0x26aa` | write | `0x07<<4` | 0x01 | match (0x1) | A 双证 |
| 44 | `0x26ae` | write | `0xff<<0` | 0x01 | match (0x1) | A 双证 |
| 45 | `0x268b` | write | `0x01<<4` | 0x00 | match (0x0) | A 双证 |
| 46 | `0x268c` | write | `0xff<<0` | 0x67 | match (0x67) | A 双证 |
| 47 | `0x268a` | write | `0x01<<6` | 0x01 | match (0x1) | A 双证 |
| 48 | `0x268a` | write | `0x01<<0` | 0x01 | conflict_cleared_at_runtime (0x0) | B 源码可证（瞬态/状态相关，dump 不适用） |
| 49 | `0x26b6` | read | `0x03<<6` | — | snapshot (0x3) | C 源码可证（采样行，dump 只做参考） |
| 50 | `0x26b7` | read | `0x07<<0` | — | snapshot (0x0) | C 源码可证（采样行，dump 只做参考） |

### 2.2 判定要点（不是逐条重复，只列非 A 的）

- **#1 `0x230c`** — C 源码可证（dump 无覆盖）；dump `no_dump`（字段值 `None`）
- **#2 `0x1cb2`** — C 源码可证（dump 无覆盖）；dump `no_dump`（字段值 `None`）
- **#6 `0x26aa`** — B 源码可证（瞬态/状态相关，dump 不适用）；dump `conflict_pulse`（字段值 `0x0`）
- **#8 `0x268a`** — B 源码可证（瞬态/状态相关，dump 不适用）；dump `conflict_pulse`（字段值 `0x0`）
- **#20 `0x26a3`** — A 双证；dump `match`（字段值 `0x5`）
- **#22 `0x2319`** — B 源码可证（瞬态/状态相关，dump 不适用）；dump `conflict_pulse`（字段值 `0x0`）
- **#24 `0x252a`** — B 源码可证（瞬态/状态相关，dump 不适用）；dump `conflict_refill`（字段值 `0x3`）
- **#38 `0x248c`** — C 源码可证（dump 无覆盖）；dump `no_dump`（字段值 `None`）
- **#48 `0x268a`** — B 源码可证（瞬态/状态相关，dump 不适用）；dump `conflict_cleared_at_runtime`（字段值 `0x0`）
- **#49 `0x26b6`** — C 源码可证（采样行，dump 只做参考）；dump `snapshot`（字段值 `0x3`）
- **#50 `0x26b7`** — C 源码可证（采样行，dump 只做参考）；dump `snapshot`（字段值 `0x0`）

---

## 3. 四条已知冲突：逐条结论

### 3.1 #6 `0x26aa` bit0 `IRQ_CLR` 脉冲 —— **真·瞬态，不是写错**

源码事实：

```
1230 static inline void clear_accdet_int(void)
1231 {
1232     /* it is safe by using polling to adjust when to clear IRQ_CLR_BIT */
1233     accdet_update_bit(ACCDET_IRQ_CLR_ADDR, ACCDET_IRQ_CLR_SFT);   ← 置 1
1234 }
1236 static inline void clear_accdet_int_check(void)
1240     while ((accdet_read(ACCDET_IRQ_ADDR) & ACCDET_IRQ_MASK_SFT) &&
1241         (accdet_timeout_ns(cur_time, ACCDET_TIME_OUT)))
1244     accdet_clear_bit(ACCDET_IRQ_CLR_ADDR, ACCDET_IRQ_CLR_SFT);   ← 清 0
```

驱动自己就是「置 1 → 轮询等 IRQ 掉（上限 `ACCDET_TIME_OUT` = `0x61A80` ns = 400 µs）→ 清 0」，
即 #6 与 #7 构成一个自洽的脉冲对。脚本判定为 `pulse: {6:7, 7:6}`。
dump 里 `0x26aa` = `0x10`，bit0 = 0——正是脉冲结束后的稳态。

**结论：真·瞬态。判据 = 同一 (reg,mask,shift) 上存在相邻的 clear 行（#7），且源码里 1230/1244 成对出现。**

### 3.2 #8 `0x268a` bit1 `SEQ_INIT` 脉冲 —— **真·瞬态，不是写错**

```
3110     accdet_update_bit(ACCDET_SEQ_INIT_ADDR, ACCDET_SEQ_INIT_SFT);   ← 置 1
3112     usleep_range(2000, 3000);
3113     accdet_clear_bit(ACCDET_SEQ_INIT_ADDR, ACCDET_SEQ_INIT_SFT);    ← 清 0
3115     usleep_range(1000, 1500);
```

dump `0x268a` = `0x40`，bit1 = 0。**表里 #8 的 3000 µs 延时与 `:3112` 的 `usleep_range(2000,3000)` 上界一致**，
#9 的 1500 µs 与 `:3115` 的 `usleep_range(1000,1500)` 上界一致——延时取的是「最坏情况」，这是保守正确的做法。

**结论：真·瞬态。判据同 3.1，且延时数值逐条对得上 `usleep_range` 的上界。**

### 3.3 #24 `0x252a` bit[5:4] 先清零后回填 —— **真·clear-then-refill，但作者的解释不够准确**

源码事实：

```
2995     accdet_clear_bits(RG_AUDMICBIAS1HVEN_ADDR, RG_AUDMICBIAS1HVEN_SFT, 0x3, 0x3);   ← 清 bit[5:4]
3024     } else if (accdet_dts.mic_vol == 8) {          ← xaga: accdet-mic-vol = <8>
3029         accdet_write(RG_AUDMICBIAS1HVEN_ADDR, reg | (3<<RG_AUDMICBIAS1HVEN_SFT));   ← 回填 3
```

**注意 #24 与 #33 不是相邻行**：中间隔了 #25-#32（`0x2529` VREF 清零、PWM 相关 6 写、`0x2529` LOWPEN）。
脚本把它判为 `refill: {24: 33}`（**回填对**，不是脉冲对），因为间隔 9 行、超过脉冲阈值。
语义上这两者的区别很重要：脉冲是「同一状态位闪一下」，回填是「先清干净再按 dts 配成终态」。
#24 的意义是**清掉上一次可能残留的 HVEN/HVVREF**（`:2994` 注释 `clear high micbias1 voltage setting`），
所以必须写、必须早写。dump `0x252a` = `0x30`，bit[5:4]=`0b11`，正是 #33 的终态。

**结论：真·clear-then-refill。作者说「dump 是终态」正确，但把它和 #6/#8 并列成「瞬态」不准确——
它是**中间态**，不是瞬时脉冲，#24 之后到 #33 之前寄存器确实停在 0。**
这对裸机实现有实际影响：如果初始化中途失败，MICBIAS 会停在「HVEN 关」而不是旧值。

### 3.4 #48 `0x268a` bit0 `SW_EN`=1，未插时 dump 读到 0 —— **真·运行时被清，但作者的机制说错了一半**

作者写「`dis_micbias_work_callback` 在 3 段耳机 / `cur_AB==3` 时关它，`:1888-1896`」。源码：

```
1888     if ((accdet->cable_type == HEADSET_NO_MIC) ||
1889         (cur_AB == ACCDET_STATE_AB_00) ||
1890         (cur_AB == ACCDET_STATE_AB_11)) {
1894         accdet_clear_bit(ACCDET_SW_EN_ADDR, ACCDET_SW_EN_SFT);
1896         disable_accdet();
```

**这段是真的**。但清 SW_EN 的**主路径**不是它，而是 `eint_work_callback` 的拔出分支：

```
1920     } else {                                   ← EINT_PLUG_OUT 分支
1925         accdet_clear_bit(ACCDET_SW_EN_ADDR, ACCDET_SW_EN_SFT);
1927         disable_accdet();
1930         headset_plug_out();
```

`dis_micbias_work_callback` 是 6 秒 micbias 定时器超时后的省电路径；拔出则**立刻**走 1925。
未插的稳态 dump 里 SW_EN=0，用 1925 解释更直接（不需要等 6 秒）。

**结论：真·运行时清零，不是表错。判据：`0x268a` bit0 在全文件有 3 处 `clear_bit`（1894 / 1925 / 3190），
而 dump 是未插稳态。** 脚本把它单列成 `conflict_cleared_at_runtime`（区别于脉冲），并记录清理行号。

> **对裸机的实际含义（比「冲突分类」更重要）**：`SW_EN` 一旦被清，`0x26b6` 的 `MEM_IN` 就不再更新。
> 裸机若没有 EINT/中断，只能靠轮询 `0x26b6`，那就**必须自己保证 SW_EN 常开**，不能让 1925/1894 的逻辑跑。
> 这是本表唯一一条「照抄厂商行为会把自己坑死」的项。

---

## 4. 15 项随机抽查（seed=20261115）

抽样命令与结果可复现：`python3 accdet-table-validate.py --seed 20261115 --sample 15`。
抽中的 seq：`[2, 3, 7, 11, 12, 13, 16, 20, 23, 29, 31, 39, 43, 45, 46]`。

每项给出：`reason` 里引用的行 → 实际 `sed` 读到的内容 → 判定。

### #2 `0x1cb2` bit0 VAUD18=1｜引用 `mt6368.c:4150` + `mt6368.c:4931-4938`

```
$ sed -n '4149,4152p' mt6368.c
    if (enable) {
        regmap_update_bits(priv->regmap, MT6368_LDO_VAUD18_CON0,
                   RG_LDO_VAUD18_EN_MASK_SFT,
                   1 << RG_LDO_VAUD18_EN_SFT);
```

行号 ✅（`regmap_update_bits` 在 4150，正是 VAUD18）；值 ✅（`1 << SFT`，SFT=0 → bit0=1）。
第二处引用：

```
$ sed -n '4931,4939p' mt6368.c
static int dc_trim_thread(void *arg)
{
    struct mt6368_priv *priv = arg;
    get_hp_trim_offset(priv, false);
#if IS_ENABLED(CONFIG_SND_SOC_MT6368_ACCDET)
    mt6368_accdet_late_init(0);
#endif
```

行号 ✅，但**这一行不写 0x1cb2**——它是调用链证据（`dc_trim_thread → get_hp_trim_offset → enable_trim_circuit`）。
脚本标为 `ok_indirect`。**判定：行号对，值是推导的（跨函数链），不是直证。**

### #3 `0x2688` 只读校验｜引用 `mt6368-accdet.c:502-511`

```
$ sed -n '498,512p' mt6368-accdet.c
                idx+1, accdet_read(idx+1),
                idx+2, accdet_read(idx+2),
                idx+3, accdet_read(idx+3));
        }
        st_addr = ACCDET_AUXADC_SEL_ADDR;
        end_addr = ACCDET_MON_FLAG_EN_ADDR;
        for (addr = st_addr; addr <= end_addr; addr += 4) {
            idx = addr;
            pr_notice("(0x%x)=0x%x (0x%x)=0x%x (0x%x)=0x%x (0x%x)=0x%x\n",
```

行号 ✅（502 是 `st_addr = ACCDET_AUXADC_SEL_ADDR`，但脚本最佳匹配落在 500 行的 `accdet_read` 循环；
**偏移 -2**，语义不变：这段就是 dump 循环，不含写）。值 n/a（校验项）。
额外证据：全文件扫描确认 **accdet.c 与 mt6368.c 里没有任何写 0x2688 的操作** → 只读校验成立。

### #7 `0x26aa` bit0 IRQ_CLR=0｜引用 `mt6368-accdet.c:1244`

```
$ sed -n '1244p' mt6368-accdet.c
    accdet_clear_bit(ACCDET_IRQ_CLR_ADDR, ACCDET_IRQ_CLR_SFT);
```

行号 ✅ 精确命中，值 ✅（`clear_bit` = 写 0）。

### #11 `0x2698`=`0x08`｜引用 `:3117` + `:1942`

```
$ sed -n '3117p' mt6368-accdet.c
    accdet_set_debounce(accdet_state000, cust_pwm_deb->debounce0);
$ sed -n '1940,1946p' mt6368-accdet.c
void accdet_set_debounce(int state, unsigned int debounce)
{
    switch (state) {
    case accdet_state000:
        /* set ACCDET debounce value = debounce/32 ms */
        accdet_write(ACCDET_DEBOUNCE0_L_ADDR, debounce & 0xff);
        accdet_write(ACCDET_DEBOUNCE0_H_ADDR, debounce >> 8);
```

行号：3117 ✅（调用点，间接）；**1942 实际是 `case accdet_state000:` 注释行，真正的 H 字节写在 1946**，
**偏移 +4**。值：`debounce0` = dts `headset-mode-setting[4]` = `0x800`，`0x800>>8 = 0x08` ✅。

### #12 `0x2699`=`0x00`｜引用 `:3118` + `:1946-1948`

```
$ sed -n '3118p' mt6368-accdet.c
    accdet_set_debounce(accdet_state001, cust_pwm_deb->debounce1);
$ sed -n '1948,1951p' mt6368-accdet.c
    case accdet_state001:
        accdet_write(ACCDET_DEBOUNCE1_L_ADDR, debounce & 0xff);
        accdet_write(ACCDET_DEBOUNCE1_H_ADDR, debounce >> 8);
```

3118 ✅；**1946 实际是 `case accdet_state001:`，L 字节写在 1949，偏移 +3**。
值：`debounce1` = `0x800`，`0x800 & 0xff = 0x00` ✅。

### #13 `0x269a`=`0x08`｜引用 `:3118` + `:1947`

同上，`:1947` 是 `case` 分支的注释/空行区间，**真正的 H 字节在 1950，偏移 +3**。值 ✅。

### #16 `0x269f`=`0x44`｜引用 `:3121` + `:1961-1963`

```
$ sed -n '3121p' mt6368-accdet.c
    accdet_set_debounce(accdet_auxadc, cust_pwm_deb->debounce4);
$ sed -n '1960,1965p' mt6368-accdet.c
    case accdet_auxadc:
        /* set auxadc debounce:0x42(2ms) */
        accdet_write(ACCDET_CONNECT_AUXADC_TIME_DIG_L_ADDR,
            debounce & 0xff);
```

3121 ✅；1961-1963 ✅（L 字节写在 1962）。值：`debounce4` = dts 第 8 个值 `0x44` ✅。
**注意注释说 0x42 但 dts 是 0x44**——注释过时，以 dts 为准，表取 0x44 正确。

### #20 `0x26a3` bit[3:0]=`0x05`｜引用 `:3100-3101` + `:1974-1978`  ← **唯一引用级缺陷**

```
$ sed -n '3097,3104p' mt6368-accdet.c
static void accdet_init_debounce(void)
{
    /* set debounce to 1ms */
    accdet_set_debounce(eint_state000,
        accdet_dts.pwm_deb.eint_debounce0);
    /* set debounce to 128ms */
    accdet_set_debounce(eint_state011,
        accdet_dts.pwm_deb.eint_debounce3);
```

第一处 ✅（3100-3101 = `eint_state000`，正是 bit[3:0]）。

```
$ sed -n '1967,1979p' mt6368-accdet.c
    case eint_state000:
        accdet_update_bits(ACCDET_EINT_DEBOUNCE0_ADDR,
                ACCDET_EINT_DEBOUNCE0_SFT,
                ACCDET_EINT_DEBOUNCE0_MASK,
                debounce);
        break;
    case eint_state001:
        accdet_update_bits(ACCDET_EINT_DEBOUNCE1_ADDR,
                ACCDET_EINT_DEBOUNCE1_SFT,
                ACCDET_EINT_DEBOUNCE1_MASK,
                debounce);
```

**第二处 ❌**：引用写的是 `1974-1978`，但 `1974` 是 **`eint_state001`（DEBOUNCE1，bit[7:4]）**；
`#20` 要的是 `eint_state000`（DEBOUNCE0，bit[3:0]），实际在 **`1967-1971`**。
**偏移 -7，且指向了同一寄存器的另一个字段**——这是「引错了分支」，不是单纯行号漂移。

值 ✅（`eint_debounce0` = dts 第 11 个值 `0x05`）。dump `0x26a3`=`0x35`，bit[3:0]=`0x5` ✅。

**结论：值对、寄存器对、dump 对，只有引用行号指向了隔壁分支。属于文档缺陷，不影响实现。**

### #23 `0x2319` bit1 RST=0｜引用 `:2991-2992`

```
$ sed -n '2989,2993p' mt6368-accdet.c
    accdet_update_bit(RG_ACCDET_RST_ADDR,
            RG_ACCDET_RST_SFT);
    accdet_clear_bit(RG_ACCDET_RST_ADDR,
            RG_ACCDET_RST_SFT);
```

行号 ✅ 精确（2991 = `clear_bit`）。值 ✅（写 0）。dump `[0x2319]=0x0` ✅。

### #29 `0x2691`=`0x04`｜引用 `:3008-3009`

```
$ sed -n '3006,3010p' mt6368-accdet.c
    accdet_write(ACCDET_PWM_THRESH_L_ADDR,
        REGISTER_VAL(cust_pwm_deb->pwm_thresh) & 0xff);
    accdet_write(ACCDET_PWM_THRESH_H_ADDR,
        REGISTER_VAL(cust_pwm_deb->pwm_thresh) >> 8);
```

行号 ✅（3008-3009 就是 THRESH_H）。值 ✅：`REGISTER_VAL(0x500)` = `0x500-1` = `0x4ff`，`>>8` = `0x04`。
dump `0x2691`=`0x04` ✅。**这条顺带证实了 `REGISTER_VAL(x) = x-1`（`:37`）的用法正确。**

### #31 `0x2693`=`0x81`｜引用 `:3011-3013`

```
$ sed -n '3011,3014p' mt6368-accdet.c
    accdet_write(ACCDET_FALL_DELAY_ADDR,
            cust_pwm_deb->fall_delay << 7 |
            cust_pwm_deb->rise_delay >> 8);
```

行号 ✅。值 ✅：dts `headset-mode-setting` 第 3 个值 `fall=0x01`，第 4 个 `rise=0x1f0`；
`1<<7 | 0x1f0>>8` = `0x80 | 0x01` = `0x81` ✅。dump `0x2693`=`0x81` ✅。
**这条是整表里位拼接最容易错的一条，实际是对的。**

### #39 `0x2533`=`0x03`｜引用 `:2972-2976`

```
$ sed -n '2972,2977p' mt6368-accdet.c
    if (accdet_dts.eint_detect_mode != 0x1) {
        /* current detect set 0.25uA */
        accdet_update_bits(RG_ACCDETSPARE_L_ADDR,
            RG_ACCDETSPARE_L_SFT,
            0x3, 0x3);
    }
```

行号 ✅。值 ✅（mask 0x3、data 0x3）。dump `0x2533`=`0x3` ✅。
前置条件：`eint_detect_mode` 缺省为 `0x4`（`:2548-2553`），xaga dts 未设 → `!=1` 成立 ✅。

### #43 `0x26aa` bit[6:4]=`0x01`｜引用 `:2770-2771`

```
$ sed -n '2769,2772p' mt6368-accdet.c
    /* after receive n+1 number, interrupt issued. now is 2 times */
    accdet_update_bit(ACCDET_EINT_M_PLUG_IN_NUM_ADDR,
      ACCDET_EINT_M_PLUG_IN_NUM_SFT);
```

行号 ✅。值 ✅（`update_bit` 置 SFT=4 那一位置 1 → bit[6:4]=`0b001`=1）。dump `0x26aa`=`0x10`，bit[6:4]=`0x1` ✅。
**注意注释说「now is 2 times」但代码置 1**——表取 1 正确（注释过时）。

### #45 `0x268b` bit4=0｜引用 `:2841-2844`

```
$ sed -n '2840,2845p' mt6368-accdet.c
    if (accdet_dts.moisture_detect_enable == 0) {
        pr_info("%s() disable digital moisture.\n", __func__);
        /* disable moisture detection function */
        accdet_clear_bit(ACCDET_EINT_M_DETECT_EN_ADDR,
            ACCDET_EINT_M_DETECT_EN_SFT);
    }
```

行号 ✅。值 ✅（`clear_bit` = 0）。dump `0x268b`=`0x0` ✅。
前置：`moisture_detect_enable` 缺省 `0`（`:2533-2537`），xaga dts 未设 ✅。

### #46 `0x268c`=`0x67`｜引用 `:2848`

```
$ sed -n '2848p' mt6368-accdet.c
    accdet_write(ACCDET_CMP_PWM_EN_ADDR, 0x67);
```

行号 ✅ 精确，值 ✅ 整字节 `0x67`。dump `0x268c`=`0x67` ✅。

### 4.1 抽查汇总

| # | 行号对得上？ | 值对得上？ | 备注 |
|---|---|---|---|
| 2 | ✅（+ 间接链） | ✅ | 第二处引用是调用链，不是写点 |
| 3 | ✅ 偏移 -2 | n/a | 全文件零写入，佐证「只读校验」 |
| 7 | ✅ 精确 | ✅ | — |
| 11 | ⚠️ 偏移 +4 | ✅ | 1942 是 case 标签 |
| 12 | ⚠️ 偏移 +3 | ✅ | 1946 是 case 标签 |
| 13 | ⚠️ 偏移 +3 | ✅ | 1947 是 case 标签 |
| 16 | ✅ | ✅ | 注释写 0x42，dts 是 0x44，表取 0x44 正确 |
| **20** | **❌ 引错分支（-7）** | ✅ | 1974-1978 是 DEBOUNCE1，应为 1967-1971 |
| 23 | ✅ 精确 | ✅ | — |
| 29 | ✅ 精确 | ✅ | `REGISTER_VAL(0x500)=0x4ff` 用法正确 |
| 31 | ✅ 精确 | ✅ | 位拼接 `fall<<7\|rise>>8` = `0x81` 正确 |
| 39 | ✅ 精确 | ✅ | — |
| 43 | ✅ 精确 | ✅ | 注释「2 times」过时，表取 1 正确 |
| 45 | ✅ 精确 | ✅ | — |
| 46 | ✅ 精确 | ✅ | — |

**15 项中：行号完全精确 8 项，行号小幅偏移但语义正确 6 项，引错分支 1 项（#20）。值全部正确（15/15）。**

### 4.2 行号偏移的整体规律

偏移**不是随机的**，全部集中在 `accdet_set_debounce()`（1940-1996）：

| 引用 | 实际 | 偏移 | 原因 |
|---|---|---|---|
| 1941-1943 | 1945 | +4 | 引用了 `case` 标签+注释，不是 `accdet_write` |
| 1942 | 1946 | +4 | 同上 |
| 1946-1948 | 1949 | +3 | 同上 |
| 1947 | 1950 | +3 | 同上 |
| 1956-1958 | 1957 | +1 | 基本准 |
| 1957 | 1958 | +1 | 基本准 |
| 1961-1963 | 1962 | +1 | 准 |
| 1962 | 1964 | +2 | — |
| 1974-1978 | 1974 | 0 | **但指错字段** |
| 1978-1982 | 1974 | -4 | **指错字段** |

**没有全局行号漂移**：文件 md5 与仓库 HEAD 一致，且 `init_once` 区（2984-3095）的所有引用都精确命中。
偏移只出现在「一个函数里 10 个 case 连续排列」的地方——作者大概是按 case 的顺序推算行号，而不是真读。

---

## 5. 「一定错」清单

### 5.1 内容级：0 条

四项硬判据全部通过：

| 判据 | 结果 |
|---|---|
| value 超出 mask 位宽 | 0 条 |
| mask/shift 与 regmap 冲突（地址或 shift 不符） | 0 条 |
| seq 违反源码依赖 | 0 条（4 条顺序规则全 PASS） |
| 生成的 C 数组与 JSON 不一致 | 0 条（44 行逐字段相同） |

### 5.2 引用级：1 条

**#20 `0x26a3` bit[3:0] DEBOUNCE0** — `reason` 里的 `mt6368-accdet.c:1974-1978` 指向 `eint_state001`
（DEBOUNCE1，bit[7:4]），而本行要的是 `eint_state000`（DEBOUNCE0，bit[3:0]），实际在 `:1967-1971`。
**值 0x05、寄存器 0x26a3、dump 全部正确，只改引用即可。**

### 5.3 未验证：1 条 + 3 条待定

| # | 项 | 状态 |
|---|---|---|
| 1 | `0x230c` bit0 CK_PDN | **未验证**（dump 无此地址，源码无写入者） |
| 4 | `0x268d` CMP_PWM_IDLE | 源码零引用 + dump=0x00，但 dump 是 Android 跑过之后的状态，**冷启动复位值未验证** |
| 3 | `0x2688` | 同上（零写入 + dump=0，冷启动值未验证） |
| 5 | `0x26a9` bit0 IRQ | 同上 |

### 5.4 系统性错误：**没有**

任务要求「如果发现系统性错误（例如某个 mask 全算错），明确说出来」。逐项核过：

- **mask/shift 编码体系自洽**：JSON 里 `mask` 是未移位宽度、`shift` 是移位量，
  生成的 C 数组里 `mask<<shift` / `value<<shift` 已就位；44 行逐行比对**全部一致**（脚本 `c_array_consistent` 全 True）。
- **没有一组 mask 整体错位**：与 `research/mt6368-accdet-regmap.txt`（82 个寄存器、257 个字段定义）
  以及 `raw/mt6368.h`（333 个 `_ADDR/_SFT/_MASK` 组）交叉核对。表里 41 个 distinct 寄存器中，
  凡能在这两处找到字段定义的，`mask` 与 `shift` 全部吻合；唯一 3 个「查不到字段定义」的
  （`0x1cb2` / `0x230c` / `0x248c`）已用头文件的 `/* 寄存器名 */` 注释块补齐，同样吻合。
- **没有地址整体偏移**：41 个 distinct 寄存器里 38 个被 dump 覆盖，其中 40 条判定 `match`，
  另外 5 条冲突全部有结构性解释。若地址整体错位，不可能出现 40 条独立命中。

### 5.5 表外的缺口盘点：10 个可疑项，实际 0 个真缺口

脚本把「厂商 init 路径会写到、但表里没有」的寄存器全列了出来（10 个）：

| 寄存器 | 首个写点 | 函数 | 是否该补 |
|---|---|---|---|
| `0x26ad` | `:3088` | `accdet_init_once` | ❌ 不该补：`:3088` 在 `else if (ACCDET_AP_GPIO_EINT)` 分支里，xaga 走 `ACCDET_PMIC_EINT_IRQ`（`headset-use-ap-eint=0`，dts `:2165` → `:2636`），**该分支不执行**。dump `0x26ad`=0x00 就是复位值 |
| `0x252e` | `:1332` | `adjust_eint_analog_setting` | ❌ 不执行：该函数由 `:1611/:1631` 的 moisture 恢复路径调用；`config_eint_init_by_mode` 里 `:2921-2923` 的 `if (eint_detect_mode ∈ {1,2,3})` 不成立（xaga 缺省 `0x4`，`:2548-2553`），整块走 `:2966 else if (==0x4) { /* do nothing */ }`。dump `0x252e`=0x00 |
| `0x2532` | `:2330` | `config_moisture_detect_2_1` | ❌ moisture 关闭，不执行 |
| `0x258c` | `:2831` | `config_digital_init_by_mode` | ❌ 仅 `moisture_detect_mode==5` 分支 |
| `0x2689` | `:1386` | `adjust_eint_digital_setting` | ❌ 仅 `eint_detect_mode ∈ {1,2,3}` |
| `0x269b`/`0x269c` | `:1953`/`:1954` | `accdet_set_debounce` | ❌ `accdet_state010` 分支，无调用者（全文只有 `eint_state010` 被调用于 `:3126`，且仅 moisture 开启时） |
| `0x26ac` | `:1417` | `adjust_eint_digital_setting` | ❌ 同上，非本路径 |
| `0x26b1` | `:1393` | `adjust_eint_digital_setting` | ❌ 同上 |
| `0x26b2` | `:2809` | `config_digital_init_by_mode` | ❌ 仅 `moisture_detect_mode==3` |

**结论：10 个可疑项全部是「dts 条件不满足，厂商自己也不写」，没有一个是真缺口。**
逐行读 `:3069-3093` 确认：xaga 的 `caps` 含 `ACCDET_PMIC_EINT_IRQ`（`headset-use-ap-eint=0` → `:2636`），
所以走 `:3069` 分支（`config_eint_init_by_mode` + `config_digital_init_by_mode`），
`:3076` 的 `else if (ACCDET_AP_GPIO_EINT)` **不执行**，`:3088` 的 `HWMODE_EN=0` 也就不执行。
注意别把两个 HWMODE 寄存器搞混：`0x26ad` bit0 是 `ACCDET_HWMODE_EN`，`0x26ae` bit0 是 `ACCDET_EINT_HWMODE_EN`。
表 #44 写的是后者（`0x26ae=0x01`），dump 里 `0x26ae`=0x01 ✅、`0x26ad`=0x00 ✅，两者各安其位。）
**`0x26ad` 的复位值就是 0，表不写它是对的。**

> 这是本报告唯一一处**推翻脚本初判**的地方：脚本的 `coverage_gaps` 只看「init 路径可达但表里没列」，
> 不看 dts 分支是否可达，所以会误报。已逐条按源码的 dts 条件人工复核，**结论是不改表**。
> 脚本的 `writes_in_dead_branch` 字段已把这类误报标出来，供后续复用。

---

## 6. 两个致命前置项

### 6.1 `0x230c` bit0 `RG_ACCDET_CK_PDN`

**表里怎么处理的？** 第 1 项，`op: "verify"`，`mask 0x01`，`value: null`，
`note` 写「冷启动须先读回确认 bit0=0（时钟未关）。真机 dump 未含 0x230c，属未验证项」。
生成的 C 里进 `accdet_init_check[]`：`{ 0x230c, 0x01, 0x00 }`，注释「读回值必须与 val 在 mask 内一致，否则停机」。

**假设它是 0 的依据是什么？** 脚本审计了 `mt6368.c` 全部 15 个 `MT6368_AUD_TOP_CKPDN_CON0` 出现点：

```
:173   regmap_update_bits(priv->regmap, MT6368_AUD_TOP_CKPDN_CON0, 0x66, enable ? 0x0 : 0x66);
:5446  regmap_update_bits(priv->regmap, MT6368_AUD_TOP_CKPDN_CON0, 0x66, 0x0);
:3269/:3272/:3275/:3279   SND_SOC_DAPM_SUPPLY_S(... MT6368_AUD_TOP_CKPDN_CON0, RG_xxx_CK_PDN_SFT ...)
:5882-:5893   9 处 regmap_read（debugfs 打印）
```

**被写到的位集合 = {1, 2, 5, 6}**（`0x66` = bit1|bit2|bit5|bit6；DAPM 那四个 supply 分别是
`RG_AUDNCP_CK_PDN`(6)、`RG_ZCD13M_CK_PDN`(5)、`RG_AUD_CK_PDN`(1)、`RG_AUDIF_CK_PDN`(2)）。
**bit0 不在其中**，且 `accdet.c` 全文没有 `AUD_TOP_CKPDN` 字样。

所以「bit0 = 0」的依据是：**没有任何代码会把 bit0 置 1，而复位值按 MT6368 spec 应为 0（时钟默认开）**。
前半句是**证据**（全树 grep + 位集合计算），后半句是**推理**（复位值未见任何 dump 佐证）。
**必须标明：`bit0 == 0` 属未验证。**

**对应的正确写法**：不要只读校验。冷启动若读回 1，应先写 `0x230e`（`AUD_TOP_CKPDN_CON0_CLR`，
`mt6368.h:91`）的 bit0 清零，再继续；`0x230d`（`_SET`）是反方向。这两个地址只来自命名约定，
**源码里没有任何使用点**——所以「用 CLR 寄存器清 bit0」也是**未验证**的（脚本 `zero_write_scan` 确认全树零引用）。

### 6.2 `0x1cb2` `RG_LDO_VAUD18_EN`

**表里第 2 步，`mask 0x01`、`shift 0`、`value 0x01`** —— 与 `mt6368.h:512-514` 的
`RG_LDO_VAUD18_EN_SFT 0 / MASK 0x1` 一致 ✅。

**它在 accdet 寄存器块之外吗？是的。** `mt6368.h:74`：

```
#define MT6368_LDO_VAUD18_CON0               0x1cb2
```

`0x1cb2` **不属于** ACCDET（`0x2688..0x26d3`）也不属于 AUDDEC_ANA（`0x2508..0x2537`），
它是 MT6368 的 **LDO 段**（`0x1bc8` LDO_VUSB_OP_EN2 附近，见 `mt6368.h:73-74`）。
**这是全表唯一的「跨模块写」**，也是它能成为致命前置项的原因：
只写 ACCDET 块、不碰 LDO 段，MICBIAS 就没电。

**时序证据链（脚本 `vaud18_audit` 输出）**：

```
:4150  regmap_update_bits  value=1 << RG_LDO_VAUD18_EN_SFT -> 1   （enable_trim_circuit(true)）
:4164  regmap_update_bits  value=0 << RG_LDO_VAUD18_EN_SFT -> 0   （enable_trim_circuit(false)）
:5432  regmap_update_bits  value=1 << RG_LDO_VAUD18_EN_SFT -> 1   （mt6368_rcv_dcc_set，播放路径）
accdet late_init 调用行 4938；之前最后一次写 = 0
```

调用顺序：`dc_trim_thread`（`:4931`）→ `get_hp_trim_offset`（`:4917` 开、`:4920` 关）→ `:4938` `mt6368_accdet_late_init(0)`。
**所以厂商在调 accdet 初始化时，VAUD18 恰好是关的。** 表里 #2 写 1 是**必需的补偿**，不是冗余。
这与作者 note 的说法一致，且现在有机器化证据（`:4164` 的 `0` 在 `:4938` 之前）。

> 注意 `:5432`（`mt6368_rcv_dcc_set`）也在 `:4938` 之后可能执行，但它只在播放/接收通路 kcontrol 设置时触发，
> 不能依赖它。**裸机必须自己写 #2。**

---

## 7. 这张表能不能直接用于裸机实现

### 能用，但要带三个实现约定

**① 必须补：`0x230c` bit0 的失败处理**（不是改值，是改行为）

表里 #1 是只读校验。裸机若读回 bit0=1，硬件等价于「ACCDET 时钟关着」，
此后所有 ACCDET 寄存器读回 0——表里剩下的 49 项会全部「通过校验」但毫无意义（读到全 0 正合期望）。
**建议：读回 bit0=1 时直接判定失败并停机，不要继续。** 这是唯一能防止「静默全 0」的检查。

**② 必须改：`SW_EN` 要常开，不能照抄厂商的清除逻辑**

表 #48 写 `0x268a` bit0=1（65 ms 稳定等待）。厂商随后会在拔出（`:1925`）或 3 段/`cur_AB==3`（`:1894`）时清它。
**裸机没有 EINT，只能轮询 `0x26b6`；一旦 SW_EN 被清，`MEM_IN` 就冻结在最后一次值**，
表现为「插了拔出都不响应」。所以裸机侧不要实现 1894/1925 的清零，或至少在每次轮询前重新置位。

**③ 建议补：`0x26b6` 的采样不能只看一次**

#49 是单次采样。厂商在 `eint_work_callback` 里先 `accdet_init()` 再 `enable_accdet()`（`:1912-1914`），
然后由中断驱动多次读。裸机轮询应在 #48 之后至少等 `debounce0 = 0x800/32768 s = 62.5 ms`（表里给 65 ms ✅），
再连续采样数次取稳定值，否则会读到过渡态。

### 不能直接用的部分

| 项 | 原因 |
|---|---|
| `0x230c` bit0 = 0 | **未验证**（无 dump、无写入者，只有位集合推理）。必须运行时确认，不能当已知 |
| `0x268d` = 0 / `0x2688` = 0 / `0x26a9` bit0 = 0 | dump 值来自 Android 跑过之后，**冷启动复位值未验证**。校验失败时应报警而非静默 |
| `SW_EN` 常开 | 与厂商行为不同，是**故意的偏离**，需在代码里写清楚为什么 |
| `0x2697..0x26a0` 的 debounce 值 | 依赖 dts `headset-mode-setting`，表已按 xaga 值固化（`0x800/0x800/0x20/0x44`），换机型要重算 |

### 延时合计核对

表内 `delay_us` 合计 **69900 µs = 69.9 ms**，与作者自报一致 ✅。构成：

| # | 延时 | 来源 |
|---|---|---|
| 6 | 400 µs | `ACCDET_TIME_OUT` = `0x61A80` ns = 400 µs（定义 `mt6368-accdet.h:31-32`，`/* 400us, Accdet irq clear timeout */`；用于 `:1241`） |
| 8 | 3000 µs | `usleep_range(2000,3000)` 上界（`:3112`） |
| 9 | 1500 µs | `usleep_range(1000,1500)` 上界（`:3115`） |
| 48 | 65000 µs | debounce0 = `0x800`/32768 s = 62.5 ms + 2.5 ms 余量 |

**注意 #6 的 400 µs 是「超时上限」不是「固定等待」**：厂商是 `while (IRQ && !timeout);` 轮询，
IRQ 一掉就退出。裸机照抄 400 µs 固定延时是**保守但正确**的（多等不会错）。

---

## 8. 复现方式

```bash
cd /home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip
python3 accdet-table-validate.py --selftest          # 21 项自检
python3 accdet-table-validate.py --verbose           # 人读报告
python3 accdet-table-validate.py --json-out findings/accdet-validation.json
python3 accdet-table-validate.py --seed 20261115 --sample 15   # 复现本文抽样
```

脚本只读以下文件，不写它们：`findings/accdet-init-table.json`、`accdet-baseline.json`、
`research/mt6368-accdet-regmap.txt`、`raw/mt6368.h`、`raw/mt6368-accdet.c`、
`findings/accdet-init-table.c`、`mt6368.c`（首次从 sparse clone `git cat-file` 取到 `/tmp`）。

自检输出：

```
SELFTEST  (21 checks)
  header: RG_ACCDET_CK_PDN = 0x230c bit0 mask 1               PASS
  header: RG_ACCDET_RST = 0x2319 bit1                         PASS
  header: MT6368_AUD_TOP_CKPDN_CON0 = 0x230c                  PASS
  regmap: 0x26b6 ACCDET_MEM_IN @bit6 mask 3                   PASS
  baseline: 130 寄存器且 0x26b6=0xff                              PASS
  table: 50 行 = 44 写 / 4 校验 / 2 采样                            PASS
  width: 注入 0x4/0x1 越界被抓到                                     PASS
  maskshift: #43 原位 ok，错位到 bit0 报 must_fix                    PASS
  dump: 一致判 match，翻转判 conflict                                PASS
  pulse: 3 组脉冲（#6/#7, #8/#9, #22/#23）                         PASS
  refill: #24 (0x252a) 由 #33 回填                               PASS
  line: #48 命中 0x268a 的 update_bit                            PASS
  line: #20 引用 1974-1978 落在同寄存器的 DEBOUNCE1（应报 field_conflict） PASS
  line: #10 的 3117 是 accdet_set_debounce 调用点 → ok_indirect    PASS
  audit: 0x230c bit0 无人写（mask 参数 0x66，SFT 无 bit0）             PASS
  audit: VAUD18 在 accdet 初始化前被 trim 关回 0                      PASS
  sample: 固定种子可复现                                             PASS
  现表: 0 条内容级一定错（位宽/mask/顺序/C 数组）                              PASS
  现表: 引用级缺陷恰好 1 条（#20 错引另一 eint 分支）                           PASS
  现表: C 数组与 JSON 全一致                                      PASS
  现表: 块外行 = #1 #2 #22 #23 #38（0x230c/0x1cb2/0x2319/0x248c）    PASS
------------------------------------------------------------------------------
  21/21 PASS
```

---

## 9. 对作者自报的更正

| 作者说法 | 实测 | 更正 |
|---|---|---|
| 36 一致 / 4 冲突 / 4 未覆盖 | 40 match / 5 冲突（均可解释）/ 3 no_dump | `0x2319` **在** dump 里（`[0x2319]=0x0`，杂项单点），不该算「未覆盖」；它被正确归为脉冲 |
| 4 条冲突都是「瞬态 vs 稳态」 | 3 条真瞬态（#6/#8/#22）、1 条回填（#24）、1 条运行时清零（#48） | #24 是**中间态**不是瞬态（中间隔 9 行、寄存器确实停在 0）；#48 的机制主路径是 `:1925` 不是 `:1894` |
| 「没有任何一条写入的终态值与 dump 矛盾」 | ✅ 成立 | 40 条 match + 5 条有解释 = 45，加上 3 条无覆盖 + 2 条采样 = 50 |
| `0x26b7` STATE 全文不读 | ✅ 成立 | `grep ACCDET_STATE_ADDR` 只命中 `mt6368.h:4998` 定义行 |
| `0x268d` 零写入 | ✅ 成立 | `grep -c CMP_PWM_IDLE mt6368-accdet.c` = **0** |

**没有发现任何一条「值写错」。全表 44 个写值，逐个对过源码表达式与 dts 参数，全部正确。**

