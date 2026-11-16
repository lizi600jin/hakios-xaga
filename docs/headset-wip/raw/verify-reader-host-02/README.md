# VERIFY-REGMAP-READER-HOST-02：独立重跑与封存

> 2026-11-16 16:44，DSH。**全程主机。
> 未 adb/fastboot、未查设备、未挂 debugfs、未播放、未重启、未刷写、未推工具到手机。**
> **未打 boot/payload、未追加内核构建、未改生产代码/旧 BUILD/wrapper/hakios-kernel/709 冻结树。**

**三层结果分开计，不混成硬件结论：内核指令窗口模型 15 例、C 源码测试 48 例、ARM64 ELF syscall 模型 60 例。**

---

## 0. 结果一览（全部与审定预期一致）

| 层 | 结果 |
|---|---|
| **内核提取** | Image **42201364 B**、SHA256 **`12d255cd…`**、build-id **`76cc81c2…`**、**140592 符号 / 550 marker** |
| **内核指令窗口模型** | **15 / 15 PASS**，`rc=0` |
| **C 源码测试（ASan/UBSan）** | **48 cases / 287 assertions / 0 failed**，零诊断 |
| **ARM64 ELF syscall 模型** | **60 cases / 0 failed** |
| **原件负对照** | **60 例中恰好 3 例 FAIL**，`rc=1` |
| **修正版 ELF** | **4232 B**、SHA256 **`63688efc…`** |

---

## 1. 两份审定文字已原样归档

| 文件 | 字节 | SHA256 | 逐字节 |
|---|---|---|---|
| **`REGMAP-READ-AUDITED.md`** | **5449** | `a89172a6b1e640c9cc0137417ec4ee5a621fabf9b89eb9842060d9d00f256f68` | **IDENTICAL** |
| **`READER-AUDIT.md`** | **2814** | `54786226bd58f52818473419d32072a0473daa66b24fbad89e4cd0c1941baa17` | **IDENTICAL** |

### 我按原文接受、不自行加解释的撤回项

| 撤回/纠正 | 原文依据 |
|---|---|
| **「40 场景、合同完整覆盖」不成立** | `READER-AUDIT.md`：交付源码**实际 32 个 `case_expect` 调用** |
| **旧记录的 test 源码 hash 不绑定当前交付件** | 同上：原记录写 `12653 B / 5b6f04e1…`，当前交付为 `13190 B / 933668e6…` |
| **原 `1d96bdd5` ELF 不作为本轮通过的候选** | 两份审定同时声明，只作负对照输入 |
| **原 DSH 候选目录保持原样** | `logs/regmap-reader-prep-20261116-161918/` 未改（原 ELF 仍 `1d96bdd5`） |

---

## 2. 内核提取（从既有 boot 备份新做）

```bash
python3 logs/regmap-trim-review-20261116/extract-stock-kernel.py \
  --boot /home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img \
  --out <新目录>/kernel
```

| 项 | 值 |
|---|---|
| cwd | `/home/lizi/IOS/hakios-mtk` |
| **真实 rc** | **`0`** |
| stderr | 空 |
| 脚本 SHA256 | `3f868183097dccec4c33318e2ae0850007d353c6f2df342bc72a6f7fd4bb9eac` |
| Python / Unicorn | **3.14.4 / 2.1.4** |
| boot 备份 SHA256 | `c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc`（67108864 B，md5 `a63b2e35…`） |

### 提取结果（逐项对上预期）

| 项 | 预期 | 实测 |
|---|---|---|
| **Image 字节** | 42201364 | **42201364** ✅ |
| **Image SHA256** | `12d255cdd09fb5b9fb5cf9608e6b060a5b2dabc109fd4344b071a3dbdc0cf391` | **同** ✅ |
| **kernel build-id** | `76cc81c2a436243ca099b361b84defb940552ea2` | **同** ✅ |
| **banner** | `5.10.237-Fuutao-Qn_miao-gc5e20589c` | **同**（完整行见 `kernel/extract.stdout.txt`） ✅ |
| **符号记录** | 140592 | **140592** ✅ |
| **marker** | 550 | **550** ✅ |
| config 字节 | — | **181983** |

**完整 banner 原文**：

```
Linux version 5.10.237-Fuutao-Qn_miao-gc5e20589c (moon-source@c5-cose-moon-core-source-merge)
(Android (on r584948) clang version 22.0.0, LLD 22.0.0 (r584948-release 2d65e4108))
#0 SMP PREEMPT Thu Feb 20 00:00:00 CST 2025
```

### 🔴 一个必须说清的边界

**此前「内核记录缺失」是当时的盘点结果。本轮从既有 boot 备份新提取了配置、符号和 notes。
不追写成当时就已存在。**
（`../host-regmap-identity/STOCK-BOOT-IDENTITY.md` 记录的缺失是那一轮的如实盘点，本轮补上了。）

---

## 3. 内核指令窗口模型：15 / 15 PASS

```bash
cd <新目录>/kernel
python3 emulate-read-window.py
```

| 项 | 值 |
|---|---|
| 脚本来源 | `logs/regmap-trim-review-20261116/emulate-read-window.py`，**逐字节复制**（`cmp` IDENTICAL） |
| 复制前后 SHA256 | `29d120cbd1679e552151c17274bcff88b89ccbf93772c75db959a801a290b724` **未变** |
| **真实 rc** | **`0`** |
| stderr | 空 |

**15 例原文（`kernel/emu.stdout.txt`）**：

```
PASS cold exact two-byte window
PASS warm exact window
PASS one complete line
PASS less than one line
PASS not enough for two lines
PASS zero count
PASS negative offset
PASS left read error
PASS right read error
PASS both read errors
PASS user copy fault retains offset
PASS allocation failure
PASS zero and ff are valid bytes
PASS misaligned seek skips partial line
PASS cold range metadata does not read registers
PASS 15 stock-kernel instruction cases
```

**未改脚本常量或输出数据。**

---

## 4. 修正版读取器：48 C 例 + 60 ELF 例

### 4.1 六项输入核验（依据 `review-inputs.sha256`）

| 文件 | 字节 | SHA256 | 核验 |
|---|---|---|---|
| `reader_core.h` | 2747 | `9caa46d2c31d2269…` | **OK** |
| `reader_core.c` | 6402 | `eb7b9a09dcece6e5…` | **OK** |
| `reader_start.c` | 3652 | `14f6b0a3edf8602e…` | **OK** |
| `test_reader.c` | 15089 | `490cd8cfc12415c1…` | **OK** |
| `test-reader-elf.py` | 10546 | `f70a54c1dc225b77…` | **OK** |
| `replay-checks.py` | 2959 | `4c86a11caaeab4b6…` | **OK** |

**6/6 全部 OK，0 失败。逐字节复制到新 `reader/` 后 `sha256sum -c` 再次 6/6 通过。**

### 4.2 replay 结果

```bash
cd <新目录>/reader
python3 replay-checks.py
```

**真实 `rc=0`**，stdout 原文：

```json
{
  "status": "PASS",
  "host_summary": "48 cases; 287 assertions passed, 0 failed",
  "elf_summary": "PASS 60 ARM64 ELF syscall cases; 0 failed",
  "reader_bytes": 4232,
  "reader_sha256": "63688efccd5728b0155a782eb299db979d6150b2096b4dacbc1e083429aa30e1"
}
```

| 项 | 预期 | 实测 |
|---|---|---|
| C cases | 48 | **48** ✅ |
| C assertions | 287 | **287** ✅ |
| C failed | 0 | **0** ✅ |
| ELF cases | 60 | **60** ✅ |
| ELF failed | 0 | **0** ✅ |
| ELF 字节 | 4232 | **4232** ✅ |
| **ELF SHA256** | `63688efccd5728b0155a782eb299db979d6150b2096b4dacbc1e083429aa30e1` | **同** ✅ |

### 4.3 ASan/UBSan 与静态检查

| 检查 | 结果 |
|---|---|
| `AddressSanitizer` / `UndefinedBehaviorSanitizer` / `LeakSanitizer` / `runtime error` 检索 | **0 命中**（replay stdout 与 stderr 都是 0） |
| `file` | `ELF 64-bit LSB executable, ARM aarch64, statically linked` |
| 类型 / 架构 | **`EXEC` / `AArch64`** |
| 入口点 | **`0x2109fc`** |
| **`INTERP` 段** | **0** |

**`reader/reviewed-elf-readelf.txt` 保存了完整 readelf 输出。**

---

## 5. 原件负对照：恰好 3 例 FAIL

```bash
python3 test-reader-elf.py \
  --reader /home/lizi/IOS/hakios-mtk/logs/regmap-reader-audit-20261116/original/regmap-reader.aarch64 \
  --output <新目录>/original-negative.json
```

| 项 | 值 |
|---|---|
| 原件 SHA256 | `1d96bdd5d3d87eeaf3b92c124126e6934292c6dc8994f136e75bc82432af1c7f`（4104 B） |
| **真实 rc** | **`1`** ✅（预期 1） |
| cases | **60** |
| **failed** | **3** ✅ |
| 其余 57 例 | **PASS** |

### 三个 FAIL 的身份（与审定完全一致）

| case | exit | 说明 |
|---|---|---|
| **`name close EIO`** | **0** | **错误退出 0** |
| **`range close EIO`** | **0** | **错误退出 0** |
| **`registers close EIO`** | **0** | **错误退出 0** |

**`error` 字段原文均为 `('exit', 0, False)` —— 即退出码是 0，而模型期望非 0。**

**🔴 这是保留的原件缺陷证据，不是修正版测试失败。**
**没有删除这些用例，也没有把它们改判 PASS。**

---

## 6. 既有产物未变（实测）

| 项 | 值 |
|---|---|
| **8 项 mode4 生产输入** | **不一致 0 项** |
| **`cac498da` host kernel** | **`cac498da96406cc4…`** ✅ |
| 709 kernel | `c17cf881745511a3…` ✅ |
| `hakios-kernel` | `67f9f933117f9bc2…` ✅ |
| wrapper | `95247bb011c136d2…` ✅ |
| 709 镜像 | `f4ff9732081e44a3…` ✅ |
| 原 DSH 候选目录 | **未改**（原 ELF 仍 `1d96bdd5…`） |

---

## 7. 主机绝对路径与重跑入口

### 7.1 本轮复核目录

```
/home/lizi/IOS/hakios-mtk/logs/verify-reader-host-02-20261116-164256/
├── REGMAP-READ-AUDITED.md                    5449
├── READER-AUDIT.md                           2814
├── kernel/
│   ├── stock-Image                       42201364   ← 42MB，留本地不入仓
│   ├── stock-kallsyms.tsv                 6414359   ← 140592 条符号，留本地
│   ├── stock-config-1.txt                  181983
│   ├── stock-kernel-identity.json            2177
│   ├── stock-kallsyms-layout.json             291
│   ├── emulate-read-window.py               12336
│   ├── read-window-emulation.json            4097
│   ├── emu.stdout.txt / emu.stderr.txt / emu.rc
│   └── extract.stdout.txt / extract.stderr.txt / extract.rc
├── reader/
│   ├── reader_core.h/.c, reader_start.c, test_reader.c
│   ├── test-reader-elf.py, replay-checks.py
│   ├── regmap-reader.aarch64                 4232   ← 修正版
│   ├── replay-result.json / replay.stdout.txt / replay.rc
│   ├── reviewed-elf-readelf.txt / syscall-trace.txt
│   └── replay-*.log
├── original-negative.json                  118993
└── negative.rc / negative.stdout.txt / negative.stderr.txt
```

### 7.2 重跑入口

```bash
# 1) 内核提取
cd /home/lizi/IOS/hakios-mtk
python3 logs/regmap-trim-review-20261116/extract-stock-kernel.py \
  --boot /home/lizi/IOS/hakios-mtk/backup/boot_a_stock_real.img --out <目录>/kernel

# 2) 内核指令窗口模型（须在含 stock-Image 的目录内跑）
cd <目录>/kernel && python3 emulate-read-window.py

# 3) 修正版读取器复核
cd <目录>/reader && python3 replay-checks.py

# 4) 原件负对照
cd <目录>/reader && python3 test-reader-elf.py \
  --reader /home/lizi/IOS/hakios-mtk/logs/regmap-reader-audit-20261116/original/regmap-reader.aarch64 \
  --output <目录>/original-negative.json
```

### 7.3 留本地不入仓的大文件

| 文件 | 字节 | 说明 |
|---|---|---|
| `kernel/stock-Image` | **42201364** | 42 MB；hash 已记录 |
| `kernel/stock-kallsyms.tsv` | **6414359** | 140592 条完整符号表 |
| `reader/test_reader` | 1829208 | ASan 构建的可执行件 |

**它们的来源、hash 与重跑入口都在本文档。**

---

## 8. 判据核对

| 判据 | 结果 |
|---|---|
| 提取符合预期 | ✅ 42201364 B / `12d255cd` / `76cc81c2` / 140592 / 550 |
| **15 例内核指令窗口模型符合** | ✅ **15/15 PASS** |
| **48 例 C 测试符合** | ✅ **287 assertions / 0 failed** |
| **60 例最终 ELF syscall 模型符合** | ✅ **0 failed** |
| **三层不混计成硬件结果** | ✅ 本文档分节陈述，未合并为单一结论 |
| 修正版输入→命令→产物链一致 | ✅ 6 项 hash → 命令 → `63688efc` |
| **原件负对照恰好三个 close 错误** | ✅ `name`/`range`/`registers` close EIO |
| 原目录及历史记录保留 | ✅ **`logs/regmap-reader-prep-20261116-161918/` 未改** |
| 模式 4 生产输入与既有产物未变 | ✅ 见 §6 |
| 未在设备执行任何步骤 | ✅ **全程主机** |

---

## 9. 边界

| 未验证 | 说明 |
|---|---|
| **当前 kernel/module 身份与运行实例一致性** | **未验证** |
| **播放时真实 trim 值** | **未取** —— 仍是 `XX` 之外的未知 |
| **电气结果** | **未测** |
| **工具是否已在手机可用** | **未声明** —— 只是"已通过主机验证的候选" |
| **本轮是否设备放行** | **🔴 没有宣布放行** |

**模型边界（引自审定）**：分配器、锁、元数据谓词、格式化、用户拷贝及 `_regmap_read`；
**没有执行 PMIF/SPMI 控制器**。这**不是硬件总线事务测量，也不是原子双寄存器快照**。

**未来设备 CMD 由 Codex 另定，等用户在手机旁再安排；
届时先核验身份与元数据，再考虑正常播放中的定点取证。**
