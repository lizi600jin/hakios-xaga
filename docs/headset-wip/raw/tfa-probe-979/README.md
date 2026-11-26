# tfa9874 应答了，两颗都在

2026-11-26，`boot-979`。i2c-6 上第一次摸到功放。

## 结果

```
i2c: tfa: gpio213 (tfa_reset): mode=0 dir=0(in)  dout=0 din=0   [read-only]
i2c: tfa: gpio160 (spk_sw):   mode=0 dir=1(out) dout=0 din=0   [read-only]

i2c: tfa: Left  (0x34) REV    (0x03) = 0x0b73 (be)   [raw_le 0x730b]
i2c: tfa: Left  (0x34) STATUS (0x00) = 0x4201 (be)
i2c: tfa: Right (0x35) REV    (0x03) = 0x0b73 (be)   [raw_le 0x730b]
i2c: tfa: Right (0x35) STATUS (0x00) = 0x4201 (be)

i2c: tfa: ktz8863a post-probe sanity raw11 = 1649 [OK]
```

**零 GPIO 写入、零 I2C 写入。** 全程只有 `i2c_rd`，代码里带 `[read-only]` 标记。

## 三个观察

### 1. reset GPIO 没释放，但芯片照样应答 I2C

`gpio213` 是 `dir=0 (in)`、`din=0`。也就是说这颗引脚现在既不是输出、也没被拉高。
按理说 `reset` 有效时芯片应该不应答，但它**两颗都 ACK 了**。

可能的原因：
- `tfa9874` 的 reset 是低有效，`din=0` 恰好是「释放」状态
- 或者芯片的 I2C 模块不受 reset 影响

**这需要对照 xaga 的设备树和原厂驱动确认极性，不能猜。**

### 2. `REV = 0x0b73`，不是预期的 `0x0c74..0x0f74`

两个可能：
- 这不是 `tfa9874`，是同系列别的型号（`tfa9873` / `tfa9890` 之类）
- 或者寄存器 `0x03` 在这个型号上不是版本号

`STATUS (0x00) = 0x4201` 两颗完全一样，说明读到的是确定性内容，不是总线噪声。

### 3. `STATUS 0x4201` 的位

`bit0 = 1`（可能是 `MTPB` 或 `DSP 运行中`），`bit9 = 1`，`bit14 = 1`。
**但这只是原始值，没有 datasheet 就不能解释。** 记下来备用。

## 一个差点写错的 bug：BatteryStatus 的位定义

我先按 Linux `bq27xxx` 的 `FLAGS` 寄存器把 `bit0` 当成了 `DSG`，
但 `bq28z610` 的 `BatteryStatus (0x0A)` 不是那个布局。

TI BQ28Z610 TRM 原文：

```
0x0A BatteryStatus()
  Bit 3:0  EC3,EC2,EC1,EC0  Error Code（0x0 = OK）
  Bit 4    FD   Fully Discharged
  Bit 5    FC   Fully Charged
  Bit 6    DSG  0 = charging, 1 = discharging
  Bit 7    INIT
```

**`bit0` 是错误码的最低位。** 按那个改，错误码为 0 时会永远读到 `dsg=0`，
拔掉 USB 也会误报「插电/满电」。

这一版实机读到 `status = 0x60` = `bit5 FC + bit6 DSG` 同时置位，
正好对应「满电，但插电脑 500 mA 带不动，电芯在净放电」。

## 验证

| 项 | 结果 |
|---|---|
| 8 核 | `8 processors running`（28 秒后） |
| 电池 | 3 条，间隔 10.0 秒 |
| 背光 | `/dev/hakios-backlight ready`，探测后 `raw11 = 1649` 未受影响 |
| GPU | `m2b: PASS` |
| **RETURN** | **30 秒** |

**功放探测没有影响 i2c-6 总线** —— `ktz8863a post-probe sanity` 就是为这个加的，
背光读回 `1649` 和探测前一致。

## 一个操作失误

第一次测 RETURN 时网卡配置丢了（`ip addr` 被 `fastboot` 重启冲掉），
请求根本没发出去，误以为卡死。**重配网卡后立刻 30 秒正常返回。**

记下来：**刷机回来第一件事是先配好 `10.0.0.1/24`，再判断设备死活。**

## 镜像

`trial736/boot-979.img`，sha256 `617b7f128b32e804745de39a62c0bf127cd22a5b3b1a5a1ac76373b5e7fb3b1a`。
manifest 用 `--from-manifest` 从 978b 钉住了 `kernel.ver`（`gdb08f63c-dirty`）。

## 下一步

需要确认的：
1. `tfa9874` 的 `reset` 极性（从设备树或原厂驱动）
2. `REV 0x0b73` 到底是哪个型号
3. `STATUS 0x4201` 的位含义

拿到这些才能谈「外放出声」。
