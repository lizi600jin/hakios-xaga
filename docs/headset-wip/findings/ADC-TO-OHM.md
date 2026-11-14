# MT6368 ACCDET / HP 阻抗 ADC → 欧姆 查找表

自动生成：`python3 adc-ohm.py --emit`。所有数字来自厂商源码，行号随行标注。
内核树 `/home/lizi/IOS/work-xaga/k-xaga` @ `0a8d13c1a180fed82803f56337a0b065ff704fb4`。

## 1. 结论

**ACCDET 自己不测阻抗。** `mt6368-accdet.c` 全文没有任何把 ADC 码换成欧姆的代码：
插孔侧只用 (a) `0x26b6` bit[7:6] 的 AB 比较器状态判类型，(b) AUXADC 通道 5 读**毫伏**，去和 DTS/efuse 的**毫伏**阈值比。

- ACCDET 的 ADC：通道 5、12 bit、VREF **1840 mV**（`mtk-spmi-pmic-adc.c:33,98,177`）。
  唯一消费点 `accdet_get_auxadc()` → `key_check()`（`mt6368-accdet.c:892-914,1029-1054`）。
- 真正的欧姆值来自**另一条路**：codec 的 HP 阻抗测量，AUXADC 通道 12（HPOFS_CAL，15 bit），
  注入 DC 偏移后算 `R = 3600000/dc × Δ / 7832`（`mt6368.c:4981-4992`）。
  这个欧姆数最终喂给 `mt6368_accdet_read_audio_res()`，在 **5000 Ω** 处二分类（`mt6368-accdet.c:845-863`）。

因此本表给的是「欧姆 → 期望 ADC 增量」，不是「ACCDET 通道码」。两者不是同一个 ADC。

## 2. 公式

```
R[ohm] = (3600000 / dc) * Δsum / 7832 / 8

dc    = 288 (phase0) | 1440 (phase1) | 6048 (phase2)   mt6368.c:5043-5047
Δsum  = Σ_{i=0..7}( adc(dc_i) - adc(dc=0) )             mt6368.c:5008,5029,5010
8     = num_detect，最后一步才除                           mt6368.c:5010
```

注意 `3600000 / dc` 是**整数截断**再乘：phase2 得 595 而非 595.238（`mt6368.c:4989`）。

### 3 个 phase 的系数与量程

| phase | dc | k=3600000/dc (int) | Ω/LSB（单次采样） | 15 bit 上限 | 源码 |
|---|---|---|---|---|---|
| 0 | 288 | 12500 | 1.5960 | 52296 Ω | `mt6368.c:5043-5047` |
| 1 | 1440 | 2500 | 0.3192 | 10459 Ω | `mt6368.c:5043-5047` |
| 2 | 6048 | 595 | 0.0760 | 2489 Ω | `mt6368.c:5043-5047` |

phase 选择（`mt6368.c:5123-5134`）：先在 dc=288 用**单次**采样算 `pick_impedance`，
`<250` 走 phase2，`<1000` 走 phase1，否则留在 phase0 用 8 次采样正式测量。

### 饱和与钳位

- 单次采样均值 > **32630**（`mt6368.c:5039`）时直接赋 `auxcable_impedance = 5000` 并 break（`mt6368.c:5140-5142`）。这里比的是 C 整数除法 `detect_sum / num_detect`，即`floor(sum/8) > 32630`，首个触发点是 sum=261048。折合 **52079 Ω** 以上一律报 5000。
- Δ 恰为 0 时（dc=288 处 `dc_sum/8 == detect_sum`，`mt6368.c:5110-5121`）也报 5000，所以 0 Ω 永远报不出来：0 Ω 的码 0 会被当成「没插」。
- `PARALLEL_OHM` 为 0（`mt6368.h:5959`），`mt6368.c:5180-5187` 的并联修正是死代码。

### efuse 校准

`HPDET_COMP[6:0]` + 符号位 bit7，取自 efuse 字节 0xCA（`mt6368.c:5249-5262`），
按 `r = (r*128 + cal)/128` 施加（`mt6368.c:5016-5018`）——这是**加性** `cal/128` 欧姆，
即 ±0.99 Ω，不是百分比。

## 3. 查找表：欧姆 → 期望 ADC 码

`Δsum` = 8 次采样增量之和；`Δ/sample` = 单次采样增量（= 真实 ADC 读数差）。

| Ω | phase | dc | Δsum | Δ/sample | 期望区间(Δ/sample) | 回报 Ω | read_audio_res | 备注 |
|---|---|---|---|---|---|---|---|---|
| 0 | 2 | 6048 | 0 | 0.00 | 0.0..5.75 | 5000 | 0 | ideal delta 0, but SRC_CODEC:5110-5121 maps delta==0 at dc_phase0 to auxcable_impedance=5000 -> driver can never report 0 ohm |
| 1 | 2 | 6048 | 101 | 12.62 | 5.875..18.875 | 1 | -1 | round-trip exact |
| 2 | 2 | 6048 | 207 | 25.88 | 19.0..32.0 | 2 | -1 | round-trip exact |
| 5 | 2 | 6048 | 523 | 65.38 | 58.5..71.5 | 5 | -1 | round-trip exact |
| 10 | 2 | 6048 | 1049 | 131.12 | 124.25..137.375 | 10 | -1 | round-trip exact |
| 15 | 2 | 6048 | 1576 | 197.00 | 190.125..203.125 | 15 | -1 | round-trip exact |
| 20 | 2 | 6048 | 2102 | 262.75 | 255.875..269.0 | 20 | -1 | round-trip exact |
| 32 | 2 | 6048 | 3366 | 420.75 | 413.875..426.875 | 32 | -1 | round-trip exact |
| 50 | 2 | 6048 | 5261 | 657.62 | 650.75..663.875 | 50 | -1 | round-trip exact |
| 70 | 2 | 6048 | 7367 | 920.88 | 914.125..927.125 | 70 | -1 | round-trip exact |
| 100 | 2 | 6048 | 10526 | 1315.75 | 1309.0..1322.0 | 100 | -1 | round-trip exact |
| 135 | 2 | 6048 | 14212 | 1776.50 | 1769.625..1782.75 | 135 | -1 | round-trip exact |
| 150 | 2 | 6048 | 15792 | 1974.00 | 1967.125..1980.125 | 150 | -1 | round-trip exact |
| 200 | 2 | 6048 | 21057 | 2632.12 | 2625.25..2638.25 | 200 | -1 | round-trip exact |
| 235 | 2 | 6048 | 24742 | 3092.75 | 3086.0..3099.0 | 235 | -1 | round-trip exact |
| 249 | 2 | 6048 | 26217 | 3277.12 | 3270.25..3283.25 | 249 | -1 | round-trip exact |
| 250 | 1 | 1440 | 6262 | 782.75 | 781.5..784.5 | 250 | -1 | round-trip exact |
| 300 | 1 | 1440 | 7515 | 939.38 | 938.125..941.125 | 300 | -1 | round-trip exact |
| 470 | 1 | 1440 | 11775 | 1471.88 | 1470.75..1473.75 | 470 | -1 | round-trip exact |
| 500 | 1 | 1440 | 12527 | 1565.88 | 1564.75..1567.75 | 500 | -1 | round-trip exact |
| 680 | 1 | 1440 | 17038 | 2129.75 | 2128.625..2131.625 | 680 | -1 | round-trip exact |
| 700 | 1 | 1440 | 17540 | 2192.50 | 2191.25..2194.25 | 700 | -1 | round-trip exact |
| 900 | 1 | 1440 | 22552 | 2819.00 | 2817.875..2820.875 | 900 | -1 | round-trip exact |
| 999 | 1 | 1440 | 25033 | 3129.12 | 3128.0..3131.0 | 999 | -1 | round-trip exact |
| 1000 | 0 | 288 | 5010 | 626.25 | 626.25..626.75 | 1000 | -1 | round-trip exact |
| 1500 | 0 | 288 | 7516 | 939.50 | 939.5..940.0 | 1500 | -1 | round-trip exact |
| 2000 | 0 | 288 | 10023 | 1252.88 | 1252.875..1253.375 | 2000 | -1 | round-trip exact |
| 2200 | 0 | 288 | 11025 | 1378.12 | 1378.125..1378.625 | 2200 | -1 | round-trip exact |
| 3000 | 0 | 288 | 15035 | 1879.38 | 1879.375..1879.875 | 3000 | -1 | round-trip exact |
| 4000 | 0 | 288 | 20048 | 2506.00 | 2506.0..2506.5 | 4000 | -1 | round-trip exact |
| 4700 | 0 | 288 | 23556 | 2944.50 | 2944.5..2945.0 | 4700 | -1 | round-trip exact |
| 4999 | 0 | 288 | 25055 | 3131.88 | 3131.875..3132.375 | 4999 | -1 | round-trip exact |
| 5000 | 0 | 288 | 25060 | 3132.50 | 3132.5..3133.0 | 5000 | 0 | round-trip exact |
| 5100 | 0 | 288 | 25561 | 3195.12 | 3195.125..3195.625 | 5100 | 0 | round-trip exact |
| 6000 | 0 | 288 | 30073 | 3759.12 | 3759.125..3759.625 | 6000 | 0 | round-trip exact |
| 8000 | 0 | 288 | 40098 | 5012.25 | 5012.25..5012.75 | 8000 | 0 | round-trip exact |
| 10000 | 0 | 288 | 50122 | 6265.25 | 6265.25..6265.75 | 10000 | 0 | round-trip exact |
| 15000 | 0 | 288 | 75185 | 9398.12 | 9398.125..9398.625 | 15000 | 0 | round-trip exact |
| 20000 | 0 | 288 | 100247 | 12530.88 | 12530.875..12531.375 | 20000 | 0 | round-trip exact |
| 30000 | 0 | 288 | 150372 | 18796.50 | 18796.5..18797.0 | 30000 | 0 | round-trip exact |
| 40000 | 0 | 288 | 200497 | 25062.12 | 25062.125..25062.625 | 40000 | 0 | round-trip exact |
| 50000 | 0 | 288 | 250622 | 31327.75 | 31327.75..31328.25 | 50000 | 0 | round-trip exact |
| 52078 | 0 | 288 | 261038 | 32629.75 | 32629.75..32630.25 | 52078 | 0 | round-trip exact |
| 52079 | 0 | 288 | 261043 | 32630.38 | 32630.375..32630.875 | 52079 | 0 | round-trip exact |
| 52080 | 0 | 288 | — | — | — | 5000 | 0 | ADC saturates (>32630 per sample): SRC_CODEC:5140-5142 assigns auxcable_impedance=5000 (SRC_CODEC:5037) |
| 65535 | 0 | 288 | — | — | — | 5000 | 0 | ADC saturates (>32630 per sample): SRC_CODEC:5140-5142 assigns auxcable_impedance=5000 (SRC_CODEC:5037) |
| 100000 | 0 | 288 | — | — | — | 5000 | 0 | ADC saturates (>32630 per sample): SRC_CODEC:5140-5142 assigns auxcable_impedance=5000 (SRC_CODEC:5037) |

`read_audio_res` 列：`0` = `RET_GT_5K`（判为 LineOut），`-1` = `RET_LT_5K`（判为普通耳机）。定义在 `mt6368-accdet.c:61-62,849-851`。

## 4. 5000 Ω 判定边界（驱动唯一的二分类）

`mt6368_accdet_read_audio_res()` 只做一件事：`res_value < 5000 ? RET_LT_5K : RET_GT_5K`（`mt6368-accdet.c:849-851`）。下表给出边界附近的精确码。

| Ω | phase | Δsum | Δ/sample | 区间 | read_audio_res | 判为 |
|---|---|---|---|---|---|---|
| 4997 | 0 | 25045 | 3130.62 | 3130.625..3131.125 | -1 | normal device |
| 4998 | 0 | 25050 | 3131.25 | 3131.25..3131.75 | -1 | normal device |
| 4999 | 0 | 25055 | 3131.88 | 3131.875..3132.375 | -1 | normal device |
| 5000 | 0 | 25060 | 3132.50 | 3132.5..3133.0 | 0 | LINE_OUT |
| 5001 | 0 | 25065 | 3133.12 | 3133.125..3133.625 | 0 | LINE_OUT |
| 5002 | 0 | 25070 | 3133.75 | 3133.75..3134.25 | 0 | LINE_OUT |

phase 0 的单次采样分辨率是 **1.596 Ω/LSB**，但驱动把 8 次采样**求和**后才除（`mt6368.c:5010`），
等效 0.1995 Ω/LSB，最后又 `DIV_ROUND_CLOSEST` 成整数欧姆。
所以可分辨的最小台阶是 **1 Ω = Δsum 5 个 LSB**（0.625 LSB/采样），上表 4999→5000 正好差 5。
要更细只能加采样次数，不是靠公式精度。

## 5. MIC 按键（hook / volume）

按键走的是**通道 5 的毫伏**，阈值来自 DTS（毫伏），不是欧姆：

`headset-three-key-threshold = <0 0x50 0xdc 0x190>`（`mt6895.dtsi:2167`）→ mid=80 / up=220 / down=400 mV。
解析：`memcpy(&three_key, three_key+1, ...)`，第 0 个元素被丢弃（`mt6368-accdet.c:2685-2697`）。

`key_check()`（`mt6368-accdet.c:1029-1054`）的判据：

| 电压区间 (mV) | 3-key | 4-key (`<0 0x3a 0x79 0xc0 0x190>`, `mt6895.dtsi:2169`) |
|---|---|---|
| [0, 58) | MD_KEY | MD_KEY |
| [58, 80) | MD_KEY | AS_KEY |
| [80, 121) | UP_KEY | AS_KEY |
| [121, 192) | UP_KEY | UP_KEY |
| [192, 220) | UP_KEY | DW_KEY |
| [220, 400) | DW_KEY | DW_KEY |
| ≥ 400 | NO_KEY | NO_KEY |

xaga 用的是 `headset-key-mode = <0>` → `ACCDET_THREE_KEY`（`mt6895.dtsi:2166`，
`mt6368-accdet.c:2660-2665`），所以生效的是 3-key 那一列。

### 5.1 按键欧姆值：源码中不存在

0/70/135/235/470 Ω 这类数字在这些文件里**一次都没出现**。要用欧姆，必须先有原理图上的
mic-bias 串联电阻 `R_bias`。有了它才能反解：

```
V_node = V_bias * R_key / (R_bias + R_key)        V_bias = 2800 mV  (mt6368-accdet.c:3024-3031)
R_key  = R_bias * V_node / (V_bias - V_node)
```
`R_bias` 未在源码中出现，**需实测/查原理图**。上表毫伏阈值可直接代入该式。

## 6. 12 bit 通道码 → 毫伏（按键/湿度用）

`mv = code * 1840 / 4096`（`mtk-spmi-pmic-adc.c:370-371`，r_ratio 默认 1/1，`:629-635`）。

| code | mV | 3-key | 4-key |
|---|---|---|---|
| 0 | 0.00 | MD_KEY | MD_KEY |
| 100 | 44.92 | MD_KEY | MD_KEY |
| 178 | 79.96 | MD_KEY | AS_KEY |
| 300 | 134.77 | UP_KEY | UP_KEY |
| 490 | 220.12 | DW_KEY | DW_KEY |
| 890 | 399.81 | DW_KEY | DW_KEY |
| 1000 | 449.22 | NO_KEY | NO_KEY |
| 2048 | 920.00 | NO_KEY | NO_KEY |
| 4095 | 1839.55 | NO_KEY | NO_KEY |

efuse 偏移先减掉再比：`auxadc_offset` 取自 nvmem 字节 `91*2`，`val&0xFF`，>128 减 256，再 `>>1`，
单位 mV，范围 −64..63（`mt6368-accdet.c:927-931`，减法在 `:904-911`）。

## 7. 前置条件（不出值就是这里没做对）

1. 初始化有**两条**路径，只跑一次的闸门是 `atomic_cmpxchg(&accdet_first,1,0)`（`mt6368-accdet.c:3142`）：
   - probe 里 `atomic_set(&accdet_first,1)` 后挂 `ACCDET_INIT_WAIT_TIMER = 10*HZ` 定时器（`mt6368-accdet.c:206,3495-3496`），超时走 `delay_init_work_callback()`；
   - 或 audio 侧显式调 `mt6368_accdet_late_init()`（`mt6368-accdet.c:3139-3149`），先 `del_timer_sync` 再初始化。
   两条都执行同一组三步：`accdet_init()` → `accdet_init_debounce()` → `accdet_init_once()`（`mt6368-accdet.c:3144-3146`，`3211-3217`）。**只做其中一步不够。**
2. `accdet_init()` 里 `ACCDET_SEQ_INIT` 置位后 `usleep_range(2000,3000)` 再清，再 `usleep_range(1000,1500)`，
   然后才下发 debounce（`mt6368-accdet.c:3110-3115`）。这是唯一让模拟前端稳定的等待，跳步会读到上一次的 AB/比较器状态。
3. `accdet_init_once()` 先打 `RG_ACCDET_RST` 复位脉冲，再配 micbias；`mic_vol=8` 走 2.8 V 分支
   （`mt6368-accdet.c:2984-2992`，`3024-3031`）。bias 没起来时读到的是 0 mV，会被当成 MD_KEY 一直按着。
4. AUXADC 通道映射走 DTS：`io-channels = <&mt6368_auxadc 9>` + `io-channel-names = "pmic_accdet"`
   （`mt6895.dtsi:2170-2171`）；通道号 9 是 dt-binding 的 `AUXADC_ACCDET`，
   硬件通道是 5（`include/dt-bindings/iio/mt635x-auxadc.h:19`，`mtk-spmi-pmic-adc.c:98`）。
5. HP 阻抗路径还要求 `HEADPHONE` 已上电且 mux 切到 `HP_MUX_HP_IMPEDANCE`，
   否则 `hp_impedance_get()` 直接返回缓存值（`mt6368.c:5222-5229`）。
   测量期间还要 `set_trim_buf_gain(TRIM_BUF_GAIN_18DB)` + `AUXADC_AVG_64`（`mt6368.c:5073-5077`）。
6. `mt6368_accdet_read_audio_res()` 在本树**没有调用者**（`EXPORT_SYMBOL`，`mt6368-accdet.c:864`）。
   谁喂它欧姆值不在 `sound/` 里——未验证。

## 8. 源码中不存在的东西

- any ADC-code -> ohm function for AUXADC channel 5 (ACCDET)
- hook/volume key resistance values in ohm
- mic-bias series resistor value / schematic divider
- an in-tree caller of mt6368_accdet_read_audio_res() (sparse clone could not fetch the rest: promisor remote TLS failure)

## 9. 与实测的对照（stock dump，未插耳机）

`accdet-baseline.json`（sid=5，`dump_reg`，2026-11-14，无耳机）里 `0x26b6 = 0xFF`，
按 `MEM_IN = (0x26b6 >> 6) & 0x3`（`mt6368-accdet.c:2005-2006` 的读法）得 **AB = 3**，
即 `ACCDET_STATE_AB_11` → 拔出（`mt6368-accdet.c:2045-2056`）。这与「未插」一致，
说明 `MEM_IN` 字段的读法正确。

同一份 dump 里 `0x2688 = 0x00`，即 `ACCDET_AUXADC_SEL`/`ACCDET_AUXADC_SW` 都是 0——
**ACCDET→AUXADC 的模拟开关在未插入时是关的**。所以想读通道 5，必须先让状态机进 MIC_BIAS
（`accdet_queue_work()` 里 `accdet_status == MIC_BIAS` 才取 `cali_voltage`，`mt6368-accdet.c:2164-2165`）。

本表所有欧姆数字**未经实测校验**：源码推导值，等有耳机实测 ADC 后再回归。

## 10. selftest

```
$ python3 adc-ohm.py --selftest
PASS  ACCDET LSB = 1840/4096 = 0.44921875 mV
PASS  80 mV -> code 178
PASS  220 mV -> code 490
PASS  400 mV -> code 890
PASS  C truncation 3600000/6048 == 595 (not 595.238)
PASS  k(phase0)=12500 k(phase1)=2500 k(phase2)=595
PASS  sum 25064 -> phase0 5000 ohm
PASS  sum 25064 -> phase1 1000 ohm
PASS  sum 25064 -> phase2 238 ohm
PASS  phase0 per-sample 3133 -> 5000 ohm
PASS  round-trip exact for 12 sample ohms
PASS  classify 4999 -> -1
PASS  classify 5000 -> 0
PASS  phase map 249/250/999/1000/5000
PASS  phase2 ceiling < 2500
PASS  phase1 ceiling < 10500
PASS  phase0 ceiling > 50000
PASS  saturation limit == 52079 ohm
PASS  sum 261047 (32630 avg) survives -> 52079
PASS  sum 261048 (32631 avg) is the first clamped sum
PASS  52080 saturates -> 5000 ohm
PASS  efuse +127 adds ~1 ohm at 5000
PASS  3-key 0 mV -> MD
PASS  3-key 79.9 -> MD, 80 -> UP
PASS  3-key 220 -> DW, 400 -> NO_KEY
PASS  4-key 57 -> MD, 58 -> AS
PASS  divider 135R/2200R -> 161.88 mV then back
PASS  table has 47 rows
PASS  boundary table brackets 5000 with 6 rows
PASS  boundary verdict flips exactly at 5000
PASS  table 0 ohm -> sum 0
PASS  table 100k reported as 5000

32/32 assertions passed
```
