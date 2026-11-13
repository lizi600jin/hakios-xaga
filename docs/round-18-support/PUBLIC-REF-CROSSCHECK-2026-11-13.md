# 公开参考交叉核对（2026-11-13 23:10）

**手机零访问。** 本轮把我们的固件与三份公开资料对照：RK3588 上**同款 G610** 的固件逆向
（icecream95）、panthor 上游源码、Collabora 的 Tyr 系列文章。

---

## 1. 交叉验证成功的一件事：`0x01010000` 就是接口版本 1.1.0

我之前编过"1.1"，6.1sol 指出是编的。**现在有出处了。**

panthor 的定义（`panthor_panthor_fw.c:209-213`）：

```c
#define CSF_IFACE_VERSION(major, minor, patch) ((major)<<24 | (minor)<<16 | (patch))
#define CSF_IFACE_VERSION_MAJOR(v)  ((v) >> 24)
#define CSF_IFACE_VERSION_MINOR(v)  (((v) >> 16) & 0xff)
#define CSF_IFACE_VERSION_PATCH(v)  ((v) & 0xffff)
```

我们固件在 `0x1003012` 写的是 `0x01010000`：

| 字段 | 值 |
|---|---|
| major | 1 |
| minor | 1 |
| patch | 0 |

而 `panthor_panthor_fw.c:1004` 有一句：

```c
if (glb_iface->control->version < CSF_IFACE_VERSION(1, 1, 0))
```

→ **我们的固件发布的正是 panthor 要求的最低接口版本 1.1.0。**

另一份独立证据：[icecream95 的 RK3588 固件 dump](https://icecream95.gitlab.io/fun-with-csf-firmware.html)
显示 RK3588 的镜像头是 `Firmware version 0x1010001` = **1.1.1**，与我们同一 major/minor 家族，
patch 高一级。**同款 G610，同一接口代数。**

→ 这条现在可以写进交接了：**版本字的值本身已被两个独立来源确认**，不再是我编的。

---

## 2. 与 RK3588 固件的结构差异（值得注意）

icecream95 的 dump 列出 RK3588 的段表：

```
va 0x0-0x1000          .vector     (rw, cached, zero)
va 0x4000000-0x4094000 .shared     (rw, cached, coherent, shared)   ← 注意
va 0x1000000-0x1039000 .text       (rwx, cached, zero)             ← 注意 0x39000 大小
va 0xff0000-0x1000000  .code_redzone
va 0x1800000-0x180c000 .rodata
va 0x1c00000-0x1c40000 .brk
va 0x2000000-0x2040000 .stack
va 0x27f0000-0x2800000 .bss_redzone
va 0x2800000-0x2831000 .bss
va 0x2c00000-0x2c21000 .data       ← 有数据的 .data
va 0x3000000-0x3021000 .data       ← PROT，但是 0x21000 且有内容标记
```

**我们的**（692 日志 + 离线解析）：

| VA | 我们 | RK3588 | 差异 |
|---|---|---|---|
| `0x0` | r-- `0x9` | rw cached zero | 权限不同 |
| `0x1000000` | 64 KB | `0x39000`（228 KB） | **我们小 3.5 倍** |
| `0x2000000` | 256 KB | 256 KB | 同 |
| `0x2800000` | 256 KB | `0x31000` | 略不同 |
| `0x3000000` | **`0x40000` 全零，flags `0x2b`** | `0x21000`，有数据 | **不同** |
| `0x4000000` | `0x94000`，flags `0xc000001b`，**有 8 KB 数据** | `0x94000`，**data 0x2c8-0x2c8 即零数据** | **我们共享段有内容，它是空的** |

最后一行值得注意：**RK3588 的 `.shared` 段在镜像里是零长度数据**，全靠 kbase 运行期填
（`kbase_csf_update_firmware_memory`）。**我们的共享段有 8192 字节预置内容**（glb 控制块、
CSG 表、stream 表）。这意味着我们和 RK3588 的固件不是同一个 build 形态。

红区（redzone）：RK3588 有 `.code_redzone` 和 `.bss_redzone`，**我们没有**。
icecream95 说它们"看起来减少 GPU page fault 和 MCU 需要 reset 的频率"。

---

## 3. panthor 的启动顺序，我们的对照

`panthor_panthor_fw.c:1515-1540`：

```c
panthor_irq_request(... "job" ...)      // job 中断
panthor_hw_l2_power_on(ptdev);          // ★ 只开 L2
fw->vm = panthor_vm_create(0, SZ_4G, 0x04000000, 0x04000000);
panthor_fw_load(ptdev);                 // 装载段
panthor_vm_active(fw->vm);              // AS0 激活
panthor_fw_start(ptdev);                // MCU_CONTROL = AUTO
```

`panthor_gpu_l2_power_on()`（`:302-324`）：

```c
panthor_gpu_coherency_set(ptdev);       // GPU_COHERENCY_PROTOCOL
panthor_gpu_l2_config_set(ptdev);       // ASN hash（仅 arch>=11）
return panthor_gpu_power_on(ptdev, L2, 1, 20000);
```

**关键：panthor 在启动 MCU 前只开 L2 电源，不开 shader/tiler。**
这与我们一致（我们 `shader ready 0 tiler ready 0 l2 ready 0x1`）。

→ 所以"shader 没上电"**不是**我们独有的缺陷，panthor 也是这样。这条排除。

---

## 4. MFG 域的实际状态（已修正，撤回原结论）

我最初写"shader_present = 0x130013 → 4 个 shader core 全部上电失败 → 可能是根因"。**这条是错的，
撤回。** 复核后：

| 寄存器 | offset | 692 读数 | 含义 |
|---|---|---|---|
| `SHADER_PRESENT` | `0x100` | `0x130013` | **能力位图（常量）**，不是状态 |
| `SHADER_READY` | `0x140` | `0` | 状态：核没就绪 |
| `STACK_PRESENT` | `0xE00` | `0x33` | 另一个独立寄存器 |

我把 `SHADER_PRESENT` 的位当成了"哪些核上电"。**两者是不同寄存器，不能互推。**
而且 692 日志第 55 行打的 `shader 0x130013` 来自 `mrd(kShaderPresent)`（`hakios.mali610.c:4539`），
"shader ready 0" 来自 `0x140`（同一文件另一处）—— 这两个数在日志里同时出现，我读串了。

更关键的是：**这个问题 6 轮前已经查过并自我否定过**（`round-6.md` §8）。当时的结论是：

> Android 的 `SHADER_READY` 首次变 `0x130013` 发生在 **AUTO 之后 +65 条**记录。
> 也就是说 shader ready 是**固件启动的结果**，不是它的前提。

→ **"shader 没 ready" 是结果，不是原因。** 这条彻底排除。

### 4.1 那么 MFG7..MFG12 没上电呢？

同样是设计。`gpufreq_mt6895.h`：`GPUFREQ_PDCv2_ENABLE = 1` 时 vendor **只手工开 MFG1**，
MFG2..12 交给 PDC（注释原文：`let GPU DDK control MTCMOS itself`，`gpufreq_mt6895.c:801-802`）。

**但我在 `PDC-FINDING-2026-11-13.md` 里记了一个更值得查的事实**：
我们写了 17 个 PDC 使能寄存器，回读时 `sc9 = 0`、`st6 = 0`（bit0 没置上），
而 601/602 轮更严重 —— 写完 17 个之后 `0xF3C` 仍然是 `0x6`（只有 MFG0/MFG1），
`0 of twelve domains up`。

## 5. 下一轮真机试验的候选（按我的置信排序）

| 序 | 候选 | 依据 | 单变量 | 风险 |
|---|---|---|---|---|
| A | **只开 MFG1（MPU）就启动 MCU**，不碰 MFG2..12 | 与 vendor PDCv2 行为一致；先排除"我们多余的上电动作干扰了 PDC" | 是 | 低 |
| B | 查 `MFG6 ST5` 为何返回 `0x4000000d`（bit30 置位，其他域都是 `0xd`） | 唯一一个 bit30 异常的域 | 是 | 低 |
| C | 补 `.code_redzone` / `.bss_redzone` 两段（RK3588 有、我们没有） | icecream95 说它们减少 page fault | 是 | 中 |
| D | 共享段：我们的有 8 KB 预置内容，RK3588 是空的 —— 查这个差异是否影响启动 | 结构差异 | 否 | 低 |

我倾向先做 A 和 B（都是低风险、可单变量）。C 和 D 需要先搞清楚是否真的相关。

---

## 6. 来源

- [Fun with CSF firmware（RK3588 / G610，icecream95）](https://icecream95.gitlab.io/fun-with-csf-firmware.html)
- [panthor 上游源码（codebrowser）](https://codebrowser.dev/linux/linux/drivers/gpu/drm/panthor/panthor_fw.c.html)
- [Building Tyr in Rust: CSF architecture and booting the MCU（Collabora）](https://www.collabora.com/news-and-blog/blog/2026/05/14/building-tyr-in-rust-csf-architecture-and-booting-the-mcu)
- [Building Tyr part 3: Managing GPU memory（Collabora）](https://collabora.com/news-and-blog/blog/2026/08/26/building-tyr-in-rust-part-3-managing-gpu-memory)
- [Arm Lands Mali Gen10 Panthor Firmware（Phoronix）](https://www.phoronix.com/news/Arm-Mali-Panthor-Firmware-Git)
