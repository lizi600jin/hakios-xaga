# I2S3 的时钟真的跑起来了

2026-11-26，`boot-981`。第一次向 AFE 写入，零 GPIO 写入、零 TFA9873 写入。

## 决定性证据：边沿计数

同一根引脚，三个时刻各采样 1 ms：

```
[before]     1739 samples: BCK(21) hi=1739 edges=0   | LRCK(22) hi=1739 edges=0   | DO(24) hi=0 edges=0
[active]     1739 samples: BCK(21) hi=879  edges=810 | LRCK(22) hi=867  edges=96  | DO(24) hi=637 edges=683
[after_stop] 1737 samples: BCK(21) hi=1737 edges=0   | LRCK(22) hi=1737 edges=0   | DO(24) hi=0 edges=0
```

**`LRCK edges=96` 在 1 ms 内。** 48 kHz 的帧时钟周期是 20.83 µs，1 ms 里正好 48 个周期、
上升沿加下降沿 96 个边沿。**和理论值分毫不差。**

**`BCK edges=810`。** 48 kHz × 32 bit × 2 ch = 3.072 MHz，1 ms 本该 6144 个边沿。
只数到 810 —— **GPIO 的输入同步时钟低于 3 MHz，欠采样**。但 810 远大于噪声，
足以证明线上有时钟。

**`DO edges=683`。** 数据线也在跳，SGEN 生成的正弦波样本真的在往外送。

**`after_stop` 三个都回零。** 关得干净，没留残状态。

**这组数字比「读一次 din」强得多** —— 单次读数可能是上拉、可能是巧合；
边沿计数只能是真有时钟。

## 寄存器回读，全部符合设计值

```
pre      APLL2 CON0..2/PWR = 000001e1 04000000 78fd5264 80000002   TOP_0e0=06020201
active   APLL2             = 000001e1/80000001
         TOP_0e0           = 06020201
         div0/2            = 00081ff7/0f070707
         AFE TOP0          = b0f94038
         DAC               = 00000001
         I2S3              = 00001a0b
         SGEN              = 04ac2ac1/00000040
         ENGEN             = 00000002
         TUNER2            = 00000375
```

`0x80000002` → `0x80000001`：**bit0 `PWR_ON` 从 0 变 1，bit1 `ISO_EN` 从 1 变 0。**
说明 APLL2 原来是断电隔离态，这一版真的把它拉起来了。

## TFA9873 感知到了时钟

```
before       Left 0x34 STA(10..13): 0017 0000 0000 010f
active       Left 0x34 STA(10..13): 0016 0000 0000 010f
             Right 0x35 STA(10..13): 0016 0000 0000 010f
```

**`0x10` 从 `0x0017` 变成 `0x0016`** —— 一位变了。芯片没有被告知任何事（我们对它全程只读），
它自己因为总线上出现了 BCK/LRCK 而改了状态位。

## 我判断错的两件事

### 1. 「APLL2 没恢复导致 RETURN 卡死」——不成立

我看到 teardown 只关 AFE 侧、没把 APLL2 恢复成断电态，就推测这会让 `loader_return` 卡住。

**实测 RETURN 30 秒，干净回安卓。**

教训：**从代码里读出来的「可疑点」，在没实测前不能当结论。**
这和之前 `boot-973` 那个「RETURN 成功但仍有潜在死锁」是同一类错误的镜像 ——
那次是有问题却以为没问题，这次是没问题却以为有问题。

### 2. 「设备卡死了」——是我自己的网卡断了

第一次要测 RETURN 时，报告「96 秒未回、USB 重新枚举」。我怀疑是 AFE 残留。

**实际是 `10.0.0.1/24` 的配置在 USB 重新枚举时丢了**，`RETURN` 那条 TCP 连接根本没建立，
请求从未到达内核。

证据：事后抢救的完整日志（2255 行）里**一条 `OK returning` 都没有**，
而日志末尾时间戳（294 s）紧跟设备 uptime（290 s）—— 说明日志是实时的、设备一直在正常跑。

**同一个错误这是第二次犯了。** 上一轮 `boot-979` 也是网卡没配就以为卡死，
当时已经写进文档「刷机回来第一件事是配网卡」，结果这次还是先怀疑硬件。

**改进**：以后测 RETURN 前，先 `PING` 一次确认通路，
拿到 `PONG` 再发 `RETURN`。这一版就是这么做的，一次就成。

```python
s.sendall(b'PING\n'); print(s.recv(70))   # 先确认通路
s.sendall(b'RETURN\n')                     # 再发命令
```

## 这一版做了什么

看代码是这些（按注释与实际回读）：

1. `APLL2` 从断电隔离态拉起（`0x34c` bit0=1, bit1=0），CON1 写 `0x3c7ea932`
2. `TOPCKGEN` 配 `aud_2_sel` / `aud_engen2_sel` / `audio_h_sel` / `AUDDIV_0` / `AUDDIV_2`
3. `AFE_APLL2_TUNER_CFG (0x3f4)` 与 `AFE_HD_ENGEN_ENABLE (0xdd0 bit1)`
4. `AFE_DAC_CON0 (0x10)` bit0 = 1（DAC 使能）
5. `AFE_I2S_CON3 (0x40) = 0x1a0b`（HD_EN + 48k + I2S_FMT + 32bit + EN）
6. `AFE_SINEGEN_CON2 (0x1dc) = 0x40`（注入点选 I2S3 的 O00/O01）
7. `AFE_SINEGEN_CON0 (0x1f0) = 0x04ac2ac1`（DAC_EN + 48k CH1/CH2 + amp + freq_div）

关闭顺序：SGEN → CON2 → I2S3 → DAC。**先停数据源再停通路**，硬件不会因为突然断流受损。

## 下一步

时钟有了、数据线在跳，但**没有声音**，因为 TFA9873 还是出厂态：

```
0x22 = 0x045c   TDMSSIZE = 23 -> 24-bit 字长
```

而 `tfa98xx.cnt` 里原厂把它改成 **31（32-bit）**，对齐 I2S3 的 32-bit 数据帧。
还要按左右分别写 `0x26/0x27` 的槽位映射（现在两颗完全相同）。

**boot-982 该配 TFA9873 了。**

## 镜像

`trial736/boot-981.img`，sha256 `722473d22c…`。
