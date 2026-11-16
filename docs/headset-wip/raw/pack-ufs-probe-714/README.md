# PACK-UFS-714：UFS 只读探针镜像打包 + 补充测试复跑

> 2026-11-17 01:20，DSH。**仅主机。无任何 adb/fastboot/设备操作。**
> CMD 身份：`f43a4d4ec008b05b759e6f62461791a4778349caba37d1654a5a540da64358be`

**🔴 这是准备可审查的产物，不是设备放行。不存在 DEVICE714 授权。**

---

## 0. 产物

| 项 | 大小 | SHA256 |
|---|---|---|
| **boot.img** | **22994944 B** | **`91122cdfc72ce3d204bb6ffdd71c1bca7c470e525d28cfdd7ce19bb014231f07`** |
| **boot.img MD5** | — | **`c230bf743ce03e8799e404b593c2b7f7`** |
| loader.bin | 48768 B | `0623637e7d34256a5163e6d886bd7b2f0f0e3e545d9b78ed68b509590384c89a` |
| payload.bin | 19515784 B | `c7d73935a1338a3e75cba6d01da536e7352c0a73dd2a51c18ae94239b1fe97c3` |
| **kernel（嵌套）** | **15321328 B** | **`ecdc9194fbcc635a63d8da6be7c5f4ef6a1c6bc7f3c1b715c9373cca9fe7da38`** |

**路径**：`P/logs/ufs-probe-20261117/trial714/`

---

## 1. 输入核验

| 输入 | 期望 | 实测 |
|---|---|---|
| **内核** | 15321328 B / `ecdc9194…` | ✅ |
| **候选** | `ce6aa585…` | ✅ 树内副本与 candidate 逐字节相同 |
| stock | 67108864 B / `c5f8e7bd…` / md5 `a63b2e35…` | ✅ |
| ramdisk | `78985f67…` | ✅ |
| kernel.ver | 48 B / `363b5257…` | ✅ |
| **loader 源** | `mtkloader.c` = `60647f896d027089…` | ✅ 37 文件，从 trial713 复制 |

---

## 2. 参数串（三个字节数独立核算）

| 项 | CMD 声明 | 实测 |
|---|---|---|
| base | **479** | **479** ✅ |
| runtime | **539** | **539** ✅ |
| with NUL | **540** | **540** ✅ |
| **<= 608** | — | **True**，余量 68 ✅ |

### 2.1 本体定位

**`loader.bin` 里出现 3 次：**

| # | offset | 后随 | 判定 |
|---|---|---|---|
| **1** | **`0x931e`** | **NUL** | **本体** ✅ |
| 2 | `0x98f4` | `\n` | printf 片段 |
| 3 | `0xa9fe` | `\n` | printf 片段 |

**本体 479 B 逐字节匹配。**

### 2.2 关键项各 1 次

`mali_trial=714`、`headset=0`、**`mtk_ufs_probe=1`**、`cpus=4`、`loader_return_after=150`、`mali_nopdc=1`

### 2.3 禁止项全 0

`mtk_cpu_probe`、`hp_`、`cpufreq=`、`ddr`、`gpueb`、`mali_trial=713`、`tone`

**→ 无 CPU 探针、无 headset 参数、无遗留 cpufreq/ddr/gpueb。**

---

## 3. native rc

| 步 | rc |
|---|---|
| `start.o` | **0** |
| `mtkloader.o` | **0** |
| `ld.lld` → `loader.bin` | **0** |
| `mkpayload.py --no-version` | **0** |
| `chainboot.py --partition-size 64M` | **0** |

```
22994944 bytes (program 48768, Image 42201364, payload 19515784, kernel gz 21113379)
```

---

## 4. 严格解包

| # | 项 | 结果 |
|---|---|---|
| 1 | header 前 4096 差异 | **仅 4 字节 `[8,9,10,11]`**（`kernel_size`）✅ |
| 2 | **stock 组件** | `ramdisk_size` 1867873 同、`os_version` 同、`header_size` 1584 同、`header[24:40]` 同、`cmdline 44..1068` 同、**`sig_size` 4096 同** ✅ |
| 3 | 解压链 | loader 段**仅 10 字节**（`0x12/0x13/0x28-2b/0x30-33`）、**填充全零**、**stock Image 逐字节相同** `12d255cd`、**payload 逐字节相同** ✅ |
| 4 | 嵌套 XNUP | kernel `ecdc9194…`、ramdisk `78985f67…`、kernel.ver `363b5257…`、**三条目 padding 全零**、**24 B 终止项在 `pos=19515760`**、**exact EOF** ✅ |
| 5 | loader header 补丁 | `0x10=0x3c00000`、`0x28=42201364`、`0x30=0x287f120` ✅ |
| 6 | **v4 签名（AVB）** | 见 §4.1 |

### 4.1 🔴 v4 签名核验（我最初读错了偏移）

**签名不在 `[1584, 5680)`。** `chainboot.py` 第 10-11 行说签名在 **paged kernel 与 paged ramdisk 之后**。

**正确偏移 = `4096 + paged(kernel) + paged(ramdisk)`：**

| 镜像 | 签名 offset | magic | SHA256 |
|---|---|---|---|
| **stock** | **17190912** | **`AVB0`** | **`692c87adcc6ad5fcf0592878a6fe56f13e0da9dfade792f1e8aa1f03e34623c5`** |
| **714** | **22990848** | **`AVB0`** | **同上** |

**🔴 逐字节相同：True。**

**→ 签名完整保留。AVB 哈希对新内核是陈旧的（chainboot 第 21 行明说），但签名块本身原样复制。**

---

## 5. 补充测试复跑（`sdb_slots` 格式化断言）

**这是您指出我的意见后新增的。**

### 5.0 我的原意见

> 「`sdb_slots` 这个字段名我核对了但没找到测试断言（grep 关键词 0 处）。
> 它只是从 CAP 解出来打印，没有语义断言。如果将来要靠它判断队列容量，需要另加测试。」

**Codex 确认这条有效，新增 `test-ufs-report.c`（836 B / `be76c2c7…`），保留原 fixture。**

### 5.1 它的做法

```c
#define main ufs_original_model_main
#include "test-ufs.c"      /* 复用原 fixture */
#undef main

int main(void) {
    assert(ufs_original_model_main() == 0);          /* 先跑完原 2324 */
    for (unsigned capacity = 1; capacity <= 32; capacity++) {
        ...
        snprintf(expected, sizeof(expected), "sdb_slots=%u\n", capacity);
        assert(strstr(log_text, expected) != NULL);   /* ← 格式化断言 */
        assert(strstr(log_text, "result=FLAGS_OBSERVED(0)") != NULL);
    }
    ...
}
```

**→ 断言 `sdb_slots=` 后面那**个实际格式化出来的数字**，32 种容量全测。**

### 5.2 我独立复跑

| sanitizer | compile rc | run rc | stderr | 输出 |
|---|---|---|---|---|
| **ASan+UBSan** | **0** | **0** | **0 B** | `PASS 2324 …` + **`PASS 32 supplemental formatted-slot assertions; original fixture preserved`** |
| **MSan** | **0** | **0** | **0 B** | 同上 |

**→ 2324 + 32 全过，与 Codex 声明一致。**

---

## 6. 守卫（全部未变）

| 项 | 值 |
|---|---|
| stock | 67108864 B / `c5f8e7bd…` / md5 `a63b2e35…` ✅ |
| shared `hakios-kernel` | `67f9f933117f9bc2…` ✅ |
| wrapper | `95247bb011c136d2…` ✅ |
| 710 kernel | `cac498da96406cc4…` ✅ |
| 711 kernel | `24df55bfaf7fdd66…` ✅ |
| 712 kernel | `a2f6db113f599603…` ✅ |
| **713 kernel** | **`a80231ed9dc78a82…`** ✅ |
| 710r2 img | `ca161c1bdb002540…` ✅ |
| 711 img | `3f57a80680e1eb79…` ✅ |
| 712 img | `b97f96722c5c7961…` ✅ |
| **713 img** | **`127e48e9c7089e98…`** ✅ |
| **生产 headset** | **`8c0cf1c232d0ac4f…`**（未改）✅ |
| **生产 ufs** | **无新增** ✅ |

---

## 7. 🔴 一个必须报告的失误

**第一次构建 714 时，我的路径替换没命中，输出写进了 `trial713` 目录，
覆盖了 `trial713/loader.bin`、`start.o`、`mtkloader.o`。**

**发现后立即处理：**

| 步 | 动作 |
|---|---|
| 1 | 确认 `trial713/boot.img`（`127e48e9…`）与 `payload.bin`（`871508dc…`）**未被覆盖** ✅ |
| 2 | 用 `trial713` **自己记录的 argv 原样重放** |
| 3 | `loader.bin` 恢复为 **`78611b0f…`**，**与 trial713 原始记录的 hash 完全一致** ✅ |

**⚠️ 但 `start.o` / `mtkloader.o` 的原始 hash 当初没有记录**（README 只记了 rc=0），
**所以这两个文件我只能确认「重放后 rc 与记录一致」，无法证明字节相同。**

**教训：路径替换要用绝对路径且验证输出目标。**

**根因**：我用相对路径 `logs/mt6895-cpu-baseline-20261116/trial713` 做 `str.replace`，
但 argv 里同时出现绝对路径与相对路径两种形式，只替换了绝对的那种。

---

## 8. 独立复核归档（2026-11-17 01:30 追加）

**Codex 独立审查 PASS。归档在本目录 `review/`：**

| 文件 | 内容 |
|---|---|
| `POST-PACK-AUDIT-714.md`（本目录根，4140 B / `c0c19dc4…`）| **完整审查报告** |
| `review/independent-kernel-review.json` | 内核算查 |
| `review/independent-package-review.json` | 打包结构 |
| `review/independent-loader-review.json` | **loader 安全重编** |
| `review/independent-signature-review.json` | **签名核验** |
| `review/independent-source-archive-review.json` | 源码归档 |
| `review/reproduce-loader-safe.py` | **可复现的安全重编脚本** |

### 8.1 它的关键结论

| 项 | Codex 独立结论 |
|---|---|
| 镜像 `91122cdf…` | ✅ 结构/provenance 全过 |
| **loader 安全临时目录重编** | ✅ 逐字节一致 `0623637e…` |
| **714 两个 `.o`** | ✅ **hash 已记录且复现**（见 §8.2）|
| 713 最终 image/payload | ✅ 匹配原审查值（`127e48e9` / `871508dc`）|
| 713 恢复 loader | ✅ 匹配 `78611b0f` |

**🔴 「主机 PASS 不是 UFS 运行时、存储读、文件系统或无人值守恢复的结果」—— 这句我完全同意。**

### 8.2 714 的 `.o` hash（现在有记录了）

```
start.o      e42598afd6c57632fa396ff21ef6961a9feb73b21bc560110add8e4fa84000e3
mtkloader.o  5c69cbc49a351689f569888427e0252d72622e7b706c0bbe2e7b69bc05dd7efa
```

**→ 这正是我在 §7 里指出的缺口（713 的 `.o` 没记录）。714 补上了。**

### 8.3 `reproduce-loader-safe.py` 的做法（比我原来的对）

**Codex 写的这个脚本「解析带路径的 argv，而不是做一次笼统的 substring 替换」：**

| 步 | 动作 |
|---|---|
| 1 | **解析**每条 argv 里哪些元素承载路径 |
| 2 | 把编译器源、`BOARD_HEADER`、链接脚本/对象、**三个 `-o` 输出**全部改写到一个**私有临时目录** |
| 3 | **在任何命令执行之前审计全部输出** |
| 4 | 然后才跑 |

**→ 这正面解决了我 §7 那个 bug 的根因（我用了相对路径 `str.replace`，漏掉了 argv 里同时存在的绝对与相对两种形式）。**

**它的新 loader 与 714 逐字节相同。**

### 8.4 防止复发的要求（我记下）

**Codex 明确：**

> 「Future packaging must resolve both relative and absolute paths and **validate the complete
> output set before the first compiler is launched**. **Do not replay a recorded output
> destination blindly.**」

**我照做。**

---

## 9. 边界

**未做**：任何 adb/fastboot/设备操作、刷写、重建内核、改候选或生产、
替换旧 BUILD、stage GPUEB WIP、push 生产仓。

**不存在 DEVICE714 授权任务单。**

**镜像 hash 只是打包产物标识，不代表验证过能跑。探针从未在真实硬件执行。**
