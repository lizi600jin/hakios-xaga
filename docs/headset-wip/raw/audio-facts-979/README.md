# 外放的真实参数：TFA9873 + I2S3 + AFE 0x1e100000

2026-11-26，在干净 Android（17 / xaga）上采集。零风险，没动设备。

## 一、芯片是 TFA9873，不是 9874 —— 官方容器文件证实

`/vendor/firmware/tfa98xx.cnt` 的前 64 字节：

```
50 4d 33 5f 30 31   "PM3_01"        容器魔数
f4 0e 00 00         3828            文件长度（= 实际大小）
54 dc a9 2f         0x2fa9dc54      校验
4d 69               "Mi"            小米定制
54 46 41            "TFA"
39 38 37 33 4e 31 42  "9873N1B"      ← 型号 9873，变体 N1B
```

**`9873N1B` 就是答案。** 设备树的 `compatible = "goodix,tfa9874"` 是沿用旧名，
驱动进 probe 后按寄存器 `0x03` 读出的 `revid = 0x73` 分发到 `tfa9873`。

这和我实机读到的 `REV (0x03) = 0x0b73` 完全对上：低字节 `0x73` = 9873，高字节 `0x0b` = 硅片版本。

## 二、外放走 I2S3

`tinymix` 读到的三个 MTK 控件：

```
537  ENUM  1  MTK_SPK_TYPE_GET          MTK_SPK_MEDIATEK_RT5512
538  ENUM  1  MTK_SPK_I2S_OUT_TYPE_GET  MTK_SPK_I2S_3          ← 外放
539  ENUM  1  MTK_SPK_I2S_IN_TYPE_GET   MTK_SPK_TINYCONN_I2S_0
```

**`MTK_SPK_I2S_3`** —— 这颗 PA 挂在 I2S3 上。

设备树里对应的 pinctrl 组：

```
aud_gpio_i2s3_on     aud_gpio_i2s3_off
aud_gpio_i2s0_on     aud_gpio_i2s0_off
```

**但 `pins_cmd1_dat` / `pins_cmd2_dat` / `pins_cmd3_dat` 三个属性读出来是空的。**
说明 I2S3 的引脚配置不在这个节点上，或者运行时由驱动自己写 pinmux 寄存器。
**这一条还没查清，别当成已经有了。**

## 三、AFE 在 0x1e100000

```
/proc/device-tree/soc/syscon@1e100000/
  compatible = "mediatek,mt6895-afe", "syscon"
  reg = <0x00 0x1e100000 0x00 0x100000>     ← 1 MB 窗口
  #clock-cells
```

**`mediatek,mt6895-afe` 是 SoC 的音频前端**，TFA9873 从它拿 BCLK + WCLK + PCM 数据。

设备树里没有单独的 `sound` 节点暴露出来（`/proc/device-tree/soc/` 下搜 afe/audio 只找到这一个）。

## 四、驱动与固件文件清单

```
/vendor_dlkm/lib/modules/snd-soc-tfa98xx.ko      462136 B    ← PA 驱动
/vendor/lib/modules/snd-soc-mt6895-afe.ko        807656 B    ← AFE 驱动
/vendor/lib/modules/snd-soc-mt6368.ko            277608 B    ← PMIC codec
/vendor/lib/modules/mtk-sp-spk-amp.ko             17368 B
/vendor/lib/modules/snd-soc-mtk-common.ko        117760 B
/vendor/lib/modules/audio_ipi.ko                 190472 B
/vendor/lib/modules/snd-soc-audiodsp-common.ko   184496 B

/vendor/firmware/tfa98xx.cnt                       3828 B    ← 主容器（9873N1B）
/vendor/etc/smartpa_param/tfa98xx_device.bin        612 B
/vendor/etc/smartpa_param/tfa98xx_voice.bin        1512 B
/vendor/etc/smartpa_param/tfa98xx_ringtone.bin     1512 B
/vendor/etc/audio_param/tfa98xx_music.bin          2649 B
/vendor/etc/audio_param/tfa98xx_voice.bin          2649 B
/vendor/etc/audio_param/tfa98xx_ringtone.bin       2649 B
/vendor/etc/audio_param/SmartPa_AudioParam.xml     4514 B
/vendor/lib64/libnxpsmartpaparser.so                       ← 容器解析器
/vendor/lib64/libaudiosmartpamtk.so
/vendor/bin/smartpa_nvtest
```

**注意两个不同的 `tfa98xx_*.bin` 目录：**
`/vendor/etc/smartpa_param/` 是 1512 字节，`/vendor/etc/audio_param/` 是 2649 字节。
**大小不同，内容不是同一份。** 用之前要弄清哪个是哪个。

## 五、为什么 GPIO 213 是 `dir=in / din=0` 芯片还应答

外部顾问给的解释：

> RST 是**高电平有效**，并且芯片内部自带约 90 µA 下拉电流源。
> RST = HIGH 才是复位/掉电；RST = LOW（包括 GPIO 设成输入、靠内部下拉拉成 0）正好是
> **正常运行、释放复位、I2C 可通信**。

**这个解释和实机现象一致**（我们没碰 GPIO 213，两颗都 ACK）。

**但这一条是从外部资料来的，我还没在 xaga 自己的设备树或原厂驱动里验证过。**
用之前应该找一个独立证据：比如看 `snd-soc-tfa98xx.ko` 里对 reset gpio 的操作是
`gpiod_set_value(1)` 还是 `(0)`。

## 六、还没做的事

- **I2S3 的引脚号没查到**（`pins_cmd*_dat` 是空的）
- **AFE 的时钟/电源域没查**（`#clock-cells` 有值，但没看 clocks 属性）
- **`tfa98xx.cnt` 的完整结构没解**（只看了头 64 字节）
- **两个 `tfa98xx_*.bin` 目录的区别没弄清**

## 七、这些数据能支撑什么

有了型号（9873）、总线（I2S3）、AFE 基址（0x1e100000），
可以开始规划「XNU 里让 AFE 出 SGEN → I2S3 → TFA9873」这条链路。

**不需要 ADSP。** AFE 硬件自带 SGEN 正弦波发生器，可以绕开 DSP 直接出声。
