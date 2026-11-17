# PACK-REVIEW717：诊断镜像独立验包

> 2026-11-17 14:32，DSH。**仅主机。未重建、未重打包、未碰设备。**
> CMD 身份：`450a66600f0cc8f760f7ac34ca8b47d4d26081a8d56f4256286df7e3b8e54447`

---

## 0. 结果

| 项 | 值 |
|---|---|
| **boot.img** | **22999040 B** / **`c275625ff431ee93b8fdc87284c1336bd631d7721af13415deba124392496699`** |
| **MD5** | **`69dea280d97bdca4267c114f1ca537b8`** |
| **loader.bin** | 47264 B / `3c88cd069e000b48d0eac8be9c5199b9395da68355ee4bfeae17b91049e7f566` |
| **payload.bin** | 19533320 B / `bb40824042997ba968afb5ad3be162f67e48df4d23ac5565d6ed0e32b216f49c` |
| **嵌套 kernel** | 15338864 B / `07b016feb866c4347d72a3915ef2dce15045bfa2d0628d3d1f9eb5ff385506d6` |
| **严格验包** | **PASS**（我独立跑到新私有路径）|

**四项身份与 CMD 声明逐项精确匹配。**

---

## 1. 八项独立核对

### 1.1 stock header 差异只有 `kernel_size` ✅

**位置 `[8,9,10,11]`，共 4 字节。**

### 1.2 v4 signature @1580 = 4096，逐字节保留 ✅

| 项 | 值 |
|---|---|
| stock / 717 的 `word@1580` | **4096** / **4096** |
| magic | `AVB0` |
| SHA256 | **`692c87adcc6ad5fcf0592878a6fe56f13e0da9dfade792f1e8aa1f03e34623c5`** |
| **与 stock 逐字节相同** | **True** |

**🔴 保留不等于密码学有效性。**

### 1.3 Android Image 与 ramdisk 未变 ✅

**stock Image 42201364 B / `12d255cd...`，717 内嵌**逐字节相同**。

### 1.4 loader 段仅 header 补丁 ✅

**10 字节：`0x12,0x13,0x28,0x29,0x2a,0x2b,0x30,0x31,0x32,0x33`**

### 1.5 填充全零 ✅

**loader 之后到 `0x40000` 全零。**

### 1.6 XNUP 三条目 + 24 B 终止 + exact EOF ✅

| 条目 | 大小 | SHA256 前 24 |
|---|---|---|
| kernel | 15338864 | `07b016feb866c4347d72a391` |
| ramdisk | 4194304 | `78985f67583b4d4666e46126` |
| kernel.ver | 48 | `363b5257dc819017ddb1f97c` |

**终止项 `pos=19533296`，24 B 全零；exact EOF True。**

### 1.7 参数本体唯一 ✅

**从**实际 loader 体**里定位（不是我照抄声明）：**

| # | offset | 后随 | 判定 |
|---|---|---|---|
| **1** | **`0x9376`** | **`00`** | **NUL 本体** ✅ |
| 2 | `0x9923` | `0a` | printf 片段 |
| 3 | `0x9d3d` | `0a` | printf 片段 |

**→ base **168 字节**，NUL 本体**恰好 1 个**，逐字节匹配。**

### 1.8 sealed 716 未被改动 ✅

| 文件 | hash 前 16 |
|---|---|
| `trial716/boot.img` | **`4ca5834f555300c6`** ✅ |
| `716 kernel` | **`c7f2f3222f314b82`** ✅ |

---

## 2. 相对 716 只有 cookie 与诊断观察

**这一点我在上一轮 REVIEW-BUILD-717 里已逐行核实：**

| 文件 | 变化 |
|---|---|
| `ufs_read_engine.h` | 加诊断掩码与采样 |
| `hakios.mt6895_ufs_read.c` | 加两处打印 |
| `ufs_read_contract.h` | `UR_TRIAL 716u -> 717u` |

**白名单、响应准入、restore、超时、guard 全部未变（含 256 个 bit 变异的等价性测试）。**

**🔴 `diagnostic-only CRC` 在失败位非零时**永不**成为被接受的结果
—— 这点在引擎与平台打印里都有体现（`accepted=%u` 字段显式打印 `data_valid`）。**

---

## 3. 两个脚本

| 脚本 | 大小 |
|---|---|
| `package-717.py` | 2314 B |
| `review-package-717.py` | 8811 B |

**验证器运行到新路径 `host-review-mine-717/`，rc=0、stderr 0 B、`status: PASS`。
**未覆盖 Codex 的 `independent-package-review-717.json`。**

**按 CMD：「No additional native loader reproduction absent a concrete concern」
—— 我没有发现具体疑虑，未重跑 loader 复现。**

---

## 4. 守卫与边界

| 项 | 状态 |
|---|---|
| trial717 镜像 | 未改（只读）|
| **sealed 716 boot/kernel** | **未写** ✅ |
| 候选源 | 未改 |
| 生产树 | 未改 |
| raw GPT / 镜像二进制 | 留本地 |

**未做**：设备查询、刷写、源码编辑、覆盖、生产 push、GPUEB WIP staging、
controller/clock/reset/UIC/watchdog 工作。

**等独立 DEVICE717 释放。**
