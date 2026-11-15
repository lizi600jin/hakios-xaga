# trial 696（witness 单变量）结果

> 2026-11-15 13:22，DSH。**实机执行，已恢复原厂。**
> 镜像 md5 `b750fd86b73affbeb318377b6b8c779a`，
> sha256 `48c7c042917087e4c4fade613edec47f7bef589f2ceaf42cf9dcbdfe7660556a`。

---

## 0. 一句话结果

**32 字节的最小 Cortex-M stub 也没有执行任何一条 `str`。**
复位向量确认被改成了 `0x1000001`（patch 生效），但三次读回共享页全是哨兵 `0xff5aa5f0`，
两个 marker（`0x11` / `0x22`）**都没出现**。

**同时：哨兵本身读得回来** —— 共享页映射、CPU 读写、cache flush 整条链都是好的。

→ **问题不在固件、不在段布局、不在 PROT 分支，而在「MCU 取指本身没有发生」。**

---

## 1. 日志原文（`trial696-full.log`）

```
[   34.827563]: mali: witness: vector was sp 0x2040000 reset 0x10018ed
[   34.827594]: mali: witness: code at 0x1000000, reset vector now 0x1000001
[   44.492408]: mali: witness: read1 0xff5aa5f0 0 read2 0xff5aa5f0 0 read3 0xff5aa5f0 0 trial 696
[   44.493581]: mali: witness: verdict trial 696 -- no completed first store observed (not an instruction trace)
```

| 行 | 含义 |
|---|---|
| `:2918` | 原始向量：`sp 0x2040000`，`reset 0x10018ed`（与固件一致） |
| `:2919` | **patch 生效**：向量已改为 `0x1000001`（Thumb 位 + 段 1 基址） |
| `:3237` | 三次读回全 `0xff5aa5f0`，**零 marker** |
| `:3238` | 驱动自己的判读：**没有观察到完成的第一笔 store**（明确写了这不是指令追踪） |

---

## 2. 同时能证明的事（这些是好消息）

| 项 | 值 | 含义 |
|---|---|---|
| 哨兵能读回 | `0xff5aa5f0` × 3 | 共享页**映射正确**，CPU 读写通 |
| 共享段布局 | `VA 0x4000000-0x4094000 size 0x94000` | 映射范围正确 |
| GLB 结构 | `version 0xff5aa5f0 features 0 input_va 0x4002000 output_va 0x4003000` | 与固件声明一致 |
| GLB 其余 | `groups 8 stride 0xa0 perfcnt 0 instr 0x71` | **与官方固件逐字相同** |
| span 校验 | `input_span_ok 1 output_span_ok 1` | 输入输出区间都在段内 |
| AS0 | `AS0 enabled: root 0x1a6d70000` | 地址空间激活成功 |
| 页表走查 | `coverage: every section fully mapped` | 10 段全覆盖 |
| MMU 中断 | `mmu irq 0`（启动期间） | **没有任何总线事务发生** |
| MMU fault | `faultstatus 0 faultaddress 0` | 无 fault |

---

## 3. 与 trial 695 的对比（单变量）

| 项 | 695 | 696 |
|---|---|---|
| `mali_witness` | `0` | **`1`** |
| kernel sha256 | `18bb68da…` | `18bb68da…` **同一文件** |
| payload sha256 | `0cdc1c25…` | `0cdc1c25…` **逐字节相同** |
| `MCU status` | `1` | `1` |
| `job irq` | `0` | `0` |
| `mmu irq`（启动期间） | `0` | `0` |
| `L2 up` | `trans 0 active 0 ready 0x1` | 同 |
| `shader ready` | `0` | `0` |
| 结果 | 固件不执行 | **连 32 字节 stub 也不执行** |

**695 与 696 在 MCU 侧的表现完全一致**，唯一区别是这次用了最小程序，
排除了「固件太复杂所以出错」这一整类解释。

---

## 4. 这排除掉的方向（可以正式划掉）

| 方向 | 为什么排除 |
|---|---|
| 固件本身有问题 | 32 字节自己的代码也不跑 |
| 段布局 / entry table 解析错 | 同上；且 GLB 结构解出来与官方固件逐字相同 |
| PROT 段处理错 | witness 完全不碰 PROT 段，仍不跑 |
| 共享页映射错 / cache 不一致 | 哨兵能读回，说明映射与刷新都对 |
| AS0 未激活 | `AS0 enabled: root 0x1a6d70000` |
| 页表覆盖不全 | `coverage: every section fully mapped` |
| MMU fault 导致静默停止 | `faultstatus 0`，`mmu irq 0`（连 fault 都没有） |
| 复位向量没改成功 | `reset vector now 0x1000001`，patch 明确生效 |

---

## 5. 🔴 撤回四条推断（Codex 2026-11-15 13:30 指出，全部成立）

### 5.1 撤回一：「未见 marker ⇒ 一条 str 都没执行」不成立

**错在把「没观察到」当成「没发生」。**

正确的表述是：

> 读回时**没有观察到已完成的 marker store**。

可能的情形包括但不限于：store 没执行、执行了但写到了别处、
写了但 CPU 这一侧的视图没看到（cache/coherency）。

witness 的 marker 只有一个「完成态」，没有中间态，
所以它**无法区分这几类**。这一点驱动日志自己写对了
（`not an instruction trace`），是我在报告里把它推强了。

### 5.2 撤回二：「CPU 回读自己写的哨兵 ⇒ MCU 映射/cache 全通」不成立

哨兵 `0xff5aa5f0` 是 **CPU 自己写进去、CPU 自己读回来**的。
这只能证明 **CPU 侧的映射与可见性**，**不能**证明：

- MCU 那侧的页表项对 MCU 是可用的；
- MCU 能看到同一份内存（coherency）；
- MCU 写入后 CPU 能看到。

**共享页路径、取指/存储路径、可见性路径，一条都不能排除。**

### 5.3 撤回三：`mmu irq 0` 不能证明「没有取指」

我原来写「正在取指的核必然产生取指事务，实测一次都没有」。**这个推理是错的。**

`panthor_mmu.c:657-661` 原文：

```c
static u32 panthor_mmu_fault_mask(struct panthor_device *ptdev, u32 value)
{
	/* Bits 16 to 31 mean REQ_COMPLETE. */
	return value & GENMASK(15, 0);
}
```

**`bit[31:16]` = `REQ_COMPLETE`，是「地址空间命令完成」事件，不是「每次取指的事务计数」。**
低位 `bit[15:0]` 才是 fault。

所以 `mmu irq = 0` 只能说明**没有 fault、没有命令完成事件**，
**不能**说明没有取指发生。

### 5.4 撤回四：不能由此把 GPUEB 升为唯一/最强原因

`gpueb: not mapped` 只说明 **AP 侧没有映射那几个探针**，
**不说明 GPUEB 没有运行**，也不说明它没有被别人请求过。

我把它和 witness 结果绑在一起提升为「最强候选」，**这个提升没有依据**，撤回。

---

## 5.5 现在还剩什么（诚实的边界）

**已经排除的（这些仍然成立）：**

| 方向 | 依据 |
|---|---|
| 固件本身有 bug | 32 字节自己的代码也没产生可观察 marker |
| 段布局 / entry table 解析错 | GLB 结构解出来与官方固件逐字相同 |
| 复位向量没改成功 | `reset vector now 0x1000001`，patch 明确生效 |
| AS0 未激活 | `AS0 enabled: root 0x1a6d70000` |
| 页表覆盖不全 | `coverage: every section fully mapped` |
| MMU fault 导致静默停止 | `faultstatus 0`（**只看 bit[15:0]，这个结论是有效的**） |

**未排除的：**

- 共享页映射（MCU 侧）与 coherency；
- 取指路径；
- MCU 存储路径；
- marker 的可见性时序；
- MCU 是否被真正唤醒 / 是否有时钟（**没有新证据**）；
- GPUEB 状态（**没有新证据**）。

**「MCU 没取指」这个结论，目前只有间接支持，没有直接证据。**

## 6. 顺带：Android 卡住是可复现的

| | trial 695 | trial 696 |
|---|---|---|
| `surfaceflinger` 崩溃次数 | 38 | **38** |
| `boot_completed` | 未置位 | 未置位 |
| 差异 | 18 行只读诊断 | **只有 `mali_witness` 一个参数** |

**两次症状完全相同，而 696 相对 695 只改了 witness。**
所以这不是某个诊断导致的，是 **XNU 返回 Android 时 GPU 状态恢复不完整**。

用户照片显示屏幕停在 loader 打完 `starting Android` 之后 ——
**loader 已交接，是 Android 图形栈起不来。**

驱动源码里 `mali_power_down()` 的注释（`mtkloader.c:6908-6914`）已经记录过这风险：

> "Leaving it released was a boot loop — the run came back once, then reset before adb:
> domains off but SRAM still connected is a combination Android's gpufreq never creates,
> and the sleep controller reports it as a domain that will not settle."

**这条与 GPU/MCU 启动失败是两个独立问题**，但每次刷机都会撞上。

---

## 7. 恢复记录

| 步骤 | 结果 |
|---|---|
| 刷 trial 696 | `OKAY`，0.783s |
| XNU 运行 | ~150s，13:13:28 → 13:16:53 返回 adb |
| 取日志 | `HLOG @ 0x9f8f8000`，`len 0x72e8a = 470666`，`sum @64 = 0x24da28c` **校验一致** |
| 刷回原厂 | `backup/boot_a_stock_real.img`，`OKAY`，1.900s |
| Android 恢复 | `boot_completed = 1`，`SurfaceFlinger` 正常 |
| `boot_a` 最终 | **`a63b2e35a7c2522cb24a6a10332b9bc8`**（= 原厂） |

---

## 8. 下一步唯一待决问题

**MCU 为什么一次取指都不发？** 只剩两个候选，都能设计成单变量：

| 候选 | 实验 | 成本 |
|---|---|---|
| **GPUEB 未请求** | 按已还原的协议发 `CMD_POWER_CONTROL`，再启动 MCU | 中（要写 IPI 代码） |
| **核未上电/未时钟** | 读 MTK 侧 MFG 时钟与电源状态寄存器，确认是否到核 | 低（只读） |

**建议先做「只读确认」**：在 `MCU_CONTROL=AUTO` 前后读 MFG 时钟相关的
PLL/分频器实际输出使能位，确认 GPU 核心时钟是否真的在跑。
这与 witness 结果直接相关且零风险。

---

## 9. Android 崩溃的根因（Codex 要求回传，2026-11-15 13:35）

Codex 要求提供 tombstone / logcat / dmesg 路径。已取得。

### 9.1 tombstone

```
docs/headset-wip/raw/trial696-sf-crash/tombstone_04    533041 B
```

对应关系：`Timestamp: 2026-09-21 08:32:23.307626463+0800`，
即 trial 696 返回 Android 后启动阶段的崩溃。

### 9.2 崩溃原因（不只是「次数」）

```
signal 6 (SIGABRT), code -1 (SI_QUEUE)
Abort message: 'no suitable EGLConfig found, giving up
  (format: 1, vendor: Android, version: 1.4 Android META-EGL,
   extensions: EGL_ANDROID_front_buffer_auto_refresh
   EGL_ANDROID_get_native_client_buffer ... ,
   Client API: OpenGL_ES)'
```

调用栈：

```
#03 SkiaGLRenderEngine::chooseEglConfig(void*, int, bool)+600
#04 SkiaGLRenderEngine::create(RenderEngineCreationArgs const&)+156
#05 RenderEngineThreaded::threadMain(...)+540
```

**根因是 `chooseEglConfig` 找不到可用的 EGL 配置**，不是随机崩溃。

### 9.3 这意味着什么（重要修正）

**EGL 配置选择依赖 GPU 驱动（vendor kbase）能正常工作。**
`no suitable EGLConfig` 说明 **Android 侧的 GPU 驱动此时不可用**。

**所以 Android 起不来的直接原因是 GPU 不可用，而不是与 GPU 无关的独立问题。**
这与我 §6 里写的「两个独立问题」判断相反，**那条撤回**。

### 9.4 但 XNU 读到的 GPU 状态与 Android 一致

对照实测（Android 恢复后）：

```
XNU 日志 :2833  mali: shader 0x130013 tiler 0x1 l2 0x1 stack 0x33 thread_max 0x800
Android 侧      [Common-Status] DVFSState: 0x0002, ShaderPresent: 0x00130013
```

**`0x130013` == `0x00130013`，完全一致。**

Android 侧其它读数：

```
[Common-Status] Dual Buck: True, GPUEB Support: On
[GPU-Power]     PowerCount: 0, CG: 0, MTCMOS: 0, BUCK: 0
/proc/gpufreqv2/mfgsys_power_control:  MFGSYS is DBG_POWER_OFF
```

`DBG_POWER_OFF` 与 `PowerCount: 0` 是**空闲态正常值**（GPU 没在用时）。

**结论：XNU 对 GPU 寄存器的读取是正确的，shader 枚举与 Android 一致。**
所以「核完全没电」这个方向**不成立** —— 至少 shader 侧是枚举得到的。

### 9.5 固件来源确认（正面证据）

```
/vendor/firmware/mali_csffw.bin  md5 ddef4ef1802903d3f54259da07605229
我们打包进内核的那份          md5 ddef4ef1802903d3f54259da07605229
sha256 (两者)  de22d2fc06bc8a859968499a6e609c27fe838be062bf5c97d92ea9f904f7229a
```

**字节完全相同。我们用的就是这台手机真实的厂商固件，不是别的来源。**

> 我自己一度误判：把 `de22d2fc…`（sha256 前 8 位）当成了「不同的 md5」，
> 说「厂商固件与我们不同」。**这条是错的，两个 md5 完全一致。** 已更正。

---

## 10. 又一处我的错误：参数长度并没有变化

我在单变量证明里写过「`mali_trial=695` → `696` 让字符串短 1 字节，
后续内容整体前移」。**这是错的**（Codex 2026-11-15 13:11 指出，我核实确认）。

实测两版的 `argument_bytes`：

| 版本 | `actual_loader_args` 长度 | build.json 里的 `argument_bytes` |
|---|---|---|
| trial 695 | **463** | **463** |
| trial 696 | **463** | **463** |

**两者完全相同。`695` 和 `696` 都是三位数，替换不改长度。**

### 我为什么搞错

我在对比 `loader.bin` 时，取到的是**编译器放进二进制里的字符串副本**，
它们长度是 `464 / 463 / 476`（476 那份尾部粘着相邻的 `"framebuffer "`）。
我把 464 那份当成了「完整参数」，于是推出「696 变成 463，短一字节」。

**实际完整参数一直是 463**，464 那份是带尾部空格的变体。

### 连带撤回

- 「701 也要三位数才等长」→ **不成立**，任何三位数 trial 号都等长；
- 「位移导致逐字节差异」→ 位移确实存在（字符串池重排），但**不是我说的那个原因**。

Codex 的验收方式更可靠：**以「同源同 flags、只改参数后的确定性重建」为准**，
而不是要求字面替换后只差两字节。他已独立重建两份 loader 并逐字节复现
（695 `8e33c679…`、696 `9707b7e4…`），与保存产物一致。

### 另一个我需要接受的限定

Codex 指出：**payload 相同不能单独证明 loader 代码相同** ——
loader 自身也是会执行的代码。这条成立，是我此前论证里没覆盖的缺口。
他本次用独立重建补足了这个缺口。
