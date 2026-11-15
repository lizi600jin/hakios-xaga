# MT6368 的 HP trim 校准机制：为什么 dmesg 里没有那两行

> 2026-11-15 22:55，DSH 接手技术决策后的第一份分析。
> **依据**：vendor 源码 `docs/headset-wip/raw/mt6368.c`（249493 B，
> sha256 `84ebc78f…`，与 Codex 那份 `vendor-mt6368.c` 完全同一文件）。

---

## 0. 一句话

**trim 校准不是启动时跑的，是由 DAPM 的 `DC Trim` widget 在 `POST_PMD`
（关机路径）触发的异步线程。原厂 Android 开机后如果没走过那条 DAPM 路径，
它就不会跑 —— 所以 dmesg 里没有那两行是预期的，不是校准失败。**

---

## 1. 判据行的确切出处

`mt6368.c:4905-4926`：

```c
static void get_hp_trim_offset(struct mt6368_priv *priv, bool force)
{
	struct dc_trim_data *dc_trim = &priv->dc_trim;
	struct hp_trim_data *hp_trim_3_pole = &priv->hp_trim_3_pole;

	if (dc_trim->calibrated && !force)
		return;

	dev_info(priv->dev, "%s(), Start DCtrim Calibrating", __func__);
	dc_trim->calibrated = true;

	enable_trim_circuit(priv, true);
	calculate_lr_trim_code(priv);
	calculate_lr_finetrim_code(priv);
	enable_trim_circuit(priv, false);

	dev_info(priv->dev, "%s(), after trim_code R:(0x%x/0x%x), L:(0x%x/0x%x)",
		 __func__,
		 hp_trim_3_pole->hp_fine_trim_r, hp_trim_3_pole->hp_trim_r,
		 hp_trim_3_pole->hp_fine_trim_l, hp_trim_3_pole->hp_trim_l);
}
```

**格式串与 [CMD] 里要的完全一致**：`after trim_code R:(0x%x/0x%x), L:(0x%x/0x%x)`。

---

## 2. 🔴 触发路径：DAPM SPPLY 的 POST_PMD，不是 probe

`mt6368.c:4931-4942`：

```c
static int dc_trim_thread(void *arg)
{
	struct mt6368_priv *priv = arg;

	get_hp_trim_offset(priv, false);
	...
}
```

`mt6368.c:3232-3250`：

```c
static int mt_dc_trim_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol,
			    int event)
{
	...
	if (dc_trim->calibrated)
		return 0;

	kthread_run(dc_trim_thread, priv, "dc_trim_thread");
	return 0;
}
```

`mt6368.c:3665-3671`：

```c
	/* DC trim : trigger dc trim flow because set the reg when init_reg */
	/* this must be at the last widget */
	SND_SOC_DAPM_SUPPLY("DC Trim", MT6368_AUDDEC_ANA_CON16,
			    RG_AUDTRIMBUF_EN_VAUDP32_SFT, 0,
			    mt_dc_trim_event, SND_SOC_DAPM_POST_PMD),
```

### 三层结构

| 层 | 内容 |
|---|---|
| widget | `DC Trim`，寄存器 `AUDDEC_ANA_CON16`（**`0x2598`**），bit `RG_AUDTRIMBUF_EN_VAUDP32`（**bit6**） |
| 事件 | **`SND_SOC_DAPM_POST_PMD`**（关机后，不是开机前） |
| 回调 | `mt_dc_trim_event` → `kthread_run(dc_trim_thread)` → `get_hp_trim_offset` |

**`POST_PMD` 是关键**：这说明 trim 在**那条 DAPM 路径关闭时**触发，不在打开时。

**vendor 的注释解释了原因**：

> DC trim : trigger dc trim flow because set the reg when init_reg
> **this must be at the last widget**

**意思是：DC Trim 放在 widget 表最后，靠「最后被关闭」来保证在寄存器初始化完之后跑。**

---

## 3. 这解释了 dmesg 为什么没有那两行

### 3.1 逻辑链

```
原厂 Android 开机
  → codec probe 加载 mt6368.c（注册 DAPM widget，但不触发 trim）
  → 用户没有播放音频
  → 没有音频路径被打开
  → 也没有音频路径被关闭
  → DC Trim widget 从未走过 POST_PMD
  → mt_dc_trim_event 从未被调用
  → dc_trim_thread 从未创建
  → get_hp_trim_offset 从未执行
  → dmesg 里没有 "Start DCtrim Calibrating" 也没有 "after trim_code"
```

### 3.2 支持这条的证据

| 证据 | 说明 |
|---|---|
| `grep -c "DC Trim" mt6368.c` = **1** | **只在 widget 声明处出现，没有任何 DAPM route 引用它** |
| `dc_trim->calibrated` 初值 | `probe` 里没有初始化它，靠 `kzalloc` 的零值 |
| `get_hp_trim_offset` 的两处引用 | `:4935`（线程内）和 `:4905`（定义），**没有 probe 直接调用** |

### 3.3 但这条有不确定性，必须说清

**`DC Trim` 没有显式 route，不等于它一定不会触发。**

DAPM 对 `SND_SOC_DAPM_SUPPLY` 有隐式规则：supply widget 可以被它的 consumer 自动带动。
**如果某个实际使用的路径把它当上游 supply，它就会跟着走。**

**我没查到它是否被隐式引用** —— 这需要 DAPM 运行时状态，不是静态 grep 能确定的。

**所以准确的说法是**：

| 能说 | 不能说 |
|---|---|
| trim 由 `DC Trim` 的 `POST_PMD` 触发，不在 probe | 「原厂 Android 从未跑过 trim」 |
| 我们没看到那两行，与「没有音频路径开关」相容 | 「trim=0」或「校准失败」 |
| `DC Trim` 没有显式 route 引用 | 「它一定不会被隐式带动」 |

---

## 4. 对我们的影响

### 4.1 好消息

**这解释了 preflight 为什么没拿到 trim 值，而且原因不是「校准缺失」。**

### 4.2 坏消息

**我们的 `headset=` 驱动完全不走 DAPM。** 它直接写 `AUDDEC_ANA` 寄存器，
**永远不会触发 `DC Trim` 的 `POST_PMD`**。

**所以即使我们实现了上电序列，trim 也不会自动跑。**

### 4.3 这意味着 DAPM 接管是必需的

**vendor 的 `mt6368.c` 用 DAPM 管理 91 处 `snd_soc_dapm_*`，
包括供电顺序（`SUPPLY_SEQ_*`）、时钟（`mt_dcxo_event`）、trim。**

**我们的直接寄存器写入绕过了整套机制。**

**要真正让 DAC 工作，有两条路：**

| # | 路线 | 代价 |
|---|---|---|
| **A** | 把 vendor 的 DAPM 表移植进 XNU（`mt6368_dapm_widgets` / `routes` / `mt_dcxo_event` / trim） | 大，但完整 |
| **B** | 在我们的驱动里**手工复现** DAPM 的关键动作（按 `SUPPLY_SEQ_*` 顺序写、手工调 trim） | 中，但要自己保证顺序正确 |

---

## 5. 下一步建议（未执行）

| # | 动作 | 依据 |
|---|---|---|
| 1 | **读 `mt_dcxo_event`** —— 它调 `mt6685_set_dcxo`，是外部时钟的关键 | `grep mt_dcxo_event mt6368.c` |
| 2 | **读 `SUPPLY_SEQ_*` 全表** —— 这是 vendor 的供电顺序编号 | 决定手工复现的顺序 |
| 3 | **读 `enable_trim_circuit` / `calculate_lr_trim_code`** | 若走路线 B 要手工实现 |
| 4 | 取得 `mt6685_audclk.ko` 或源码 | preflight 发现文件缺失，这是待解项 |

---

## 6. 证据边界

| 已证明（源码） | 未证明 |
|---|---|
| trim 的打印格式串与判据完全一致 | 原厂 Android 是否曾触发过 trim |
| 触发点是 `DC Trim` widget 的 `POST_PMD` | `DC Trim` 是否被某条隐式路径带动 |
| 不在 codec probe 路径里 | 我们的实现应该走路线 A 还是 B |
| 我们的驱动不走 DAPM，不会触发它 | trim 的实际值是多少 |
