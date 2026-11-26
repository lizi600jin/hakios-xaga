# 电池读通了：7 个寄存器全对，273 和 5080 与 Android 一模一样

2026-11-26，`boot-965`。这是 P3 的终点。

## 结果

```
i2c: gauge: i2c-7's clock is on (CG_STA 00000000); reading address 0x55
i2c: gauge: voltage            (0x08) = 4355 mV
i2c: gauge: current            (0x0c) = 65469 mA
i2c: gauge: temperature        (0x06) = 3068 0.1K
i2c: gauge: state of charge    (0x2c) = 96 %
i2c: gauge: cycle count        (0x2a) = 273
i2c: gauge: full charge        (0x12) = 4773 mAh
i2c: gauge: design capacity    (0x3c) = 5080 mAh
i2c: gauge: 7 of 7 words read
```

对照同一时刻 Android 的 `bms` uevent：

| 项 | XNU | Android |
|---|---|---|
| 电压 | 4355 mV | 4439 mV |
| 电量 | 96 % | 100 % |
| 循环 | **273** | **273** ✓ |
| 满充 | **4773 mAh** | **4773 mAh** ✓ |
| 设计 | **5080 mAh** | **5080 mAh** ✓ |
| 温度 | 3068 = 30.68 °C | 295 = 29.5 °C |

三个**不会随时间变化的量完全一致**——273、4773、5080。这比电压对得上更有说服力：
电压和电量在充电动，循环次数和容量不会。读对了就是读对了。

`current = 65469` 是 `0xFFBD`，按有符号 16 位是 **−67 mA**，放电，与当时的状态相符。
驱动现在按无符号打印，要改成有符号。

`temperature = 3068` 是 0.1 K，减 2731 得 `30.68 °C`。

## 两个真正的 bug，都在 LK 的机器码里

前置结论：**读的是 `bq28z610`，不是 `mt6375`。**
`/sys/class/power_supply/bms` 指向 `11d04000.i2c/i2c-7/7-0055`，
`MODEL_NAME` 是 `nfg1000b`。副 PMIC 管充电，电量计是这颗 TI 芯片。

### bug 1：数据端口是 8 位，不是 32 位

`I2C_DATA_PORT` 在 `+0x00`。LK 的代码用 `strb` / `ldrb` 一个字节一个字节地搬：

```
0x20e40  strb  w13, [x15, x11]    写命令字节
0x21040  strb  w13, [x15, x11]    写入 FIFO（发送路径）
0x21054  ldrb  w12, [x8, x9]      读一个字节
0x21058  strb  w14, [x13, x11]    回到 FIFO
```

用 32 位 `str` 写，每写一次往 16 字节的 FIFO 里塞 4 个字节，**四笔就满，第五笔卡死**。
用 32 位 `ldr` 读，一次弹走 4 个字节，**两字节的应答只剩一个，第二次读返回 `0xFF`**。

两个看起来无关的故障——「第二个字节读不到」和「第五笔开始超时」——是同一个原因。

### bug 2：`FIFO_ADDR_CLR` 写 `0x5`，不是 `0x1`

LK 在 `0x20fa8` 装 5、在 `0x20fb8` 写进寄存器：

```
0x20fa8  mov   w17, #5
0x20fb8  str   w17, [x15, x3]     x3 = regs[13] = +0x38
```

`0x1` 是老 SoC 的值。这颗芯片的 FIFO 有多通道指针，`0x1` 清不干净。

## 关键的一步：单次 WRRD，不是两段事务

正确的序列整条照抄 LK：

```c
i2c_wr(base, I2C_EXT_CONF, 0x1801u);
i2c_wr(base, I2C_DELAY_LEN, 2u);
i2c_wr(base, I2C_IO_CONFIG, 3u);                  /* open drain */
i2c_wr(base, I2C_CONTROL, 0x3au);                 /* ACKERR|CLK_EXT|DIR_CHANGE|RS */
i2c_wr(base, I2C_SLAVE_ADDR, (uint32_t)slave7 << 1);   /* 0xaa，八位地址 */
i2c_wr(base, I2C_INTR_STAT, 0x1ffu);              /* 写 1 清 */
i2c_wr(base, I2C_FIFO_ADDR_CLR, 5u);
i2c_wr(base, I2C_INTR_MASK, i2c_rd(base, I2C_INTR_MASK) & ~0x7u);
i2c_wr(base, I2C_TRANSFER_LEN, 1u);               /* 出 1 字节 */
i2c_wr(base, I2C_TRANSFER_LEN_AUX, 2u);           /* 进 2 字节 */
i2c_wr(base, I2C_TRANSAC_LEN, 2u);                /* 两段：写 + 读 */
i2c_wr8(base, I2C_DATA_PORT, command);            /* 八位 */
i2c_wr(base, I2C_START, 1u);

/* 等 INTR_STAT 的 COMP，且没有 ACKERR/HS_NACKERR/ARB_LOST，20 ms 超时 */
lo = i2c_rd8(base, I2C_DATA_PORT);
hi = i2c_rd8(base, I2C_DATA_PORT);
```

**两段独立事务在这里行不通。** 读那一半要两个字节，只收到一个，
第二次读 FIFO 已经空了。从机要的是那个 repeated start，
而 `DIR_CHANGE | RS` 两个位合起来才是它。

## 十三个镜像的教训

| 镜像 | 改动 | 结果 |
|---|---|---|
| 953 | 只读 dump | 拿到全部寄存器状态，`CG_STA = 0` |
| 954 | 首次事务，单连接 WRRD | 2/7，值错 |
| 955 | 8 位地址 + CONTROL 位 + FIFO_CLR | 7/7 全 0 |
| 956 | START 加 `RS_MUL_TRIG` | 7/7 全 0 |
| 957 | 改成两段独立事务 | 1/7，但 `4356 mV` 是对的 |
| 958 | 事务前写 `START = 0` | 4/7，全 `0xFFxx` |
| 959 | 去掉 `RS` | 4/7，同 958 |
| 960 | 去掉 `START = 0` | 4/7，同 958 |
| 961 | `DATA_PORT` 一次读 32 位 | 4/7，只有低字节 |
| 962 | 事务间隔 200 µs → 2 ms | 1/7，更糟 |
| 963 | 成功路径也打寄存器快照 | 诊断用 |
| 964 | 8 位端口 + `FIFO_CLR = 5` | 4/7，`FIFO 0x840` 显示还剩 2 字节 |
| **965** | **LK 的单次 WRRD** | **7/7 全对** |

**953 到 964 都在猜。** 真正解决问题的是去反汇编 `lk_a.img`——
那块硬件在每次充电时都被 LK 用同样的方式读过，答案一直在那里。

中间有一件事值得记下来：`boot-957` 曾经读出过 `4356 mV`，那是真的，
但后面每一版都把它弄坏了。**一个偶然的成功不能当成「已经修好了」的证据**，
因为不知道它为什么成功，也就不知道下一版会不会破坏它。

## 下一步

1. 把 `current` 按有符号打印（`(int16_t)value`）。
2. 温度换算成摄氏度（`value / 10 - 273.1`）。
3. 接进 `HakiosBatteryReport()`（`iokit/Kernel/hakios.battery.cpp` 里现成）。
4. 加一个 `battery` 设备节点，让用户态能查。

## 镜像

`trial736/boot-965.img` —— 7/7 全对。
另存 `boot-948.img` 作为无 I2C 的基线。

---

## 完成：电池进了 IOPMPowerSource（`boot-967`）

```
i2c: battery: 95.00% 4355 mV -45 mA 33.85 C  cycles 273  full 4773 mAh  design 5080 mAh
PMRD: setAggressiveness(0) kPMPowerSource = 2
```

`kPMPowerSource = 2` 是电源管理认到电池的信号。
`-45 mA` 的负号说明有符号转换生效了（放电）。

同一时刻的完整状态：

```
mali: M1 done        — GPU 固件在跑
mali: m2b: PASS      — GPU 执行了指令并写了内存
i2c: gauge: 7 of 7   — 电量计七个寄存器全读对
PUT 2.0 MB → 4.86 MB/s
```

## 单位和符号的三个坑

| 量 | 电量计给的 | `hakios_battery_state` 要的 |
|---|---|---|
| 电压 | 毫伏 `4355` | 微伏 `4355000` |
| 电流 | 有符号毫安 `-45` | 微安 `-45000`，正数表示充电 |
| 温度 | 0.1 开尔文 `3070` | 摄氏度百分之一 `3385` |

温度的换算是 `dk * 10 - 27315`：先乘 10 得百分之一开尔文（`30700`），
再减 `273.15 × 100`（`27315`），得 `3385` = 33.85 °C。

**电流必须是 `(int16_t)`。** 不转的话 `0xFFD3` 会印成 65491 mA —— 一块 65 安的电池。

## 剩下的

- 充电状态（`status` / `external`）现在是按电流正负猜的，不是问充电器。
  真要准得读 `mt6375` 的充电寄存器。
- 没有周期性更新，只在启动时报一次。要变成每秒一次需要挂到 `hakios_battery` 的轮询里。
- `battery_test` 和状态栏还没验（那需要用户态跑起来并截图）。
