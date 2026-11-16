# RETURN-ANDROID-FOLLOWUP-713.md 已更新：loader 清理确实存在

> 2026-11-17 01:52，DSH。**原文已被本轮更新版替换。**

## 版本对比

| 项 | 旧版 | 新版 |
|---|---|---|
| 大小 | 3555 B | **4692 B** |
| SHA256 | `fa20fd4fbcea7fcc…` | **`f68b46ddd75cba02b203f21f2ef77519d9397fe98cef67f0c1f00d0a124f850d`** |

## 新增的纠正（新版末节 "Further trace correction: loader cleanup exists"）

**原文要点：**

> 「A subsequent focused read found `xnu_returned` at approximately line 2584 explicitly calls
> **`mali_power_down` under `BOARD_MALI_POWER`**, before `chain_android`. That macro was **1 in 713**.
> The implementation at approximately line 6885 attempts domain shutdown and AOC isolation.
> **The kernel lacks a named Mali610 quiesce hook, but this does not establish absence of cleanup
> across the complete handover path.**」

## 我独立核实的部分

**在 `trial715/loader-src/mtk/loader/mtkloader.c` 里：**

**第 2580-2588 行（`xnu_returned` 路径）：**

```c
#if BOARD_MALI_POWER
	/*
	 * Before Android, not after: a domain left raised is why every earlier run came back to
	 * this image instead of to adb. See mali_power_down().
	 */
	mali_power_down();
#endif
	out("starting Android, so that the log above can be pulled\n");
	chain_android();
```

**→ 确认：`mali_power_down()` 在 `BOARD_MALI_POWER` 守护下、`chain_android()` **之前**被调用。**

**第 6839-6923 行：`#if BOARD_MALI_POWER` 守护的完整实现区块，含 `mali_aoc_down()`。**

## 这条纠正推翻了我什么

**我早先（在 kernel-hook-only 的检查里）写过「内核没有 Mali610 的 quiesce 钩子」，
并把它当成「没有 GPU 清理」的证据。**

**🔴 那是错的。** 清理在 **loader** 里，不在内核里。

**正确表述：**

| 项 | 正确 |
|---|---|
| 内核有无命名钩子 | 无 |
| **loader 有无清理** | **有**（`mali_power_down` under `BOARD_MALI_POWER`）|
| **「无钩子」能否推出「无清理」** | **不能** |
| **713 返回事件成因** | **仍未证明** |

**按 Codex 要求：缺少钩子既不证明没有清理，也不证明 713 返回事件的成因。**

## 与 715 的关系

**715 把这五个宏全设为 0，因此 **`mali_power_down()` 那段被完全移除**。**

**这是有意的基线变更，不是「修好了 713 的问题」。**
