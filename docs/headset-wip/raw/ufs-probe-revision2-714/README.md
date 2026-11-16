# UFS 只读探针 revision2（714）：独立审查与构建

> 2026-11-17 01:07，DSH。**仅主机。未打包、未刷写、无任何设备操作。**
> CMD 身份：`ef072a4c8596505e7de9e1e746e6c525313dc548d4c9e5f52303dd753cf3604d`

---

## 0. 结果

| 项 | 值 |
|---|---|
| **候选源** | **8138 B / `ce6aa5850fba8d318e44cdb18e2f3c9592820a12e27c1a1e3847cede730045f2`** ✅ 与批准值一致 |
| **ASan+UBSan** | **build rc=0、run rc=0、stderr 空**，`PASS 2324 cases` |
| **MemorySanitizer** | **build rc=0、run rc=0、stderr 空**，`PASS 2324 cases` |
| **ARM64 对象** | **rc=0**，13168 B / `5a725268923e76736e99bb0bc6c87c21859742ba99e3c3f21f16548110703237`，**与原件逐字节相同** |
| **5206 manifest** | **rc=0、5206 OK、0 FAILED** |
| **构建** | 76 秒，**make rc=2** |
| **judge** | **rc=0**，只豁免既有 symbolset |
| **新内核** | **15321328 B** |
| **SHA256** | **`ecdc9194fbcc635a63d8da6be7c5f4ef6a1c6bc7f3c1b715c9373cca9fe7da38`** |

---

## 1. 六处修正是否真解决了问题 —— 我逐条核对

### 1.1 输出初始化与有效位掩码 ✅

**修正前**：`first`/`second` 栈上未初始化，读失败时打印栈垃圾。

**修正后（第 53-56、84-88、97-99 行）：**

```
struct ufs_snapshot { uint32_t word[UFS_WORDS]; uint32_t attempted, valid; };
...
s->attempted |= 1u << i;          /* 尝试过 */
if (!io->read(...)) return false;
s->valid |= 1u << i;              /* 成功读到 */
...
*first = (struct ufs_snapshot){ 0 };   /* 两个都清零 */
*second = (struct ufs_snapshot){ 0 };
```

**格式化（第 133-142 行）：**

```
if (s->valid & (1u << row))        snprintf(text, size, "%#x", s->word[row]);
else snprintf(text, size, "%s", (s->attempted & (1u << row)) ? "FAILED" : "NOT_COLLECTED");
```

**→ 真解决了。失败的字显示 `FAILED`，没试的显示 `NOT_COLLECTED`，绝不冒充硬件零。**

**测试确实验证了这一点**：第 83 行注入 `*out = 0xdeadbeefu`，第 148 行断言
`strstr(log_text,"FAILED") && !strstr(log_text,"deadbeef")` —— **确认脏值不会被打印。**

### 1.2 全 1 / 无效响应 ✅

**修正后（第 109-115 行）：**

```
if (second->word[0] == UINT32_MAX || second->word[1] == 0 ||
    second->word[1] == UINT32_MAX || second->word[3] == UINT32_MAX ||
    second->word[4] == UINT32_MAX) {
    return UFS_UNTRUSTED_RESPONSE;
}
```

**→ 全 1 的 CAP/VER/HCS/HCE 与 VER==0 都判 `UNTRUSTED_RESPONSE`，不再进 `FLAGS_OBSERVED`。**

**注释还写明「Unknown nonzero versions are retained, without claiming version support」—— 未知版本保留不宣称。**

### 1.3  移出一致性检查 ✅

**修正后（第 103-108 行）：**

```
for (uint32_t i = 0; i < UFS_WORDS; i++) {
    /* Interrupt status changes autonomously; report both samples. */
    if (ufs_offset[i] != UFS_IS && first->word[i] != second->word[i]) {
        return UFS_UNSTABLE;
    }
}
```

**→ `IS` 仍打印两份采样，但不再参与稳定性判定。**

**且枚举名从 `UFS_UNSTABLE` 注释改为「non-IS words changed, **not a hardware-failure diagnosis**」。**

### 1.4 术语 ✅

- `HCS_DP` 注释：`device present`（**不是「命令响应成功」**）
- `HCS_UTRLRDY`：`transfer request list ready flag`（**只说「就绪标记」，不是「队列所有权」**）
- 第 128 行：`CAP[4:0]+1 is the legacy single-doorbell transfer-slot count`
- 输出字段名 `sdb_slots`（**single doorbell slots**）

**→ 原来的 task/UTRL 混淆已改正。**

### 1.5 就绪表述 ✅

**枚举 0 从 `UFS_OBSERVED` 改为 `UFS_FLAGS_OBSERVED`**，注释：

> `stable words with the three flags set; **not permission for IO**`

**第 124 行：** `A ready flag does not establish queue ownership or DMA validity.`

**第 9 行：** `A flag sample is not proof that the link or an inherited queue can execute IO.`

**→ 明确区分「标志位观察」与「可以发 IO」，这是我最满意的一处修正。**

### 1.6 测试消费格式化日志 ✅

**第 46-51 行**：`test_log` 现在是 `vsnprintf` 到缓冲区，**真收 varargs**。

**测试用 `strstr` 断言**（第 148、317 行），不只是计数。

**第 8 节引用 `mprotect` 只读页**（3 处）—— 真实适配器在只读页上跑过。

---

## 2. 复跑详情

### 2.1 ASan + UBSan

```bash
clang -std=c11 -Wall -Wextra -Werror -O1 -g -fno-omit-frame-pointer \
      -fsanitize=address,undefined -I. test-ufs.c -o <本目录>
```

| 项 | 值 |
|---|---|
| build rc | **0**（`-Werror` 零警告）|
| run rc | **0** |
| stderr | **0 B** |
| stdout | **`PASS 2324 cases; 18 failure positions; valid masks and formatted diagnostics; dynamic IS; read-only mappings`** |

### 2.2 MemorySanitizer

```bash
clang -std=c11 -Wall -Wextra -Werror -O1 -g -fno-omit-frame-pointer \
      -fsanitize=memory -fPIE -pie -I. test-ufs.c -o <本目录>
```

| 项 | 值 |
|---|---|
| build rc | **0** |
| run rc | **0** |
| stderr | **0 B** |
| stdout | 同上 **`PASS 2324`** |

**→ 2324 = 83 语义/失败/格式化场景 + 2241 MMIO 白名单/边界检查。**

### 2.3 ARM64（真实 XNU 头）

**用 `arm64-result.json` 的 194 元素 argv，只改输出路径。**

| 项 | 值 |
|---|---|
| native rc | **0** |
| stdout / stderr | 0 B / 0 B |
| 产物 | 13168 B |
| SHA256 | **`5a725268923e76736e99bb0bc6c87c21859742ba99e3c3f21f16548110703237`** |
| **与候选目录原件** | **逐字节相同** ✅ |

**→ 用真实 XNU 头编译成功，不是 shim。**

---

## 3. 隔离树核对

| 项 | 值 |
|---|---|
| 文件数 | **5206** |
| 构建前 BUILD | **不存在** ✅ |
| **5206 manifest** | **rc=0、5206 OK、0 FAILED** |
| 与 5205 基底差异 | **恰好 5 变 + 1 增**，与 `isolated-tree-preparation.json` 一致 |

**5 处变化**：`bsd/conf/files`、`hakios.input.c`、`hakios.mt6895_headset.c`、
`hakios.input.h`、`hakios.pmif.h`

**1 处新增**：`bsd/dev/hakios.mt6895_ufs.c`（与 candidate 逐字节相同 ✅）

**🔴 CMD 第 4 条要求的「不能嫁接 CPU 探针」已核实：**
- 树里**没有** `hakios.mt6895_cpufreq.c` ✅
- `conf/files` 与 `hakios.input.h` 里 `mt6895_cpufreq`/`mtk_cpu_probe` **各 0 次** ✅

**headset/pmif 保持 712 冻结值**（`8c0cf1c2` / `f4d9a964`）✅

---

## 4. 构建与 judge

| 项 | 值 |
|---|---|
| 命令 | `XNU_SRC=<绝对路径> ./xnu-build.sh -k -j8` |
| 耗时 | 01:05:46 → 01:07:02（**76 秒**）|
| **make native rc** | **2** |
| 源码编译错误 | **0** |
| `error` / `undefined` 出现 | **0 / 0** |
| **`CC hakios.mt6895_ufs.o`** | ✅ 编译成功 |
| **judge native rc** | **0** |
| 判词 | `only the kext symbol-set checks failed` |

**🔴 `make rc=2` 与 `judge rc=0` 分开记录，未混淆。**

---

## 5. 内核身份与探针插入证明

| 项 | 值 |
|---|---|
| 大小 | **15321328 B** |
| SHA256 | **`ecdc9194fbcc635a63d8da6be7c5f4ef6a1c6bc7f3c1b715c9373cca9fe7da38`** |

**八条 `mt6895-ufs` 消息全部编入：**

```
mt6895-ufs: read-only probe; flag samples do not establish IO readiness
mt6895-ufs: mtk_ufs_probe must be 0 or 1; no access
mt6895-ufs: map failed base=%#x size=%#x
mt6895-ufs: base=%#x result=%s(%u) valid=%#x/%#x attempted=%#x/%#x sdb_slots=%u
mt6895-ufs: raw %#04x=%s/%s
mt6895-ufs: flags-only dp=%u utrlrdy=%u ucrdy=%u enabled=%u
mt6895-ufs: flags=UNTRUSTED_RESPONSE
mt6895-ufs: flags=INCOMPLETE
```

**关键字符串**：`mtk_ufs_probe` 2 次、`mt6895-ufs` 8 次、
`FLAGS_OBSERVED` / `HCE_CLEAR` / `UNTRUSTED_RESPONSE` / `NOT_COLLECTED` 各至少 1 次。

**🔴 字符串存在只证明代码被编入，不是执行结果。**

---

## 6. 编译后复查与守卫

| 项 | 结果 |
|---|---|
| 编译前后文件数 | **5206 / 5206** |
| **排除 BUILD 后内容变化** | **0** ✅ |
| **新增** | **0** ✅ |

| 守卫 | 值 |
|---|---|
| 710 kernel | `cac498da96406cc4…` ✅ |
| 711 kernel | `24df55bfaf7fdd66…` ✅ |
| 712 kernel | `a2f6db113f599603…` ✅ |
| **713 kernel** | **`a80231ed9dc78a82…`** ✅ |
| shared `hakios-kernel` | `67f9f933117f9bc2…` ✅ |
| wrapper | `95247bb011c136d2…` ✅ |
| **生产树** | **无新增 ufs 驱动**，headset 仍 `8c0cf1c2` ✅ |

---

## 7. 我的审查结论

**六处修正是真解决，不是改措辞。** 特别是：

| 修正 | 为什么算真解决 |
|---|---|
| 有效位掩码 + `FAILED`/`NOT_COLLECTED` | 测试用 `deadbeef` 注入 + `strstr` 断言，**确认脏值不会冒充硬件值** |
| `UNTRUSTED_RESPONSE` | 全 1 的 CAP/HCS/HCE 与 VER==0 都进这个分支，**不再可能误判 OBSERVED** |
| `IS` 移出一致性 | `ufs_offset[i] != UFS_IS` 条件明确，**且枚举注释改成「不是硬件故障诊断」** |
| `FLAGS_OBSERVED` | 名字本身就否定了「可以发 IO」，**加三处注释反复强调** |

**没有发现阻塞性源码问题。**

---

## 8. 边界与未确定性

| # | 项 |
|---|---|
| 1 | **未打包、未刷写、无任何设备操作** |
| 2 | **内核 hash 只是构建产物标识，不代表验证过能跑** |
| 3 | **探针从未在真实硬件上执行过** —— 所有 2324 例都是主机模型 |
| 4 | **硬件 data abort 不会变成 `false`** —— 驱动注释承认这点，实际行为未知 |
| 5 | 未做 `ufscfg_ao`/`ufscfg_pdn` syscon 读、UIC 命令、SCSI、clock/power/reset/队列操作 |
| 6 | `loader713-macros.txt` 显示当前活动路径用 **LK 已有链表**（`UFS_USE_LK_LIST 1`、`UFS_HOST_RESET` 未定义），**`ufs_init` 的 stop/rebase/restart 备选路线不能当自动配方用** |

---

## 9. CPU713 结果归档说明

**`RESULT-TRIAL713.md`（3892 B）与 `offline-lut-interpretation.json`（2224 B）
已在 `docs/headset-wip/raw/device-cpu-probe-713/` 归档**（本次任务开始前完成，提交 `a59babf`）。

**所有限制与手动恢复记载均保留**，包括「Android 侧 reboot-to-fastboot 超时、用户手动进 fb、不是无人值守恢复通过」。
