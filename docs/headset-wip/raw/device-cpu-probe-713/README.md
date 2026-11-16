# DEVICE-713：单次只读 CPU 快照试验

> 2026-11-17 00:57，DSH。**DSH 独占设备。一次试验，已恢复原厂。**
> **此后仅主机分析，无任何 adb/fastboot 或设备操作。**

---

## 四个必需标签

| 标签 | 结果 |
|---|---|
| **IMAGE_IDENTITY** | ✅ **PASS** |
| **CPU_SNAPSHOT_CAPTURE** | ✅ **完成**（96 行原始 LUT 完整；result 全为 3，**不强求 0**）|
| **NORMAL_RETURN** | ⚠️ **仅指 XNU 侧 `reason=0x0`**（见 §5，Android 侧单列，**不写自动恢复 PASS**）|
| **STOCK_RECOVERY** | ✅ **完成**（全分区 MD5 回到 `a63b2e35`）|

---

## 1. IMAGE_IDENTITY

| 项 | 值 |
|---|---|
| boot.img | **22994944 B** |
| SHA256 | **`127e48e9c7089e9845bfd0a3565ccacfb1b80acd3d84776b3946fa8a702b165c`** |
| MD5 | **`e91d1caf11378f98553cc51f6a771a46`** |
| 内核 | 15321336 B / `a80231ed…` |
| loader | 48768 B / `78611b0f…` |
| payload | 19515800 B / `871508dc…` |

**刷前复查与批准值逐项一致。**

---

## 2. 参数串（539 B 无 NUL）

`arguments713.json` 的 `full_arguments`，539 B、含 NUL 540、余量 68。

关键项：`mtk_cpu_probe=1`、`mali_trial=713`、`headset=0`、`cpus=4`、`loader_return_after=150`。

**HLOG 里确认出现只读 banner 与三个 domain 汇总行。**

---

## 3. CPU_SNAPSHOT_CAPTURE（实际数据）

### 3.1 三个 domain 的汇总行（HLOG 原文）

```
[19.544336]: mt6895-cpu: domain 0 base=0x11bc10 enable=0x201/0x201 status=0x3/0x3 request=0/0  result=3 rows=0 request_khz=0
[19.627736]: mt6895-cpu: domain 1 base=0x11bd30 enable=0/0         status=0x3/0x3 request=19/19 result=3 rows=0 request_khz=0
[19.742119]: mt6895-cpu: domain 2 base=0x11be50 enable=0/0         status=0x3/0x3 request=28/28 result=3 rows=0 request_khz=0
```

| 域 | base | **enable** | **status** | **request** | **result** |
|---|---|---|---|---|---|
| 0 | `0x11bc10` | **`0x201`** | **`0x3`** | **0** | **3** |
| 1 | `0x11bd30` | **`0`** | **`0x3`** | **19** | **3** |
| 2 | `0x11be50` | **`0`** | **`0x3`** | **28** | **3** |

**`result=3` = `CPU_NOT_ENABLED`。**

### 3.2 🔴 但这里必须分清两件事

**`result=3` 是代码路径的判定，不是物理结论。**

| 事实 | 我的表述 |
|---|---|
| `enable` 读回 **`0x201` / `0` / `0`** | **原始观察** |
| 代码要求 `enable == 1` 才继续 | **驱动逻辑** |
| `0x201 != 1`，所以 domain0 判 `NOT_ENABLED` | **代码结果** |
| **不能推出「UFS/CPU 电源域被物理禁用」** | **我不做这个推断** |

**`0x201` 这个值本身也不等于「已使能」或「未使能」** ——
`enable` 寄存器的 `bit0` 之外那些位（`0x200`）含义我没有依据解释。

### 3.3 96 行原始 LUT 完整

| 域 | 行数 | 范围 | 唯一 |
|---|---|---|---|
| domain0 | **32** | 0..31 | **32** |
| domain1 | **32** | 0..31 | **32** |
| domain2 | **32** | 0..31 | **32** |
| **合计** | **96** | — | **全部唯一** |

**每行含 `first/second` 两个采样值。**

### 3.4 🔴 最重要的发现：LUT 与 Android 实测完全一致

**按 `低12位 × 1000` 解码：**

**domain0（前 25 项，与 Android `policy0` 实测逐项相同）：**

```
2000 1900 1800 1700 1600 1500 1400 1300 1250 1200 1150 1100 1050
1000 950 900 850 800 750 700 650 600 550 500 450
```

**→ 与我从 Android 侧读到的 `scaling_available_frequencies` **完全一致**（25 项）。**

**domain1（32 项，与 Android `policy4` 实测完全一致）：**

```
2850 2750 2650 2550 2450 2400 2350 2300 2250 2200 2150 2100 2050
2000 1900 1800 1700 1600 1500 1400 1300 1200 1100 1000 900 800
700 600 500 400 300 200
```

**domain2（29 项有效，与 Android `policy7` 一致）：**

```
2850 … 500 500 0 0
```

**🔴 这是本轮最强的一条证据：LUT 读到的是真实频率表，解码方式（低 12 位 × 1000 kHz）被实机证实。**

**尽管 `result=3`（因为 `enable` 检查未过），原始数据仍然完全可用 —— 这正是 Codex 指出的「`NOT_READY` 时仍打印原始 LUT」在实际中的体现。**

---

## 4. 双快照一致性

**三个 domain 的每一次 `row` 行都是 `raw=X/X`（两份快照相同）。**

**汇总行里 `enable`、`status`、`request` 也都是 `X/X` 形式。**

**→ 没有出现 `CPU_UNSTABLE`（result 不是 2）。**

---

## 5. NORMAL_RETURN（严格限定）

**HLOG 侧：**

| 项 | 值 |
|---|---|
| magic | `0x474f4c48`（HLOG）|
| version | 1 |
| **len** | **519004**（<= 4194304）|
| **reason** | **`0x0`** |
| header sum | `0x2865061` |
| **完整 sum(body)** | **`0x2865061`** |
| **相等** | ✅ **True** |

**→ `NORMAL_RETURN` 仅指 XNU 正常内核返回。**

### 5.1 🔴 Android 侧：不写「自动恢复 PASS」

**事实记录：**

| 时刻 | 观察 |
|---|---|
| 恢复尝试 1 | `adb reboot bootloader` **超时**（`rc=None`，实测 `rc=124`），**fastboot 未出现** |
| 恢复尝试 2 | 重试仍超时（`rc=124`）|
| **关键** | **`adb devices` 仍能列出设备** |
| 用户手动进 fb | **00:53:46 检测到 fastboot** |
| **00:53:53 → 00:53:55** | **stock 刷写完成**（9 秒）|

**🔴 我先前写过「设备仍在内核态」—— 那是推断，现删除。**

**`adbdevices` 能看到设备不等于「只在内核态」**（recovery、fastbootd 也可能响应 adb）。
**我只记录观察到的事实：adbd 曾可用、`reboot bootloader` 超时、手动进 fb 后恢复成功。**

**→ 不声称自动恢复通过。**

---

## 6. STOCK_RECOVERY

**stock 身份复查：67108864 B / `c5f8e7bd…` / md5 `a63b2e35…` ✅**

**刷写：`native rc=0`、2.18 秒、`Sending/Writing OKAY`。只刷 `boot_a`，未换 slot。**

| 项 | 值 |
|---|---|
| slot | **`_a`** ✅ |
| boot_completed | **1** ✅ |
| SELinux | **`Enforcing`** ✅ |
| **boot_a size** | **67108864** ✅ |
| **boot_a 全分区 MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** ✅ |
| **新 boot_id** | **`c8acf0e0-ec30-4b5a-906b-fe29e1a74a9d`** |
| SurfaceFlinger | 正常响应 ✅ |

---

## 7. 原始件与哈希

| 文件 | 大小 | SHA256 |
|---|---|---|
| **`raw/trial713-logbook.bin`** | **4198400** | **`310c2f0a8084019c8b9f72d877b7d7f0670d7eda0b952d9dd991c8c73f073213`** |
| **`raw/trial713-logbook-body.bin`** | **519004** | **`7fbecaa318c1e1496c23f1fedfe8d9e4d66ed816f141faa0587efb92345b87d8`** |
| `raw/pre-logbook.bin` | 8388608 | `dcf1585e2bcdcc0066f64a6a74fd1add10b812f1c9e39900900ea227b50ba797` |

**pre-logbook 里旧记录是 `mali_trial=712`，不含 713 证据 —— 非重复试验。**

---

## 8. 失败与未确定性

| # | 项 |
|---|---|
| **1** | **`result` 三个域全是 3（`CPU_NOT_ENABLED`）** —— 原因是 `enable` 读回 `0x201`/`0`/`0` 而非 `1`。**这是代码判定，不是物理禁用结论。** |
| **2** | **`0x201` 里 `bit0` 之外的位（`0x200`）含义未知** —— 我没有依据解释。 |
| **3** | **`status` 三个域都是 `0x3`** —— 但 `enable` 检查先失败，所以 `status` 的判定没走到。**`0x3` 是否代表真实就绪，未证实。** |
| **4** | **`request` 值 0/19/28 未解读** —— 它们落在各自 LUT 范围内（domain1 有 32 项、domain2 有 29 项有效），但我不断言代表当前频率。 |
| **5** | **`recovery` 段第一次尝试失败** —— `adb reboot bootloader` 超时，原因未知。 |
| **6** | **未做任何频率/电压写入** —— 本轮纯读。 |

---

## 9. 独立复核引用

| 文件 | 位置 |
|---|---|
| `review-package.py` | `P/logs/mt6895-cpu-baseline-20261116/review-package.py`（8065 B）|
| `independent-kernel-review.json` | 同目录（430 B，PASS）|
| `arguments713.json` | 同目录（1706 B）|
| `production-before.json` | 同目录（1263 B）|
| loader 独立重编 | Codex 复核：逐字节相同 `78611b0f…` |
| 生产 8 项 | Codex 复核：未变 |

---

## 10. 边界

**本轮之后仅主机分析，不再有任何 adb/fastboot 或设备操作。**

**未做**：频率写入、电压写入、UFS 相关操作、自动加试、watchdog/slot/SELinux 改动。

**未把 `result=3` 推成物理禁用，未把 `request` 推成实际频率。**
