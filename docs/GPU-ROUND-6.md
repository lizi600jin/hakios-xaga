# Round 6：实验 C（PDCv2 交接）—— 三次刷机，判定「PDC 未接管」

执行：DSH 主控（codex 双账号额度耗尽，按用户授权临时接管手机）
日期：2026-11-12 23:31–23:56（Asia/Shanghai）

---

## 0. 一句话结论

**PDCv2 路径走通了一半，但 PDC 硬件没有接管 mfg2..mfg12。**
`0xF3C` 仍然是 `0x6`（只有 mfg1），不是期望的 `0x3FFE`。
**F3C 判据明确否掉"只上 mfg1 + PDC 使能就能让 12 域全上电"这个假设。**

过程中抓到并修掉一个**真实的顺序 bug**（第一次刷机时 PDC 写在时钟门打开之前，17 笔 store 全部读回 0）。

---

## 1. 三次刷机（同一内核，只改 boot-args 与 pdcauto 的先后位置）

| trial | mali= | pdcauto | 镜像 md5 | 关键结果 |
|---|---|---:|---|---|
| 600 | 6（全流程） | 1 | `4ea1ae324122f5e25ff47da02af11aeb` | PDC 输出被 ring buffer 冲掉，**判定不可得** |
| 601 | 1（只上电） | 1 | `5d58bb519ea0262c67fb618a89429c1a` | PDC 写在时钟门之前 → 17 笔全读回 0 |
| 602 | 1（只上电） | 1（顺序已修） | `29cfe270954e30ad463cb5c83e9e6a13` | PDC 写入正常，但 `0xF3C` 仍 `0x6` |

驱动源码 sha256：602 为 `3cb6b5bb51b99a7526ae3ea6dd4e71c27e61ae0036eed24093d6fc9e5760e188`。

三次都正常返回 Android（`sys.boot_completed=1`，分别 23:35:34 / 23:41:12 / 23:56）。三次未用救砖包。

---

## 2. 抓到并修掉的 bug：PDC 写在时钟门打开之前

### 第一次（trial 601）的失败形态

```text
21.138828 mali: pdc auto: mfg1 up, 17 pdc words written
21.138836 mali: pdc: sc0 0 sc9 0 cg 0 st0 0 st4 0 st6 0     ← 七个数全是 0
21.342752 mali: pdc auto: 0xF3C 0x6 (want 0x3ffe) ...
```

`sc0 = 0` 而不是 `0x2000001`，说明**十七笔写全部没落地**。

### 原因

补丁脚本把 PDC 分支插在 `power_on()` 开头，而 `power_on()` 在 `mali_bring_up()` 里被调用于 **`clock_26m()` 之前**（:3808 vs :3849）。
`mfgcfg` 在 `0x13FBF000`，**物理上位于 mfg1 域内**，其 gate 是 `BG3D`，只有 `clock_26m()` 才会打开。

驱动自己在 :3853 就写着这句话：

> `mfgcfg's own gate is BG3D, and clock_26m() is what opens it. Written while the gate is shut, all seventeen stores read back as zero -- which is exactly what the first run of this did`

**补丁作者没有读到这条注释，把 PDC 放在了它警告过的位置。** 我按这条注释把 PDC 移到 `clock_26m()` 之后。

### 修正后（trial 602）

```text
21.309869 mali: pdc auto: 17 pdc words written (gate open)
21.309896 mali: pdc: sc0 0x2000001 sc9 0 cg 0x2000001 st0 0x2000001 st4 0x2000001 st6 0
21.511166 mali: pdc auto: 0xF3C 0x6 (want 0x3ffe) 0xF40 0x6 0xF34 0x1fc
21.511184 mali: pdc auto: 1 of twelve domains up via PDC
```

写入确实生效了（`sc0 = 0x2000001`、`cg = 0x2000001`、`st0/st4 = 0x2000001`），
但等 200 ms 之后 `0xF3C` 仍是 `0x6`。

---

## 3. 判据解读（这是本轮最有价值的部分）

### 3.1 `0xF3C = 0x6` 意味着什么

`0xF3C` 的 bit (n+2) 对应 MFGn，掩码 `0x3FFE`。
`0x6` = bit1|bit2 = **只有 bit2（mfg1）**，其余 11 个域全 0。

对比 `0xF34 = 0x1FC`（bits 2..8，7 个域）：
**两个寄存器对同一件事给出了不同的答案**，而且 `0xF3C` 比 `0xF34` 更悲观（1 vs 7）。

这说明 `0xF3C` 与 `0xF34` **不是同一份状态的两种视图**。可能：
- `0xF3C` 是 gpufreq 侧的镜像，只有在 gpufreq 自己的初始化路径跑过之后才会更新
- `0xF34` 是 scpsys 的实时状态，`0xF3C` 需要额外的同步动作

**证据边界**：我们只证明了 `0xF3C` 在裸机读出来是 `0x6`，**没有证明**它一定反映真实供电状态。
如果 `0xF3C` 是个需要"喂"的镜像寄存器，那用 `0x6` 否掉 PDC 就是错的。
这一点必须在下一次实验中排除（见 §5）。

### 3.2 `pdc: sc9 0` 与 `st6 0` 一直为 0

三次实验中 `sc9` 和 `st6` 恒为 0，其余五个从 0 变成 `0x2000001`。
说明 PDC 的 17 个寄存器里至少有 2 个**从来没被正确寻址或使能**。
`st6 = 0` 尤其可疑 —— 如果 `st6` 是第二层 shader 栈的使能位，它恒 0 正好对应 mfg8..mfg12 起不来。

### 3.3 PDC 使能后 mfg5/6/10/12 控制字的变化

```text
trial 601: after pdc: mfg5 0x1112 mfg6 0x1112 mfg10 0x1112 mfg12 0x1112
trial 602: after pdc: mfg5 0x1112 mfg6 0x1112 mfg10 0x1112 mfg12 0x1112
```

对比旧路径（round-5）的 `0x100d`：`0x1112` 与 `0x100d` 差异在 bit 1/4/8，
但 `sta` 字段（bit31:30）都是 0 → **仍然没有真正上电**，只是控制字里多了几个请求位。

---

## 4. 与之前 9 轮的对比（域上电从未变化）

| 轮次 | 路径 | `0xF34` | `0xF3C` | 域数 |
|---|---|---|---|---|
| 203/206/216/226/236/246/400/401/402 | 逐个真写 mfg2..mfg12 | `0x1FC` | `0x4` | 7/12 |
| 601 | PDC，但时钟门关着 | `0x1FC` | `0x6` | 7/12（PDC 写全失败） |
| 602 | PDC，时钟门已开 | `0x1FC` | `0x6` | 7/12（PDC 写成功但没效果） |

**唯一的变化是 `0xF3C` 从 `0x4` 变成 `0x6`**，即多了 mfg1 那一位。
这更支持"`0xF3C` 是 gpufreq 侧镜像、不随 PDC 更新"的解释 —— 因为如果 PDC 真接管了，它应该跳到 `0x3FFE`。

---

## 5. 下一步（按我判断的优先级）

### P1：先验证 `0xF3C` 到底是不是可信的状态源（**不用刷机**）

在 trial 602 的内核里加一段：**同时连续读 `0xF34` / `0xF38` / `0xF3C` / `0xF40` 若干次**，
并打印 `0xF3C` 的原始值（不带 `& 0x3ffe` 掩码，看高位有什么）。

如果 `0xF3C` 高 16 位有非零值，说明它是个带状态标志的复合寄存器，低位不能直接当域状态读。

### P2：查 `st6` 为什么恒 0（**最容易有收获**）

`pdc_report()` 打印 `sc0 sc9 cg st0 st4 st6`，其中 `sc9` 与 `st6` 恒 0。
- `sc9 = 0` 可能是索引 9 对应的核不存在（G610 只有 6 个 shader core）
- **`st6 = 0` 必须查清**：如果它对应第二层栈的使能，那 mfg8..mfg12 起不来就有解释了

需要：`pdc_enable()` 里 `MALI_PDC_CORES` 的定义、`st` 系列寄存器的 offset 表、
以及 MT6895 上 PDC 的 core→domain 映射（来自 `gpufreq_mt6895.c` 或 DTS）。

### P3：按用户/web 端主控的判据回报

按 MASTER-BRIEF 的判读规则：
> 仍是 `0x1FC`/`0x4` → PDC 没接管，还缺一个使能，回报给我继续挖。

**当前是 `0x1FC` / `0x6`，属于"PDC 没接管"。** 我倾向于认为还缺的是
**PDC 侧的主动触发**（不只写 `MFG_ACTIVE_POWER_CON`，还要写 `PDC_..._REQ` 之类），
以及 `st6` 那个恒 0 的寄存器。

---

## 6. 本轮改动（驱动源码）

1. 修掉 `mali_pdcauto` 分支的位置：从 `power_on()` 开头 → `clock_26m()` 之后
2. 修正后 `pdcauto` 分支不再重复统计、不再 `return up` 提前退出，改为让 bring-up 继续走
3. 顺带加了一个独立开关 `mali_domainwait`（默认 0）：让 `domain_on()` 的两处轮询
   按 scpsys 的方式 **10 µs 间隔 / 1 秒预算**，而不是现在这种 **0.72 秒无延时紧循环**。
   **本轮三次刷机都没打开它**（`domainwait 0`），留作下一个独立变量。

### 关于 `mali_domainwait` 的实测依据

从 trial 402 日志量出：相邻两次域放弃间隔 **0.723 s**，即每域 2,000,000 次无延时读共约 0.72 s。
而 scpsys 的语义是 `MTK_POLL_DELAY_US=10` × 100,000 次 = **1.0 s**。
**0.72 s < 1.0 s**，每域少等约 28%。这意味着当前代码可能在硬件还没答复时就放弃，
然后对"仍在握手中"的域清隔离、抬复位 —— 正是驱动注释里描述的失败形态。

**证据边界**：这只证明"等待预算比 vendor 短 28%"，**不证明** 5 个域是因为等不够才失败。
要区分这两者，必须跑 `domainwait=1` 对照。

---

## 7. 证据文件

| 文件 | 内容 |
|---|---|
| `trial-600-logbook.bin` / `-mali.txt` | mali=6 全流程（PDC 输出被 ring 冲掉） |
| `trial-601-logbook.bin` / `-mali.txt` | mali=1，PDC 写在时钟门之前（全读 0） |
| `trial-602-logbook.bin` / `-mali.txt` | mali=1，PDC 顺序已修（写入生效但 F3C 未变） |
| `trial-600-flash.txt` / `-reboot.txt` | 刷入与重启原始输出 |
| `driver-final.c` | 改动后的驱动快照 |
| `driver-sha.txt` | 驱动与镜像哈希 |

关键原始行（trial 602）：

```text
21.106260 mali: mfg1  MPU on: 0xc000000d
21.106267 mali: pdc auto: mfg1 up (PDC deferred until after clock_26m)
21.307329 mali: pdc auto: 0xF3C 0x6 (want 0x3ffe) 0xF40 0x6 0xF34 0x1fc
21.309869 mali: pdc auto: 17 pdc words written (gate open)
21.309896 mali: pdc: sc0 0x2000001 sc9 0 cg 0x2000001 st0 0x2000001 st4 0x2000001 st6 0
21.511166 mali: pdc auto: 0xF3C 0x6 (want 0x3ffe) 0xF40 0x6 0xF34 0x1fc
21.511184 mali: pdc auto: 1 of twelve domains up via PDC
21.511242 mali: after pdc: mfg5 0x1112 mfg6 0x1112 mfg10 0x1112 mfg12 0x1112
```
