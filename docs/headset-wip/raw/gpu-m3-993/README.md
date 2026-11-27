# M3：HakiosGPU panthor 服务（boot-993）

2026-11-27。M3 内核侧完成，服务启动成功，但 `panthor_test` 有一个匹配方式 bug
导致找不到服务。**未 PASS，留待下一轮。**

## 一、本轮成果

### 编译通过

- 新增 `iokit/Kernel/hakios.mali610.cpp`（IOKit 用户客户端）
- `conf/files` 已有 `optional hakios_mali610 iokitcpp`
- `.config` 里 `-DHAKIOS_MALI610` 和 `-DIOKITCPP` 都在
- **0 error / 0 warning**，内核 15549936 B

### 服务启动成功

```
mali: gpueb: POWER_ON answered 1, a reference is held for this run
mali: gpu_id after the GPUEB request: 0xa8670004 (csf 0x40a0412)
mali: mali_gpu=1: leaving CSG slots and AS1..AS7 clean for HakiosGPU
HakiosGPU: Mali-G610 panthor service started
```

## 二、阻塞问题：panthor_test 找不到服务

### 现象

```
panthor_test: waiting for HakiosGPU service (rounds=10)...
panthor_test: FAIL: HakiosGPU service not found (is mali_gpu=1 set?)
```

### 根因（审计发现）

驱动和测试用的匹配方式不一致：

| 侧 | 代码 | 匹配什么 |
|---|---|---|
| 驱动 `hakios.mali610.cpp:633` | `setName("HakiosGPU")` | `IORegistryEntry` 的 name |
| 测试 `panthor_test.c:382` | `IOServiceMatching("HakiosGPU")` | `IOService` 的 class |

SDK 文档（`IOKitLib.h:1322`）：

> `IOServiceMatching` — Create a matching dictionary that specifies an IOService
> **class** match. Class matching is successful on IOService's of this class or
> any subclass.

我们的类名是 `HakiosMali610`（`OSDefineMetaClassAndStructors(HakiosMali610, IOService)`），
`setName` 不会改类名。所以 `IOServiceMatching("HakiosGPU")` 永远匹配不到。

### 修复方案

A. 改测试（推荐）—— 一行：

```c
/* panthor_test.c:382 */
service = IOServiceGetMatchingService(0, IOServiceNameMatching("HakiosGPU"));
```

`IOServiceNameMatching`（`IOKitLib.h:1332`）：
> Create a matching dictionary that specifies an IOService **name** match.

B. 改驱动 —— 在 `start()` 里加：

```c
setProperty("IOClass", "HakiosGPU");
setName("HakiosGPU");
registerService();
```

A 成本更低：不用重编内核，只需重编 `panthor_test` + 重打 `root.apfs`。

## 三、Neon 终审结论（架构把关）

### 独立核验通过

- 双份同步 `cmp` 全部 OK：
  `hakios.mali610.c` (ea7d3fc1...) / `hakios.mali610.cpp` (a886b0d3...) /
  `hakios.stubs.cpp` (a4306124...)
- 独立跑 `xnu-clang` / `xnu-clang++` `-fsyntax-only`：全部 0 error / 0 warning
- CSF ring 编码、tiler heap 链表头、`gShared` 区间、MT6895 `be527d1` TILER_OOM
  勘误、RETURN 停机链路全部核对无误

### 1 个边界隐患

`vmDestroy()` 的 `fGroups[]` 清理循环目前为空（`hakios.mali610.cpp:1230-1236`）：

> 若客户端异常先 `VM_DESTROY` 再 `GROUP_DESTROY`，底层已释放 `csg_slot`
> 但 `fGroups[g]` 仍残留旧 `hw_group`，后续同 slot 复用会串号。
> 补一个 `fGroupVms[]` 数组在 `vmDestroy()` 同步清零即可。

### 2 个非阻塞优化（M4a 可理）

1. `hakios.mali610.c:7919` —— `ep_req` bit `[31:28]` 填的是 `csg_slot` 而非 `args->priority`
2. `groupSubmit()` 与 `queue_submit()` 各 bump 一次 `gMaliLatestFlushId`（纯递增无副作用）

## 四、Carbon 终审结论（实现 review）

### 独立核验通过

- 三份文件 `cmp` 一致；`mt6895/drivers/gpu/conf`、`iokit/conf/files`、
  `boot-args.lst`、`userland/build.sh`、`obj/panthor_test` 均就位
- 独立 `-fsyntax-only` 全 PASS，`-DHAKIOS_MALI610` 确认生效
- 硬件/协议逐项核对：
  - `CS_FEATURES_WORK_REGS((x&0xff)+1)`
  - `CS_FEATURES_SCOREBOARDS`
  - ring 指令编码对齐 `panthor_sched.c:3196-3249`
  - tiler heap chunk 链表头对齐 `panthor_heap.c:178`
  - `gShared [0x58000, 0x94000)` 60 页区间
  - MT6895 `be527d1` CS_TILER_OOM 镜像坑
  - 三层单向锁序、`hakios_mali610_stop()` RETURN 闭环

### 与 Neon 一致的隐患

同一个 `vmDestroy()` 的 `fGroups[]` 空循环（Carbon 也独立发现）。

## 五、编译时的关键坑

必须触发 XNU config 重生成 `iokit/DEVELOPMENT/Makefile`。

新增了 `iokit/Kernel/hakios.mali610.cpp`（`iokitcpp`），旧 Makefile 会漏编
`hakios.mali610.cpo`，导致链接缺 `_HakiosMali610Start`。

操作：

```bash
rm -f BUILD/obj/.../iokit/DEVELOPMENT/Makefile
rm -f BUILD/obj/.../iokit/DEVELOPMENT/makedeps.late
# 编译，Makefile 会在 60 秒内重新生成
```

验证：

```bash
grep -c "hakios.mali610.cpp" BUILD/obj/.../iokit/DEVELOPMENT/Makefile   # 2
ls BUILD/obj/.../iokit/DEVELOPMENT/hakios.mali610.cpo                  # 存在
```

## 六、本轮镜像

```
boot-993     sha256 ea316c700d32821081603936f32a0d1a1f9125fbb7bee25c81c1daef7165feee
boot-args    在 boot-992 基础上：去掉 mali_m2c=2，加 mali_gpu=1
ramdisk      含 /redwood/bin/panthor_test（mode 100755，50576 B）
```

兜底：

```
golden/boot-992-compute-pass.img  sha256 91ee276c...  (Task 2 PASS)
golden/boot-991-m2c-pass.img      sha256 beb5e96f...  (M2c PASS)
golden/boot-985b-golden.img       UI 基线
```

## 七、下一轮要做

1. 修 `panthor_test` 的匹配方式（`IOServiceMatching` -> `IOServiceNameMatching`）
2. 重编 `panthor_test` + 重打 `root.apfs` + 刷 `boot-994`
3. 跑 `panthor_test`，验收：
   - 10/10 轮 `compute=16/16`
   - `magic` 低字 `0xdeadbeef`
   - `gstate=0x0` / `vstate=0x0`
   - `panthor_test: PASS (gpu_id=0xa8670004, seq=10, wrote=0xdeadbeef)`
   - 0 次 MMU/GPU/CS fault
   - RETURN < 40s
4. 顺手补 `vmDestroy()` 的 `fGroups[]` 清理（5 行）

## 八、教训

`setName` 和 `IOServiceMatching` 是两套命名空间。

`setName` 设置的是 `IORegistryEntry` 的 name（`kIONameKey`），
`IOServiceMatching` 匹配的是 class（`IOClass` 属性）。

要按名字查找，必须用 `IOServiceNameMatching`。

**这是审计时才发现的 —— 编译通过、服务启动、日志正常，但测试就是找不到。**
