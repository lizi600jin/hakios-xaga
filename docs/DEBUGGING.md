# 从一台没有串口的手机上把日志拿出来

`xaga` 的 AP UART 没有引出，ramoops 区域被 Android 占着，pstore 一直是空的。内核出问题的时候屏幕上只有一页字，翻不了页，也没法复制。

下面这几套东西是绕开这个限制的办法。都在真机上跑通了。

## logbook：把日志写进自己的分区

`mtkloader` 在 `xnusys` 分区尾部留了 8 MiB 给日志。kernel 交给 loader 的控制权回来之后（`loader_return_after` 到期、或者内核 panic），loader 把 DRAM 里的内核日志头部加正文写进这块区域，然后回到 Android 或者停住。

头部 72 字节：

```c
struct logbook_hdr {
	uint32_t magic;      /* "HLOG" = 0x474f4c48 */
	uint32_t version;
	uint64_t len;        /* 正文长度 */
	uint64_t reason;
	uint64_t code;
	uint64_t elr;
	uint64_t far;
	uint64_t ticks;
	uint64_t where;
	uint64_t sum;
};
```

取出来：

```bash
HAKIOS_SERIAL=$S sh mtk/tools/xaga-pull-log.sh
# → xaga-logbook.bin（4198400 字节：72 字节头 + 4 MiB 正文上限）
```

解析：

```python
import struct

data = open('xaga-logbook.bin', 'rb').read()
head = struct.unpack('<IIQQQQQQQQ', data[:72])
magic, version, length = head[0], head[1], head[2]
assert magic == 0x474f4c48, "not a logbook"

lines = [l.decode('utf-8', 'replace') for l in data[72:72 + length].split(b'\n')]
```

**头部是 72 字节，不是 64。** 按 64 解析会 `IndexError`，这个坑很浪费时间。

也不能用 `strings` 直接扫 —— 分区里留着上一次运行的内容，`strings` 会把新旧混在一起。必须按头部里的 `len` 截断。

### 为什么会丢日志

正文是个 4 MiB 的环形缓冲。任何每秒打几百行的驱动都能在十几秒内把前面的输出全部顶掉。

触控驱动调试的时候就是这样：主循环每 200 ms 读一次 65 字节缓冲，每次都 `printf` 整个缓冲，一秒几百行，18 秒之后 USB 的初始化输出一行不剩。

要读某个驱动的早期输出，先把话多的驱动从 boot-args 里去掉：

```c
/* mtk/mt6895/loader/board.h 的 XNU_EXTRA_ARGS */
"cpus=4 usb=2"                                  /* 只看 USB */
"cpus=4 touchscreen=3 keylog=2 touch_raw=1"     /* 只看触控 */
```

### 抓屏也吃缓冲

`fbshot` 一次抓屏会写 543 行。同一轮里先抓屏再看日志，早期内容就没了。

## 内核跑完之后把日志交回来

`xnu/files/osfmk/arm64/loader_return.c:464`：

```c
if (!PE_parse_boot_argn("loader_return_after", &seconds, sizeof(seconds)) || seconds == 0) {
	return;
}
```

boot-arg 给非零值，XNU 跑够那么多秒就把控制权交回 loader，loader 写 logbook 再 `chain_android()` 回 Android —— 一次刷机就能自动完成「跑 → 收日志 → 回 Android」，不用手动救砖。

开发时用 `-DXNU_RETURN_AFTER='"70"'`，一轮 113 秒。给用户发的版本必须写 `'"0"'`，否则机器会自己重启回 Android，看着像崩溃。

## 看门狗快照：内核彻底死掉时

XNU 挂死的话 `loader_return_after` 那条路走不成，日志还在 DRAM 里但没人来取。第二核的看门狗负责这一份：

`XNU_WATCH_SECONDS` 到期后，看门狗线程把最近的文本通过 `dbg_tee` 写进 DRAM 的 `SNAP_PA 0x90000100`（`SNAP_SIZE 0x100000`，魔数 `"SNAP"`），然后尝试复位。下一轮 loader 启动时 `snapshot_take()` 把它取出来写进 logbook 并上屏。

**看门狗刻意不碰 UFS** —— XNU 已经动过控制器，复位后状态不可信。DRAM 是唯一能跨复位的东西，所以快照走 DRAM。

## 屏幕：一页大字报告

`report_kernel_log(pa, len)` 把内核日志按 45×51 字符分页画到 framebuffer，`XNU_REPORT_DWELL_MS 20000` 一页，`XNU_REPORT_MAX_KB 6` 封顶，`report_park` 控制最后一页是否永久停在屏上。

`BOARD_REPORT_PAGES=0` 关掉整段，`BOARD_REPORT_SYSTEM_IMAGE=0` 关掉跳转前那张信息屏 —— 给用户发的版本两个都关。

## 屏幕上验证某一步到底跑到没有

内核挂了看不到日志、loader 里又不能 `printf` 的时候，用纯像素色带：

```c
#define STEP_BAND(n, colour) fb_rect(0, 1000 + (n) * 95, PANEL_W, 90, (colour))
```

`mtkloader` 每越过一个阶段画一条满屏宽的色带。重启一次看屏幕上有几条，就知道走到哪。比什么都可靠。

有两条经验：

- 拍屏幕判读不要数超过 8 个色块，摩尔纹会把相邻的糊在一起。一次只判单一颜色的一块。
- 不要选窄行文字，选大面积纯色或者明确的几何量（比如条宽占屏宽的百分比）。

文字路径（`out()` → `dbg_putc` → `plog_putc` + `fbcon_putc`）在早期阶段自己就可能挂，所以探针要用 `fb_rect` 而不是 `printf`。

## 主机侧

刷机的瞬间主机 `dmesg` 会看到两条：

```
usb 3-2: New USB device found, idVendor=18d1, idProduct=d00d ... Product: Android / Manufacturer: MediaTek
usb 3-2: USB disconnect, device number NN
```

第一条是 loader 自己的 fastboot，第二条是 loader 交出控制权、D+ 掉下来。之后整个 iOS 窗口如果零事件，说明设备侧根本没拉起 D+，不用再去翻内核日志。

## Android 侧

工作态寄存器可以从 debugfs 抄，这是对寄存器做对照的基准：

```bash
adb shell 'su -c "mount -t debugfs none /sys/kernel/debug"'
adb shell 'su -c "cat /sys/kernel/debug/usb/11201000.usb0/regs/reg-dev"'
# 另有 reg-csr、reg-ippc
```

`/dev/mem`、`/dev/kmem`、`/dev/port` 在这台机上都不存在，不要指望从 Android 直接读写物理地址。

拉二进制文件不要用 `adb shell ... > file`，`adb shell` 是 pty，二进制会被改坏。手机上先 `dd of=/data/local/tmp/x`，再 `adb pull`。
