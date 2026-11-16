# PREP-REGMAP-READER-HOST-01：定点 regmap 读取器（仅主机实现与验证）

> 2026-11-16 16:22，DSH。**全程主机。
> 未 adb/fastboot、未查设备、未挂 debugfs、未播放、未重启、未刷写、未推工具到手机。**

**🔴 这不是"工具已可上设备"的声明。** 工具只按合同实现并做过主机验证，
**当前内核与两个模块和运行实例的一致性仍待以后单独核验。**

---

## 0. 三句话

**一、合同逐条满足：固定路径、六叶文件、强制顺序、`registers` 只一次
`pread64(count=18, offset=86526)`、无重试无补读、`00`/`ff` 合法、`XX` 失败。**

**二、主机测试 191 项断言全过、`rc=0`、ASan/UBSan 零诊断；
syscall 轨迹 337 行，14 个元数据失败用例全部确认未打开 `registers`。**

**三、ARM64 静态 ELF 交叉编译成功（4104 B），AArch64、无 `INTERP`、
无动态段、无未定义符号、入口 `0x210978` = `_start`。未执行、未推设备。**

---

## 1. 文件与身份

| 文件 | 字节 | SHA256 |
|---|---|---|
| `reader_core.h` | **2703** | `05f0f0751b6da00d652720825fdc7d268e4eb948f8043a41d15c6053b821656f` |
| `reader_core.c` | **6054** | `a4d793dd05763c897b88aebae1b0d20aa6cdd0bea6438df309e6ca191207f1d6` |
| `reader_start.c` | **3652** | `14f6b0a3edf8602e9c85ceeae465d90d12e84a1134427e0d8ef79017510fc1d7` |
| `test_reader.c` | **13190** | — |
| **`regmap-reader.aarch64`** | **4104** | **`1d96bdd5d3d87eeaf3b92c124126e6934292c6dc8994f136e75bc82432af1c7f`** |

**主机原始目录**：`/home/lizi/IOS/hakios-mtk/logs/regmap-reader-prep-20261116-161918/`

---

## 2. 合同常量（一字不改）

```c
READER_MAP_DIR   "/sys/kernel/debug/regmap/0-05/"
READER_F_NAME    ... "name"
READER_F_RANGE   ... "range"
READER_F_CACHE_ONLY / _BYPASS / _DIRTY
READER_F_REGISTERS ... "registers"

READER_META_MAX     64
READER_PREAD_OFFSET 86526L      /* 9614 * 9 = 0x258e * 9 */
READER_PREAD_COUNT  18
```

**没有用户可传的路径、地址或长度。** `reader_run()` 不接受任何参数来控制目标。

### 顺序由控制流强制

```
name → range → cache_only → cache_bypass → cache_dirty → registers → stdout
```

**任一步失败即返回，不试别的 map、不做全量 dump、不猜偏移。**

---

## 3. 关键设计点

### 3.1 `registers` 只经一次 `pread64`

```c
n = ops->pread_once(fd, win, READER_PREAD_COUNT, READER_PREAD_OFFSET);
```

**没有 `read()`、没有 `fread`/`getline`、没有缓冲 stdio、没有循环补短读、
没有第二次 `pread`。** `n != 18` 时直接判 `R_REGS_COUNT` 失败。

### 3.2 三个 cache 叶只能靠 `-ENOENT` 证明不存在

```c
fd = ops->open_ro(path);
if (fd >= 0) { close; fail; }        /* 存在即失败 */
if (fd == -READER_E_ENOENT) return 0; /* 只有 ENOENT 才算不存在 */
fail;                                 /* EACCES/ENOTDIR/EINTR 都失败 */
```

### 3.3 `00`/`ff` 合法，`XX` 失败

```c
static int is_lower_hex(char c)
{ return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }
```

**`00` 与 `ff` 都通过；`XX` 不是 hex，失败。**

### 3.4 格式失败仍输出原文供取证，但退出码非 0

```c
if (window_format_ok(win, reason) != 0) {
	(void)ops->write_all(1, win, READER_PREAD_COUNT);  /* 原文留证 */
	return -1;                                          /* 但失败 */
}
```

---

## 4. 主机测试：191 项断言，0 失败

```bash
clang -std=c11 -O1 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  reader_core.c test_reader.c -o test_reader
```

| 项 | 结果 |
|---|---|
| 编译 | `rc=0`，`-Werror` 无警告 |
| **运行** | **`rc=0`** |
| 断言 | **191 passed, 0 failed** |
| **ASan/UBSan** | **零诊断**（grep 计数 0） |

### 覆盖的用例（40 个场景）

| 类别 | 用例 |
|---|---|
| **成功** | `00/ff`、`ff/00`、`1a/2b` |
| **name** | open ENOENT、open EACCES、缺换行、多一字节、错芯片 |
| **range** | open ENOENT、错范围、缺换行 |
| **cache 存在** | `cache_only`/`cache_bypass`/`cache_dirty` 各自存在 |
| **cache 非 ENOENT** | EACCES、ENOTDIR、EINTR |
| **registers open** | EACCES |
| **pread** | EINTR、EIO、短读 17、短读 1 |
| **格式** | 左 XX、右 XX、双 XX、错地址 `258d`/`2590`、错分隔符（`-`/`=`）、非 hex、大写 hex、坏行尾 |

### 每个 case 的公共断言

| 断言 | 说明 |
|---|---|
| `preads <= 1` | **任何路径最多一次 pread64** |
| `pread(len=18,off=86526)` | 那一次必须是固定 count/offset |
| 成功时 `reads == 2` | 只读 name 与 range |
| 失败时 `reads <= 2` | 不多读 |
| **元数据失败 → `regs_opens == 0`** | **不打开 registers** |
| `read(fd=105)` 计数 0 | **registers 从未被 read()** |

---

## 5. Syscall 轨迹（`syscall-trace.txt`，337 行）

### 成功用例的完整轨迹

```
case valid window 00/ff rc=0 reason=ok
  open(/sys/kernel/debug/regmap/0-05/name)
  read(fd=100,len=64)
  close(100)
  open(/sys/kernel/debug/regmap/0-05/range)
  read(fd=101,len=64)
  close(101)
  open(/sys/kernel/debug/regmap/0-05/cache_only)
  open(/sys/kernel/debug/regmap/0-05/cache_bypass)
  open(/sys/kernel/debug/regmap/0-05/cache_dirty)
  open(/sys/kernel/debug/regmap/0-05/registers)
  pread(len=18,off=86526)
  close(100)
  write(fd=1,len=18,"258e: 00.258f: ff.")
```

**顺序完全符合合同；`write` 恰好 18 字节；`pread` 恰好一次。**

### 14 个元数据失败用例：全部未打开 `registers`

```
OK   name open ENOENT        OK   range open ENOENT       OK   cache_only present
OK   name open EACCES        OK   range wrong extent      OK   cache_bypass present
OK   name missing newline    OK   range missing newline   OK   cache_dirty present
OK   name trailing byte                                  OK   cache_only EACCES
OK   name wrong chip                                     OK   cache_bypass ENOTDIR
                                                          OK   cache_dirty EINTR
```

---

## 6. ARM64 静态 ELF

```bash
clang --target=aarch64-linux-gnu -std=c11 -O2 -Wall -Wextra -Werror \
  -ffreestanding -nostdlib -static -fno-stack-protector -fno-pic \
  -Wl,-e,_start -Wl,--build-id=none -fuse-ld=lld \
  reader_core.c reader_start.c -o regmap-reader.aarch64
```

| 检查 | 结果 |
|---|---|
| `rc` | **0** |
| `file` | `ELF 64-bit LSB executable, ARM aarch64, statically linked` |
| 类型 | **`EXEC`** |
| 架构 | **`AArch64`** |
| **入口点** | **`0x210978` = `_start`** ✅ |
| **`INTERP` 段** | **0（无）** ✅ |
| **动态段** | **`There is no dynamic section`** ✅ |
| **未定义符号** | **无** ✅ |
| 大小 | **4104 B** |

**系统调用号（合同给定，与 `include/uapi/asm-generic/unistd.h` 一致）**：
`openat=56`、`close=57`、`read=63`、`write=64`、`pread64=67`、`exit=93`。

**🔴 未在设备执行、未推送、未打包任何镜像。**

---

## 7. 判据核对

| 判据 | 结果 |
|---|---|
| 合同逐条满足 | ✅ 见 §2/§3 |
| syscall 轨迹符合固定地址 / 一次 pread 边界 | ✅ **337 行轨迹，成功用例 11 步完全吻合** |
| ASan/UBSan 无诊断 | ✅ **grep 计数 0** |
| 错误一律非 0 | ✅ 所有失败 case 都断言 `rc != 0` |
| 无重试或全量 dump | ✅ `preads <= 1`，无全量路径 |
| 正常 stdout 仅 18 字节原文 | ✅ `write(fd=1,len=18)` |
| `00`/`ff` 不被随意判坏 | ✅ 两个成功用例 |
| ARM64 ELF 有输入→命令→产物 hash 链 | ✅ 见 §1/§6 |
| 没有设备执行记录 | ✅ **全程未碰设备** |

---

## 8. 实现过程中我修掉的问题（如实记录）

**没有靠删掉失败用例来满足合同。** 修的是夹具与断言的错误：

| # | 问题 | 修法 |
|---|---|---|
| 1 | `require_absent()` 是死代码，且无法区分 ENOENT 与 EACCES | **删掉**，只用 `require_enoent()` |
| 2 | 测试缺 `<stdarg.h>` | 补上 |
| 3 | `run_case()` 是未使用的残留 | **删掉** |
| 4 | `fake_read()` 对所有 fd 返回同样内容 | 按 fd 100..104 映射到实际叶子 |
| 5 | 断言 `reads == 3` 错误（cache 叶不该被读） | 改为**成功时 `reads == 2`** |
| 6 | `trace_count("read(")` 误匹配 `pread(` | 改为 `"read(fd="` |
| 7 | 短读用例用 `-1`（负值走错误分支） | 改为 **`17`**（正数短读） |
| 8 | 分隔符用例破坏了行尾，先报 `R_FMT_NL` | 保持行尾合法，只改 `[4]` |
| 9 | 轨迹只在失败时打印 | **改为每个 case 都写入 `syscall-trace.txt`** |

**9 个问题全部是夹具/断言错误，核心逻辑 `reader_core.c` 未因此改动判据或放行条件。**

---

## 9. 边界（语义只引用审定文字，我不新增）

| 项 | 状态 |
|---|---|
| 工具是否可上设备 | **🔴 未声明。** 等 Codex 审计 |
| 当前内核身份 | **未核验** |
| 两个模块与运行实例一致性 | **未核验**（`../host-regmap-identity/` 已标未验证） |
| 「校准」语义 | **不涉及** —— 工具不解码、不生成 boot-args、不把捕获 byte 当已校准 |
| 「读取副作用」语义 | **不涉及** —— 归 Codex |
| 实际硬件验证 | **无** —— 底层寄存器读、格式化、锁、用户拷贝都是外部模型 |

**这 18 字节窗口也不是原子双寄存器快照，未来须配合播放与 `HP_ANA_TRIM` 状态取证
（此句引自合同 §"为什么是这个窗口"）。**

---

## 10. 构建工具链

| 项 | 值 |
|---|---|
| clang | **Ubuntu clang 21.1.8** |
| ld.lld | **Ubuntu LLD 21.1.8** |
| 新装 SDK | **无** |
| 系统工具链/分析器配置改动 | **无** |
