# 背光的硬件路径（摸底，未实现）

2026-11-26。从 stock Android 和设备树查出来的。

## 结论：背光是显示 PWM，MMIO，不是 I2C

```
/sys/class/leds/lcd-backlight
  → devices/platform/soc/soc:mtk_leds/leds/lcd-backlight

compatible: mediatek,disp-leds
max_brightness: 4095  (0xfff)
brightness:     0     (读的时候屏幕是灭的)
```

设备树里 `mtk_leds` 只有 `label` / `max-brightness` / `max-hw-brightness` 三个属性，
**没有 pwm 引用**，所以它不是通用 PWM 通道，而是显示子系统自带的那个：

```
disp_pwm0@1100e000 {
        compatible = "mediatek,disp_pwm0", "mediatek,mt6895-disp-pwm";
        reg = <0x00 0x1100e000 0x00 0x1000>;
        interrupts = <0x00 0xd7 0x04 0x00>;
};
```

**基址 `0x1100e000`，一页（4 KB）。**

另外还有一个通用 PWM 控制器 `pwm@11008000`（`mediatek,pwm`，版本 2，4 个通道），
那是给红外、振动之类用的，不是屏幕背光。

## 设备树的两个值不一致

```dts
mtk_leds {
        backlight {
                label = "lcd-backlight";
                max-brightness = <0xff>;          /* 255   */
                max-hw-brightness = <0x7ff>;      /* 2047  */
        };
};
```

而 Android 运行时 `/sys/class/leds/lcd-backlight/max_brightness` 读出来是 **4095**。

三个数都不一样，说明驱动在 probe 时按硬件实际能力覆盖了设备树的值。
**实现时要按 4095 算**，不是 255 也不是 2047。

## 还缺什么

`disp_pwm` 的寄存器布局。vendor 树里没有这个驱动
（`drivers/` 下只有 clk / gpu / misc / soc，是裁剪版），
主线内核也没有（`mediatek,mt6895-disp-pwm` 是私有的）。

拿到的办法，按代价排：

1. **从 Android 现场读**：`disp_pwm0` 在跑的时候，把 `0x1100e000` 那一页 dump 出来，
   改一次亮度再 dump 一次，diff 就知道哪个寄存器是占空比。
2. **从 LK 反汇编**：`lk_a.img` 里应该有初始化序列。
3. **试错**：4 KB 的窗口，寄存器不多，但盲写有让屏幕黑掉的风险。

**第 1 条最稳，而且只需要一次 adb。**

## 为什么值得做

现在屏幕亮度锁死在 loader 配的那个值。不能调暗意味着：

- 晚上用刺眼
- 不能靠调暗省电（背光是这个设备最大的耗电项之一）
- 没法息屏

而它是纯 MMIO，不需要 I2C 基础设施，比电池那条路短得多。

## 相关

- `docs/headset-wip/raw/varspace-948/` —— 同一批摸底里的存储侧
- 电池走 I2C（`mt6375@34` 在 `i2c-5` / `0x11280000`），比背光贵一个数量级

## 第一步：从 Android 现场读寄存器（不用刷机）

背光 PWM 的窗口是 `0x1100e000`，一页。Android 在跑的时候可以读：

```sh
# 改亮度之前
adb shell 'su -c "dd if=/dev/mem bs=4096 skip=$((0x1100e000/4096)) count=1 2>/dev/null"' > before.bin

# 改亮度
adb shell 'su -c "echo 2047 > /sys/class/leds/lcd-backlight/brightness"'
sleep 1
adb shell 'su -c "echo 100  > /sys/class/leds/lcd-backlight/brightness"'
sleep 1

# 改完之后
adb shell 'su -c "dd if=/dev/mem bs=4096 skip=$((0x1100e000/4096)) count=1 2>/dev/null"' > after.bin
```

`/dev/mem` 在大多数 Android 上可用（有 root 的话）。如果 Secure Boot 关掉了它，
换 `/proc/kcore`，或者干脆用一个读物理地址的小程序。

两份 dump 一 diff，变化的那几个字就是占空比和使能。

**这一步不需要写内核代码，也不需要理解寄存器手册。**

## 第二步：在 XNU 里写

拿到寄存器偏移之后：

```c
#define DISP_PWM_BASE   0x1100e000ull

static vm_offset_t disp_pwm;

static void
disp_pwm_set(uint32_t level)      /* 0 .. 4095 */
{
	/* 偏移待测 */
	reg_wr(disp_pwm, DISP_PWM_DUTY, level);
}
```

挂到 `/dev/hakios-backlight`，让用户态 echo 一个数进去。

还需要确认一件事：**PWM 的时钟是谁开的**。如果 disp_pwm 的时钟由 disp 子系统管，
而那部分在 loader 里配好之后没人关，那就直接能用。
如果时钟被关了，还要开 CG（clock gate）——那是另一个寄存器。

从 Android dump 里也能看出这一点：如果 `disp_pwm` 那页读出来不是全零或全 F，
说明时钟是开的，可以直接写。
