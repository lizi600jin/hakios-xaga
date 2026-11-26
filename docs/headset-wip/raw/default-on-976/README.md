# 电池和背光默认开启，以及「跟随系统亮度」的实机证据

2026-11-26，`boot-976`。删掉了 boot-arg `i2c=`，让这两样东西开箱即有。

## 默认开启后的启动日志

```
i2c: IMP_IIC_WRAP_C at 0x11282000 CG_STA 00000000 (a set bit means that clock is off)
i2c: IMP_IIC_WRAP_S at 0x11d07000 CG_STA 00000000
i2c: default-on (9 buses clocked): starting battery (i2c-7) and backlight (i2c-6)
i2c: battery: reading the gauge every 10 seconds
i2c: c6: i2c-6's clock is on (CG_STA 00000000); reading ktz8863a at 0x11
i2c: c6: DEVICE_ID  (0x01) = 0x12 ( 18)  (matches loader)
i2c: c6: BL_CFG2    (0x03) = 0xcd (205)  (matches loader)
i2c: c6: BL_EN      (0x08) = 0x17 ( 23)  (matches loader)
i2c: c6: brightness raw11 = 1649 / 2047  (0x04=0x01 0x05=0xce)
i2c: bl: /dev/hakios-backlight ready (orig=1649, safe range [200..1967]; cmds: status|raw|level|restore)
i2c: battery: 96.00% 4356 mV -242 mA 29.15 C  cycles 273  full 4773 mAh  design 5080 mAh  rem 4576 mAh  status 0x40
i2c: battery: 96.00% 4356 mV -231 mA 29.15 C  cycles 273  full 4773 mAh  design 5080 mAh  rem 4575 mAh  status 0x40
```

**九条总线的 64 字 dump 没了。** 之前每一版启动都刷 600 多行寄存器，
把 1 MB 的 hlog 环形缓冲占掉一半，真正有用的行反而被挤出去。

`i2c=0` 是逃生开关：什么都不碰。`i2c=1..5` 是原来的调试档位，全部保留。

## 验证

```
backlightctl status   -> raw11=1649 orig=1649
backlightctl raw 800  -> 800   (0x04=0x00 0x05=0x64)
backlightctl restore  -> 1649  (0x04=0x01 0x05=0xce)
RETURN                -> 30 秒
```

## 「跟随系统亮度」的证据

这一段是为了下一步采集的。**`backboardd` 已经在调用亮度相关的 selector，
只是 `MiniIOMobileFramebuffer` 没有实现。**

`backboardd` 一次启动里调用过的 selector：

```
[3, 4, 5, 6, 8, 12, 15, 17, 22, 27, 28, 29, 33, 50, 55, 57, 68, 74, 79, 84]
```

其中未实现的：

```
[15, 17, 22, 27, 29, 33, 50, 55, 57, 68, 84]
```

已实现的：`[3, 4, 5, 6, 8, 12, 28, 74, 79]`

日志原文的样子：

```
MiniIOMobileFramebuffer: backboardd[10] selector 15 (scalars 2 in [0x0 0x10 0x0 0x0], 1 out; structure 0 in, 0 out)
MiniIOMobileFramebuffer: selector 15 not implemented (scalars 2 in [0x0 0x10 0x0], 1 out; structure 0 in, 0 out)
```

注意 `selector 15` 带了 `scalars 2 in [0x0 0x10 ...]` —— **有输入参数，一个返回标量。
这是「设置某个值」的形状，不是「查询」的形状。**

这一段是判断哪个 selector 是亮度的关键线索，但还不足以确定：
需要对照 Darwin 的 `IOMobileFramebuffer` 头文件里 selector 的编号约定。

## 还没做

- 不知道哪个 selector 是亮度。`15` 和 `17` 都像（一个 2 入 1 出，一个也是），
  但 `IOMobileFramebuffer` 的 selector 编号不是公开文档，需要从头文件或者别处找。
- 就算找到，还要确认 `backboardd` 传进来的数值范围（0..1 的浮点？0..255？0..4095？）
  和 `ktz8863a` 的 `200..1967` 怎么对应。

## 镜像

`trial736/boot-976.img` —— 电池 + 背光默认开启，RETURN 30 秒，**当前基线**。
