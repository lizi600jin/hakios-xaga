# 源码身份：8 份封存源码的机械核验

> 2026-11-16 15:47，DSH。**只核 SHA256 与 Git blob ID，不做语义判断。**

---

## 1. 核验方法

**三重独立校验：**

1. **SHA256**：对文件内容直接算，与 `source-manifest.json` 的记录比对
2. **Git blob ID**：本地算 `sha1("blob <len>\0" + content)`，与 manifest 的 `blob` 字段比对
3. **真实仓库交叉验证**：在 `/home/lizi/IOS/work-xaga/k-xaga` 里跑
   `git rev-parse 0a8d13c1a180fed82803f56337a0b065ff704fb4:<path>`

**三重全部一致才算通过。**

---

## 2. 八份全部通过

| # | 上游路径 | 字节 | SHA256 | Git blob |
|---|---|---|---|---|
| 1 | `drivers/base/regmap/regmap-debugfs.c` | **17087** | `4fe6a9900c47819d4ebe42270994b9a48a9f39b2fb06003d9cbdc38a8583e00d` | `ed54dc31e6fd44e8405e60ecbafc47ad8fbf57da` |
| 2 | `drivers/base/regmap/regmap.c` | **82148** | `f41ff8ab333303c20d650c8eba997d1accdb0bd67365cc3803942ba7589e30d2` | `33279af071d3f0790726d9ff6c1ea5edeb2264b4` |
| 3 | `drivers/base/regmap/internal.h` | **8442** | `4f3153445578d0ee4006f81207eea823c52e30cfddb0bf4d345eda764d5f9f5f` | `0097696c31de2d81cb2c578c0a5aca3a035d3729` |
| 4 | `include/linux/regmap.h` | **60734** | `ad811a96011145ba01178f34c1db7c256535572fd0c67ecff1533f6f7c077d94` | `751ca386930786b371dd8ab397c13d31e5a28119` |
| 5 | `drivers/mfd/Makefile` | **10548** | `6253f76f9095364fda45bb44dfec028b993444d5cc5c685292d722b209756ff1` | `40c61dcb09d5870fa42543a86979b7cd4e34d0a4` |
| 6 | **`drivers/mfd/mtk-spmi-pmic.c`** | **18894** | **`2c4efc39c095746220084aa41850b8a7a6751d6c353fe85be380359327b9e535`** | **`631535382063e5b0fa1c196c76de63c2e972d2b8`** |
| 7 | **`drivers/base/regmap/regcache.c`** | **17852** | **`3ead65ef61726f5333d596e17e51311b7cadcb29f157996c96171be548074d5e`** | **`7fdd702e564ae504d9c7043318041b769aca9768`** |
| 8 | **`drivers/base/regmap/regmap-spmi.c`** | **5074** | **`cdf12d2aa3a1c48f02bc3282a74c89fec2ad41d7`** ← 注意：这是 blob，SHA256 见下 | **`cdf12d2aa3a1c48f02bc3282a74c89fec2ad41d7`** |

**第 8 份的完整 SHA256**：`2cf1415525b090a1…`（实测）

### 关键交叉验证结果

```
仓库 /home/lizi/IOS/work-xaga/k-xaga
当前 HEAD = 0a8d13c1a180fed82803f56337a0b065ff704fb4   ← 正是那个固定提交
git rev-parse <commit>:<path> 对 8 份逐个返回的 blob：
  ed54dc31e6fd44e8405e60ecbafc47ad8fbf57da  ✓
  33279af071d3f0790726d9ff6c1ea5edeb2264b4  ✓
  0097696c31de2d81cb2c578c0a5aca3a035d3729  ✓
  751ca386930786b371dd8ab397c13d31e5a28119  ✓
  40c61dcb09d5870fa42543a86979b7cd4e34d0a4  ✓
  631535382063e5b0fa1c196c76de63c2e972d2b8  ✓
  7fdd702e564ae504d9c7043318041b769aca9768  ✓
  cdf12d2aa3a1c48f02bc3282a74c89fec2ad41d7  ✓
```

**8/8 与真实 git 仓库一致。** 仓库 HEAD 就是这个 commit，所以**不是"猜的提交"。**

---

## 3. 🔴 两类输入分开记录

### 3.1 「5/6 指定文件存在」—— 我上轮采集的结果

**这 5 份是我上轮从 raw.githubusercontent 下载的，与封存件逐字节相同：**

| 文件 | `cmp` 结果 |
|---|---|
| `regmap-debugfs.c` | **IDENTICAL** |
| `regmap.c` | **IDENTICAL** |
| `internal.h` | **IDENTICAL** |
| `regmap.h` | **IDENTICAL** |
| `Makefile` | **IDENTICAL** |

**所以「5/6」这个记录仍然成立**（第 6 项 `mt6368-core.c` 是 404）。

### 3.2 「通用 MFD 替代输入」—— 本轮新增的 3 份

| 文件 | 字节 | 说明 |
|---|---|---|
| **`drivers/mfd/mtk-spmi-pmic.c`** | **18894** | **MT6368 的真实 MFD 入口** |
| `drivers/base/regmap/regcache.c` | 17852 | regmap 缓存实现 |
| `drivers/base/regmap/regmap-spmi.c` | 5074 | regmap SPMI 后端 |

**这 3 份是"替代输入"，不是"补齐了原来的 6 项"。两者必须分开看。**

---

## 4. 🔴 原错误文件名 / 404 记录保留，不抹掉

**Codex 纠正：`drivers/mfd/Makefile` 第 209 行是
`CONFIG_MFD_MTK_SPMI_PMIC += mtk-spmi-pmic.o`，
MT6368 的入口实际为 `drivers/mfd/mtk-spmi-pmic.c`。**

**但以下记录一律保留，不追认成功：**

| 记录 | 状态 |
|---|---|
| `drivers/mfd/mt6368-core.c` → **HTTP 404** | **保留** |
| 该 Makefile 里 `mt6368` 零命中（`mt6685` 有 2 处） | **保留**（这解释了为什么按 Makefile 找不到 `mt6368-core.c`） |
| 三个猜测路径全 404 | **保留** |
| 上轮结论「覆盖率 5/6」 | **保留** |

**新增的 `mtk-spmi-pmic.c` 是"换个正确的文件名"，不是"原来那个 404 文件找到了"。**

**这个区别很重要：前者说明我们的路径假设错了，后者才说明文件缺失。
实际是前者。**

---

## 5. 封存件位置

| 项 | 路径 |
|---|---|
| 源码目录 | `/home/lizi/IOS/hakios-mtk/logs/regmap-trim-review-20261116/sources/` |
| 身份清单 | `/home/lizi/IOS/hakios-mtk/logs/regmap-trim-review-20261116/source-manifest.json` |
| 完整仓库 | `/home/lizi/IOS/work-xaga/k-xaga`（HEAD = 固定提交） |
