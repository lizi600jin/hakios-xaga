# CMD-PACK-CPU-PROBE-713 逐条对照

> 2026-11-17 00:50，DSH。**CMD 身份：3519 B /
> `b7f7df7da45beef631231281763a9a61b5cbecf25a575252cd0ccc0c2de58710`**

---

## 前置条件

| CMD 要求 | 实测 |
|---|---|
| 读 HANDOFF、REVIEW-AND-PLAN、arguments713.json、independent-kernel-review.json | ✅ 四份都读过 |
| **不重建 kernel-src、不重建内核、不改冻结输入** | ✅ 未做 |
| 内核 15321336 B / `a80231ed…` | ✅ 一致 |
| 身份不匹配则停止 | ✅ 无失配 |

---

## 第 1 条：参数串

| CMD 要求 | 实测 |
|---|---|
| **base 479 B** | **479** ✅ |
| **runtime 539 B** | **539** ✅ |
| **withNUL 540 B** | **540** ✅ |
| **<= 608** | **True**，余量 **68** ✅ |
| `mtk_cpu_probe=1` | 1 次 ✅ |
| `headset=0` | 1 次 ✅ |
| **无 `hp_*` 参数** | `hp_` **0 次** ✅ |
| `mali_trial=713` | 1 次 ✅ |
| 保留四核/GPU 行为参数 | ✅ 与 712 逐项相同，仅 trial 号与 headset/probe 变 |
| `loader_return_after=150` | 1 次 ✅ |
| Watcher=0、硬件 WDT=0 | ✅ `-DXNU_WATCH_SECONDS=0` |

---

## 第 2 条：loader 源与编译 argv

| CMD 要求 | 实测 |
|---|---|
| 从 trial712 复制冻结 loader 源镜像 | ✅ **37 文件**，`mtkloader.c` = `60647f896d027089…` |
| 用 trial712/compile-commands.json 的结构化 argv | ✅ 已对照 |
| **只替换 output/source 路径与 XNU_EXTRA_ARGS** | ✅ **argv 长度完全一致**（27/27/9），差异仅 4 处/每条，全部是路径或 `XNU_EXTRA_ARGS` |
| **不把 JSON 字符串插进 shell 命令** | ✅ 全部用 Python `subprocess` 传 argv 列表 |
| 保留真实换行与引号 | ✅ `BOARD_HEADER` 的字面双引号在 argv 元素内 |
| 记录 native rc 与完整 argv | ✅ `compile-commands.json`（6378 B） |
| 不改生产 loader 或 board.h | ✅ 未改 |

---

## 第 3 条：mkpayload

| CMD 要求 | 实测 |
|---|---|
| `mkpayload.py --no-version` | ✅ |
| kernel = 新 713 内核（显式） | ✅ 绝对路径 |
| ramdisk / kernel.ver 来自 trial710 revision2 | ✅ `78985f67…` / `363b5257…` |
| **24 B 全零 XNUP 终止项** | ✅ `pos=19515776` |
| 规范 padding | ✅ kernel 8 B padding 全零 |
| **exact EOF** | ✅ **True** |
| kernel.ver 是遗留元数据，不是新内核身份 | ✅ 文档已注明 |

---

## 第 4 条：chainboot

| CMD 要求 | 实测 |
|---|---|
| stock `boot_a_stock_real.img` | ✅ 67108864 B / `c5f8e7bd…` / md5 `a63b2e35…` |
| `--partition-size 64M` | ✅ |
| 结构化 argv 与 native rc | ✅ `chainboot-cmd.json` |
| native rc | **0** ✅ |

---

## 第 5 条：严格解包

| CMD 要求 | 实测 |
|---|---|
| **stock header 仅 `kernel_size` 字节 8..11 不同** | ✅ **差异恰好 4 字节，位置 `[8,9,10,11]`** |
| 保留 ramdisk | ✅ 1867873 相同 |
| 保留 header 24..40 | ✅ 相同 |
| 保留 cmdline | ✅ `44..1068` 逐字节相同 |
| **保留 signature size 与 signature** | 🔴 **原写「`signature_size = 0`」是错的**。正确：字段在 offset **1580**，值 **4096**；三方签名均 `AVB0` 且 SHA256 `692c87ad…` 逐字节相同。见 [`SIGNATURE-CORRECTION.md`](SIGNATURE-CORRECTION.md) |
| 保留 stock Android Image | ✅ 逐字节相同 `12d255cd…` |
| **loader 差异只在于文档化的 0x10/0x28/0x30 补丁** | ✅ **恰好 10 字节**：`0x12/0x13/0x28-2b/0x30-33` |
| 零填充 | ✅ `[48768, 0x40000)` 全零 |
| 嵌套 payload 与 kernel 精确 | ✅ payload 逐字节相同；kernel `a80231ed…` |
| **24 B 终止项与 exact EOF** | ✅ |
| **检查真正的 NUL 结尾 base 参数体，不只是 printf 串** | ✅ **本体在 `0x9b4d`，479 B，逐字节匹配**；另两处后随 `\n` 是 printf 片段 |
| **runtime 608 限制** | ✅ 540 <= 608，余量 68 |
| 关键键唯一 | ✅ 六项各 1 次 |
| **无 stale trial / headset 输出参数 / gpueb / 遗留 cpufreq / ddr 请求** | ✅ 13 项禁止词**全部 0 次** |

---

## 第 6 条：记录与守卫

| CMD 要求 | 实测 |
|---|---|
| 完整 SHA256/MD5 与尺寸 | ✅ 见 `ARTIFACTS.txt` |
| **不覆盖 trial710/711/712、shared kernel、任何先前 BUILD** | ✅ 三个内核 `cac498da`/`24df55bf`/`a2f6db11` 未变；三个镜像 `ca161c1b`/`3f57a806`/`b97f9672` 未变；shared `67f9f933` 未变 |
| 打包后复检 stock 与旧守卫 | ✅ |

---

## 第 7 条：归档

| CMD 要求 | 实测 |
|---|---|
| 归档到新 `D/docs/headset-wip/raw/pack-cpu-probe-713/` | ✅ |
| README 含精确输入/命令/native rc/参数/解包结果/清单 | ✅ |
| **不上传原始环境 MP3** | ✅ 未上传 |
| **公开记录脱敏序列号** | ✅ 无序列号 |
| **只提交 docs、不 push 生产仓** | ✅ |
| **不 stage 旧 WIP** | ✅ GPUEB patch 未动 |
| **不 stash/reset/checkout/clean/gc/prune** | ✅ 未做 |
| **不自动额外测试** | ✅ 未做额外设备测试 |

---

## 我主动补做的三项（CMD 未逐字要求但属其意图）

| # | 补做 |
|---|---|
| 1 | **独立核算 CMD 声称的三个字节数**（479/539/540），不只信 json |
| 2 | **argv 与 trial712 模板逐元素对照**，确认只差路径与 `XNU_EXTRA_ARGS` |
| 3 | **header 全 8 字段对照 + 签名段实测**，确认 `signature_size=0` |
