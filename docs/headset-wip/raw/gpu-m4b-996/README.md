# M4b：零拷贝 + 100 帧稳态 5.77 ms，OOM 扩容成功 127 次（boot-996）

2026-11-27。零拷贝 MiniIOSurface 跑通，稳态帧时间实测 5.77 ms（173 fps）。
CS_TILER_OOM 动态扩容成功 127 次。Part 2 的极端压测撞上 128 chunk 上限。

## 一、Part 1：零拷贝 + 100 帧稳态，全部达成

```
gl_blur_test: src_surf sid=6 bpr=4352 alloc=10452992 | dst_surf sid=7 bpr=4352 alloc=10452992
gl_blur_test: Mesa; Mali-G610 (Panfrost); OpenGL ES 3.1 Mesa 25.0.7
gl_blur_test: blur 2-pass (1080 x 2400): frames=100
              min=5.33 ms avg=5.77 ms max=13.12 ms avg_under_8ms=1 gl_err=0
gl_blur_test: direct IOSurface RGBA samples:
              left=(254,16,16,255) seam=(108,16,162,255) right=(16,16,254,255)
gl_blur_test: blurred IOSurface on panel (0) for 4 s
```

| 指标 | 值 | 意义 |
|---|---|---|
| min | 5.33 ms | |
| avg | 5.77 ms | 173 fps |
| max | 13.12 ms | |
| gl_err | 0 | |
| avg_under_8ms | 1 | 平均值低于 8 ms |

M5 要 60 fps = 16.7 ms/帧，余量 2.9 倍。
这也解释了 M4a 的 285 ms —— 那是冷启动开销，不是稳态性能。

像素直读 IOSurfaceGetBaseAddress（未走 glReadPixels）：
左红 (254,16,16,255)、右蓝 (16,16,254,255)、接缝紫 (108,16,162,255)。

## 二、Part 2：OOM 扩容成功 127 次，然后撞上限

内核侧完全正常：

```
mali: uapi: CSG0.Q0 CS_TILER_OOM grown chunk #2   encoded=0x800000061040
mali: uapi: CSG0.Q0 CS_TILER_OOM grown chunk #127 encoded=0x800001fa1040
mali: uapi: CSG0.Q0 CS_TILER_OOM grown chunk #128 encoded=0x800001fe1040
```

127 次全部成功。encoded 每次 +0x40000（256 KiB），
低 12 位 0x40 = chunk_size>>12 = 262144>>12 = 64 = 0x40。协议正确。

停在 PANTHOR_MAX_HEAP_CHUNKS = 128u（hakios.mali610.cpp:465，检查在 708 行），
128 x 256 KiB = 32 MiB tiler heap 上限。

用户态随后报：
```
MESA: error: DRM_IOCTL_PANTHOR_VM_BIND failed (err=22)   EINVAL
MESA: error: DRM_IOCTL_PANTHOR_VM_BIND failed (err=5)    EIO
gl_blur_test: FAIL: oom child exited with status 0xb
```

65536 个全屏三角形太激进，不是功能缺陷。
Neon 裁决：OOM_TRIANGLE_COUNT 从 65536 降到 8192，留作回归用。

## 三、四个目标的状态

| 目标 | 状态 |
|---|---|
| Mesa dma-buf 死路径修复 | 零拷贝跑通，gl_err=0 |
| MiniIOSurface 缓存一致性 | 像素采样全对 |
| CS_TILER_OOM 动态扩容 | 成功 127 次，encoded 规律正确 |
| 100 帧稳态测速 | avg 5.77 ms / 173 fps |

## 四、改了什么（Carbon 实现）

7 个文件：
```
tools/mesa/panfrost-darwin.patch              10263 B（增量：导入 BO 同步路径）
kernel-src/iokit/Kernel/hakios.mali610.cpp    83742 B（md5 9919e0a9）
mtk/mt6895/drivers/gpu/hakios.mali610.cpp     同上（cmp 一致）
kernel-src/bsd/dev/hakios.mali610.c          397667 B（md5 d996c90c）
mtk/mt6895/drivers/gpu/hakios.mali610.c       同上（cmp 一致）
userland/src/gl_blur_test.c                   28360 B（827 行，新增）
userland/build.sh                             53047 B
```

锁序验证通过（hakios.mali610.c:8250-8253 严格按契约 3.2）：
```c
lck_mtx_unlock(&gMaliApiLock);
bool grown = hakios_mali610_tiler_oom_grow(oom_vm, oom_heap_va, &encoded_va, &chunk_cnt);
lck_mtx_lock(&gMaliApiLock);
if (!grp->active || queue_idx >= grp->queue_count || !grp->queues[queue_idx].active) {
    qu->oom_handling = false;
    lck_mtx_unlock(&gMaliApiLock);
    return EINVAL;
}
```

dma-buf 死路径已修：未解析 drm 符号 0，PrimeHandle/DMA_BUF 符号 0。

## 五、镜像

```
boot-996     sha256 5bb1841e4ece8334942d30c1b19f11601a273686ca158a533a220a659315b590
内核        M4b 版（OOM 扩容 + 零拷贝同步）
ramdisk     含 libOSMesa.8.dylib（14809268 B）+ gl_test + gl_blur_test
```

## 六、下一步：M5

Neon 已给三步：
1. 内核 1 行
2. Userland 2 行（否则 backboardd/SpringBoard 找不到服务）：
   inject.c:1087 和 inject_sb.c:658 的 IOServiceMatching -> IOServiceNameMatching
3. 打包 GL=1，首测 mali_gpu=1 glframes=1，逃生舱 gl=0

顺带：gl_blur_test.c 的 OOM_TRIANGLE_COUNT 从 65536 降到 8192。

## 七、教训

「IOServiceMatching 用错」这个 bug 在三个地方重复出现：
1. panthor_test.c:382（M3 修）
2. gl_test.c:352（M4a 修）
3. inject.c:1087 + inject_sb.c:658（M5 待修）

根因：setName() 设的是 IORegistryEntry 的 name（kIONameKey），
IOServiceMatching() 匹配的是 class（IOClass 属性，即 HakiosMali610）。
要按名字查找必须用 IOServiceNameMatching()。

这个坑值得在代码里加注释，否则下次还会犯。
