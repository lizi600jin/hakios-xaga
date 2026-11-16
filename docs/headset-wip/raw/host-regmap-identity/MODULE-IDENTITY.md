# 模块身份：从 vendor_boot 缓存的来源链

> 2026-11-16 15:48，DSH。**仅复用既有主机缓存，未重新获取设备分区、未碰设备。**

---

## 1. 源镜像（两个副本，字节级相同）

| 副本路径 | 字节 | SHA256 |
|---|---|---|
| `/home/lizi/IOS/hakios-mtk/backup/vendor_boot_a_stock.img` | **67108864** | `fed80580d619b916d73b51b704f2af6c9f08afafc84810ced0b2397a0b52797f` |
| `/home/lizi/IOS/work-xaga/dump/vendor_boot_a.img` | **67108864** | **同上** |

**`cmp` 验证两副本 `IDENTICAL`。**

**与 `vendor-boot-module-manifest.json` 记录的 `fed80580…` 一致 —— 这就是那个原始 vendor_boot。**

---

## 2. 提取过程（全新目录，未覆盖历史原件）

目录：**`/home/lizi/IOS/hakios-mtk/logs/host-regmap-identity-20261116-154659/`**

| 步骤 | 结果 |
|---|---|
| vendor_boot 头解析 | magic **`VNDRBOOT`**、header version **4**、page size **4096** |
| `vendor_ramdisk_size` | **42741855** |
| ramdisk 提取 | `vendor_ramdisk.lz4`，42741855 B，首 4 字节 `02214c18` = **LZ4 legacy** |
| `lz4 -d -l` 解压 | `vendor_ramdisk.cpio`，**88036352 B**，rc=0 |
| cpio 成员名 | `lib/modules/mtk-spmi-pmic.ko`、`lib/modules/regmap-spmi.ko`（**连字符命名，与指令一致**） |

**archive 里相关成员完整列表：**

```
lib/modules/mtk-spmi-pmic-adc.ko
lib/modules/mtk-spmi-pmic.ko        ← 目标 1
lib/modules/regmap-spmi.ko          ← 目标 2
lib/modules/spmi-mtk-mpu.ko
lib/modules/spmi-mtk-pmif.ko
```

---

## 3. 两个模块的身份

**位置**：`.../host-regmap-identity-20261116-154659/extracted/lib/modules/`

| 模块 | archive member 路径 | 字节 | SHA256 |
|---|---|---|---|
| **`mtk-spmi-pmic.ko`** | `lib/modules/mtk-spmi-pmic.ko` | **42160** | **`1f2d95c5e11d6a197d90cb7dc60d047ff700275cad7e9738f63684adc00bee7f`** |
| **`regmap-spmi.ko`** | `lib/modules/regmap-spmi.ko` | **17312** | **`0ac2b7bc746632b86ab474264f1be3b04e79564271d5e8609a9fea3a3311ce84`** |

### ELF build-id（机械提取）

| 模块 | Build ID |
|---|---|
| `mtk-spmi-pmic.ko` | **`9119ad181d63b4f694a561e4a2d367647ae78371`** |
| `regmap-spmi.ko` | **`031912cdcf9827082872fc9f52f1921f380e621c`** |

### vermagic（两者相同）

```
vermagic=5.10.198-android12-9-g5e8653a7cee7 SMP preempt mod_unload modversions aarch64
```

---

## 4. 🔴 与运行模块的一致性：**未验证**

| 项 | 状态 |
|---|---|
| 来源镜像 → archive member → 文件 hash 链 | ✅ **完整**（镜像 `fed80580` → member `lib/modules/...` → 文件 SHA256） |
| **与设备上运行实例一致** | **🔴 未验证** |

**为什么未验证：**

- **本任务禁止任何设备动作**（含只读 adb），所以无法取设备侧 `/sys/module/*/notes/.note.gnu.build-id` 来比对
- **镜像 `fed80580` 与 `vendor-boot-module-manifest.json` 记录的原始 vendor_boot 一致**，
  这是「来源可信」的依据，**但不是「当前运行实例一致」的证明**

**对比参考**：`mt6685-audclk.ko` 那次是**有** build-id 比对的
（`64facb6c…` 与运行模块匹配）。**本次两个模块没有做这个比对。**

**要补这个缺口，需要在设备旁取一次 `/sys/module/{mtk_spmi_pmic,regmap_spmi}/notes/.note.gnu.build-id`。**
**本任务不做。**

---

## 5. 不反汇编、不下语义结论

**只做了 `readelf -n`（build-id）与 `strings | grep vermagic`。
没有反汇编、没有寄存器语义提取、没有错误处理或缓存行为判断 —— 那些归 Codex。**
