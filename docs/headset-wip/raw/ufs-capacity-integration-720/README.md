# 720 集成与构建审查

> 2026-11-17 20:45，DSH。**仅主机。无设备查询、无打包、无刷写。**
> CMD 身份：`385f2862aaf42cc571d362c737050aabe0cd2a20b5ab9466262dd2cae89793ea`

---

## 0. 结果

| 项 | 值 |
|---|---|
| **46 冻结输入** | **46/46** ✅ |
| **5220 源输入** | **5220/5220** ✅ |
| **变更集** | **4 改 / 6 增 / 0 删** ✅ |
| **86 例解析器** | **`{"status": "PASS", "cases": 86}`** ✅ |
| **make native rc** | **2**（既有 kext symbol-set 允许项）|
| **judge 判定** | **内核已生成**（详见 §6）|
| **内核** | **`179efc781d17b41721a2d9cd5e83fb23d9b279f44c253aff27ddc00604bca19f`** / **15338680 B** |
| **具体阻塞** | **无** |

---

## 1. Root 两处测试语义更正（已追加到 v2 文档）

**Root 独立复核在**它自己的私有源码里**改了两处宿主测试。运行时 C 一个字节没变，冻结的 v2 驱动仍是 `03d08064`。**

| 项 | 我原先 | 问题 | Root 更正 |
|---|---|---|---|
| `stop_at_ring` | 只留 DBR 置位 | **实际只是轮询超时**，没真 `us_stop` | **真的调用 `us_stop`** |
| `partial_response` | `byte7=0x01`，名为「部分响应」 | **其实是完整响应加非零状态** | 改名 `SCSI_CHECK_CONDITION`，status **2** |

**原测试 `86d57d1b` 与修正版 `85000799` 两份都记录，原始不静默替换。**

### 1.1 我原先说过头的地方

**真正的部分读取（回调返回 false）**由引擎故障模型覆盖，不是本适配器的声明**。
MMIO 故障在真实 provider 上不会变成 false。**

**→ v2 的测试**不声称**覆盖真实部分读取。**

### 1.2 Root 首次编译失败

**Root 第一次 reviewer 编译**漏了 `US_HOST_TEST`**，rc1，源码与失败单独保留；之后
`independent-adapter-review720-v2` 才成功。**那次初始失败**不计入通过**。**

---

## 2. 86 例解析器（我独立跑）

**私有目录复制三分文件后运行：**

```
{"status": "PASS", "cases": 86}
```

**rc=0，stderr 0 B。**

### 2.1 三项关键判据我逐一核了

| 判据 | 源码位置 | 强制方式 |
|---|---|---|
| **未接受的查询不得产出几何** | `:138` | `need(geometry is None,'unaccepted query exposes accepted geometry')` |
| **几何不同仍可观测** | `:131` | `ANDROID_COMPARISON='MATCH' if matches else 'DIFFERENT'` |
| **饱和不得变无介质** | `:130` | `CAPACITY_OBSERVATION='UNRESOLVED' if decoded==2 else ...` |
| **审计必须在 HLOG EOF** | `:54` | `need(text.endswith(audit[0]+'\n'), ...)` |

### 2.2 解码状态由 raw 推出，不能自报

```c
last=int.from_bytes(raw[:4],'big');block=int.from_bytes(raw[4:],'big')
expected_decode=2 if last==0xffffffff else 3 if block==0 else 0
need(decoded==expected_decode,'decode status contradicts raw payload')
```

**→ 解析器**从 raw 八字节重新推导**解码状态，与日志自报值比对。这是独立校验。**

**Root 还喂了 11 条真实 HOST 日志加合成审计 —— 那是**格式测试**，无 720 运行时证据。**

---

## 3. 构建前置验证

### 3.1 BUILD 不存在 ✅

**构建前 `X/BUILD` 不存在。**

### 3.2 46 冻结输入 ✅

**全部匹配。**

### 3.3 5220 源输入 ✅

| 项 | 值 |
|---|---|
| 清单 sha256 | **`2cad662bfa76f8e2`** ✅ |
| 条目数 | **5220** ✅ |
| 逐项校验 | **5220/5220** ✅ |
| 构建后复查（BUILD 之外） | **5220/5220 未变** ✅ |

### 3.4 4 改 / 6 增 / 0 删 ✅

| 类 | 文件 |
|---|---|
| **改 4** | `bsd/conf/files`、`bsd/dev/hakios.input.c`、`bsd/sys/hakios.input.h`、`osfmk/arm64/loader_return.c` |
| **增 6** | `bsd/dev/hakios.mt6895_ufs_capacity.c`、`bsd/dev/support/` ×3、`bsd/dev/ufs_capacity/` ×2 |

**头文件命名空间与候选的相对 include 一致 ✅**

---

## 4. 构建

```
make native rc=2   耗时 72s   开始 2026-11-17T20:34:27+08:00
stdout 124342 B    stderr 76025 B
```

### 4.1 🔴 rc=2 的真实原因

```
exported name not in import list: _uat_get_desc
config/Makefile:108: .../config/Private.symbolset 错误 1
```

**→ 是 **kext symbol-set 允许项**，与 719 的 rc=2 同类。**
**不是编译错误，不是链接错误。**

### 4.2 产物

**内核**15338680 B** / **`179efc78...`**

**→ 内核**确实生成**，只是 config 阶段的符号集检查未过。**

---

## 5. 符号验证

```
fffffff007963c34 S _hakios_mt6895_ufs_capacity_init
fffffff007963b80 S _hakios_mt6895_ufs_capacity_quiesce
```

### 5.1 旧对象全部缺席 ✅

| 符号 | 命中 |
|---|---|
| `_mtk_ufs_read` | **0** ✅ |
| `_mtk_ufs_probe` | **0** ✅ |
| `_mtk_cpu_probe` | **0** ✅ |
| `_hakios_mt6895_ufs_service_init` | **0** ✅ |

### 5.2 日志契约字符串全部进入内核 ✅

**`single RC10`、`guard magic=`、`opened baseline_verified=`、`result=`、
`raw valid=`、`tail complete=` —— 六条都在。**

### 5.3 一处我须澄清

**`strings` 命中 `mtk_ufs_service` 一次 —— 那是**我自己的竞争检查字符串**，
不是旧对象。路径在 `hakios.mt6895_ufs_capacity.c:208`。**

---

## 6. 🔴 关于 judge

**CMD 给的是 `KERNEL=X/BUILD/.../mach.development.bcm2837` 供 judge。
但 `xnu-build.sh` **没有 judge 子命令** —— 我执行 `./xnu-build.sh judge` 得到
`make: *** 没有规则可制作目标“judge”`，rc=2。**

**719 的 `judge native rc=0` 实际是**人工判定**：
「`the log shows a built kernel; only the kext symbol-set checks failed`」。**

**→ 我按同样口径判定：**

| 项 | 值 |
|---|---|
| **make native rc** | **2** |
| **判定** | **构建内核成功，仅 kext symbol-set 检查失败** |
| **内核产物** | **存在且被 sha256 记录** |

**🔴 我不声称 make 成功 —— rc=2 如实记录为 2。**

---

## 7. 与 719 的对照

| 项 | 719 | **720** |
|---|---|---|
| 内核 | `5a85ac7f` / 15339768 B | **`179efc78` / 15338680 B** |
| make rc | 2 | **2** |
| 变更集 | — | **4 改 6 增 0 删** |

**注：`60bd6b19` 是 **718 的内核**，我先前守卫列表里写混了，此处更正。**

---

## 8. 守卫复查

| 项 | 值 |
|---|---|
| stock boot_a | **md5 `a63b2e35`** ✅ |
| 719 镜像 | **`fd6e19ac`** ✅ |
| 719 树内核 | **`5a85ac7f`** ✅ 未见变化 |

---

## 9. 边界

**未做**：设备查询、打包、刷写、候选/引擎/codec 重跑、
stash/reset/checkout/clean/gc/prune、生产推送、GPUEB WIP staging、
raw GPT/GUID/镜像/pre-log 上传。

**🔴 编译进来的字符串**只是包含关系**，不能推出硬件几何或整盘能力。**
**🔴 全局独占仍是前提，未被「已知路径缺席」证明。**

---

## 10. 等待

**打包审查与单独的 ONE DEVICE720 释放。**
