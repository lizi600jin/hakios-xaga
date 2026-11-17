# UFS READ716 集成审查与隔离构建

> 2026-11-17 13:50，DSH。**仅主机。未打包、未碰设备、未刷写。**
> CMD 身份：`44a84a093db70a60d7634ad8e25f653172a37501b22de72a854a23777f60d2c4`

---

## 0. 结果

| 项 | 值 |
|---|---|
| **MANIFEST** | **51/51 全匹配** |
| **kernel inputs** | **5210 / 5210 OK，0 FAILED，rc=0** |
| **集成 diff** | **4 变化 + 4 新增 + 0 删除**，无 CPU 探针 |
| **三处集成点** | **全部按声明位置核实** |
| **make native rc** | **2** |
| **judge native rc** | **0** |
| **新内核** | **15338824 B** |
| **SHA256** | **`c7f2f3222f314b82885b0567975e3af759c9b172ececd6fbefe437b65ddff6aa`** |

---

## 1. MANIFEST 与输入

### 1.1 `HOST-INTEGRATION-MANIFEST.json`

| 项 | 值 |
|---|---|
| 大小 / hash | 8782 B / `f18ad924926eaec5...` |
| status | `FROZEN_FOR_HOST_REVIEW` |
| files | **51 项**（39 loader 镜像 + 12 候选）|
| **校验结果** | **51/51 匹配，0 不符，0 缺失** |

### 1.2 5210 内核输入

**`kernel_manifest_sha256 = 7f7680433081188646e512f21cd1ecbc33a85b28a3d7e5ed6f941dd5b79a5c1d`**

**🔴 我一开始误以为这是「内容总表 hash」，实际它是 `kernel-inputs.sha256` **文件自身**的 SHA256**
（我算文件内容总表得 `caf906cf...`，两者不同 —— 已核对清楚）。

**`sha256sum -c kernel-inputs.sha256` → rc=0，5210 OK，0 FAILED。**

### 1.3 集成 diff（716 vs 715 基底）

| 类 | 数 | 明细 |
|---|---|---|
| **变化** | **4** | `bsd/conf/files`、`bsd/dev/hakios.input.c`、`bsd/sys/hakios.input.h`、`osfmk/arm64/loader_return.c` |
| **新增** | **4** | `hakios.mt6895_ufs_read.c`、`ufs_atomic_lease.h`、`ufs_read_contract.h`、`ufs_read_engine.h` |
| **删除** | **0** | — |

**✅ 无 `hakios.mt6895_cpufreq.c`，diff 里 `cpufreq` / `cpu_probe` 零命中。**
**✅ 构建前 `BUILD` 不存在。**

---

## 2. 🔴 三处集成点的实际顺序（我逐行核实）

### 2.1 ① loader 的 READ 在 `logbook_boot_note` **之后** ✅

**`loader-candidate/loader-src/mtk/loader/mtkloader.c`：**

| 行 | 内容 |
|---|---|
| **3828** | `logbook_boot_note();` |
| **3829** | `ufs_read_guard_start();` |
| 3830 | `out("Jumping to XNU...")` |
| **3831** | `jump_to_kernel(...);` |

**预处理输出 `preprocessed.c` 同样：4994 到 4995 到 4997。**

**→ 顺序正确：先 logbook_boot_note（最后一次 WRITE），再 guard_start（READ + lease 初始化）。**

### 2.2 ② `ufs_read_guard_before_storage()` 是 `xnu_returned` 的**第一条语句** ✅

```c
void xnu_returned(uint64_t log_pa, ...)
{
	ufs_read_guard_before_storage();   /* 第一条 */
	struct span sp = { ... };
	wdt_disable();
	...
```

**预处理输出第 **4208** 行 —— 在整个函数体最前，早于 `wdt_disable`、`read_screen`、
HLOG 写、gpt 操作。**

**失败路径：**

```c
if (!ur_guard_allows_storage(r, ufs_read_guard_barrier, 0)) {
    out("ufs-read guard: ownership NOT returned; no HLOG/Android IO; manual fastboot required");
    for (;;) { __asm__ volatile("wfe"); }
}
```

**→ 无限 wfe 停住，**不写 HLOG、不 warm-start Android、不猜 reset** —— 与 CMD 第 2 条一致。**

### 2.3 ③ quiesce 在 SMP-stop 之后、紧邻 headset ✅

**`osfmk/arm64/loader_return.c:377`：**

```c
psci_smp_stop_others();
hakios_mt6895_headset_quiesce(); /* software stop only: other CPUs may not all have stopped */
hakios_mt6895_ufs_read_quiesce(); /* software STOP/shared lease only; no controller access */
```

**→ 位置正确。且注释明确承认「other CPUs may not all have stopped」——
这正是 CMD 第 3 条要我核对的竞态前提。**

**🔴 我曾错误地只搜 `bsd/` 判断「找不到调用者」—— 实际在 `osfmk/arm64/`。
这是我的搜索范围错误，不是代码问题。**

### 2.4 quiesce 实现核对（`hakios.mt6895_ufs_read.c:162`）

**该函数体只调 `ur_atomic_stop(&lease)`，无 MMIO、无 printf、无等待循环。**

---

## 3. 🔴 CAS 顺序与「其他 CPU 未必都停」的核对

| 项 | 状态 |
|---|---|
| quiesce 在 `psci_smp_stop_others()` **之后** | ✅ 代码顺序 |
| 但注释承认其他 CPU **未必全停** | ✅ 代码注释 |
| quiesce 用 `fetch_or(STOP_BIT)` 且可 publish POISONED | ✅ 原子操作，无等待 |
| `finish(true)` 的 CAS 要求 STOP 缺席 | ✅ 若 quiesce 先到，CAS 必失败 |

### 🔴 更正：不能用「核都停了」解释 STOP 的 load/store 竞态

**CMD 明确指出：其他 CPU **可能没有全部停止**，所以**不能**用这个假设来免除竞态。**

**正确的 fail-closed 理由是另外两条：**

| # | 理由 |
|---|---|
| **1** | **STOP 阻止 clean CAS** —— STOP_BIT 一旦置位，`finish(true)` 的 `CLAMED → RESTORED` CAS **必然失败**，phase 永远无法变成 RESTORED |
| **2** | **loader gate 拒绝交接** —— `ur_loader_may_use_storage` 只在 `UNUSED` 或 `RESTORED` 时放行；STOP 后的状态是 CLAIMED+STOP 或 POISONED，**两者都不放行**，loader 拿不到存储与 Android 交接权 |

**→ 安全性**不依赖**「其他 CPU 已停」，而是依赖「**即使 STOP 后还有晚到的 load/store 生效，
最终状态也无法变成 RESTORED，loader 仍被拒绝**」。**

**这与我上面写的第 1、2、5 条并不冲突，但**把结论的支点放错了位置** ——
支点应当是「CAS 赢不了 + gate 不放行」，不是「核停了所以没有并发」。**

**没有发现语义漏洞，但解释已按此更正。**

---

## 4. 构建

| 项 | 值 |
|---|---|
| 命令 | `XNU_SRC=<绝对> ./xnu-build.sh -k -j8` |
| 时间 | 13:48:36 到 13:49:52 |
| **make native rc** | **2** |
| 编译错误 | **0** |
| undefined | **0** |
| `CC loader_return.o` | ✅ |
| `CC hakios.mt6895_ufs_read.o` | ✅ |
| **judge native rc** | **0** |
| 判词 | `only the kext symbol-set checks failed` |

**🔴 make rc=2 与 judge rc=0 分开记录，未混淆。**

---

## 5. 内核身份与编入证明

| 项 | 值 |
|---|---|
| 大小 | **15338824 B** |
| SHA256 | **`c7f2f3222f314b82885b0567975e3af759c9b172ececd6fbefe437b65ddff6aa`** |

**13 条 `mt6895-ufs-read` 消息全部编入：**

| 关键消息 | 含义 |
|---|---|
| `single READ10 lun2 lba1 bytes4096; guarded ownership required` | 入口 |
| `guard magic=%#x version=%u trial=%u state=%u` | **lease 快照** |
| **`fresh loader guard absent; no request`** | **fail-closed** |
| `fixed scratch overlaps managed memory; no mapping` | 地址检查 |
| `inherited controller predicates failed; no request` | idle 检查 |
| `result=%s(%u) claimed=%u issued=%u restored=%u data_valid=%u polls=%u` | 结果 |
| **`final shared state=%u loader_io_allowed=%u`** | **最终租约状态** |

**符号 `hakios_mt6895_ufs_read_init` / `_quiesce` 均存在。**

**🔴 消息存在只证明编入，不是存储 IO 证据。**

---

## 6. 构建后复查与守卫

| 项 | 结果 |
|---|---|
| **5210 输入** | **rc=0，5210 OK，0 FAILED** |
| BUILD 外文件数 | 5210（未变）|
| **新增/删除（BUILD 外）** | **0** |

| 守卫 | 值 |
|---|---|
| stock | 67108864 B / `c5f8e7bd...` / md5 `a63b2e35...` ✅ |
| shared `hakios-kernel` | `67f9f933...` ✅ |
| wrapper | `95247bb0...` ✅ |
| 710 / 711 / 712 / 713 / 715 内核 | `cac498da` / `24df55bf` / `a2f6db11` / `a80231ed` / `ecdc9194` ✅ |
| **loader-candidate** | **47264 B / `ca0112e2d6659c37...`** ✅ |
| 生产 headset | `8c0cf1c2...`（未改）✅ |
| 生产 ufs_read | **无** ✅ |

---

## 7. 私有目录 loader 重编复现

**源镜像复制到 `host-review-mine-716/loader-src`（39 文件），
审计所有 argv 无旧目录路径后执行：**

| 命令 | rc | 输出 |
|---|---|---|
| compile | **0** | start.o |
| compile | **0** | mtkloader.o |
| link | **0** | loader.bin |

**私有产物：47264 B / `ca0112e2d6659c37a469fe60881f9a14d8de5e87008ee50cce8007572b800a13`**

**🔴 与候选 loader.bin 逐字节相同 ✅**

**九条既有警告保留（第 [1] 条命令 9 条）。**

---

## 8. 关于 v3 与撤回（按 CMD 要求保留）

**CMD：「Preserve the old review and withdrawn objection with corrected attribution.」**

| 项 | 状态 |
|---|---|
| 旧 `ufs-engine-review-716`（审 `21c88fb5`） | **原样保留** |
| 旧 `ufs-engine-review-v3-716`（审 `7c2b4df4`） | **原样保留**，含 4.5 的**撤回记录** |
| 我的错误判断 | **明确标注已撤回，写明错在「用 grep 关键词代替覆盖分析」** |
| v3 的 `7c2b4df4` | **未改** |

---

## 9. 我的反对意见

### 9.1 一条技术观察（不是阻塞）

**`ufs_read_guard_before_storage` 失败时用无限 wfe 停住 —— 这是**有意的**，但意味着：**

| 后果 | 说明 |
|---|---|
| 设备停在 loader | 需用户手动进 fastboot |
| **当前 HLOG 会丢失** | CMD 第 2 条已承认 |
| 无 watchdog 兜底 | CMD 明确不添加 |

**→ 设计选择，已如实记录。**

### 9.2 一处建议未来加测试的点

**`platform_write_word` 用 `__atomic_load_n(..., ACQUIRE)` 单独读 phase，
再写。若 quiesce 在这个 load 与 store 之间加了 STOP_BIT，这次 store 仍会执行
（低两位还是 CLAIMED）。**

**→ 严格说 quiesce 之后可能还有一次描述符写生效。**

**但本设计里不构成问题**：quiesce 只可能发生在 SMP-stop 之后，此时核已不再跑引擎；
即使发生，`finish(true)` 的 CAS 会因 STOP 失败 → poison。

**→ 记为观察项，建议未来若加并发测试可覆盖这个窗口。**

---

## 10. 边界

**未做**：打包、刷写、设备查询、生产编辑、旧 BUILD/产物覆盖、
stash/reset/checkout/clean/gc/prune、GPUEB、PMIC、watchdog、OEM expdb。

**原始 GPT 未上传。忽略规则未改。GPUEB WIP 未 stage。**
