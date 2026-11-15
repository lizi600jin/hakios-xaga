# panthor 启动判据：本地源码核实

> 2026-11-15 13:00，DSH。**全部结论引自本地 panthor 源码**（`.csf-re/src/`），
> 不是转述子代理，也不是凭记忆。
>
> 本地副本路径：`.csf-re/src/panthor_fw.c`、`panthor_fw_regs.h`
> （已在 `.gitignore` 里，不提交）。

---

## 0. 一句话

**panthor 判定「MCU 启动成功」的唯一依据是 `JOB_INT_GLOBAL_IF`（bit31），
从不使用 `MCU_STATUS`。** `MCU_STATUS` 只在超时后被读一次，用于打印错误信息。

所以 trial 695 的 `MCU_STATUS=1 (ENABLED)` **不是矛盾状态**，
它正是 panthor 会打印 `status=enabled` 的那条失败分支。

---

## 1. `panthor_fw_start()` 全文（`panthor_fw.c:1132-1163`）

```c
static int panthor_fw_start(struct panthor_device *ptdev)
{
	struct panthor_fw *fw = ptdev->fw;
	bool timedout = false;

	ptdev->fw->booted = false;
	panthor_irq_enable_events(&ptdev->fw->irq, ~0);
	panthor_irq_resume(&ptdev->fw->irq);
	gpu_write(fw->iomem, MCU_CONTROL, MCU_CONTROL_AUTO);

	if (!wait_event_timeout(ptdev->fw->req_waitqueue,
				ptdev->fw->booted,
				msecs_to_jiffies(1000))) {
		if (!ptdev->fw->booted &&
		    !(gpu_read(fw->irq.iomem, INT_STAT) & JOB_INT_GLOBAL_IF))
			timedout = true;
	}

	if (timedout) {
		static const char * const status_str[] = {
			[MCU_STATUS_DISABLED] = "disabled",
			[MCU_STATUS_ENABLED] = "enabled",
			[MCU_STATUS_HALT] = "halt",
			[MCU_STATUS_FATAL] = "fatal",
		};
		u32 status = gpu_read(fw->iomem, MCU_STATUS);

		drm_err(&ptdev->base, "Failed to boot MCU (status=%s)",
			status < ARRAY_SIZE(status_str) ? status_str[status] : "unknown");
		return -ETIMEDOUT;
	}

	return 0;
}
```

### 逐条判读

| 行 | 内容 | 含义 |
|---|---|---|
| `:1137` | `ptdev->fw->booted = false` | 先清标志 |
| `:1138` | `panthor_irq_enable_events(&fw->irq, ~0)` | **mask 写全 1 = 全使能**（极性已多次确证） |
| `:1139` | `panthor_irq_resume(&fw->irq)` | |
| `:1140` | `gpu_write(MCU_CONTROL, MCU_CONTROL_AUTO)` | 写 **2** 到 `0x700` |
| `:1142-1146` | `wait_event_timeout(booted, 1000ms)` | **唯一成功判据** |
| `:1145-1146` | 超时后再补查一次 `INT_STAT & JOB_INT_GLOBAL_IF` | 防止竞态漏读 |
| `:1157` | `gpu_read(MCU_STATUS)` | **只在失败路径读，只用于打印** |

**关键：`MCU_STATUS` 不出现在任何成功判定里。** `:1153` 的
`[MCU_STATUS_ENABLED] = "enabled"` 存在的唯一目的，就是让
「超时 + status=enabled」这个组合能被打印出来。

---

## 2. `booted` 只由 `JOB_INT_GLOBAL_IF` 置位（`panthor_fw.c:1109-1110`）

```c
	if (!ptdev->fw->booted && (status & JOB_INT_GLOBAL_IF))
		ptdev->fw->booted = true;
```

`JOB_INT_GLOBAL_IF` = **bit31**。除此之外没有任何地方写 `fw->booted`。

---

## 3. 枚举值（`panthor_fw_regs.h:15-18`）与我们的实现一致

```c
#define   MCU_STATUS_DISABLED				0
#define   MCU_STATUS_ENABLED				1
#define   MCU_STATUS_HALT				2
#define   MCU_STATUS_FATAL				3
```

```c
#define MCU_CONTROL_BASE				0x700
#define MCU_CONTROL					0x0
#define   MCU_CONTROL_ENABLE				1
#define   MCU_CONTROL_AUTO				2
#define   MCU_CONTROL_DISABLE				0
```

**是顺序枚举，不是 one-hot 位掩码。** 判读必须用 `==`，不能用 `&`。
我们的驱动用的是 `==`，正确。

> ⚠️ 注意：kbase 那边写的是 `MCU_STATUS_HALTED (1 << 1)`，
> **长得像位掩码**。照搬 kbase 的写法去 `&` 会出错。以 panthor 的为准。

---

## 4. `panthor_fw_stop()`：我们这块板会稳定命中它的错误路径

`panthor_fw.c:1165-1176`：

```c
static void panthor_fw_stop(struct panthor_device *ptdev)
{
	...
	gpu_write(fw->iomem, MCU_CONTROL, MCU_CONTROL_DISABLE);
	if (gpu_read_poll_timeout(fw->iomem, MCU_STATUS, status,   /* :1173 */
				  status == MCU_STATUS_DISABLED, 10, 100000))
		drm_err(&ptdev->base, "Failed to stop MCU");
```

**「停」的判据是 `MCU_STATUS == 0`。** trial 695 里写 `MCU_CONTROL=0` 之后
`status` 仍是 `1`（日志 `:3259`「MCU control written 0, status now 1」），
所以这块板会**稳定命中** `Failed to stop MCU`。

> 含义：**真正在跑的核会响应 DISABLE。** 我们的核不响应。
> 这条与「`MCU_STATUS` 可能根本不反映核心真实状态」相容，
> 但目前**没有证据判定**它是「核没跑」还是「状态寄存器粘滞」。

---

## 5. 对我们项目的结论

| 事项 | 结论 |
|---|---|
| 「`MCU_STATUS=1` 说明启动成功」 | **错**。panthor 自己不用它判成功 |
| 「`MCU_STATUS=1` 与无 GLOBAL_IF 矛盾」 | **不矛盾**。这是 panthor 定义的启动失败 |
| 我们的判据（等 GLOBAL_IF） | **对的**，与 panthor 一致 |
| `JOB_INT_MASK` 极性（1=使能） | **对的** |
| `MCU_CONTROL_AUTO = 2` | **对的** |
| 仍需解释的 | 为什么 GLOBAL_IF 从未升起 |

---

## 6. 证据边界

- **已用本地源码证实**：上文引用的每一段代码。
- **未证实**：`MCU_STATUS=1` 的物理含义。panthor 只是把它当错误信息打印，
  并没有解释「为什么 enabled 了却没 boot」。公开资料里也没有。
- **未证实**：我们的 `MCU_STATUS` 粘滞（DISABLE 后仍为 1）是核心没停，
  还是寄存器根本不映射到真实状态。要区分它，需要另一条独立的状态线索。
