# xaga 的输入：触控、按键，以及还不能用的 USB

这一页只写 xaga（Redmi Note 11T Pro，MT6895）。两台参考板（x6826b 的 MT6768、
redwood 的高通）在输入上每一处都不一样，所以下面每条都标注了出处，能拿真机二进制核对的
都核对过了，不靠推测。

## 一、现状

进入 iOS 桌面这一步已经打通。桌面能画出来、SpringBoard 能跑、`36 checks, 0 failed`。
进去之后发现三样东西不响应：

| 部件 | 状态 | 驱动 | boot-arg |
|---|---|---|---|
| 触控 | 已写，待真机验证 | `mtk/mt6895/drivers/input/hakios.nvt.c` | `touchscreen=3` |
| 音量 / 电源键 | 已写第二版，待真机验证 | `mtk/mt6895/drivers/input/hakios.mt6895_keys.c` | `buttons=2` |
| USB（含外接鼠标） | **没有实现** | 无 | —— |

USB 是空的：`find xnu/files -ipath "*usb*"` 零命中，内核里连 USB 栈的骨架都没有。
这一项不是「配错了开关」，是要从 MTU3 / xHCI 的设备栈写起。

## 二、触控：Novatek NT36672E

### 硬件在哪

从 `work-xaga/dump/xaga_live.dts` 和 `kx/arch/arm64/boot/dts/vendor/mediatek/xaga-mt6895-touch.dtsi`：

```
spi2 @ 0x11012000   compatible "mediatek,mt6983-spi"
  novatek@0         compatible "novatek,NVT-ts-spi"
                    spi-max-frequency   13000000
                    novatek,irq-gpio    <&pio 135 0x2001>
                    novatek,swrst-n8-addr 0x03F0FE
                    novatek,spi-rd-fast-addr 0x03F310
```

引脚（全部 `SPI2_A_*` 复用）：

| 信号 | GPIO |
|---|---|
| MI（MISO） | 109 |
| CS | 110 |
| MO（MOSI） | 111 |
| CLK | 112 |
| INT | 135 |

控制器是 **NT36672E**：固件名 `novatek_nt36672e_l16_fw01.bin`（Tianma，tp-vendor `0x46`）
与 `fw02.bin`（CSOT，`0x53`）。本机面板 cmdline 是 `panel-l16-42-02-0a-dsc-vdo`，属 Tianma 批次。

芯片跑自己 flash 里的固件，**上电就能报点，不需要下发固件**。

### 为什么用 GPIO 手推而不是 SPI 控制器

MT6895 的 `SPI2_A_*` 引脚功能号在任何可得头文件里都只以宏名出现
（`PINMUX_GPIO110__FUNC_SPI2_A_CSB`），没有数字。当 GPIO 用不需要那个数字。
代价是速率：66 字节按 100 kHz 推不到 1 ms，而报点周期是 8 ms 量级，够。

### 协议（四处关键点，全部由真机 `nvt36672c.ko` 反汇编确认）

模块在 `work-xaga/dt-device_xiaomi_xaga-TWRP/recovery/root/lib/modules/nvt36672c.ko`
（380528 B，未 strip）。反汇编存 `/tmp/nvt.txt`。

1. **一次读的总线字节数 = 报告长度 + 2。**
   仓库里的 Linux 源码 `CTP_SPI_READ(buf, POINT_DATA_LEN + 1)` 传的是 66，模块内部
   `t.len = len + DUMMY_BYTES`（`DUMMY_BYTES 1`）⇒ 总线上 67 字节；返回后
   `memcpy(dst + 1, rbuf + 2, len - 1)`，报告落在 `rbuf[2..]`。
   驱动里对应 `spi_xfer(out, in, len + 2)`。

2. **校验和覆盖报告的第 0..63 字节，比对第 64 字节。**
   模块 `nvt_ts_work_func` 的循环：`i = 1`，`sum += buf[i]`，`cmp x8, #0x40` 相等才退出
   ⇒ 累加 `buf[1..0x40]`，校验字节取 `buf[0x41]`。`buf[0]` 是地址字节
   （`CTP_SPI_READ` 里 `and w8, w8, #0x7f; strb w8, [x19]` 写回），整体前移一格就得到
   上面的表述。

3. **第 i 指从报告的第 `6*i` 字节起。**
   模块里 `position = 1 + 6*i` 是 `buf` 坐标，`buf[0]` 是地址 ⇒ 换算过来是 `6*i`。
   每指 6 字节：`[0] = (id << 3) | state`（1 = down，2 = move），
   `x = (pd[1] << 4) | (pd[3] >> 4)`，`y = (pd[2] << 4) | (pd[3] & 0x0f)`，`[4]` 压力。

4. **INT 是高电平有效。**
   `nt36xxx.h:53 #define INT_TRIGGER_TYPE IRQ_TYPE_EDGE_RISING`。空闲时是低，
   固件跑起来有数据才拉高。

### page 必须锁存一次

`nvt_ts_work_func` 读点数据**前不调 `nvt_set_page`**：page 在 probe 时锁存一次，
之后读地址 0 就等于读 `EVENT_BUF_ADDR`。驱动里线程启动时 `nvt_set_page(page)` 一次，
之后 `nvt_read(0, data, POINT_DATA_LEN)`。

默认 `NVT_EVENT_BUF_ADDR 0x21C00`，来自 NT36672A 的 map（`nt36xxx_mem_map.h:127`）。
本机是 NT36672E，`trim_id_table` 里没有 E 的条目，所以做成 boot-arg `nvt_page` 可覆盖。
page 机制自洽：`0x21C00 & ~0x7f == 0x21C00`，`0x21C00 | 0x78 == 0x21C78` 同 page。

### GPIO 寄存器布局（真机 `pinctrl-mt6895.ko` 的 reg_cal 表逐字确认）

模块 `.rodata` 里有连续表：`0x6438`（DIR 组）、`0x64d8`（DOUT 组）、`0x6578`（DIN 组）、
`0x61f4`（MODE 组，`bitstart=0 len=4`）。表项 20 字节，偏移在 `+8`：

```
DIR   0x000 + n*0x10     n = pin / 32
DOUT  0x100 + n*0x10     实际写入用 +0x104 set / +0x108 clr
DIN   0x200 + n*0x10
MODE  0x300 + (pin / 8)*0x10，脚内偏移 (pin % 8) * 4 位，4 位宽
```

xaga 用的五个脚落在 `0x3d0`（109/110/111）、`0x3e0`（112）、`0x400`（135）。

**这一条踩过坑**：仓库里 MT6768 的参考实现（`mtk/mt6768/drivers/input/hakios.tcm.c`）
把 DIR/DOUT 写成固定的 `0x004/0x104/0x200`，因为它用的引脚 25-28 全在第 0 组。
xaga 的 109-135 不在第 0 组，照抄会操作错寄存器。

### 一次启动就能判断是没电还是协议错

驱动在锁存 page 之后打印：

```
nvt: at rest GPIO135 is 1; the input register of its group reads 0x...,
     the group of the bus pins reads 0x...
```

INT 空闲应该是 1。一直是 0 说明芯片没出复位，读总线不会有意义——先查供电
（`mt6368_vtp`，`/spmi@1c804000/second_pmic@5/regulators/VTP`，由 LK 打开），
而不是改协议。

## 三、按键：SoC keypad + PMIC

### 两套硬件，不是一套

| 键 | 走哪 | 依据 |
|---|---|---|
| 音量下 | SoC keypad 行 0 | `mt6895.dts:2053` 的 `hw-init-map = <114 0 0 ...>` |
| 音量上 | SoC keypad 行 17 | `cust/xaga.dtsi:381-390` 的 `mediatek,hw-rstkey = <17>` |
| 电源 | SoC keypad 行 8 **和** MT6363 | `mediatek,hw-pwrkey = <8>`；`mt6363.dtsi:108-122` 也有 `power { linux,keycodes = <116> }` |

`xaga_live.dts` 里 dump 出来的 keypad 只剩 `hw-init-map = <0x72>`（114），
是因为那一份是 bootloader 打过补丁的形态，`hw-pwrkey` 之类的属性没写进去。
源头的 `cust/xaga.dtsi` 才是完整的。

### keypad 寄存器布局（真机 `mtk-kpd.ko` 反汇编确认）

模块在 `work-xaga/dt-device_xiaomi_xaga-TWRP/recovery/root/lib/modules/mtk-kpd.ko`
（26400 B，未 strip）。反汇编存 `/tmp/kpd.txt`。

**读状态**（`kpd_get_keymap_state @0x930`）：用 **`ldrh`（16 位）** 依次读
`base+0x04`、`+0x08`、`+0x0c`、`+0x10`、`+0x14`，写进 uint16 数组，每读之前 `dmb oshld`。

⇒ 5 个 16 位寄存器，起始 `+0x04`，步长 4，共 80 位（与 `hw-map-num = 0x48` = 72 吻合）。

**极性**（`kpd_keymap_handler`）：

```
7bc: lsl  w8, w27, w21        /* 1 << bit */
7d4: ands w23, w8, w28        /* 新值里这一位是 1 吗 */
810: cset w3, eq              /* eq ⇒ 新值为 0 ⇒ pressed */
```

⇒ **位为 0 = 按下，位为 1 = 松开**。索引 `index = 寄存器号 * 16 + bit`，上限 71。

**上电必做的两个写**（`kpd_pdrv_probe`）：

```
380: add x22, x9, #0x20      /* base+0x20，置 bit0 = 使能 */
388: strh w21, [x22]
3e0: add x22, x9, #0x18      /* base+0x18，bits 13:0 = debounce */
3e8: strh w23, [x22]
```

不做这两个，keypad 不工作。

**中断不用清**（`kpd_irq_handler @0x8a0`）：只做原子置位 + `tasklet_schedule`，
不写硬件清中断 ⇒ 中断是电平/自动清除。驱动用轮询，不需要中断。

### 板级注意

`work-xaga/dump/iomem.txt` 的 `1c0` 段只有 `1c001000.power-controller`、
`1c007000.watchdog`、`1c00f000.dvfsrc`、`1c013000.syscon` —— **`0x1c00e000` 没出现**。
DTS 里有节点、ko 也存在，所以它是能用的，只是 Android 起来后那个映射被回收了。
loader 阶段（MMU 关闭）访问它要小心总线错误：驱动在读之前先读一遍五个寄存器，
若全 `0xffff` 或全 0 就打印并退出，不去写使能位——因为没有异常向量，总线 abort 会挂死整机。

## 四、USB：还没有

内核里没有任何 USB 代码。要做的话，可参考的真机模块都在同一个 TWRP 目录：

```
mtu3.ko            372560 B   设备控制器
xhci-mtk-hcd.ko     65232 B   主机控制器
extcon-mtk-usb.ko             线缆检测
usb_dp_selector.ko            DisplayPort 切换
```

`mtu3.ko` 是 Linux 的 DRD 驱动，寄存器序列可以直接对照；但 xHCI 那一层要自己写。
这一项工作量远大于触控和按键。

## 五、构建与验证

```bash
cd /home/lizi/IOS/hakios-mtk && source xnu-env.sh

# 内核（必须 LC_ALL=C，否则 tools/make-kernel.sh 的失败判定被中文 locale 卡住）
LC_ALL=C make kernel DEVICE=xaga

# loader 镜像
DEVICE=xaga ./mtk/loader/build.sh -DBOARD_NO_UART -DBOARD_NO_OVL -DBOARD_SKIP_MENU \
  -DBOARD_NO_CONNSYS -DXNU_CORES=4 -DXNU_VERBOSE=0 -DXNU_DISPLAY=1 \
  -DXNU_WATCH_SECONDS=0 -DXNU_RETURN_AFTER='"180"'
make payload DEVICE=xaga
python3 mtk/loader/build_flashed.py

# 刷
fastboot -s GAJZ4HIN99AQ69Y9 flash boot_a xnu-flashed-slot_a.img
fastboot -s GAJZ4HIN99AQ69Y9 reboot
```

boot-arg 在 `mtk/mt6895/loader/board.h` 的 `XNU_EXTRA_ARGS`，当前是

```
cpus=4 touchscreen=3 buttons=2 keylog=2 touch_raw=1
```

`CONFIG_BOOTARGS` **不进 loader**：`mtk/loader/build.sh` 完全不读 `.config`。
loader 用的是 `mtkloader.c` 里的 `XNU_BOOT_ARGS`。要改运行参数只能改 `board.h`。

`keylog=2` 只为首批启动用，寄存器偏移定准后应当删掉——logbook 只有 4 MB，
和内核日志共享，每 60 秒 dump 16 个寄存器会在一轮启动内把日志填满。

取日志：

```bash
sh mtk/tools/xaga-pull-log.sh        # → xaga-logbook.bin
```

loader 跑完会自动起 Android（`BOARD_RETURN_TO_ANDROID 1`），所以 adb 会回来，
不用手动复位。这个板的 `reset_board()` 两条路都无效——PSCI system reset
（`smc #0 0x84000009`）返回但不动作，裸 `TOPRGU_SWRST` 也被忽略。唯一实测能把
这块板子复位的是**带 LENGTH 编程的看门狗**：

```c
wr32(TOPRGU_LENGTH, ((seconds * 32768u / 512u) << 5) | 0x8u);
wr32(TOPRGU_RESTART, TOPRGU_RESTART_KEY);
wr32(TOPRGU_MODE, TOPRGU_MODE_KEY | 0x1u);
```

不给 `TOPRGU_LENGTH` 编程，`RESTART` 和 `SWRST` 都是空写。
