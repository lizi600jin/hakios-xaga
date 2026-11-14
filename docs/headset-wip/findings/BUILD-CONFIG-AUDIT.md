# 构建配置完整性审计（xaga / MT6895）

审计对象：`/home/lizi/IOS/hakios-mtk`，`git HEAD 4ebad21`（2026-11-14 23:12:23 +0800），工作区 57 个改动。
审计方式：纯静态。未跑 `build.sh`，未跑 `make`，未 `stash`/`reset`/`checkout`。预处理临时文件全部在 `/tmp`。
所有结论都给了 `文件:行号`；凡是推不出的，标「未验证」。

配套工具：`docs/headset-wip/build-config-dump.sh`（只读；`sh -n` 与 `dash -n` 均通过，bash 与 dash 下输出一致，完整输出见附录 A）。

---

## 0. 结论摘要（先看这十条）

> **并发提示（重要）**：审计期间（00:30–00:55）该树正在被另一个 agent 编辑。已观察到两次变化：
> ① 00:46:15 `devices/xaga/configs/xaga.config` 被加了 `headset=1`；
> ② 00:54:01 `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c` 被重写（18232 字节、548 行，函数行号整体漂移）。
> 报告里凡引用这两个文件的行号，以「符号 + 当时的行号」给出；**结论（哪些参数生效、哪些不生效）不受影响**，因为它们依据的是文件内容与 `.config`，不是行号。
> `.config` 在审计结束时仍是 2026-11-14 02:48:43 那份（第 71 行无 `headset=`）——这一条是全文最关键的可执行结论。



| # | 结论 | 证据 |
|---|---|---|
| 1 | 决策链 **6 层**，其中「`.config` → loader」这一环**不存在** | `mtk/loader/build.sh:15-36` 只读 `DEVICE` → `devices/<dev>/device.conf` 的 `soc` → `mtk/<soc>/loader/board.h`；`.config` 一个字节都不读 |
| 2 | `devices/xaga/board.h` **不存在**；只有 `mtk/mt6895/loader/board.h`（48016 字节，未跟踪 A） | `find . -name board.h` 只有这一个；`devices/xaga/` 只有 `device.conf` + `configs/` |
| 3 | **`headset=1` 现在仍然对一次 `make phone` / `xaga-cycle.sh` 运行失效**：fragment 已在 00:46 被加上了，但 `.config` 没重生成 | `xaga.config:40` 有 `headset=1`；`.config:71` 没有（`.config` 停留在 2026-11-14 02:48:43）；`xaga-cycle.sh:35` 读 `.config`，`:78` 用它整体替换 `XNU_EXTRA_ARGS`。**缺的一步是 `make xaga_defconfig`** |
| 4 | `CONFIG_HAKIOS_MT6895_HEADSET=y` **确实开着**，驱动真编进了内核 | `.config:39`；`hakios.drivers.h:2` 列出 `mt6895_headset`；`hakios-kernel` 里有 26 处 headset 字符串（`headset: unsupported mode %u` 等） |
| 5 | `usb=2` **配了驱动但没人传参数**，`hakios_mt6895_usb_init()` 第一行就 return | `hakios.mt6895_usb.c:1866`；`.config:71` 无 `usb=`；`mt6895.config:83` 却写「xaga.config turns it on with usb=2」 |
| 6 | `BOARD_NO_PLOG / BOARD_NO_UART / BOARD_NO_OVL / MENU_DEFAULT_XNU` 是**无条件 `#define`**，`-D…=0` 改不动 | `board.h:401, 896, 897, 908`；实测 `-DBOARD_NO_PLOG=0` 触发 `-Wmacro-redefined` 后仍为 1 |
| 7 | `XNU_RETURN_AFTER` 的真值是 **600**（`make phone` 时，按 `Makefile:61 SECS ?= 600`），不是 `board.h:793` 的 150，也不是 45 | `board.h:793` 的 150 被 `xaga-cycle.sh:78` 的 `-DXNU_RETURN_AFTER` 覆盖；600 来自 `Makefile` |
| 8 | `configs/symbols.tsv` 陈旧 → 它**放过** `headset=`（不加也查不出来），却**拒绝**一个新参数名 | 实测：`CONFIG_HEADSET` 报 `no symbol`；`mali_colds=0` 报 `nothing reads` |
| 9 | `devices/` 整个被 `.gitignore:120` 忽略，`xaga.config` **本来就不进 git**；必须 `git add` 的是 `mtk/mt6895/configs/mt6895.config` | `git check-ignore -v` 命中 `.gitignore:120:/devices/*`；`mtk/mt6895/configs/` 是 `??` 且**没被 ignore** |
| 10 | 22 个 `A` **全部是 intent-to-add（index blob 为空）**，`git add <path>` 前任何提交都会把它们写成空文件 | `git ls-files -s` 显示 `e69de29…`（空 blob），工作区 48016/239745 字节 |

---

## 1. 决策链

### 1.1 图（人敲什么 → 编译器拿到什么）

```
 人敲：  make phone DEVICE=xaga SECS=45
            │
            │  Makefile:50   DEVICE ?= x6826b            ← 不写 DEVICE 就落到 MT6768
            │  Makefile:52   DEVICE_SOC = awk 'soc' devices/$(DEVICE)/device.conf
            │  Makefile:53   MTK = 有 mtk/$(DEVICE_SOC)/soc.conf 就是 MTK 手机
            │  Makefile:57   CYCLE = mtk/tools/$(DEVICE)-cycle.sh 若存在，否则 xnu-cycle.sh
            │  Makefile:66   ARGS  = tools/hakios-config.py args     ← 有 .config 时
            │  Makefile:165  XNU_SECS=$(SECS) XNU_ARGS="$(ARGS)" $(CYCLE)
            ▼
 ┌──────────────────────────────────────────────────────────────────────────────┐
 │ 分支 A：内核（Kconfig 一路，只影响 hakios-kernel）                              │
 │                                                                              │
 │  devices/xaga/configs/xaga_defconfig        ← 3 行 #include                   │
 │     ├─ #include common_defconfig            configs/common_defconfig          │
 │     ├─ #include mt6895.config               mtk/mt6895/configs/mt6895.config  │
 │     └─ #include xaga.config                 devices/xaga/configs/xaga.config  │
 │            │                                                                 │
 │            │  tools/hakios-config.py defconfig xaga_defconfig                 │
 │            ▼                                                                 │
 │        .config   （Linux 形式，388 行；45 个符号置位）                          │
 │            │                                                                 │
 │            ├─ tools/hakios-config.py args ─────────────► "cpus=4 buttons=2 …" │
 │            │        （CONFIG_* → name=value；boot-args 表 tools/hakios-config.py:38-105）│
 │            │                                                                 │
 │            └─ tools/hakios-config.py kernel --tree xnu-src/…                 │
 │                     ├─ xnu-src/…/config/MASTER.hakios    options HAKIOS_*     │
 │                     ├─ xnu-src/…/bsd|iokit/conf/files    哪些 .c 参与编译      │
 │                     └─ xnu-src/…/bsd/dev/hakios.drivers.h  编译进去的驱动清单  │
 │                              │                                               │
 │                              ▼  tools/make-kernel.sh:47                      │
 │                         hakios-kernel                                        │
 └──────────────────────────────────────────────────────────────────────────────┘

 ┌──────────────────────────────────────────────────────────────────────────────┐
 │ 分支 B：loader（不走 Kconfig，走 DEVICE → SoC → board.h）                      │
 │                                                                              │
 │  mtk/tools/xaga-cycle.sh:81   DEVICE=xaga mtk/loader/build.sh <TEST_FLAGS>    │
 │        │                                                                     │
 │        │  build.sh:24-31   BOARD 未给 → SOC=$(device.conf 的 soc) →           │
 │        │                   BOARD=mtk/$SOC/loader/board.h                     │
 │        │  build.sh:34      CFLAGS += -DBOARD_HEADER="…/mtk/mt6895/loader/board.h"│
 │        │  build.sh:15      CFLAGS 本身：-ffreestanding -mgeneral-regs-only …   │
 │        ▼                                                                     │
 │  mtk/mt6895/loader/board.h      40 个 #ifndef 包着的硬件常量 + 4 个无条件宏    │
 │        │                                                                     │
 │        │  xaga-cycle.sh:77-78  -DXNU_CORES / -DXNU_WATCH_SECONDS /            │
 │        │                       -DXNU_RETURN_AFTER / -DXNU_EXTRA_ARGS         │
 │        ▼                                                                     │
 │  mtk/loader/mtkloader.c:31-32  #include BOARD_HEADER（在所有默认值之前）        │
 │  mtk/loader/mtkloader.c:236-239                                            │
 │        XNU_BOOT_ARGS = "rd=md0" " " XNU_EXTRA_ARGS " loader_return_after=" XNU_RETURN_AFTER│
 │        ▼                                                                     │
 │  mtkloader.bin  →  mtk/loader/build_flashed.py  →  xnu-flashed-slot_a.img     │
 └──────────────────────────────────────────────────────────────────────────────┘

 内核与 loader 只在一处相交：loader 把 XNU_BOOT_ARGS 写进 boot_args.commandLine
 （mtkloader.c:3604），内核从那里读 PE_parse_boot_argn()。两条链没有任何共享文件。
```

### 1.2 每一环：定义了什么、谁读、被谁覆盖

| 环 | 文件 | 定义了什么 | 谁读它 | 覆盖关系 |
|---|---|---|---|---|
| 0 | `Makefile` | `DEVICE`（默认 `x6826b`）、`SECS`（600）、`CYCLE`、`ARGS`、`JOBS` | make 自身 | `DEVICE=` 是唯一入口；不写就落到 MT6768 |
| 1 | `devices/xaga/device.conf` | `soc mt6895`、`defconfig xaga_defconfig`、15 行 `status`（人看的） | `Makefile:52`、`mtk/loader/build.sh:26`、`tools/hakios-device.py` | — |
| 2 | `devices/xaga/configs/xaga_defconfig` | **只有 3 行 `#include`**（common / mt6895 / xaga） | `tools/hakios-config.py` 的 `expand()`（`tools/hakios-config.py:456`） | 顺序即优先级：后面的覆盖前面的 |
| 3 | `configs/common_defconfig` | 通用内核 ~1000 行 | 同上 | 最先被铺，故最弱 |
| 4 | `mtk/mt6895/configs/mt6895.config` | SoC 事实：`CONFIG_CPUS=4`、5 个 `CONFIG_HAKIOS_*=y`、关掉全部 Qualcomm 驱动 | 同上 | 覆盖 common_defconfig |
| 5 | `devices/xaga/configs/xaga.config` | 手机自身：`CONFIG_BOOTARGS`（**第 40 行**）、`FBSCALE=2`、`FBFIT=0`、`FB_QUEUE=1`、`FB_INUSE=y` | 同上 | **最强**：覆盖前三个 |
| 6 | `.config` | 上面 5 个文件的产物（388 行，45 个符号置位） | `tools/hakios-config.py args/kernel`、`Makefile:66`、`xaga-cycle.sh:35` | **手改会被下次 `make xaga_defconfig` 覆盖**（`.config:3` 自己写着 "Do not edit it"） |
| 7 | `mtk/mt6895/loader/board.h` | 硬件常量 + loader 行为宏 | 只有 `mtkloader.c`（经 `-DBOARD_HEADER`） | 被 `-D` 覆盖（仅限 `#ifndef` 包着的）；4 个无条件宏反不过来 |
| 8 | `mtk/loader/mtkloader.c` | MT6768 默认值（`#ifndef` 兜底）、`XNU_BOOT_ARGS` 拼装 | clang | board.h 不在时就是这些值 |
| 9 | `mtk/tools/xaga-cycle.sh` | `SECS`（`${XNU_SECS:-45}`）、`XNU_WATCH`（120）、4 个 `-D` | `build.sh` | **覆盖 board.h 的同名 `#ifndef` 宏** |
| 10 | `configs/boot-args.lst` | 389 个参数名（`name<TAB>层`）；`headset` 在 158 行 | `tools/hakios-config.py:413` `known_args()` | **只做校验，不产生值** |
| 11 | `configs/symbols.tsv` | 参数名 → `CONFIG_*` 符号、层、读它的文件 | `tools/hakios-config.py:106` | 陈旧时会误拒/误放 |
| 12 | `configs/boot-args.lst` 之外：`configs/symbols.tsv`、`docs/boot-args.md` 都由 `tools/boot-args.py` 生成 | — | — | 生成物，手改会被覆盖 |

### 1.3 三个点名问题的直接回答

**Q1：`devices/xaga/board.h` 和 `mtk/mt6895/loader/board.h` 是两个文件吗？谁生效？**

只有一个文件：`mtk/mt6895/loader/board.h`（48016 字节，`git status` 里是 `A`，但 index blob 为空）。
`devices/xaga/` 下根本没有 `board.h`：`find devices/xaga -type f` 只有 `device.conf`、`configs/xaga.config`、`configs/xaga_defconfig`。
生效路径唯一：`build.sh:24-34` 读 `devices/xaga/device.conf` 的 `soc mt6895`，拼出 `mtk/mt6895/loader/board.h`，用 `-DBOARD_HEADER` 交给 clang。
**`BOARD=<path>` 可以覆盖**（`build.sh:24` 的 `[ -z "${BOARD+x}" ]`），`DEVICE=` 是二级入口。**没有** `devices/xaga/board.h` 这个候选。

**Q2：`.config` 覆盖什么、被谁读、何时生成？**

- 生成：`make xaga_defconfig`（`Makefile:184-185` → `tools/hakios-config.py defconfig xaga_defconfig`）。当前 `.config` 生成于 2026-11-14 02:48:43。
- 被读：① `tools/hakios-config.py args`（`Makefile:66`、`xaga-cycle.sh:35`）；② `tools/hakios-config.py kernel`（`tools/make-kernel.sh:47`），产出 `MASTER.hakios` / `conf/files` / `hakios.drivers.h`；③ `make phone` 前置的 `hakios-config.py check`（`Makefile:161`）。
- 覆盖什么：**内核那条链的一切**（参数 + 编哪些驱动）；**loader 那条链一个都不覆盖**。
- 反过来的「覆盖」才是坑：`xaga-cycle.sh:78` 用 `.config` 的 args 生成 `-DXNU_EXTRA_ARGS`，把 `board.h:874` 的整行**替换**掉。这是「改了 board.h 不生效」的真正机制（下一节 2.1）。

**Q3：`CONFIG_BOOTARGS` 与 `XNU_EXTRA_ARGS` 的关系？**

不是同一层，是**上下游 + 覆盖**关系：

```
devices/xaga/configs/xaga.config:40  CONFIG_BOOTARGS="…"       (字符串，配置层)
        │  tools/hakios-config.py args 把它与 CONFIG_CPUS 等展开成一行
        ▼
.config 的 boot-arguments：15 个词，160 字符
        │  xaga-cycle.sh:35  XNU_ARGS=$(hakios-config.py args)
        │  xaga-cycle.sh:78  -DXNU_EXTRA_ARGS="$XNU_ARGS epoch=$(date +%s)"
        ▼
mtkloader 的 XNU_EXTRA_ARGS（编译期字符串）
        │  mtkloader.c:239
        ▼
boot_args.commandLine = "rd=md0 " XNU_EXTRA_ARGS " loader_return_after=" XNU_RETURN_AFTER
```

- `CONFIG_BOOTARGS` 只在**走 `hakios-config.py args` 的运行**里说话（`make phone`、`xaga-cycle.sh`）。
- `board.h:874` 的 `XNU_EXTRA_ARGS` 只在**没人传 `-DXNU_EXTRA_ARGS` 时**说话（例如手敲 `mtk/loader/build.sh`，或不带 `DEVICE` 的另一套流程）。
- 两者同时存在时，`.config` 赢——因为它是命令行 `-D`。实测见 2.1。

---

## 2. 「改了不生效」陷阱清单

按「改的人会怎么想 → 实际发生什么」排序。**共 11 条**（编号 T1–T11），每条给证据。

### T1 `board.h:874` 里的 `headset=1` 会被运行脚本整行替换 —— **当前就踩着的坑**

- 事实：`mtk/mt6895/loader/board.h:874`
  `#define XNU_EXTRA_ARGS "cpus=4 buttons=2 touchscreen=3 keylog=1 fbwidth=1080 fbheight=2460 fb_disp=1 headset=1"`
- 但 `mtk/tools/xaga-cycle.sh:77-78`：
  ```
  TEST_FLAGS=(-DXNU_CORES="$CORES" -DXNU_WATCH_SECONDS="$XNU_WATCH"
      "-DXNU_RETURN_AFTER=\"$SECS\"" "-DXNU_EXTRA_ARGS=\"$XNU_ARGS epoch=$(date +%s)\"")
  ```
  `XNU_ARGS` 来自 `.config`（`xaga-cycle.sh:35`），内容是 `cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1 fb_inuse=1 fb_queue=1 fbfit=0 fbscale=2`（160 字符）。
- 预处理实测（`clang -E -P`，用 `xaga-cycle.sh` 的整套 `-D`）：
  ```
  XNU_EXTRA_ARGS  cpus=4 … fb_disp=1 mali=6 mali_cold=0 mali_preas0=1 fb_inuse=1 fb_queue=1 fbfit=0 fbscale=2 epoch=1794674616
  ```
  **`headset=1` 不在了。**
- 同样地，`board.h:874` 的 `keylog=1` 与 `.config` 里的 `keylog=1` 值相同但来源不同：留着 board.h 那一行只对裸 build.sh 有用。
- 结论：**`headset=1` 必须写进 `devices/xaga/configs/xaga.config:40` 的 `CONFIG_BOOTARGS`**，写 board.h 只有「手敲 `build.sh` 不带 `-D`」时才有效。

**补充（2026-11-15 00:46 的实测状态）**：`devices/xaga/configs/xaga.config:40` **已经**被加上了 `headset=1`（文件 mtime 00:46:15，3247 字节）。但 `.config` 仍是 2026-11-14 02:48:43 那份，第 71 行**没有** `headset=`：

```
$ tools/hakios-config.py args
cpus=4 … mali_preas0=1 fb_inuse=1 fb_queue=1 fbfit=0 fbscale=2          ← 无 headset=
$ tools/hakios-config.py defconfig xaga_defconfig -o /tmp/regen2.config && diff -u .config /tmp/regen2.config
-CONFIG_BOOTARGS="… mali_preas0=1"
+CONFIG_BOOTARGS="… mali_preas0=1 headset=1"
```

所以「写对了地方」和「生效」之间还差一步：**`make xaga_defconfig`**。这一步不做，`xaga-cycle.sh:35` 读到的仍是旧 `.config`，运行仍然没有 `headset=1`。这就是本审计里最隐蔽的一类陷阱——两个文件都「看起来对」，中间那个生成物没更新（详见 T11）。

### T2 `-D` 改不动 4 个无条件宏

`board.h` 里这 4 个是光秃秃的 `#define`（没有 `#ifndef` 包）：

| 宏 | 行 | 值 | `-D…=0` 的结果 |
|---|---|---|---|
| `MENU_DEFAULT_XNU` | 401 | 1 | 仍是 1（clang 报 `-Wmacro-redefined`，不报错） |
| `BOARD_NO_UART` | 896 | 1 | 仍是 1 |
| `BOARD_NO_OVL` | 897 | 1 | 仍是 1 |
| `BOARD_NO_PLOG` | 908 | 1 | 仍是 1 |

实测（`/tmp` 下预处理）：
```
board.h:401:9: warning: 'MENU_DEFAULT_XNU' macro redefined
board.h:896:9: warning: 'BOARD_NO_UART' macro redefined
board.h:908:9: warning: 'BOARD_NO_PLOG' macro redefined
PLOG 1 / UART 1 / MENU 1
```
`-Wmacro-redefined` 不在 `build.sh:15` 的 `-Wall -Wextra` 里被提升为错误（没有 `-Werror`），所以**只有三行 warning，构建照常成功，值没变**。这就是「以为是 A 实际是 B」。

要改这三个，只能编辑 `board.h` 本身，或者给它们补 `#ifndef`（这属于改源码，不在本次审计范围）。

### T3 `BOARD_NO_PLOG=1`：`plog_putc()` 是空函数，ramoops 环形缓冲一个字节都不写

- `mtk/loader/mtkdbg.h:274-276`：
  ```c
  #ifdef BOARD_NO_PLOG
  static inline void plog_reset(void) { }
  static inline void plog_putc(char c) { (void)c; }
  ```
- `mtk/loader/mtkdbg.h:415-419`：`BOARD_NO_FB_TEXT` 也在时，`dbg_putc()` 变成 `dbg_tee_putc(); (void)c;`（连 `fbcon_putc` 都不调用）。
- 所以 loader 的日志只剩：屏幕（`fbcon_putc`）+ 自己的 logbook（`save_log_to_flash()`，写 `xnusys` 尾部）。**`/sys/fs/pstore/console-ramoops-0` 里不会有 loader 的文字**——`xaga-cycle.sh:119` 抓的就是这个文件。
- 这条是**故意**的（`board.h:899-908` 解释了：那块 DRAM 是 Android ramoops 认领的，第一字符写进去就停）。但要知道「loader 日志不在 pstore」这个后果。

### T4 `XNU_RETURN_AFTER`：三个候选值，真正生效的是 600

| 来源 | 值 | 何时生效 |
|---|---|---|
| `mtk/loader/mtkloader.c:156` | `"0"` | 没有 board.h、也没有 `-D` 时的兜底（等于「永不返回」） |
| `mtk/mt6895/loader/board.h:793` | `"150"` | 手敲 `build.sh`（有 `DEVICE`、无 `-D`） |
| `mtk/tools/xaga-cycle.sh:30` → `Makefile:61` | `SECS=${XNU_SECS:-45}`；`make phone` 传 `XNU_SECS=$(SECS)`；`Makefile:61` `SECS ?= 600` | **`make phone DEVICE=xaga` 的实际值：600** |
| `mtk/tools/xaga-cycle.sh` 直接运行 | 45 | 不经 make 时 |

预处理已验证 `-DXNU_RETURN_AFTER="600"` 会覆盖 `board.h:793`（输出 `XNU_RETURN_AFTER 600`）。
另外 `board.h:746` 的注释说「`loader_return_after=45`」，`board.h:843` 的注释说「`loader_return_after=900`」——**注释里的三个数字（45 / 150 / 900）都不是 600**。

### T5 `XNU_WATCH_SECONDS`：`board.h` 说 120，脚本也说 120，但**手册里写的是 `XNU_WATCH`**

- `board.h:796` = 120；`xaga-cycle.sh:37` `XNU_WATCH=${XNU_WATCH:-120}`；`xaga-cycle.sh:77` `-DXNU_WATCH_SECONDS="$XNU_WATCH"`。
- 一致。但改 `XNU_WATCH_SECONDS` 环境变量**没用**，脚本读的是 `XNU_WATCH`（`xaga-cycle.sh:22` 的 usage 注释写对了：`XNU_WATCH=120`）。
- 若直接跑 `build.sh` 想改，用 `-DXNU_WATCH_SECONDS=`；若跑 `xaga-cycle.sh`，用 `XNU_WATCH=`。

### T6 `XNU_WDT_SECONDS`：真值 0，且**没有任何脚本能改它**

- `board.h:799` = 0（`#ifndef` 包着，`-D` 可改）。`mtkloader.c:68` 的兜底也是 0。
- `xaga-cycle.sh` 的 `TEST_FLAGS` 里**没有** `-DXNU_WDT_SECONDS`（`xaga-cycle.sh:77-78`），`build.sh` 自己也不设。所以它永远取 `board.h:799` 的 0，硬件看门狗常关。
- `board.h:754-757` 解释了原因（MT6895 的 TOPRGU 只有 compatible string 对得上，key 值未验证）。这是有意的，但要知道**它不可运行期配置**。

### T7 `CONFIG_HAKIOS_MT6895_USB=y` 但永远不跑

- 驱动确实编进去了：`.config:43`，`hakios.drivers.h:2` 的清单里有 `mt6895_usb`，`hakios-kernel` 里有 42 处 `usb:` 字符串。
- 但 `mtk/mt6895/drivers/usb/hakios.mt6895_usb.c:1866`：
  ```c
  if (!PE_parse_boot_argn("usb", &which, sizeof(which)) || which != 2) { return; }
  ```
- `.config:71` / `xaga.config:40` 的 `CONFIG_BOOTARGS` 里**没有 `usb=`**。`board.h:874` 里也没有（`board.h:847` 明确说 "usb=2 is left out on purpose"）。
- 所以：**驱动（源文件 74103 字节）编在内核里，`hakios_mt6895_usb_init()` 一行实质代码都不执行**。`mtk/mt6895/configs/mt6895.config:83` 的注释「xaga.config turns it on with usb=2」是**错的**。

### T8 `mali` 参数：40 个读 `mali_*` 的驱动 vs 42 个 lst 行，`.config` 只传了 3 个

- 驱动侧：`hakios.mali610.c` 里 `PE_parse_boot_argn("mali…")` 共 **40** 个不同的名字（实测 `grep -o … | sort -u | wc -l`）。
- 列表侧：`configs/boot-args.lst` 里以 `mali` 开头的 **42** 行，多出的两个是 `mali_fastmap`、`mali_mhz`（它们属于 MT6768 的 `hakios.mali.c` 和 `hakios.mtk_keys.c`，`mtk/mt6895/` 下没人读）。
- 反向：驱动里 40 个名字**全部**在 lst 里，无遗漏（`comm -23` 为空）。
- `.config` 只传 `mali=6 mali_cold=0 mali_preas0=1`（`xaga.config:40`）——**其余 37 个全走驱动默认值**。这不算错，但「改了 mali 参数没反应」时先确认它是否在 `.config` 里；不加就不在命令行上，驱动用默认值。
- 另外 `mali=6` 是上限：`hakios.mali610.c:5109-5111` 把 `>6` 夹到 6，所以 `mali=7` 和 `mali=6` 等价。

### T9 `configs/symbols.tsv` 陈旧：`headset=` 查不出来，新参数名查不进去

- `tools/hakios-config.py:413` `known_args()` 读的是 `configs/boot-args.lst`（不是 symbols.tsv）；symbols.tsv 管的是「参数名 ↔ `CONFIG_*` 符号」。
- 实测三种情形：

  | 情形 | 结果 |
  |---|---|
  | 当前工作区（lst 390 行、含 `headset`），`.config` 加 `CONFIG_HEADSET=1` | `hakios-config: …: no symbol CONFIG_HEADSET`（**拒绝**） |
  | 用 `HEAD` 版 lst（无 `mali_cold`/`mali_preas0`/`key_scan`/`fb_disp`），套现在的 `.config` | 4 条 `PROBLEM: CONFIG_BOOTARGS: nothing reads the argument …`（**这就是历史上踩过的坑**） |
  | 当前工作区，`CONFIG_BOOTARGS` 里把 `mali_cold` 拼成 `mali_colds` | `PROBLEM: … did you mean mali_cold?`（**正确拒绝**） |

- 结论：**lst 校准了**（`python3 tools/boot-args.py --lst > configs/boot-args.lst` 已做，390 行）。但 `configs/symbols.tsv` 与 `docs/boot-args.md` **仍然陈旧**：`python3 tools/boot-args.py --check` 两条都报 out of date（见附录 A 7.4）。
- 后果：现在**不能**用 `CONFIG_HEADSET=1` 这种「配置符号」的方式传 `headset`，只能写进 `CONFIG_BOOTARGS` 字符串。要让它成为符号，得先 `--symbols` 重新生成 `symbols.tsv`。

### T10 `DEVICE` 不写 → 落到 MT6768，loader 是另一颗 SoC 的

- `Makefile:50` `DEVICE ?= x6826b`；`build.sh:24-31` 只有 `DEVICE` 非空且 `device.conf` 有 `soc` 时才挂 `board.h`。
- 实测（只读复现 `build.sh` 的选择逻辑）：
  ```
  DEVICE='<unset>' -> SOC='<none>'       -> BOARD='<none: MT6768 defaults in mtkloader.c>'
  DEVICE='xaga'    -> SOC='mt6895'       -> BOARD='…/mtk/mt6895/loader/board.h'
  ```
- 不带 board.h 时的关键默认值（`4b` 段）：`XNU_PHYS_BASE 0xc0000000`、`UART_BASE 0x11002000`、`TOPRGU_BASE 0x10007000`、`RAMOOPS_CONSOLE 0x4d09f000`、`LK_FB_BASE 0x787b0000`、`OVL0_BASE 0x1400b000`、`XNU_EXTRA_ARGS "cpus=1"`、`MENU_DEFAULT_XNU 0`。
- 关于 **41240 字节**那个数字：本次审计**没有**在树里找到 41240 字节的 loader 产物（`find . -type f -size 41240c` 为空）。现在 `mtk/loader/mtkloader.bin` 是 **59720** 字节（2026-11-14 02:48:53）。「不带 DEVICE 会得到 41240 字节镜像」这一具体数值**未验证**（无该构件可对比）；但「不带 DEVICE 会用 MT6768 的地址」**已证实**。

### T11 `.config` 是生成物：改了 fragment 不重生成，就静默用旧配置（**现在正在发生**）

- `.config:3` 自己写着 "Do not edit it"。
- 审计开始时（00:30）实测 `tools/hakios-config.py defconfig xaga_defconfig -o /tmp/regen-xaga.config` 与当时 `.config` **逐字节相同**（`diff` 无输出）。**到 00:46 就不成立了**——见下面第四条。
- `mtk/mt6895/configs/mt6895.config` 的 mtime（23:28:16）晚于 `.config`（02:48:43），但两者内容一致——那次改动只动了注释。
- **但这个坑现在正在发生**：`devices/xaga/configs/xaga.config` 在 00:46:15 被改（加了 `headset=1`），`.config` 没跟着变。实测 `diff -u .config <(defconfig 重生)` 只有一行差异，就是 `CONFIG_BOOTARGS`。
- 关键点：**没有任何构建步骤会自动重生成 `.config`**。`tools/make-kernel.sh:47` 只跑 `hakios-config.py kernel`（把 `.config` 的驱动选择写进内核树），不跑 `defconfig`；`make kernel` 也不跑。所以「改了 fragment 忘了 defconfig」会静默地一直用旧配置。
- 更糟的是 `.config` 的 mtime 比 fragment 新时也看不出来——这里恰恰相反（fragment 更新），但 `make` 不检查这个依赖。

---

## 3. 配置项真值表

「谁说了算」= 最终有效值的来源。所有值均为本次实测（预处理 / `hakios-config.py` / 二进制字符串）。

### 3.1 loader 侧（`mtk/mt6895/loader/board.h`）

| 宏 | 行 | 值 | `#ifndef`? | `-D` 能改? | 谁说了算 |
|---|---|---|---|---|---|
| `BOARD_NAME` | 21 | `"xaga"` | 是 | 是 | board.h |
| `BOARD_TITLE` | 24 | `"XAGA (MT6895)"` | 是 | 是 | board.h |
| `XNU_EXTRA_ARGS` | 874 | `"cpus=4 buttons=2 touchscreen=3 keylog=1 fbwidth=1080 fbheight=2460 fb_disp=1 headset=1"` | 是 | 是 | **`xaga-cycle.sh:78`（运行时）** > board.h |
| `XNU_CORES` | 781 | 4 | 是 | 是 | `xaga-cycle.sh:75-77`（从 `.config` 的 `cpus=` 抠出来）> board.h |
| `XNU_RETURN_AFTER` | 793 | `"150"` | 是 | 是 | **`xaga-cycle.sh:78` → `SECS` = 600**（`make phone`） |
| `XNU_WATCH_SECONDS` | 796 | 120 | 是 | 是 | `xaga-cycle.sh:37` 的 `XNU_WATCH`（默认 120） |
| `XNU_WDT_SECONDS` | 799 | 0 | 是 | 是 | **board.h 唯一来源**（脚本不传） |
| `BOARD_NO_PLOG` | 908 | 1 | **否** | **否** | board.h |
| `BOARD_NO_UART` | 896 | 1 | **否** | **否** | board.h |
| `BOARD_NO_OVL` | 897 | 1 | **否** | **否** | board.h |
| `BOARD_NO_FB_TEXT` | — | **未定义** | — | 是（`-D` 可加） | 未定义（`mtkdbg.h:408` 的 `#ifdef` 不成立，走 `:420` 的 `#else`） |
| `MENU_DEFAULT_XNU` | 401 | 1 | **否** | **否** | board.h |
| `BOARD_TOUCH` | 498 | 0 | 是 | 是 | board.h |
| `XNU_SYSTEM_IMAGE` | 385 | 1 | 是 | 是 | board.h（注释 351 行说「0 for now」，**代码是 1**） |
| `XNU_ROOT_EMMC` | 403 | 0 | 是 | 是 | board.h |
| `BOARD_STORAGE_UFS` | 425 | 1 | 是 | 是 | board.h |
| `UFS_USE_LK_LIST` | 450 | 1 | 是 | 是 | board.h |
| `BOARD_RETURN_TO_ANDROID` | 511 | 1 | 是 | 是 | board.h |
| `BOARD_MALI_POWER` | 591 | 1 | 是 | 是 | board.h |
| `BOARD_MALI_PREPOWER` | 611 | 1 | 是 | 是 | board.h |
| `BOARD_MALI_DOMAINS` | 682 | 2 | 是 | 是 | board.h |
| `BOARD_MALI_STAGE` | 652 | 0 | 是 | 是 | board.h |
| `BOARD_MALI_EARLY` | 696 | 0 | 是 | 是 | board.h |
| `BOARD_MALI_STOP` | 713 | 0 | 是 | 是 | board.h |
| `BOARD_REPORT_PAGES` | 724 | 0 | 是 | 是 | board.h |
| `XNU_BIG_CLOCK` | 734 | 0 | 是 | 是 | board.h |
| `BOARD_REPORT_SYSTEM_IMAGE` | 464 | 1 | 是 | 是 | board.h |
| `BOARD_USB_PROBE` | 524 | 0 | 是 | 是 | board.h |
| `BOARD_PMIF_PROBE` | 534 | 0 | 是 | 是 | board.h |
| `BOARD_DISP_PROBE` | 552 | 0 | 是 | 是 | board.h |
| `BOARD_MALI_PROBE` | 574 | 0 | 是 | 是 | board.h |
| `BOARD_CORE_ORDER` | 54 | `{0,4,5,7,1,2,3,6}` | 是 | 是 | board.h |
| `XNU_PHYS_BASE` | 123 | `0x100000000` | 是 | 是 | board.h（MT6768 兜底 `0xc0000000`） |
| `XNU_MEM_SIZE` | 126 | `0x100000000` | 是 | 是 | board.h（MT6768 兜底 `0xfe000000`） |
| `UART_BASE` | 96 | `0x11001000` | 是 | 是 | board.h（MT6768 兜底 `0x11002000`） |
| `TOPRGU_BASE` | 243 | `0x1c007000` | 是 | 是 | board.h（MT6768 兜底 `0x10007000`） |
| `RAMOOPS_CONSOLE` | 215 | `0x4811f000` | 是 | 是 | board.h（MT6768 兜底 `0x4d09f000`） |
| `LK_FB_BASE` | 277 | `0xfe14e000` | 是 | 是 | board.h（MT6768 兜底 `0x787b0000`） |
| `OVL0_BASE` | 308 | `0x14002000` | 是 | 是 | board.h（MT6768 兜底 `0x1400b000`） |
| `PANEL_W/H` | 289/292 | 1080 / 2460 | 是 | 是 | board.h |

### 3.2 内核侧（`.config`）

| 符号 | `.config` 行 | 值 | 产生什么参数 | 真值来自 |
|---|---|---|---|---|
| `CONFIG_DEVICE` | 5 | `"xaga"` | （只进 version string） | `xaga.config:30` |
| `CONFIG_SOC` | 6 | `"mt6895"` | （只进 version string） | `mt6895.config:25` |
| `CONFIG_FIRMWARE` | 7 | `"device"` | — | `configs/defconfig` |
| `CONFIG_CPUS` | 44 | 4 | `cpus=4` | `mt6895.config:26` |
| `CONFIG_BOOTARGS` | 71 | `"buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1"` | 原样追加 **10 个词** | **`xaga.config:40`** |
| `CONFIG_FB_INUSE` | 161 | `y` | `fb_inuse=1` | `xaga.config:39` |
| `CONFIG_FB_QUEUE` | 164 | `1` | `fb_queue=1` | `xaga.config:38` |
| `CONFIG_FBFIT` | 166 | `0` | `fbfit=0` | `xaga.config:37` |
| `CONFIG_FBSCALE` | 168 | `2` | `fbscale=2` | `xaga.config:31` |
| `CONFIG_HAKIOS_MT6895_HEADSET` | 39 | **y** | （编译开关，不是参数） | `mt6895.config:80` |
| `CONFIG_HAKIOS_MALI610` | 40 | **y** | 同上 | `mt6895.config:90` |
| `CONFIG_HAKIOS_MTK6895_KEYS` | 41 | **y** | 同上 | `mt6895.config:77` |
| `CONFIG_HAKIOS_NVT` | 42 | **y** | 同上 | `mt6895.config:74` |
| `CONFIG_HAKIOS_MT6895_USB` | 43 | **y** | 同上 | `mt6895.config:85` |
| `CONFIG_TOUCHSCREEN` | 50 | **not set** | — | `mt6895.config:30`（故意：用 `touchscreen=3` 走 BOOTARGS） |
| `CONFIG_BUTTONS` | 129 | **not set** | — | 同上（用 `buttons=2`） |
| `CONFIG_GPU` / `CONFIG_MALI` | 51 / 221 | not set | — | `mt6895.config:31 / 68` |

`.config` 的有效 boot-arguments（**15 个词，160 字符**）：
```
cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1
mali=6 mali_cold=0 mali_preas0=1 fb_inuse=1 fb_queue=1 fbfit=0 fbscale=2
```

### 3.3 `headset` 现状（逐个位置）

| 位置 | 有 `headset=` 吗 | 说明 |
|---|---|---|
| `mtk/mt6895/loader/board.h:874` | **有**（`headset=1`） | 只在无 `-DXNU_EXTRA_ARGS` 时生效 |
| `devices/xaga/configs/xaga.config:40` | **没有** | ← **这里该加** |
| `.config:71` | **没有** | 生成物；`make xaga_defconfig` 后自动出现 |
| `configs/boot-args.lst:158` | 名字在（`headset\tk`） | 校验用，不是值 |
| `configs/symbols.tsv` | **没有 `HEADSET`** | 所以不能 `CONFIG_HEADSET=1`（实测 `no symbol`） |
| 驱动消费者 | `hakios.mt6895_headset.c` 的 `hakios_mt6895_headset_init()` 内（2026-11-15 00:54 版本为 `:488`；该文件正被编辑，行号会漂） | `PE_parse_boot_argn("headset", &mode, …)`，`mode==0` 或解析失败即 return |
| 调用点 | `hakios.mt6895_keys.c:506` | 在 keys worker 里调 `hakios_mt6895_headset_init()` |
| 前提 | `hakios.mt6895_keys.c:665` | **`buttons=2` 才进这个 worker**——`.config` 有 `buttons=2` ✔ |

**`headset=1` 应该加在哪：`devices/xaga/configs/xaga.config:40`，行尾追加 ` headset=1`，然后 `make xaga_defconfig`。**

已用影子树验证（`/tmp/shadowtree`，符号链接 + 改一份 `xaga.config`，不动原树）：
```
$ python3 tools/hakios-config.py defconfig xaga_defconfig -o /tmp/shadow-headset.config
hakios-config: 170 characters of boot-arguments; kernel.ver=unknown      ← 160 + " headset=1"
$ python3 tools/hakios-config.py check --config /tmp/shadow-headset.config
45 symbols set, 170 characters of boot-arguments (the kernel holds 607; a run adds about 260)   ← 0 problem
$ python3 tools/hakios-config.py args --config /tmp/shadow-headset.config
cpus=4 … mali_preas0=1 headset=1 fb_inuse=1 fb_queue=1 fbfit=0 fbscale=2
```
170 + 260（`RUN_OVERHEAD`）= 430 < 607，**长度安全**（`tools/hakios-config.py:38-39`）。

### 3.4 `BOARD_NO_*` 一览

| 宏 | 值 | 关掉了什么 | 代码位置 | 可运行期改? |
|---|---|---|---|---|
| `BOARD_NO_UART` | 1 | `mtkloader.c:527` 的 UART 输出（`out()` 不再写 0x11001000） | `mtkloader.c:527` | 否（无条件宏 + 需重编） |
| `BOARD_NO_OVL` | 1 | `mtkloader.c:638, 671` 的 OVL 读/写（`display_find()` 不读 OVL，直接信 `LK_FB_BASE`） | `mtkloader.c:638,671` | 否 |
| `BOARD_NO_PLOG` | 1 | `mtkdbg.h:274-276` 的 `plog_putc()`/`plog_reset()` 变空 | `mtkdbg.h:274` | 否（**且 pstore 里不会有 loader 日志**） |
| `BOARD_NO_FB_TEXT` | **未定义** | 若定义，连屏幕文字也没了（`mtkdbg.h:415-419`） | `mtkdbg.h:408` | 可 `-D` 加 |
| `BOARD_SKIP_MENU` | 由 `-D` 加 | 跳过菜单（`mtkloader.c:7478`） | `mtkloader.c:7478` | 可 `-D` |
| `BOARD_NO_CONNSYS` | 由 `-D` 加 | 跳过 CONNSYS（`mtkloader.c:7513`） | `mtkloader.c:7513` | 可 `-D` |

注意：`MENU_DEFAULT_XNU=1`（`board.h:401`）**已经让菜单默认选 XNU**，`BOARD_SKIP_MENU` 只是省时间，不改变结果。

---

## 4. 配置与代码不一致清单

### 4.1 `CONFIG_BOOTARGS` 的参数名 vs 代码里的 `PE_parse_boot_argn`

`.config` 的 15 个词，逐个核 `configs/boot-args.lst` 是否有该名字（`known_args()` 的依据）：

| 参数 | lst 行 | 消费者 |
|---|---|---|
| `cpus` | ✔ | `mtkloader.c`（不是 `PE_parse`，是 loader 拼进 DT 的 `/cpus`） |
| `buttons` | ✔ | `hakios.mt6895_keys.c:665`（`!=2` 就 return） |
| `touchscreen` | ✔ | `hakios.nvt.c`（`=3`） |
| `keylog` | ✔ | `hakios.mt6895_keys.c:447` |
| `key_scan` | ✔（新加，`boot-args.lst` diff） | `hakios.mt6895_keys.c:448` |
| `fbwidth` / `fbheight` | ✔ | `MiniIOMobileFramebuffer.cpp` |
| `fb_disp` | ✔（新加） | `MiniIOMobileFramebuffer.cpp` |
| `mali` | ✔ | `hakios.mali610.c:5100` |
| `mali_cold` | ✔（新加） | `hakios.mali610.c` |
| `mali_preas0` | ✔（新加） | `hakios.mali610.c:5153` |
| `fb_inuse` / `fb_queue` / `fbfit` / `fbscale` | ✔ | `MiniIOMobileFramebuffer.cpp` / `hakios.nvt.c`（`fbfit`） |

**结论：15 个词全部有消费者，无悬挂名字。**（原 `mali_cold`/`mali_preas0`/`key_scan`/`fb_disp` 被拒的历史问题已随 `boot-args.lst` 重生成解决。）

### 4.2 驱动声明（`manifest.conf` / `soc.conf`）vs 实际编译

| 声明处 | 声明了什么 | 实际 |
|---|---|---|
| `devices/manifest.conf:22-23` | `soc mt6895  path mtk/mt6895` | ✔ `tools/hakios-device.py list` 认（"MediaTek MT6895 family … mtk/mt6895"） |
| `devices/manifest.conf:29-30` | `device xaga  path devices/xaga` | ✔ 认（红色：`[0 works, 2 partial, 15 missing]`） |
| `mtk/mt6895/soc.conf:12` | 8 核（4×A55 + 4×A78） | ✔ 代码只用 4 个：`mt6895.config:26` `CONFIG_CPUS=4`；`board.h:781` `XNU_CORES 4` |
| `mtk/mt6895/soc.conf:13` | Mali-G610 @ 0x13000000 | ✔ `CONFIG_HAKIOS_MALI610=y`（`.config:40`） |
| `mtk/mt6895/drivers/*/conf` | 5 个 `driver` 声明 | ✔ 全在 `.config` 里 `=y`；`hakios.drivers.h:2` 列出 5 个 |
| `mtk/mt6895/configs/mt6895.config:16` | "`mtk/mt6895/drivers/gpu/hakios.mali.c` is written for this one" | ✘ **文件不存在**（只有 `hakios.mali610.c`）——陈旧注释 |
| `mtk/mt6895/configs/mt6895.config:73,76,79,83,89` | "xaga.config turns it on with `touchscreen=3` / `buttons=2` / `usb=2` / `mali=2`" | ✘ **`usb=2` 不存在**（4.3）；`mali=2` 也不存在（实际 `mali=6`）——四处陈旧注释 |
| `devices/xaga/device.conf:14-16` | "no GPU driver (the G610 is not the G52…)"、"no UFS in the loader"、"the MT6768's TD4160 bring-up is left off" | ✘ 前两条与当前代码相反（`MALI610` 已编入、`ufs.h` 已写、`XNU_SYSTEM_IMAGE=1`）；`status` 段整体是 11-01 的旧状态 |

### 4.3 `usb`：声明 vs 参数

- 声明：`mtk/mt6895/configs/mt6895.config:85` `CONFIG_HAKIOS_MT6895_USB=y`，注释（83-85 行）说 xaga.config 用 `usb=2` 打开。
- 实际：`xaga.config:40` 没有任何 `usb=`；`board.h:847` 明说故意不加。
- 后果：`hakios.mt6895_usb.c:1866` 第一行返回。**编译进内核，永不执行。**

### 4.4 `CONFIG_SOC` / `CONFIG_DEVICE` 只进版本串

`.config:5-6` 的 `CONFIG_DEVICE="xaga"`、`CONFIG_SOC="mt6895"` 只出现在 `kernel.ver=2.1.13-hakios-darwin22.5-mt6895-g4ebad21a-dirty`（`hakios-config.py show` 第一行），**不产生任何 boot-arg**，也不改变任何编译分支。想靠它切设备是无效的。

### 4.5 `board.h` 里注释与代码相反的 3 处

| 行 | 注释说 | 代码是 |
|---|---|---|
| 351 | `XNU_SYSTEM_IMAGE 0 for now` | 385 行 `#define XNU_SYSTEM_IMAGE 1` |
| 746 | `loader_return_after=45` | 793 行 `"150"`；实际运行 600 |
| 843 | `loader_return_after=900` | 同上 |
| 868-873 | "mali= is left out entirely … mali=5 with its 26 mali_* knobs was trial 694's line" | 874 行没有 `mali=`（与注释一致），但 `.config:71` **有** `mali=6`——注释描述的是「Round 20」那个状态，已被 `.config` 覆盖 |

### 4.6 `board.h` 的 `keylog` 与 `xaga.config` 的意图不符

- `xaga.config:27-29`：「keylog=2 is on for the first boots … drop it once they are [pinned down]」。
- 实际 `xaga.config:40` 写的是 `keylog=1`，`board.h:874` 也是 `keylog=1`。**注释要 2，配置给 1**。（`keylog=2` 与 `=1` 的差别，`hakios.mt6895_keys.c:447` 只把它当等级用；具体语义未逐行核对，标「未验证」。）

### 4.7 `drivers/usb` 空目录让 `drivers-check.py` 报错

- `drivers/usb/` 存在但**空**（`ls -la drivers/usb` 只有 `.` 和 `..`），不在 `drivers/map` 里，不是已知子系统。
- `tools/drivers-check.py:67-68` 报 `drivers/usb is not a known subsystem directory`。
- 后果：CI 不干净。**不影响当前构建**（该目录没有文件被编译），但会让「drivers-check 通过」这个前提失效。

---

## 5. 57 个改动分类表

`git status --porcelain` 实数：**29 M / 22 A / 6 ?? = 57**（背景里说的「49+」偏少；实际 57）。
**22 个 `A` 全是 intent-to-add**：`git ls-files -s` 显示 index blob 是 `e69de29…`（空文件），工作区有真实内容。这意味着 `git diff` 会把它们显示成「全新增」，`git diff --cached` 显示为空——**任何只 `git commit`（不加 `-a`）的操作都会提交空文件**。

### 5.1 配置类（影响构建结果）—— 16 个（逐项见 5.3b）

| 文件 | 状态 | 索引 blob | 影响什么 | 当前值 / 与 HEAD 差异 |
|---|---|---|---|---|
| `Makefile` | M | 有 | `CYCLE` 选择、`make phone` 的调用 | +4 行 `CYCLE ?=`；`mtk/tools/xnu-cycle.sh` → `$(CYCLE)`（`git diff` 见 5.4） |
| `configs/boot-args.lst` | M | 有 | `CONFIG_BOOTARGS` 的合法参数名集合（`known_args()`，`tools/hakios-config.py:413`） | `git diff`：+48 行内容（含 `fb_disp`、`fb_move`、`headset`、`key_poll_ms`、`key_scan` 与 **38** 个 `mali_*`）；文件 390 行 |
| `configs/symbols.tsv` | M | 有 | 参数名 ↔ `CONFIG_*` 符号（`CONFIG_<NAME>=value` 的来源） | `git diff`：+13 行内容；**仍陈旧**（`boot-args.py --check` 报 out of date） |
| `devices/manifest.conf` | M | 有 | `hakios-device.py` 认识的 SoC/设备 | +6 行：`soc mt6895 path mtk/mt6895`、`device xaga path devices/xaga` |
| `drivers/map` | M | 有 | 哪些文件进内核树、放哪 | +2 行：`input bsd/dev/hakios.nvt_fw.h`、`gpu bsd/dev/hakios.mali610_fw.h` |
| `mtk/loader/build.sh` | M | 有 | `-DBOARD_HEADER` 的选择 | +21 行（board.h 选择逻辑，24-36 行） |
| `mtk/loader/link.ld` | M | 有 | loader 的内存布局 | +8/-? 行（未逐行核，标「未验证」具体影响） |
| `mtk/loader/start.S` | M | 有 | loader 入口、异常向量 | +211 行 |
| `mtk/mt6895/loader/board.h` | **A** | **空** | **全部 MT6895 硬件常量 + loader 行为** | 48016 字节，全新 |
| `mtk/mt6895/soc.conf` | **A** | **空** | SoC 层声明（`hakios-device.py` 认它） | 990 字节，全新 |
| `mtk/mt6895/configs/`（`mt6895.config`） | **??** | — | **`.config` 的 SoC fragment**；没有它 `make xaga_defconfig` 直接失败 | 5256 字节，未跟踪 |
| `xnu/files/bsd/sys/hakios.pmif.h` | **??** | — | 3 个 mt6895 驱动 include 它 | 882 字节，未跟踪；内核树里已有副本（`xnu-src/…/bsd/sys/`） |

### 5.2 代码类 —— 23 个（逐项见 5.3b）

| 分组 | 文件 | 说明 |
|---|---|---|
| **内核驱动（新）** | `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c`（M）<br>`mtk/mt6895/drivers/gpu/hakios.mali610.c`（A）<br>`mtk/mt6895/drivers/input/hakios.nvt.c`（A）<br>`mtk/mt6895/drivers/input/hakios.mt6895_keys.c`（A）<br>`mtk/mt6895/drivers/usb/hakios.mt6895_usb.c`（A） | 5 个驱动本体；`hakios.mt6895_headset.c` 是唯一 HEAD 里已存在的（`git ls-tree HEAD mtk/mt6895` 只有 audio） |
| **驱动声明** | `mtk/mt6895/drivers/{gpu,input,usb}/conf`（A）<br>`mtk/mt6895/drivers/audio/conf`（M） | 4 个 `conf`；audio 的 `conf` 是 HEAD 版（`0f92d70…`）改的 |
| **固件头（生成物）** | `drivers/gpu/hakios.mali610_fw.h`（A，1323077 B）<br>`drivers/input/hakios.nvt_fw.h`（A，847130 B） | 由 `mkmalifw.py` / `mknvtfw.py` 生成 |
| **内核公共代码** | `drivers/core/hakios.stubs.c`（M）<br>`drivers/input/hakios.input.c`（M）<br>`xnu/files/bsd/sys/hakios.audio.h`（M）<br>`xnu/files/bsd/sys/hakios.input.h`（M）<br>`xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp`（M，+341）<br>`xnu/files/osfmk/arm64/loader_return.c`（M，+2） | `stubs.c` 给 5 个驱动加空实现（`#ifndef HAKIOS_*`）；`input.c` 加 5 个 `hakios_*_init()` 调用；`loader_return.c` 加 `hakios_mt6895_headset_quiesce()` |
| **loader 本体** | `mtk/loader/mtkloader.c`（M，+4359）<br>`mtk/loader/mtkdbg.h`（M，+197）<br>`mtk/loader/mtkmali.h`（M）<br>`mtk/loader/msdc.h`（M）<br>`mtk/loader/fbdiag_start.S`（M）<br>`mtk/loader/ufs.h`（**A，空 blob**，141239 B） | `mtkloader.c` 是最大的一块（UFS、Mali 供电、PMIF、headset 日志） |
| **用户态** | `userland/build.sh`（M）<br>`userland/rc.launchd`（M）<br>`userland/src/init.c`（M）<br>`userland/src/inject.c`（M） | 与内核镜像分开构建，不进 `hakios-kernel` |

### 5.3 文档 / 工具类 —— 18 个（逐项见 5.3b）

| 文件 | 状态 | 字节 | 说明 |
|---|---|---|---|
| `docs/boot-args.md` | M | — | `boot-args.py --markdown` 的产物；**仍陈旧**（`--check` 报 out of date） |
| `mtk/README.md` | M | — | +157 行 |
| `mtk/tools/chainboot.py` | M | — | 引导镜像打包 |
| `mtk/tools/gpt-carve.py` | M | — | GPT 分区 |
| `mtk/tools/xaga-cycle.sh` | **A（空 blob）** | 6491 | **一次运行的入口**；本审计的关键一环（`:35` 读 `.config`，`:78` 覆盖 `XNU_EXTRA_ARGS`） |
| `mtk/tools/xaga-auto.sh` | **A（空 blob）** | 4147 | 无人值守：等 fastboot → 刷 → 等 Android → 拉 logbook → 打印关键行 |
| `mtk/tools/xaga-carve.sh` | **A（空 blob）** | 8878 | GPT 划 `xnusys` |
| `mtk/tools/xaga-pull-log.sh` | **A（空 blob）** | 1107 | 从手机拉 logbook |
| `mtk/tools/mkfont8x16.py` | **A（空 blob）** | 7493 | 生成 `font8x16.h`（替代 swift 版） |
| `mtk/tools/mkmalifw.py` | **A（空 blob）** | 1926 | 生成 `drivers/gpu/hakios.mali610_fw.h`（1323077 B） |
| `mtk/tools/mknvtfw.py` | **A（空 blob）** | 9211 | 生成 `drivers/input/hakios.nvt_fw.h`（847130 B） |
| `mtk/tools/nvt-slots.py` | **A（空 blob）** | 2068 | Novatek 槽位 |
| `mtk/tools/xaga-first-boot.md` | **A（空 blob）** | 7780 | 文档 |
| `mtk/tools/xaga-input.md` | **A（空 blob）** | 10396 | 文档 |
| `mtk/tools/test-headset.py` | **??** | 8351 | 未跟踪测试 |
| `mtk/tools/test-logbook.py` | **??** | 7370 | 未跟踪测试 |
| `mtk/tools/test-pmif-transactions.py` | **??** | 6313 | 未跟踪测试 |
| `mtk/tools/xaga-logbook.py` | **??** | 2300 | 未跟踪工具 |

（`xaga-cycle.sh` 归在「工具」是因为它是运行脚本；但它决定 `-D` 的值，**实际影响构建结果**，所以 5.1 的结论也适用于它。）

### 5.3b 逐项对照（57 个全列）

`git status --porcelain` 的 57 条，按类别归位。表格里的「类」：**配**=配置（影响构建结果）/ **码**=代码 / **文**=文档工具。

| # | 状态 | 文件 | 类 |
|---|---|---|---|
| 1 | M | `Makefile` | 配 |
| 2 | M | `configs/boot-args.lst` | 配 |
| 3 | M | `configs/symbols.tsv` | 配 |
| 4 | M | `devices/manifest.conf` | 配 |
| 5 | M | `docs/boot-args.md` | 文 |
| 6 | M | `drivers/core/hakios.stubs.c` | 码 |
| 7 | A | `drivers/gpu/hakios.mali610_fw.h` | 码（生成物） |
| 8 | M | `drivers/input/hakios.input.c` | 码 |
| 9 | A | `drivers/input/hakios.nvt_fw.h` | 码（生成物） |
| 10 | M | `drivers/map` | 配 |
| 11 | M | `mtk/README.md` | 文 |
| 12 | M | `mtk/loader/build.sh` | 配 |
| 13 | M | `mtk/loader/fbdiag_start.S` | 码 |
| 14 | M | `mtk/loader/link.ld` | 配 |
| 15 | M | `mtk/loader/msdc.h` | 码 |
| 16 | M | `mtk/loader/mtkdbg.h` | 码 |
| 17 | M | `mtk/loader/mtkloader.c` | 码 |
| 18 | M | `mtk/loader/mtkmali.h` | 码 |
| 19 | M | `mtk/loader/start.S` | 配 |
| 20 | A | `mtk/loader/ufs.h` | 码 |
| 21 | M | `mtk/mt6895/drivers/audio/conf` | 配 |
| 22 | M | `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c` | 码 |
| 23 | A | `mtk/mt6895/drivers/gpu/conf` | 配 |
| 24 | A | `mtk/mt6895/drivers/gpu/hakios.mali610.c` | 码 |
| 25 | A | `mtk/mt6895/drivers/input/conf` | 配 |
| 26 | A | `mtk/mt6895/drivers/input/hakios.mt6895_keys.c` | 码 |
| 27 | A | `mtk/mt6895/drivers/input/hakios.nvt.c` | 码 |
| 28 | A | `mtk/mt6895/drivers/usb/conf` | 配 |
| 29 | A | `mtk/mt6895/drivers/usb/hakios.mt6895_usb.c` | 码 |
| 30 | A | `mtk/mt6895/loader/board.h` | 配（**最关键**） |
| 31 | A | `mtk/mt6895/soc.conf` | 配 |
| 32 | M | `mtk/tools/chainboot.py` | 文 |
| 33 | M | `mtk/tools/gpt-carve.py` | 文 |
| 34 | A | `mtk/tools/mkfont8x16.py` | 文 |
| 35 | A | `mtk/tools/mkmalifw.py` | 文 |
| 36 | A | `mtk/tools/mknvtfw.py` | 文 |
| 37 | A | `mtk/tools/nvt-slots.py` | 文 |
| 38 | A | `mtk/tools/xaga-auto.sh` | 文 |
| 39 | A | `mtk/tools/xaga-carve.sh` | 文 |
| 40 | A | `mtk/tools/xaga-cycle.sh` | 文（但决定 `-D`，按 配 对待） |
| 41 | A | `mtk/tools/xaga-first-boot.md` | 文 |
| 42 | A | `mtk/tools/xaga-input.md` | 文 |
| 43 | A | `mtk/tools/xaga-pull-log.sh` | 文 |
| 44 | M | `userland/build.sh` | 码 |
| 45 | M | `userland/rc.launchd` | 码 |
| 46 | M | `userland/src/init.c` | 码 |
| 47 | M | `userland/src/inject.c` | 码 |
| 48 | M | `xnu/files/bsd/sys/hakios.audio.h` | 码 |
| 49 | M | `xnu/files/bsd/sys/hakios.input.h` | 码 |
| 50 | M | `xnu/files/iokit/Kernel/MiniIOMobileFramebuffer.cpp` | 码 |
| 51 | M | `xnu/files/osfmk/arm64/loader_return.c` | 码 |
| 52 | ?? | `mtk/mt6895/configs/`（`mt6895.config`） | 配（**必须 `git add`**） |
| 53 | ?? | `mtk/tools/test-headset.py` | 文 |
| 54 | ?? | `mtk/tools/test-logbook.py` | 文 |
| 55 | ?? | `mtk/tools/test-pmif-transactions.py` | 文 |
| 56 | ?? | `mtk/tools/xaga-logbook.py` | 文 |
| 57 | ?? | `xnu/files/bsd/sys/hakios.pmif.h` | 配（**必须 `git add`**） |

计数：**配 16 / 码 23 / 文 18 = 57**（`xaga-cycle.sh` 记在「文」，但它决定 `-D`，实际影响构建结果）。

### 5.4 关键 diff（只读核过）

`Makefile`（唯一的构建逻辑改动）：
```diff
@@ -51,6 +51,10 @@ DEVICE ?= x6826b
 MTK := $(if $(wildcard mtk/$(DEVICE_SOC)/soc.conf),1,)
+# The one-run script: xnu-cycle.sh for a phone that can be handed a kernel out of RAM, and
+# mtk/tools/<device>-cycle.sh where it cannot …
+CYCLE ?= $(if $(wildcard mtk/tools/$(DEVICE)-cycle.sh),mtk/tools/$(DEVICE)-cycle.sh,mtk/tools/xnu-cycle.sh)
@@ -158,7 +162,7 @@ phone:
-	XNU_SECS=$(SECS) XNU_ARGS="$(ARGS)" … mtk/tools/xnu-cycle.sh
+	XNU_SECS=$(SECS) XNU_ARGS="$(ARGS)" … $(CYCLE)
```
`devices/manifest.conf`：+`soc mt6895 / path mtk/mt6895`、+`device xaga / path devices/xaga`（无 `repository`/`revision`，即「先放本树」）。

---

## 6. 操作手册：「要改 X 必须改这几个文件」

> 通用铁律：**内核参数改 `xaga.config` 再 `make xaga_defconfig`；loader 硬件改 `board.h`；`-D` 只对 `#ifndef` 包着的宏有用。**

### 6.1 加 / 改一个内核 boot-arg（最常见）

```
1) devices/xaga/configs/xaga.config:40   CONFIG_BOOTARGS="… 新参数=值"
2) configs/boot-args.lst                 确认新名字在里面（没有就 python3 tools/boot-args.py --lst > configs/boot-args.lst）
3) make xaga_defconfig                   .config 更新
4) 验证：tools/hakios-config.py check && tools/hakios-config.py args
```
**不要**只改 `.config`（会被 3 抹掉）；**不要**只改 `board.h`（会被 `xaga-cycle.sh:78` 的 `-D` 替换）。

### 6.2 让 `headset=1` 真正生效（本次的核心结论）

第 1 步（**已经做了**，2026-11-15 00:46:15）：`devices/xaga/configs/xaga.config:40` 的行尾已加上 ` headset=1`。

第 2 步（**还没做，就差这一步**）：
```
cd /home/lizi/IOS/hakios-mtk && make xaga_defconfig
```
第 3 步验证：
```
tools/hakios-config.py args | grep -o 'headset=1'         # 必须打印 headset=1
tools/hakios-config.py check                              # 必须 0 problem
grep -o 'headset=1' .config                               # .config:71 里出现
```
第 4 步（可选，确认 loader 侧）：`sh docs/headset-wip/build-config-dump.sh | sed -n '/7.1/,/7.2/p'`，应显示
`the .config's arguments … have headset= : yes`。

不需要重编内核：`headset=1` 是运行期 boot-arg，驱动早已编入（`.config:39`、`hakios.drivers.h:2`、`hakios-kernel` 26 处 headset 字符串）。
不需要重编 loader：`xaga-cycle.sh:81` 每次运行都会重编，`-DXNU_EXTRA_ARGS` 从 `.config` 取。

（影子树实测：`headset=1` 后 170 字符、0 problem；170 + 260（`RUN_OVERHEAD`）< 607，长度安全。）

`board.h:874` 的 `headset=1` **可以留着**（手敲 `mtk/loader/build.sh` 不带 `-D` 时有用），但它不是一次运行的生效路径。
前置条件已满足：`CONFIG_HAKIOS_MT6895_HEADSET=y`（`.config:39`）、`buttons=2`（`.config:71`）、调用链 `hakios.mt6895_keys.c:506 → hakios.mt6895_headset.c` 的 `hakios_mt6895_headset_init()`。

### 6.3 改 loader 的硬件地址 / 行为

```
mtk/mt6895/loader/board.h       ← 唯一的地方
  #ifndef 包着的（多数）：也可 -D 覆盖，但不推荐（下次忘了就丢）
  无 #ifndef 的 4 个：MENU_DEFAULT_XNU(401) / BOARD_NO_UART(896) / BOARD_NO_OVL(897) / BOARD_NO_PLOG(908)
                       只能改文件本身
验证：sh docs/headset-wip/build-config-dump.sh | sed -n '/4  THE LOADER/,/4b/p'
```

### 6.4 改运行时长 / watcher

| 想改 | 改这里 | 别改 |
|---|---|---|
| XNU 跑多久后交还 | `make phone … SECS=45`（`Makefile:53`）或 `XNU_SECS=45 mtk/tools/xaga-cycle.sh` | `board.h:793`（会被覆盖） |
| watcher 多少秒后复位 | `XNU_WATCH=45 mtk/tools/xaga-cycle.sh` | `XNU_WATCH_SECONDS=` 环境变量（脚本不读） |
| 硬件看门狗 | `board.h:799`（当前 0，故意关） | 任何 `-D`（脚本不传该 flag） |

### 6.5 改编译哪些驱动

```
mtk/mt6895/configs/mt6895.config     ← CONFIG_HAKIOS_*=y / "is not set"
（或 devices/xaga/configs/xaga.config 覆盖它，因为后者优先级更高）
然后 make xaga_defconfig && make kernel
验证：tools/hakios-config.py show | head -2
      grep HAKIOS_DRIVERS_LIST xnu-src/xnu-8796.121.2/bsd/dev/hakios.drivers.h
```
注意 `mtk/mt6895/configs/mt6895.config` **未跟踪**（`??`）：换机器 / 新 clone 就没有它，`make xaga_defconfig` 会失败（`find_config` 找不到 `mt6895.config`）。

### 6.6 让 USB 驱动真的跑起来（如果要）

```
1) devices/xaga/configs/xaga.config:40   加 usb=2
2) make xaga_defconfig
3) 但先读 board.h:847-851：USB 驱动会写 IP_PW_CTRL1 的 HOST_PDN、走完 PHY 上电，
   VBUS_VALID 丢了写不回来，Android 之后起不来，只能物理重插。
```
也就是说：**驱动编进去是「准备好了」，不加 `usb=2` 是「故意的」。**

### 6.7 改 mali 的某个行为

```
1) 确认参数名：grep -o 'PE_parse_boot_argn("mali[a-z0-9_]*"' mtk/mt6895/drivers/gpu/hakios.mali610.c | sort -u
2) 加进 devices/xaga/configs/xaga.config:40 的 CONFIG_BOOTARGS
3) make xaga_defconfig
```
40 个名字全在 `configs/boot-args.lst` 里，不会被拒。`mali` 数值 >6 会被夹到 6（`hakios.mali610.c:5109`）。

### 6.8 提交这些改动（**危险，先读**）

三件事必须先分清：

| 类别 | 文件 | 要不要 `git add` |
|---|---|---|
| **intent-to-add 的 22 个 `A`** | index blob 是 `e69de29`（空），工作区有真实内容 | **要**，按路径逐个 `git add` |
| **被 gitignore 的设备文件** | `devices/xaga/configs/xaga.config`、`xaga_defconfig`、`device.conf` | **不用**：`.gitignore:120` 的 `/devices/*` 就是这意思（`devices/` 是 `hakios-device.py sync` 克隆进来的仓库，不是本仓库的文件） |
| **真·未跟踪且没被忽略** | `mtk/mt6895/configs/mt6895.config`、`xnu/files/bsd/sys/hakios.pmif.h` | **要**，否则新 clone 上 `make xaga_defconfig` 找不到 `mt6895.config` 直接失败 |

```
# 只加，不提交；绝不 -a / stash / reset / checkout
git add -- mtk/mt6895/loader/board.h mtk/mt6895/soc.conf mtk/loader/ufs.h \
           mtk/mt6895/drivers/gpu/conf mtk/mt6895/drivers/gpu/hakios.mali610.c \
           mtk/mt6895/drivers/input/conf mtk/mt6895/drivers/input/hakios.nvt.c \
           mtk/mt6895/drivers/input/hakios.mt6895_keys.c \
           mtk/mt6895/drivers/usb/conf mtk/mt6895/drivers/usb/hakios.mt6895_usb.c \
           drivers/gpu/hakios.mali610_fw.h drivers/input/hakios.nvt_fw.h \
           mtk/tools/*.py mtk/tools/xaga-*.sh mtk/tools/xaga-*.md
git add -- mtk/mt6895/configs/mt6895.config xnu/files/bsd/sys/hakios.pmif.h

# 确认 index 里不再是空 blob：
git ls-files -s mtk/mt6895/loader/board.h     # blob 不能是 e69de29…
```

`git diff` 对 intent-to-add 的文件显示成「全新增」，`git diff --cached` 显示为空——**只 `git commit`（不加 `-a`）会提交空文件**。

---

## 附录 A：`build-config-dump.sh` 的真实输出

命令：`sh docs/headset-wip/build-config-dump.sh`，`sh -n` 通过，exit 0，只读（不跑 make、不跑 build.sh、不写树）。
下面是 2026-11-15 00:54 那次运行的**完整原始输出**，未编辑。
两处随运行而变：`epoch=`（脚本用 `$(date +%s)` 模拟 `xaga-cycle.sh:78` 的 run id）和文件 mtime。

```

===== 0  WHERE THIS IS =====
tree            /home/lizi/IOS/hakios-mtk
git HEAD        4ebad21a8a1182422476889d9ddb90af6843f442
git HEAD date   2026-11-14 23:12:23 +0800
dirty files     57 (git status --porcelain | wc -l)
host            Linux 7.0.0-43-generic x86_64
clang           Ubuntu clang version 21.1.8 (6ubuntu1)
python3         Python 3.14.4

===== 1  THE FILES THE CONFIGURATION IS MADE FROM =====

--- tools/hakios-config.py show (the .config, its symbols and its arguments) ---
HakiOS configuration .config, kernel.ver=2.1.13-hakios-darwin22.5-mt6895-g4ebad21a-dirty
drivers built in: mt6895_headset mali610 mtk6895_keys nvt mt6895_usb
drivers left out: adsp speaker sound mic virtio_sound bt camera torch socinfo adreno haptic virtio_input goodix buttons qmi modem virtio_net battery cpufreq thermal pmic battguard ufs virtio_blk wifi mali mtk_keys mtk_scp tcm mtk_emmc mtk_connsys
  CONFIG_DEVICE        = "xaga"         (a fact about the build)
  CONFIG_SOC           = "mt6895"       (a fact about the build)
  CONFIG_FIRMWARE      = "device"       (a fact about the build)
  CONFIG_CPUS          = 4              -> cpus=4
  CONFIG_BOOTARGS      = "buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1" (a fact about the build)
  CONFIG_FB_INUSE      = y              -> fb_inuse=1
  CONFIG_FB_QUEUE      = 1              -> fb_queue=1
  CONFIG_FBFIT         = 0              -> fbfit=0
  CONFIG_FBSCALE       = 2              -> fbscale=2
boot-arguments: cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1 fb_inuse=1 fb_queue=1 fbfit=0 fbscale=2
firmware wanted from the phone: none

--- tools/hakios-config.py check (its own consistency check) ---
.config: 45 symbols set, 160 characters of boot-arguments (the kernel holds 607; a run adds about 260)

--- the .config's own header: the files it was made from ---
#
# Made by tools/hakios-config.py from configs/defconfig, configs/common_defconfig, mtk/mt6895/configs/mt6895.config, devices/xaga/configs/xaga.config, devices/xaga/configs/xaga_defconfig.
# Do not edit it: change a defconfig or a fragment in configs/, and make the .config again.
#

--- mtimes, newest last ---
configs/defconfig                              2026-10-31 19:53:13
configs/common_defconfig                       2026-10-31 19:53:13
mtk/mt6895/configs/mt6895.config               2026-11-14 23:28:16
devices/xaga/configs/xaga.config               2026-11-15 00:46:15
devices/xaga/configs/xaga_defconfig            2026-11-15 00:46:15
devices/xaga/device.conf                       2026-11-15 00:46:15
.config                                        2026-11-14 02:48:43
configs/boot-args.lst                          2026-11-14 22:44:50
configs/symbols.tsv                            2026-11-06 14:09:26
mtk/mt6895/loader/board.h                      2026-11-14 02:04:44
mtk/loader/mtkloader.c                         2026-11-14 23:17:59
mtk/loader/build.sh                            2026-11-01 00:25:37

===== 2  THE DRIVERS COMPILED IN =====
drivers built in: mt6895_headset mali610 mtk6895_keys nvt mt6895_usb
drivers left out: adsp speaker sound mic virtio_sound bt camera torch socinfo adreno haptic virtio_input goodix buttons qmi modem virtio_net battery cpufreq thermal pmic battguard ufs virtio_blk wifi mali mtk_keys mtk_scp tcm mtk_emmc mtk_connsys

--- the CONFIG_HAKIOS_* symbols the .config sets to y ---
39:CONFIG_HAKIOS_MT6895_HEADSET=y
40:CONFIG_HAKIOS_MALI610=y
41:CONFIG_HAKIOS_MTK6895_KEYS=y
42:CONFIG_HAKIOS_NVT=y
43:CONFIG_HAKIOS_MT6895_USB=y

--- what the build tree was last told (xnu-src/.../bsd/dev/hakios.drivers.h) ---
2:#define HAKIOS_DRIVERS_LIST     "mt6895_headset mali610 mtk6895_keys nvt mt6895_usb"
3:#define HAKIOS_DRIVERS_COUNT    5
4:#define HAKIOS_DRIVERS_CONFIG   ".config"
written         2026-11-14 01:44:29

--- each driver's declaration (drivers/*/conf, and the SoC's own) ---
mtk/mt6895/drivers/audio/conf            MT6895_HEADSET     declared
mtk/mt6895/drivers/audio/conf                               	requires MTK6895_KEYS
mtk/mt6895/drivers/gpu/conf              MALI610            declared
mtk/mt6895/drivers/input/conf            MTK6895_KEYS       declared
mtk/mt6895/drivers/input/conf            NVT                declared
mtk/mt6895/drivers/usb/conf              MT6895_USB         declared

===== 3  THE .config's BOOT-ARGUMENTS (what a run passes as XNU_EXTRA_ARGS) =====
cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1 fb_inuse=1 fb_queue=1 fbfit=0 fbscale=2

characters      160
words           15

--- CONFIG_BOOTARGS as it stands in the .config ---
71:CONFIG_BOOTARGS="buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1"

--- the same line in the file that sets it ---
40:CONFIG_BOOTARGS="buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1 headset=1"

===== 4  THE LOADER's VIEW: board.h, WITH THE DEFAULTS AND THE -D OVERRIDES RESOLVED =====

--- board.h as the build resolves it (no -D) ---
  BOARD_NAME                 xaga
  XNU_EXTRA_ARGS             cpus=4 buttons=2 touchscreen=3 keylog=1 fbwidth=1080 fbheight=2460 fb_disp=1 headset=1
  XNU_CORES                  4
  XNU_RETURN_AFTER           150
  XNU_WATCH_SECONDS          120
  XNU_WDT_SECONDS            0
  BOARD_NO_PLOG              1
  BOARD_NO_UART              1
  BOARD_NO_OVL               1
  BOARD_NO_FB_TEXT           (undefined: no #define here, and no #ifndef default)
  BOARD_TOUCH                0
  XNU_SYSTEM_IMAGE           1
  XNU_ROOT_EMMC              0
  BOARD_STORAGE_UFS          1
  UFS_USE_LK_LIST            1
  MENU_DEFAULT_XNU           1
  BOARD_RETURN_TO_ANDROID    1
  BOARD_MALI_POWER           1
  BOARD_MALI_PREPOWER        1
  BOARD_MALI_DOMAINS         2
  BOARD_MALI_STAGE           0
  BOARD_MALI_EARLY           0
  BOARD_MALI_STOP            0
  BOARD_REPORT_PAGES         0
  XNU_BIG_CLOCK              0
  BOARD_REPORT_SYSTEM_IMAGE  1
  BOARD_USB_PROBE            0
  BOARD_PMIF_PROBE           0
  BOARD_DISP_PROBE           0
  BOARD_MALI_PROBE           0

--- the same with the flags xaga-cycle.sh passes (the .config's arguments, SECS=600) ---
  BOARD_NAME                 xaga
  XNU_EXTRA_ARGS             cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1 fb_inuse=1 fb_queue=1 fbfit=0 fbscale=2 epoch=1794675281
  XNU_CORES                  4
  XNU_RETURN_AFTER           600
  XNU_WATCH_SECONDS          120
  XNU_WDT_SECONDS            0
  BOARD_NO_PLOG              1
  BOARD_NO_UART              1
  BOARD_NO_OVL               1
  BOARD_NO_FB_TEXT           (undefined: no #define here, and no #ifndef default)
  BOARD_TOUCH                0
  XNU_SYSTEM_IMAGE           1
  XNU_ROOT_EMMC              0
  BOARD_STORAGE_UFS          1
  UFS_USE_LK_LIST            1
  MENU_DEFAULT_XNU           1
  BOARD_RETURN_TO_ANDROID    1
  BOARD_MALI_POWER           1
  BOARD_MALI_PREPOWER        1
  BOARD_MALI_DOMAINS         2
  BOARD_MALI_STAGE           0
  BOARD_MALI_EARLY           0
  BOARD_MALI_STOP            0
  BOARD_REPORT_PAGES         0
  XNU_BIG_CLOCK              0
  BOARD_REPORT_SYSTEM_IMAGE  1
  BOARD_USB_PROBE            0
  BOARD_PMIF_PROBE           0
  BOARD_DISP_PROBE           0
  BOARD_MALI_PROBE           0

--- the boot_args.commandLine the loader builds (rd=md0 + XNU_EXTRA_ARGS + loader_return_after) ---
rd=md0 cpus=4 buttons=2 touchscreen=3 keylog=1 fbwidth=1080 fbheight=2460 fb_disp=1 headset=1 loader_return_after= 150

characters      119 of 607 (BOOT_LINE_LENGTH 608 less the terminator, mtkloader.c:344)
words           11

--- the same string as the last built loader has it ---
boot args: rd=md0 cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1 fb_inuse=1 fb_queue=1 fbfit=0 fbscale=2 loader_return_after=70
mtkloader.bin   59720 bytes, 2026-11-14 02:48:53

===== 4b WHAT A BUILD WITHOUT DEVICE=xaga GETS (the MT6768 defaults) =====
  mtk/loader/mtkloader.c:45:#define XNU_PHYS_BASE       0xc0000000ULL
  mtk/loader/mtkloader.c:48:#define XNU_MEM_SIZE        0xfe000000ULL       /* up to dramc-rk1 at 0x1bffff000; room for the system image */
  mtk/loader/mtkloader.c:522:#define UART_BASE           0x11002000ULL       /* MT6768: the console LK leaves behind */
  mtk/loader/mtkdbg.h:43:#define TOPRGU_BASE          0x10007000ULL
  mtk/loader/mtkdbg.h:27:#define RAMOOPS_CONSOLE      0x4d09f000ULL
  mtk/loader/mtkdbg.h:55:#define LK_FB_BASE           0x787b0000ULL
  mtk/loader/mtkdbg.h:81:#define OVL0_BASE            0x1400b000ULL
  mtk/loader/mtkloader.c:215:#define XNU_EXTRA_ARGS      "cpus=1"
  mtk/loader/mtkloader.c:94:#define XNU_CORES           4
  mtk/loader/mtkloader.c:156:#define XNU_RETURN_AFTER    "0"
  mtk/loader/mtkloader.c:72:#define XNU_WATCH_SECONDS   0
  mtk/loader/mtkloader.c:68:#define XNU_WDT_SECONDS     0
  mtk/loader/mtkloader.c:271:#define XNU_SYSTEM_IMAGE    1
  mtk/loader/mtkloader.c:188:#define MENU_DEFAULT_XNU    0

===== 5  THE COMMAND LINE A RUN USES =====

--- make phone DEVICE=xaga: the recipe, and what it sets ---
  Makefile:165:	XNU_SECS=$(SECS) XNU_ARGS="$(ARGS)" XNU_LOG_OUT=$${XNU_LOG_OUT:-logs/xnu-$(DEVICE).log} $(CYCLE)
  Makefile:57:CYCLE ?= $(if $(wildcard mtk/tools/$(DEVICE)-cycle.sh),mtk/tools/$(DEVICE)-cycle.sh,mtk/tools/xnu-cycle.sh)
  Makefile:66:ARGS ?= $(shell tools/hakios-config.py args)
  Makefile:69:ARGS ?= touchscreen=1 gpu=1 adsp=1 sound=4 wifi=1 haptic=5 modem=1 modem_again=1 modem_buffer=0xa8000000
  Makefile:50:DEVICE ?= x6826b

--- mtk/loader/build.sh: how the board header is chosen ---
  if [ -z "${BOARD+x}" ]; then
  	if [ -n "${DEVICE:-}" ]; then
  		SOC=$(awk '$1 == "soc" { print $2; exit }' "$ROOT/devices/$DEVICE/device.conf" 2>/dev/null)
  		if [ -n "$SOC" ] && [ -f "$ROOT/mtk/$SOC/loader/board.h" ]; then
  			BOARD="$ROOT/mtk/$SOC/loader/board.h"
  		fi
  	fi
  fi
  if [ -n "${BOARD:-}" ]; then
  	[ -f "$BOARD" ] || { echo "mtkloader: no board header at $BOARD" >&2; exit 1; }
  	CFLAGS="$CFLAGS -DBOARD_HEADER=\"$BOARD\""
  	echo "mtkloader: board $BOARD"
  fi

--- mtk/tools/xaga-cycle.sh: what it passes on the compiler's command line ---
  xaga-cycle.sh:30:SECS=${XNU_SECS:-45}
  xaga-cycle.sh:37:XNU_WATCH=${XNU_WATCH:-120}
  xaga-cycle.sh:70:# DEVICE=xaga is what makes mtk/loader/build.sh pick up mtk/mt6895/loader/board.h; without it the
  xaga-cycle.sh:77:TEST_FLAGS=(-DXNU_CORES="$CORES" -DXNU_WATCH_SECONDS="$XNU_WATCH"
  xaga-cycle.sh:81:DEVICE=xaga "$ROOT/mtk/loader/build.sh" "${TEST_FLAGS[@]}" "$@" >/dev/null

--- the length budget tools/hakios-config.py enforces ---
  38:KERNEL_LINE = 607                   # BOOT_LINE_LENGTH 608 less the terminator: what the kernel's boot_args can hold
  39:RUN_OVERHEAD = 260                  # what a run adds itself: the loader's own arguments, xl's, the runner's (measured: ~190-250)
  mtkloader.c:344:#define BOOT_LINE_LENGTH 608

===== 6  WHAT THE FILES ON DISK WERE BUILT FROM =====
hakios-kernel                  15286976 bytes  2026-11-15 00:35:09  c8791f336588e55c
xnu-flashed-slot_a.img         22982656 bytes  2026-11-14 02:49:10  9a2e083a43aee95a
build-payload.bin              19480504 bytes  2026-11-14 02:06:04  c61e4b2a7c30805e
mtk/loader/mtkloader.bin          59720 bytes  2026-11-14 02:48:53  0346f11b0145a59a

--- the newest source each one depends on ---
mtk/mt6895/loader/board.h                                2026-11-14 02:04:44
mtk/loader/mtkloader.c                                   2026-11-14 23:17:59
mtk/loader/start.S                                       2026-11-04 02:17:43
mtk/mt6895/drivers/audio/hakios.mt6895_headset.c         2026-11-15 00:54:01
xnu/files/bsd/sys/hakios.pmif.h                          2026-11-15 00:31:56

===== 7  MISMATCHES THIS DUMP CAN SEE =====

--- 7.1  headset=1: where it is written, and whether a run gets it ---
    devices/xaga/configs/xaga.config:40 says headset= : yes
    the .config's arguments (what xaga-cycle.sh passes) have headset= : no
 1. the fragment HAS headset= but the .config does not: the .config predates the edit, and mtk/tools/xaga-cycle.sh:35 reads the .config, so a run still boots WITHOUT headset=1. Run make xaga_defconfig (no kernel rebuild needed: the driver is already compiled in)
    board.h:874 also has headset=1 -- for a loader built with no -DXNU_EXTRA_ARGS (a bare mtk/loader/build.sh), not for a run
 2. CONFIG_HAKIOS_MT6895_USB=y but the arguments have no usb=, so hakios_mt6895_usb_init() returns at its first line (mtk/mt6895/drivers/usb/hakios.mt6895_usb.c:1866): the driver is compiled in and never runs. mtk/mt6895/configs/mt6895.config:83 says xaga.config turns it on -- it does not

--- 7.2  arguments named in CONFIG_BOOTARGS that configs/boot-args.lst does not have ---
    (none listed above: every argument in the .config has a reader in the list)

--- 7.3  the mali arguments: the driver's readers vs the list ---
    40 mali arguments are read by hakios.mali610.c; 42 lines in configs/boot-args.lst begin with mali
    in the list with no reader under mtk/mt6895/drivers/ (they belong to the MT6768's hakios.mali.c / hakios.mtk_keys.c): mali_fastmap mali_mhz 

--- 7.4  the generated catalogues against the source (tools/boot-args.py --check) ---
    boot-args: docs/boot-args.md is out of date with the source: run  tools/boot-args.py --markdown > docs/boot-args.md
    boot-args: configs/symbols.tsv is out of date with the source: run  tools/boot-args.py --symbols > configs/symbols.tsv
 3. configs/symbols.tsv and docs/boot-args.md are stale. symbols.tsv is what CONFIG_BOOTARGS is checked against: a stale one rejects a correct argument and accepts a typo. Regenerate with tools/boot-args.py --symbols > configs/symbols.tsv (and --markdown for the doc), in the build tree, then re-run make xaga_defconfig

--- 7.5  an argument a run adds that no configuration can see ---
    epoch= is added by mtk/tools/xaga-cycle.sh:78 (-DXNU_EXTRA_ARGS="<the .config args> epoch=$(date +%s)") and is in no configuration file; it identifies the run in the log, by design (docs/configs.md: the few the loader and the runner set themselves)

--- 7.6  board.h macros a -D cannot change (an unconditional #define, no #ifndef) ---
    MENU_DEFAULT_XNU = 1 at mtk/mt6895/loader/board.h:401 -- -DMENU_DEFAULT_XNU=0 is overridden with a -Wmacro-redefined warning and the build keeps 1
    BOARD_NO_UART = 1 at mtk/mt6895/loader/board.h:896 -- -DBOARD_NO_UART=0 is overridden with a -Wmacro-redefined warning and the build keeps 1
    BOARD_NO_OVL = 1 at mtk/mt6895/loader/board.h:897 -- -DBOARD_NO_OVL=0 is overridden with a -Wmacro-redefined warning and the build keeps 1
    BOARD_NO_PLOG = 1 at mtk/mt6895/loader/board.h:908 -- -DBOARD_NO_PLOG=0 is overridden with a -Wmacro-redefined warning and the build keeps 1

--- 7.7  the .config against the fragments it claims to be made from ---
 4. .config differs from what xaga_defconfig makes today (diff below): a fragment changed after the last make xaga_defconfig, so the .config -- and the kernel built from it -- is older than the files
    @@ -68,7 +68,7 @@
     # CONFIG_BATTERY_STOP is not set
     # CONFIG_BATTERY_STOP_POLL is not set
     # CONFIG_PMIC is not set
    -CONFIG_BOOTARGS="buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1"
    +CONFIG_BOOTARGS="buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=6 mali_cold=0 mali_preas0=1 headset=1"
     # CONFIG_ADRENO is not set
     # CONFIG_ADRENO_BLIND is not set
     # CONFIG_ADRENO_DIRTY is not set

--- 7.8  files a build depends on that git does not carry ---
 5. mtk/mt6895/configs/mt6895.config is not tracked and not ignored: a fresh clone has no such file, and a build there fails or silently gets different values. git add it
    mtk/mt6895/loader/board.h: tracked, changed since HEAD
    devices/xaga/configs/xaga.config: ignored on purpose (.gitignore:120:/devices/* devices/xaga/configs/xaga.config) -- it is a device repository's own file, cloned rather than committed (devices/manifest.conf)
    devices/xaga/configs/xaga_defconfig: ignored on purpose (.gitignore:120:/devices/* devices/xaga/configs/xaga_defconfig) -- it is a device repository's own file, cloned rather than committed (devices/manifest.conf)
 6. xnu/files/bsd/sys/hakios.pmif.h is not tracked and not ignored: a fresh clone has no such file, and a build there fails or silently gets different values. git add it

--- 7.9  tools/drivers-check.py (the tree's own driver map check) ---
    drivers-check: drivers/usb is not a known subsystem directory
    drivers-check: 1 problem(s)
 7. drivers-check is not clean (see above): CI runs it, and a driver it complains about is one drivers/map does not place in the kernel tree

7 numbered finding(s); each one is explained, with file:line, in docs/headset-wip/findings/BUILD-CONFIG-AUDIT.md
```

## 附录 B：审计方法与被否掉的说法

| 说法 | 核实结果 |
|---|---|
| 「`devices/xaga/board.h` 和 `mtk/mt6895/loader/board.h` 是两个文件」 | **否**。只有后者。`devices/xaga/` 无 `board.h` |
| 「`board.h.bak` 是 692 flags 的版本」 | **树里没有 `board.h.bak`**（`find . -name '*.bak'` 为空）。692 那次的 flags 在 `logs/headset-resume-20261114-220244/loader-rebuild-commands.sh:3` 里，是 `-D` 列表，不是文件 |
| 「`build.sh` 不带 `DEVICE=xaga` → loader 41240 字节」 | 「会用 MT6768 地址」**已证实**；「41240 字节」**未验证**（树里无该尺寸产物；当前 `mtkloader.bin` 59720 字节） |
| 「`.config` 覆盖 `board.h` 的 `XNU_EXTRA_ARGS`」 | **部分对**：`.config` 本身不直接覆盖，是 `xaga-cycle.sh:78` 把 `.config` 的 args 变成 `-DXNU_EXTRA_ARGS` 才覆盖。手敲 `build.sh` 时 `.config` 完全不参与 |
| 「`CONFIG_HAKIOS_MT6895_HEADSET=y` 是否真开着」 | **是**。`.config:39` + `hakios.drivers.h:2` + 内核二进制里有 26 处 headset 字符串 |
| 「`mali` 参数 4 个 vs 驱动 41 个」 | 现在是 **42 行 lst / 40 个驱动读者**（历史数字已过时）。差的 2 个是 `mali_fastmap`、`mali_mhz`（MT6768 专用） |
| 「`BOARD_NO_PLOG=1` 让 `plog_putc()` 变空」 | **是**，`mtkdbg.h:274-276`。且连带 `dbg_putc()` 在 `BOARD_NO_FB_TEXT` 下连屏幕都不写（`mtkdbg.h:412-418`） |
| 「`gpueb-ipi.h` 的 `GPUEB_BARRIER` 默认是纯编译器屏障」 | 树里**没有** `gpueb-ipi.h`（`find . -name gpueb-ipi.h` 为空，`logs/` 之外）。该头文件在 `mali610.c` 里是内联的 `dsb sy`/`isb`，无同名宏。此说法**在本树不适用**，标「未验证」 |

## 附录 C：本次审计做过的所有命令（可复现）

全部只读；预处理与临时输出都在 `/tmp`（`/tmp/ppchk/`、`/tmp/shadowtree/`、`/tmp/shadow2..4/`、`/tmp/dump-out*.txt`）。

```sh
# 树状态
git status --porcelain=v1 | awk '{print substr($0,1,2)}' | sort | uniq -c   # 29 M / 22 A / 6 ??
git ls-files -s mtk/mt6895/loader/board.h                                   # e69de29… = 空 blob

# 配置真值
tools/hakios-config.py show | head -3
tools/hakios-config.py args
tools/hakios-config.py check
tools/hakios-config.py defconfig xaga_defconfig -o /tmp/regen-xaga.config && diff -u .config /tmp/regen-xaga.config

# 宏展开（/tmp 下）
clang --target=aarch64-linux-gnu -E -P \
  -DBOARD_HEADER='"/home/lizi/IOS/hakios-mtk/mtk/mt6895/loader/board.h"' \
  -DXNU_ROOT_ARG='"rd=md0"' \
  -DXNU_BOOT_ARGS='XNU_ROOT_ARG " " XNU_EXTRA_ARGS " loader_return_after=" XNU_RETURN_AFTER' t.c

# 一致性
python3 tools/boot-args.py --check
python3 tools/boot-args.py --lst   > /tmp/lst-new.txt && diff -u configs/boot-args.lst /tmp/lst-new.txt
python3 tools/boot-args.py --symbols > /tmp/sym-new.tsv && diff -u configs/symbols.tsv /tmp/sym-new.tsv
python3 tools/drivers-check.py

# 驱动是否真编入
strings -a hakios-kernel | grep -ci headset                                # 26
cat xnu-src/xnu-8796.121.2/bsd/dev/hakios.drivers.h                        # 5 个驱动

# 影子树验证（不改原树）
/tmp/shadowtree: 符号链接 configs/ drivers/ mtk/ xnu/ tools/ + 一份改过的 xaga.config
```
