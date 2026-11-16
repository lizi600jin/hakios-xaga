# UFS 调查 01：xaga 的存储缺口与三个可参考项目

> 2026-11-16 21:20，DSH。**仅主机读取源码与文档。未改代码、未碰设备。**

---

## 0. 结论速览

| # | 结论 |
|---|---|
| **1** | **xaga 现在没有磁盘** —— `ufs`/`ufshc`/`ans2` 全部未编译，根是 4 MiB ramdisk |
| **2** | **iOS 系统镜像已就位** —— `xnusys` 分区 2.68 GB，里面是 2.32 GiB 的 `system.apfs` |
| **3** | **现有 `hakios.ufs.c` 走不通** —— 它要求 Qualcomm UEFI 的 `UFSDxe` 常驻，MT6895 没有 UEFI |
| **4** | **K20（sweet）与 redwood（hackios）都只接管、不初始化** —— 依赖 bootloader 已把 UFS 配好 |
| **5** | **xaga 的 LK 从 UFS 启动**（Android 就跑在它上面）**→ 这条前提大概率满足** |

---

## 1. 当前状态：没有磁盘

### 1.1 编译事实

**从 `logs/mt6895-cpu-baseline-20261116/kernel-src` 的 BUILD 实测 16 个 `hakios*.o`：**

```
apfs_check  audio  image  input  mali610  mt6895_cpufreq  mt6895_headset
mt6895_keys  mt6895_pmif  mt6895_usb  net  nvt  platform  sparse  stubs  version
```

**🔴 `ufs.o`、`ufshc.o`、`ans2.o` 都不在。**

### 1.2 config 原文（`mt6895.config`）

| 行 | 内容 |
|---|---|
| **10-14** | 注释解释为什么没有 UFS |
| **29** | `# CONFIG_UFS is not set` |
| **39** | `# CONFIG_HAKIOS_UFS is not set` |

**注释原文（第 13-14 行）：**

> "**Until the loader grows a UFS host and the kernel driver can own the controller from reset,
> a first boot has no disk: the root is the ramdisk the payload carries and nothing else.**"

### 1.3 根文件系统实况

| 项 | 值 |
|---|---|
| payload 里的 ramdisk | `ref-ramdisk.bin`，**4194304 B** |
| APFS 卷名 | **`redwood.apfs`**（又是移植痕迹） |
| 里面的可执行 | **4 个** |
| `usr/libexec/xpcproxy` | ❌ **不存在** |
| `System/Library/Frameworks` | ❌ **不存在** |
| `bin/sh` | ❌ **不存在** |

**→ 这就是为什么 logbook 里 25 个服务 spawn 了却没有后续。**

---

## 2. iOS 系统镜像其实已经在设备上

| 项 | 值 |
|---|---|
| 分区 | `xnusys` → `/dev/block/sdc88` |
| 大小 | **2685370368 B**（2.5 GiB） |
| `userland/system.apfs` | **2492203008 B**（2.32 GiB） |
| **条目数** | **62357**（16547 目录 + 45809 文件） |

**关键路径都在：**

| 路径 | 项数 |
|---|---|
| `System/Library/CoreServices` | **951** |
| `System/Library/Frameworks` | **2690** |
| `System/Library/PrivateFrameworks` | **32836** |
| `usr/libexec/xpcproxy` | ✅ |
| `usr/lib/dyld` | ✅ |

**→ 镜像不缺，缺的是挂载它的驱动。**

---

## 3. 现有 `hakios.ufs.c` 为什么走不通

**头注释原文（`bsd/dev/hakios.ufs.c:2-6`）：**

> "**There is no UFS driver here, and none is needed.** UEFI never exits on this port:
> Qualcomm's own UFS driver (**UFSDxe**) has brought up the controller, its PHY, its clocks and
> power, and **stays resident** … this driver writes a request … and core 0 reads them with that
> driver's ReadBlocks … **Nothing in this kernel touches the storage controller.**"

**这是 Qualcomm/redwood 的设计：UEFI 常驻，内核通过共享页发请求。**

**🔴 MT6895（xaga）没有 UEFI** —— LK 直接起 XNU。**所以这套 IPC 没有对端。**

**而这个驱动 70167 B 的代码全是 IPC 逻辑，没有一行控制器操作。**

---

## 4. 🔴 决定性发现：K20 与 redwood 都只接管、不初始化

### 4.1 K20（`dipa-studio/ios-sweet`，Redmi Note 10 Pro）

**`shim/ufs.c` 266 行，注释原文：**

> "**Milestone 5 probe: can we talk to the internal UFS that ABL booted from?**
> Runs with the MMU off … **Dumps the UFSHCI registers ABL left behind** and, if the host is
> enabled and the link is up, points the transfer request list at our own buffers and issues
> READ-ONLY SCSI commands on LUN 0. **Nothing is ever written to the device.**"

**它做的事：**

| 步 | 内容 |
|---|---|
| 1 | 读 `CAP`/`VER`/`IS`/`HCS`/`HCE`/`UTRLBA`/`UTRLDBR` |
| 2 | **检查 `(hce & 1)`、`(hcs & HCS_DP)`、`(hcs & HCS_UTRLRDY)`** —— 不满足就打印 `host not enabled/ready: stop` 并返回 |
| 3 | 停表（`UTRLRSR=0`）→ 指向自己的链表 → 启动 |
| 4 | 只发 READ-ONLY SCSI |

**🔴 全文没有任何 `clk`/`clock`/`power`/`phy`/`reset`/`regulator` 初始化代码**（我 grep 过，0 命中）。

**寄存器布局：标准 JEDEC UFSHCI（JESD223）。**

### 4.2 redwood（`oosexgang/hackios-xnu-kernel`，POCO X5 Pro）

**`xnu/files/bsd/dev/redwood_ufshc.c`（1333 行）头注释原文：**

> "**Nothing else of the controller is changed: no reset, no link startup, no UIC command, no
> interrupt enable, aggregation, clock or power setting**; the task management list is not touched;
> IS and UTRLCNR are only read."

**它做的也是接管链表（UTRLRSR/UTRLBA），依赖 UEFI 已经配好控制器。**

**基址 `0x1d84000`（`ufshc@1d84000`）。**

### 4.3 三个项目的共同模式

| 项目 | SoC | UFS 基址 | 谁初始化 UFS | 内核做什么 |
|---|---|---|---|---|
| **sweet** | Snapdragon 680 | `0x01d84000` | **ABL（bootloader）** | 接管链表，只读 SCSI |
| **redwood** | SD 778G | `0x1d84000` | **UEFI 的 UFSDxe** | 接管链表，只读 SCSI |
| **xaga（现在）** | MT6895 | `0x112b0000` | **没人** | 无（驱动未编译） |

**→ 共同点：都不自己做 PHY/clock/power 初始化，都依赖前一级。**

---

## 5. xaga 的关键优势：LK 是从 UFS 启动的

**Android 就跑在这台手机的 UFS 上。**
**`lk_a.img`（LK）必须能从 UFS 读到 `boot` 分区才能起 Android。**

**→ 「bootloader 已把 UFS 配好」这条前提，xaga 大概率满足。**

**🔴 但我没有验证过 LK 在跳转到 XNU 时是否保留了 UFS 控制器的状态** —— 这是关键未知。

---

## 6. xaga UFS 的硬件事实（live DT 行 8733）

```dts
ufshci@112b0000 {
    compatible = "mediatek,mt8183-ufshci";
    reg = <0x00 0x112b0000 0x00 0x2300>;
    interrupts = <0x00 0x89 0x04 0x00>;
    clocks = <...10 个 clock...>;
    clock-names = "phy_tx_sym", "phy_rx_sym0", "phy_rx_sym1", "unipro_sys",
                  "unipro_sap", "phy_sap", "ufs_ck", "ufs_fde_ck", "ufs_fde", "ufs";
    power-domains = <0x04 0x02>;
    vcc-supply = <0xcf>;
    vccq-supply = <0xd0>;
    resets = <0xd1 0x00 0xd2 0x00 0xd2 0x01>;
    reset-names = "unipro_rst", "crypto_rst", "hci_rst";
    mediatek,ufs-qos;
};
```

| 项 | 值 |
|---|---|
| **基址** | **`0x112b0000`** |
| **大小** | **`0x2300`** |
| 中断 | **137**（`0x89`） |
| **需要 10 个 clock** | phy 3 个 + unipro 2 个 + phy_sap + ufs_ck + fde 3 个 |
| **需要 1 个 power-domain** | `<0x04 0x02>` |
| **需要 3 个 reset** | unipro / crypto / hci |
| **需要 2 个 regulator** | vcc（`0xcf`）、vccq（`0xd0`） |

**🔴 `board.h` 里记的也是 `0x112b0000`，两处一致。**

---

## 7. 两条可行路线（我的分析，待 Codex 判）

### 路线 A：LK 保留了 UFS 状态 → 抄 K20 的做法

**做法**：写一个 300 行左右的只读接管驱动（像 `sweet/shim/ufs.c`），
检查 `HCE`/`HCS` 就绪后接管链表，发 SCSI READ。

| 优点 | 缺点 |
|---|---|
| 代码量小（K20 只有 266 行） | **强依赖「LK 保留了状态」这个未验证前提** |
| 不需要碰 clock/power/PHY | 若 LK 已关掉 UFS，则完全无效 |
| 风险可控（先只读） | |

**第一步应该是验证这个前提** —— 但**需要一个能读寄存器的 XNU 内核**，
而这又回到了「要先编一个探针」。

### 路线 B：自己做初始化

**做法**：按 DT 的 10 个 clock + 1 power-domain + 3 reset + 2 regulator，
自己把 UFS 从复位状态拉起来。

| 优点 | 缺点 |
|---|---|
| 不依赖 LK | **工作量巨大** |
| 彻底 | 需要 clock 框架、regulator 框架、power-domain 框架——**XNU 侧全都没有** |
| | 风险高（配错会挂） |

**→ 我倾向先走 A，把 B 留作后备。**

---

## 8. 立即可做的下一步（不需要 Codex）

**写一个只读 UFS 探针**，结构类似刚做完的 `hakios.mt6895_cpufreq.c`：

| 项 | 内容 |
|---|---|
| 映射 | `0x112b0000`，大小 `0x2300`（只读） |
| 读 | `CAP`/`VER`/`IS`/`HCS`/`HCE`/`UTRLBA`/`UTRLBAU`/`UTRLDBR`/`UTRLRSR` |
| 判断 | HCE bit0、HCS bit0(DP)、HCS bit1(UTRLRDY)、HCS bit3(UCRDY) |
| 输出 | 原始值 + 就绪判断 |
| **绝不写** | 不写 UTRLRSR/UTRLBA/UTRLDBR，不发 SCSI |
| 参数 | 类似 `mtk_ufs_probe=1` |

**这个探针能回答最关键的问题：LK 跳转后 UFS 控制器还在不在。**

**成本低、风险零（纯读）、信息量大。**

---

## 9. 还需要 Codex 判定的

| # | 问题 |
|---|---|
| 1 | 路线 A 还是 B |
| 2 | 只读探针的寄存器白名单是否够（是否需要读 UIC 相关） |
| 3 | 若 HCS 不就绪，下一步是查 clock 还是查 LK 的行为 |
| 4 | **K20 的 266 行能否直接改基址复用**（它用标准 UFSHCI，理论上可以） |
| 5 | APFS vs HFS+：K20 用 HFS+，hackios 用 APFS。**xaga 的 `system.apfs` 是 APFS，需要 hackios 那套** |

---

## 10. 边界

**本轮只做**：读 `sweet`/`hackios`/`mu` 源码与 README、读 live DT、查编译产物、列 APFS 内容。

**没有做**：改任何代码、构建、碰设备、读写寄存器。

**「LK 保留了 UFS 状态」是**未验证的推测**，基于「Android 从 UFS 启动」这一点。**
