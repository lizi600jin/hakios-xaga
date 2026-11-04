# 板级常量是从哪来的

`mtk/mt6895/loader/board.h` 里每一个地址、每一个尺寸都有出处。这份文件记下它们分别来自哪里，免得后来的人以为是猜的，也免得有人改坏了不知道去哪对。

## 三个一手来源

### 1. 真机 dump 的 `lk_a.img`

`boot_a` 之外的 `lk_a` 是 LK（Little Kernel）本体，8 MB，未压缩，字符串表和设备树直接可读。

从里面拿到的：

- **fastboot 命令注册表** —— `getvar:`、`erase:`、`set_active:`、`continue`、`reboot-bootloader`、`reboot-fastboot`、`reboot-recovery`、`oem poweroff`、`oem p2u`、`download:`、`oem ultraflash`。`boot:` 出现 0 次。
- **同一份设备树** —— `lk_a.img` 里 offset `0x266450` 有一个 FDT，`totalsize 0x4fe8c`。这是本机最权威的板级数据。
- **UART / TOPRGU / GIC / 显示 / PMIC 的地址与参数** —— 都在那份设备树里。
- **MTU3 的字符串** —— `[USB] U2 pullup D%s`、`[USB] U3 pullup D%s`、`[USB][QMU] ...`。LK 自己就是一份完整的 MTU3 设备模式实现。

`lk_a.img` 的熵分析显示 `0x300000` 之后全是 0，所以字符串表是完整的，没有压缩段。

### 2. 官方 ROM 里的 `vendor_boot.dtb`

`xaga_images_OS2.0.12.0.ULOCNXM_14.0.zip` 里的 `vendor_boot.dtb`，md5 与 `lk_a.img` 里的 FDT、与真机 `/proc/device-tree` 三者**完全相同**。所以 `board.h` 里的数值不是从 LK 抄的二手货，是真实厂商数据。

官方 ROM 里那份 `lk.img` 与本机 `lk_a.img` 的 fastboot 命令表也完全一致，`boot:` 精确计数都是 0。

### 3. Android 工作态

手机跑 Android 时：

```bash
adb shell 'su -c "cat /proc/iomem"'        # 内存映射
adb shell 'su -c "cat /proc/cmdline"'      # 内核命令行
adb shell 'su -c "cat /proc/interrupts"'   # 中断
adb shell getprop                           # 各种 ro.boot.*
adb shell 'su -c "ls -l /dev/block/by-name"'  # 分区
```

USB 寄存器对照的基准：

```bash
adb shell 'su -c "mount -t debugfs none /sys/kernel/debug"'
adb shell 'su -c "cat /sys/kernel/debug/usb/11201000.usb0/regs/reg-dev"'
```

## 关键数值与出处

| `board.h` 里的项 | 值 | 出处 |
|---|---|---|
| `UART_BASE` | `0x11001000` | 设备树（mt6768 是 `0x11002000`，不能照抄） |
| `TOPRGU_BASE` | `0x1c007000` | `compatible = "mediatek,mt6895-wdt"`（mt6768 是 `0x10007000`） |
| `GICD_BASE` / `GICR_BASE` | `0x0c000000` / `0x0c040000` | 设备树 `gic-v3` 的 `reg`，size 分别是 `0x40000` / `0x200000` |
| timer 频率 | `13000000` | 设备树 `clock-frequency = <0xc65d40>` |
| CPU 拓扑 | `{0,4,5,7,1,2,3,6}`，小核 `<4` | 设备树：4×A55 在 `Aff1=0`（`cpu@000..003`），4×A78 在 `Aff1=1`（`cpu@100/101/102/200`） |
| `LK_FB_BASE` | `0xfe14e000` | `/proc/iomem` 的 framebuffer carve-out `0xfe14e000-0xffffefff` |
| `LK_FB_PITCH` | `4352` | 1080 × 4 |
| 面板 | `1080×2460`，`l16-42-02-0a-dsc-vdo` | `cmdline`（Tianma 批次） |
| ramoops | console `0x4811f000`、pmsg `0x48160000` | `cmdline` 的 `ramoops.mem_address=0x48090000 mem_size=0xe0000` 拆解 |
| `PANIC_LOG_BASE` | `0x80100000` | `/proc/iomem` 显示 `0x80100000-0x89ffffff` 是 159 MiB System RAM |
| 存储 | UFS `0x112b0000`，LUN 2 | 设备树 `ufshci@112b0000`，`compatible` 是 `mediatek,mt8183-ufshci` |
| 触控 | SPI2 `0x11012000`，MI/CS/MO/CLK = GPIO 109/110/111/112，INT = 135 | 设备树 `novatek,NVT-ts-spi` + pinmux |
| 按键 | keypad `0x1c00e000`，PMIC = MT6363 @ `spmi@1c804000` | 设备树 `mediatek,kp` + `mediatek,mt6363-keys` |
| USB | mac `0x11201000`、ippc `0x11203e00`、PHY `0x11e40000` | 设备树 `usb0@11201000` + `usb2-phy0@11e40000`；`/proc/iomem` 交叉确认这三个窗口都由 Android 内核实际映射 |

## 内存窗口

XNU 的物理窗口不能照抄上游。上游默认 `0xc0000000` 起、`0xfe000000` 长，在这台机上不成立。

`/proc/iomem` 的 `0xfd280000-0x23effffff` 这段大窗口里嵌着 10 个 reserved 子区，包括 framebuffer 的 `0xfe14e000-0xffffefff`（30 MiB）。把它们扣掉之后，唯一一块 ≥ 64 MiB 的连续空闲区是：

```
0xfffff000 - 0x21bb77fff     （4539 MiB）
```

`board.h` 用的是这一段。

## 分区大小

从官方 scatter（182 条分区）+ TWRP 的 `BoardConfig.mk` + 真机 `blockdev` 三方核对：

| 分区 | 大小 |
|---|---|
| `boot_a` / `boot_b` | 64 MiB |
| `vendor_boot_a` / `vendor_boot_b` | 64 MiB |
| `dtbo_a` | 32 MiB |
| `super` | 8.5 GiB |
| `userdata`（原始） | 106.48 GiB |
| `xnusys`（我们自己刻的） | 2.50 GiB |

`boot_a` 是 64 MiB 而不是上游写死的 32 MiB，`chainboot.py` 里的 `BOOT_PARTITION` 要放宽。

## 逻辑块大小

`xaga` 的 GPT 用 **4096 字节逻辑块**，不是 512。

- header 在 byte 4096，`mylba = 1`、`altlba = 31240191`、总 LBA `31240192`
- `31240192 × 4096 = 127959826432`，与 `blockdev --getsize64` 一字不差
- 备份 GPT 时，entry array 在 header **前面** 7 块

`gpt_find()` 一开始硬编码读 byte 512 验 `"EFI PART"`，在这台机上永远失败。后来加了 `gpt_detect()` 依次试 512 和 4096。

## 怎么核对自己手上的数字

改完 `board.h` 之后：

```bash
python3 tools/hakios-config.py check          # 39 个符号全绿
python3 tools/hakios-device.py check          # 4 个 repository 一致
```

反汇编核对常量用 `/usr/lib/llvm-21/bin/llvm-objdump`（PATH 里没有 `llvm-objdump`）。注意 ARM64 会把大常量拆成 `movz`/`movk` 几条指令，**连续字节搜不到是正常的**，要按指令序列看。

核对 loader 里的常量要反汇编 `mtk/loader/mtkloader.o`（ELF），`mtkloader.bin` 是 `--oformat binary` 出来的裸镜像，反不出符号。
