# MT6368 ACCDET 冷启动寄存器初始化序列（xaga / MT6895）

> 对象：`/home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip/raw/mt6368-accdet.c`（3609 行）
> 与 `raw/mt6368.h`（5974 行）、`raw/mt6895.dtsi`（7254 行）
> 交叉核对：`accdet-baseline-dump.txt`（真机 stock Android 未插耳机 dump，SID 5）
> 仓源比对：`git -C /home/lizi/IOS/work-xaga/k-xaga cat-file -p HEAD:sound/soc/codecs/mt6368-accdet.c`
> 与本地 `raw/` 文件 **逐字节相同**（`diff -q` 无差异）
> 目标读者：在裸机 XNU 里自己实现耳机检测的人

---

## 0. 结论先行

1. **厂商没有一条独立的「冷启动初始化函数」。** 初始化被拆成三段，且**只由插入事件驱动**：
   `accdet_init()` → `accdet_init_debounce()` → `accdet_init_once()`，
   入口是 `mt6368_accdet_late_init()`（`mt6368-accdet.c:3139-3150`），
   该函数由 codec 的 `dc_trim_thread` 在 DC trim 之后调用（`mt6368.c:4931-4938`）。
   **probe 里不调用初始化**——probe 只解析 dts、申请 IRQ、挂 10 秒定时器（`:3496`）。
2. **真正的「上电 → 能读出 cur_AB」需要 44 次写 + 4 次只读校验**，
   串行延时合计 **69.9 ms**（其中 65 ms 是开检测后的稳定等待）。
3. **`ACCDET_SW_EN` 是最后一步**（`:1814-1817`，调用点 `:1914`）。
   在它置 1 之前，`0x26b6` 的 `MEM_IN` 字段无意义——厂商是在插入中断里
   `accdet_init()` → `enable_accdet()` 之后才开始读 `cur_AB`。
4. **`0x268d`（`ACCDET_CMP_PWM_IDLE`）在整份驱动里零写入。**
   任务描述里把它列为「必写」，源码不支持这一点。本报告按源码办事：
   该项列为「只读校验」，值取真机 dump 的 `0x00`，并标注**未验证**。
5. **纯轮询可行，但只能轮询 `0x26b6` 与 `0x26a9`，不能靠 `0x26b7`。**
   `ACCDET_STATE`（`0x26b7` bit[2:0]）驱动全文**一次都没读**
   （`grep ACCDET_STATE_ADDR` 只命中 `mt6368.h:4998` 的定义行）。

---

## 1. 寄存器写入表（44 写 + 4 校验 + 2 采样）

约定：
- `reg` 是 **MT6368 内部字节地址**（经 PMIF `EXT_REG_LONG` 访问，SID 5）。
- `mask` 是**未移位**的位域宽度；`value` 是**未移位**的字段值。
- 落地为：`v = read8(reg); v = (v & ~(mask<<shift)) | (value<<shift); write8(reg, v)`。
  `mask=0xff` 即整字节写，不必读回。
- 「dump」列 = 真机 stock Android 未插耳机时的读回值，用来判「一致 / 冲突 / 无覆盖」。

### 阶段 0 · 前置条件（1 写 + 3 只读校验）

| # | reg | mask<<shift | 写值 | 延时 | dump | 为什么 | 源码 |
|---|---|---|---|---|---|---|---|
| 1 | `0x230c` | `0x01<<0` | **只读校验，须为 0** | 0 | 无覆盖 | `RG_ACCDET_CK_PDN`（AUD_TOP_CKPDN_CON0 bit0）。**驱动全文无写入者**；codec 只写 mask `0x66`（不含 bit0） | `mt6368.h:3843-3846`（定义）、`mt6368.c:5446`（唯一写 0x230c 处，mask=0x66） |
| 2 | `0x1cb2` | `0x01<<0` | `0x01` | 0 | 无覆盖 | `RG_LDO_VAUD18_EN=1`。VAUD18 是 MICBIAS/VREF 的模拟供电。**vendor 靠 dc_trim 先行打开**（`enable_trim_circuit(true)`），trim 结束又写回 0（`mt6368.c:4168-4171`），所以裸机必须自己保证 | `mt6368.c:4150`、`mt6368.c:4931-4938` |
| 3 | `0x2688` | `0xff` | **只读校验，须为 0** | 0 | `0x00` | `AUXADC_SEL/AUXADC_SW`，只服务 moisture 与按键 ADC，本路径不用；驱动无写入 | `:502-511`（dump 范围） |
| 4 | `0x268d` | `0xff` | **只读校验，须为 0** | 0 | `0x00` | `ACCDET_CMP_PWM_IDLE`。**驱动全文零引用**（`CMP_PWM_IDLE`/`PWM_IDLE` 都 grep 不到） | `mt6368.h:4348-4350`（仅定义） |

> ⚠️ 第 1、4 项是本报告与「任务描述」不一致之处，按源码为准：
> 时钟门控与 PWM_IDLE 都没有厂商写入路径。**在真机 dump 补到 `0x230c` 之前，这两条属未验证。**
> 尤其 `0x230c` bit0：若冷启动读回是 1（时钟被关），后面所有 ACCDET 寄存器都会读回 0，
> 必须先解决（写 `0x230c` 的 CLR 寄存器 `0x230e`，或写 SET `0x230d`——**这两条地址来自
> `AUD_TOP_CKPDN_CON0_SET/CLR` 的命名，未在源码里找到使用点，属未验证**）。

### 阶段 1 · 清 stale IRQ（2 写 + 1 校验）

| # | reg | mask<<shift | 写值 | 延时 | dump | 为什么 | 源码 |
|---|---|---|---|---|---|---|---|
| 5 | `0x26a9` | `0x01<<0` | 只读校验 | 0 | `0x00` | 冷启动可能残留上次的中断标志 | `:1236-1241` |
| 6 | `0x26aa` | `0x01<<0` | `0x01` | **400 µs** | `0x10`（bit4，与本次写的 bit0 不冲突） | 置 `ACCDET_IRQ_CLR`，压住 IRQ 电平 | `:1230-1234` |
| 7 | `0x26aa` | `0x01<<0` | `0x00` | 0 | 同上 | **IRQ_CLR 是电平保持，IRQ 落下后必须释放**，否则下一次中断永远不来 | `:1244` |

> 400 µs 来自 `ACCDET_TIME_OUT = 0x61A80` ns = 400 000 ns（`mt6368-accdet.h:32`），
> 在 `clear_accdet_int_check()` 里作为轮询上限。裸机可直接延时 400 µs，或轮询 `0x26a9` bit0 到 0。

### 阶段 2 · `accdet_init()`：SEQ_INIT 脉冲 + debounce（14 写）

| # | reg | mask<<shift | 写值 | 延时 | dump | 为什么 | 源码 |
|---|---|---|---|---|---|---|---|
| 8 | `0x268a` | `0x02<<1` | `0x02` | **3000 µs** | `0x40` | `ACCDET_SEQ_INIT=1`，复位状态机序列 | `:3110-3112` |
| 9 | `0x268a` | `0x02<<1` | `0x00` | **1500 µs** | 同上 | `SEQ_INIT=0`，脉冲宽度决定序列长度 | `:3113-3115` |
| 10 | `0x2697` | `0xff` | `0x00` | 0 | `0x00` | `DEBOUNCE0` 低字节，值 `0x800` | `:3117` → `:1941-1943` |
| 11 | `0x2698` | `0xff` | `0x08` | 0 | `0x08` | `DEBOUNCE0` 高字节 | 同上 |
| 12 | `0x2699` | `0xff` | `0x00` | 0 | `0x00` | `DEBOUNCE1` 低字节，值 `0x800` | `:3118` → `:1946-1948` |
| 13 | `0x269a` | `0xff` | `0x08` | 0 | `0x08` | `DEBOUNCE1` 高字节 | 同上 |
| 14 | `0x269d` | `0xff` | `0x20` | 0 | `0x20` | `DEBOUNCE3` 低字节，值 `0x20` | `:3119` → `:1956-1958` |
| 15 | `0x269e` | `0xff` | `0x00` | 0 | `0x00` | `DEBOUNCE3` 高字节 | 同上 |
| 16 | `0x269f` | `0xff` | `0x44` | 0 | `0x44` | `CONNECT_AUXADC_TIME_DIG` 低字节（auxadc 2 ms） | `:3121` → `:1961-1963` |
| 17 | `0x26a0` | `0xff` | `0x00` | 0 | `0x00` | 同上高字节 | 同上 |
| 18 | `0x26a3` | `0x0f<<4` | `0x30` | 0 | `0x35`（低半 `0x5` 与本次写的 bit[7:4] 不冲突） | `EINT_DEBOUNCE1 = eint_debounce1 = 0x03` | `:3129` → `:1978-1982` |
| 19 | `0x26a5` | `0xff` | `0x0e` | 0 | `0x0e` | `EINT_INVERTER_DEBOUNCE = 0x0e`，整字节写 | `:3132` → `:1991-1992` |
| 20 | `0x26a3` | `0x0f<<0` | `0x05` | 0 | 见 #18 | `EINT_DEBOUNCE0 = eint_debounce0 = 0x05`（`accdet_init_debounce()`，**在 `accdet_init()` 之后**） | `:3100-3101` → `:1974-1978` |
| 21 | `0x26a4` | `0x0f<<4` | `0x50` | 0 | `0x5e`（低半 `0xe` 不冲突） | `EINT_DEBOUNCE3 = eint_debounce3 = 0x05` | `:3103-3104` → `:1986-1990` |

> **顺序陷阱**：`accdet_init()` 自己会写 `0x26a3` bit[7:4]（#18），
> 而 `accdet_init_debounce()` 又写 `0x26a3` bit[3:0]（#20）。
> 两次是对**同一字节的不同半字节**做读改写，顺序不能颠倒——否则 `0x26a3` 高半会被覆盖。

### 阶段 3 · `accdet_init_once()`：复位 + 模拟 + 数字（25 写）

| # | reg | mask<<shift | 写值 | 延时 | dump | 为什么 | 源码 |
|---|---|---|---|---|---|---|---|
| 22 | `0x2319` | `0x01<<1` | `0x02` | 0 | 无覆盖（dump 只到 `0x2508`） | `RG_ACCDET_RST=1`，数字域复位 | `:2989-2990` |
| 23 | `0x2319` | `0x01<<1` | `0x00` | 0 | 同上 | `RG_ACCDET_RST=0`。**源码此处没有 usleep**，裸机也照抄不延时 | `:2991-2992` |
| 24 | `0x252a` | `0x03<<4` | `0x00` | 0 | `0x30` | 清 `MICBIAS1 HVEN|HVVREF`，为下面按 mic_vol 重设做准备 | `:2995-2996` |
| 25 | `0x2529` | `0x07<<4` | `0x00` | 0 | `0x04` | 清 `MICBIAS1 VREF` | `:2998-2999` |
| 26 | `0x268e` | `0xff` | `0xff` | 0 | `0xff` | `PWM_WIDTH = REGISTER_VAL(0x500) = 0x4ff`，低字节 | `:3002-3003` |
| 27 | `0x268f` | `0xff` | `0x04` | 0 | `0x04` | `PWM_WIDTH` 高字节 | `:3004-3005` |
| 28 | `0x2690` | `0xff` | `0xff` | 0 | `0xff` | `PWM_THRESH = 0x4ff`，低字节 | `:3006-3007` |
| 29 | `0x2691` | `0xff` | `0x04` | 0 | `0x04` | `PWM_THRESH` 高字节 | `:3008-3009` |
| 30 | `0x2692` | `0xff` | `0xf0` | 0 | `0xf0` | `RISE_DELAY_L = rise_delay(0x1f0) & 0xff` | `:3010` |
| 31 | `0x2693` | `0xff` | `0x81` | 0 | `0x81` | `FALL_DELAY = fall(1)<<7 \| rise(0x1f0)>>8` | `:3011-3013` |
| 32 | `0x2529` | `0x01<<2` | `0x04` | 0 | `0x04` | `mic_vol==8` 分支：置 `MICBIAS1LOWPEN` | `:3024-3026` |
| 33 | `0x252a` | `0x03<<4` | `0x30` | 0 | `0x30` | `mic_vol==8`：`reg \| (3<<HVEN_SFT)` → HVEN=1 且 HVVREF=1 → **2.8 V** | `:3027-3030` |
| 34 | `0x252d` | `0xff` | `0x8f` | 0 | `0x8f` | `mic_mode==2`（xaga dts）：`reg \| RG_ACCDET_MODE_ANA11_MODE2`。**注意是读改写后的整字节**（`reg` 在 `:3040` 读入） | `:3040-3052` |
| 35 | `0x2530` | `0x03<<4` | `0x30` | 0 | `0x30` | `mic_mode==2` 分支：`ANALOGFDEN = 0x3`（模拟快速放电） | `:3054-3055` |
| 36 | `0x2531` | `0x01<<2` | `0x04` | 0 | `0x04` | `RG_EINT0EN=1`（caps 含 `ACCDET_PMIC_EINT0`，来自 `headset-eint-num=0`） | `:2903-2905` |
| 37 | `0x2534` | `0x01<<0` | `0x01` | 0 | `0x01` | `ACCDETSPARE_H` bit0=1，ESD 开关打开 | `:2916` |
| 38 | `0x248c` | `0x01<<0` | `0x01` | 0 | 无覆盖 | `RG_NCP_PDDIS_EN=1`，负压前先把 NCP 拉低 | `:2918-2919` |
| 39 | `0x2533` | `0x03<<0` | `0x03` | 0 | `0x03` | `eint_detect_mode != 1` → `ACCDETSPARE_L = 0x3`（电流检测 0.25 µA） | `:2972-2976` |
| 40 | `0x252f` | `0x0f<<4` | `0x20` | 0 | `0x20` | `RG_EINTCOMPVTH = eint_comp_vth`。xaga 的 dts **没有** `accdet-eint-comp-vth`，走缺省 2 | `:2979-2981`、`:2596-2599` |
| 41 | `0x2694` | `0xff` | `0x41` | 0 | `0x41` | `(eint_pwm_width<<4) \| eint_pwm_thresh = 4<<4\|1` | `:2729-2731` |
| 42 | `0x26ab` | `0xff` | `0x27` | 0 | `0x27` | `ACCDET_EINT0_STABLE_VAL = DA_STABLE\|EINT0_EN\|EINT0_CMPEN\|EINT0_CEN` | `:2735-2736`、`mt6368.h:5528-5531` |
| 43 | `0x26aa` | `0x07<<4` | `0x10` | 0 | `0x10` | `EINT_M_PLUG_IN_NUM=1`：收到 n+1 次才发中断，压抖 | `:2770-2771` |
| 44 | `0x26ae` | `0xff` | `0x01` | 0 | `0x01` | `moisture_detect_mode` 缺省 4 → else 分支 `EINT_HWMODE_EN=1` | `:2837-2838` |
| 45 | `0x268b` | `0x01<<4` | `0x00` | 0 | `0x00` | `moisture_detect_enable==0` → 清 `EINT_M_DETECT_EN` | `:2841-2844` |
| 46 | `0x268c` | `0xff` | `0x67` | 0 | `0x67` | `CMP_PWM_EN` 整字节：CMP\|VTH\|MBIAS\|EINT_EN\|CMPMEN\|CTURBO | `:2848` |
| 47 | `0x268a` | `0x01<<6` | `0x40` | 0 | `0x40` | `eint_detect_mode != 1` → `EINT0_INVERTER_SW_EN=1` | `:2878-2879` |

### 阶段 4 · 开检测 + 采样（1 写 + 2 读）

| # | reg | mask<<shift | 写值 | 延时 | dump | 为什么 | 源码 |
|---|---|---|---|---|---|---|---|
| 48 | `0x268a` | `0x01<<0` | `0x01` | **65 000 µs** | `0x40`（bit0 是 0） | `ACCDET_SW_EN=1`，**状态机从这里开始跑**。延时要覆盖 debounce0 = 0x800/32768 s = **62.5 ms** | `:1814-1817`，调用点 `:1914` |
| 49 | `0x26b6` | `0x03<<6` | **读** | 0 | `0xff`（未插 → `0x3` = 拔出） | `cur_AB = (read8(0x26b6)>>6)&0x3` | `:2005-2006` |
| 50 | `0x26b7` | `0x07` | **读（仅诊断）** | 0 | `0x00` | `ACCDET_STATE`。**驱动全文不读它**——不要用它判插入 | `mt6368.h:4998`（仅定义） |

### dump 交叉核对结果

对 44 条写项做「真机未插 dump 是否与之矛盾」检查：

| 结果 | 数量 | 说明 |
|---|---|---|
| 一致 | 36 | 位域掩码内值与 dump 相同 |
| 冲突（**均属预期**） | 4 | 见下 |
| dump 未覆盖 | 4 | `0x1cb2`、`0x2319`×2、`0x248c`；dump 范围是 `0x2688..0x26d3` + `0x2508..0x2537` |

4 条冲突逐条解释（都不是错）：

| seq | reg | dump | 表内 | 解释 |
|---|---|---|---|---|
| 6 | `0x26aa` | `0x10` | `0x01` | dump 是**稳态**（驱动已释放 IRQ_CLR，bit4 是 `M_PLUG_IN_NUM`），本步是**瞬态**置位 |
| 8 | `0x268a` | `0x40` | `0x02` | 同上，SEQ_INIT 是脉冲，dump 时已落回 0 |
| 24 | `0x252a` | `0x30` | `0x00` | 本步是**先清零**再在 #33 写回 `0x30`，dump 是终态 |
| 48 | `0x268a` | `0x40` | `0x01` | **真机未插时 `SW_EN=0`**（`dis_micbias_work_callback` 在 3 段耳机 / `cur_AB==3` 时关它，`:1888-1896`）。裸机要一直开着才能轮询，见 §4 |

→ 结论：4 条冲突全部是**瞬态 vs 稳态**（#6/#8/#24/#48），不是写错值。
→ 换句话说：**没有任何一条写入的「终态值」与真机 dump 矛盾。**

---

## 2. xaga 的 dts 全部 accdet 参数（逐条 + 寄存器效应）

节点位置：`raw/mt6895.dtsi:2155-2175`（`second_pmic@5` / SID 5 下）。
`raw/mt6895-xiaomi-xaga.dts` 里**没有** accdet 节点，`&accdet` 覆盖在 vendor 的
`arch/arm64/boot/dts/vendor/mediatek/xaga-mt6895.dtsi:607-615`（该文件只重复写了
`accdet-mic-vol=8`、`accdet-mic-mode=2`，与 dtsi 相同）。

| 属性 | 值 | 驱动落点 | 对寄存器的具体影响 |
|---|---|---|---|
| `accdet-name` | `"mt63xx-accdet"` | **驱动不读**（`grep "accdet-name"` = 0 次） | 无 |
| `accdet-mic-vol` | `8` | `mic_vol`（`:2604-2606`） | 走 `==8` 分支：`0x2529` bit2=1、`0x252a` bit[5:4]=`0b11` → MICBIAS1 = **2.8 V**（`:3024-3030`） |
| `accdet-plugout-debounce` | `1` | `plugout_deb`（`:2608-2610`） | **只在 AP-GPIO-EINT 路径用**（`gpio_set_debounce(pin, plugout_deb*1000)`，`:2448-2449`）。本平台走 PMIC EINT，**该值不落任何寄存器** |
| `accdet-mic-mode` | `2` | `mic_mode`（`:2612-2614`） | `0x252d = reg \| 0x8f`（`RG_ACCDET_MODE_ANA11_MODE2`，DCC 低成本无内部偏置）+ `0x2530` bit[5:4]=`0b11`（模拟快放） |
| `eint_use_ext_res` | `0` | `eint_use_ext_res`（`:2539-2541`） | 只在 `eint_detect_mode∈{1,2,3}` 时用（`:2921-2965`）。本机 `eint_detect_mode=4`，**该值不落寄存器** |
| `headset-mode-setting` | 15 个 u32：`0x500 0x500 0x01 0x1f0 0x800 0x800 0x20 0x44 0x04 0x01 0x05 0x03 0x03 0x05 0x0e` | `pwm_deb`（`:2614-2618`） | 见下方逐项展开 |
| `headset-use-ap-eint` | `0` | caps：`ACCDET_PMIC_EINT_IRQ`（`:2633-2639`） | **决定走整条 PMIC EINT 分支**：`accdet_init_once` 里进 `config_eint_init_by_mode()` + `config_digital_init_by_mode()`（`:3069-3075`），而不是 AP-GPIO 分支 |
| `headset-eint-num` | `0` | caps：`ACCDET_PMIC_EINT0`（`:2641-2647`） | 选 EINT0：`0x2531` bit2=1、`0x252f` 用 EINT0 阈值、`0x26ab` 写 `EINT0_STABLE` 值 |
| `headset-eint-trig-mode` | `1` | caps：`ACCDET_PMIC_INVERTER_TRIG_EINT`（`:2649-2655`） | **驱动里没有消费者**（`ACCDET_PMIC_GPIO_TRIG_EINT`/`INVERTER_TRIG` 只在此处置位，全文无判断），**不落寄存器** |
| `headset-key-mode` | `0` | caps：`ACCDET_THREE_KEY`（`:2657-2669`） | 只决定按键判定的阈值表用哪一组（`:2686-2694`），**初始化阶段不落寄存器** |
| `headset-three-key-threshold` | `0 0x50 0xdc 0x190` | `three_key.mid/up/down`（`:2686-2694`，丢掉第 0 项） | mid=0x50、up=0xdc、down=0x190。**只用于 `key_check()` 软件比较**（`:1029-1053`），不落寄存器 |
| `headset-three-key-threshold-CDD` | `0 0x79 0xc0 0x258` | 同上传入 `three_key` 变量，但 `headset-key-mode=0` 走的是上面那组 | **本机不生效** |
| `headset-four-key-threshold` | `0 0x3a 0x79 0xc0 0x190` | 只在 `ACCDET_FOUR_KEY` 时读（`:2672-2684`） | 本机 `headset-key-mode=0` → **不读、不生效** |
| `io-channels` / `io-channel-names` | `<&mt6368_auxadc 9>` / `"pmic_accdet"` | `devm_iio_channel_get(..., "pmic_accdet")`（`:3311-3312`） | 决定 `accdet_get_auxadc()` 读 AUXADC CH9；**初始化序列不写 AUXADC 寄存器**（`0x2688` 保持 0） |
| `nvmem` / `nvmem-names` | `<&mt6368_efuse>` / `"mt63xx-accdet-efuse"` | `accdet_get_efuse()`（调用点 `:3332`，实现 `:916-996`） | 读 efuse 得 `auxadc_offset`（`nvmem_device_read(..., 91*2, 2, ...)`，`:927-931`）。**只影响 ADC 读数校准，不影响初始化写表** |
| `status` | `"okay"` | — | — |

**节点里没有、但驱动有缺省值的属性**（这些缺省值才真正决定了写表）：

| 属性 | dts 缺失 | 驱动缺省 | 影响 |
|---|---|---|---|
| `eint_detect_mode` | 缺 | `0x4`（`:2545-2553`）；且 `moisture_detect_enable==0` 时**强制** `0x4`（`:2554-2557`） | 使 `:2921-2965` 整段「配 CONFIGACCDET」被跳过，`:2966-2967` 走「do nothing」 |
| `moisture_detect_enable` | 缺 | `0x0`（`:2533-2537`） | 清 `0x268b` bit4；不调 `config_analog_moisture_init_by_mode()` |
| `moisture_detect_mode` | 缺 | `0x4`（`:2559-2565`） | 走 `config_digital_init_by_mode()` 的 else 分支 → `0x26ae = 0x01` |
| `accdet-eint-comp-vth` | 缺 | `2`（`:2596-2599`） | `0x252f` bit[7:4] = `0x2` |
| `headset-eint-level-pol` | 缺 | `8`（`:2620-2625`） | **驱动里没有任何消费者**，不落寄存器 |

### `headset-mode-setting` 15 项逐项展开

字段顺序由 `struct pwm_deb_settings`（`mt6368-accdet.h:82-98`）决定：

| idx | 字段 | 值 | 落点 | 展开后 |
|---|---|---|---|---|
| 0 | `pwm_width` | `0x500` | `0x268e`/`0x268f` | `REGISTER_VAL(0x500)=0x4ff` → `0x268e=0xff`、`0x268f=0x04` |
| 1 | `pwm_thresh` | `0x500` | `0x2690`/`0x2691` | 同上 → `0x2690=0xff`、`0x2691=0x04` |
| 2 | `fall_delay` | `0x01` | `0x2693` bit7 | `1<<7 = 0x80` |
| 3 | `rise_delay` | `0x1f0` | `0x2692` + `0x2693` bit[6:0] | `0x2692=0xf0`、`0x2693` 高位 `0x01` → 合起来 `0x2693=0x81` |
| 4 | `debounce0` | `0x800` | `0x2697`/`0x2698` | `0x00`/`0x08`；时间 = **62.5 ms**（`/32768`，`:3116` 注释） |
| 5 | `debounce1` | `0x800` | `0x2699`/`0x269a` | `0x00`/`0x08`；62.5 ms |
| 6 | `debounce3` | `0x20` | `0x269d`/`0x269e` | `0x20`/`0x00`；**0.98 ms** |
| 7 | `debounce4`（auxadc） | `0x44` | `0x269f`/`0x26a0` | `0x44`/`0x00`；`:3120` 注释写 "auxadc:2ms" |
| 8 | `eint_pwm_width` | `0x04` | `0x2694` bit[6:4] | `4<<4 = 0x40` |
| 9 | `eint_pwm_thresh` | `0x01` | `0x2694` bit[2:0] | `0x01` → 合起来 `0x2694=0x41` |
| 10 | `eint_debounce0` | `0x05` | `0x26a3` bit[3:0] | `0x05`；`:3099` 注释 "set debounce to 1ms" |
| 11 | `eint_debounce1` | `0x03` | `0x26a3` bit[7:4] | `0x30` |
| 12 | `eint_debounce2` | `0x03` | `0x26a4` bit[3:0] | **只在 `moisture_detect_enable==1` 时写**（`:3122-3127`）。本机为 0 → **不写** |
| 13 | `eint_debounce3` | `0x05` | `0x26a4` bit[7:4] | `0x50`；`:3102` 注释 "set debounce to 128ms" |
| 14 | `eint_inverter_debounce` | `0x0e` | `0x26a5` 整字节 | `0x0e` |

---

## 3. H/W 与 S/W 模式、`accdet_mode` 的真相

**xaga 用的不是「accdet_mode」参数——源码里不存在这个名字。** 与「模式」相关的只有三处：

| 概念 | 名字 | xaga 的值 | 落点 |
|---|---|---|---|
| MIC 偏置模式 | `accdet-mic-mode` = **2** | `HEADSET_MODE_2`（DCC 低成本、**无**内部偏置） | `0x252d = 0x8f`、`0x2530` bit[5:4]=`0b11` |
| EINT 检测模式 | `eint_detect_mode`（dts 无，缺省 4） | **4**（EINT 2.1） | 跳过 `0x252e` 的 CONFIGACCDET 配置；`0x26ab` 写 EINT0 稳定值 |
| ACCDET 数字 H/W vs S/W | `ACCDET_HWMODE_EN`（`0x26ad` bit0） | **`init_once` 完全不碰 `0x26ad`** | 见下 |

关于 H/W 与 S/W：

- **`0x26ad`（`ACCDET_HWMODE_EN`）在 PMIC-EINT 路径里从不被写**。
  只有 AP-GPIO-EINT 分支会写 `accdet_write(ACCDET_HWMODE_EN_ADDR, 0x0)`（`:3088`）。
  真机 dump `0x26ad = 0x00`，说明 **xaga 跑的是 S/W 模式（HWMODE_EN=0）**。
- 另有 `ACCDET_HWMODE_SEL`（`0x26ad` bit[2:1]）、`ACCDET_PLUG_OUT_DETECT`（bit3）
  在 `config_digital_init_by_mode()` 里**没有写入**，dump 也是 0。
- `0x26ae`（`EINT_HWMODE_EN`）是**另一套**：EINT 侧的 H/W 模式，xaga 写 `0x01`。
  两者名字像但不是一个寄存器，移植时**别搞混**。

> 结论：`0x26ad` 保持 dump 值 `0x00`（S/W 模式）。本表不写它，也无从写它——**源码没有给值**。

---

## 4. 中断路径：为什么裸机用不上，以及纯轮询怎么做

### 4.1 厂商的中断拓扑（三段串联，全在 PMIC 里）

```
耳机插入
  → ACCDET 模拟比较器（EINT0）
  → 0x26a9 bit2 (ACCDET_EINT0_IRQ)        [mt6368.h:4596-4601]
  → 0x2323 bit2 (INT_STATUS_ACCDET_EINT0) [AUD_TOP_INT_STATUS0, W1C]
  → MT6368 内部 interrupt-controller（dtsi:2141-2147，SID 5）
  → SPMI 中断 → PMIF rcs_irq → GPIO243（dtsi:2032）
      或 PMIF 自身 → GIC SPI 414（dtsi:2033）
  → Linux: devm_request_threaded_irq(..., mtk_accdet_irq_handler_thread, IRQF_TRIGGER_HIGH|IRQF_ONESHOT)
     （mt6368-accdet.c:3338-3349）
```

- `mt6368-accdet.c:3342-3349` 申请的是 **`platform_get_irq(pdev, 0)`**，即 PMIC 的 IRQ 域输出，
  **不是 GIC 号**。
- dtsi 里 `spmi@1c804000` 的注释也写明：「no interrupt-controller plumbing」（`:2014-2016` 上下文）。
- **我们裸机没有 PMIC EINT**：`hakios.mt6895_headset.c` 头注释第 13-15 行明确写
  「Init and poll run only in the keys worker… There is no second bus worker」，
  现有实现就是**纯轮询**，不走中断。

### 4.2 纯轮询方案（本任务的核心交付）

#### 轮询哪个寄存器

| 优先级 | 寄存器 | 字段 | 判据 | 说明 |
|---|---|---|---|---|
| **主** | `0x26b6` | bit[7:6] `MEM_IN` | `0/1/3` | **`cur_AB` 的唯一权威来源**（`:2005-2006`）。`0`=三段耳机、`1`=四段耳麦、`3`=拔出、`2` 本平台不用 |
| 辅 | `0x26a9` | bit0 `ACCDET_IRQ` | 1 = 状态机有事件 | 只作「有新事件」的提示，**不是插入状态** |
| 辅 | `0x26b8` | bit[7:6] `EINT0_MEM_IN` | 0..3 | EINT 侧独立状态机；`M_PLUG_OUT=3`（`mt6368-accdet.h:63-69`）；字段定义 `mt6368.h:5043-5048` |
| **禁用** | `0x26b7` | bit[2:0] `ACCDET_STATE` | — | **驱动全文不读**，语义未经厂商确认 |

#### 轮询间隔

**50 ms**。理由（三条，都可核）：
1. 现有裸机实现已经用 50 ms（`hakios.mt6895_headset.c:508` 打印
   `"polling every 50 ms"`），实测能跑。
2. debounce0 = `0x800`/32768 = **62.5 ms** 是插入判定的最短稳定窗（`:3116`）；
   轮询周期必须小于它，50 ms < 62.5 ms 满足。
3. 单次 PMIF 事务是几十微秒量级，50 ms 周期下总线占用可忽略。

#### 最坏情况延迟

| 场景 | 延迟 | 算式 |
|---|---|---|
| 插入 → 首次读到 `cur_AB != 3` | **≤ 112.5 ms** | 62.5 ms（debounce0）+ 50 ms（轮询相位） |
| 插入 → 首次读到「稳定」值（连续 2 次相同） | **≤ 162.5 ms** | 62.5 + 50 + 50 |
| 拔出 → 首次读到 `cur_AB == 3` | **≤ 51 ms** | debounce3 = `0x20`/32768 = 0.98 ms + 50 ms |
| 按键按下 → 读到 `cur_AB == 0`（MIC_BIAS 态下） | ≤ 50 ms + 按键 debounce | 见 §4.3 陷阱 |
| 冷启动初始化本身 | **69.9 ms** | 表内 `delay_us` 合计（其中 65 ms 是最后一步） |

#### 轮询循环伪代码

```c
/* 已完成 accdet_init_seq[0..ACCDET_INIT_SEQ_LEN-1]，SW_EN=1 */
uint8_t prev = 0xFF, cur;
unsigned stable = 0;

for (;;) {
    cur = pmif_read8(0x26b6) >> 6 & 3;      /* cur_AB */
    if (cur == prev) {
        if (++stable >= 2) {                 /* 连续 2 次相同 -> 认定 */
            report(cur);                     /* 0=三段 1=四段 3=拔出 */
            stable = 0;
        }
    } else {
        prev = cur; stable = 0;
    }
    if (pmif_read8(0x26a9) & 1) {            /* 有事件：清掉，避免电平一直压着 */
        pmif_write8(0x26aa, pmif_read8(0x26aa) | 0x01);
        while ((pmif_read8(0x26a9) & 1) && spins++ < LIMIT) ;
        pmif_write8(0x26aa, pmif_read8(0x26aa) & ~0x01);
    }
    delay_ms(50);
}
```

> 清 IRQ 的顺序必须照 `clear_accdet_int_check()`（`:1236-1247`）：先置 `IRQ_CLR` → 等
> `0x26a9` bit0 落 0 → 再清 `IRQ_CLR`。**反了会导致中断不再触发**（虽然轮询不依赖它，
> 但电平一直压着会让 `0x26a9` 永远读 1，失去「有新事件」的提示）。

### 4.3 四个必须知道的陷阱（源码为证）

1. **`cur_AB` 不是「已插入」布尔。**
   `check_cable_type()` 的每个分支都先看 `accdet->accdet_status`（前态），再看 `cur_AB`
   （`:2011` 的 `switch`）。同一个 `cur_AB==0`：
   - 前态 `PLUG_OUT` → 三段耳机（`:2013-2018`）
   - 前态 `MIC_BIAS` → **按键按下**（`:2063-2067`，`button_status = 1`）
   裸机必须自己维护前态，不能只读 `cur_AB`。

2. **`eint_sync_flag` 门控。**
   所有分支都判 `if (accdet->eint_sync_flag)`（`:2015`、`:2025`、`:2049`…），
   否则打 `"accdet hp has been plug-out"` 并**丢弃**该次采样。
   这个标志只在 `eint_work_callback()` 的插入分支被置 true（`:1907`），
   拔出分支置 false（`:1917`）。
   → 裸机对应物：「本次插入会话已建立」的布尔，**插入后置 1、拔出后置 0**。

3. **厂商会在拔出 6 秒后把 `SW_EN` 关掉——裸机不能学。**
   `MICBIAS_DISABLE_TIMER = 6 * HZ`（`:213`），定时器到点后 `dis_micbias_work_callback()`
   在「3 段耳机 / `cur_AB==3`」时 `clear_bit(ACCDET_SW_EN)`（`:1888-1896`）。
   这是**省电**设计：厂商靠 EINT 中断唤醒，不需要一直开着状态机。
   裸机没有 EINT，一旦关了就再也读不到插入——所以 **`SW_EN` 必须常开**，
   且不要注册这个 6 秒定时器。

4. **进入 MIC_BIAS 前要读一次 AUXADC。**
   `accdet_queue_work()` 在 `accdet_status == MIC_BIAS` 时先
   `cali_voltage = accdet_get_auxadc()`（`:2164-2165`）。这一步是按键 ADC 的基线，
   **不做也能读 `cur_AB`**，但按键档位会偏。

### 4.4 进入检测循环的条件（哪些位必须是什么）

| 位 | 必须 | 依据 |
|---|---|---|
| `0x268a` bit0 `SW_EN` | **1** | `enable_accdet()`（`:1814-1817`）。**这是唯一的「开跑」开关** |
| `0x268c` bit0..6 = `0x67` | 全开 | `:2848`，比较器/VTH/MBIAS/EINT/CTURBO 的 PWM 使能 |
| `0x26ab` = `0x27` | EINT0 的 4 个 stable 位全 1 | `:2735-2736`；dump 印证 |
| `0x26ae` bit0 = 1 | EINT H/W 模式 | `:2837-2838`；dump 印证 |
| `0x2319` bit1 `RG_ACCDET_RST` | **0**（复位释放） | `:2989-2992` 先 1 后 0；dump `0x2319=0x00` |
| `0x230c` bit0 `CK_PDN` | **0**（时钟不能关） | 定义 `:3843-3846`；**无写入者，需读回确认**（未验证） |
| `0x26ad` bit0 `HWMODE_EN` | **0**（S/W 模式） | PMIC 路径从不写它；dump `0x00` |
| `0x268b` bit4 `M_DETECT_EN` | **0**（关 moisture） | `:2841-2844`；dump `0x00` |
| `0x268a` bit1 `SEQ_INIT` | 脉冲后回到 **0** | `:3113-3115` |

---

## 5. 稳定时间 / debounce 对照

| 状态 | 寄存器 | 值 | 时间 | 依据 |
|---|---|---|---|---|
| `accdet_state000`（插入） | `0x2697/0x2698` | `0x800` | `0x800/32768` = **62.5 ms** | `:3116` 注释 `(debounce/32768)sec`、`:3117` |
| `accdet_state001` | `0x2699/0x269a` | `0x800` | 62.5 ms | `:3118` |
| `accdet_state011`（拔出） | `0x269d/0x269e` | `0x20` | **0.98 ms** | `:3119` |
| `accdet_auxadc` | `0x269f/0x26a0` | `0x44` | 注释 "auxadc:2ms"（**单位未验证**：`0x44=68`，若同样 `/32768` 只有 2.07 ms，与注释的数字吻合但量纲不同） | `:3120`、`:3121` |
| `eint_state000` | `0x26a3` bit[3:0] | `0x05` | 注释 "set debounce to 1ms" | `:3099-3101` |
| `eint_state011` | `0x26a4` bit[7:4] | `0x05` | 注释 "set debounce to 128ms" | `:3102-3104` |
| `eint_inverter_state000` | `0x26a5` | `0x0e` | 注释未给 | `:3132` |

> **每次状态变化后等多久才能读 `cur_AB`**：
> - 插入方向：**≥ 62.5 ms**（debounce0），建议 65 ms（表内 #48 就取 65 ms）。
> - 拔出方向：**≥ 1 ms**（debounce3 = 0.98 ms），但轮询周期 50 ms 已覆盖。
> - 按键：debounce0 在按键路径被改成 `button_press_debounce = 0x400`（=31.25 ms，
>   `:222-223` 定义、`:2035-2036` 调用），所以按键反应比插入快一倍。

---

## 6. 需要真机 dump 才能定的项（不许猜）

| 项 | 现状 | 为什么定不了 |
|---|---|---|
| `0x230c`（`RG_ACCDET_CK_PDN`） | 建议值 **bit0=0**，标**未验证** | 驱动无写入者；真机 `dump_register` 打印了这个地址（`:525-529`），但 `accdet-baseline-dump.txt` 是 `cat_register` 的输出，**未包含 `0x230c`** |
| `0x230d`/`0x230e`（`AUD_TOP_CKPDN_CON0_SET/CLR`） | 地址来自命名推断，**未验证** | 源码无使用点 |
| `0x268d`（`ACCDET_CMP_PWM_IDLE`） | 建议保持 `0x00`（dump 值） | 驱动零引用 |
| `0x26ad`（`HWMODE_EN`/`HWMODE_SEL`/`PLUG_OUT_DETECT`） | 建议保持 `0x00`（dump 值） | PMIC 路径不写 |
| `0x26a6`/`0x26a7`/`0x26a8`（`IVAL_*` 电流档） | **表里没有**，保持 dump 值 `0xbf`/`0x0a`/`0x00`（原文 `0xa`） | 驱动全文不写这三个字节（`grep IVAL` 零命中） |
| `0x26c2`..`0x26cb`（`CUR_DEB`） | **表里没有**，保持 dump 值 | 驱动全文不写（`grep CUR_DEB` 零命中） |
| `0x2688`（`AUXADC_SEL/SW`） | 保持 `0x00` | 只服务 moisture/按键 ADC |

---

## 7. 生成器与自检

`accdet-init-gen.py` 读 `findings/accdet-init-table.json`，输出
`findings/accdet-init-table.c`（`struct accdet_wr` 数组 + 校验表 + 采样表）。

约束（脚本强制的，不是口头约定）：
- `seq` 必须从 1 连续；
- `reg` 必须落在 `0x0100..0x2fff`；
- `mask` 必须是连续低位掩码，且 `mask<<shift <= 0xff`（PMIF 一次只搬 1 字节）；
- `value <= mask`（value 是未移位字段值）；
- `write` 项**不允许** `value=null`（不许猜值）；
- `reason` 必须含 `file:line`；
- `delay_us <= 2s`（防单位写错）。

### selftest 实际输出

```
$ python3 accdet-init-gen.py --selftest
== accdet-init-gen.py --selftest ==
input : /home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip/findings/accdet-init-table.json
PASS  JSON 解析：顶层数组 50 项
PASS  逐项校验：seq 连续 1..50，字段齐全
PASS  分类：write=44 verify=4 read=2
PASS  地址范围：35 个 distinct 寄存器，0x1cb2..0x26ae（全部落在 0x0100..0x2fff）
PASS  无空值 write 项（44 项全部可追到源码行号）
PASS  全部 50 项 reason 含 file:line
INFO  串行延时合计 69900 us（69.9 ms）
INFO  整字节写 20 项，位域读改写 24 项
RESULT: PASS
```

### 生成 C 数组

```
$ python3 accdet-init-gen.py -o findings/accdet-init-table.c --quiet
wrote findings/accdet-init-table.c (44 write / 4 verify / 2 read)
```

### 生成的 C 数组编译验证

驱动桩（`/tmp/t2.c`，只 `#include` 生成的表）：

```c
#include <stdint.h>
#include <stdio.h>
#include "/home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip/findings/accdet-init-table.c"
int main(void){ unsigned tot=0;
  for(unsigned i=0;i<ACCDET_INIT_SEQ_LEN;i++) tot+=accdet_init_seq[i].delay_us;
  printf("ok %u entries %u us\n",ACCDET_INIT_SEQ_LEN,tot); return 0;}
```

```
$ gcc -std=c11 -Wall -Wextra -Werror -o accdet_t2 t2.c && ./accdet_t2
ok 44 entries 69900 us
[exit 0]
```

生成的表（`findings/accdet-init-table.c` 全文）：

```c
/* 由 accdet-init-gen.py 从 accdet-init-table.json 生成，勿手改。 */
/* MT6368 ACCDET 冷启动初始化序列（xaga / MT6895，SPMI SID 5）。 */
/* 每一项展开后都是『单字节 read-modify-write』：*/
/* 表中 mask/val 均已是『字节内就位』的形式（JSON 里的 mask<<shift 与 value<<shift）。 */
/*   v = pmif_read8(reg); v = (v & ~mask) | val; pmif_write8(reg, v); */
/* mask=0xff 表示整字节写（此时无需读回）。 */

#ifndef _ACCDET_INIT_TABLE_H_
#define _ACCDET_INIT_TABLE_H_

#include <stdint.h>

struct accdet_wr {
	uint16_t reg;
	uint8_t  mask;
	uint8_t  val;
	uint16_t delay_us;
};

/* 阶段 A：只读校验。读回值必须与 val 在 mask 内一致，否则停机。 */
struct accdet_chk {
	uint16_t reg;
	uint8_t  mask;
	uint8_t  val;
};

static const struct accdet_wr accdet_init_seq[] = {
	{ 0x1cb2, 0x01, 0x01,     0 }, /*  2 */
	{ 0x26aa, 0x01, 0x01,   400 }, /*  6 */
	{ 0x26aa, 0x01, 0x00,     0 }, /*  7 */
	{ 0x268a, 0x02, 0x02,  3000 }, /*  8 */
	{ 0x268a, 0x02, 0x00,  1500 }, /*  9 */
	{ 0x2697, 0xff, 0x00,     0 }, /* 10 */
	{ 0x2698, 0xff, 0x08,     0 }, /* 11 */
	{ 0x2699, 0xff, 0x00,     0 }, /* 12 */
	{ 0x269a, 0xff, 0x08,     0 }, /* 13 */
	{ 0x269d, 0xff, 0x20,     0 }, /* 14 */
	{ 0x269e, 0xff, 0x00,     0 }, /* 15 */
	{ 0x269f, 0xff, 0x44,     0 }, /* 16 */
	{ 0x26a0, 0xff, 0x00,     0 }, /* 17 */
	{ 0x26a3, 0xf0, 0x30,     0 }, /* 18 */
	{ 0x26a5, 0xff, 0x0e,     0 }, /* 19 */
	{ 0x26a3, 0x0f, 0x05,     0 }, /* 20 */
	{ 0x26a4, 0xf0, 0x50,     0 }, /* 21 */
	{ 0x2319, 0x02, 0x02,     0 }, /* 22 */
	{ 0x2319, 0x02, 0x00,     0 }, /* 23 */
	{ 0x252a, 0x30, 0x00,     0 }, /* 24 */
	{ 0x2529, 0x70, 0x00,     0 }, /* 25 */
	{ 0x268e, 0xff, 0xff,     0 }, /* 26 */
	{ 0x268f, 0xff, 0x04,     0 }, /* 27 */
	{ 0x2690, 0xff, 0xff,     0 }, /* 28 */
	{ 0x2691, 0xff, 0x04,     0 }, /* 29 */
	{ 0x2692, 0xff, 0xf0,     0 }, /* 30 */
	{ 0x2693, 0xff, 0x81,     0 }, /* 31 */
	{ 0x2529, 0x04, 0x04,     0 }, /* 32 */
	{ 0x252a, 0x30, 0x30,     0 }, /* 33 */
	{ 0x252d, 0xff, 0x8f,     0 }, /* 34 */
	{ 0x2530, 0x30, 0x30,     0 }, /* 35 */
	{ 0x2531, 0x04, 0x04,     0 }, /* 36 */
	{ 0x2534, 0x01, 0x01,     0 }, /* 37 */
	{ 0x248c, 0x01, 0x01,     0 }, /* 38 */
	{ 0x2533, 0x03, 0x03,     0 }, /* 39 */
	{ 0x252f, 0xf0, 0x20,     0 }, /* 40 */
	{ 0x2694, 0xff, 0x41,     0 }, /* 41 */
	{ 0x26ab, 0xff, 0x27,     0 }, /* 42 */
	{ 0x26aa, 0x70, 0x10,     0 }, /* 43 */
	{ 0x26ae, 0xff, 0x01,     0 }, /* 44 */
	{ 0x268b, 0x10, 0x00,     0 }, /* 45 */
	{ 0x268c, 0xff, 0x67,     0 }, /* 46 */
	{ 0x268a, 0x40, 0x40,     0 }, /* 47 */
	{ 0x268a, 0x01, 0x01, 65000 }, /* 48 */
};

#define ACCDET_INIT_SEQ_LEN 44

/* 只读校验项：不写，只确认这些位已是期望值。 */
static const struct accdet_chk accdet_init_check[] = {
	{ 0x230c, 0x01, 0x00 }, /*  1 */
	{ 0x2688, 0xff, 0x00 }, /*  3 */
	{ 0x268d, 0xff, 0x00 }, /*  4 */
	{ 0x26a9, 0x01, 0x00 }, /*  5 */
};

#define ACCDET_INIT_CHECK_LEN 4

/* 采样项：cur_AB = (read8(0x26b6) >> 6) & 3；STATE = read8(0x26b7) & 7。 */
static const struct accdet_chk accdet_sample[] = {
	{ 0x26b6, 0xc0, 0x00 }, /* 49 */
	{ 0x26b7, 0x07, 0x00 }, /* 50 */
};

#define ACCDET_SAMPLE_LEN 2

#endif /* _ACCDET_INIT_TABLE_H_ */
```

## 8. 与现有裸机实现的差异（供后续对齐，不在本任务改动范围）

现有 `hakios-mt6895/mtk/mt6895/drivers/audio/hakios.mt6895_headset.c`
（`/home/lizi/IOS/hakios-mtk`，**本任务未改动该仓库**）的 `accdet_init_once()`：

| 项 | 现有实现 | 本表 | 差异性质 |
|---|---|---|---|
| `0x2697/0x2699` | `write16(..., 0x800)` | 同 | 一致 |
| `0x26a3` bit[7:4] | `configure(0x26a3, 0xf0, 0x30)` | 同 | 一致 |
| `0x26a4` bit[7:4] | `configure(0x26a4, 0xf0, 0x50)` | 同 | 一致 |
| `0x268e`/`0x2690` | `write16(0x268e, 0x4ff)`（低字节 `0xff`） | `0xff` + `0x04` | 一致 |
| `0x252a` | `configure(MICBIAS1_HIGH, 0x30, 0x30)` | 同 | 一致 |
| VAUD18 `0x1cb2` | **没有** | `0x01` | **缺**：现有实现没开 VAUD18 |
| `0x248c`（NCP） | 有 | 有 | 一致 |
| 最后开 `SW_EN` | **没有单独的开 SW_EN 步骤**（只在 `configure(ACCDET_CTRL, 0x01, 0x01)` 出现在插拔分支 `:334`/`:344`） | 明确列为 #48 并给 65 ms | 现有实现靠插拔分支补，表里是显式一步 |

> `0x1cb2` 这条建议后续单独验证：若 VAUD18 在冷启动时已由别处打开，写 1 无害；
> 若没打开，MICBIAS 就是死的，`0x26b6` 会一直读到 `0x3`（拔出）。

---

## 9. 未验证清单（汇总）

| # | 项 | 状态 |
|---|---|---|
| 1 | `0x230c` bit0 冷启动实值 | **未验证**（真机 dump 不含该地址） |
| 2 | `0x230d`/`0x230e`（CKPDN SET/CLR）地址 | **未验证**（命名推断，源码无使用点） |
| 3 | `0x268d`（PWM_IDLE）应为 0 | 依据 dump，非源码 |
| 4 | `0x26ad`（HWMODE_EN）应为 0 | 依据 dump，非源码 |
| 5 | `accdet_auxadc` debounce `0x44` 的时间单位 | **未验证**（注释说 2 ms，换算吻合但量纲存疑） |
| 6 | `headset-eint-trig-mode=1` 的实际作用 | 驱动无消费者，**推测无作用** |
| 7 | `headset-eint-level-pol` 缺省 8 的作用 | 驱动无消费者，**推测无作用** |
| 8 | `cur_AB==2` 的语义 | 源码无分支，`mt6368-accdet.h:45-52`（`enum accdet_status`）里 `BI_MIC_BIAS=3`，但源码没有对应分支，**本平台不用** |
| 9 | 整条表在真机上跑通 | **未验证**（从未跑到耳机代码，见 `ROUND-20-VERDICT.md` §8） |
| 10 | PMIC EINT 在裸机是否真的不可用 | 依据现有实现的选择，**未做过对照实验** |

---

## 10. 证据索引

| 结论 | 文件:行 |
|---|---|
| 初始化入口 | `mt6368-accdet.c:3139-3150`（`late_init`）、`mt6368.c:4931-4938`（调用者） |
| `accdet_init()` | `mt6368-accdet.c:3107-3134` |
| `accdet_init_debounce()` | `mt6368-accdet.c:3097-3105` |
| `accdet_init_once()` | `mt6368-accdet.c:2984-3095` |
| `config_eint_init_by_mode()` | `mt6368-accdet.c:2901-2982` |
| `config_digital_init_by_mode()` | `mt6368-accdet.c:2726-2885` |
| `accdet_set_debounce()` | `mt6368-accdet.c:1940-1996` |
| `enable_accdet()` | `mt6368-accdet.c:1814-1817` |
| dts 解析 | `mt6368-accdet.c:2517-2724` |
| `cur_AB` 读取 | `mt6368-accdet.c:2005-2006` |
| `check_cable_type()` | `mt6368-accdet.c:2000-2136` |
| 清 IRQ | `mt6368-accdet.c:1230-1247` |
| IRQ 申请 | `mt6368-accdet.c:3342-3349` |
| accdet dts 节点 | `mt6895.dtsi:2155-2175` |
| MT6368 = SID 5 | `mt6895.dtsi:2141-2147` |
| PMIC 中断 → GPIO243 / GIC 414 | `mt6895.dtsi:2032-2033` |
| `xaga-mt6895.dtsi` 的 `&accdet` 覆盖 | `xaga-mt6895.dtsi:607-615`（sparse clone `HEAD:`） |
| 现有裸机实现 | `/home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/audio/hakios.mt6895_headset.c`（只读，未改） |
