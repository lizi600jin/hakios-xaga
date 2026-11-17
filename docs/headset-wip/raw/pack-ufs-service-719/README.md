# PACK-REVIEW719：服务集成镜像独立验包

> 2026-11-17 19:25，DSH。**仅主机。未重建、未重打包、未碰设备。**
> CMD 身份：`78f0f5fb088d7d1b7638450a5f14a76004af17f74a41103c4e2a022f59696cb2`

---

## 0. 结果

| 项 | 值 |
|---|---|
| **boot.img** | **23003136 B** / **`fd6e19ac8386804ac697564a98a1bce62b697b8fbaac2821b6033bf32f4d5862`** |
| **MD5** | **`d54237475b4e1633f9c88720a3fb2b2e`** |
| **loader.bin** | 47952 B / `9d2c5fb9fe534a15...` |
| **payload.bin** | 19534232 B / `c6e9b51e47a89ef8...` |
| **嵌套 kernel** | 15339768 B / `5a85ac7fed4392b1...` |
| **严格验包** | **PASS** |
| **具体阻塞** | **无** |

**四项身份与 CMD 声明逐项精确匹配。**

---

## 1. 八项独立核对

| # | 项 | 结果 |
|---|---|---|
| 1 | stock header 差异 | **只 4 字节 `[8,9,10,11]`** = `kernel_size` ✅ |
| 2 | **`signature_size` @1580** | **4096**，签名 `692c87ad...` **逐字节相同** ✅ |
| 3 | Android Image / ramdisk | **逐字节相同**（42201364 B）✅ |
| 4 | **loader 段** | **恰好 10 字节** header 补丁（`0x12,0x13,0x28-2b,0x30-33`）✅ |
| 5 | 填充 | **全零**到 `0x40000` ✅ |
| 6 | XNUP | 三条目 + **24 B 终止项 `pos=19534208`** + **exact EOF** ✅ |
| 7 | **参数本体唯一** | **`0xaae2`** 后随 NUL，**171 字节**，恰好 1 个 ✅ |
| 8 | **sealed 718 未动** | `3a3ceb1d` / `60bd6b19` ✅ |

**🔴 第 2 项：保留**不等于密码学验证**。签名的 AVB 哈希对新内核是陈旧的。**

---

## 2. 参数：171 / 231 / 232

| 项 | 与 CMD 一致 |
|---|---|
| **base** | **171** ✅ |
| **runtime** | **231** ✅ |
| **with NUL** | **232** ✅ |

### 2.1 关键项各 1 次

`mtk_ufs_service=1`、`mali_trial=719`、`headset=0`、`mali=0`、`cpus=4`、`loader_return_after=150`

### 2.2 🔴 旧参数全部缺席（全 0）

`mtk_ufs_read`、`mtk_ufs_probe`、`mtk_cpu_probe`、`tone`、`gpueb`

**→ 与 CMD 要求「old read/probe/cpu/tone/gpueb arguments absent」一致。**

### 2.3 本体定位

**base 串在 `loader.bin` 里出现 3 次：**

| # | offset | 后随 | 判定 |
|---|---|---|---|
| 1 | `0x9baa` | `0a` | printf 片段 |
| 2 | `0xa996` | `0a` | printf 片段 |
| **3** | **`0xaae2`** | **`00`** | **NUL 本体** ✅ |

---

## 3. 三份独立复核（我核对了要点）

### 3.1 `independent-kernel-review-719.json`

| 项 | 值 |
|---|---|
| status | **PASS** |
| kernel | 15339768 B / `5a85ac7f...` |
| 输入验证 | **5214** |
| 候选/loader 文件 | **57** |
| make / judge rc | **2 / 0** |
| 编译 quiesce 引用 | **True** |

### 3.2 `independent-loader-review-719.json`

| 项 | 值 |
|---|---|
| status | **PASS** |
| loader | 47952 B / `9d2c5fb9...` |
| **私有重编** | **3 个 rc=0** |
| **源文件验证** | **40** |
| **所有输出执行前已审计** | **True** |
| **首个返回语句是 guard** | **True** |
| **最后 boot 写先于 guard 初始化** | **True** |
| **返回审计在既有 HLOG 最后** | **True** |
| **既有 span writer 未改** | **True** |

### 3.3 🔴 它的 limits 记录了我们上轮的更正

**原文：**

> 「One save invocation may make multiple physical WRITE10 commands」

**→ 这正是您指出的「`save` 调用一次 ≠ 物理 `WRITE(10)` 一次」，现已写入独立复核的限制项。**

### 3.4 按 CMD 第 4 条：这是**真实的 gate/audit 变更**

**CMD 明确：「This 719 loader is a real gate/audit change, not merely a cookie change;
do not reuse 718's old equivalence claim.」**

**复核文件确认了三项**行为**变更：**
- **首个返回语句是 guard**（不是 718 的旧形式）
- **最后 boot 写**先于** guard 初始化**
- **返回审计作为第二 span 在既有 HLOG 最后**

**→ 我**没有**复用 718 的等价性声明。**

### 3.5 未重做 loader 复现

**CMD：「No extra loader reproduction needed absent a concrete concern.」**

**我没有发现具体疑虑，故跳过。复核已记录私有重编 3 个 rc=0 且逐字节一致。**

---

## 4. 守卫

| 项 | 状态 |
|---|---|
| trial719 镜像 | 未改（只读）|
| **sealed 718 boot / kernel** | `3a3ceb1d` / `60bd6b19` ✅ 未写 |
| 冻结 57 输入 | 未改 |
| service/state/engine | 未编辑 |
| whiteList/READ geometry/reference/timeout | 未扩 |
| 镜像二进制 / raw GPT / GUID / pre-log | **留本地** |

---

## 5. 边界

**未做**：设备查询、刷写、重建、重打包、冻结输入编辑、生产 push、
旧物覆盖、GPUEB WIP staging、stash/reset/checkout/clean/gc/prune。

**🔴 主机验证成功**不是**运行时注册/VFS/租约证据。**

**等独立的 ONE DEVICE719 释放。**
