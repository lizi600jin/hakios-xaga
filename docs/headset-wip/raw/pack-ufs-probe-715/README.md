# PACK-UFS-715：GPU 实验路径移除版 + 独立主机审查

> 2026-11-17 01:50，DSH。**严格仅主机。无任何 adb/fastboot/设备查询。**
> CMD 身份：`b2880fab8fdcfa011d76e4004ca1a53eb06b9fbe7a281919c8e33139c2259f53`

**🔴 主机审查通过不等于硬件可用。714 与 715 都从未上机。无 DEVICE715 授权。**

---

## 0. 产物身份（我独立核验，全部匹配声明）

| 项 | 大小 | SHA256 |
|---|---|---|
| **boot.img** | **22994944 B** | **`65bd797dce99a4f29d898c7055a25fcce207e1b3642109daae0ad217f653c994`** |
| **MD5** | — | **`dced496c752ae0bec3de806b94a81615`** |
| **loader.bin** | **46504 B** | **`c3d9c30a0c14c3e6c644fbf499fc189819b818c03080fe2beee18590cf7eafea`** |
| **payload.bin** | **19515784 B** | **`c7d73935a1338a3e75cba6d01da536e7352c0a73dd2a51c18ae94239b1fe97c3`** |
| **嵌套 kernel** | **15321328 B** | **`ecdc9194fbcc635a63d8da6be7c5f4ef6a1c6bc7f3c1b715c9373cca9fe7da38`** |

**boot 与 loader 的尺寸/哈希和计划声明逐项一致；boot MD5 一致。**

---

## 1. 与 714 的关系（我实测）

| 项 | 结果 |
|---|---|
| **payload.bin** | **逐字节相同** ✅（`cmp` 无差异）|
| **嵌套 kernel** | **同 hash** `ecdc9194…`，15321328 B ✅ |
| boot.img | **不同**（因为 loader 与参数变了）|
| loader.bin | **46504 B vs 714 的 48768 B**，小 2264 B |

**→ 完全符合「与 714 同 payload/kernel」的声明。**

---

## 2. 参数串（169 / 229 / 230）

| 项 | 声明 | 实测 |
|---|---|---|
| base | **169** | **169** ✅ |
| runtime | **229** | **229** ✅ |
| with NUL | **230** | **230** ✅ |
| **<= 608** | — | **True**，余量 **378** ✅ |

**完整 base 串：**

```
rd=md0 -v cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=0 mali_trial=715 headset=0 mtk_ufs_probe=1 loader_return_after=150
```

### 2.1 关键项各 1 次

`mali=0`、`mali_trial=715`、`headset=0`、`mtk_ufs_probe=1`、`cpus=4`、`loader_return_after=150`

### 2.2 🔴 21 个 Mali 实验参数**全部移除**

我逐个查了：`mali_cold` / `mali_preas0` / `mali_low` / `mali_trace` / `mali_witness` /
`mali_ptvendor` / `mali_scclk` / `mali_scupd` / `mali_sc26` / `mali_cacheflush` /
`mali_resetonly` / `mali_cohnone` / `mali_hwissues` / `mali_stackpwron` / `mali_mcuwin` /
`mali_mcuseq` / `mali_doorbell` / `mali_pdcsettle` / `mali_statussem` / `mali_assorder` / `mali_nopdc`

**→ 21 项全部 0 次出现。**

### 2.3 其他禁止项全 0

`mtk_cpu_probe`、`hp_`、`cpufreq`、`ddr`、`gpueb`、`tone`、`mali_trial=714`、`mali_trial=713`

---

## 3. 五个 GPU 宏（两个层面都核验）

### 3.1 编译 argv 里显式为 0

```
-DBOARD_MALI_POWER=0
-DBOARD_MALI_PREPOWER=0
-DBOARD_MALI_EARLY=0
-DBOARD_MALI_RETURN=0
-DBOARD_MALI_STOP=0
```

### 3.2 预处理器输出的最终 `#define`

**`trial715/loader-macros.txt`（31138 B / `d9c42628f9dd577a…`）里：**

| 宏 | 最终值 | 714/713 基线 |
|---|---|---|
| `BOARD_MALI_POWER` | **0** ✅ | **713 是 1** |
| `BOARD_MALI_PREPOWER` | **0** ✅ | 0 |
| `BOARD_MALI_EARLY` | **0** ✅ | 0 |
| `BOARD_MALI_RETURN` | **0** ✅ | 0 |
| `BOARD_MALI_STOP` | **0** ✅ | 0 |

**→ 五个宏确实都为 0，且唯一有变化的是 `BOARD_MALI_POWER`（1→0）。**

### 3.3 🔴 宏生效的**决定性证据**：看二进制

**`loader-macros.txt` 是 `-dM -E` 输出 —— 937 行，全部是 `#define`，没有代码体。**
**所以在宏文件里 grep 函数名永远是 0 次，那不是有效方法。**

**我改用 `strings` 查实际产物：**

| loader.bin | `gpu:` 消息数 |
|---|---|
| **713** | **4** |
| **714** | **4** |
| **715** | **0** ✅ |

**713/714 里那 4 条原文：**

```
gpu: the sleep controller does not answer; leaving the domains alone
gpu: about to put the SRAM isolation back
gpu: domains down and isolation back
gpu: the domains are back as Android expects them
```

**→ 715 里一条都没有。GPU 编译路径确实被完全移除。**

**（715 里仍有 3 条含 `mali` 的字符串，但全部是 boot-args 参数串本身，不是 GPU 代码。）**

---

## 4. 运行时 Mali 早期返回

**`hakios_mali610_init` 在 `mali=0` 时创建 worker 之前就返回。**

**这移除的是实验软件路径，不改变 LK 留下的物理 GPU 状态，也不修复 713 暖返回事件的成因。**

---

## 5. 五步 native rc 全 0

| 步 | rc |
|---|---|
| `start.o` | **0** |
| `mtkloader.o` | **0** |
| `ld.lld` → `loader.bin` | **0** |
| `mkpayload.py` | **0** |
| `chainboot.py` | **0** |

### 5.1 🔴 九条编译警告（保留，不是零警告构建）

**全部 9 条在 `mtkloader.o` 这一步，且都在冻结源码里：**

| 文件 | 行 |
|---|---|
| `ufs.h` | 762、1544、1608、2702、2882 |
| `mtkloader.c` | 7488、611、617 |
| `mtkconnsys.h` | 98 |

**→ 这是既有警告，不是本轮引入。我不把它们算作「干净构建」。**

---

## 6. 独立复现（Codex 的做法，我审了实现）

**`independent-loader-review-715.json`（5633 B）记录：私有目录重编，两个对象与 loader 逐字节一致。**

**`reproduce-loader-safe.py` 的关键设计（我已审）：**

| 步 | 动作 |
|---|---|
| 1 | **解析**带路径的 argv，不做笼统 substring 替换 |
| 2 | 编译器源、`BOARD_HEADER`、链接脚本/对象、**三个 `-o` 输出**全部改写进私有临时目录 |
| 3 | **在任何命令执行之前审计全部输出** |
| 4 | 然后才跑 |

**→ 这正面解决了我 714 那次 bug 的根因。旧 713/714 目录不是重放目标。**

---

## 7. 严格解包（Codex 已验，我核对了产物身份与结构一致性）

**按计划声明，以下均已通过：**

- stock Android Image 与 ramdisk 未变
- 仅文档化的 header 补丁
- payload 精确匹配
- 三个 XNUP 条目 + **24 字节零终止项** + **exact EOF**
- 嵌套 kernel 是批准的 UFS hash

### 7.1 签名字段（沿用 714 的更正）

**`signature_size` 在 offset **1580**，值 **4096**。**

**stock / 713 / 714 / 715 的签名字节保留；保留 ≠ 该签名对改动后的内核有效。**

---

## 8. UCRDY 的准确表述（按 CMD 要求澄清）

**在本探针里 `UCRDY` 是 **report-only** —— 它不是门槛，只在输出里报告。**

**这只是**本探针的选择**，**不是关于所有 vendor 驱动检查项的通则**。**

**证据**：候选源码里 `HCS_UCRDY` 只出现在一处打印语句，不在任何 `if` 判定里。

---

## 9. 环境与冻结件

| 项 | 值 |
|---|---|
| 探针源 | **8138 B / `ce6aa585…`**（未改）|
| loader 源镜像 | 与 713 相同的冻结镜像（37 文件）|
| 测试 fixture | 未改（2324 + 32 仍适用，715 未重跑不同探针模型）|

---

## 10. 未验证项（与 714 相同，未因打包而变化）

| # | 项 |
|---|---|
| 1 | **714 与 715 都从未上机** |
| 2 | 映射可能 fault；回调失败**不是** abort 恢复 |
| 3 | 只读源码**不保证**硬件读没有副作用 |
| 4 | 未建立队列所有权、有效 DMA 地址、真实存储 IO |
| 5 | 未建立 link mode、gear/lane 状态、时钟或电源状态 |
| 6 | `FLAGS_OBSERVED` 只报告所选的标志位 |
| 7 | **没有 XNU UFS 存储驱动** —— 这是有限快照探针 |

---

## 11. 边界

**未做**：任何 adb/fastboot/设备查询、刷写、重建内核、重放旧目录、改生产树或 GPUEB。

**未改忽略规则，未覆盖封存 713/714 产物，保留了先前的打包事件与签名更正记录。**
