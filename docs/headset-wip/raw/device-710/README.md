# DEVICE-710：一次 profile4 试验与原厂恢复

> 2026-11-16 18:47–18:57，DSH。**已按 CMD 执行一次试验并强制恢复原厂。**
> 序列号在脚本中以 `HAKIOS_SERIAL` 传递，本文档不写序列号字面值。

**🔴 本报告不宣称出声、电气静音、DC offset 质量、实测时钟波形或完整音频驱动。**

---

## 0. 五个标签分开陈述

| 标签 | 结果 |
|---|---|
| **IDENTITY** | ✅ **PASS** |
| **UP_READBACK** | ✅ **PASS**（38 对全 verified） |
| **DOWN_RESTORE** | ✅ **PASS**（38 对 + HP off shape + 36 bytes） |
| **NORMAL_RETURN** | ✅ **reason=0x0** |
| **STOCK_RECOVERY** | ✅ **PASS**（MD5 回到 `a63b2e35…`） |

---

## 1. 批准产物与实际刷写

| 项 | 值 |
|---|---|
| 镜像 | `trial710/revision2/boot.img` |
| 大小 | **22994944 B** |
| SHA256 | **`ca161c1bdb002540a2454e8222ef7306255e175108ead9fffe48d995aebe56b1`** |
| MD5 | `b016ef1708b46830e9f74787bb169815` |

**刷前复查 hash 一致后执行。**

```
fastboot -s $HAKIOS_SERIAL flash boot_a <revision2>
native rc = 0   elapsed = 0.94s
stderr: Sending 'boot_a' (22456 KB)  OKAY [ 0.604s]
        Writing 'boot_a'             OKAY [ 0.110s]
```
**→ FLASH SUCCESS**（唯一一次试验刷写，未重试）

**随后**：`fastboot -s $HAKIOS_SERIAL reboot` **native rc=0**。

---

## 2. Phase A 前置（全部通过）

| 项 | 值 |
|---|---|
| adb 连接 | `device` |
| slot | **`_a`** |
| boot_completed | **1** |
| **试验前 boot_id** | **`0ebf2d81-4822-47df-be3c-51916b1a7663`** ✅ 与期望一致 |
| SELinux | `Enforcing` |
| **boot_a 大小 / MD5** | **67108864 / `a63b2e35a7c2522cb24a6a10332b9bc8`** ✅ |
| **xnusys 大小** | **2685370368** |
| **减 8388608 =** | **2676981760 = `0x9f8f8000`** ✅ 精确匹配 |

### 旧 logbook 保存（未清区、未写 xnusys）

```
dd if=/dev/block/by-name/xnusys bs=4096 skip=653560 count=2048
native rc=0   8388608 B  sha256 418c6dd0860e694e7bb42e19ab05bdacac526f80c524a278661ef3a165d493d5
```
**header**：`HLOG` v1、len **524668**、reason **0x0**、sum `0x28ce7c0`

**旧记录身份**：`mali_trial=709`、**不含任何 trial710 证据**（`profile4` 0 次）→ **可继续，非重复试验**

---

## 3. Phase C 日志抓取

**adb 在重启后 200.4 秒恢复**（在 150s 链式返回窗口内），**未等 `boot_completed` 立即抓取**。

```
dd if=/dev/block/by-name/xnusys bs=4096 skip=653560 count=1025
native rc=0   4198400 B  sha256 347a3089c1e10217f84fa252ea6faf15f2e23f4bc2b3e906e3b1a6c4b76ad189
```

### HLOG header

| 字段 | 值 |
|---|---|
| magic | **`HLOG`**（`0x474f4c48`）✅ |
| version | **1** ✅ |
| **len** | **528685** ✅（`0 < len <= 4 MiB`） |
| **reason** | **`0x0`（正常 kernel 返回）** ✅ |
| sum（header 64 位） | **`0x291d3ce`** |

### checksum（🔴 更正：完整 64 位 sum，不是 8-bit）

| 项 | 值 |
|---|---|
| body 长度 | **528685 B** |
| **body SHA256** | **`7032ba03ee75a62f7ed71e3189ad5d0de6552cf96880671a896fd3c582f485fc`** |
| **完整 sum(body)** | **`0x291d3ce`（43111374）** |
| **header sum 64 位** | **`0x291d3ce`** |
| **精确相等** | ✅ **True** |

> **更正说明**：我此前写「8-bit additive checksum」不准确。
> 实际判据是 **sum of all body bytes 与 header 的 64 位 sum 字段精确相等**。
> 低 8 位 `0xce` 只是巧合匹配，**不是判据**。

---

## 4. IDENTITY

**完整 boot args 原文**（logbook 中）：

```
boot-args = rd=md0 -v cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1
fbwidth=1080 fbheight=2460 fb_disp=1 mali=5 mali_cold=1 mali_preas0=0 mali_low=0
mali_trace=1 mali_witness=0 mali_ptvendor=1 mali_scclk=1 mali_scupd=1 mali_sc26=0
mali_cacheflush=0 mali_resetonly=0 mali_cohnone=0 mali_hwissues=0 mali_stackpwron=0
mali_mcuwin=0 mali_mcuseq=0 mali_doorbell=0 mali_pdcsettle=0 mali_statussem=0
mali_assorder=1 mali_trial=710 mali_nopdc=1 headset=4 hp_trim_l=3 hp_trim_r=17
hp_fine_l=0 hp_fine_r=3 loader_return_after=150 hakios_kver=2.1...
```

| 检查 | 次数 | 结果 |
|---|---|---|
| `mali_trial=710` | **1** | ✅ 无重复 |
| `headset=4` | **1** | ✅ |
| `hp_trim_l=3` | **1** | ✅ |
| `hp_trim_r=17` | **1** | ✅ |
| `hp_fine_l=0` | **1** | ✅ |
| `hp_fine_r=3` | **1** | ✅ |

**profile4 trim 行**：

```
[19.606492]: headset: profile4 caller-supplied trim L=3/0 R=17/3; calibration quality unverified
```

**✅ 与实测捕获的 `258e=0x18` / `258f=0x8b` 推导一致。**

---

## 5. UP_READBACK

| 项 | 原文 |
|---|---|
| 分类 | `[20.039814]: headset: cable headphone without mic (AB 0, detector 2)` |
| 捕获 | `[20.040446]: headset: profile4 captured 38 field groups; muted SGEN, low-power 48k reference` |
| 配置验证 | `[20.064802]: headset: profile4 codec configuration verified; muted SGEN, physical output unmeasured` |

### 关键 on 字段（`read & check == expected` 且 `verified`）

| SID | reg | own | check | expected | read | keep | 结果 |
|---|---|---|---|---|---|---|---|
| 5 | `0x230c` | 66 | 66 | **00** | 00 | 81 | ✅ |
| 5 | `0x2388` | 01 | 01 | **01** | 01 | c0 | ✅ |
| 5 | `0x2389` | 01 | 01 | **01** | 01 | 00 | ✅ |
| 5 | `0x238e` | 01 | 01 | **01** | 01 | 1e | ✅ |
| 5 | `0x23c8` | fc | fc | **80** | 80 | 00 | ✅ |
| 5 | `0x2488` | ff | ff | **01** | 01 | 00 | ✅ |
| 5 | `0x2537` | 01 | 01 | **01** | 01 | 7e | ✅ |
| 5 | `0x2588` | ff | ff | **ff** | ff | 00 | ✅ |
| 5 | `0x2589` | ff | ff | **3a** | 3a | 00 | ✅ |
| 5 | `0x258a` | ff | ff | **03** | 03 | 00 | ✅ |
| 5 | **`0x258e`** | ff | ff | **18** | **18** | 00 | ✅ |
| 5 | **`0x258f`** | ff | ff | **8b** | **8b** | 00 | ✅ |
| 5 | `0x260b` | 1f | 1f | **1f** | 1f | 00 | ✅ |
| 5 | `0x260c` | 1f | 1f | **1f** | 1f | 00 | ✅ |
| **9** | **`0x079e`** | 04 | 04 | **04** | **bc** | fb | ✅ |

**SID9 `0x079e`**：`check=04` 只看 bit2，`read=bc` 的其余位由 `keep=fb` 保护，
`(0xbc & 0x04) == 0x04` ✅

**15/15 关键字段全部通过。**

---

## 6. DOWN_RESTORE

### 三阶段各 38 对，唯一性确认

| 阶段 | 对数 | 唯一 |
|---|---|---|
| `on` | **38** | ✅ |
| `off` | **38** | ✅ |
| `restored` | **38** | ✅ |
| **三阶段共同对** | **38** | ✅ |

### HP off shape

```
[169.397338]: headset: profile4 HP off shape 2588=00 258a=00 2589=30 25a2=00: verified
```

**核对**：`2588=00` ✅、`258a=00` ✅、`(2589&0f)=0`（`0x30 & 0x0f = 0`）✅、`(25a2&01)=0`（`0x00 & 0x01 = 0`）✅

### 逐对 masked 比较

**38 对 off/restored 的 `(read & check) == expected` 且 `verified` —— 失败 0 / 38。**

### 收尾

```
[169.719854]: headset: profile4 off and 38 original field groups restored: verified
[169.726847]: headset: restored 36 configuration bytes: ok
```

**ACCDET 恢复计数 = 36，与已知参考一致。**

---

## 7. NORMAL_RETURN

| 项 | 值 |
|---|---|
| **reason** | **`0x0`（正常 kernel 返回）** ✅ |
| len | 528685 |
| checksum | 完整 64 位 sum 精确相等 ✅ |

**无 headset 失败、无 PMIF 错误、无 failed/partial cleanup、无 worker stop timeout。**

---

## 8. STOCK_RECOVERY

| 项 | 值 |
|---|---|
| **stock 镜像** | `backup/boot_a_stock_real.img`，67108864 B，md5 **`a63b2e35…`** |
| **flash** | `fastboot -s $SERIAL flash boot_a <stock>` **native rc=0**，elapsed 2.16s |
| stderr | `Sending 'boot_a' (65536 KB) OKAY [1.720s]` / `Writing 'boot_a' OKAY [0.258s]` |
| **reboot** | **native rc=0** |
| Android 就绪 | **31 秒**，`boot_completed=1` |
| **slot** | **`_a`** ✅ |
| **boot_id（恢复后）** | **`8af533e7-e2fb-4841-a239-c224e40e030d`**（新启动） |
| SELinux | **`Enforcing`** ✅ |
| **boot_a 大小** | **67108864** ✅ |
| **boot_a MD5** | **`a63b2e35a7c2522cb24a6a10332b9bc8`** ✅ **回到原厂** |
| SurfaceFlinger | 正常响应 |

**✅ 原厂恢复完全成功。**

---

## 9. 一处记录局限（OBJ 指出，不追认）

**回 fastboot 之前我用 `timeout 10 adb reboot bootloader | tee`，其后的 `echo $?` 取的是 `tee` 的 rc，不是 `adb` 的 native rc。**

**该条 `rc=0` 只证明 `tee` 成功。**

**从 trial flash 起，全部改用 Python `subprocess` 独立保存 stdout/stderr/native rc/timeout 标志。**

---

## 10. 消息时间线

| 时刻 | 事件 |
|---|---|
| 18:48:55 | Phase A 前置核验 |
| 18:49:0x | 旧 logbook 保存（8388608 B） |
| 18:49:34 | `adb reboot bootloader` |
| 18:50:16 | fastboot 出现 |
| **18:50:30** | **trial flash（rc=0）** |
| **18:50:36** | **trial reboot（rc=0）** |
| 18:54:0x | **adb 恢复（+200.4s），立即抓 HLOG** |
| 18:56:28 | stock flash（rc=0） |
| 18:56:30 | stock reboot（rc=0） |
| 18:57:0x | Android ready（+31s），MD5 验证通过 |

**注**：trial reboot 与 adb 恢复之间用户手动回 fb 一次（自动返回链失效，非本轮参数问题）。

---

## 11. 边界

**未执行**：`spmi_access`、vendor codec dump、`tinymix` getter、
Audio HP ImpeDance getter、SELinux/权限修改、GPUEB `0x13c40000` 访问、
PMIC `014b` 写或 `0240` RMW、PMIF 控制器 reset/gating、OEM expdb、
watchdog、force/lazy unmount、杀系统服务、stash/reset/checkout/clean/gc/prune、
混杂树重建、生产仓 push。

**未宣称**：出声、电气静音、DC offset 质量、实测时钟振荡、完整音频驱动。

**历史结论不变**：705/707 FAIL、709 源链 `INSUFFICIENT`。
