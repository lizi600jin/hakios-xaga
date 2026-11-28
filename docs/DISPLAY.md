# 显示：144 Hz、零撕裂与彩色壁纸

本文记录 xaga（Redmi Note 11T Pro，MT6895Z / 天玑 8100，1080×2460 面板）上
HakiOS 显示通路从「10 fps 且壁纸全黑」到「126 fps、零撕裂、彩色壁纸」的完整结论。

## 一、面板与几何

| 项目 | 值 |
| :--- | :--- |
| 物理面板 | 1080 × 2460，432 或 4352 字节每行 |
| 刷新模式 | 7 档：30 / 48 / 50 / 60 / 90 / 120 / 144 Hz |
| 触控硬件上限 | 270 Hz（但只有 ≤ 90 Hz 刷新时才达到） |
| iPhone 参照 | iPhone X，1125 × 2436 @3x = 375 × 812 pt |

xaga 的面板用**两条 DSC 切片**驱动，每片 540 列：左半由 `ovl4` 读，右半由 `ovl6` 读，
两者 pitch 都是 4352，右片地址比左片高 `0x870` 字节（540 像素 × 4 字节，正好一片）。
`ovl0` 是另一条管道，它的 layer 0 寄存器全部读回 0 —— 早期三次尝试都看错了地方。

## 二、`display-scale` 与彩色壁纸

`MobileGestalt` 的 `main-screen-scale` 来自设备树的 `/chosen/display-scale`。
这个值被移植时从 720p 机型（x6826b，@2x）带过来，一直是 **2**，导致：

```
display-scale = 2
  → MobileGestalt 缓存 main-screen-scale=2、width=1080、height=2460、class=(none)
  → UIKit 认为屏幕是 540 × 1230 @2x，而不是 375 × 812 @3x
  → WallpaperKit 判定所有 @3x 壁纸不兼容（"No compatible wallpaper found on disk"）
  → 回退到 400（Solid_Black），壁纸纯黑
```

修复需要**两步，缺一不可**：

1. `mtk/loader/mtkloader.c` 里 `display-scale` 改为 **3**（与上游 `xnuloader/loader.c:3243` 一致）；
2. 删除 `/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist`。

只改 ① 不够（旧缓存还在），只删 ② 不够（新值仍是 2）。

修复后 `contents.userInfo` 从 `{'wallpaperRepresentingIdentifier': '400'}` 变为 `{'7400'}`，
iOS 16 的 WWDC 2022 彩色壁纸正常渲染；同时键盘偏移与关机滑动条也一并恢复正常，
因为它们依赖同一套屏幕尺寸。

> **注意**：`mtk/loader/mtkloader.c` 与构建目录里的副本是两份文件，
> 编译实际读的是后者（`loader-build.json` 的工作目录）。改完必须确认 loader 的 sha256 变了，
> 否则刷进去的仍是旧镜像。

## 三、帧率：三层限制的定位

| 阶段 | 配置 | 峰值 fps | `driver us` |
| :--- | :--- | ---: | ---: |
| 初始 | `fbwidth=1080 fbheight=2460`（1:1，壁纸黑） | 119 | 1.12 ms |
| 去掉 `fbwidth` | 双线性缩放 1125→1080 | 83 | 6.0–9.2 ms |
| `fb_filter=0` | 最近邻缩放，无裁边 | 111 | 1.3–1.7 ms |
| `fbfit=3` | 1:1 拷贝，左右各裁 22 px | 107 | 1.2–1.4 ms |
| **`fb_filter=0` + `fb_flip=1`** | **双缓冲 + 最近邻** | **126** | **2.1–2.4 ms** |

### `driver us` 为什么在彩色壁纸下暴涨

`MiniIOMobileFramebuffer.cpp` 的双线性缩放有两处短路：

```c
mixed[c] = a == b ? a : blend(a, b, weight);            /* (most of a screen is the same as next to it) */
out[i]   = w == 0 || a == from[c + 1] ? a : blend(a, from[c + 1], w);
```

纯黑壁纸时约九成像素命中 `a == b`，跳过 `blend()`，只要 3.9 ms；
换成彩色渐变壁纸后几乎每个像素都要 `blend()`，265 万像素每帧触发数百万次标量插值，
直接飙到 6–9 ms。`fb_filter=0` 走最近邻，完全跳过 `blend()`，回到 1.3–1.7 ms。

### 裁边与缩放的两难

```
375 pt × 3 = 1125 px   ← UIKit 必须画 1125 宽
面板只有 1080 宽        ← 必须缩放或裁切
```

没有第三条路。`fbwidth=1080` + `display-scale=3` 会让 UIKit 算出 `1080 / 3 = 360 pt`，
而 iPhone X 是 375 pt，360 pt 没有对应布局，UIKit 弹出 `rdar://45025538` 红条；
触摸驱动按 1125 宽映射坐标，也会系统性偏移。**这条路数学上必然失败。**

`fbfit=3` 用裁边换 1:1 拷贝（文字最锐利），代价是左右各裁 22 px（4%），
画面被切成硬边矩形，与面板物理圆角冲突。

`fb_filter=0` 用缩放换完整画面，圆角、触摸、键盘全部正常，代价是轻度锯齿。
当前采用这一条。

## 四、零撕裂：`fb_flip` 的三次失败与最终解法

### 症状与成因

往正在被扫描输出的显存里 `memcpy`，会看到半帧新、半帧旧的横条带：

```
a frame copied into it while it is read shows on the panel as part this frame,
part the last -- in bands, the copy being split between fb_threads processors
```

`fb_threads=3` 把一帧拆给三个处理器并行写，各写一段横条，Ovl 同时在扫，
于是「某一块区域优先滑过去，其他区域跟着」—— 正是分块拖影的来源。

官方解法是双缓冲（`fb_flip=1`）：画进第二块，画完再让引擎指过去。

### 三次失败

1. **`ovl0` 读回 0**（boot-1013/1014）：`ovl0` 不是驱动本面板的管道。
2. **`kmem_alloc` 失败导致 panic**（boot-1015）：
   `ml_io_map_wcomb` 从 `io_submap` 分配，而 `io_submap` 只有 8 MB
   （`osfmk/arm/io_map.c:65`）；一张图 10.24 MB，必然 `KERN_NO_SPACE`。
3. **`ovlPointAt()` 不能在每帧调用**：它每次映射 12 个块且从不 unmap，
   100 fps 下每秒泄漏 1200 次 `io_submap` 映射；而且会把 `ovl0`/`ovl1` 一起平移。

### 最终实现

- **平面**：`flipStart()` 里一次性映射 `ovl4`（`0x14004000`）与 `ovl6`（`0x14404000`），
  读回两者地址，确认左片等于 `vinfo.v_physaddr`、右片高 `0x870`，记下这个间距。
- **第二块缓冲**：放在 `scanned + 2 * picture`（`0xff5be000`），而不是 `+ 1` ——
  因为另一条管道的 `ovl0 l1` 读 `0xfeb88000` 共 `0xa21c00` 字节，
  正好落在第一张图的后半段，`+1` 会被它盖掉。`+2` 起点在它之后，终点仍在 carve-out 内。
- **映射**：用 `IOMemoryDescriptor::withPhysicalAddress` + `createMappingInTask(...)` 配
  `kIOMapWriteCombineCache`。这条路从 `kernel_map` 出，是 write-combining，
  失败只返回 `NULL` 不会 panic。
- **切屏**：每帧只写两个寄存器，两侧各一次 `__builtin_arm_dsb(0xf)`，
  保证 write-combining 的像素在引擎读到之前已经落到 DRAM。

日志确认：

```
MiniIOMobileFramebuffer: fb_flip: two pictures, at 0xfe14e000 and 0xff5be000 (ovl4/ovl6, 0x870 apart)
```

### 结果

峰值 126 fps（面板 144 Hz 的 88%），滑动中位 110 fps，撕裂完全消失。
后期掉到 71–87 fps 时 `driver us` 同步涨到 3.5–4.2 ms，是热降频而非渲染问题。

## 五、当前 boot-args

```
cpus=8 cpufreq_mhz=2850 mali_gpu=1 mali_opp=0 fb_hz=144 ca_vbl=1
fb_queue=3 fb_filter=0 fb_flip=1
```

- `fb_queue=3`：让下一帧的 GPU 工作与前一周期的 beat 重叠（默认 2 会排队）。
- `fb_filter=0`：最近邻缩放，跳过双线性插值。
- `fb_flip=1`：双缓冲，消除撕裂。
- 不含 `fbwidth` / `fbheight` / `fbfit` / `glframes` / `glslow` / `applog`。

## 六、已知未解

- **Siri 长按**：`assistantd` 二进制不在镜像里（`sysimage/select-system.py` 的
  `SKIP_SYSTEM_LIBRARY` 与 `BIG_PROGRAM` 裁剪掉了它），长按会被正确转发但无人响应。
- **中文界面中英混杂**：同一脚本的 `KEEP_LANG = ('en', 'Base', 'English', 'en_US', 'en_GB')`
  裁掉了所有 `zh_CN.lproj` / `zh-Hans.lproj`；`.loctable` 里其实有完整中文，
  但 CoreFoundation 靠 bundle 里的空 `.lproj` 目录判断支持哪些语言。
- **剩余 18 fps**：126 / 144。瓶颈已不在显示驱动（`waited` 接近 0），
  而在彩色 4 层 HEIC 壁纸带来的 CA/GL 负载。
