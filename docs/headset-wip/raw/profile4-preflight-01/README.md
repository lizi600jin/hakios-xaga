# PROFILE4-PREFLIGHT-01：刷机前只读前检

> 2026-11-16 18:35，DSH。**仅只读。
> 未重启、未刷写、未生成 boot args、未碰 spmi_access / codec dump / PMIC / GPUEB、未动 WIP。**
> 所有命令均限时。

---

## 0. 判据全部通过

| 判据 | 期望 | 实测 | 结果 |
|---|---|---|---|
| serial | `GAJZ4HIN99AQ69Y9` | **同** | ✅ |
| 连接 | `device` | **`device usb:1-1`** | ✅ |
| **Android 就绪** | `boot_completed=1` | **1** | ✅ |
| **slot** | `_a` | **`_a`** | ✅ |
| **boot_a MD5** | `a63b2e35a7c2522cb24a6a10332b9bc8` | **`a63b2e35a7c2522cb24a6a10332b9bc8`** | ✅ |

**boot_id**：`0ebf2d81-4822-47df-be3c-51916b1a7663`
**uptime**：69153.13 秒
**分区**：`/dev/block/by-name/boot_a` → `/dev/block/sdc43`，**67108864 字节**

**即：设备处于原厂 Android，slot_a，boot_a 未被改动。**

---

## 1. 打包工具链

### 1.1 709 打包命令生成器

| 项 | 值 |
|---|---|
| **路径** | **`/home/lizi/IOS/hakios-mtk/mtk/tools/chainboot.py`** |
| 大小 | **8458 B** |
| **SHA256** | **`c47ea4720eaec8d3dc02a1af2f258a20c960391954e0daefed7280f28556640f`** |

**这是 709 用的 chaining 脚本**：把 stock `boot.img` + `loader.bin` + `payload.bin` 合成可刷镜像。

### 1.2 loader 源（生成 `loader.bin`）

| 文件 | 字节 | SHA256 前 16 |
|---|---|---|
| `mtk/loader/start.S` | **16953** | `8d27dfa271a4c5f5` |
| `mtk/loader/mtkloader.c` | **263793** | `60647f896d027089` |
| `mtk/loader/link.ld` | **770** | `0936f28bac65f276` |

### 1.3 内核构建入口

| 工具 | 字节 | SHA256 |
|---|---|---|
| **`xnu-build.sh`** | **1593** | **`b5c1857b8a3c5a9ed81aa2cda145645947a62edc2020d512ed7325a47d7be9f6`** |
| `tools/make-kernel.sh` | 3547 | `95247bb011c136d22ca70b485a3388e46720464b6b40f3842bac2acdee80dbd3` |

**说明**：`make-kernel.sh` 的**正常构建分支**会 sync 默认树并向
`./hakios-kernel` 复制产物；**参考构建应直调 `xnu-build.sh`**，
`make-kernel.sh` 只用 `--judge` 分支（该分支不构建、不复制）。

---

## 2. 日志提取工具

| 工具 | 路径 | 字节 | SHA256 |
|---|---|---|---|
| **拉取** | **`mtk/tools/xaga-pull-log.sh`** | **1107** | **`48a337d83a0cc67b020fb93082834888fbc824142f2766837129ee0d9ca4239b`** |
| **解码** | **`mtk/tools/xaga-logbook.py`** | **2300** | **`ff784c3ef9f0b41c3bec7cfdb62ac21e65c2baaf70ed88c4ab9d0946e4440f9e`** |

- `xaga-pull-log.sh`：从 `xnusys` 分区把 logbook 拉出来
- `xaga-logbook.py`：**解码 HLOG 窗口，拒绝不完整或损坏的记录**

**另有**：`mtk/tools/test-logbook.py`（7370 B，`5e8202e3…`）——
对内存 UFS stub 测生产 loader 的 HLOG 写入器，**不是取证工具**。

**记忆中的手动提取参数（供参考，本轮未执行）**：
HLOG 在 `0x9f8f8000`，header（`magic@0` / `len@8` / `reason@16` / `sum@64`），
body 在 `+4096`，8-bit additive checksum。

---

## 3. 原厂恢复工具

| 项 | 值 |
|---|---|
| **恢复镜像** | **`/home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img`** |
| 大小 | **67108864 B** |
| **MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** ✅ |
| 与设备 boot_a MD5 | **完全一致（即当前设备上就是这份）** |

**另有副本**（同 MD5，可交叉验证）：

| 路径 | 字节 | MD5 |
|---|---|---|
| `backup/boot_a_magisk.img` | 67108864 | `a63b2e35…` |
| `/home/lizi/IOS/work-xaga/dump/boot_a.img` | 67108864 | `a63b2e35…` |

### 恢复步骤（本轮未执行，仅记录）

1. `adb reboot bootloader`（或用户手动回 fastboot）
2. **用 `fastboot flash` 验证通道**（`getvar` 静默不等于通道死）
3. `fastboot flash boot_a backup/boot_a_stock_real.img`
4. `fastboot reboot`
5. 等约 1 分钟，`adb shell getprop sys.boot_completed` 应为 `1`
6. **复查 `md5sum /dev/block/by-name/boot_a` 必须回到 `a63b2e35…`**

### 已知坑（历史记录）

- **每次刷机后 Android 常卡在 "starting Android"**，需用户手动回 fastboot 恢复
- `fastboot oem expdb` **会杀掉 USB endpoint**，禁用
- 黑屏后**不要长按电源**

---

## 4. 源码准备状态

**profile4 封存件**（沿用交接，本轮不构建）：

| 项 | 值 |
|---|---|
| 驱动 | `headset=4`，SHA256 `3c79386ac367ba81fe02774fbb8d37a6475845432a644ff91fdcb56a98c279ce` |
| PMIF 头 | SHA256 `f4d9a964e304ea14686359b85e75560325e9d47ee2ecb5614aae538f9e293d78` |
| 隔离树 | `logs/profile4-kernel-src-20261116-1525/`（5205 文件、无 BUILD 时基线） |
| **host kernel** | **`cac498da96406cc4f660e2cc717dbcf93b259ae1d864de6f30a9e920ed03401d`**（15321160 B） |
| 8 项生产输入 | **不一致 0 项** |

**这些是既有封存件，本轮未改动、未重建。**

---

## 5. 边界

**本轮没有**：重启、刷写、生成 boot args、打镜像、
碰 `spmi_access` / codec dump / PMIC / GPUEB、动任何 WIP。

**只读命令**：`adb devices -l`、`getprop`、`cat`、`md5sum`、`ls`、`blockdev`。
全部限时。

**设备状态**：原厂 Android 运行中、slot_a、boot_a MD5 未变。
