# DEVICE-715：UFS 只读探针单次实机试验

> 2026-11-17 13:03，DSH。**DSH 独占设备。一次试验，已恢复原厂。**

**🔴 结果只说明「flags/控制器字可观察」，未执行 SCSI，未建立队列所有权。**

---

## 四个必需标签（独立记录）

| 标签 | 结果 |
|---|---|
| **IMAGE_IDENTITY** | ✅ **PASS** |
| **UFS_SNAPSHOT_CAPTURE** | ✅ **完成**（`FLAGS_OBSERVED(0)`，9 对全一致，valid/attempted 均 `0x1ff`）|
| **NORMAL_RETURN** | ✅ **`reason=0x0`**，完整 sum 匹配 |
| **STOCK_RECOVERY** | ✅ **完成**（全分区 MD5 回到 `a63b2e35`），**本次自动恢复成功** |

---

## 1. IMAGE_IDENTITY

| 项 | 大小 | SHA256 |
|---|---|---|
| **boot.img** | **22994944 B** | **`65bd797dce99a4f29d898c7055a25fcce207e1b3642109daae0ad217f653c994`** |
| loader.bin | 46504 B | `c3d9c30a0c14c3e6c644fbf499fc189819b818c03080fe2beee18590cf7eafea` |
| payload.bin | 19515784 B | `c7d73935a1338a3e75cba6d01da536e7352c0a73dd2a51c18ae94239b1fe97c3` |
| 嵌套 kernel | 15321328 B | `ecdc9194fbcc635a63d8da6be7c5f4ef6a1c6bc7f3c1b715c9373cca9fe7da38` |

**boot MD5 `dced496c752ae0bec3de806b94a81615`。**

**刷前我独立复算了三个文件与 stock，全部与批准值一致。**

---

## 2. 参数串

**`runtime_args_bytes = 229`**（独立解析器读出的值）。

**HLOG 里确认出现只读 banner 与**恰好一条** `base=0x112b0000` 汇总行。**

---

## 3. UFS_SNAPSHOT_CAPTURE（实测）

### 3.1 HLOG 里的探针输出原文

```
[18.420313]: mt6895-ufs: read-only probe; flag samples do not establish IO readiness
[18.420547]: mt6895-ufs: base=0x112b0000 result=FLAGS_OBSERVED(0) valid=0x1ff/0x1ff attempted=0x1ff/0x1ff sdb_slots=32
[18.421293]: mt6895-ufs: flags-only dp=1 utrlrdy=1 ucrdy=1 enabled=1
```

### 3.2 九对原始值（独立解析器输出，我交叉核对过）

| 偏移 | first | second | 一致 |
|---|---|---|---|
| **`0x00`** CAP | `0x3587071f` | `0x3587071f` | ✅ |
| **`0x08`** VER | `0x200` | `0x200` | ✅ |
| **`0x20`** IS | `0x15` | `0x15` | ✅ |
| **`0x30`** HCS | `0x10f` | `0x10f` | ✅ |
| **`0x34`** HCE | `0x1` | `0x1` | ✅ |
| **`0x50`** UTRLBA | `0x51266400` | `0x51266400` | ✅ |
| **`0x54`** UTRLBAU | `0x0` | `0x0` | ✅ |
| **`0x58`** UTRLDBR | `0x0` | `0x0` | ✅ |
| **`0x60`** UTRLRSR | `0x1` | `0x1` | ✅ |

**九对全部一致，无 `CHANGING_WORDS`。**

### 3.3 掩码与结果

| 项 | 值 |
|---|---|
| `valid` | **`[511, 511]`**（= `0x1ff/0x1ff`）|
| `attempted` | **`[511, 511]`** |
| **`capture_complete`** | **True** |
| **`result`** | **`FLAGS_OBSERVED`（0）** |
| `sdb_slots` | **32**（CAP[4:0]+1）|

### 3.4 标志位解读（仅报告位）

**`flags-only dp=1 utrlrdy=1 ucrdy=1 enabled=1`**

| 位 | 值 | 我**不**据此断言 |
|---|---|---|
| `HCE` bit0 | **1** | 不推断时钟/电源状态 |
| `HCS` `DP` | **1** | 不推断 SCSI 能成功 |
| `HCS` `UTRLRDY` | **1** | **不推断队列所有权** |
| `HCS` `UCRDY` | **1** | 本探针里 report-only |

**🔴 这些只是标志位的采样值。不是存储 IO 能力、不是 DMA 有效性、不是 link mode 或 gear/lane。**

### 3.5 值得记下的采样值（**只报字段，不推断语义**）

**`UTRLBA = 0x51266400`（`UTRLBAU = 0`），`UTRLDBR = 0`，`UTRLRSR = 1`。**

**🔴 按字段逐项只写采样值：**

| 偏移 | 字段 | 采样值 | **我只说这些** |
|---|---|---|---|
| `0x50`/`0x54` | `UTRLBA`/`UTRLBAU` | **`0x51266400` / `0`** | 这两个采样位就是这个值 |
| **`0x58`** | **`UTRLDBR`（门铃）** | **采样位全 0** | **采样位全 0，仅此** |
| **`0x60`** | **`UTRLRSR`** | **run/stop 字段 = 1** | **该字段为 1，仅此** |

**🔴 已撤回的两处强推断：**

| 我原来写的 | 为什么撤回 |
|---|---|
| ~~「门铃为 0（**无在途请求**）」~~ | **门铃字段采样为 0，不等于「没有在途请求」** —— 我没有查任何队列状态 |
| ~~「链表**处于运行态**」~~ | **`UTRLRSR` 的 run/stop 字段是 1，不等于控制器真的在跑那条链表** |

**这可能与 loader 移交的状态有关，但**我没有追指针、没有发命令、没有验证该地址的内容**。**

**按 CMD：只记录这些字，不跟随指针。**

---

## 4. NORMAL_RETURN

| 项 | 值 |
|---|---|
| magic | `0x474f4c48`（HLOG）|
| version | 1 |
| **len** | **494290**（<= 4194304）✅ |
| **reason** | **`0x0`** |
| header sum | `0x26b7a7c` |
| **完整 sum(body)** | **`0x26b7a7c`** |
| **相等** | ✅ |

**`full_sum_verified = True`（独立解析器）。**

**→ 这是 XNU 正常内核返回，与探针结果无关，也与后续 Android 健康无关。**

### 4.1 失败扫描

**HLOG 里未发现 panic / data abort / shutdown-stop 错误。**

---

## 5. STOCK_RECOVERY（本次自动恢复成功）

### 5.1 恢复过程

| 步 | 结果 |
|---|---|
| `adb reboot bootloader` | **native rc=0，未超时**（`timed_out=False`）|
| fastboot 出现 | **+3 秒** |
| stock 身份复查 | 67108864 B / `c5f8e7bd…` / md5 `a63b2e35…` ✅ |
| **stock flash** | **native rc=0**，2.31 秒，`Sending/Writing OKAY` |
| `fastboot reboot` | **native rc=0** |

### 5.2 恢复后核验

| 项 | 值 |
|---|---|
| slot | **`_a`** ✅ |
| boot_completed | **1** ✅ |
| SELinux | **`Enforcing`** ✅ |
| **boot_a size** | **67108864** ✅ |
| **boot_a 全分区 MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** ✅ |
| **新 boot_id** | **`11aead4e-9595-4574-8e6a-6b229e287480`** |
| SurfaceFlinger | 正常响应 ✅ |
| Android 就绪 | **40 秒**（**轮询区间内的观测值，起点为 `fastboot reboot` 之后我开始轮询的时刻；不能当作 reboot 的精确耗时，也不能用来比较快慢**）|

**🔴 本次**未**需要用户手动兜底 —— **`adb reboot bootloader` 与 `fastboot reboot` 都没超时。**

### 5.3 🔴 这不代表 713 的问题被修复

**713 那次 `adb reboot bootloader` 超时、需用户手动进 fb。**

**715 这次成功了，但：**

| 不能推出 | 理由 |
|---|---|
| 「713 的问题解决了」 | 单次成功不构成因果证明 |
| 「GPU 路径关闭修好了返回」 | 本轮没有做对照实验 |
| 「自动恢复可靠了」 | **这是一次观察，不是稳定性结论** |

**正确表述：715 这次自动恢复成功，是一次观察记录；不能判定它与 713 事件成因的关系。**

---

## 6. 独立复核（Codex 的解析器）

**`review/review-device-715.py`（7646 B）+ `test-review-device-715.py`（6112 B）+ `device-parser-tests-715.json`（1085 B，35 项合成证据解析测试）。**

**Codex 已独立运行：PASS，9 对全一致，`FLAGS_OBSERVED(0)`，valid/attempted 均 `1ff/1ff`，`sdb_slots=32`，reason0/fullsum `26b7a7c` 通过。**

**我也独立跑了一遍：**

| 项 | 值 |
|---|---|
| parser rc | **0** |
| stderr | **0 B** |
| `NORMAL_RETURN` | **PASS** |
| `full_sum_verified` | **True** |
| `capture_complete` | **True** |

**与 Codex 结果完全一致。**

---

## 6.1 归档的独立复核与结果文件

| 文件 | 大小 | 内容 |
|---|---|---|
| **`RESULT-TRIAL715.md`** | **5990 B** / `2e6e3ce455ac824b…` | **Codex 的完整结果复核** |
| **`review/independent-recovery-review-715.json`** | **1373 B** / `67a9e362c2d34c96…` | **原厂恢复独立复核** |
| `review/review-device-715.py` | 7646 B | HLOG 审计解析器 |
| `review/test-review-device-715.py` | 6112 B | 解析器测试 |
| `review/device-parser-tests-715.json` | 1085 B | **35 项仅主机合成证据解析测试** |
| `review/independent-review-715-output.json` | 1974 B | Codex 解析器输出的独立结果 |
| **`reboot.meta`** | **53 B** | **B6 reboot 的真实时刻** |

**🔴 35 例是**仅主机**的解析测试，不是设备测试。**

### 6.2 🔴 时间线：轮询起点 **不是** reboot 起点

**`reboot.meta` 原文：**

```
rc=0
timed_out=False
time=2026-11-17T12:58:15.735208
```

**我核算的时间差：**

| 区间 | 时长 |
|---|---|
| reboot → 抓 HLOG | **206.6 s** |
| reboot → 开始恢复 | **214.4 s** |
| **抓 HLOG → 开始恢复** | **7.8 s** |

**轮询在我 B6 reboot 命令返回后立即启动，所以 `+195.3s` 是「轮询起点 → adb 可用」的观测值。**

**轮询起点晚于 `reboot.meta` 记录的时刻（差值为 host 侧 setup 时间）。**

**→ `+195.3s` **不能**当作 reboot → adb 的精确耗时，只能作为轮询观测记录。**

---

## 7. 原始件与哈希

| 文件 | 大小 | SHA256 |
|---|---|---|
| **`raw/trial715-logbook.bin`** | **4198400** | **`ff2fca7f0b63ac5d14e7d56124d9619621d430d1ef5de8891ad64d77c3c32e14`** |
| **`raw/trial715-logbook-body.bin`** | **494290** | **`ce8fbf536fc7ebdebb66407e871a294bbf1a6af2f9863ed7229c6b4333787098`** |

**pre-logbook（8388608 B / `02617b08129c353ff1ea8cc33d468bd3be50587afba2f6dd030760ee761e26c1`）留本地，
其旧记录是 `mali_trial=713` 且不含 715 证据 —— 非重复试验。**

---

## 8. 🔴 未验证项（严格边界）

| # | 未验证 |
|---|---|
| 1 | **未执行任何 SCSI 命令** —— 连 `TEST UNIT READY` 都没有 |
| 2 | **未建立队列所有权** —— `UTRLBA` 是读出来的值，我没写它、没追指针 |
| 3 | **未验证存储 IO 能成功** |
| 4 | 未建立 link speed / gear / lane |
| 5 | **未测量物理 GPU 状态** |
| 6 | 未做 DMA / 文件系统 / 挂载 |
| 7 | `UCRDY` 在本探针里是 report-only，不是 vendor 通则 |
| 8 | **没有 XNU UFS 存储驱动** —— 这是有限快照探针 |

**`FLAGS_OBSERVED` 只表示「九个字稳定且三个标志位为 1」，仅此而已。**

---

## 9. 边界

**未做**：SCSI/UIC 命令、队列接管、任意 MMIO/syscon 读、PMIC getter、GPUEB、
watchdog、OEM expdb、slot 变更、自动加试。

**未改**：全局 ignore 规则、旧 BUILD/产物、生产仓。
