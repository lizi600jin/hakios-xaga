# 音频域是开着的，AFE 可以直接读

2026-11-26，`boot-980`。纯只读，零写入。

## 一、六根 GPIO

```
gpio213 (tfa_reset): mode=0 dir=0(in)  dout=0 din=0   [read-only]
gpio160 (spk_sw):    mode=0 dir=1(out) dout=0 din=0   [read-only]
gpio21  (I2S3_BCK):  mode=1 dir=0(in)  dout=0 din=1   [read-only]
gpio22  (I2S3_LRCK): mode=1 dir=0(in)  dout=0 din=1   [read-only]
gpio24  (I2S3_DO):   mode=1 dir=0(in)  dout=0 din=0   [read-only]
gpio23  (I2S0_DI):   mode=1 dir=0(in)  dout=0 din=1   [read-only]
```

**两件事值得记：**

1. **I2S3 三根引脚已经是 `mode=1`** —— pinmux 还是 LK 或 Android 留下的 I2S3 功能配置，
   没有被打回 GPIO。上电后不用重新 pinmux 就能用。
2. **`din=1`（21/22/23），只有 24 是 0** —— BCK 和 LRCK 读到高电平，DO 是 0。
   但这时还没有时钟在跑（AFE 的 I2S3 控制器没启用），所以 `din` 更可能是引脚内部上拉，
   不是真有信号。**不能据此判断时钟在跑。**

`gpio160`（`spk-sw`）是 `dir=out, dout=0` —— 按反汇编结果这是「外放」态（profile≠1）。

## 二、音频电源域已经上电

```
spm: AUDIO_PWR_CON(0xe14)=c000000d  ADSP_INFRA(0xe1c)=c000000d
     PWR_STA=de0001fd/de0001fc      INFRA_PROT(0x220)=00000000
```

- `0xc000000d` 的 bits[31:30] = `0b11`，**两个 PWR_ACK 都置位** —— 域已上电完成
- `INFRA_PROT(0x220) = 0` —— **bus protect 全部清除**
- 所以三重门控（`pd_on && prot_clear && clk_on`）通过，AFE 才被读

**这是好消息：音频域不用我们自己去拉，它本来就是开的。**

## 三、时钟

```
topckgen: aud(0x90)=01040401    intbus(0xa0)=02010001  aud1(0xf0)=01030307
          aud2(0x100)=0c050001  audh(0x110)=0103000d
          div0(0x320)=00001fff  div2(0x328)=07070707

apmixed: APLL1(0x328/338)=000001e1/80000002
         APLL2(0x33c/34c)=000001e1/80000002
```

**`APLL1_PWR = 0x80000002`，bit31 = 1 = APLL 已使能。APLL1 和 APLL2 都是开的。**

## 四、AFE（0x1e100000，门控通过后读到）

```
TOP_CON0..3 = b0fd4038 00000000 00000000 60000004
DAC_CON0    = 00000000
I2S_CON3    = 00000000        ← I2S3 控制器还没启用
SGEN0/2     = 00000000/0000007f
```

`I2S_CON3 = 0` 和 `SGEN0 = 0` 说明：**AFE 现在没有在往外送数据。**
这正是我们要在 boot-981 里打开的东西。

## 五、TFA9873 两颗的寄存器（完全对称，都是出厂默认）

```
Left (RCV) 0x34   SYS(00..04): 4201 8000 0008 0b73 0340
Left (RCV) 0x34   STA(10..13): 0017 0000 0000 010f
Left (RCV) 0x34   TDM(20..27): 2883 c1f0 045c 0003 0000 0000 0010 0001

Right (SPK) 0x35  SYS(00..04): 4201 8000 0008 0b73 0340
Right (SPK) 0x35  STA(10..13): 0017 0000 0000 010f
Right (SPK) 0x35  TDM(20..27): 2883 c1f0 045c 0003 0000 0000 0010 0001
```

**两颗一字不差，包括 `0x26/0x27`（左右声道的槽位映射）。**

这说明它们**现在都是出厂默认值、没被配置过** —— 因为 Android 的
`snd-soc-tfa98xx` 驱动没在 XNU 里跑。真实播放前必须按左右分别写槽位。

`0x03 = 0x0b73` 再次确认型号（9873，rev 0x0b）。

## 六、验证

| 项 | 结果 |
|---|---|
| GPIO / SPM / TOPCKGEN / APMIXED / AFE / TFA 全部读到 | ✅ 17 行 |
| 电池 | 间隔 10.0 秒 |
| 背光 | `/dev/hakios-backlight ready`，`ktz8863a sanity = 1649 [OK]` |
| GPU | `m2b: PASS` |
| **RETURN** | **30 秒** |

## 七、还没确认的

- **8 核那条 `8 processors running` 这次没在日志里看到**（只看到 `5 of 8`）。
  boot-979 已验证过它能起来，且本版没动核相关代码。
  **下次看日志时留意一下，别当成回归。**
- `TOP_CON0 = b0fd4038` 的位含义没解（要对照 `mt6895-reg.h`）
- `0x22 = 0x045c` 按「`[6:2]` = 槽宽」算出来是 23，不是 31 —— 和预期不符，
  但这是出厂默认值，可能字段位置和我理解的不同
- `SGEN2 = 0x7f` 有值而 `SGEN0 = 0`，这个不对称需要解释
