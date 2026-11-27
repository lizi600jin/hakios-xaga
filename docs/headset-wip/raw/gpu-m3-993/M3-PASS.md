# M3 PASS：panthor UAPI 全线跑通（boot-994）

2026-11-27。M3 完成，panthor_test 10/10 轮通过。

## 一、结果

```
panthor_test: gpu_id=0xa8670004 csf_id=0x40a0412 shader_present=0x130013
              tiler_present=0x1 as_present=0xff mmu=0x2830
panthor_test: csif csg=4 cs=8 regs=96 sb=8 unpreserved=4 | ts_freq=13000000
panthor_test: LATEST_FLUSH_ID mapped at 0x100f80000 (16384 B), initial=1

round  1/10: vm=1 magic=0x1deadbeef cycles=0xa45671d compute=16/16 flush=14->16  gstate=0 vstate=0
round  2/10: vm=1 magic=0x2deadbeef cycles=0xa49dee7 compute=16/16 flush=43->45  gstate=0 vstate=0
round  3/10: vm=1 magic=0x3deadbeef cycles=0xa4d5800 compute=16/16 flush=72->74  gstate=0 vstate=0
round  4/10: vm=1 magic=0x4deadbeef cycles=0xa50d61c compute=16/16 flush=101->103 gstate=0 vstate=0
round  5/10: vm=1 magic=0x5deadbeef cycles=0xa544e37 compute=16/16 flush=130->132 gstate=0 vstate=0
round  6/10: vm=1 magic=0x6deadbeef cycles=0xa57eaab compute=16/16 flush=159->161 gstate=0 vstate=0
round  7/10: vm=1 magic=0x7deadbeef cycles=0xa5b984a compute=16/16 flush=188->190 gstate=0 vstate=0
round  8/10: vm=1 magic=0x8deadbeef cycles=0xa5f0b75 compute=16/16 flush=217->219 gstate=0 vstate=0
round  9/10: vm=1 magic=0x9deadbeef cycles=0xa628b99 compute=16/16 flush=246->248 gstate=0 vstate=0
round 10/10: vm=1 magic=0xadeadbeef cycles=0xa660174 compute=16/16 flush=275->277 gstate=0 vstate=0

panthor_test: PASS (gpu_id=0xa8670004, seq=10, wrote=0xdeadbeef)
```

RETURN 30 秒，0 fault。

## 二、修的两处（boot-993 -> boot-994）

### 1. IOServiceMatching -> IOServiceNameMatching

boot-993 服务启动了但测试找不到。

驱动 hakios.mali610.cpp:633 用 setName("HakiosGPU") 设的是 IORegistryEntry 的 name，
测试 panthor_test.c:382 用 IOServiceMatching("HakiosGPU") 匹配的是 IOService 的 class。

SDK 文档 IOKitLib.h:1322：

> IOServiceMatching — Create a matching dictionary that specifies an IOService
> **class** match.

我们的类名是 HakiosMali610，setName 不改类名，所以永远匹配不到。

改成 IOServiceNameMatching("HakiosGPU")（IOKitLib.h:1332 定义）。

### 2. vmDestroy() 的 fGroupVms 清理

hakios.mali610.cpp:1230 原来 fGroups[] 清理循环为空。

补上 uint32_t fGroupVms[PANTHOR_MAX_CLIENT_GROUPS]，在 vmDestroy() 里
把属于 req->id 的 fGroups[g] 一并 groupDestroy/清零，与 fHeaps 清理对齐。

这个隐患 Neon 和 Carbon 各自独立发现，说明它确实是个真问题。

## 三、panthor_test 输出的含义

| 字段 | 值 | 含义 |
|---|---|---|
| gpu_id | 0xa8670004 | Mali-G610 MC6 |
| csf_id | 0x40a0412 | CSF 固件版本 |
| shader_present | 0x130013 | 6 个 Shader Core（低 16 位 3 + 高 16 位 3） |
| tiler_present | 0x1 | 1 个 tiler |
| as_present | 0xff | 8 个地址空间全在线 |
| mmu | 0x2830 | MMU 配置 |
| csg / cs | 4 / 8 | 4 个命令流组，8 个命令流 |
| regs | 96 | 每组 96 个寄存器 |
| sb | 8 | 8 个 scoreboard |
| ts_freq | 13000000 | 时间戳频率 13 MHz |

LATEST_FLUSH_ID 映射到 0x100f80000，16384 B，初值 1。
每轮 flush 单调递增，说明 FLUSH_CACHE2 和 gMaliLatestFlushId 在工作。

## 四、走通的完整链路

```
用户态 panthor_test
  -> IOServiceNameMatching("HakiosGPU")
  -> IOServiceOpen
  -> IOConnectCallStructMethod(12 个 ioctl)
      DEV_QUERY        -> gpu_id/csif/shaders/tiler/mmU
      VM_CREATE        -> as_id=1
      BO_CREATE        -> 分配 buffer
      BO_MMAP_OFFSET   -> mmap 到用户态
      VM_BIND          -> 映射 4 KiB 页
      GROUP_CREATE     -> CSG 槽位
      GROUP_SUBMIT     -> CSF ring 提交
      SYNC_OBJ_*       -> 7 个 syncobj 操作
  -> mmap(USER_FLUSH_ID_MMIO_OFFSET) -> 拿到 16384 B 只读页
  -> GPU 执行 Valhall compute shader
  -> 读回 16 个 u32，全部 0xNdeadbeef
```

## 五、镜像

```
golden/boot-994-m3-pass.img
  sha256 fe54c9b09a743dfd4d8a5b0ab26bb9de9097ebd38700d2b8c45b370237b8098c
  23314432 B
  boot-args: mali_gpu=1（其余同 boot-992）
  ramdisk:   含 /bin/panthor_test（100755，50584 B）
```

## 六、下一步：M4a

Mesa panfrost 跨编译 + gl_test 实机画三角形。

1. 扩展 tools/mesa/hakios-drm（用户态 DRM shim）
2. 写 panfrost-darwin.patch（参考 freedreno-darwin.patch 365 行）
3. 编 DRIVERS="softpipe,panfrost" 的 libOSMesa.8.dylib
4. gl_test 跑通：GL_RENDERER = Mali-G610 (Panfrost) + 屏幕出三角形

Mesa 25.0.7 源码在 xnu-deps/build/mesa/mesa-25.0.7/，工具链齐
（meson 1.12.1 / ninja / bison / flex / llvm-ar）。

## 七、教训

`setName` 和 `IOServiceMatching` 是两套命名空间。

setName 设的是 IORegistryEntry 的 name（kIONameKey），
IOServiceMatching 匹配的是 class（IOClass 属性）。

这在 boot-993 上表现为：编译通过、服务启动、日志正常，但测试就是找不到。
光看日志会以为驱动没问题 —— 必须两边对照才能发现。
