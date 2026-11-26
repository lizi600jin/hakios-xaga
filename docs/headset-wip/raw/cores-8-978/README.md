# 8 核：从 4 核到 8 核，返回仍然是 32 秒

2026-11-26，`boot-978b`。

## 结果

```
19.389813  GenericARMPE: platform "D22AP"

19.390914  cpu 0 (phys id 0x0) is a slower core: the scheduler uses it after cpu 4 (phys id 0x700)
19.391302  cpu 5 (phys id 0x100) is a slower core: the scheduler uses it after cpu 3 (phys id 0x600)
19.391691  cpu 6 (phys id 0x200) is a slower core: the scheduler uses it after cpu 2 (phys id 0x500)
19.392080  cpu 7 (phys id 0x300) is a slower core: the scheduler uses it after cpu 1 (phys id 0x400)

19.418239  GenericARMPE: 5 of 8 processors are running
47.424871  GenericARMPE: the slower cores started too: 8 processors running
```

**28 秒后 8 核全齐。** 前 5 个是核心 0（A55 启动核）+ 四颗 A78（4/5/6/7），
后 3 个是剩下的 A55（核心 1/2/3）。

`cpu 0` 那条看起来矛盾——启动核被列成 slower core——其实是对的：
设备树里 `cpu0` 节点的 `core_order[0] = 0`，而核心 0 是 A55。

## 为什么要 28 秒

`GenericARMPE.cpp:262` 的注释解释了：

> The slower cores ... are started 28 seconds later (boot-arg `small_cores_after=S`).
> With all seven from the start the lock screen came 1.5 s later than with the four big
> ones alone, while that work as a whole was done earlier. With the slower cores joining
> once the lock screen is there: 25.3 s and 36 s.

**开机快 1.5 秒，整体完成反而更早 2 秒。** 这个默认值是调过的，别动。

## 验证

| 项 | 结果 |
|---|---|
| 核数 | **8**（`8 processors running`） |
| GPU | M1 + M2b PASS |
| 背光 | `/dev/hakios-backlight ready` |
| 电池 | 16 条，间隔严格 10.0 秒 |
| **RETURN** | **32 秒** |

**8 核 RETURN 正常** —— 这是最需要验的一项，因为要 7 个从核全部下线。

## 两个真正的 bug，都在 loader 里

外部规划文档说「`XNU_CORES=8` 直接改就行」。实际上直接改会编译失败：

### bug 1：`_Static_assert(XNU_CORES <= 7)`

`mtkloader.c` 里写死了这个断言，因为原来假定有一个核要留给 watcher。
但我们的 `XNU_WATCH_SECONDS` 一直是 0，根本没有 watcher。

改成：`XNU_WATCH_SECONDS == 0` 时允许 `XNU_CORES <= 8`，
`WATCH_CORE` 落回 `core_order[7]`。

### bug 2：核序表把一颗 A78 放错了位置

```
原：{ 0, 4, 5, 7, 1, 2, 3, 6 }    ← 核心 6（A78，MPIDR 0x600）在最后
新：{ 0, 4, 5, 6, 7, 1, 2, 3 }    ← 四颗 A78 紧跟启动核
```

注释把核心 6 当成 A55 了。设备树里写得很清楚：

```
cpu@000 a55 reg=0x00    cpu@100 a78 reg=0x400
cpu@001 a55 reg=0x100    cpu@101 a78 reg=0x500
cpu@002 a55 reg=0x200    cpu@102 a78 reg=0x600
cpu@003 a55 reg=0x300    cpu@200 a78 reg=0x700
```

**A55 是 0x00/0x100/0x200/0x300（核心 0–3），A78 是 0x400–0x700（核心 4–7）。**

放错的后果：`cpus=5` 时第五个核会是核心 1（A55）而不是核心 6（A78），
而且 `BOARD_SMALL_CORE` 的判断会错。

## 还有一个注释不一致（不影响运行）

`board.h:62` 写 `BOARD_SMALL_CORE(core) ((core) < 4)`，正确。
`mtkloader.c:119` 的 fallback 还是旧注释 `cores 0-5 are the A55s, 6 and 7 the A75s`，
但因为 `BOARD_HEADER` 已经包含了 board.h，那个 `#ifndef` 分支永远不生效。

**注释留着会误导下一个接手的人**，应该改掉。

## 关于外部文档的另一个说法

它说 8 核的代价是「失去自动返回」，并给了三个补偿方案
（SPM 定时唤醒 / 外挂 WDT / 降级 7 核）。

**那个代价不存在。** 我们的 `XNU_WATCH_SECONDS` 一直是 0，自动返回靠内核的
`thread_call`（`loader_return.c:504`），不占核。8 核 RETURN 实测 32 秒。

## 镜像

- `golden/boot-977-4core-battery-sysbl.img` —— 4 核黄金基线（背光滑条实测可用）
- `trial736/boot-978b.img` —— **8 核**，全部验证通过

## 下一步（Neon 排的序）

1. `pack_xaga.py` 可复现 hash
2. UFS `0x112b0000` 接管队列
3. GPU M2c / 外放 / WiFi
