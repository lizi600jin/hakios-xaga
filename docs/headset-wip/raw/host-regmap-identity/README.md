# HOST-REGMAP-IDENTITY-01：源码身份、模块身份、原厂 boot 定位

> 2026-11-16 15:50，DSH。**全程主机。
> 未 adb/fastboot、未读写设备、未挂 debugfs、未播放、未重启、未刷写。
> 未跑 vendor dump / Audio HP ImpeDance getter / spmi_access。
> 未构建、未打包镜像、未改生产代码或 wrapper。**

---

## 0. 三句话

**一、8 份封存源码的 SHA256 与 Git blob ID 全部一致，并用真实 git 仓库（HEAD 就是这个提交）
交叉验证 8/8。**

**二、两个模块从 vendor_boot 缓存成功提取，来源链完整
（镜像 `fed80580` → archive member → 文件 SHA256）；但与设备运行实例的一致性 🔴 未验证。**

**三、原厂 boot_a 身份充分（MD5 `a63b2e35…` 三个副本互证）；
但内核层分析记录（build-id / config / BTF / vmlinux / System.map）🔴 全部缺失。**

---

## 1. 三份详细文档

| 文档 | 内容 |
|---|---|
| **`SOURCE-IDENTITY.md`** | 8 份源码的三重核验、「5/6 指定文件」与「3 份替代输入」分开记录、原 404 不抹掉 |
| **`MODULE-IDENTITY.md`** | vendor_boot 来源链、两个模块的 size/hash/build-id/vermagic、运行一致性未验证 |
| **`STOCK-BOOT-IDENTITY.md`** | 原厂 boot 的绝对路径/size/MD5/SHA256、三个副本、内核记录缺失清单 |

---

## 2. 判据核对

| 判据 | 结果 |
|---|---|
| 5 份已下载源码与固定 Git blob 逐字节相同 | ✅ **`cmp` 全 IDENTICAL**，且 blob ID 与真实仓库一致 |
| 3 份新增输入身份明确 | ✅ `mtk-spmi-pmic.c` / `regcache.c` / `regmap-spmi.c`，SHA256 + blob 双记录 |
| **原错误文件名 / 404 不被抹掉** | ✅ **`mt6368-core.c` 的 404 与「Makefile 里 mt6368 零命中」全部保留** |
| 两个模块有来源镜像→member→文件 hash 链 | ✅ **镜像 `fed80580` → `lib/modules/*.ko` → SHA256** |
| **无法证明与运行模块一致的部分明确标未验证** | ✅ **`MODULE-IDENTITY.md` §4 明确写了，并说明为什么（禁止设备动作）** |
| stock boot 按完整实测 hash 判断 | ✅ **MD5 `a63b2e35…` + SHA256 `c5f8e7bd…`**，三个副本互证 |
| 未找到则如实记录 | ✅ **内核层记录全部报缺** |
| 不改变 8 项 mode4 生产输入 | ✅ 见 §4 实测 |
| 不改变 `cac498da…` host kernel | ✅ 见 §4 实测 |
| 不改变任何既有产物 | ✅ 见 §4 实测 |

---

## 3. 所有有效输入的主机绝对路径

### 3.1 源码（8 份，封存）

```
/home/lizi/IOS/hakios-mtk/logs/regmap-trim-review-20261116/sources/
  drivers__base__regmap__regmap-debugfs.c     17087
  drivers__base__regmap__regmap.c             82148
  drivers__base__regmap__internal.h            8442
  include__linux__regmap.h                    60734
  drivers__mfd__Makefile                      10548
  drivers__mfd__mtk-spmi-pmic.c               18894   ← 新增
  drivers__base__regmap__regcache.c           17852   ← 新增
  drivers__base__regmap__regmap-spmi.c         5074   ← 新增
```
**身份清单**：`.../regmap-trim-review-20261116/source-manifest.json`
**完整仓库**：`/home/lizi/IOS/work-xaga/k-xaga`（HEAD = `0a8d13c1…`）

### 3.2 我上轮交的 5 份（与封存件 `cmp` IDENTICAL）

```
/home/lizi/IOS/hakios-mtk/logs/headset-trim-inputs-20261116-153613/
```

### 3.3 两个模块（本轮提取）

```
/home/lizi/IOS/hakios-mtk/logs/host-regmap-identity-20261116-154659/
  vendor_ramdisk.lz4                      42741855
  vendor_ramdisk.cpio                     88036352
  extracted/lib/modules/mtk-spmi-pmic.ko     42160  1f2d95c5e11d6a197d90cb7dc60d047ff700275cad7e9738f63684adc00bee7f
  extracted/lib/modules/regmap-spmi.ko       17312  0ac2b7bc746632b86ab474264f1be3b04e79564271d5e8609a9fea3a3311ce84
```

### 3.4 源镜像（两个副本，字节相同）

```
/home/lizi/IOS/hakios-mtk/backup/vendor_boot_a_stock.img     67108864  fed80580d619b916d73b51b704f2af6c9f08afafc84810ced0b2397a0b52797f
/home/lizi/IOS/work-xaga/dump/vendor_boot_a.img              67108864  同上
```

### 3.5 原厂 boot_a（三个副本，MD5 相同）

```
/home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img       67108864  md5 a63b2e35a7c2522cb24a6a10332b9bc8
/home/lizi/IOS/work-xaga/dump/boot_a.img                     67108864  同上
/home/lizi/IOS/hakios-mtk/backup/boot_a_magisk.img           67108864  同上
```
**SHA256（主身份）**：`c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc`

---

## 4. 既有产物未变（实测）

| 项 | 值 |
|---|---|
| **8 项 mode4 生产输入** | **不一致 0 项** |
| **host kernel** | **`cac498da96406cc4f660e2cc717dbcf93b259ae1d864de6f30a9e920ed03401d`** ✅ |
| 709 kernel | `c17cf881745511a3…` ✅ |
| `hakios-kernel` | `67f9f933117f9bc2…` ✅ |
| wrapper | `95247bb011c136d2…` ✅ |
| 709 镜像 | `f4ff9732081e44a3…` ✅ |

---

## 5. 缺失项（保留，不填补）

| 缺失 | 说明 |
|---|---|
| **`drivers/mfd/mt6368-core.c`** | **HTTP 404，原记录保留**；由 `mtk-spmi-pmic.c` 作替代输入（**不追认原请求成功**） |
| **两个模块与运行实例的一致性** | **未验证** —— 需要设备侧 build-id 比对，本任务禁止设备动作 |
| **内核 build-id** | 缺失 |
| **内核配置 `.config`** | 缺失 |
| **BTF** | 缺失 |
| **`vmlinux`** | 缺失 |
| **`System.map`** | 缺失 |

**没有为补证据重新拉取设备分区、下载未知固件或运行 adb。**

---

## 6. 边界：语义由 Codex 分析

**我只做：SHA256 计算、Git blob ID 计算、`git rev-parse` 交叉验证、
vendor_boot 头解析、LZ4 解压、cpio 抽取、`readelf -n`（build-id）、
`strings | grep vermagic`、MD5/SHA256 计算、文件定位。**

**我没有做：反汇编、regmap 缓存行为分析、读取副作用判断、错误处理分析、
寄存器语义解释。这些全部归 Codex。**
