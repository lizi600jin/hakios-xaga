# 无线：从 CONN 电源域到 MT6637 A-die

这份文档记录 MT6895 的无线链路在真机上走到了哪一步、每一步的实测值，以及在它之前
必须知道的那几个坑。全部数字来自真机日志，不是推断。

结论先说：**CONN 电源域已经能安全上电，D-die 的 POS Part 3 五步全部通过，A-die 的
MT6637E2 芯片 ID 已经读到 `0x66378a01`。** 蓝牙和 Wi-Fi 还没起来——那需要载入固件，
是下一步的事。

---

## 一、硬件身份（实测确认）

| 部件 | 地址 / 总线 | 实测值 |
|---|---|---|
| D-die CONNINFRA | `0x18011000` `CONN_CFG_IP_VERSION` | `0x02050100` |
| A-die 射频 | RFSPI `0x18042000` 读 A-die `0x02C` | **`0x66378a01`（MT6637E2）** |
| 参考时钟 | `MT6685` SID 9 `0x07F4` bit0 | `0x06` → **26 MHz** |
| 主 PMIC | `MT6363` SID 4 `HWCID 0x08/0x09` | `0x30 / 0x63` |
| 副 PMIC | `MT6368` SID 5 `HWCID 0x08/0x09` | `0x30 / 0x68` |

### 无线 LDO（地址取自实机 `mt6363-regulator.ko` 的 `mt6363_regulators[]`）

| 名称 | 电压寄存器 | 值 | 使能寄存器 | 值 | 实际回读 |
|---|---|---|---|---|---|
| `VRFIO18` | `0x1F2C` (VOSEL) | `0x0B` = 1.70 V | `0x1BCD` (CON0) | `0x01` | `MON 0x1BD1 = 0x03` |
| `VCN13` | `0x1B3F` (VOSEL) | `0x04` = 1.30 V | `0x1D07` (CON0) | `0x01` | `MON 0x1D0B = 0x03` |
| `VCN13` 细调 | `0x1F88` (VOCAL) | `0x05` = +50 mV → **1.35 V** | — | — | 同上 |

**写入顺序是电压在前、使能在后，每一步都回读校验。** 回读不符就中止，不继续往下走——
这一层保护在 `hakios.mt6895_connsys.c` 的 `connsys_pmic_write_verify()` 里。

`VCN33_1` / `VCN33_2`（3.3 V RF PA）**本轮没有开**。它们要等 BGF/WF 子系统启动时才需要。

---

## 二、CONN 电源域上电

`0x1C001E04`（`SPM + 0x0E04`，`CONN_PWR_CON`）走标准 MTCMOS 序列：

```
PWR_ON (bit2)      → 等 PWR_ACK      (bit30) == 1
PWR_ON_2ND (bit3)  → 等 PWR_ACK_2ND  (bit31) == 1
CLK_DIS (bit4) = 0 → delay 1 µs
ISO (bit1) = 0
RST_B (bit0) = 1   → delay 500 µs
```

实测：`0x00000012 → 0xc000000d`，`ack1=1 ack2=1`，耗时 0.6 ms。

`MD1_PWR_CON`（`0x0E00`）读回 `0x4000000d`，说明 MD1 域是 LK 开着留下的。MD1 与本路的
Wi-Fi / 蓝牙无关，**不要去动它**。

`MD1_SEQ` 在 `0x1C803000`（不是 `0x0D124000`——那个地址是 L2 SRAM，读出来的
`0x3358ae1a` / `0x92b6bdcc` 是不像寄存器的噪声）。

### INFRACFG 保护

`0x10001000` 的四条 `bus_prot` 撤除后：`rx_ok=1 tx_ok=1`，
`ifr1 rdy 0xFBF81080 → 0xFBF80080`（bit12 归零）。

---

## 三、POS Part 3（D-die 底座）

五步，全部通过，每步都有读回校验：

| 步骤 | 内容 | 实测 |
|---|---|---|
| `pos3[1]` | EMI remap：`0x1804B354 = 0x08E50`（`0x8E500000 >> 16`，20 位字段） | MATCH；`wf=0x1000 bt=0x1E00 gps=0x1000 scp=0x1C60` |
| `pos3[2]` | SYSRAM `0x18050000` 清零 20 KiB；EMI path `0x04000000..0x043FFFFF`；D-die Efuse `0x18011020 → 0x18053820`；RGU 休眠门控 | `efuse 0x00010000 → 0x00010000`，`rgu=0/3/0/1` |
| `pos3[3]` | WT_SLP 路由表 `0x18003070..0x98`（11 个寄存器，逐位 RMW） | `0x70=0x0af40a04 … 0x98=0x000000f8` |
| `pos3[4]` | Subsys PLL（26 MHz） | `0xF4=0x03140521 0x04=0x00032057 0x0C=0x00068000` |
| `pos3[5]` | CONN_INFRA 时钟 + CMDBT 4 KiB | `fetch=0x00050200`，`w0/w128/w508` 全中，`ckgen_bus=0x00001100` |

**`0x18011050`（`CMDBT_FETCH_START_ADDR0`）是 20 位字段**（`CODA/conn_cfg.h:268`，
掩码 `0x000FFFFF`）。写 `0x18050200` 之后硬件锁存低 20 位，读回 `0x00050200` ——
这是正确的，校验时必须先掩码。

**`pos3[5]` 不置 `0x18001200` bit0（dsleep）**，让 CONN_INFRA 保持唤醒态，
否则后面的 A-die SPI 和 BGF 读会超时。

---

## 四、A-die 探测（MT6637）

1. **PMIC 供电**：`VRFIO18` 1.70 V + `VCN13` 1.35 V，两个 `MON` 都回到 `0x03`。
2. **pinmux 切 Aux1**：`GPIO_MODE20 (0x440)` mask `0x77700000` val `0x11100000`；
   `GPIO_MODE21 (0x450)` mask `0x77777777` val `0x11111111`。
   实测 `MODE20 0x00001100 → 0x11101100`，`MODE21 0x00000000 → 0x11111111`。
3. **strap**：`CONN_WF_CTRL2` 临时切 GPIO 输出，按 26 MHz 拉低；再打开
   `0x18003120` bit0 并等 bit1 归零；`0x18001010` bit0 = 1 释放 A-die 复位。
   实测 `WB_SLP_TOP_CK_0=0x00000011 (ok=1)`，`ADIE_CTL=0x0000000b`。
4. **RFSPI 信号量**：`0x18070000`，`RFSPI_INDEX = 12`。取锁读 `0x18072030` bit0，
   放锁写 `0x18072230 = 1`。
5. **读芯片 ID**：`0x18042050 = 0x0000B02C`（A-die 内部地址 `0x02C`），
   `0x18042054 = 0`，`0x18042058` 读回 **`0x66378a01`**。

**无论成功失败，RFSPI 信号量都要释放、`CONN_WF_CTRL2` 都要切回 CONN 模式。**

---

## 五、四个已经踩过的坑

### 1. 不要写 TOPCKGEN `0x158 / 0x154 / 0x00C`

LK 已经把 `CLK_CFG_20 (0x10000150)` 设成 `0x01020101`，其中 `bits[3:0] = 1`
就是 `ap2conn_host_sel = 1`（`mainpll_d7_d4`）。

再写一遍「先清 `0x158=0xF`、再置 `0x154=0x1`、再触发 `0x00C` bit18」是无意义操作，
而且 `UPDATE2` 的 bit18 会触发**整个 `CLK_CFG_20` 组**更新——那一位组里有八个 4 位 mux，
不只是 `bits[3:0]`。实测会让 NT36672E 触摸失效。

**XNU 不该动它。**

### 2. `connsys_delay_s=70` 必须保留

`nvt` 触摸驱动在开机 19.85 s 左右正用 GPIO109–112 做软件 bit-bang 的 SPI 初始化。
CONN 域上电只需 0.6 ms，但它紧跟着的 INFRACFG `delay(5000)` 合计约 5.6 ms，
正好压在这个窗口里，会让 NT36672E 的固件启动态变成 `a1`（正常是 `a0`），从此不再上报。

把整个上电动作推迟到 70 秒后（`nvt` 已完成固件烧录和基线），问题消失。

### 3. `0x184x` 读回 `0xdead0001` 不是内存毒化

那是 MT6895 CONNSYS 总线桥在 WFSYS 子域没上电时的超时返回码
（`wlan/core/gen4m/os/linux/hif/common/kal_pdma.c:153` 把它写作 `Host bus hang timeout`；
`soc7_0.c:2799` 里 `0xdead0001` / `0xdead0003` 是同一类特征码）。

映射本身是成功的，`0x180x` 总线正常应答。要等 `wfsys_power_on()` 之后 `0x184x` 才有真值。

### 4. NT36672E 事件区全 `0xFE` 是 WDT 复位信号，不是坏点

这颗芯片**没有 Flash，固件只存在于易失 SRAM**。一旦内部看门狗或 ESD 软复位，
SRAM 里的固件就丢了，芯片退回 ROM 态，并主动往事件区 `[0..5]` 填 `0xFE` 或 `0xFD`、
频繁脉冲 IRQ，催主机重新下固件。

Android 的 `nt36xxx.c`（`NVT36672C/nt36xxx.c:1413-1435`）看到连续 11 帧全 `0xFE`/`0xFD`
就会 `nvt_sw_reset_idle()`（往 `0x03F0FE` 写 `0xAA`）再重载固件，三秒内无感恢复。

`hakios.nvt.c` 原本只在开机下过一次固件，主循环里失败只做 `bad++` 和
`delay(200000)`——所以芯片只要复位一次就永远死在 `0xFE`，`bad` 每 2.05 秒涨 10。

补上 `nvt_wdt_fw_recovery()` 之后，boot-1034 实测触发 4 次、4 次全部
`attempt 1` 成功，每次约 3.3 秒；`reports` 从 144 一路涨到 1135，`bad` 停在 42 不动了。

---

## 六、当前的 boot-args

```
connsys_power=1 connsys_delay_s=70 connsys_readid=1 connsys_pos3=5 connsys_adie=1
wifi=2 bt=2 cell=2 consys_emi_pa=0xbd000000
```

| 参数 | 作用 |
|---|---|
| `connsys_power=1` | SPM CONN MTCMOS + INFRACFG 四条 `bus_prot` 清除 |
| `connsys_delay_s=70` | 整套上电推迟到第 70 秒，避开触摸初始化窗口 |
| `connsys_readid=1` | 读 `0x18011000` 与 39 个窗口；为 0 时只上电不读 |
| `connsys_pos3=0..5` | POS Part 3 逐级累加；5 是满级 |
| `connsys_adie=1` | PMIC 供电 + pinmux + RFSPI 读 MT6637 |
| `consys_emi_pa=0xbd000000` | 20 MiB Wi-Fi DMA 区，必须与出厂值一致，否则拒绝映射 |

`0x8E500000`（8 MiB）是另一块 EMI，给 CONNINFRA / BGF MCU / BT 固件共用，
写进 `0x1804B354`，**不要和 `0xBD000000` 弄混**。

---

## 七、下一步

**Step 2.4 — 蓝牙**

1. 载入**两个**固件，不是只一个：
   - `soc7_0_ram_mcu_1b_t_1_hdr.bin` — BGF N10 MCU 的 ROM patch
   - `soc7_0_ram_bt_1b_t_1_hdr.bin`（600,456 B）— BT 的 RAM code
   两个文件前 48 字节是 EMI header，要剥掉；剩余部分整体拷到
   `0x8E500000 + patch_emi_offset`，`patch_emi_offset` 从前 48 字节的
   `emi_addr[1..2]` 算出（`btmtk_mt66xx.c:46-55`）。
2. 补 `MT6368 VCN33_1`（3.3 V）、`connsys_a_die_cfg_PART1/2`、`connsys_afe_wbg_cal`，
   让 BGF 的射频和时钟就绪。
3. `bgfsys_power_on()` 十个步骤，三个里程碑：
   - `0x18810000`（`BGF_IP_VERSION`）== `0x02060000`
   - `0x18060C00` bit15 == 1（`CONNINFRA_READY`）
   - `0x1880016C` == `0x00001D1E`（MCU 进 idle loop）
4. 之后才能经 BTIF 发 WMT `FUNC_CTRL`（`01 6F FC 06 01 06 02 00 00 01`），
   等 `04 E4 05 02 06 01 00 00`。

**Wi-Fi** 还要更远：`wfsys_power_on()` + Wi-Fi 固件 + `wlan_drv_gen4m`
（需要 `bsp-rodin-v-oss` 的 `libgen4m_soc7_core.a` 补齐 XNU KAL）。

在 iOS 设置里，Wi-Fi 和蓝牙开关要等到这些接口注册成网络设备之后才会变成可点。

---

## 八、相关文件

```
mtk/mt6895/drivers/net/hakios.mt6895_connsys.c   CONN 上电 / POS Part 3 / A-die
mtk/mt6895/drivers/net/hakios.mt6895_bt.c       BTIF
mtk/mt6895/drivers/net/hakios.mt6895_cell.c     DPMAIF / en1
mtk/mt6895/drivers/net/hakios.mt6895_wireless.c 三者按序调用
mtk/mt6895/drivers/net/hakios_mt6895_connsys_nvdata.h  窗口表与 EMI 守卫
mtk/mt6895/drivers/input/hakios.nvt.c           WDT 恢复
tree/xnu/files/osfmk/arm/io_map.c               IO_MAP_SIZE 8 MB → 32 MB
tree/xnu/files/bsd/dev/hakios.input.c           驱动启动顺序（29 个调用）
```
