# GPU 参考实现：RK3588 的 XNU 驱动与 xaga 的 Android 驱动

> 2026-11-15 01:15。用户提供了闭源 HakiOS 开发者的线索（"靠主线 Linux 的 panvk，然后转 Metal"），
> 顺藤摸瓜找到两个**已 device-verified 的开源实现**。这改变了 GPU 主线的整个判断。
> 每条结论都附来源，未核实的一律标「未验证」。

> Codex 收尾核查（2026-11-15）：这里的“实测”是参考项目作者的记录，尚未在本项目独立复现；第二个项目运行于 Android/Linux。AS_MEMATTR 已查到本地读回为 `0x00084c8d888d8f88`。INA_BITS 差异是候选，当前源码 `as_enable()` 本来就有刻意省略它以匹配 Android 轨迹的注释，不能写成已确认的新漏项。对“无 fault 是否排除取指故障”仍无结论。

---

## 1. 找到的两个实现

### 1.1 `Ken-u/Mali` —— Mali-G610 CSF 的 **XNU 内核驱动**

仓库描述（`https://api.github.com/repos/Ken-u/Mali` 原文）：
> **Mali-G610 (CSF) kernel driver for UniXNU: power, firmware, MMU, command streams and user client**

`README.md` 原文：
> Mali-G610 (CSF, Valhall, **arch 10.8**) kernel driver for UniXNU on RK3588. It powers the GPU,
> loads the CSF firmware into the MCU address space, builds AArch64 4 KiB GPU page tables and
> command-stream groups, and exposes a Panthor-style `IOUserClient` that the PanVK-based Metal
> stack in **MaliMetal** drives.

| 文件 | 内容 |
|---|---|
| `Mali.cpp`（177614 B） | 电源域、时钟、复位、MCU/固件、MMU 与页表（4 KiB 页，2 MiB block 从 reserved pool）、命令流组、tiler heap、syncobj/fence、buffer objects、`MaliService`/`MaliClient` |
| `mali_uapi.h` | 与用户态 libdrm 垫片共享的内核接口 |
| `mali_csffw.inc` | **生成的，不入库**（`tools/gen-fw-inc.py`） |
| `docs/notes.zh-CN.md`（348 行） | **中文 bring-up 日志**：硬件事实、M0–M4 里程碑、实测数据 |

**状态（README 原文）**：
> Verified on the RK3588 EVB10 V10 (see its porting record): the driver brings the GPU up, runs
> compute and fragment jobs, and composites the iPadOS lock screen through PanVK and metal2vulkan.

**已知缺口（README 原文）**：
> there is no DVFS and **no interrupt-driven completion yet (waits poll)**. GPU interrupts, error
> reporting to user space and more than eight groups are open items.

→ **它的「纯轮询」状态和我们一样**，所以这不是我们和它之间的差异点。

### 1.2 `bkaneko/xaga-mali-r54-port` —— **xaga 专用**的 Mali 驱动移植

`README.md` 原文：
> 把 MediaTek **Mali-G610 (MT6895)** 的 GPU 内核驱动从原厂 **r32p1** 升级到 **r54p1**，
> 并配套把 **GED** 和 **gpueb** 从 6.12 源码 backport 到 Linux 5.10 的完整记录。
> 目标设备：Redmi Note 11T Pro / POCO X4 GT（代号 **xaga**，MT6895，Mali-G610 MC6），
> 运行 **Android 17 / LineageOS 24**，内核 **5.10.269**。

这是 **Android 侧**（不是 XNU），但同一台机器、同一颗 GPU，而且含**真机固件**。

---

## 2. 🔴 决定性对比：`Ken-u/Mali` 的 M1 实测日志

`docs/notes.zh-CN.md:67-77`（`板子运行 mali1`，2026-10-28 15:05）：

```
firmware: header 0.3, 274432 bytes, entries end at 960
section 0..6 loaded（共 7 段 + 1 段 protected-mode 跳过），36864 B 页表，root 0x5e802000
AS0 enabled: transtab 0x5e802000 transcfg 0x420001c6 memattr 0x4c4c4c4c9c4c9f4c
MCU booted (MCU status 1, job irq raw 0x80000000)
CSF interface v1.1.0, features 0, 8 groups (stride 0xa0), instrumentation features 0x71, input 0x4002000 output 0x4006000
group 0: 8 streams (stride 0xc), suspend buffer 0x63c0
global interface config + ping ACKNOWLEDGED (req 0x10e ack 0x10e)
```

### 2.1 与我们逐项对比

| 项 | 我们 | `Ken-u/Mali` | 判定 |
|---|---|---|---|
| `MCU_STATUS` | `1` | `1` | **相同** |
| `JOB_INT` raw | **bit31 从不置位** | **`0x80000000`（bit31 置位）** | 🔴 **决定性差异** |
| `AS_TRANSCFG` | `0x42000006` | **`0x420001c6`** | 🔴 差 `0x1c0` |
| `AS_MEMATTR` | `0x00084c8d888d8f88` | **`0x4c4c4c4c9c4c9f4c`** | 待比 |
| 固件大小 | `258048` | `274432` | 不同 |
| 章节 | 「10 段全映射」 | **「7 段 + 1 段 protected-mode 跳过」** | 🔴 可疑 |
| 页表 | 4 KiB 粒 | 4 KiB 粒，`36864 B`，root 见上 | 待比 |
| `SHADER_READY` | `0` | M2b 时 `0 → 0x50005`（**FW 自己上电**） | ✅ 证实 `=0` 是启动前正常态 |

### 2.2 `AS_TRANSCFG` 的 `0x1c0`

`0x420001c6 - 0x42000006 = 0x1c0` = bits [8:6] = `111`。

之前子代理曾指出这是 `INA_BITS` 字段缺失。**现在有了实证对照**：跑通的实现写了它，我们没写。
**这是待验证的映射配置候选，尚无证据证明它导致本机启动失败。**

（待核实：`panthor_mmu_regs.h` 里 `AS_TRANSCFG_*` 的确切定义。子代理 `81edd5ba` 在做。）

### 2.3 页表 36864 字节

`36864 = 36 KiB = 9 × 4096`。如果 root 表 4 KiB，余下 8 页是下级表。
我们的是多少，需要查（`hakios.mali610.c` 的 pt 分配）。

---

## 3. 🔴 固件三份全不同

| 来源 | 大小 | sha256 |
|---|---|---|
| 我们内嵌（`hakios.mali610_fw.h:12` `MALI_FW_LENGTH 258048u`） | **258048** | `de22d2fc06bc8a859968499a6e609c27fe838be062bf5c97d92ea9f904f7229a` |
| `Ken-u/Mali`（RK3588 EVB10） | **274432** | `8e7c821a55ca1c345cc79f61243616dd25fc35b81f090946f302886d67712df5` |
| `bkaneko/xaga-mali-r54-port`（**xaga 真机**，UK 1.38） | **282624** | `9f67b2c3e4886f8d3f51ceac5170d0731bd44cb7c9ae5b094081e95f4028c7f8` |

`bkaneko/firmware/README.md` 原文：
> | `mali_csffw.bin` | 282,624 B | Mali CSF 固件，**UK 1.38**，配合 r54p1 用户态驱动 |
> 设备上的位置：`/vendor/firmware/mali_csffw.bin`
> 拉取方式：`adb shell 'su -c "cp /vendor/firmware/mali_csffw.bin /data/local/tmp/ ..."'`

**我们的 258048 从哪来的，目前未知。** 这是必须查清的——如果固件版本与驱动不匹配，
即使所有寄存器都对，MCU 也可能拒绝执行。

（待核实：我们的固件 header 版本号是多少？`Ken-u/Mali` 的是 `header 0.3`。）

---

## 4. 🔴 GPUEB 一手资料（`bkaneko/firmware/README.md`）

这是我们从没拿到过的 xaga GPUEB 硬事实：

> GPUEB 是 MT6895 上的 **RISC-V (RV33)** 协处理器，**GPU 的频率、电压、电源域全部由它控制**。
> AP 只能通过 IPI 发请求，固件决定实际频率。

固件头（**未加密，无 hash 无签名**）：
```
0x00: 88 16 88 58      magic
0x04: 2c ef 01 00      size
0x08: "tinysys-gpueb-RV33_A"
```
实际数据约 **96 KB**（`0x0–0x17000`），其余 `0xFF` 填充。

**GPU OPP 表**在固件偏移 `0x15d60`，6 项 × 24 字节：

| idx | freq (kHz) | volt | vsram | posdiv |
|---|---|---|---|---|
| 0 | **950000** | 78750 | 78750 | 2（固件永不选） |
| 1 | 880000 | 78750 | 75000 | 2（实际最高） |
| 2 | 800000 | 72500 | 75000 | 2 |
| 3 | 610000 | 65000 | 75000 | 2 |
| 4 | 430000 | 60000 | 75000 | 2 |
| 5 | 350000 | 57500 | 75000 | 3 |

电压单位 **0.1 mV**（950 MHz → 787.5 mV）。

→ 这记录了参考项目的 EB 控制策略；固件/OPP 存在本身不能证明 XNU 继承状态中所有 GPU 控制都只能由 GPUEB 更改。门控与当前固件活性仍待验证。

### 4.1 `bkaneko/docs/GPUEB.zh-CN.md` 的一条关键否证

原文：
> **"gpueb 是架构级死路"这个结论建立在一个错误前提上：EB 模式下 AP 侧根本没有 attach 那 12 个
> `pd_mfg` genpd，所以"pm_runtime resume 成功但 mfg1-12 全 off"是必然结果，不是"框架 trigger
> 不到"的证据。**

以及：
> 原厂就在 gpufreq **v2 + EB** 形态，不存在"把 v2 移植过来"这回事。

**注意**：这是 **Android/Linux** 侧的情况（AP 不 attach genpd，由 GPUEB 管）。
**不能直接套到我们的裸机 XNU 上** —— 我们**是**自己上电 12 域的（`:1741-1756` 遍历 MFG1-12），
且 `PWR_STATUS(0xF3C) = 0x3ffc` 证明 12 域全 up。

→ 所以这条资料**不改变**「我们 12 域已 up」的结论，但它解释了**为什么 Android 侧看到 mfg 全 off**。
两边的控制模型不同，这一点必须清楚。

---

## 5. 已确认无效的方向（不要再走）

| 方向 | 为什么无效 |
|---|---|
| 「`SHADER_READY=0` 说明电源有问题」 | `Ken-u/Mali` 实测 `SHADER_READY` 从 `0` 变 `0x50005`，是 **FW 自己上电**。`=0` 是启动前正常态 |
| 「12 域没上电」 | 我们 `PWR_STATUS(0xF3C)=0x3ffc` = MFG1-12 全 up（厂商判据 `MFG_1_12_PWR_MASK`） |
| 「MCU_STATUS=1 就是启动成功」 | `Ken-u/Mali` 的 status 也是 1，但它**同时**有 `job irq raw 0x80000000`。判据是 GLOBAL_IF |
| 「等更久就能起来」 | XNU `delay(1)` 是1微秒；百万次名义1秒，NOPDC实录从spin0到结束约9.67秒，已超过panthor的1秒窗口 |

---

## 6. 下一步（优先级排序）

1. **核实 `AS_TRANSCFG` 的 `0x1c0`** —— 这是最强的候选。子代理 `81edd5ba` 在做
2. **核实固件版本** —— header 版本号对比，我们的 258048 来源
3. **核实章节数差异** —— 「7+1 跳过」vs「10 全映射」
4. **对比 `AS_MEMATTR`**
5. 上述任何一项确认后，做一次最小改动实验

---

## 7. 待扫的开发者仓库（进行中）

用户提供：`mytiantian001`、`bkaneko`、`zhaoqing412`、`woaphone`、`Ken-u`

已知有价值的：
- `bkaneko/panvk-mtk-driver` (10★) —— panvk kbase (MTK Mali) Android Vulkan
- `mytiantian001/Mu-Silicium` —— **Project Mu UEFI Ports for MediaTek MT6895 Family**
- `zhaoqing412/xaga-mt6895-k6.12` —— xaga 的 ACK 6.12
- `zhaoqing412/fenrir` —— 控制 MTK bootchain
- `Ken-u/UniXNU-evb10` —— RK3588 设备仓库（Mali 合成 50 fps 实测）
- `UniXNU` 组织 —— 官方组件目录

子代理 `bda216ff` 在扫。
