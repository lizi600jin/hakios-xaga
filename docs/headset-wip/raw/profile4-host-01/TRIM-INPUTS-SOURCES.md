# HOST-TRIM-INPUTS-01 源码采集
# date: 2026-11-16T15:36:59+08:00
# repo XagaForge/android_kernel_xiaomi_mt6895
# commit 0a8d13c1a180fed82803f56337a0b065ff704fb4

文件                                                     HTTP       字节  SHA256
---------------------------------------------------------- ------ ----------  ----------------------------------------------------------------
drivers__base__regmap__regmap-debugfs.c                    200         17087  4fe6a9900c47819d4ebe42270994b9a48a9f39b2fb06003d9cbdc38a8583e00d
drivers__base__regmap__regmap.c                            200         82148  f41ff8ab333303c20d650c8eba997d1accdb0bd67365cc3803942ba7589e30d2
drivers__base__regmap__internal.h                          200          8442  4f3153445578d0ee4006f81207eea823c52e30cfddb0bf4d345eda764d5f9f5f
include__linux__regmap.h                                   200         60734  ad811a96011145ba01178f34c1db7c256535572fd0c67ecff1533f6f7c077d94
drivers__mfd__Makefile                                     200         10548  6253f76f9095364fda45bb44dfec028b993444d5cc5c685292d722b209756ff1

---

## 🔴 请求的第 6 项：drivers/mfd/mt6368-core.c —— HTTP 404，缺失

| 项 | 值 |
|---|---|
| URL | `https://raw.githubusercontent.com/XagaForge/android_kernel_xiaomi_mt6895/0a8d13c1a180fed82803f56337a0b065ff704fb4/drivers/mfd/mt6368-core.c` |
| HTTP | **404** |
| 响应体 | `404: Not Found`（14 字节，**已丢弃，未当源码**） |

### 按指令机械查 `drivers/mfd/Makefile` 的 mt6368 对象名

**结果：`mt6368` 零命中。**

```
mt6368   0
mt6363   0
mt6685   2      ← 有 mt6685，但没有 mt6368
mt6359   0
spmi     3
```

**Makefile 共 281 行，是标准内核 `drivers/mfd/Makefile`，其中 PMIC 条目只含 `mt6685`。**

### 所以无法按指令的「查 Makefile 找对应 .c」路径补上

**该 Makefile 不含 `mt6368` 对象，没有可推导的同目录 `.c` 文件名。**

### 仅探测（不计入有效输入，全部 404，未下载内容）

| 猜测路径 | HTTP |
|---|---|
| `drivers/mfd/mt6368.c` | **404** |
| `drivers/mfd/mt6368-mfd.c` | **404** |
| `drivers/misc/mediatek/pmic/mt6368/mt6368-core.c` | **404** |

### 🔴 按指令：记录缺失，不自行更换 commit

**没有尝试其他 commit，没有拿错误页当源码，没有覆盖任何已有文件。**

**结论：`mt6368-core.c` 在本固定提交的 `drivers/mfd/` 下不存在。
MT6368 的 MFD 绑定可能在别处（本轮不推测位置）。**

---

## 有效输入清单：5 项

| # | 文件 | HTTP | 字节 | SHA256 |
|---|---|---|---|---|
| 1 | `drivers__base__regmap__regmap-debugfs.c` | 200 | 17087 | `4fe6a9900c47819d4ebe42270994b9a48a9f39b2fb06003d9cbdc38a8583e00d` |
| 2 | `drivers__base__regmap__regmap.c` | 200 | 82148 | `f41ff8ab333303c20d650c8eba997d1accdb0bd67365cc3803942ba7589e30d2` |
| 3 | `drivers__base__regmap__internal.h` | 200 | 8442 | `4f3153445578d0ee4006f81207eea823c52e30cfddb0bf4d345eda764d5f9f5f` |
| 4 | `include__linux__regmap.h` | 200 | 60734 | `ad811a96011145ba01178f34c1db7c256535572fd0c67ecff1533f6f7c077d94` |
| 5 | `drivers__mfd__Makefile` | 200 | 10548 | `6253f76f9095364fda45bb44dfec028b993444d5cc5c685292d722b209756ff1` |

**命名规则**：完整相对路径用双下划线编码，**不是 basename** —— 所以不会撞名。

**下载流程**：先落 `/tmp/hti-tmp.bin` → 验 HTTP 200 + 大小 >100 + 首行含 SPDX/Makefile → 才 `mv` 进有效清单。

**首行验真（五份全部通过）**：

```
drivers__base__regmap__regmap-debugfs.c    // SPDX-License-Identifier: GPL-2.0
drivers__base__regmap__regmap.c            // SPDX-License-Identifier: GPL-2.0
drivers__base__regmap__internal.h          /* SPDX-License-Identifier: GPL-2.0 */
include__linux__regmap.h                   /* SPDX-License-Identifier: GPL-2.0-only */
drivers__mfd__Makefile                     # SPDX-License-Identifier: GPL-2.0
```

**覆盖率：5/6。第 6 项明确缺失，不把下载失败当完整覆盖。**
