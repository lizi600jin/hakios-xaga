# GPU 两轮对照的结果，以及一个被我读错的寄存器

2026-11-24 夜到 25 日凌晨。`boot-905` → `boot-908`。

## 一、先说我读错的地方

上一版 `GPU-SURVEY.md` 里我写过：

> `mfg0 ctrl 0x1112` → `ack=1 clk_dis=1 iso=1 rst_b=0`，这是问题所在。

**这个判断是错的。** Sol 逐行核对了厂商的 SPM 定义：

| 我说的 | 实际是 |
|---|---|
| `SPM+0xEBC` = MFG0_PWR_CON | `SPM+0xEBC` 是 **MFG1_PWR_CON（EMI1）** |
| `SPM+0xEB8` = MFG0 的控制字 | `SPM+0xEB8` 才是 **MFG0_PWR_CON** |
| 日志里的 `ack 1` = 电源 ACK | 是 **SRAM 掉电 ACK，bit 12** |

**正确的读数：**

```
SPM+0xEB8  MFG0_PWR_CON  0xc000224d   ← 两路电源 ACK 都置位、时钟未禁、隔离解除、复位释放
SPM+0xEB0  EMI1_PWR_CON  0xc000024d
SPM+0xEBC  MFG1_PWR_CON  0x1112
```

**MFG0 一切正常。** 我把 MFG1 的字当成了 MFG0 的，得出了相反的结论。

## 二、GPUEB 确实活着

```
gpueb pll en   0x15445   bit0 = 1   ← preloader 的使能位已置
gpueb pll ctrl 0x8000
mfgpll con0    0x15444
mfgpll con3    0x2
```

**preloader 确实把 GPUEB 启动了。** 这条线索到此为止——问题不在"GPUEB 没上电"。

## 三、GPU_ID 能读出来，只是要走完冷路径

907 那一轮的完整时间线：

```
21.837 s   gpu_id = 0                 ← 冷，走冷启动路径
           MFG1 拉到 0xc000000d
           切到参考时钟
35.084 s   gpu_id = 0xa8670004        ← 正确 ID
           之后才启用 300 MHz MFGPLL
```

**我上次说"GPU_ID 读不出来"是因为等得不够久。** 冷启动路径要走 13 秒。

之前那次热路径（`mali=6` 且 loader 已经上电）读到 `0xa8670004`，是另一回事。

## 四、mali=6 让退出变慢，这是确定的

| 配置 | RETURN 到 Android |
|---|---|
| `mali=0` | **27 秒** |
| `mali=6` | **5 分钟以上，有时失败** |

**验证方式**：`mali=0` loader + 907 payload（`boot-908`）：

```
00:09:20  hakid 就绪（内核 28 秒）
          RETURN
00:09:47  Android 就绪（+27 秒）
```

**所以 `mali610_quiesce()` 不是主要问题。** 只要不进完整启动序列，退出就正常。
`quiesce()` 在 `mali=6` 下执行时，GPU 已经被拉到 12 个域全开，收不回来。

## 五、Sol 对 quiesce 的诊断（我原来的写法有问题）

> 现有 `quiesce()` 在 `psci_smp_stop_others()` 之后执行，只凭 `gGpu` 已映射就写
> `MCU_CONTROL`，随后用常数覆盖三个共享保护 EN 寄存器。
>
> 应把常规硬件收尾**移到停核前的线程上下文**，先让 Mali worker 停下，
> 再按 bring-up 实际完成的阶段操作。保护位按**启动前快照**做掩码恢复。

**我的写法有三个错：**
1. 位置在停核之后（应该之前）
2. 用常数覆盖总线保护寄存器（应该按快照掩码恢复）
3. 没按实际完成的阶段操作（不管 bring-up 走到哪都做全套）

## 六、Sol 给的关域顺序

> MFG12→11→10→9→8→7→6→5→4→3→2→1 是合法顺序。只用于恢复本轮实际改过的域，
> 包括没拿到完整 ACK、却已经改写了电源请求的域。
>
> **MFG0 和 GPUEBPLL 留给原所有者。**

## 七、下一轮：只读探针

按 Sol 的建议：

> GPU 数据采集用**短窗口和真正的只读探针**。把它放在 `bus_protect_release()` 之前，
> 读完 SPM 和 PLL 就结束。`mali=1` 不适合作为只读对照，因为它会先执行上电和互连写入。

**设计**：探针只读 SPM 和 PLL 两组寄存器，不写任何东西，不进启动序列。
这样 GPU 完全没被动过，退出应该和 `mali=0` 一样快（27 秒级）。

**疑问待 Sol 确认**：探针阶段 MFG1 是关着的，所以只读 SPM + PLL、**不读 GPU_ID**，对吗？

## 八、当前状态

- 设备在 Android，`boot_a` = `boot-908.img`（`mali=0` loader + 907 payload）
- 退出路径正常（27 秒）
- 完整 HLOG 已存到 `/tmp/hlog-boot907.txt`（229 KB，2336 行，214 行 mali）

## 九、一个操作纪律

**RETURN 失败一次就停手，不要连续发。** 今晚我连着发了三次，每次都多跑一轮 XNU，
反而更慢。正确做法是：RETURN 后等 60 秒，如果 `hakid` 消失了但 `adb` 没出现，
那就是在 loader 阶段，**继续等**，不要重发。

判断是否真退出：`hakid` 连不上 = XNU 已退出。
`lsusb` 显示 `18d1:d00d` **不能**作为判断依据——loader 自己也在用这个 ID。
