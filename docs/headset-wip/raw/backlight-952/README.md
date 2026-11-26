# 背光：寄存器读到了，但改它把屏幕弄黑了（boot-950…952）

2026-11-26。这一轮有收获也有代价，两样都记下来。

## 读到了什么

`boot-951`，`bl=1`，`disp_pwm0` 映射成功，整页只有四个字不是零：

```
backlight: disp_pwm0 mapped at 0x1100e000 (page 1024 words)
backlight:   +010 00000001 1
backlight:   +018 00000102 258
backlight:   +01c 000003ff 1023
backlight:   +030 40000000 1073741824
backlight: 4 of 1024 words are not zero
```

`+01c = 1023` 看起来最像占空比——设备树说 `max-hw-brightness = 0x7ff`（2047），
1023 正好是它的一半，而屏幕当时的亮度确实在中间。

`+030 = 0x40000000` 只可能是某些使能或分频位。
`+010 = 1` 和 `+018 = 258`（`0x102`）还没对应上。

## 然后我把屏幕弄黑了

用 `set 1c 64` 把 `+01c` 从 1023 改成 100 之后，设备进了不可用状态，
操作者要手动长按电源才回得来。

**这一次是盲写。** 那个字看起来像占空比，但同一页里还有三个非零字，
任何一个都可能是使能、分频、或者别的时钟相关的东西。改错了其中一个，
PWM 就停了，而这块屏的背光是它驱动的——屏幕黑了之后什么都看不见，
连日志都读不了（要在屏幕上做操作才能回 fastboot）。

## 正确的顺序应该是这样

**先确认、再写。** 确认的办法不需要写寄存器：

1. **在 Android 下改亮度，从内核侧看哪个字变了。**
   但 Android 的 `/dev/mem` 被 STRICT_DEVMEM 挡着（试过，`No such device or address`）。
2. **所以要在 XNU 里做**：boot 一次（只读 dump），
   然后**在 Android 里改亮度**，再 boot 一次（只读 dump），**diff 两份 dump**。
   变化的那一个字就是占空比。
3. 这样两次都是只读，没有黑屏风险。

代价是两次刷机，但比弄黑屏幕便宜得多。

## 另外三个字也应该查清

`+010`、`+018`、`+030` 是什么，最好也从 Android 侧对照：

- 改亮度前后 `+030` 变不变？变的话它可能是使能位。
- 屏幕熄屏再亮，哪些字跟着变？

## 这一轮真正的收获

**驱动本身是好的。** `bl=1` 时它映射、dump、建 `/dev/hakios-backlight` 全部正常，
读出来的数字是真实的硬件状态。失败的是「用这些数字做什么」这一步。

## 顺带修掉的一个真 bug

`hakios.stubs.c` 里 UFS 那五个 stub 被 `#if 0` 关着，注释写着「真实驱动在内核里」。
那只对写它的那个配置成立。任何一个不带 `CONFIG_HAKIOS_UFS` 的配置都拿不到
`hakios.ufs.c`，于是链接失败在 `_hakios_ufs_init`、`_hakios_ufs_dev`、
`_hakios_ufs_quiesce`、`_hakios_ufs_report`、`_hakios_var_save_on_return`。

改成和这个文件里其它 stub 一样的 `#ifndef HAKIOS_UFS`。

（另外：`CONFIG_HAKIOS_UFS` 不能打开。`hakios.mt6895_ufs_capacity.c`、
`ufs_varread`、`ufs_twoblock` 是 `standard`，永远编译，而它们每个都有一句
`#error` 拒绝 `HAKIOS_UFS`。所以这个配置必须走 stub。）

## 镜像

- `boot-948.img` —— **已知最稳**，35 分钟长跑 + M2b + USB 7.07 MB/s
- `boot-951.img` —— 带背光**只读**探针（`bl=1`），没有问题
- `boot-952.img` —— 带 `backlightctl`，**不要用**：它让盲写变容易了

## 下一步

按上面「先确认、再写」的顺序：boot 951（只读 dump）→ Android 改亮度 →
再 boot 951 → diff。确认占空比是哪个字，并且搞清另外三个字，再写。
