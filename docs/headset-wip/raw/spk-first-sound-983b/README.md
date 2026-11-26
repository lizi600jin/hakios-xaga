# 第一次外放出声

2026-11-27 00:53，`boot-983b`。

**操作者原话：「听见了……双扬都有是听出来了」，但「声音还是很大」。**

## 日志

```
19.654  i2c: spk: tone start (LR, dur=2500 ms, sgen amp_div=3)

19.697  i2c: spk: ON  Left(RCV)  (0x34) after 20 polls: SYS0=4218 STA0=0056 (CLKS=1 SWS=0)
19.734  i2c: spk: ON  Right(SPK) (0x35) after 20 polls: SYS0=4218 STA0=0056 (CLKS=1 SWS=0)

19.735  i2c: tfa: i2s3_din[spk_active] 1737 samples in 1000 us:
                BCK(21) hi=862 edges=805 | LRCK(22) hi=866 edges=96 | DO(24) hi=632 edges=571

22.245  i2c: spk: OFF Left(RCV)  (0x34): SYS0=4211 STA0=0016
22.253  i2c: spk: OFF Right(SPK) (0x35): SYS0=4211 STA0=0016
22.258  i2c: spk: ktz8863a post-tone sanity raw11 = 1649 [OK]
22.264  i2c: spk: tone done
```

## 三个关键信号

### `CLKS=1`

**两颗功放都锁到了外部时钟。** 这是整条链路最关键的一步 ——
PA 确认 BCK/LRCK 真的到了它的引脚上。

对比 `boot-980`（只读时）的 `STA0=0x0017`，现在是 `0x0056`。
`0x56 - 0x16 = 0x40` = `bit6` = `CLKS`。

### `SYS0 = 4218 → 4211`

`0x4218 - 0x4211 = 0x08` = `bit3` = `AMPE`。
**播放时输出级真的打开了，关停后又干净地关回去。**

`STA0` 同理：`0x0056 → 0x0016`（bit6 CLKS 消失）。

### `DO edges=571`

数据线在跳。对比 `boot-981`（没配 PA 时）是 683，同一量级。
说明 SGEN 的正弦波样本真的在往外送。

## 音量比预期大

`spk_amp=3` 按代码注释是 `1/16 = -24 dB = 满功率 1.56%`。

**但操作者说「声音还是很大」。** 说明还有另一个音量来源没被控制：

**TFA9873 自己的音量寄存器。**

看 `tfa_power_on_one_locked()` 只写了四个位：

```c
tfa_set_bf_locked(addr7, 0x0050u, 0u);   /* SBSL=0 */
tfa_set_bf_locked(addr7, 0x0120u, 1u);   /* MANSCONF=1 */
tfa_set_bf_locked(addr7, 0x0000u, 0u);   /* PWDN=0 */
tfa_set_bf_locked(addr7, 0x0030u, 1u);   /* AMPE=1 */
```

**没有写音量。** 所以放音时用的是容器里配的默认音量（很可能接近满幅）。

实机声压 = `SGEN amp_div` 衰减 × `TFA9873` 音量寄存器。
只降前者，后者还是满的，所以听起来仍然很大。

## 下一步

1. **短期验证**：`spk_amp=0`（1/128，比这次还小 8 倍）—— 不改代码就能试
2. **正解**：找到 `tfa98xx.cnt` 里的音量寄存器（TFA98xx 系列一般是 `0x0A`/`0x0B`，
   即 `MTP`/`VOLUME`），在 `tone` 里显式写一个低值

## 一个待解的差异

Neon 预期是 `CLKS=1 SWS=1`，实际是 **`CLKS=1 SWS=0`**。

`SWS` 大概是采样率/字长识别位。**但声音出来了**，所以它不是发声的必要条件。

**这一位为什么不置位，还没查。** 可能与 `spk_amp` 无关，与 I2S3 的 `I2S_CON3` 格式位有关。

## 镜像

`trial736/boot-983b.img`，sha256 `abe170b3ce2c…`。

boot-args：`cpus=8 tfa_probe=1 spk_init=1 spk_tone=1 spk_amp=3 spk_ms=2500`

## 这条链路的完整路径

```
XNU 内核
  └─ AFE (0x1e100000)
       ├─ APLL2  196.608 MHz
       ├─ AUDDIV / TOPCKGEN 分出 48 kHz
       ├─ I2S3_CON3 = 0x1a0b   主模式 32-bit
       └─ SINEGEN_CON0 = 0x04ac2ac1 → CON2 = 0x40（注入 O0O1）
            ↓
       GPIO 21 (BCK) / 22 (LRCK) / 24 (DO)
            ↓
  i2c-6  0x34 (Left, 750 Hz) / 0x35 (Right, 1500 Hz)
            ↓
       两颗 TFA9873N1B Class-D
            ↓
        外放喇叭
```

**从内核到喇叭，全线打通。**
