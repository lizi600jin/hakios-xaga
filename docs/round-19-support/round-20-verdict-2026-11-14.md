# Round 20（trial 693 / 694）：两次都没拿到结果

**2026-11-13 21:12 – 23:55。手机已恢复原厂并核验。**

---

## 1. 做了什么

目标：加读第三个判据字 `0x4001040`（`stream0.control.features`），验证固件是否执行到
`0x1002ff8` 那段填表循环。

**驱动改动**（`hakios.mali610.c`，纯只读）：

```c
/* 0x4001040 = stream0.control.features
 * CSG0 在 CSF_GROUP_CONTROL_OFFSET(0x1000)，stream 在 CSF_STREAM_CONTROL_OFFSET(0x40)
 * csg_idx=0 / cs_idx=0 -> shared+0x1040
 * 映像初值 0；固件从 0x1002ff8 写入的值恒有 0x70000 位 -> 必非零 */
printf("mali: discriminate trial %u: version %#x perfcnt %#x"
       " stream0_features %#x read_ok %u\n",
       gMaliTrial, version, perfcnt_size, stream0_features, read_ok);
```

`read_ok` 是为了区分"读到 0"和"没读"——自查边界条件时加的。

**安全清单**：三处 `continue` 都不在 `load_firmware` 循环里；未碰禁读地址；只读不写；
边界 `>= 0x1044`；编译通过（`hakios.mali610.o` 已生成）；格式串确认已进内核
（`strings hakios-kernel | grep "discriminate trial"` 命中）。

---

## 2. trial 693：内核一行都没输出

| 项 | 693 | 692（对照） |
|---|---|---|
| logbook 非零字节 | **1201** | 469246 |
| `[ts]` 内核行 | **0** | 264 条 mali 行 |
| loader 行 | 11 条，走完全程并进入 XNU | 完整 |
| boot args | 有，`mali_trial=693` | 有，`mali_trial=692` |

**根因已查明**：`loader_return_after=70`。

`board.h:784` 默认值是 `70`，而 trial 692 用的是 `150`。693 用的是默认值。
但我当时改的是 **`board.h` 源文件**，而 `make boot-image` **没有重编 loader**——
二进制里仍然是 `70`（`strings mtkloader.bin` 确认）。

→ **这个诊断后来被 694 推翻了**，见下。

---

## 3. trial 694：崩得比 693 更早

改成 150 并强制 `touch mtk/loader/mtkloader.c` 重编，确认二进制里是
`loader_return_after=150`、`mali_trial=694`，然后跑。

结果：

| 项 | 694 | 693 |
|---|---|---|
| HLOG 记录数 | **43** | 695 |
| logbook 非零字节 | **60** | 1201 |
| loader 行 | **只有 1 条**：`power: about to release the SRAM isolation` | 11 条 |
| boot args 行 | **没有** | 有 |
| 是否进入 XNU | **否** | 是 |

**694 的 loader 崩在 `mali_aoc_up()` 之前**，连 boot args 都没打印。

而两次的源码差异只有两个常量（`XNU_RETURN_AFTER` 70→150、`mali_trial` 693→694），
loader 二进制只差 40 字节。

→ **同一份源码不可能造成这种差异。694 这次开机本身失败了，与代码无关。**

---

## 4. 手机状态：已恢复

- 两次都刷入 `boot_a`，两次都需要人工回到 fastboot（用户手动操作）
- 已用 `fastboot flash boot_a backup/boot_a_stock_real.img` 恢复
- **核验：`boot_a` MD5 = `a63b2e35a7c2522cb24a6a10332b9bc8`（原厂）**
- `sys.boot_completed=1`

---

## 5. 这一轮的结论

**没有拿到任何 GPU 观测数据。** 两次运行都在 loader 阶段或更早失败，都没走到内核的
mali 驱动。所以：

| 问题 | 状态 |
|---|---|
| `stream0_features @0x4001040` 是 0 还是非零 | **仍然未知** |
| 固件是否执行到 `0x1002ff8` | **仍然未知** |
| 版本字、perfcnt 的判据 | 不变（692 的数据仍然有效） |

---

## 6. 下一轮要注意的

1. **`board.h` 改了必须强制重编 loader**。`make boot-image` 依赖 loader 二进制的时间戳，
   改头文件不会触发重编。必须 `touch mtk/loader/mtkloader.c` 或删掉 `.bin`。
   **这个坑我踩了一次**（693 实际用的是 70，不是我以为的 150）。

2. **loader 大小与 692 不同**：692 的 loader 是 48208 字节，693/694 是 60456/60496。
   差了 12 KB。说明 692 的构建路径与本次不同（可能是不同的 `DEVICE` 或不同的编译选项）。
   **接手时先搞清楚 692 是怎么构建的**，否则无法复现它的条件。

3. **694 的失败原因未定**。不是代码差异。可能是：
   - 刷机后首次冷启动的时序问题
   - 前一次 693 的失败在 SoC 里留下了状态（两次之间没有完整断电）
   - USB/电源的偶发问题
   → 下一轮如果重试，**建议先完整断电重启一次**再刷。

4. **需要人工兜底的次数**：这两轮都出现了"刷完起不来、需要手动回 fastboot"。
   在无人值守的情况下这个流程不安全。
