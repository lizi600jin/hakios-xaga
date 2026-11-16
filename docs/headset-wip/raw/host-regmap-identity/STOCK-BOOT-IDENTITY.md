# 原厂 boot_a 定位：身份充分

> 2026-11-16 15:49，DSH。**只读取与计算 hash，未修改、未重打包、未刷写。**

---

## 1. 主身份（完全验证）

| 项 | 值 |
|---|---|
| **绝对路径** | **`/home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img`** |
| **大小** | **67108864 字节**（64 MiB） |
| **MD5（实测）** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** |
| **历史记录 MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** → **完全一致** ✅ |
| **SHA256（实测）** | **`c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc`** |
| 文件时间 | Oct 23 19:58 |

**这不是"只看文件名"—— MD5 与历史设备记录逐字符一致，SHA256 是本次新算的完整值。**

---

## 2. 另外两个副本（同一镜像）

| 路径 | 大小 | MD5 | 与主身份 |
|---|---|---|---|
| `/home/lizi/IOS/work-xaga/dump/boot_a.img` | 67108864 | **`a63b2e35a7c2522cb24a6a10332b9bc8`** | **相同** |
| `/home/lizi/IOS/hakios-mtk/backup/boot_a_magisk.img` | 67108864 | **`a63b2e35a7c2522cb24a6a10332b9bc8`** | **相同** |

**三个独立副本的 MD5 都是 `a63b2e35…`，互相印证。**

**注意：`boot_a_magisk.img` 的文件名带 "magisk"，但 MD5 与原厂完全一致 ——
说明它是打 Magisk 之前的备份，内容就是原厂。文件名不可信，hash 可信。**

---

## 3. 已有的来源记录位置

| 记录 | 位置 |
|---|---|
| 恢复流程里引用的 md5 | 本仓 `docs/headset-wip/` 下的多份 handoff / findings 文档（如 `TRIAL-709-MT6685-SID9-READ.md` §6「恢复记录」写明 `boot_a = a63b2e35…`） |
| 备份动作的原始记录 | `hakios-mtk/backup/` 目录 |
| vendor_boot 对应记录 | `hakios-mtk/logs/headset-dapm-review-20261115/vendor-boot-module-manifest.json`（`fed80580…`） |

---

## 4. 🔴 已有原厂内核分析记录：缺失

**按指令列出已有的原厂 Linux version / 内核 build-id / 配置 / BTF / vmlinux 记录。**

| 项 | 状态 |
|---|---|
| **Linux version 字符串** | **⚠️ 仅从模块 vermagic 间接得知**：`5.10.198-android12-9-g5e8653a7cee7` |
| **内核 build-id** | **🔴 缺失** —— 没有 `vmlinux` 或 `Image` 的 build-id 记录 |
| **内核配置文件（`.config` / `config-*`）** | **🔴 缺失** |
| **BTF（`.BTF` 段 / `vmlinux.btf`）** | **🔴 缺失** |
| **`vmlinux`（未剥离符号）** | **🔴 缺失** |
| **`System.map`** | **🔴 缺失** |

### 搜索范围

```
find /home/lizi/IOS -maxdepth 4 \( -name "*vmlinux*" -o -name "*.config" \
     -o -name "config-*" -o -name "*.btf" -o -name "System.map*" \) -type f
  → 无结果
```

**`/home/lizi/IOS/work-xaga/dump/` 里只有分区镜像与设备树**
（`boot_a.img`、`vendor_boot_a.img`、`dtbo_a.img`、`lk_a.img`、`lk.fdt`、
`xaga_live.dts`、`byname.txt`、`cmdline.txt`、`iomem.txt`），**没有内核分析产物**。

### 唯一可用的间接记录

**模块 vermagic**（本轮新提取的两个模块，以及之前三个 mt6685 模块）：

```
vermagic=5.10.198-android12-9-g5e8653a7cee7 SMP preempt mod_unload modversions aarch64
```

**这给出内核版本与 git 短 hash `g5e8653a7cee7`，
但`vermagic` 不等于内核 build-id，也不能拿来当内核身份的完整证明。**

---

## 5. 按指令：没有为补证据运行 adb

**上述缺失项全部如实报缺。
没有运行 adb（本任务明确禁止，含只读），没有为了取 `/proc/version`、
`/sys/kernel/notes` 或 `config.gz` 去碰设备。**

**要补这些记录，需要在设备旁做一次只读取证（另一个任务）。**
