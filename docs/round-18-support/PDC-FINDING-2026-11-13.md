# PDC 发现（2026-11-13 23:35）：驱动写了 17 个使能位，但 sc9 和 st6 读回 0

**手机零访问。全部来自 692 日志 + vendor 源码 + 我们的驱动源码。**

---

## 1. vendor 的做法（`gpufreq_mt6895.c`）

```
/* set PDCv2 register when power on, let GPU DDK control MTCMOS itself */
__gpufreq_pdc_control(POWER_ON);          // :802
```

`__gpufreq_pdc_control()`（`:1488`）对 10 个 shader cluster 寄存器
（`0x13FBF400 + n*0x18`）和若干 core-group 寄存器写 **bit0 = rg_scN_active_pwrctl_en**。

**vendor 的话很明确：开了这个使能之后，MTCMOS 由 GPU 自己（即固件）控制，不由软件控制。**
这就是为什么 `GPUFREQ_PDCv2_ENABLE=1` 时 vendor 只手工开 MFG1，MFG2..12 交给 PDC。

---

## 2. 我们的驱动也想做同样的事 —— `pdc_enable()`

`hakios.mali610.c:1855-1878`：

```c
for (i = 0; i < MALI_PDC_CORES; i++) {          /* 10 个 */
    at = 0x400 + i*0x18;
    reg_wr(gMfgcfg, at,     reg_rd(at)   | (1u<<0));
    reg_wr(gMfgcfg, at+4,   reg_rd(at+4) | (1u<<31));
}
for (i = 0; i < 7; i++)                          /* kPdcGroups */
    reg_wr(gMfgcfg, kPdcGroups[i], reg_rd(...) | (1u<<0));
```

692 日志第 41 行：`pdc enabled on 17 registers` —— **17 = 10 + 7，全部写了。**

---

## 3. 但回读说没生效

692 日志第 42 行：

```
mali: pdc: sc0 0x2000001 sc9 0 cg 0x1 st0 0x2000001 st4 0x2000001 st6 0
```

| 寄存器 | 地址 | 读回 | bit0 |
|---|---|---|---|
| sc0 | `0x400` | `0x2000001` | **1 ✅** |
| sc9 | `0x4d8` | `0x0` | **0 ❌** |
| cg | `0x100` | `0x1` | 1 ✅ |
| st0 | `0x120` | `0x2000001` | 1 ✅ |
| st4 | `0xc0` | `0x2000001` | 1 ✅ |
| st6 | `0x1c0` | `0x0` | **0 ❌** |

**写进去了，读不回来。** 这一条以前没人注意过。

---

## 4. 三种解释，都不便宜

**(a) sc9 / st6 在这个 part 上不存在。**
`MALI_PDC_CORES = 10` 是我们自己定的（注释说 "rg_sc0..rg_sc9, and no further"）。
如果 MT6895 的 G610 只有 6 个 cluster，那 sc6..sc9 都不该写。但 sc6..sc8 没在报告里，
**无法判断**（`pdc_report()` 只打 sc0 和 sc9）。

**(b) 这两个寄存器在某个没上电的域里。**
`sc9` 在 `0x4d8`、`st6` 在 `0x1c0`，都在 `mfgcfg`（`0x13FBF000`）这个块内。
如果 mfgcfg 的某些页需要另一个电源域，就会出现"能写不能读"。

**(c) 写入被硬件忽略（只读或保留位）。**
`0x2000001` = bit25 + bit0。bit0 是使能，**bit25 是什么我们不知道** ——
vendor 的注释只提到 bit0（en）和 bit31（rsv），**没提 bit25**。
sc0/st0/st4 都读到 `0x2000000` 这个位，说明它是硬件自己置的，不是我们写的。

---

## 5. 为什么这条重要

如果 (a) 成立，那我们的 `pdc_enable()` 写了不存在的寄存器 —— 无害但也无用，
且掩盖了"到底该写哪几个"这个问题。

如果 (b)/(c) 成立，那 **PDC 使能可能从来没有真正生效过**，而 MCU 固件要拉起 shader
MTCMOS 就依赖这个使能。这会是"固件跑了但什么也没发生"的一个候选解释。

**关键测试（零成本）**：把 `pdc_report()` 扩展成打印全部 10 个 sc 和 7 个 group 的回读，
而不是只打 4 个。现在的报告是抽样，抽样恰好跳过了 sc1..sc8，所以看不出是"只有 sc9 坏"
还是"sc6..sc9 全坏"。

---

## 6. 我要纠正的一个说法

我先前写"7 of twelve domains answered"，把它当成"只有 7 个 MFG 域上电成功"。
但那 7 个是 **MFG1..MFG5 + 另外两个**，而且日志里 MFG6 返回 `0x4000000d`（bit30 置位）、
MFG7..MFG12 都是 `0xd`。

**在 PDC 模型下这是预期的**：vendor 自己也只手工开 MFG1，其余交给 PDC/固件。
所以"MFG7..MFG12 没上电"**不是缺陷**，是设计。我先前把它当线索是错的。

真正的问题变成了：**PDC 使能有没有真的打开**。而 692 的回读显示至少两个没有。


---

## 7. 撤回一条作用域错误（23:55）

我先前写：

> vendor 只在 `shader_present == GPU_SHADER_PRESENT_6` 时检查 MFG0-12 全上电，
> 我们的 `PRESENT` 是 `0x130013` 不等于任何 `GPU_SHADER_PRESENT_n`。

**这条的前提是错的。** 我先核了 `#if` 嵌套：

```
3381: #if GPUFREQ_CHECK_MTCMOS_PWR_STATUS      [depth 1]
3392: #endif                                   [depth 0]
3394: #if !GPUFREQ_PDCv2_ENABLE                [depth 1]
3472:   #if GPUFREQ_CHECK_MTCMOS_PWR_STATUS    [depth 2]
3485:   #endif                                 [depth 1]
3486: #endif /* GPUFREQ_PDCv2_ENABLE */        [depth 0]
```

而 `GPUFREQ_PDCv2_ENABLE = 1`（`gpufreq_mt6895.h:70`）。

→ **3472-3485 那段整体被编译掉**（它在 `!GPUFREQ_PDCv2_ENABLE` 里）。
→ 所以 vendor 在 PDCv2 模式下**根本不检查** MFG0-12 是否全上电。
→ 我说"vendor 期望 12 个域全上电"是错的。**它不期望，它压根不问。**

同时我又核对了两件事：

1. **`0x130013` 是 `SHADER_READY(0x140)` 的正常读数，不是 `SHADER_PRESENT`。**
   Android 真机的 `hist_boot.txt:228` 就是 `r: reg ...15800140 val 00130013`，
   而且它出现在 **AUTO 之后**（rec136 AUTO → rec228 READY），和 `round-6.md` §8 一致。
   → "shader ready = 0x130013" 是**固件跑起来的结果**。我们读到 `0x140 = 0`，
   说明固件没跑到那一步。**这是症状，不是原因。**

2. **`g_shader_present` 是编译期常量**，来自 segment_id 的 switch（`gpufreq_mt6895.c:4416-4431`），
   MT6895 无论 segment 都取 `GPU_SHADER_PRESENT_6`。**它不读硬件。**
   → 我拿它去和硬件 `0x140` 比较，是把编译期配置和运行时状态混在一起了。

## 8. 那么 PDC 那条还剩下什么

**剩下的是日志事实本身，这个没错：**

- trial 602：写了 17 个 PDC 使能寄存器 → `0xF3C` 仍是 `0x6`（只有 MFG0/MFG1）→ `1 of twelve`。
- trial 692：`sc9 = 0`、`st6 = 0`（17 个里至少 2 个读回没置上）。

**这两个是"写入没生效"的直接观测，与 vendor 期望什么无关。**

但现在也**不能**说这就是根因，因为：
- PDC 在 PDCv2 模式下是"让 GPU 自己控制 MTCMOS"，它本来就不保证软件写完之后域就起来；
- 域起来的前提是**固件在跑**并主动请求。**这是循环依赖，我没解开。**

→ **PDC 这条应该降级为"待查的观测异常"，不是根因候选。**
我也不该拿它去推"固件跑了但什么也没发生"。

## 9. 本轮我犯的第 11 处错误，性质归类

这处和附录 A 的第一类（位域/常量算错）不同，也和第二类（局部扫描当全局）不同。
它属于**第四类：读代码时没有核对条件编译作用域**。

→ 新规程条款（写进 COLLAB-PROTOCOL.md）：
**引用 vendor 源码的某个条件分支之前，必须先跑一遍 `#if/#endif` 配对，
确认该行实际处于哪个分支的哪个嵌套层。**


---

## 10. 结案（00:10）：sc6..sc9 读 0 是因为这台机器没有那四个 cluster

**找到完整读数了**，就在 trial 686/687 的日志里（`pdc raw ... sc0=... sc9=...`）：

```
trial 686 before the start request:
  sc0=0x2000001 sc1=0x2000001 sc2=0x2000001 sc3=0x2000001 sc4=0x2000001 sc5=0x2000001
  sc6=0 sc7=0 sc8=0 sc9=0

trial 686 after AUTO (before DISABLE):
  完全相同
```

**六个置上了 bit0，四个连 bit25 都没有。AUTO 前后没有任何变化。**

再看 `0x2000001`：

| 位 | 含义 | 来源 |
|---|---|---|
| bit0 | `rg_scN_active_pwrctl_en` | vendor 注释明确 |
| bit25 | **未知** | vendor 注释里没有 |

sc6..sc9 读 **0**，连 bit25 都是零。

### 结论

`gpufreq_mt6895.h:99`：**`SHADER_CORE_NUM = 6`**。

→ MT6895 的 G610 是 **6 核**，对应 **sc0..sc5 六个 cluster 寄存器**。
→ `sc6..sc9` 是驱动按 `MALI_PDC_CORES = 10`（我们自己定的）硬写进去的，
   **这台机器根本没有那四个 cluster**，所以读回 0。
→ `st6 = 0` 同理（`kPdcGroups` 里 `0x1c0` 那个 group 对应更上面的档位）。

**这不是缺陷，是写得比硬件需要的多。** `pdc_enable()` 写 10 个而硬件只有 6 个，
多写的四个读回 0 完全正常。

### 这条线到此关闭

- 不是根因
- 不是"PDC 使能没生效"
- 不需要下一轮验证
- 唯一可改进的是 `MALI_PDC_CORES` 应该按 `SHADER_CORE_NUM` 取，而不是硬编码 10
  （但这是整洁性问题，不影响行为）

### 那么 `0xF3C = 0x6` 呢

`0x6` = bits 1,2 = **MFG0 + MFG1**。而 `MFG2..MFG12` 由 PDC（即 GPU 固件）按需拉起。

固件没跑 → 不会有任何域被拉起 → `0xF3C` 就停在 `0x6`。

→ **`0xF3C = 0x6` 是固件没跑的又一个症状，不是原因。**
和 `SHADER_READY = 0`、`version word = 哨兵` 是同一类观测。

### 本轮累计

- 11 处错误（四类，每类一条强制条款，见 `COLLAB-PROTOCOL.md` 附录 A/B/C）
- 关闭 3 条伪线索：skipprot、fwlog 通道、PDC 使能
- 确认 1 件事实：`0x01010000` = 接口版本 1.1.0（两个独立来源）
- 净结论：**所有已知观测都是"固件没执行"的症状，没有一条是原因。**

---

## 11. 再次自我更正（00:20）：vendor 也写 sc9

我 §10 说"`sc6..sc9` 是驱动硬写进去的、这台机器没有那四个 cluster"。**这条又过头了。**

6.1sol 指出：`gpufreq_mt6895.c:1539` 的注释明确写着

```c
/* MFG_ACTIVE_POWER_CON_54 0x13FBF4D8 [0] rg_sc9_active_pwrctl_en = 1'b1 */
val = readl(g_mfg_top_base + 0x4D8);
val |= (1UL << 0);
writel(val, g_mfg_top_base + 0x4D8);
```

**vendor 自己也写 `0x4D8`（sc9）。** 所以：

- "`MALI_PDC_CORES = 10` 是我们自己硬编的"——**错**，vendor 也写满 10 个。
- "这台机器没有那四个 cluster"——**未证明**。`SHADER_CORE_NUM = 6` 是**着色器核**数量，
  cluster 是另一层（一个 cluster 可能含多个核）。两者不能直接相等。

**我能确定的只有一件事：写入后读回 0。**
不能推出"寄存器不存在"，也不能推出"写得比硬件需要的多"。

## 12. 最终状态：PDC 这条是"未解释的观测"，不是"已关闭"

| 我确定 | 我不确定 |
|---|---|
| 写 10 个 sc + 7 个 group，共 17 个 | sc6..sc9 为什么读 0 |
| sc0..sc5 读 `0x2000001`（bit0+bit25） | bit25 是什么 |
| sc6..sc9 读 `0` | 是寄存器不存在、还是写入被忽略、还是需要某个前置条件 |
| AUTO 前后完全不变 | 固件本应改变它们吗 |
| vendor 也写满这 10 个 | 为什么 vendor 写完不检查（`#if !PDCv2` 那段被编译掉） |

→ **正确表述**：PDC 使能写入了 17 个寄存器，其中 4 个读回 0，原因未知。
**不进入下一轮试验**，但也不记为"已关闭/已排除"，记为"待查"。

## 13. 这是第 12 处错误

我这一轮在同一类错误上栽了三次：
- 把"缓存可读"当"写入可见"（§4d → §4e）
- 把"读回 0"当"寄存器不存在"（§10 → §11）
- 把"编译期常量"当"硬件读数"（§7）

**共同点：把"没观测到 X"当成"X 不存在"。** 这条应该进附录 A 的第三类，
作为最具体的例子。
