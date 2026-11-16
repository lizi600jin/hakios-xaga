# PACK-710：trial710 主机打包（未刷写）

> ## 🔴 本文件描述的是**已被拒绝**的首版候选
>
> **本目录的 `boot.img`（sha256 `201688e5…`）与 `payload.bin`（19515600 B）缺少
> XNUP 必需的 24 字节全零终止项，结构不合格，已拒绝、从未刷写。**
>
> **当前候选是 `revision2/`（payload 19515624 B、boot.img `ca161c1b…`），
> 已在实际设备上完成一次试验并通过判据，随后恢复原厂。**
>
> **本文件下方的任何"打包成功/通过"表述都是首版的历史记录，不代表当前候选。**
> **失败产物按历史证据保留，不删除、不覆盖。**

> 2026-11-16 18:42，DSH。**仅主机打包。
> 未碰设备、未重建内核、未改 WIP、未打 payload/boot 到设备。**

**🔴 打包完成不等于硬件 PASS。等 Codex 审阅并明确指定镜像后才可刷。
不把刷写写成连接探针。**

---

## 0. 产物

| 项 | 值 |
|---|---|
| **boot.img** | **22994944 B** |
| **SHA256** | **201688e568e9737a58d4c1873892f82ff13fdbb723a15fe09febbb21b35b60b6** |
| **MD5** | **83222c148639827e9037ef6e029cf53b** |

**路径**：`/home/lizi/IOS/hakios-mtk/logs/profile4-device-20261116/trial710/boot.img`

---

## 1. 固定输入（全部核验通过）

| 输入 | 期望 | 实测 |
|---|---|---|
| profile4 内核 | 15321160 B / `cac498da…` | **一致** ✅ |
| stock boot 包装 | 67108864 B / `c5f8e7bd…` / md5 `a63b2e35…` | **一致** ✅ |
| 参考 payload | `5ac894d6…` | **一致** ✅ |
| chainboot.py | `c47ea472…` | **一致** ✅ |
| loader.c | `60647f89…` | **一致** ✅ |
| 8 项生产输入 | — | **不一致 0** ✅ |

**5205 文件清单**：基底 5205 / 当前 5205，**新增 0、消失 0**，
**内容差异恰好 2 项**（`bsd/dev/hakios.mt6895_headset.c` 与 `bsd/sys/hakios.pmif.h`），
与该内核的审计输入一致。

**参考 payload 结构**：

```
XNUP magic=0x50554e58  body_len=19515328
  kernel      15320872  c17cf881…
  ramdisk      4194304  78985f67583b4d46…（保留）
  kernel.ver        48  363b5257…（保留）
```

---

## 2. trim 值推导（来自实测 258e/258f）

| byte | 值 | coarse = (b>>3)&31 | fine = b&7 |
|---|---|---|---|
| **258e** | `0x18` = 24 | **3** | **0** |
| **258f** | `0x8b` = 139 | **17** | **3** |

**→ `hp_trim_l=3 hp_trim_r=17 hp_fine_l=0 hp_fine_r=3`**

**与 CMD 声明一致。未使用模型哨兵 3/7 或 1/2。**

---

## 3. loader 构建

### 3.1 冻结快照（保持原相对布局）

```
loader-src/
├── mtk/loader/            原 mtk/loader
├── mtk/mt6895/loader/     board.h
└── xnuloader/             identity.h
```

**37 个文件。`mtkloader.c` 的 `../../xnuloader/identity.h` 可解析到快照内。**

### 3.2 依赖核查

**全部项目内 include 落在 SNAPSHOT 内，指向快照外 = 0。**
（`stdint.h`/`stddef.h` 是编译器自带）

### 3.3 编译（Python subprocess argv，无管道）

| 目标 | native rc | 字节 |
|---|---|---|
| `start.o` | **0** | **8648** |
| `mtkloader.o` | **0** | **91512**（40 行既有警告） |
| **链接 → loader.bin** | **0** | **48872** |

**`loader.bin` SHA256**：`daa05b1736fafcb24499a8b063b20d9d94ee2403f95e46014ae02b2676cad18d`

**`BOARD_HEADER` 参数值含字面双引号**（已在 argv 中验证）。

**完整 argv/stdout/stderr/rc 在 `compile-commands.json`。**

---

## 4. 参数串核验（关键）

**loader 中内嵌的 512 字节参数串（offset `0x9fb0`）**：

```
rd=md0 -v cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=5 mali_cold=1 mali_preas0=0 mali_low=0 mali_trace=1 mali_witness=0 mali_ptvendor=1 mali_scclk=1 mali_scupd=1 mali_sc26=0 mali_cacheflush=0 mali_resetonly=0 mali_cohnone=0 mali_hwissues=0 mali_stackpwron=0 mali_mcuwin=0 mali_mcuseq=0 mali_doorbell=0 mali_pdcsettle=0 mali_statussem=0 mali_assorder=1 mali_trial=710 mali_nopdc=1 headset=4 hp_trim_l=3 hp_trim_r=17 hp_fine_l=0 hp_fine_r=3 loader_return_after=150
```

| 检查 | 结果 |
|---|---|
| 长度 | **512 字节**（不含 NUL） |
| 含 NUL | **513** |
| **< BOOT_LINE_LENGTH(608)** | ✅ **余量 95 字节** |
| 关键项（`mali_trial=710` 等 8 项） | **全部 YES** |
| **禁止项**（`mali_trial=709`/`headset=3`/`headset=1`/`mali_trial=695`/gpueb） | **全部 NO** ✅ |

### `hakios_kver` 追加后的总长

**源码 `mtkloader.c:3607` 是独立常量 `" hakios_kver="`（13 字节），
第 3613 行有边界检查 `n + sizeof(prefix)-1 + len < BOOT_LINE_LENGTH`。**

**核算**：`512 + 13 + 47`（`kernel.ver` 截断后长度）**= 572**，
**含终止 NUL 为 573 < 608**，**余量 35 字节**。

---

## 5. 新 payload

**只替换 kernel，ramdisk 与 kernel.ver 原样保留。**

| 条目 | 大小 | SHA256 |
|---|---|---|
| `kernel` | **15321160** | **`cac498da96406cc4f660e2cc717dbcf93b259ae1d864de6f30a9e920ed03401d`** |
| `ramdisk` | **4194304** | `78985f67583b4d4666e4612681b879720259d83b309530e2b82c1246d5ed5bce`（**与参考相同**） |
| `kernel.ver` | **48** | `363b5257dc819017ddb1f97c15be0d3c542f023ef8052c3c4801eb4463dd06f6`（**与参考相同**） |

**payload.bin**：**19515600 B**，SHA256 `9738815e6f4dfd2c95d88d978195336da0b506a28609a41aff79d38137cdda34`

---

## 6. 镜像合成与独立解包核验

**命令**（`compile-commands.json` 外另记）：

```
python3 mtk/tools/chainboot.py backup/boot_a_stock_real.img \
  <trial710>/loader.bin <trial710>/boot.img <trial710>/payload.bin \
  --partition-size 64M
```
**native rc = 0**

### 验收 1：Android 头

| 项 | 值 |
|---|---|
| magic | **`ANDROID!`** |
| kernel_size | 21112307 |
| 镜像总长 | **22994944 < 64 MiB**（余量 44113920 B） |
| 前 4096 字节与 stock 差异 | **仅 4 字节**（`0x8–0xb` = `kernel_size` 字段） |

**ramdisk / cmdline / signature 字节未被 chainboot 之外的字段修改。**

### 验收 2：解压链

| 结构 | 核验 |
|---|---|
| `[loader 0, 48872)` | **差异仅 10 字节**：`0x12`、`0x28-0x2b`、`0x30-0x33`，**正是 chainboot 文档的 arm64 header 补丁** |
| `[48872, 262144)` 填充区 | **全零** ✅ |
| `stock Image @ 262144` | **与 stock 解压 Image 逐字节相同** ✅ `12d255cd…` |
| `payload @ 42463520` | **与新 payload.bin 逐字节相同** ✅ |

**header 补丁值**：
- `blob[0x10]` image_size = `0x3c00000`
- `blob[0x28]` res2 = **42201364**（stock Image 长度）
- `blob[0x30]` res3 = **`0x287f120`**（payload 偏移）

### 验收 3：嵌套 XNUP

| 条目 | 大小 | SHA256 |
|---|---|---|
| `kernel` | 15321160 | **`cac498da…`** ✅ |
| `ramdisk` | 4194304 | `78985f67…`（与参考一致）✅ |
| `kernel.ver` | 48 | `363b5257…`（与参考一致）✅ |

### 验收 4：无合成 trim、无重复参数、内核精确

**见 §4 的禁止项检查，全部 NO。**

### 验收 5：既有产物未变

| 项 | 值 |
|---|---|
| 共享 `hakios-kernel` | `67f9f933117f9bc2…` ✅ |
| 709 内核 | `c17cf881745511a3…` ✅ |
| 709 镜像 md5 | `2e6b93bf83f7f604…` ✅ |
| 709 payload | `5ac894d6b6debbd6…` ✅ |
| profile4 内核 | `cac498da96406cc4…` ✅ |
| wrapper | `95247bb011c136d2…` ✅ |
| 生产仓 WIP | `9cdcb0da…` 未动 ✅ |

**`build-payload.bin`（19480504 B）mtime = 2026-11-14 02:06，早于本轮，未被触碰。**

**709 历史源链判定仍为 `INSUFFICIENT`。**

---

## 7. 边界

**本轮没有**：adb/fastboot/USB 操作、PMIC 访问、稳压器实验、
从混杂脏树构建、生产仓 push、stash/reset/checkout/clean/gc/prune、
force push、重写旧证据。

**未把刷写写成连接探针。**

**打包不建立**：hardware PASS、电气静音、时钟振荡、
DC offset 质量或出声。

---

## 8. 文件清单

| 文件 | 字节 | 说明 |
|---|---|---|
| **`boot.img`** | **22994944** | **待刷候选（等 Codex 批准）** |
| `payload.bin` | 19515600 | 新 XNUP |
| `loader.bin` | 48872 | 新 loader |
| `start.o` / `mtkloader.o` | 8648 / 91512 | 目标文件 |
| `compile-commands.json` | — | 完整 argv/stdout/stderr/rc |
| `inputs-verify.txt` | — | 固定输入核验 |
| `payload-and-inputs.txt` | — | payload 结构与 8 项输入 |
| `trim-and-manifest.txt` | — | trim 推导与 5205 清单 |
| `loader-args-verify.txt` | — | 参数串与长度核算 |
| `verify-1-header.txt` | — | Android 头核验 |
| `verify-2-chain.txt` | — | 解链核验 |
| `actual-args.txt` | — | 完整参数串原文 |
| `loader-src/` | 37 文件 | 冻结 loader 源快照 |
