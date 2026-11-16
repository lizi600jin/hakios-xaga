# 反对意见 1 的更正：我读错了代码

> 2026-11-17 01:05，DSH。**Codex 指出后我重新读了候选驱动，我的说法有误。**

---

## 我原来写的（错误）

> 「这个镜像只能回答『UFS 探针那个问题』的一半。它测的是 CPU 频率域，
> 而 CPU 探针的 status bit1 严格检查会让结果很可能是 `CPU_NOT_READY` ——
> 按你之前的判定那是故意保守、不是设备坏，但意味着这一轮很可能拿不到 `CPU_OBSERVED`。
> **若目的是验证『探针能安全读寄存器而不崩』，那没问题；若期望看到完整 LUT，可能落空。**」

## 我错在哪

**「若期望看到完整 LUT，可能落空」这句不成立。我读了代码，两处都否证它：**

### 事实 1：`cpu_observe` 先填满两份快照，再做 status 判定

**`hakios.mt6895_cpufreq.c:76-95`：**

```c
static enum cpu_result
cpu_observe(const struct cpu_reader *io, struct cpu_observation *out)
{
	struct cpu_observation o = { 0 };
	...
	if (!cpu_read_snapshot(io, &o.first) || !cpu_read_snapshot(io, &o.second)) {
		result = CPU_READ_FAILED;   /* ← 两份快照在这里已经读完 */
		goto done;
	}
	...
	if (o.second.enable != 1u) {
		result = CPU_NOT_ENABLED;   /* ← 只是设结果码 */
		goto done;
	}
	if ((o.second.status & 3u) != 3u) {
		result = CPU_NOT_READY;     /* ← 只是设结果码 */
		goto done;
	}
```

**`status` 检查只决定 `result` 的值，不回退、不清空已读的快照。**

### 事实 2：`*out = o` 在 `done:` 标签处，所有路径都执行

**第 126-129 行：**

```c
done:
	*out = o;
	return result;
}
```

**无论从哪个 `goto done` 出来，输出结构都被填好。**

### 事实 3：32 行 LUT 的打印在 result 判定之外

**`hakios_mt6895_cpufreq.c` 的 init 里：**

```c
result = cpu_observe(&io, &o);
printf("mt6895-cpu: domain %u base=%#x ... result=%u rows=%u request_khz=%u\n", ...);
for (uint32_t row = 0; row < CPU_ROWS; row++) {
    printf("mt6895-cpu: domain %u row %u raw=%#x/%#x\n", domain, row,
        o.first.lut[row], o.second.lut[row]);      /* ← 无条件循环 */
}
```

**`for` 循环不在任何 `if` 里 —— 每个域都无条件打印 32 行、每行两个值（first/second）。**

---

## 更正后的正确表述

| 项 | 事实 |
|---|---|
| **`CPU_NOT_READY` 时能否拿到原始 LUT** | **能。** 三个域 × 32 行 × 2 份快照 = **192 个原始字全部打印** |
| **`CPU_NOT_READY` 意味着什么** | **只是 `result` 字段的值**，不阻断数据输出 |
| **本轮验收标准** | **三域快照 + 正常返回 + 恢复，不强求 `OBSERVED`**（Codex 明确）|

**→ 我的「可能落空」是错的。这一轮无论 status 如何，原始表都拿得到。**

---

## 我另外自己核实的：这轮完全不测 UFS

**我原来第一句写「只能回答 UFS 探针那个问题的一半」—— 这句也是混淆。**

**这个镜像里没有任何 UFS 代码：**

| 检查 | 结果 |
|---|---|
| 参数串里有 `mtk_ufs_probe` | **0 次** |
| 参数串里有 `ufs` | **0 次** |
| 内核里 `mt6895-ufs` 字符串 | **0 条**（只有 `mt6895-cpu` 五条） |
| 候选驱动里 `.o` | `hakios.mt6895_cpufreq.o` 有，`hakios.mt6895_ufs.o` **无** |

**→ 这轮是纯 CPU 探针试验，与 UFS 无关。我把两件事混在一起说了。**

---

## 我保留的两条意见（未被反驳）

| # | 内容 | 状态 |
|---|---|---|
| **2** | `loader.bin` 比 712 小 176 B（48768 vs 48944），因为参数串去掉了 headset/tone/trim 段 | 事实，与参数变化一致 |
| **3** | `hakios_kver` 带 `-dirty`，是沿用的遗留元数据 | **Codex 已确认：「-dirty 仅遗留 metadata 已记录，无需改镜像」** |

---

## 边界

**本轮只读代码与字符串，未改任何文件。**
