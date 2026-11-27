# M4a PASS：Mesa panfrost 在 Mali-G610 上画出三角形（boot-995）

2026-11-27。Mesa 25.0.7 的 panfrost（Panthor CSF）驱动在 xaga 上第一次真正画出图。

## 一、实机输出

```
gl_test: Mesa; Mali-G610 (Panfrost); OpenGL ES 3.1 Mesa 25.0.7
gl_test: 1080 x 2400 drawn in 285 ms, read back in 222 ms; error 0;
         a corner          26  26  51 255
         the middle        68  69 118 255
         near the red     235  12   8 255
         near the green    12 235   8 255
         near the blue     10  10 235 255
         673920 of 2592000 pixels inside the triangle (26%; 26% by geometry)
gl_test: the picture is on the panel (0), for 8 seconds
```

GL_RENDERER 是 Mali-G610 (Panfrost) —— 硬件渲染，不是 softpipe。

## 二、判据对照（契约 6）

| 判据 | 要求 | 实测 |
|---|---|---|
| GL_RENDERER | 含 Mali-G610 (Panfrost) | Mali-G610 (Panfrost) |
| 非 softpipe | 严禁回退 | 无 softpipe |
| error | 0x0 | error 0 |
| 四角背景 | 深蓝 ~26 26 51 255 | 26 26 51 255 精确 |
| 三顶点渐变 | 红/绿/蓝 | 235 12 8 / 12 235 8 / 10 10 235 |
| 覆盖率 | 25-27% | 26%（几何 26%） |
| 上屏 | picture is on the panel | (0), for 8 seconds |
| 0 fault | GPU/MMU/CS | gpu_fault 0 / 0 MMU / 0 CS_FATAL / 0 syncobj timeout |
| RETURN | < 40s | 30 秒 |

绘制 285 ms，读回 222 ms。

## 三、主机侧自检

```
libOSMesa.8.dylib   14825700 B  arm64 Mach-O
install_name        /redwood/lib/libOSMesa.8.dylib
未解析 drm/hakios 符号   0
Mali-G610/panthor/Panfrost 命中  11 次
内核未被动          c:ea7d3fc1 / cpp:1db9d925
freedreno-darwin.patch  365 行完好
```

DRIVERS="softpipe,panfrost" tools/mk-mesa.sh 全新解包 + 8 个文件 patch
全部干净应用，0 fuzz / 0 hunk fail。

## 四、改了什么

### tools/mesa/hakios-drm/（Carbon）

- xf86drm.h 4377 B —— 补齐 Mesa panfrost/panthor_kmod 依赖的 libdrm 声明
- hakios.drm.c 12574 B —— 支持 panthor 打开、版本上报、全套 drmSyncobj*、
  drmCloseBufferHandle、时钟换算、IOServiceNameMatching

### tools/mesa/panfrost-darwin.patch（Carbon，235 行，新增）

改了 8 个文件：

```
meson.build
src/panfrost/lib/kmod/meson.build
src/panfrost/lib/kmod/pan_kmod.h
src/panfrost/lib/kmod/panthor_kmod.c      LATEST_FLUSH_ID 映射/解映射
src/panfrost/lib/kmod/pan_kmod.c          BO CPU 映射 + IOSurface 导入预留
src/gallium/drivers/panfrost/pan_bo.c     BO CPU 解映射
src/gallium/targets/osmesa/meson.build
src/gallium/targets/osmesa/osmesa_target.c
```

按字母序排在最后应用（air-frontend / air-ir3 / freedreno-darwin /
ir3-round-default / osmesa-es-profile / panfrost-darwin）。

### userland/src/gl_test.c（Carbon）

352 行 IOServiceMatching("HakiosGPU") -> IOServiceNameMatching("HakiosGPU")。

和 M3 里 panthor_test 是同一个 bug —— setName 设的是 IORegistryEntry 的 name，
IOServiceMatching 匹配的是 class（HakiosMali610）。

### tools/mk-mesa.sh（Carbon 微调）

支持 DRIVERS="softpipe,panfrost"，-Dfreedreno-kmds=msm 不报错。

## 五、镜像

```
golden/boot-995-m4a-pass.img
  sha256 04361d8b41c028c25bd1985c21590e43ea581788bd7f8599cacf41eeee3e8ca9
  27181056 B
  内核沿用 boot-994（mali_gpu=1）
  ramdisk 增加 /bin/gl_test（52616 B）+ /lib/libOSMesa.8.dylib（14825700 B）
```

## 六、额度池优化（本轮建立的工作模式）

操作者提醒 Neon 和 Carbon 共用同一个 Gemini 额度池。流程改了：

```
原来：Carbon 自检 5 项 -> Neon Review -> Neon 放行 -> DSH 刷机     （烧 2 次）
现在：Carbon 实现 -> DSH 跑机械自检 -> DSH 刷机 -> 出问题才叫 Neon （烧 1 次）
```

契约 5 的 5 项里，4 项是纯机械的（otool / nm / strings / md5sum），
DSH 30 秒跑完，不烧额度。这次实测证明可行 —— 契约足够精确，
Carbon 照着写一次通过，不需要中间 review。

## 七、观察：285 ms 偏慢

1080x2400 单三角形本应 < 50 ms。可能原因：

- 首次提交有初始化开销
- pan_bo.c 的 CPU 映射是非缓存/写合并
- 每次 glReadPixels 强制 flush + 等待

这个数字在 M5 会成瓶颈（60 fps = 16.7 ms/帧）。M4b 需要测稳态帧时间。

## 八、下一步：M4b

- CS_TILER_OOM 动态 chunk 扩容
- MiniIOSurface 零拷贝（kHakiosImportSurface）
- 100 帧模糊压测
- 新增：稳态帧时间测量
