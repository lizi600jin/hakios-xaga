# 144 Hz 攻坚：从 10 fps 到目标 144 Hz（boot-999 阶段）

2026-11-27 夜 ~ 2026-11-28 凌晨。GPU 合成已通，但帧率只有 10 fps，壁纸纯黑。
本轮定位了全部限制层，并落盘了触控 143 Hz 修复。未完成，明天继续。

## 一、今天的成果（按时间顺序）

| 里程碑 | 镜像 | 关键突破 |
|---|---|---|
| M2c | boot-991 | AS1 页表，三个 PA 全 6.49 GiB |
| Task 2 | boot-992 | 6 核 Shader Core 上电，16/16 线程写对 |
| M3 | boot-994 | panthor UAPI 全通，panthor_test 10/10 |
| M4a | boot-995 | Mesa panfrost 画出三角形 |
| M4b | boot-996 | 零拷贝 5.77 ms / 173 fps |
| M5 | boot-997 | backboardd 上 GPU，flush_id +70/s |
| PosterBoard | boot-998 | 壁纸进程启动，/tmp/hakios-gl.log tee |
| fb_hz=144 | boot-999 | 验证 fb_hz 不是限流点 |

## 二、144 Hz 目标的三层限制

### 面板硬件（Android 实测）

小米官网规格（mi.com/redminote11t-pro/specs）：

| 项 | 值 |
|---|---|
| 屏幕 | 6.6" LCD, 2460x1080 |
| 显示帧率 | 30 / 48 / 50 / 60 / 90 / 120 / 144（7 档变速） |
| 触控采样率 | 270 Hz（但 <=90 Hz 刷新时才可达） |

dumpsys display 实测：

```
mSupportedRefreshRates = [144.00002, 120.00001, 90.0, 60.0, 50.0, 48.000004, 30.000002]
操作者解锁后: mActiveModeId=2, peakRefreshRate=144.00002
```

各档 vsync 时序：

```
id=1  144.00002   appVsyncOffsetNanos=4166666   presentationDeadlineNanos=9333333
id=2  120.00001   appVsyncOffsetNanos=1199999   presentationDeadlineNanos=14800000
id=0  60.0        appVsyncOffsetNanos=2400001   presentationDeadlineNanos=28600000
```

144 Hz 周期 = 6.944 ms。

### 触控采样率（Android 实测）

getevent -t /dev/input/event5（NVTCapacitiveTouchScreen），滑动 12 秒：

```
触点样本 1240 个，11.83 秒
   7 ms x1205   (143 Hz)   <- 主力
   6 ms x  15   (167 Hz)
   8 ms x   5   (125 Hz)
中位数 6.89 ms = 145 Hz
```

143 Hz 和 144 Hz 面板 1:1 对应。270 Hz 只在 <=90 Hz 刷新时可达。

### iOS 侧的硬上限：CADisableMinimumFrameDurationOnPhone

Apple 官方文档（优化 iPhone 和 iPad app 以支持 ProMotion 显示屏）：

> 若不启用此支持，Core Animation 不会访问更高的框架速率（高于 60Hz）。

我们的 rootfs 里这个键缺席：

```
SpringBoard.app/Info.plist
  CADisableMinimumFrameDurationOnPhone: <缺席>
```

同一份文档的刷新率表把 10 Hz 列为合法省电档：

```
120Hz (8ms)  60Hz (16ms)  30Hz (33ms)  12Hz (83ms)  10Hz (100ms)
```

## 三、XNU 侧实测：三个限制叠加

### 限制 1：fb_hz 不是限流点（已验证无效）

boot-999 加了 fb_hz=144，帧间隔分布和 boot-998（fb_hz=60）几乎相同：

```
276 个间隔，31.0s
   100 ms x140  (10 fps)
   101 ms x 34  (10 fps)
    99 ms x 31  (10 fps)
    17 ms x 14  (59 fps)
    16 ms x 13  (62 fps)
```

结论：CoreAnimation 自己决定帧率，不参考内核 fb_hz。

MiniIOMobileFramebuffer.cpp:258 的注释也说明了：

> this driver only writes into the memory it scans, so fb_hz says how often frames
> are made, not how often the panel shows one.

refreshHz() 允许 1-240（:290-294），所以 144 是合法值，但改了没用。

### 限制 2：渲染只要 2.2 ms

/tmp/hakios-gl.log 的 frame N: 行：

```
frame 100: 67 GL calls, 11 draws, 102 triangles, 2613 us (106 handing on, 1327 waiting, 1 copying out)
frame 200: 64 GL calls, 11 draws, 2207 us (57, 1330, 1)
frame 240: 67 GL calls, 11 draws, 2203 us (58, 1332, 1)
frame 280: 67 GL calls, 11 draws, 2163 us (52, 1314, 2)
```

稳态 2.2 ms，其中 1.3 ms 等 GPU。剩 97.8 ms 是纯等待。

### 限制 3：启动期的极端长帧

```
frame   1: 17039 us + 20894 us 建 IOSurface 纹理 = 38 ms
frame  24: 827478 us  <- 827 ms，2002 calls, 713 triangles, 5 clears, 25 textures refilled
frame  26: 28885 us
```

### 200 帧平均

```
after 200 frames: 8045 us a frame
  of which 99 handing on, 1510 waiting, 0 copying out
  and before each 475 making textures of IOSurfaces
  3 IOSurfaces were the textures' memory, 26 had to be copied
  25 shaders, 22 programs; biggest texture 1080 x 2460; 0 errors
```

关键：3 of 29 零拷贝覆盖率只有 10%。26 个 IOSurface 必须拷贝。
单张 1080x2460 BGRA = 10.2 MB（日志里 10455 KB）。

## 四、壁纸纯黑：已排除和待查

### census 只缺 2 个 function，都不是壁纸

```
census: since the start: 61 edr x268 (x268 not drawn) 66 lub x267 (x267 not drawn)
```

eagl.c:2884-2892 的注释说明 edr 不画是正确的：

> Drawn as the picture it is given, it read what the GPU had not yet written out of its own
> memory -- nothing -- and wrote that over everything: every launch image SpringBoard keeps
> came out black

### 壁纸纹理确实创建了

```
第  67 行: texture  1 is IOSurface  1 ('BGRA' 1080 x 2460)  <- display surface 1
第 140 行: texture  4 is IOSurface  2 ('BGRA' 1080 x 2460)  <- display surface 2
第 326 行: texture 86 is IOSurface 31 ('BGRA' 1080 x 2460)  <- 壁纸
```

每个 texture 都只 attach 一次（包括每帧都在用的 display surface），
所以「只 attach 一次」不是判据。

### 但没有任何大尺寸纹理上传

```
textures: filled 0x1908/0x80e1/0x1401: 2 times, up to 129 x 158
textures: filled 0x881a/0x1908/0x140b: 4 times, up to 256 x 1024
textures: filled 0x1906/0x1906/0x1401: 41 times, up to 248 x 106
textures: filled 0x190a/0x190a/0x1401: 1 times, up to 512 x 512
```

最大只有 512x512。没有 1080x2460 的上传。
而 biggest texture 1080 x 2460 说明全屏纹理全部来自 IOSurface（attachImage）。

### 待查方向

Neon 判断：用 applog=1 applog_name=PosterBoard,CollectionsPoster 看 PosterBoard
为什么起了却没提交图层。不推荐 glshow=86（纹理编号每次开机漂移，且 texture 86
是渲染目标不是输入）。

## 五、触控 5 Hz 是滑动手感的直接来源（已修，未验证）

hakios.nvt.c 原来的 delay(200000)：

```c
/*
 * Two hundred milliseconds between reads, not the four this loop started with. The
 * number is not a tuning knob: it is what makes the read rate lower than the report
 * rate by two orders of magnitude, so a read that finds 0x77 means the chip has
 * nothing rather than that this thread took the report away.
 */
delay(200000);
```

5 Hz 触控 vs 143 Hz 原生速率。

Neon 已落盘修复（hakios.nvt.c:1515-1545）：

```c
if (fast_touch) {
    for (unsigned drop = 0; drop < 40u && pin_in(PIN_IRQ); drop++) {
        delay(NVT_IRQ_STEP_US);      /* 50 us */
    }
    was_high = pin_in(PIN_IRQ);
} else {
    delay(200000);
}
```

好包走 IRQ 脉冲下落（最多 2 ms），坏包/静默保留 200 ms 退避。

宏：
```c
#define NVT_IRQ_STEP_US    50u
#define NVT_IRQ_WINDOW_US  200000u
```

## 六、GPU 提频（mali_opp 新增，未验证）

hakios.mali610.c:637：
```c
static int gMaliOpp = -1;    /* mali_opp=0..39: GPUEB TARGET_STACK OPP commit (-1=default) */
```

:9644：
```c
if (PE_parse_boot_argn("mali_opp", &opp, sizeof(opp)) && opp >= 0 && opp <= 39) {
```

mali_opp=0 取 GPUEB TARGET_STACK 的最高档。

## 七、明天的执行清单

### Neon 推荐的 boot-1000 boot-args

```
mali_gpu=1 mali_opp=0 glframes=1 glcensus=1 fb_hz=144 ca_vbl=1
cpufreq_mhz=2850 applog=1 applog_name=PosterBoard,CollectionsPoster
```

注意：ca_vbl=1 和 applog 内核侧还没实现，需要 Neon 补。

### 待验证

1. 触控 143 Hz —— 滑动是否变顺
2. GPU 提频 —— mali_opp=0 后帧率是否上升
3. 壁纸 —— applog 抓 PosterBoard 为什么不提交图层
4. ca_vbl=1 —— 让 CoreAnimation 跟随内核节拍

### 三层还差什么

| 层 | 现状 | 目标 | 缺口 |
|---|---|---|---|
| 面板 DSI | loader 配（Android 能 144） | 144 Hz | 待 fb_measure=1 实测 |
| 内核节拍 | fb_hz=144 已设但无效 | 144 | 需 ca_vbl=1 让 CA 跟随 |
| CoreAnimation | 10 fps（空闲档） | 144 | CADisableMinimumFrameDurationOnPhone 缺席 |
| 触控 | 5 Hz | 143 Hz | 已改代码，待验证 |
| GPU | 219 MHz? | 600-852 MHz | mali_opp=0 待验证 |
| 零拷贝 | 3/29 | 尽量多 | pan_kmod.c 导入路径 |

## 八、方法论：今天学到的

### 有效的做法

1. 用 flush_id 判据证明 GPU 真在跑 —— 每秒 +70 说明 60 fps 提交，比看日志可靠
2. /tmp/hakios-gl.log 的 tee 是决定性的 —— say() 写 stderr，不进内核日志，必须 tee 到文件
3. frame N: 行的分段耗时（handing on / waiting / copying out）直接指出瓶颈
4. 对比 Android 基线 —— 知道硬件上限（144 Hz / 270 Hz 触控）才能判断差多少
5. fb_hz=144 单独验证 —— 排除了「内核节拍是限流点」这个假设，省了后面的弯路

### 踩过的坑

1. 「texture 86 只 attach 一次」不是判据 —— 所有 texture 都只 attach 一次，
   包括每帧在用的 display surface
2. fb_hz 只管内核生成帧，不管面板刷新 —— MiniIOMobileFramebuffer.cpp:258 明写了
3. edr / lub 不画是正确的 —— 画了会把整个屏幕涂黑
4. hakid 会卡死 —— TimeoutError 时设备没坏，长按电源 10 秒重启即可
5. adb reboot bootloader 在 XNU 运行时无效 —— USB 被 XNU 的 ECM 占用，必须先 RETURN

### 关键文件

```
hakios.nvt.c     触控驱动（两份：kernel-src/bsd/dev/ 和 mtk/mt6895/drivers/input/）
hakios.mali610.c 内核 GPU（两份：kernel-src/bsd/dev/ 和 mtk/mt6895/drivers/gpu/）
hakios.mali610.cpp IOKit 用户客户端
eagl.c           CoreAnimation 的 GL hook（plain_draw_begin / vblur / census）
inject.c         inject_sb.c 的姊妹；opengl_es() 决定是否加载 GL 库
MiniIOMobileFramebuffer.cpp  显示驱动（fb_hz / fb_beat / FRAME_PAUSE）
```

## 九、当前状态

```
boot_a       boot-999（fb_hz=144，sha256 881d7b0db659d2bfdef81166f6fc556e27c29a2d567d56b1090aff730b44ccd2）
设备         XNU 里（hakid 可达）
帧率         10 fps
壁纸         纯黑
PosterBoard  在跑（126 进程）
触控         5 Hz（代码已改，未刷）
GPU          频率未提（mali_opp 已实现，未刷）

golden/boot-997-m5-pass.img      sha256 3dafb13a...
golden/boot-995-m4a-pass.img     sha256 04361d8b...
golden/boot-994-m3-pass.img      sha256 fe54c9b0...
golden/boot-992-compute-pass.img sha256 91ee276c...
golden/boot-991-m2c-pass.img     sha256 beb5e96f...
golden/boot-985b-golden.img      sha256 ae41a7ee...
```

## 十、明天开工第一步

```bash
# 1. 确认设备
adb devices; fastboot devices

# 2. 让 Neon 补 ca_vbl=1 和 applog 的内核实现
# 3. 编译内核（触控 + mali_opp 已落盘）
R736=/home/lizi/IOS/hakios-mtk/logs/ufs-apfs-fstree-root-736
XNU_SRC="$R736/kernel-src" bash /home/lizi/IOS/hakios-mtk/tools/make-kernel.sh -j6

# 4. boot-args
#    mali_gpu=1 mali_opp=0 glframes=1 glcensus=1 fb_hz=144 ca_vbl=1
#    cpufreq_mhz=2850 applog=1 applog_name=PosterBoard,CollectionsPoster

# 5. 刷 boot-1000，验三件事：触控手感、帧率、壁纸
```
