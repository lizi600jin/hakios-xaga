# PACK-710 revision2：补齐 XNUP 终止项后的重新打包

> 2026-11-16 18:52，DSH。**仅主机。未碰设备、未重建内核、未覆盖旧失败产物。**

**旧 `trial710/` 的 payload/boot.img 保留为失败证据，拒绝刷写。**
**本目录是修正版。**

---

## 0. OBJ 指出的缺陷（已确认并修正）

**参考 payload 末尾有 24 字节全零终止项**（实测 `pos=19515312`，占满到 19515336）。
**旧 build 漏了它** —— 末尾直接是 `kernel.ver` 内容。

**根因**：我手写 XNUP 时没实现 `body += b'\0' * 24`；
`xnuloader/mkpayload.py:55` 有这一行，我之前绕过了它。

**规则**（`mkpayload.py` 文档串）：`end : an entry whose name is empty`。

---

## 1. 产物

| 项 | 值 |
|---|---|
| **payload.bin** | **19515624 B** |
| **payload SHA256** | **`e29f55eba59aa2569d9d440d0f8fe3f71b0861a819e7e46242a5ad11b741eb7d`** |
| **boot.img** | **22994944 B** |
| **boot.img SHA256** | **`ca161c1bdb002540a2454e8222ef7306255e175108ead9fffe48d995aebe56b1`** |
| **boot.img MD5** | **`b016ef1708b46830e9f74787bb169815`** |

**路径**：`/home/lizi/IOS/hakios-mtk/logs/profile4-device-20261116/trial710/revision2/`

---

## 2. payload 用 mkpayload.py 生成

```
python3 xnuloader/mkpayload.py -o <rev2>/payload.bin --no-version \
  kernel=logs/profile4-kernel-src-20261116-1525/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837 \
  ramdisk=<rev2>/ref-ramdisk.bin \
  kernel.ver=<rev2>/ref-kernel.ver.bin
```
**rc=0**

**`--no-version` 阻止它自己跑 `tools/kernel-version.py` 生成第二个版本条目；
kernel.ver 由我显式给出（从参考 payload 提取的原始 48 字节）。**

### 四条目结构

| # | 名称 | 大小 | SHA256 | 与参考 |
|---|---|---|---|---|
| 0 | **kernel** | **15321160** | **`cac498da96406cc4f660e2cc717dbcf93b259ae1d864de6f30a9e920ed03401d`** | **CHANGED（预期）** |
| 1 | ramdisk | 4194304 | `78985f67583b4d4666e4612681b879720259d83b309530e2b82c1246d5ed5bce` | **SAME** ✅ |
| 2 | kernel.ver | 48 | `363b5257dc819017ddb1f97c15be0d3c542f023ef8052c3c4801eb4463dd06f6` | **SAME** ✅ |
| 3 | **\<终止\>** | 0 | — | **SAME** ✅ |

**终止项位置 `pos=19515600`，24 字节全零，解析完整（`pos==len`）。**

**非 kernel 条目全部与参考一致。**

### 一次中途纠错

**第一次我用 47 字节的 kernel.ver（去掉了尾 NUL）生成，结果该条目变 47 B、SHA 不同。
改用原始 48 字节后与参考完全一致。**
（`mkpayload` 对文件条目不额外补 NUL，所以必须给原始含 NUL 的字节）

---

## 3. loader 复用（未重建）

**从 `trial710/loader.bin` 复制，`cmp` IDENTICAL。**

| 项 | 值 |
|---|---|
| 大小 | **48872 B** |
| SHA256 | **`daa05b1736fafcb24499a8b063b20d9d94ee2403f95e46014ae02b2676cad18d`** |

**参数串未变**（512 字节 + NUL < 608），trim 仍是 `hp_trim_l=3 hp_trim_r=17 hp_fine_l=0 hp_fine_r=3`。

---

## 4. chainboot

```
python3 mtk/tools/chainboot.py backup/boot_a_stock_real.img \
  <rev2>/loader.bin <rev2>/boot.img <rev2>/payload.bin --partition-size 64M
```
**native rc = 0**

**stdout**：`22994944 bytes (program 48872, Image 42201364, payload 19515624, kernel gz 21112310)`

---

## 5. 严格解包核验（全过）

### 1) Android header

| 项 | 值 |
|---|---|
| magic | `ANDROID!` |
| kernel_size | 21112310 |
| 总长 | 22994944 < 64 MiB ✅ |
| **前 4096 与 stock 差异** | **仅 4 字节，位置 `[8,9,10,11]`** = `kernel_size` 字段 ✅ |

### 2) 解压链

| 项 | 结果 |
|---|---|
| blob 总长 | 61979144 |
| **loader 段差异** | **仅 10 字节**：`0x12`、`0x28–0x2b`、`0x30–0x33`（chainboot 文档的 arm64 header 补丁）✅ |
| 填充区 `[48872, 262144)` | **全零** ✅ |
| **stock Image @ 262144** | **逐字节相同** ✅ `12d255cd…` |
| **payload @ 42463520** | **逐字节相同** ✅ |

### 3) 嵌套 XNUP

| 项 | 值 |
|---|---|
| magic / body_len | `0x50554e58` / 19515616，`8+body == 总长` ✅ |
| kernel | 15321160 **`cac498da…`** ✅ |
| ramdisk | 4194304 `78985f67…` ✅ |
| kernel.ver | 48 `363b5257…` ✅ |
| **终止项** | **pos 19515600，24 B 全零** ✅ |
| 解析完整 | **True** ✅ |

### 4) loader header 补丁值

| 偏移 | 值 | 期望 |
|---|---|---|
| `0x10` image_size | `0x3c00000` | — |
| `0x28` res2 | **42201364** | stock Image 长度 ✅ |
| `0x30` res3 | **`0x287f120`** | payload 偏移 ✅ |

### 5) 与旧失败产物对比

| 项 | 旧（失败） | 新（revision2） |
|---|---|---|
| payload | 19515600（**无终止项**） | **19515624（有终止项）** |
| boot.img | `201688e5…` | **`ca161c1bdb002540a2454e8222ef7306255e175108ead9fffe48d995aebe56b1`** |

---

## 6. 旧产物保留为失败证据

**`trial710/boot.img`（`201688e5e568e9737a58d4c1873892f82ff13fdbb723a15fe09febbb21b35b60b6`）
与 `trial710/payload.bin`（`9738815e…`，19515600 B）未被删除、未被覆盖。**

**两者均拒绝刷写。**

---

## 7. 既有产物未变

| 项 | 值 |
|---|---|
| 共享 `hakios-kernel` | `67f9f933117f9bc2…` ✅ |
| 709 内核 | `c17cf881745511a3…` ✅ |
| 709 镜像 | md5 `2e6b93bf…` ✅ |
| profile4 内核 | `cac498da…`（未重建）✅ |
| wrapper | `95247bb0…` ✅ |
| 生产仓 WIP | `9cdcb0da…` 未动 ✅ |

**`build-payload.bin`（19480504 B，mtime 2026-11-14）未被触碰。**

**709 源链判定仍 `INSUFFICIENT`。**

---

## 8. 边界

**未碰设备、未 adb/fastboot、未重建内核、未改生产源、未覆盖旧失败产物或生产文件。**

**打包不建立**：hardware PASS、电气静音、时钟振荡、DC offset 质量或出声。

**等 Codex 审阅并明确指定镜像后才可刷。**
