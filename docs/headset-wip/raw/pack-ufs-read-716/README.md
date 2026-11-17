# PACK-REVIEW716：镜像独立验包

> 2026-11-17 14:05，DSH。**仅主机。未重打包、未重建、未碰设备。**
> CMD 身份：`60cd26fbf9d58eed...`

---

## 0. 结果

| 项 | 值 |
|---|---|
| **boot.img** | **22999040 B** / **`4ca5834f555300c6616245e265923ac1897ef9d8d6d12c57638fa870afc9c057`** |
| **MD5** | **`3abcb40dfe056bb152eee1fbbef9af57`** |
| **loader.bin** | 47264 B / `ca0112e2d6659c37...` |
| **payload.bin** | 19533288 B / `c13981d45c8d46d9...` |
| **嵌套 kernel** | 15338824 B / `c7f2f3222f314b82...` |
| **严格验包** | **PASS**（我独立运行到新路径）|

**四项身份与 REVIEW-AND-PLAN-716 声明**逐项精确匹配**。**

---

## 1. 参数（168 / 228 / 229）

| 项 | 声明 | 实测 |
|---|---|---|
| base | **168** | **168** ✅ |
| runtime | **228** | **228** ✅ |
| with NUL | **229** | **229** ✅ |
| <= 608 | — | True，余量 **379** ✅ |

```
rd=md0 -v cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=0 mali_trial=716 headset=0 mtk_ufs_read=1 loader_return_after=150
```

**关键项各 1 次**：`mali=0`、`mali_trial=716`、`headset=0`、
`mtk_ufs_read=1`、`cpus=4`、`loader_return_after=150`

**禁止项全 0**：`mtk_ufs_probe`、`mtk_cpu_probe`

### 1.1 🔴 参数本体唯一性（我独立定位）

**在 `loader.bin` 里 base 串出现 3 次：**

| # | offset | 后随 | 判定 |
|---|---|---|---|
| 1 | `0x9871` | `0a` | printf 片段 |
| **2** | **`0x99b2`** | **`00`** | **NUL 本体** ✅ |
| 3 | `0xa934` | `0a` | printf 片段 |

**→ NUL 结尾本体**恰好 1 个**，长度 **168**，逐字节匹配 ✅**

---

## 2. 七项独立核对（我自己的脚本）

### 2.1 signature_size @1580 = 4096 ✅

| 镜像 | `word@1580` | `word@1584` |
|---|---|---|
| stock | **4096** | 0 |
| **716** | **4096** | 0 |

**→ 与 714 那次更正的结论一致：字段在 **1580**，1584 是 padding。**

### 2.2 签名逐字节保留 ✅

| 项 | 值 |
|---|---|
| stock 签名 offset | 17190912 |
| 716 签名 offset | 22994944 |
| magic | `AVB0` / `AVB0` |
| **SHA256** | **`692c87adcc6ad5fcf0592878a6fe56f13e0da9dfade792f1e8aa1f03e34623c5`** |
| **逐字节相同** | **True** |

**🔴 保留不等于密码学验证。**

### 2.3 header 差异只有 4 字节 ✅

**位置 `[8, 9, 10, 11]` —— 正是 `kernel_size`。**

### 2.4 frozen stock Android Image ✅

| 项 | 值 |
|---|---|
| stock Image | 42201364 B / `12d255cdd09fb5b9...` |
| **716 内嵌相同** | **True** |

### 2.5 loader 段仅 header 字段被改 ✅

**10 字节：`0x12,0x13,0x28,0x29,0x2a,0x2b,0x30,0x31,0x32,0x33`**

### 2.6 XNUP 三条目 + 24 B 终止项 + exact EOF ✅

| 条目 | 大小 | SHA256 前 32 |
|---|---|---|
| kernel | 15338824 | `c7f2f3222f314b82885b0567975e3af7` |
| ramdisk | 4194304 | `78985f67583b4d4666e4612681b87972` |
| kernel.ver | 48 | `363b5257dc819017ddb1f97c15be0d3c` |

**终止项 `pos=19533264`，24 B 全零；exact EOF: True。**

---

## 3. 两个脚本的审查

### 3.1 `package-716.py`（2314 B）

**关键：它在动手前做多层断言**——

| 断言 | 作用 |
|---|---|
| `assert not OUT.exists()` | **输出目录不存在才继续** |
| 内核 hash == `independent-kernel-review-716.json` 里的值 | 交叉验证 |
| loader hash == `ca0112e2...` | 硬编码期望值 |
| `mk.count("-o") == 1` | argv 结构检查 |
| `chain[2] == stock 路径` | 恢复源检查 |
| **`Path(...).parent == OUT` 两处** | **所有输出必须落在新目录** |

**→ 这与我在 714 那次踩坑后的教训一致：路径必须显式验证，不能靠字符串替换。**

### 3.2 `review-package-716.py`（8811 B）

**纯读取验证器，我运行到新路径 `host-review-mine-716/`，
**未覆盖 Codex 的独立结果**。**

**rc=0，stderr 0 B，输出 `status: PASS`。**

---

## 4. 🔴 被拒的计划（原始记录保留）

**`trial716-plan-rejected-1/`：**

> 「Host packaging preflight rejected: chainboot argv length was incorrectly expected 9;
> actual recorded argv has 8 elements. **No packaging subprocess ran.** Only the verified
> loader copy existed here; no boot.img/payload and no old artifacts changed.
> Preserve this rejected stage; corrected script validates plans before creating its final
> output directory.」

**已归档 `REJECTION.md`（350 B）与 `package-716.before-fix.py`（2314 B）。**

**→ 按 CMD 要求**保留原始失败记录，不隐藏**。这次失败是**良性的**：
断言先于任何子进程，没有产生半成品。**

---

## 5. 与先前审查的一致性

| 项 | 状态 |
|---|---|
| 5210 内核输入 | 我上轮验过 rc=0 全 OK；Codex 独立复核无变化 |
| 51 候选/loader 源 | MANIFEST 全匹配 |
| loader `ca0112e2` | 与私有目录重编逐字节一致（上轮已验）|
| engine `7c2b4df4` | 冻结未改 |
| platform `bbec3f6d` | 冻结未改 |

**→ 本轮**没有**重跑 loader 复现 —— CMD 明确「No additional reproduction is needed
absent a concrete concern」，我没有发现具体疑虑。**

---

## 6. 两处解释更正（按 CMD 要求）

### 6.1 STOP 的 load/store 竞态 ❌ 不能用「核都停了」解释

**我在 `ufs-read-integration-716` 里曾用「quiesce 在 SMP-stop 之后，核已不跑引擎」
作为安全依据 —— **这是错的支点**。**

**其他 CPU **可能没有全部停止**，所以不能拿这个假设免除竞态。**

**正确的 fail-closed 理由是：**

| # | 理由 |
|---|---|
| **1** | **STOP 阻止 clean CAS** —— STOP_BIT 置位后 `finish(true)` 的 `CLAIMED -> RESTORED` CAS **必然失败**，phase 永远无法变 RESTORED |
| **2** | **loader gate 拒绝交接** —— `ur_loader_may_use_storage` 只放行 `UNUSED` 或 `RESTORED`；STOP 后是 CLAIMED+STOP 或 POISONED，**两者都不放行** |

**→ 安全性**不依赖**「其他 CPU 已停」，而依赖「**即使 STOP 后还有晚到的 load/store 生效，
最终状态也无法变成 RESTORED，loader 仍被拒绝**」。**

**已更正 `ufs-read-integration-716/README.md` 第 3 节。**

### 6.2 OCS 新鲜度不能由 DBR0 单独建立

**DBR0 只说明门铃位被清了 —— 但**上一轮遗留**的 OCS 值同样可能出现在 `table[2]`，
而门铃此刻也是 0。**

**真正建立新鲜度需要三者合取：**

| 要求 | 作用 |
|---|---|
| **DBR0** | 门铃位已清 |
| **OCS 离开 0x0f** | 处理器确实写了 OCS（预填是 0x0f）|
| **响应事务有效** | 事务类型 0x21、LUN 2、状态 0、长度与 tag |

**已追加到 `ufs-engine-review-v3-716/UFS-ENGINE-REVIEW-V3-716.md`。**

---

## 7. 守卫

| 项 | 状态 |
|---|---|
| trial716 三个镜像 | **未改**（我只读）|
| v3 引擎 `7c2b4df4` | 未改 |
| 候选源 | 未改 |
| 生产树 | 未改 |
| 原始 GPT | **留本地，未上传** |
| 镜像二进制 | **留本地，未归档** |
| GPUEB WIP | 未 stage |

---

## 8. 边界

**未做**：重打包、重建、设备操作、生产编辑、ignore 规则改动。

**验包是结构/来源检查，**不是**硬件或电气结果。**
**签名保留不等于密码学验证。镜像 hash 不等于验证过能跑。**
