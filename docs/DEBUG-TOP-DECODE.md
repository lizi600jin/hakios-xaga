# `mfg_debug_top()` — MediaTek MFG_DEBUG_TOP decode

**Status: FOUND in public MediaTek vendor kernel source.** Not a guess, not a Mali register,
not SPM/infracfg/pericfg.

---

## §1 Exact file path + line, with code quoted

### Upstream (public, HTTP 200 verified)

<https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/xaga-s-oss/drivers/gpu/mediatek/gpufreq/v2/gpufreq_mt6895.c#L1249-L1273>

`drivers/gpu/mediatek/gpufreq/v2/gpufreq_mt6895.c`, function `__gpufreq_check_bus_idle()`,
lines **1249–1273**:

```c
1249 void __gpufreq_check_bus_idle(void)
1250 {
1251 	u32 val = 0;
1252
1253 	/* MFG_QCHANNEL_CON 0x13FBF0B4 [0] MFG_ACTIVE_SEL = 1'b1 */
1254 	val = readl(g_mfg_top_base + 0xB4);
1255 	val |= (1UL << 0);
1256 	writel(val, g_mfg_top_base + 0xB4);
1257
1258 	/* MFG_DEBUG_SEL 0x13FBF170 [1:0] MFG_DEBUG_TOP_SEL = 2'b11 */
1259 	val = readl(g_mfg_top_base + 0x170);
1260 	val |= (1UL << 0);
1261 	val |= (1UL << 1);
1262 	writel(val, g_mfg_top_base + 0x170);
1263
1264
1265 	/*
1266 	 * polling MFG_DEBUG_TOP 0x13FBF178 [0] MFG_DEBUG_TOP
1267 	 * 0x0: bus idle
1268 	 * 0x1: bus busy
1269 	 */
1270 	do {
1271 		val = readl(g_mfg_top_base + 0x178);
1272 	} while (val & 0x1);
1273 }
```

This is the byte-identical function the local tree carries at
`docs/round-13-support/hakios.mali610.trial-686.c:2241-2285`, so the board's
`mfg_bus_idle()` is a faithful (but bounded) reimplementation of the vendor's.

### Register identity

| Item | Value | Source |
|---|---|---|
| Block | `MFG_TOP_CONFIG` | MT6983 SW register table, base `+0x13FBF000` — [16rd thread 601254](https://bbs.16rd.com/thread-601254-1-1.html) |
| Base | `0x13FBF000` | `xaga.dts:9451` maps it as DT resource `mfg_top_config` (`0x13fbf000`, size `0x1000`) |
| `MFG_QCHANNEL_CON` | `+0xB4` → `0x13FBF0B4` | same table (`13FBF0B4 MFG_QCHANNEL_CON`) |
| `MFG_DEBUG_SEL` | `+0x170` → `0x13FBF170` | same table (`13FBF170 MFG_DEBUG_SEL`) |
| `MFG_DEBUG_TOP` | `+0x178` → `0x13FBF178` | vendor driver comments; absent from the table (it jumps `0x174 MFG_DEBUG_LATCH` → `0x180`) |

### Who calls it, and where

`gpufreq/v2/gpufreq_v2.c:260` `gpufreq_check_bus_idle()` → platform fp `check_bus_idle`
(`gpufreq_mt6895.c:265`, `:280`) → `__gpufreq_check_bus_idle()`.

The Mali driver calls it on the power-off path:

`drivers/gpu/mediatek/mali-valhall/gpu/arm/midgard/platform/mt6895/mali_kbase_config_mt6895.c`
`pm_callback_power_off_nolock()`, lines **153–162**:

```c
153 	/* check MFG bus if idle */
154 #if defined(CONFIG_MTK_GPUFREQ_V2)
155 	gpufreq_check_bus_idle();
156 #endif /* CONFIG_MTK_GPUFREQ_V2 */
...
162 	if (gpufreq_power_control(POWER_OFF) < 0) {
```

It is also called twice from `__gpufreq_dump_infra_status()`
(`gpufreq_mt6895.c:1275`, arm/`mt6983` file `:1244`), which merely prints the word.

**Note on the call site:** the vendor loop is an **unbounded `do { } while (val & 0x1)`** with no
timeout, sitting *before* `POWER_OFF` on the power-off path. So in the vendor kernel a
permanently-busy bus does not log a warning — it **hangs the caller forever**. It behaves as a
gate, not as a debug aid, whatever the comment style suggests. The local driver's bounded
100000-spin loop is a deliberate deviation (documented at
`hakios.mali610.trial-686.c:2259-2260`).

---

## §2 Decoding of 0x4 and 0x1b

**A partial decode exists — bit 0 only — and it is fully sufficient to settle 0x4 vs 0x1b.**

The only documented field is **bit 0**:

```
MFG_DEBUG_TOP 0x13FBF178 [0] MFG_DEBUG_TOP
0x0: bus idle
0x1: bus busy
```

There is **no public bit-by-bit table for bits 1–31**. NOT FOUND.

Applying bit 0 to the observed values:

| Value | Binary | Bits set | bit 0 | Verdict |
|---|---|---|---|---|
| `0x4` | `0b00000100` | 2 | **0** | **idle** |
| `0x1b` | `0b00011011` | 0, 1, 3, 4 | **1** | **busy** |

So `0x4` is **not** "bit 2 = the busy flag". It is a debug word whose *other* bits happen to be
set, and whose bit 0 reads idle. `0x1b` is **not** a meaningful bitmask to enumerate — bits 1, 3
and 4 are undocumented side-channel state of the debug mux; only bit 0 carries the defined
busy/idle meaning.

### The cross-check that makes this airtight

There are **two independent generations of this same register in the tree, with opposite bit
conventions**, and *both* return the same verdict for these exact values:

**v2 (mt6895, mt6983)** — bit 0, active-high busy
`gpufreq/v2/gpufreq_mt6895.c:1265-1272` and `gpufreq_mt6983.c:1259-1266`

```c
 * polling MFG_DEBUG_TOP 0x13FBF178 [0] MFG_DEBUG_TOP
 * 0x0: bus idle
 * 0x1: bus busy
```

**v1 (mt6873, mt6885, mt6853)** — bit 2, active-high *idle*
`gpufreq/v1/mt6873/mtk_gpufreq_plat.c:816-820` (identical at `mt6885:650`, `mt6853:771`)

```c
	/* polling register MFG_DEBUG_TOP (0x13fb_f178) bit 2 = 0x1 */
	/* => 1 for bus idle, 0 for bus non-idle */
	do {
		val = readl(g_mfg_base + 0x178);
	} while ((val & 0x4) != 0x4);
```

Score both conventions against the two observed words:

| Word | v2 rule (bit0: 1=busy) | v1 rule (bit2: 1=idle) |
|---|---|---|
| `0x4` | bit0=0 → idle | bit2=1 → **idle** |
| `0x1b` | bit0=1 → busy | bit2=0 → **non-idle** |

Both conventions independently say **idle → non-idle**. The transition is real, not an artifact of
picking the wrong platform's bit. This also explains the trap noted in the repo README: copying
v1's bit 2 onto v2 inverts the conclusion.

Bit 2 and bit 0 are therefore almost certainly the *same* internal condition exposed with opposite
polarity on different SoC generations (v1: `1=idle`; v2: `1=busy`). No source comment states this
explicitly — [plausible], inferred from the two tables above, not from a decode document.

---

## §3 What `qchannel 0x11` means

**`qchannel` is not a channel ID. It is the raw value of `MFG_QCHANNEL_CON` (0x13FBF0B4).**
[proven by source]

The board's print reads the whole register:

```c
2280	printf("mali: bus idle %s: qchannel %#x debug_sel %#x debug_top %#x (%s after %u spins)\n",
2281	    when, reg_rd(gMfgcfg, MALI_MFG_QCHANNEL_CON), ...
```

`0x11` = `0b0001_0001`:

| Bit | Field | Value | Meaning |
|---|---|---|---|
| 0 | `MFG_ACTIVE_SEL` | 1 | set by the driver one line earlier (`gpufreq_mt6895.c:1253-1256`) so the debug channel watches the live bus |
| 4 | `QCHANNEL_ENABLE` | 1 | pre-existing; v1 sets it explicitly as `MFG_QCHANNEL_CON [4] QCHANNEL_ENABLE = 0x1` (`mt6873:848`) |

Bit 4 = 1 is corroborated independently by the LPM DCM table, which records the expected value of
this register as `0x10`:

`drivers/misc/mediatek/lpm/modules/debug/mt6895/mtk_dcm_array.c:271`
```c
	0x13FBF0B4, 0x10, 0x10,/* MFG_QCHANNEL_CON */
```

So `0x11 = 0x10 (QCHANNEL_ENABLE) | 0x01 (ACTIVE_SEL)`. It is a **configuration word the driver
itself just wrote** — which is exactly why it never changes across the three samples. It carries
no per-transaction information.

Likewise `debug_sel 0x3`: `MFG_DEBUG_TOP`'s selection field `[1:0] = 2'b11`
(`gpufreq_mt6895.c:1258-1262`). Also config, also constant. Both constants being stable is not
evidence of anything about the MCU.

There is **no `qchannel` array, no QoS channel numbering, no `qchannel 17`** anywhere in the vendor
tree. NOT FOUND — because that reading does not exist.

---

## §4 Verdict

**Does 0x4 → 0x1b prove the MCU is executing, prove it hung, or neither?**

### Neither. It proves one narrow, well-defined thing.

> The MFG block's bus-idle flag went from idle to not-idle at some point between the table walk
> and the MCU start request, and stayed not-idle.

That is all. [proven by source] — the sole documented semantic is bit 0 of 0x13FBF178.

### Per-hypothesis grading

| Claim | Grade | Reasoning |
|---|---|---|
| (a) "the MCU is actively fetching instructions" | **[speculation]** | The register has no master attribution. It reports MFG-block aggregate bus state. Nothing in the vendor source links bit 0 to the CSF/MCU specifically, and the same bit is used purely as a "may I cut the power yet" precondition. |
| (b) "the MCU issued a bus transaction that never completed (hang)" | **[plausible]** | Best-supported reading, but not proven. Supporting: the word is a *level*, not a mux artifact — 4 reads 1 ms apart all returned `0x1b 0x1b 0x1b 0x1b` (`trial-685/686/687-mali.txt`), and a multiplexed debug value would wander. It also never clears after `MCU_CONTROL=0`, which is what an uncleared outstanding transaction looks like. Missing: any source statement that bit 0 latches or reflects an incomplete AXI handshake. |
| (c) "just the register window got selected/armed" | **ruled out as the cause of 0x4→0x1b** | [proven by source] The arming writes are to `0xB4` and `0x170`. Neither touches `0x178`. And arming happened *before* both samples — `debug_sel 0x3` and `qchannel 0x11` are already present in the `0x4` sample. If arming caused the busy bit, the first read would already have been `0x1b`. |

### What this does and does not settle for the driver

- It **does** establish that the MFG AXI channel is not idle, and that it stays that way even
  after the core is switched off. A bus that never drains is a genuine anomaly worth the log line.
- It **does not** establish that the MCU ever drove the bus. A stuck transaction from *any* MFG
  master — or from a GALS/async-bridge state left armed, or from the GPUEB coprocessor if it is
  still alive — would set the same bit. This matches the local driver's own caution at
  `docs/GPU-ROUND-1.md:192` and `:240`: `debug_top=0x1b` cannot by itself prove "an instruction
  was fetched and refused by EMI", and "a change in `debug_top` alone does not mean the firmware
  started."

**Bottom line:** `debug_top` is a **liveness/blockage indicator for the MFG bus**, not a liveness
signal for the GPU microcontroller. Reading it as "the MCU is executing" is unsupported; reading it
as "the MCU hung on a bus transaction" is plausible but unproven, because the register is
aggregate and the vendor never documents bits 1–31 or any master mapping.

---

## Citation index

All URLs verified live (HTTP 200) at time of writing.

| Purpose | URL |
|---|---|
| **Primary definition** (mt6895, `__gpufreq_check_bus_idle`, L1249-1273) | <https://github.com/MiCode/Xiaomi_Kernel_OpenSource/blob/xaga-s-oss/drivers/gpu/mediatek/gpufreq/v2/gpufreq_mt6895.c#L1249-L1273> |
| Raw primary | <https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/xaga-s-oss/drivers/gpu/mediatek/gpufreq/v2/gpufreq_mt6895.c> |
| Same, mt6983 (sibling platform, identical semantics) | <https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/xaga-s-oss/drivers/gpu/mediatek/gpufreq/v2/gpufreq_mt6983.c> |
| **v1 opposite-bit convention** (bit 2, 1=idle) | <https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/xaga-s-oss/drivers/gpu/mediatek/gpufreq/v1/mt6873/mtk_gpufreq_plat.c> |
| **kbase call site** (power-off path) | <https://raw.githubusercontent.com/XagaForge/android_kernel_xiaomi_mt6895/16.2/drivers/gpu/mediatek/mali-valhall/gpu/arm/midgard/platform/mt6895/mali_kbase_config_mt6895.c> |
| `MFG_QCHANNEL_CON` expected `0x10` (bit 4) | <https://raw.githubusercontent.com/MiCode/Xiaomi_Kernel_OpenSource/xaga-s-oss/drivers/misc/mediatek/lpm/modules/debug/mt6895/mtk_dcm_array.c> |
| MT6983 `MFG_TOP_CONFIG` register map (external) | <https://bbs.16rd.com/thread-601254-1-1.html> |
| Mirror branch used locally | <https://github.com/XagaForge/android_kernel_xiaomi_mt6895/tree/16.2> |

Local corroboration inside this repo:
`docs/round-13-support/hakios.mali610.trial-686.c` (`mfg_bus_idle()` 2262-2285,
`mfg_debug_top()` 2301-2305), `docs/round-13-support/trial-68{5,6,7}-mali.txt`,
`docs/GPU-ROUND-1.md:192,240`.
