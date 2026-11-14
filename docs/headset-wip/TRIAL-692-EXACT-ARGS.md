# trial 692 的真实启动参数（从 loader 二进制提取，非备份文件）

> 提取来源：`docs/round-17-support/trial-692-loader-strings.txt`
> 提取方式：`strings -a <692 的 loader 二进制> | grep "^rd=md0 -v"`
> 日期：2026-11-14 22:08
>
> **为什么要单独存这份**：`codex/out/round-19-support/board.h.bak` 被误当成「692 那轮的 board.h」，
> 但它里面的 `XNU_RETURN_AFTER` 是 `70`、`XNU_EXTRA_ARGS` 是 `mali=6 mali_cold=0 mali_preas0=1`，
> **与 692 实际运行的完全不同**。判据必须从产物里取。

---

## 完整参数串

```
rd=md0 -v cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 \
fbwidth=1080 fbheight=2460 fb_disp=1 \
mali=5 mali_cold=1 mali_preas0=0 mali_low=0 mali_trace=1 mali_witness=0 \
mali_ptvendor=1 mali_scclk=1 mali_scupd=1 mali_sc26=0 mali_cacheflush=0 \
mali_resetonly=0 mali_cohnone=0 mali_hwissues=0 mali_stackpwron=0 \
mali_mcuwin=0 mali_mcuseq=0 mali_doorbell=0 mali_pdcsettle=0 mali_statussem=0 \
mali_assorder=1 mali_trial=692 \
loader_return_after=150
```

## 转成 `CONFIG_BOOTARGS`（去掉 `rd=md0`、`-v`、`cpus=`、`loader_return_after=`）

`cpus=4` 由 `CONFIG_CPUS=4` 负责，写进 BOOTARGS 会被 `hakios-config.py` 拒绝。
`loader_return_after` 是 loader 的 `-D` 宏，不在 `.config` 里。

```
CONFIG_BOOTARGS="buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=5 mali_cold=1 mali_preas0=0 mali_low=0 mali_trace=1 mali_witness=0 mali_ptvendor=1 mali_scclk=1 mali_scupd=1 mali_sc26=0 mali_cacheflush=0 mali_resetonly=0 mali_cohnone=0 mali_hwissues=0 mali_stackpwron=0 mali_mcuwin=0 mali_mcuseq=0 mali_doorbell=0 mali_pdcsettle=0 mali_statussem=0 mali_assorder=1 mali_trial=692"
```

`loader_return_after` 用默认值 `150`（`board.h:793` 已经是 150），不用传 `-D`。

## 与「我以为的 692」的差异

| 参数 | 692 真实 | `board.h.bak` 里写的 | 昨天第 4 次刷机用的 |
|---|---|---|---|
| `mali=` | **5** | 6 | 6 |
| `mali_cold` | **1** | 0 | 0 |
| `mali_preas0` | **0** | 1 | 1 |
| `mali_low` | 0 | — | 未传 |
| `mali_trace` | 1 | — | 未传 |
| `mali_witness` | 0 | — | 未传 |
| `mali_ptvendor` | 1 | — | 未传 |
| `mali_scclk` | 1 | — | 未传 |
| `mali_scupd` | 1 | — | 未传 |
| `mali_sc26` | 0 | — | 未传 |
| `mali_assorder` | 1 | — | 未传 |
| `mali_trial` | 692 | — | 未传 |
| **`loader_return_after`** | **150** | **70** | **70** |
| 参数总数 | 29 | 4 | 7 |

## 两个已确证的结论

1. **loader 源码与 692 是同一份**。692 的构建日志行号（`5764`/`5766`/`6755`/`7424`）
   与当前 `mtkloader.c` 逐条吻合，包括函数名 `mali_dump`、`mali_leave_powered` 和变量 `v`、`xnu_chosen`。
2. **已提交的 3453 行 loader 是 MT6768（Infinix）的**，不能用于 xaga
   （文件头第一句就写着 `Infinix Hot 20 (X6826B, MT6768)`，`BOARD_` 宏零引用）。
