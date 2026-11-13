# MT6368 accdet 完整解码（可重实现规范）

> 对象：`docs/headset-wip/ko/mt6368-accdet.ko`（120232 B，not stripped，`.text` 0x0–0x6ce8，`.rodata` 0x18d8）
> 工具：`aarch64-linux-gnu-objdump -dr`、`aarch64-linux-gnu-objcopy -O binary --only-section=.rodata`、`readelf -sW/-rW`
> 对拍基准：任务给定的真机 `dump_reg` 值（耳机未插）
> 规范：本文件每条结论都带 `.text` 地址（`函数+0x偏移 @绝对地址`）或 `.rodata`/`.bss` 偏移。做不到的一律写「未确定」。

**怎么复现本次分析**

```sh
source ~/.dsh/env.sh
cd docs/headset-wip/ko
aarch64-linux-gnu-objdump -dr mt6368-accdet.ko > /tmp/accdet_full.dis
aarch64-linux-gnu-objcopy -O binary --only-section=.rodata mt6368-accdet.ko /tmp/rodata.bin
aarch64-linux-gnu-readelf -sW mt6368-accdet.ko | grep -E 'accdet_dts|cust_pwm_deb'
```

---

## 0. 先立地基：结构体布局（后面所有结论都靠它）

`nm -S` 给出 `accdet_dts` 是一个 0x8c 字节的 `.bss` 对象，起点 **`.bss+0x98`**，终点 `0x124`。
`accdet_probe` 里 26 次 `of_property_read_variable_u32_array` 的落点全部落在 `0x98..0x120`（见 §2.1 表），
所以「`.bss+0xNN`」就是「DT 结构体第 NN-0x98 字节」。这一点把原来所有「未确定」的偏移一次性解开了。

| 结构体偏移 | 含义 | DT 属性 / 默认值 | 证据 |
|---|---|---|---|
| `0x98` | mic_vol | `accdet-mic-vol`，缺省 8 | `accdet_probe+0x2d4` @0x3ab0，默认 `accdet_probe+0x308` @0x3ad8 |
| `0x9c` | mic_mode | `accdet-mic-mode`，缺省 2 | `accdet_probe+0x368` @0x3b30，默认 @0x3b38 |
| `0xa0` | plugout_deb | `accdet-plugout-debounce`，缺省 1 | `accdet_probe+0x338` @0x3b04，默认 @0x3b0c |
| `0xa4` | eint_level_pol | `headset-eint-level-pol`，缺省 8 | `accdet_probe+0x3f8` @0x3bbc，默认 @0x3bd0 |
| `0xa8..0xe3` | **`headset-mode-setting[15]`**（`cust_pwm_deb` 指向这里） | 15 个 u32 | 拷贝 `accdet_probe+0x3a8..0x3d4` @0x3b78-0x3b90；指针赋值 `accdet_probe+0x3fc` @0x3bb8 |
| `0xe4/0xe8/0xec` | 三键阈值 `thr[1..3]` | `headset-three-key-threshold` 或 `-CDD` | `accdet_probe+0x62c` @0x3df0-0x3e04 |
| `0xf0/0xf4/0xf8/0xfc` | 四键阈值 `thr[1..4]` | `headset-four-key-threshold` | `accdet_probe+0x5d8` @0x3dc0-0x3dc8 |
| `0x100` | moisture_detect_enable | `moisture_detect_enable`，缺省 0 | `accdet_probe+0x10c` @0x38e8，失败清零 @0x390c |
| `0x104` | eint_detect_mode | `eint_detect_mode`，缺省 4 | `accdet_probe+0x160` @0x3944，默认 @0x396c |
| `0x108` | eint_use_ext_res | `eint_use_ext_res` | `accdet_probe+0x138` @0x3914 |
| `0x10c` | accdet-eint-use_ext_res | 同名属性 | `accdet_probe+0x27c` @0x3a58 |
| `0x110` | accdet-eint-comp-vth | 同名属性 | `accdet_probe+0x2a8` @0x3a84 |
| `0x114` | moisture_detect_mode | `moisture_detect_mode`，缺省 4 | `accdet_probe+0x1a8` @0x3984 |
| `0x118` | moisture_comp_vth | 同名属性，缺省 0 | `accdet_probe+0x1d8` @0x39b4 |
| `0x11c` | moisture_comp_vref2 | 同名属性，缺省 0 | `accdet_probe+0x204` @0x39e0 |
| `0x120` | moisture_use_ext_res | 同名属性，缺省 -1 | `accdet_probe+0x230` @0x3a0c |

**推论 1**：`cust_pwm_deb`（`.bss+0x128` 的指针）在 `accdet_probe+0x3fc` @0x3bb8 被赋成 `&sms[0] = .bss+0xa8`。
所以 `accdet_init` 里的 `cust_pwm_deb[4..7]` 就是 `sms[4..7]`，**不是另一个数组**。
这一点纠正了旧版文档里「deb0/deb1/deb3/deb4 跳过 deb2」的猜测——下标是连续的 4,5,6,7。

**推论 2**：`0xd0/0xd4/0xd8/0xdc/0xe0` 全都落在 `sms[]` 内部：

```
0xd0 = sms[10]   0xd4 = sms[11]   0xd8 = sms[12]   0xdc = sms[13]   0xe0 = sms[14]
0xc8 = sms[8]    0xcc = sms[9]
```

本机 `sms = [0x500,0x500,0x1,0x1f0,0x800,0x800,0x20,0x44,0x4,0x1,0x5,0x3,0x3,0x5,0xe]`，于是：

| 变量 | 值 | 用在哪 | 真机对拍 |
|---|---|---|---|
| `sms[8]` = `0xc8` | `0x4` | `0x2694` 高 4 位 | `0x2694 = 0x41` ✅ |
| `sms[9]` = `0xcc` | `0x1` | `0x2694` 低 4 位 | 同上 ✅ |
| `sms[10]` = `0xd0` | `0x5` | `0x26a3` 低 4 位 | `0x26a3 = 0x35` ✅ |
| `sms[11]` = `0xd4` | `0x3` | `0x26a3` 高 4 位 | 同上 ✅ |
| `sms[12]` = `0xd8` | `0x3` | `0x26a4` 低 4 位（仅 `[0x100]==1` 时） | 本机 `[0x100]=0`，未写 → `0x26a4` 低 4 位保持 `0xe` |
| `sms[13]` = `0xdc` | `0x5` | `0x26a4` 高 4 位 | `0x26a4 = 0x5e` ✅ |
| `sms[14]` = `0xe0` | `0xe` | `0x26a5` 整字节 | `0x26a5 = 0x0e` ✅ |

四组值全部被真机寄存器反向验证过（`0x2694`、`0x26a3`、`0x26a4`、`0x26a5`），这是 §2 的地基。

---

## 1. 寄存器地图（0x2688 – 0x26d3，SID=5，16 位寄存器号）

图例：`R`=只读（驱动只读不写）、`W`=写、`RMW`=read-modify-write（`regmap_update_bits`）。
「代码未触及」= 全模块无任何 `mov w1,#0x26xx` 命中（我对全部 9404 行反汇编做了立即数扫描），只有 `dump_register` 会读它。

### 1.1 控制 / 状态（0x2688 – 0x268d）

| 寄存器 | 角色 | 位/掩码 | 证据 | 真机值 |
|---|---|---|---|---|
| `0x2688` | 代码未触及 | — | 仅 `dump_register+0x238` @0x2f94 起的分组读 | `0x00` |
| `0x2689` | 控制（模拟前端使能类） | bit1=`0x2`, bit2=`0x4` | `accdet_init_once+0x658` @0x1008（mask2 val2）、`+0x67c` @0x102c（mask4 val0）——**这两笔只在 `[0x100]==1`（moisture）分支里**，本机不执行；`adjust_eint_digital_setting+0xf0` @0x2ac8（mask2 val0） | `0x00` |
| `0x268a` | **控制：复位 + 中断/检测开关** | bit0=`0x1`、bit1=`0x2`、bit6=`0x40` | bit1 复位脉冲 `accdet_init+0x20` @0x7e8 / `+0x50` @0x818；bit0 `accdet_work_callback+0x3b8` @0x4ea0（清）、`eint_work_callback+0x80` @0x5198（置1）/`+0x1c0` @0x52d8（清0）；bit6 `accdet_init_once+0xc1c` @0x15cc（置 0x40，本机生效） | `0x40` ✅ |
| `0x268b` | **控制：数字 moisture + EINT 使能** | bit0=`0x1`、bit2=`0x4`、bit3=`0x8`、bit4=`0x10` | bit4 `accdet_init_once+0x998` @0x1348(置)/`+0xb34` @0x14e4(清，本机生效)；bit0 `+0xbb4` @0x1564；bit2/bit3 `accdet_irq_handle+0x2bc` @0x2070、`+0x368` @0x211c、`+0x828` @0x25dc、`+0x84c` @0x2600 | `0x00` ✅ |
| `0x268c` | 控制（固定常量写） | 整字节 | `accdet_init_once+0x3c4` @0xd74 写 `0x7`；`+0xb58` @0x1508 写 `0x67`（后者覆盖前者，本机生效） | `0x67` ✅ |
| `0x268d` | 代码未触及 | — | 仅 dump | `0x00` |

### 1.2 去抖/PWM 参数组（0x268e – 0x26a0）

| 寄存器 | 角色 | 写什么 | 证据 | 真机值 |
|---|---|---|---|---|
| `0x268e/0x268f` | **PWM 去抖参数 A（16 位，低字节先）** | `sms[0]-1 = 0x4ff` | `accdet_init_once+0xcc` @0xa7c、`+0xec` @0xa9c | `0xff` / `0x04` ✅ |
| `0x2690/0x2691` | **PWM 去抖参数 B（16 位）** | `sms[1]-1 = 0x4ff` | `+0x10c` @0xabc、`+0x12c` @0xadc | `0xff` / `0x04` ✅ |
| `0x2692` | **PWM 参数 C 低字节** | `sms[3] & 0xff = 0xf0` | `+0x14c` @0xafc | `0xf0` ✅ |
| `0x2693` | **PWM 参数 C 高位 + D bit0** | `(sms[3]>>8) \| (sms[2]<<7) = 0x1\|0x80 = 0x81` | `+0x164` @0xb14（`ldp w8,w10,[x8,#8]`→sms[2],sms[3]；`lsr`+`orr ... lsl #7`） | `0x81` ✅ |
| `0x2694` | **偏置/共模设置（两个 4 位字段）** | `[0xcc] \| ([0xc8]<<4) = 0x1\|0x40 = 0x41` | `+0x770` @0x1120（`ldp w10,w9`）、`+0x780` @0x1130 `orr w2,w9,w10,lsl#4`、写 @0x1138 | `0x41` ✅ |
| `0x2695/0x2696` | 代码未触及 | — | 仅 dump | `0x00` / `0x00`（未确定） |
| `0x2697/0x2698` | **插入去抖计数（16 位）** | `sms[4] = 0x800` | `accdet_init+0x88` @0x850 / `+0xa4` @0x86c；另 `accdet_set_debounce+0x50` @0x1c24、`accdet_work_callback+0x1f4` @0x4cdc | `0x00`/`0x08` ✅ |
| `0x2699/0x269a` | **拔出/其它去抖计数（16 位）** | `sms[5] = 0x800` | `accdet_init+0xb8` @0x880 / `+0xd4` @0x89c；`accdet_work_callback+0x21c` @0x4d04 | `0x00`/`0x08` ✅ |
| `0x269b/0x269c` | 去抖档位 2（仅 `accdet_set_debounce` 用） | 参数值 | `accdet_set_debounce+0xa8` @0x1c7c / `+0xbc` @0x1c90 | `0x10`/`0x00`（未确定，本机无写入路径调用它） |
| `0x269d/0x269e` | **按键/中断去抖计数（16 位）** | `sms[6] = 0x20` | `accdet_init+0xe8` @0x8b0 / `+0x104` @0x8cc；运行时被改写 `accdet_irq_handle+0x44c` @0x2200、`+0x498` @0x224c、`+0x6ec` @0x24a0 | `0x20`/`0x00` ✅ |
| `0x269f/0x26a0` | **拔出判定去抖（16 位）** | `sms[7] = 0x44` | `accdet_init+0x118` @0x8e0 / `+0x134` @0x8fc | `0x44`/`0x00` ✅ |

### 1.3 MIC 偏置 / 检测门限（0x26a1 – 0x26a8）

| 寄存器 | 角色 | 位/掩码 | 证据 | 真机值 |
|---|---|---|---|---|
| `0x26a1` | 代码未触及 | — | 仅 dump | `0x04`（未确定） |
| `0x26a2` | 代码未触及 | — | 仅 dump | `0x00`（未确定） |
| `0x26a3` | **低 4 位 = EINT/检测模式档；高 4 位 = mic 偏置档** | 低 `0x0f`、高 `0xf0` | 高 4 位 `accdet_init+0x158` @0x920（`val=[0xd4]<<4`）；低 4 位 `mt6368_accdet_late_init+0x78` @0x728（`val=[0xd0]`）、`accdet_irq_handle+0x24c` @0x2000（`val=[0xd0]`）、`+0x394` @0x2148（`val=6`） | `0x35` ✅ |
| `0x26a4` | **低 4 位 = moisture 档；高 4 位 = mic 偏置档（第二级）** | 低 `0x0f`、高 `0xf0` | 高 4 位 `mt6368_accdet_late_init+0x98` @0x748（`[0xdc]<<4=0x50`）、`accdet_irq_handle+0x280` @0x2034、`accdet_work_callback+0xe4` @0x4bcc、`eint_work_callback+0x338` @0x5450；低 4 位 `accdet_init+0x188` @0x950（`[0xd8]`，本机跳过）、`accdet_work_callback+0x270` @0x4d58（`0x10`） | `0x5e` ✅（高 4 位 `0x50` 由驱动写，低 4 位 `0xe` 代码里查不到写入 → 见 §7 未确定） |
| `0x26a5` | **MICBIAS 电压档（整字节）** | 整字节 | `accdet_init+0x1b0` @0x978（`val=[0xe0]=0xe`）、`accdet_set_debounce+0x1a4` @0x1d78 | `0x0e` ✅ |
| `0x26a6` | 代码未触及 | — | 仅 dump | `0xbf`（未确定） |
| `0x26a7` | 代码未触及 | — | 仅 dump | `0x0a`（未确定） |
| `0x26a8` | 代码未触及 | — | 仅 dump | `0x00`（未确定） |

### 1.4 中断状态 / 清除（0x26a9 – 0x26ae）

| 寄存器 | 角色 | 位 | 证据 | 真机值 |
|---|---|---|---|---|
| `0x26a9` | **状态（只读）：中断 pending** | bit0 = ACCDET_IRQ pending；bit2 = EINT0 pending；bit3 = EINT1 pending | 读 `accdet_irq_handle+0x2c` @0x1de0、`+0x8c` @0x1e40、`+0x94c` @0x2700；bit0 判定 `+0x3c` @0x1e80（`tbz w20,#0`）；bit2/3 采样 `+0x34..0x50` @0x1e1c-0x1e30；`0xc` 掩码再确认 `multi_key_detection+0x140` @0x6b88；`clear_accdet_eint_check+0x50` @0x28b4（等 bit2 落） | `0x00` ✅（未插且无 pending） |
| `0x26aa` | **中断清除/使能（写 1 → 再写 0）** | bit0 = IRQ 清除；bit2 = EINT0 清除；bit3 = EINT1 清除；bit4 = 中断输出使能 | bit0 置 `accdet_irq_handle+0xd4` @0x1e88、清 `+0x160` @0x1f14；`disable_accdet+0x28` @0x64bc / `+0xac` @0x6540；bit2/3 `accdet_irq_handle+0x30c` @0x20c0、`+0x334` @0x20e8、`+0x884` @0x2638、`+0x8ac` @0x2660、`clear_accdet_eint_check+0x7c` @0x28e0 / `+0xfc` @0x2960；bit4 `accdet_init_once+0x84c` @0x11fc | `0x10` ✅（bit4 已由驱动的 init_once 置位） |
| `0x26ab` | **模拟通路/检测使能** | bit5=`0x20`，另有整写 `0x27` / `0xc5` | 整写 `accdet_init_once+0x7a8` @0x1158 / `+0x7c8` @0x1178（caps bit2→`0x27`，bit3→`0xc5`，bit4→先 `0x27` 后 `0xc5`）；bit5 清 `+0x82c` @0x11dc、`adjust_eint_digital_setting+0x9c` @0x2a74 | `0x27` ✅（caps bit2 生效，见 §2.4） |
| `0x26ac` | 辅助位 | bit2=`0x4` | `accdet_init_once+0x81c` @0x11cc（写 4） | `0x00`（该分支未走，本机 `[0x114]=4`） |
| `0x26ad` | 清/复位位 | 整字节 | `accdet_init_once+0x3d8` @0xd88（写 0） | `0x00` ✅ |
| `0x26ae` | **检测模式选择（整字节 0/1/5）** | 整字节 | `+0x8c0` @0x1270（0）、`+0x9bc` @0x136c（1）、`+0xa20` @0x13d0（5） | `0x01` ✅（`[0x114]=4` 走 case4 → 写 1） |

### 1.5 检测/比较器设置（0x26b1 – 0x26c0）

| 寄存器 | 角色 | 位 | 证据 | 真机值 |
|---|---|---|---|---|
| `0x26af/0x26b0` | 代码未触及 | — | 仅 dump | `0x00` |
| `0x26b1` | moisture 比较器设置 | bit6=`0x40`、bit7=`0x80` | `+0x980` @0x1330（置 0x40，case3）、`+0x9d8` @0x1388（置 0x80，case5）、`adjust_eint_digital_setting+0x14c` @0x2b24 / `+0x1b4` @0x2b8c | `0x10`（**不是本模块写的** → 未确定，见 §7） |
| `0x26b2` | 同上（bit4） | `0x10` | `+0x95c` @0x130c（case3 置位） | `0x00`（case3 未走） |
| `0x26b3` | 同上（bit7） | `0x80` | `adjust_eint_digital_setting+0x170` @0x2b48 / `+0x1d8` @0x2bb0 | `0x27`（非本模块 → 未确定） |
| `0x26b4/0x26b5` | 代码未触及 | — | 仅 dump | `0x00` |
| `0x26b6` | **状态（只读）：AB/配件类型** | **bit[7:6] = AB**（0/1/2/3） | 读 `accdet_irq_handle+0xa8` @0x1e5c；`ubfx #6,#2` `accdet_work_callback+0x48` @0x4b30 → `+0x68` @0x4b50；`+0x37c` @0x4e64 | `0xff` → AB=3 = 未插 ✅ |
| `0x26b7` | 代码未触及 | — | 仅 dump | `0x00` |
| `0x26b8` | **状态（只读）：AB（IRQ 上下文用）** | bit[7:6] = AB | 读 `accdet_irq_handle+0xbc` @0x1e70、`+0x1b8` @0x1f6c；`ubfx #6,#2` @0x1f84 → 存 `[x8+368]` @0x1f88 | `0xff` → AB=3 ✅ |
| `0x26b9/0x26ba/0x26bb` | 代码未触及 | — | 仅 dump | `0x00` / `0xff` / `0x00`（未确定） |
| `0x26bc` | 代码未触及 | — | `dump_register+0x704` @0x3460 读过 | `0x00` |
| `0x26bd..0x26bf` | 代码未触及 | — | 仅 dump | `0x70` / `0x00` / `0x00`（未确定） |
| `0x26c0` | 代码未触及 | — | `dump_register+0x720` @0x347c | `0x00` |
| `0x26c1..0x26d3` | 代码未触及 | — | 仅 dump | 见任务给定值（`0x26c2=0x20` 等），**全部未确定来源** |

> **重要澄清**：`0x26a9` 的 bit2/bit3/bit4 不是「插入状态」。`accdet_irq_handle` 开头检查的是
> **caps 的 bit2/bit3/bit4**（EINT 配置），再由 caps 决定从 `0x26a9` 里取哪几位（§3.2）。
> 真正的「插了什么」在 `0x26b6`/`0x26b8` 的 bit[7:6]。

---

## 2. 初始化序列（可直接照抄）

### 2.1 probe：DT 解析（按执行顺序）

`accdet_probe` 0x37dc 起，`of_find_matching_node_and_match`（`+0x44` @0x38c0）找到
`mediatek,MT6368-accdet` 节点后，依次读：

| # | 属性 | 落点 | 证据 | 本机值 |
|---|---|---|---|---|
| 1 | `moisture_detect_enable` | `0x100` | @0x38e8 | 0（缺省） |
| 2 | `eint_use_ext_res` | `0x108` | @0x3914 | 0 |
| 3 | `eint_detect_mode` | `0x104` | @0x3944 | 4（缺省） |
| 4 | `moisture_detect_mode` | `0x114` | @0x3984 | 4（缺省） |
| 5 | `moisture_comp_vth` | `0x118` | @0x39b4 | 0（缺省） |
| 6 | `moisture_comp_vref2` | `0x11c` | @0x39e0 | 0（缺省） |
| 7 | `moisture_use_ext_res` | `0x120` | @0x3a0c | -1（缺省） |
| 8 | `accdet-eint-use_ext_res` | `0x10c` | @0x3a58 | 0 |
| 9 | `accdet-eint-comp-vth` | `0x110` | @0x3a84 | 2（缺省） |
| 10 | `accdet-mic-vol` | `0x98` | @0x3ab0 | 8 |
| 11 | `accdet-plugout-debounce` | `0xa0` | @0x3b04 | 1 |
| 12 | `accdet-mic-mode` | `0x9c` | @0x3b30 | 2 |
| 13 | `headset-mode-setting`（15 项） | `0xa8..0xe3` | @0x3b5c 读到栈 `sp+0x38`，@0x3b78-0x3b90 拷到 `0xa8` | 15 项 |
| 14 | `headset-eint-level-pol` | `0xa4` | @0x3bbc | 8（缺省，**DT 里没有这个属性**） |
| 15 | `headset-use-ap-eint` | caps bit0 | @0x3c04 | 0 → `caps \|= 1` |
| 16 | `headset-eint-num` | caps bit2/3/4 | @0x3c58，查表 `.rodata+0x18c0` = `{0x4,0x8,0x10}` | 0 → `caps \|= 0x4` |
| 17 | `headset-eint-trig-mode` | caps bit5/6 | @0x3cb0 | 1 → `caps \|= 0x40` |
| 18 | `headset-key-mode` | caps bit8/9/10 | @0x3d04，查表 `.rodata+0x18cc` = `{0x100,0x200,0x400}` | 0 → `caps \|= 0x100` |
| 19 | 三/四键阈值 | `0xe4..` 或 `0xf0..` | `+0x5a0` @0x3d78：caps bit9→`headset-four-key-threshold`；caps bit8→`headset-three-key-threshold-CDD`；否则 `headset-three-key-threshold` | 见下 |

**本机 caps = 0x145**（`1 | 4 | 0x40 | 0x100`），日志 `accdet caps=%x`（`.rodata+0xe5a`，@0x3be8）会打印这个值。

- caps bit8 置位、bit9 清零 → **读的是 `headset-three-key-threshold-CDD = <0 121 192 600>`**，
  不是 `headset-three-key-threshold = <0 80 220 400>`。落点：
  `0xe4 = 121`、`0xe8 = 192`、`0xec = 600`（`accdet_probe+0x62c` @0x3df0-0x3e04：
  `ldur x8,[sp,#44]` 取 `thr[1],thr[2]`，`ldr w9,[sp,#52]` 取 `thr[3]`）。
- 四键表 `0xf0..0xfc` 本机不读（`+0x5d8` @0x3dc0 只在该分支执行）。
- **`<0 ...>` 的 `thr[0]` 被丢弃**，代码只存 `thr[1..3]`。

### 2.2 三个入口函数的调用关系

```
probe ──(mod_timer bss+0x60, jiffies+0x9c4)──► delay_init_timerhandler (0x49f4)
                                                   │ queue_work
                                                   ▼
                                     delay_init_work_callback (0x4a4c)
                                       ① accdet_init           @0x4a60
                                       ② 0x26a3[3:0] = sms[10] @0x4a7c
                                       ③ 0x26a4[7:4] = sms[13]<<4 @0x4a9c
                                       ④ accdet_init_once      @0x4abc
```
证据：定时器 `init_timer_key` @0x4404、`expires` @0x4408-0x442c、`mod_timer` @0x472c。

`mt6368_accdet_late_init`（0x6b0，`EXPORT_SYMBOL`，由 `snd-soc-mt6368.ko` 调用）做**完全相同**的四步，
只是用 `accdet_first`（`.bss+0x58`）做 once 保护，并在最前面 `del_timer_sync(.bss+0x60)`：

```
del_timer_sync(bss+0x60)                      @0x708
accdet_init()                                 @0x70c
0x26a3: update_bits(0x0f, [0xd0])             @0x728   ← sms[10] = 5
0x26a4: update_bits(0xf0, [0xdc]<<4)          @0x748   ← sms[13] = 5 → 0x50
accdet_init_once()                            @0x768
```

**这两个入口的先后不能交换**：`accdet_init` 先把 `0x26a3` 高 4 位写成 `sms[11]<<4 = 0x30`，
`late_init` 随后把低 4 位写成 `0x5`，才得到真机的 `0x35`。同理 `0x26a4` 高 4 位由 `late_init` 写 `0x50`，
低 4 位 `0xe` 全模块无写入路径 → 是硬件/更早阶段的默认值（§7）。

### 2.3 `accdet_init`（0x7c8）逐条

| # | 寄存器 | 操作 | 本机值 | 证据 |
|---|---|---|---|---|
| 1 | `0x268a` | `update_bits(0x02, 0x02)` | bit1=1 | `+0x20` @0x7e8-0x804 |
| 2 | — | `usleep_range(2000, 3000)` | — | `+0x40` @0x808（`0x7d0`,`0xbb8`） |
| 3 | `0x268a` | `update_bits(0x02, 0x00)` | bit1=0 | `+0x50` @0x818-0x834 |
| 4 | — | `usleep_range(1000, 1500)` | — | `+0x70` @0x838（`0x3e8`,`0x5dc`） |
| 5 | `0x2697/0x2698` | `write`，16 位拆分 | `0x00/0x08`（`sms[4]=0x800`） | `+0x88` @0x850 / `+0xa4` @0x86c |
| 6 | `0x2699/0x269a` | 同上 | `0x00/0x08`（`sms[5]`） | `+0xb8` @0x880 / `+0xd4` @0x89c |
| 7 | `0x269d/0x269e` | 同上 | `0x20/0x00`（`sms[6]=0x20`） | `+0xe8` @0x8b0 / `+0x104` @0x8cc |
| 8 | `0x269f/0x26a0` | 同上 | `0x44/0x00`（`sms[7]=0x44`） | `+0x118` @0x8e0 / `+0x134` @0x8fc |
| 9 | `0x26a3` | `update_bits(0xf0, [0xd4]<<4)` | `0x30`（`sms[11]=3`） | `+0x158` @0x920 |
| 10 | `0x26a4` | 仅当 `[0x100]==1`：`update_bits(0x0f, [0xd8])` | **跳过**（`[0x100]=0`） | 判定 `+0x174` @0x93c，写入 `+0x188` @0x950 |
| 11 | `0x26a5` | `write([0xe0])` | `0x0e`（`sms[14]`） | `+0x1b0` @0x978 |
| 12 | — | `printk("accdet_init")` | — | `+0x1bc` @0x984（`.rodata+0x796`,`+0xddc`） |

> `+0x170` @0x914 的 `ldr w20,[x9,#44]`（x9 = `.bss+0xd4`）读的是 `.bss+0x100`，即
> `moisture_detect_enable` —— 这就是第 10 步的条件变量，**旧版文档里「来源未证明」的那一项**。

### 2.4 `accdet_init_once`（0x9b0）本机实际执行路径

按 `caps=0x145`、`mic_vol=8`、`mic_mode=2`、`[0x100]=0`（moisture 关）、`[0x104]=4`、`[0x114]=4` 走完分支后，
实际落地的写入如下（顺序即代码顺序）。

`0x25xx` 段（codec 模拟前端）只在 `[0x100]==1`（moisture 开）时才大面积配置；
本机 `[0x100]=0`，所以 `0x25xx` 段只有 mic_vol / mic_mode / caps 三组必写项（第 3/4/9–12 步）。

| # | 寄存器 | 操作 | 本机值 | 证据 |
|---|---|---|---|---|
| 1 | `0x2319` | `update_bits(0x02, 0x02)` | 置位 | `+0x50` @0xa00 |
| 2 | `0x2319` | `update_bits(0x02, 0x00)` | 复位 | `+0x74` @0xa24 |
| 3 | `0x252a` | `update_bits(0x30, 0x00)` | 清 mic_vol 档 | `+0x7c` @0xa2c-0xa48 |
| 4 | `0x2529` | `update_bits(0x70, 0x00)` | 清 mic_vol 高位 | `+0xa0` @0xa50-0xa6c |
| 5 | `0x268e/0x268f` | `write (sms[0]-1)` 低/高 | `0xff`,`0x04` | `+0xcc` @0xa7c / `+0xec` @0xa9c（`sub w8,w8,#1` @0xa88/@0xaa8） |
| 6 | `0x2690/0x2691` | `write (sms[1]-1)` | `0xff`,`0x04` | `+0x10c` @0xabc / `+0x12c` @0xadc |
| 7 | `0x2692` | `write sms[3]&0xff` | `0xf0` | `+0x14c` @0xafc |
| 8 | `0x2693` | `write (sms[3]>>8)\|(sms[2]<<7)` | `0x81` | `+0x164` @0xb14 |
| 9 | `0x2529` | `update_bits(0x04, 0x04)` — **mic_vol==8 档** | `0x04` | 分支 `+0x184` @0xb34（`cmp w8,#7; b.hi`）→ `+0x1d0` @0xb80-0xb9c |
| 10 | `0x252a` | 读后 `write(old \| 0x30)` — mic_vol==8 档 | `0x30` | 读 `+0x208` @0xbb8，`orr #0x30` @0xbc0，写 `+0x26c` @0xc1c |
| 11 | `0x252d` | 读后 `write(old \| 0x8f)` — mic_mode==2 | — | mic_mode 分派 `+0x294` @0xc44-0xc58（`cmp #6`/`#2`/`#1`），读 `+0x2a8` @0xc2c，写 `+0x34c` @0xcfc |
| 12 | `0x2530` | `update_bits(0x30, 0x30)` — mic_mode==2 | — | `+0x354` @0xd04-0xd20 |
| 13 | `0x2531` | `update_bits(0x04, 0x04)`（caps bit2） | — | 分派 `+0x3c4` @0xdc4，写 `+0x44c` @0xdfc-0xe18 |
| 14 | `0x2534` | `update_bits(0x01, 0x01)` | — | `+0x474` @0xe24-0xe40 |
| 15 | `0x248c` | `update_bits(0x01, 0x01)` | — | `+0x494` @0xe44-0xe60 |
| 16 | `0x2533` | `update_bits(0x03, 0x03)`，条件 `([0x104]-1) > 2` | `0x03` | 判定 `+0x4b4` @0xe64-0xe74（`sub #1; cmp #2; b.hi`），`[0x104]=4` → 3>2 成立，写 `+0x5ac` @0xf5c-0xf7c |
| 17 | `0x252f` | `update_bits(0xf0, [0x110]<<4)` | `0x20`（`accdet-eint-comp-vth = 2`） | `+0x5d0` @0xf80：`adrp x19,.bss+0x100`、`ldr w9,[x19,#16]`（=`[0x110]`）、`lsl #4`、写 `+0x5ec` @0xfa0-0xfac |
| 18 | `0x2694` | `write [0xcc]\|([0xc8]<<4)` | `0x41` | `[0x100] != 1` → `+0x76c` @0xfbc 跳到 0x111c，写 `+0x784` @0x1120-0x1138 |
| 19 | `0x26ab` | `write 0x27`（caps bit2） | `0x27` | 分支 `+0x798` @0x1148，写 `+0x7c8` @0x1178 |
| 20 | `0x26aa` | `update_bits(0x10, 0x10)` | bit4=1 | `+0x84c` @0x11fc |
| 21 | 模式表 `[0x114]-1` | `[0x114]=4` → index 3 → 目标 `0x1340` | — | 跳表 `.rodata+0x0000` = `{0x00,0x21,0x27,0x3f,0x4f}`，基址 `0x1244`（`+0x87c` @0x122c-0x1240） |
| 22 | `0x268b` | `update_bits(0x10, 0x10)`（模式表 index3 块） | 置 | `+0x998` @0x1348 |
| 23 | `0x26ae` | `write 1`（同一块尾部） | `0x01` | `+0x9c0` @0x1370-0x1378 |
| 24 | `0x268b` | `update_bits(0x10, 0x00)`（`[0x100]==0`） | 清 → `0x00` | 判定 `+0xb10` @0x14c0，写入 `+0xb34` @0x14e4 |
| 25 | `0x268c` | `write 0x67` | `0x67` | `+0xb58` @0x1508-0x1514 |
| 26 | `0x268a` | `update_bits(0x40, 0x40)`（caps bit2） | `0x40` | 分派 `+0xa70` @0x1080 前先判 caps；写 `+0xbc0` @0x1570-0x15cc |
| 27 | — | `printk("...done")` | — | `+0xc38` @0x15e8 |

**对拍结果（`0x26xx` 全中 12/12，`0x25xx` 全中 2/2）**：
第 5–8、18、23、24、25、26 步给出
`0x268e/0x268f=0xff/0x04`、`0x2690/0x2691=0xff/0x04`、`0x2692=0xf0`、`0x2693=0x81`、
`0x2694=0x41`、`0x26ae=0x01`、`0x268b=0x00`、`0x268c=0x67`、`0x268a=0x40`，与真机逐字节一致；
第 9/10 步给 `0x2529=0x04`、`0x252a=0x30`，也与真机一致。

第 9/10 步同时也是旧版文档的误判点：它把 `0x2529[2]` / `0x252a[5:4]` 当成「codec 侧写的、原因未知」。
实际上 `accdet_init_once` 自己就写了 —— **这两位是 `accdet-mic-vol = 8` 的档位编码**
（`mic_vol ≤ 7` 走 `+0x18c..+0x1a0` 的 `write(old \| (mic_vol<<4) \| 0x4)`，`mic_vol == 9` 走 `+0x218` 的 `update_bits(0x01,0x01)`）。

### 2.5 可直接照抄的顺序表（本机型最终值）

```text
# ---- stage A: accdet_init_once（本机型实际执行的 27 步里挑出必须的） ----
0x2529 &= ~0x70                           # 清 mic_vol 高位
0x2529 |= 0x04                            # accdet-mic-vol = 8 档
0x252a &= ~0x30
0x252a |= 0x30                            # 同上，档位另一半
0x268e <= 0xff      0x268f <= 0x04        # sms[0]-1 = 0x4ff
0x2690 <= 0xff      0x2691 <= 0x04        # sms[1]-1 = 0x4ff
0x2692 <= 0xf0      0x2693 <= 0x81        # sms[3], sms[2]<<7
0x252d |= 0x8f                            # mic_mode = 2
0x2530 |= 0x30
0x2531 |= 0x04                            # caps bit2（headset-eint-num=0）
0x2534 |= 0x01
0x248c |= 0x01
0x2533 |= 0x03                            # [0x104]=4 → 触发
0x252f |= 0x20                            # [0x110]<<4，accdet-eint-comp-vth=2
0x2694 <= 0x41                            # sms[9] | sms[8]<<4
0x26ad <= 0x00
0x26ab <= 0x27                            # caps bit2 分支
0x26aa |= 0x10                            # 中断输出使能
0x268b |= 0x10                            # 模式块内置位
0x26ae <= 0x01                            # 同一块尾部
0x268b &= ~0x10                           # 紧接着又清（moisture_detect_enable=0）→ 净结果 0x00
0x268c <= 0x67
0x268a |= 0x40
# ---- stage B: accdet_init ----
0x268a |= 0x02 ; delay 2500us
0x268a &= ~0x02; delay 1250us
0x2697 <= 0x00  0x2698 <= 0x08            # 0x0800
0x2699 <= 0x00  0x269a <= 0x08            # 0x0800
0x269d <= 0x20  0x269e <= 0x00            # 0x0020
0x269f <= 0x44  0x26a0 <= 0x00            # 0x0044
0x26a3 = (r & 0x0f) | 0x30                 # 高 4 位 = sms[11]
# 0x26a4 低 4 位本机不写（moisture_detect_enable=0）
0x26a5 <= 0x0e
# ---- stage C: late_init / delay_init 的收尾（必须在 stage B 之后）----
0x26a3 = (r & 0xf0) | 0x05                 # 低 4 位 = sms[10]
0x26a4 = (r & 0x0f) | 0x50                 # 高 4 位 = sms[13]
```

---

## 3. 插入检测的判定逻辑

### 3.1 谁表示「已插入」

**不是 `0x26a9` bit0，而是 `0x26b6[7:6]`（IRQ 上下文用 `0x26b8[7:6]`）。**

证据链：
- `accdet_irq_handle` 收尾读 `0x26b8`，`ubfx #6,#2` 得到 `cur_AB`（@0x1f78-0x1f88），据此 4 路分派（跳表 `.rodata+0x0f` = `{0,0x16,0x24,0x0f}`，基址 `0x1fb4`）。
- `accdet_work_callback` 读 `0x26b6`，`ubfx #6,#2` 得 AB（@0x4b30-0x4b50），并打印
  `"cur_AB = %d, accdet_status = %d"`（`.rodata+0xbf4`，@0x4b60）。
- 真机未插：`0x26b6 = 0xff`、`0x26b8 = 0xff` → AB = 3 → 状态机判「拔出」。
- 「插拔」在没有耳机时无法实测，AB 的 0/1/2 语义只能从代码分支 + 日志字符串推出（见 §3.3），**标为待真机验证**。

### 3.2 `accdet_irq_handle` 开头为什么看 bit2/3/4

`0x1e04-0x1e30`：

```
w10 = *(int*)(bss+0xC0)        ← caps（DT 算出来的能力位）
w9  = read(0x26a9)
if      (caps & 0x04) w19 = (w9 >> 2) & 1   ← EINT0 在 0x26a9 的 bit2
else if (caps & 0x08) w19 = (w9 >> 2) & 2   ← EINT1 在 bit3
else if (caps & 0x10) w19 = (w9 >> 2) & 3   ← 两个 EINT
else                  w19 = 0
```

所以 **bit2/bit3 是 EINT0/EINT1 的 pending 位，bit4 不是状态位**；`caps & 0x10`（`headset-eint-num` 传 2 时才是 0x10）
表示「两个 EINT 都要看」，此时用掩码 `3` 同时取 bit2+bit3。本机 `headset-eint-num = 0` → caps bit2 → 只看 `0x26a9` bit2。

主流程（@0x1e7c-0x1e88）：

```
if (w19 != 0) goto EINT 路径 (0x1f5c)
if ((0x26a9 & 1) == 0) goto 0x1f5c → cbz w19 → printk("no interrupt detected") (0x237c)
否则：0x26aa bit0 = 1（ACK ACCDET_IRQ）@0x1e88
     if (accdet_status == 1) → 读 IIO auxadc 做 moisture 检查 @0x231c
     else queue_work(accdet_work_callback) @0x1ec0
```

### 3.3 AB → cable_type 的映射（`accdet_work_callback` 状态机）

入口：`w21 = cable_type`（`[0x160]` @0x4b1c）、`w19 = AB`（`0x26b6[7:6]`）、`status = [0x15c]` @0x4b4c。
`[0x15c]`/`[0x160]` 是相邻两字，代码用一次 `str x` 同时写（`0x100000001` = status 1 + cable_type 1）：
`status` 的取值在代码里就是 0/1/2 三个，`2` 是「已插入且已完成类型识别」的稳定态
（`send_status_event(cable_type, 0)` 走 status 路径 @0x5308 也印证这一点：`b9416100 ldr w0,[x8,#352]` 直接拿 cable_type 报告）。

| status | AB | 动作 | 证据 |
|---|---|---|---|
| 0 | 0 | `status=2, cable_type=2`（`str x10=0x200000002`） | 分支 `+0x84` @0x4b94 落空 → `+0xd0` @0x4bb0 |
| 0 | 1 | `status=1, cable_type=1`（`str x10=0x100000001`） | `+0xa4` @0x4b90 → `+0x1c8` @0x4cb0 → `+0x1dc` @0x4cc4 |
| 0 | 3 | `status=0, cable_type=0`（`str xzr`） | `+0xa8` @0x4b88 → `+0x19c` @0x4c84 → `+0x1bc` @0x4ca4 |
| 0 | 2 | 打印 `"Invalid AB. Do nothing"`，丢弃 | `+0xb8` @0x4b94 → `+0x4a8` @0x4f90（`.rodata+0xd24`） |
| 1 | 0 | `status=2, cable_type=2`，并 `multi_key_detection(0)` | `+0x130` @0x4c18 → `+0x158` @0x4c40-0x4c54 |
| 1 | 1 | `status=1, cable_type=1`（保持） | `+0x130` @0x4c18 → `+0x244` @0x4d2c → `+0x258` @0x4d40 |
| 1 | 3 | `status=0, cable_type=0`（拔出） | `+0x130` @0x4c18 → `+0x178` @0x4c60 → `+0x18c` @0x4c74 |
| 1 | 2 | Invalid AB | 同上 `+0x4a8` @0x4f90 |
| 2 | 0 | `status=0, cable_type=0`（`cbz w19 → 0x4d78` 前先清） | `+0xf8` @0x4be0 判定，`+0x290` @0x4d78 |
| 2 | 1 | `multi_key_detection(1)`（按键轮询，不换状态） | `+0xf8` @0x4be0 → `+0x100` @0x4bf4 → `+0x120` @0x4c08 |
| 2 | 3 | `status=0, cable_type=0` | `+0xf8` @0x4be0 → `+0x178` @0x4c60 |
| 2 | 2 | Invalid AB → 丢弃 | `+0x4a8` @0x4f90 |

随后：
- 若 `status != 1`，`cable_type != 0` 时写 `0x1bc8 = 0x85`（`@0x4dd0`，不属 `0x26xx` 域）。
- `send_status_event(cable_type, 1)` 只在 `cable_type != 本次入口值` 时发（`@0x4d90` 比较、`@0x4da0` 调用）。
  注意 `+0x4d90` 的 `ldr w0,[x8,#352]` 让 status==1 的分支用更新后的 cable_type 作为参数。

**三态区分**：
- 未插 → AB = 3 → `cable_type = 0`（sysfs `state` 读的就是这个字节，`state_show` @0x5a18 `ldrb w3,[x8,#352]`，真机 `state = 0` ✅）
- 3-pole 耳机 → `cable_type = 2`；4-pole 耳麦 → `cable_type = 1`
  依据：`send_status_event` 里 `cable_type==2` 分支打印 `.rodata+0x932 = "accdet HEADPHONE(3-pole) %s"`（@0x4e4、@0x688），
  `cable_type==1` 分支打印 `.rodata+0x13d8 = "accdet MICROPHONE(4-pole) %s"`（@0x608）。
  **注意**：这与「AB=1 对应 3-pole」的直觉相反，本机无耳机无法实测 → §7 列为待验证。

### 3.4 `mux_select` / `accdet_status` / `cable_type` 的落点

| 变量 | `.bss` 偏移 | 读写点 | 语义 |
|---|---|---|---|
| `accdet_status` | `+0x15c` | 写 `str x` @0x4bb4 / @0x4ccc / @0x4d48（`status`+`cable_type` 一次写两字）、`str wzr` @0x4c74 / `str xzr` @0x4ca8 | 0=未插 / 1=已插待判 / 2=已判类型 |
| `cable_type` | `+0x160` | 同上一并写；读 `@0x4b1c`（`ldr w21`）、`@0x5a18`（`state_show` 的 `ldrb`）、`@0x5304`（`send_status_event`） | 0=无 / 1=耳麦 / 2=耳机（见 §7 待实测） |
| `cur_AB` | `+0x170` | 写 `@0x1f88`（IRQ 路径，取 `0x26b8[7:6]`）；`accdet_work_callback` 只在局部 `w19` 用，不落盘 | `0x26b6`/`0x26b8` 的 bit[7:6] |
| `cur_key` | `+0x164` | 写 `@0x6b48`；清 `@0x6bc4` / `@0x6bd0` / `@0x53ec` | §4 的 key 值 |
| `cur_voltage` | `+0x168` | 写 `@0x2568` | auxadc mV − efuse 偏移（`+0x16c`） |
| `is_enable` | `+0x14c` | 置 1 `@0x213c`（`strb`）；清 0 `@0x52ac` | 驱动是否已使能（`0x26a9` 采样前置条件） |
| `dis_micbias_done`（局部标志） | `+0x148` | 清 0 `@0x4b60`、置 1 `@0x4c4c` | `accdet_work_callback` 的「已处理」标记 |
| `cur_eint_state` | `+0x150` | 写 `cset w9,eq` `@0x1904`→`@0x190c`；清 `@0x2054`/`@0x2274`/`@0x2398`/`@0x23e8`/`@0x36d8`/`@0x5510`；置 1 `@0x2014`/`@0x228c`/`@0x555c` | 当前 EINT 电平（1=高） |
| EINT0/EINT1 清除标记 | `+0x154` / `+0x158` | 清 0 `@0x2228` / `@0x24c8`；置 1 `@0x2480` / `@0x24dc` | 每个 EINT 各自的「已 clear」状态，与 `+0x150` 按位与/或合成 `@0x24fc`/`@0x2518` |

**`mux_select` 全模块不存在**：`nm mt6368-accdet.ko | grep -i mux` 为空，也没有同名 DT 属性。
早期 MTK 版本用它选「EINT 走 AP 还是 PMIC」，本版改成 caps bit0（`headset-use-ap-eint`）+ caps bit2/3/4（`headset-eint-num`）
两个概念替代，所以这个变量**在本机型上没有对应实现**。

---

## 4. 按键阻抗判定（`multi_key_detection` 0x6a48）

输入：`w0 = 0`（首次判定，自己算电压）/ `!= 0`（用已存的 `cur_key`）。
电压来源：`[x8+360]`（`cur_voltage`，mV）。它由 `accdet_irq_handle+0x508` @0x2568 写入：
`max(0, auxadc_mV - [x8+364])`，`[x8+364]` 是 efuse 校准偏移（`accdet_probe+0x58c` @0x3fec 起，nvmem 偏移 `0xb6`）。

阈值来源：`caps` 的 byte1 bit1（= caps bit9）判断四键；本机 caps=0x145 → bit9=0 → **走三键表**
（`@0x6a80` 的 `ldrb w10,[x9,#1]` + `@0x6a88` 的 `tbnz w10,#1, 0x6ad4`）。

三键表本机值（CDD 表）：`[0xe4]=121`、`[0xe8]=192`、`[0xec]=600`。
代码先 `ldp w10,w11,[.bss+0xe8]`（`@0x6a94`）→ `w10=192`、`w11=600`。

反汇编判定树（`@0x6a98-0x6b48`，按本机阈值代入，逐分支复核过）：

| 电压 `cur`（mV） | key | 路径 | 证据 |
|---|---|---|---|
| `>= 600` | **0**（无键） | `600<=cur` → 6aa8 → 121 比较 ls → 6b3c → `192>600` 不成立 → 6b44 置 0 | `+0x50` @0x6a98 → `+0x108` @0x6b44 |
| `192 .. 600` | **4** | `192<=cur` 且 `cur<600` | `+0x58` @0x6aa0 → `+0xa8` @0x6af0（`mov w2,#4`） |
| `121 .. 192` | **1** | 6aa8 → `121<=cur` 成立、`192>cur` → `cinc` 得 1，6b3c 保留 | `+0x60` @0x6ab4-0x6abc，`+0xf4` @0x6b3c |
| `< 121` | **2** | `121<=cur` 不成立 → `ccmp ...,#0`（nzcv=0 → LS 为真）→ `cinc` 得 2 → `@0x6ac8` 直接存 | `+0x60` @0x6ab4-0x6abc，`+0x80` @0x6ac8 |

> `< 121` 落到 key **2** 而不是 1，是 `ccmp w10,w9,#0x0,ls` 这条指令的语义造成的
> （cond 不成立时把 nzcv 直接置 0，于是紧跟的 `cinc ...,ls` 恒加 1）。这是真实行为，不是笔误。

四键表（本机不走，caps bit9 置位才走；阈值 58/121/192/400，`@0x6ad4-0x6b44`）：
`<58 → 2`、`58..121 → 8`、`121..192 → 1`、`192..400 → 4`、`>=400 → 0`。

结果存 `[x8+356]`（`cur_key`，`@0x6b48`），再：
1. `usleep_range(10000, 12000)`（`@0x6b60`，`0x2710/0x2ee0`）
2. 复读 `0x26a9`，`tst w9,#0xc`（bit2/bit3 仍有 pending → 放弃本次）`@0x6b88-0x6b9c`
3. 仅当 `accdet_status == 1` 时才 `send_key_event(cur_key, pressed)` `@0x6ba0-0x6bb8`

### 4.1 key 值 → jack 位 → Linux keycode

`send_key_event`（0x6658）的跳表在 `.rodata+0x1c`（8 项，索引 `key-1`）：

| key | 触发的块 | jack 掩码 | `snd_jack_set_key` 绑定的 keycode | 证据 |
|---|---|---|---|---|
| 1 | 0x6690 | `0x1000` | `0x101` = 257（`BTN_1`） | 表项 0 @0x6674-0x6688；块 @0x66d8（`mov w2,#0x1000`） |
| 2 | 0x66e0 | `0x4000` | `0xe2` = 226（`KEY_MEDIA`） | 表项 1；块 @0x672c |
| 3 / 5 / 6 / 7 | 0x67a8（直接返回） | — | — | 表项 = `0x47`（跳到函数尾） |
| 4 | 0x6734 | `0x2000` | `0x102` = 258（`BTN_2`） | 表项 3；块 @0x6780 |
| 8 | 0x6788 | `0x800` | `0x246` = 582（`KEY_VOICECOMMAND`） | 表项 7；块 @0x6798 |

keycode 绑定证据：`mt6368_accdet_init`（0x17a0）里 4 次 `snd_jack_set_key`
（`@0x1834` 0x4000→0xe2、`@0x1848` 0x2000→0x102、`@0x185c` 0x1000→0x101、`@0x1870` 0x800→0x246）。
每个块还会维护 `.bss+0x10/0x12/0x0e` 的 4 位计数（按下/抬起位图），再 `snd_soc_jack_report`。

---

## 5. 中断路径

### 5.1 注册

| 中断 | `platform_get_irq` 索引 | 名字 | 处理函数 | 证据 |
|---|---|---|---|---|
| `ACCDET_IRQ`（SPMI 子号 97） | 0 | `.rodata+0x36e` = `"ACCDET_IRQ"` | `mtk_accdet_irq_handler_thread`（`0x6c78` = 0x4974 的 CFI 跳板） | `@0x4254`（get_irq）、`@0x4290`（request）、`@0x4264` 存 `[x8+232]` |
| `ACCDET_EINT0`（子号 98） | 1 | `.rodata+0xd05` = `"ACCDET_EINT0"` | 同上 | `@0x42b4`/`@0x4328`（get_irq）、`@0x42ec`/`@0x4360`（request）、存 `[x8+236]` |
| `ACCDET_EINT1`（子号 99） | 2 | `.rodata+0x1f7` = `"ACCDET_EINT1"` | 同上 | `@0x42f8`（get_irq）、`@0x4360`（request）、存 `[x8+240]` |

标志全为 `0x2004` = `IRQF_ONESHOT | IRQF_TRIGGER_HIGH`（`@0x4280`、`@0x42e0`、`@0x4354`）。
EINT0/EINT1 是否注册由 caps 决定（`@0x42a8`：bit2→只注册 EINT0；bit3→注册 EINT1；bit4→两个都注册）。
本机 caps bit2 → **只注册 97 和 98**。

线程处理函数 `mtk_accdet_irq_handler_thread`（0x4974）就是 `bl accdet_irq_handle` 后 `return IRQ_HANDLED`（`@0x4984`）。

另有 AP-EINT 路径（`headset-use-ap-eint = 1` 时）：`devm_pinctrl_get` @0x4544、`pinctrl_select_state` @0x45a0、
`of_get_named_gpio_flags("deb-gpios")` @0x45d0 → `[x8+392]`、`of_property_read("debounce")` @0x45f4 → `[x8+396]`、
`gpiod_set_debounce` @0x4610、`irq_of_parse_and_map` @0x461c → `[x8+400]`、`request_threaded_irq`（`ex_eint_handler`）@0x4670。
本机 `headset-use-ap-eint = 0` → **整段不执行**。

### 5.2 清除（都写「1 再写 0」）

| 中断源 | 清哪个寄存器哪一位 | 证据 |
|---|---|---|
| `ACCDET_IRQ` | `0x26aa` bit0：`update_bits(0x01,0x01)` → `update_bits(0x01,0x00)` | 置 `accdet_irq_handle+0xd4` @0x1e88；清 `+0x160` @0x1f14（前置轮询 `0x26a9` bit0 最多 400 µs，`+0xf8` @0x1ef8-@0x1f0c） |
| 同时还会写 `0x2323` bit1（PMIC EINT 控制器） | `0x2323` bit1 = `0x2` | 置 `accdet_irq_handle+0x178` @0x1f38-@0x1f54；`disable_accdet+0xa4` @0x6564-0x6580 |
| `ACCDET_EINT0` | `0x26aa` bit2 (`0x04`) **且** `0x2323` bit2 (`0x04`) | `accdet_irq_handle+0x30c` @0x20c0 / `+0x334` @0x20e8；`clear_accdet_eint_check+0x44..0x78` @0x28b4-0x2920 |
| `ACCDET_EINT1` | `0x26aa` bit3 (`0x08`) **且** `0x2323` bit3 (`0x08`) | `+0x884` @0x2638 / `+0x8ac` @0x2660；`clear_accdet_eint_check+0xc4..0x140` @0x2934-0x29a0 |

`clear_accdet_eint_check(eint_mask)`（0x2864）的逻辑：先轮询 `0x26a9` 对应 pending 位最多 **0x61a80 ns ≈ 400 µs**
（`sched_clock` 差值比较，`@0x2888` `0x61a80`），再清。这一点裸机可照搬：**清中断前要等 pending 位落下**，
否则清不掉（硬件是电平式的）。

`disable_accdet`（0x6494）是关机/卸载路径：先置 `0x26aa` bit0，再 `__const_udelay(0xd1b78)` ≈ 200 ms 级忙等，
再轮询 `0x26a9` bit0 直到落下（上限 `0xc35`×128 ns ≈ 400 µs），最后清 bit0、清 `0x2323` bit1、
把 `0x2697/0x2699/0x269d` 恢复成 `sms` 默认值（`@0x659c-0x6620`）。

---

## 6. 最小可行检测流程（裸机操作手册）

前提：SPMI 写/读已能访问 MT6368（SID=5）；`0x26xx` 是 ACCDET 寄存器域。
**只做读 + 最少写**就能判插拔；初始化是为了让模拟前端上电、比较器工作。

### 6.1 上电后必须写的（本机型实测值）

```c
/* --- 0. 探活：读 0x26b6，任何值都可，只要 SPMI 读写通 --- */
u8 probe = spmi_read8(5, 0x26b6);

/* --- 1. 模拟前端基础配置（accdet_init_once 的 0x26xx 部分） --- */
spmi_write8(5, 0x268e, 0xff);  spmi_write8(5, 0x268f, 0x04);  /* PWM 去抖 16 位 = 0x4ff */
spmi_write8(5, 0x2690, 0xff);  spmi_write8(5, 0x2691, 0x04);
spmi_write8(5, 0x2692, 0xf0);  spmi_write8(5, 0x2693, 0x81);
spmi_write8(5, 0x2694, 0x41);                                 /* 偏置/共模 */
spmi_write8(5, 0x26ad, 0x00);
spmi_write8(5, 0x26ab, 0x27);                                 /* 模拟检测通路使能 */
spmi_update(5, 0x26aa, 0x10, 0x10);                           /* 中断输出使能 */
spmi_update(5, 0x268b, 0x10, 0x00);                           /* 关数字 moisture */
spmi_write8(5, 0x268c, 0x67);
spmi_write8(5, 0x26ae, 0x01);
/* mic_vol = 8 的档位编码（走 codec 模拟前端，缺了麦克风会没偏置） */
spmi_update(5, 0x2529, 0x70, 0x00);  spmi_update(5, 0x2529, 0x04, 0x04);
spmi_update(5, 0x252a, 0x30, 0x00);  spmi_update(5, 0x252a, 0x30, 0x30);

/* --- 2. 复位脉冲 + 去抖 + MICBIAS（accdet_init） --- */
spmi_update(5, 0x268a, 0x02, 0x02);  delay_us(2500);
spmi_update(5, 0x268a, 0x02, 0x00);  delay_us(1250);
spmi_write8(5, 0x2697, 0x00);  spmi_write8(5, 0x2698, 0x08);  /* 插入去抖 0x0800 */
spmi_write8(5, 0x2699, 0x00);  spmi_write8(5, 0x269a, 0x08);
spmi_write8(5, 0x269d, 0x20);  spmi_write8(5, 0x269e, 0x00);
spmi_write8(5, 0x269f, 0x44);  spmi_write8(5, 0x26a0, 0x00);
spmi_update(5, 0x26a3, 0xf0, 0x30);                           /* mic 偏置档 */
spmi_write8(5, 0x26a5, 0x0e);                                 /* MICBIAS 电压档 */

/* --- 3. 收尾（late_init 的两笔，不能省：真机就在这两位） --- */
spmi_update(5, 0x26a3, 0x0f, 0x05);
spmi_update(5, 0x26a4, 0xf0, 0x50);
spmi_update(5, 0x268a, 0x40, 0x40);                           /* bit6 必须置 */
```

写完自检（应与真机未插状态一致）：
`0x268a=0x40 0x268b=0x00 0x268c=0x67 0x2694=0x41 0x2697..0x26a0=00/08/00/08/20/00/44/00 0x26a3=0x35 0x26a5=0x0e 0x26aa=0x10 0x26ab=0x27 0x26ae=0x01 0x2529=0x04 0x252a=0x30`

`0x269b/0x269c`、`0x2695/0x2696`、`0x26a1/0x26a2`、`0x26a6/0x26a7`、`0x26b9/0x26ba/0x26bb`、`0x26bd..0x26c0` 代码里零引用，
**不要写它们**，只读基线、必要时 `update_bits` 改单个位。

### 6.2 判插拔：只读 `0x26b6` 的 bit[7:6]

```c
for (;;) {
    u8 v = spmi_read8(5, 0x26b6);
    u8 ab = (v >> 6) & 0x3;
    switch (ab) {
    case 3: /* 未插 */                     break;
    case 0: /* 4-pole? → cable_type = 2 */ break;
    case 1: /* 3-pole? → cable_type = 1 */ break;
    case 2: /* 驱动判为 Invalid AB，忽略 */ break;
    }
    delay_ms(100);                 /* 去抖：驱动用 0x2697=0x800 计数 + 10~12ms 二次确认 */
}
```

`ab` 变化后再读一次确认（驱动的去抖是硬件计数 + `usleep_range(10000,12000)` 后复读 `0x26a9`），
避免插头半插入时抖动。`0x26b8` 的 bit[7:6] 与 `0x26b6` 同义（IRQ 上下文用），两个都读过再下结论更稳。

### 6.3 要走中断（可选）

1. ACL 里把 SPMI 子中断 97（ACCDET_IRQ）、98（EINT0）打开。
2. 中断里：
   ```c
   u8 st = spmi_read8(5, 0x26a9);
   if (st & 0x01) {                       /* ACCDET_IRQ pending */
       spmi_update(5, 0x26aa, 0x01, 0x01);/* ACK：写 1 */
       spmi_update(5, 0x26aa, 0x01, 0x00);/* 再写 0 */
   }
   if (st & 0x04) {                       /* EINT0 pending */
       poll_until(!(spmi_read8(5,0x26a9) & 0x04), 400us);
       spmi_update(5, 0x26aa, 0x04, 0x04);
       spmi_update(5, 0x26aa, 0x04, 0x00);
       spmi_update(5, 0x2323, 0x04, 0x04);/* PMIC EINT 控制器同步清 */
       spmi_update(5, 0x2323, 0x04, 0x00);
   }
   ev = spmi_read8(5, 0x26b6);            /* 用 AB 判类型 */
   ```
3. `0x26aa` 的位语义：bit0/2/3 = IRQ/EINT0/EINT1 清除，bit4 = 中断输出使能（§1.4）。

### 6.4 判按键（可选）

插好后每 ~10 ms 读 `0x26b6`，并读 `0x26a9` 确认 `(st & 0x0c) == 0`（无 EINT pending），
再用 auxadc 电压（减 efuse 偏移 `[0x16c]`）按 §4 表映射（三键 CDD 阈值 121/192/600，单位 mV）：

```
cur >= 600        -> key 0（无键）
192 <= cur < 600  -> key 4  (jack 0x2000 -> 258 BTN_2)
121 <= cur < 192  -> key 1  (jack 0x1000 -> 257 BTN_1)
cur < 121         -> key 2  (jack 0x4000 -> 226 KEY_MEDIA)
```

四键机型把 caps bit9 置起来后阈值换成 58/121/192/400，映射见 §4。

---

## 7. 未确定清单（不要当结论用）

| 项 | 状态 | 原因 / 下一步 |
|---|---|---|
| `0x26b6[7:6]` 的 0/1 到底哪个是 3-pole | **待实测** | 只能从 `send_status_event` 的日志字符串（"HEADPHONE(3-pole)" ↔ cable_type 2 ↔ AB 0）反推；无耳机无法验证。**插一次耳机读 `0x26b6` 即可定案** |
| `0x26a4` 低 4 位 `0xe` | **来源未知** | 全模块无写入路径（唯一写点在 `accdet_init+0x188` @0x950 的 `moisture_detect_enable==1` 分支里，本机跳过）。推测 LK 或硬件复位默认值 |
| `0x26b1=0x10`、`0x26b3=0x27`、`0x26bd=0x70`、`0x26c2=0x20`、`0x26c8..0x26cb`、`0x26cc=0x6f`、`0x26ce=0x38` | **来源未知** | 代码从未写过 → LK/硬件默认。裸机重实现时**先读再改**，不要假设为 0 |
| `0x26a6=0xbf`、`0x26a7=0x0a`、`0x26a1=0x04` | **来源未知** | 同上 |
| `0x26a6/0x26a7/0x26a1/0x26a2/0x2695/0x2696/0x26b9/0x26ba/0x26bb` | **未确定** | 代码零引用，仅 `dump_register` 可见 |
| `0x269b/0x269c`（去抖档 2） | **未确定** | 只有 `accdet_set_debounce(2, v)` 会写，但全模块找不到 `set_debounce` 的调用者（无 `bl` 指向 0x1bd4）→ 死代码 |
| `mux_select` | **不存在** | 符号表无此变量 |
| `0x2319` 的 toggle 是否真执行 | **不可判定** | 置位后立刻清位，真机终值 `0x00` 无法区分「执行过」与「没写」 |
| `0x248c` / `0x2531` / `0x2534` / `0x2533` 的实际终值 | **未验证** | 本模块确实写了，但真机 `dump_reg` 只覆盖 `0x2508..0x2533`，`0x248c` 不在 dump 范围内；`0x2531`/`0x2533`/`0x2534` 在范围内但被 codec 侧后续覆盖的可能性无法排除 |
| AB=2 的配件 | **未确定** | 驱动直接打 "Invalid AB. Do nothing"（`@0x4f90`），无分支 |
| `0x26c9=0x1f`、`0x26cb=0x1f` 等尾部寄存器 | **未确定** | 完全无引用 |

---

## 8. 证据索引（按地址）

| 主题 | 地址 |
|---|---|
| DT 结构体起点 / 大小 | `readelf: accdet_dts @ .bss+0x98, size 0x8c` |
| `headset-mode-setting` → `0xa8`，`cust_pwm_deb = .bss+0xa8` | `0x3b5c`（读）、`0x3b78-0x3b90`（拷）、`0x3bb8`（指针） |
| 三键 CDD 阈值表 | `0x3d78`（选表）、`0x3df0-0x3e04`（落 `0xe4/0xe8/0xec`） |
| 四键阈值表 | `0x3d90`、`0x3dc0-0x3dc8`（落 `0xf0..0xfc`） |
| `caps` 组装 | `0x3c04`(bit0) / `0x3c58`(bit2..4) / `0x3cb0`(bit5/6) / `0x3d04`(bit8..10) |
| `accdet_init` 全序列 | `0x7e8`–`0x984` |
| `accdet_init_once` 全序列 | `0x9b0`–`0x15e8` |
| `0x2694` 计算 | `0x1120`–`0x1138` |
| `0x26ab` 三选一 | `0x1148`–`0x117c` |
| `0x26aa` bit4 | `0x11fc` |
| `0x268b` 置/清 | `0x1348` / `0x14e4` |
| `0x268c=0x67` | `0x1508` |
| `0x268a` bit6 | `0x15cc` |
| `accdet_irq_handle` 的 caps→`0x26a9` 位选择 | `0x1e08`–`0x1e30` |
| `0x26aa` bit0 ACK / 清 | `0x1e88` / `0x1f14` |
| `0x26b6` / `0x26b8` 读 + AB 提取 | `0x1e5c` / `0x1e70` / `0x1f78`–`0x1f88` |
| AB 四路分派（IRQ 侧） | 表 `.rodata+0x0f`，基址 `0x1fb4` |
| `0x26a3` 低 4 位 = `sms[10]` | `0x2000`（IRQ）、`0x728`（late_init） |
| `0x26a4` 高 4 位 = `sms[13]<<4` | `0x2034` / `0x748` / `0x4bcc` / `0x5450` |
| `0x269d` 运行时改写 | `0x2200` / `0x224c` / `0x24a0` |
| `cur_voltage` 计算 | `0x2534`–`0x2568` |
| `accdet_work_callback` 状态机 | `0x4ae8`–`0x4dec` |
| `multi_key_detection` | `0x6a48`–`0x6bd4` |
| `send_key_event` 跳表 | 表 `.rodata+0x1c`，基址 `0x668c` |
| `snd_jack_set_key` 绑定 | `0x1834` / `0x1848` / `0x185c` / `0x1870` |
| `clear_accdet_eint_check` | `0x2864`–`0x29a0` |
| `disable_accdet` | `0x6494`–`0x6620` |
| 中断注册 | `0x4254`–`0x4368` |
| `dump_register` 扫的范围 | `0x2688`–`0x26cf`（`0x2f94`+循环 `0x2f9c`，上界 `0x26ce`）；`0x2508`–`0x2533`（`0x305c`+循环 `0x3064`，上界 `0x2534`）；单点 `0x230c/0x2319/0x231d/0x2320/0x2323/0x2529/0x252d/0x11c0/0x258c`；第二段 `0x26ad..0x26c0`、`0x2527..0x2531` |
