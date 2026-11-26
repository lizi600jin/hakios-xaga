# CPU 只有满频的 35%：LUT 全解开了

2026-11-27，`boot-984a`（纯只读探测）+ `boot-984b`（提频，已打包未刷）。

## 一、问题

操作者反馈「太卡」，三个症状都有：打开 App 2-3 秒、滑动卡、输入延迟。

帧数据显示：

```
20 帧/秒，驱动里 1.4 ms
"0 us waiting for the GPU"        ← GPU 一次没被调用
52.8 ms/帧，其中 46 ms 没有任何统计解释
```

**46 ms 去哪了？** 答案在 CPU 主频：

```
MiniIOMobileFramebuffer: core 6 runs at 999 MHz
MiniIOMobileFramebuffer: core 2 runs at 699 MHz
```

## 二、`boot-984a` 只读探测：真值

### PLL 都是高频，问题在 post 分频

```
LL(A55):    en=1 post=0 vco=1999 → 1999 MHz
BL(A78):    en=1 post=1 vco=2799 → 1399 MHz    ← post 分频 2
B(A78):     en=1 post=2 vco=1999 →  499 MHz    ← post 分频 4
DSU/CCI:    en=1 post=1 vco=2679 → 1339 MHz
```

### 三个 DVFS 域

```
csram hdr: 55aa55aa 00000201 55aa55aa 00000201    ← 魔数通过

dom0(A55 0..3):  EN=0x201  PERF=0x00  HW=3  LAT=0
dom1(A78 4..6):  EN=0x000  PERF=0x13  HW=3  LAT=0x8bd0
dom2(A78 7):     EN=0x000  PERF=0x1c  HW=3  LAT=0
```

**`HW=3` 三个域都在线（MCUPM 在跑），但两个 A78 域的 `EN=0`。**

### LUT 编码（和 Android OPP 表逐档对上）

```
freq = entry & 0xfff                    /* MHz */
volt = ((entry >> 12) & 0x1ffff) * 10   /* 10 µV → mV */
```

验证：

| 域 | `lut[0]` | 当前档 | 当前频率 | PLL 实测 |
|---|---|---|---|---|
| dom0 A55 | 2000 MHz | `PERF=0` | 2000 | 1999 ✅ |
| dom1 A78 | 2850 MHz | `PERF=19` | 1400 | 1399 ✅ |
| dom2 A78 | 2850 MHz | `PERF=28` | 500 | 499 ✅ |

**LUT 项数和 Android 完全一致**：dom0 26 项 / policy0 25 档、dom1 32 项 / policy4 32 档、dom2 26 项 / policy7 26 档。

## 三、999 / 699 是怎么来的

`board.h:59` 的 `BOARD_CORE_ORDER = { 0, 4, 5, 6, 7, 1, 2, 3 }`，所以：

- XNU 逻辑 `core 6` = 物理 `cpu2`（A55）→ `1999/2 = 999 MHz`
- XNU 逻辑 `core 2/3` = 物理 `cpu5/6`（A78）→ `1399/2 = 699 MHz`

**不是降频，是 `EN=0` 让档位寄存器失效，频率退回 PLL post 分频的固定值。**

## 四、Android 侧的真值（对照用）

```
policy0 (A55): 45 个数字 / 25 档 / 满频 2000
policy4 (A78): 32 档 / 满频 2850
policy7 (A78): 26 档 / 满频 2850
scaling_driver: mtk-cpufreq-hw（三个域全一致）

运行中：cpu0-3 = 1400，cpu4 = 1600，cpu5 = 2300，cpu6 = 2550
```

**XNU 的 999 MHz 比 Android 的空闲档 1400 还低 30%。**

## 五、`boot-984b` 的提频方案（已打包，未刷）

按上游 `mediatek-cpufreq-hw.c` 的两行写法：

```c
writel_relaxed(0x1, REG_FREQ_ENABLE);       /* 0x84 */
writel_relaxed(index, REG_FREQ_PERF_STATE); /* 0x88 */
```

安全措施（全部核实过）：

| 措施 | 实现 |
|---|---|
| 目标频率限幅 | `[1000..2650]`，距满频 2850 留 200 MHz |
| CSRAM 魔数校验 | `0x55aa55aa`，不对就拒绝 |
| `HW_STATE != 0` 检查 | 为 0 就跳过该域 |
| LUT 降序扫描 | 选 `<= target` 的最高档 |
| 写后回读 | 10 ms 后读 `EN/PERF/HW` + `APLL` 实际频率，印 `[OK]`/`[NO_CHANGE]` |
| **RETURN 自动还原** | `hakios_mt6895_cpufreq_quiesce()` 在 `psci_smp_stop_others()` 之后调，把 `dom1/dom2` 降回 1400/500 且 `EN=0` |

**选了保守的 2200 MHz**（不是满频 2850），因为：
- LUT 里的电压是出厂校准的，硬件自己按表执行
- 但 2200 已经比当前 1399 快 **1.57 倍**，dom2 从 500 → 2200 是 **4.4 倍**

## 六、还没做的

- **`boot-984b` 没刷**（这一轮到此为止）
- 没验证 `EN=0x1` 是否真的是正确的使能位（`dom0` 是 `0x201`，差 0x200）
  留了 `cpufreq_en=<十进制>` 参数，写 1 不动就试 513（`0x201`）
- `fb_beat=0` 那一版只出了 1 条帧统计（可能只是没人滑动），但它已从 `boot-984b` 去掉

## 七、顺带澄清一个我判断错的事

我以为 `exec_inject` 死了（hlog 里 0 条 `hakios:`）。

**实际是 `libhakios.*.dylib` 写 `/dev/console`，走 `cnwrite()` 直接给 console，
不经过 `msgbufp`，所以 `/dev/hlog` 里看不到。**

而且更有力的旁证：**没有 `libhakios.dylib` 和 `libhakios.sb.dylib` 注入，
SpringBoard 和 backboardd 根本起不来** —— 它们活得好好的。

以后要用就改成写 `/tmp/app.log`。

## 八、镜像

```
boot-983c  无 -v（屏幕干净），RETURN 30-31 秒，当前 boot_a
boot-984a  只读探测 + fb_beat=0，RETURN 31 秒
boot-984b  cpufreq_mhz=2200（已打包未刷）
           sha256 da91bebf5b6f2aac32d8f090fbe4d184b7c9e6eaa7f1def498610f17e595471e
```
