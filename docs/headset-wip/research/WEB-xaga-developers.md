# WEB-xaga-developers —— xaga / MT6895 开发者仓库扫荡

> 扫描时间：2026-11-15 02:04–02:50（Asia/Shanghai）
> 克隆目录：`/tmp/xagadev/`（不污染项目目录）
> 产出副本：`docs/headset-wip/raw/xaga-devs/`
> 结构化清单：`docs/headset-wip/findings/XAGA-DEV-FINDINGS.json`

**证据约定**

- `[源码]` = 我读到仓库内文件内容，给出 `仓库/路径:行号` 或 `raw.githubusercontent.com` URL
- `[字节]` = 我对二进制做的实测（strings / 偏移扫描 / 反汇编上下文）
- `[自述]` = 仓库 README 的说法，未独立复核
- `[未验证]` = 明确标注的未核实项
- **跨模型边界**：本次扫到的全是 **Android/Linux 侧**或**别人家的 XNU 移植**。每节末尾单列「能不能跨到裸机 XNU」，不允许把 Linux 的资源管理模型当成我们的。

---

## 0. 一句话结论

三条：

1. **GPU 找到一条此前 19 轮没走过的具体怀疑**：`bkaneko` 与上游 panthor 都显示 **`JOB_INT_MASK` 必须在写 `MCU_CONTROL=AUTO` 之前清零**；而我们的驱动在 `start_mcu()` 之前由 `gpu_irq_mask_prestart()` 把 `JOB_INT_MASK` 写成 `0xffffffff`，轮询 `JOB_INT_STATUS` 的整个循环里**掩码一直是全屏蔽**，直到 `booted=true` 之后才 `mwr(kJobIntMask, 0)`（`hakios.mali610.c:3132` / `:3976`）。这与我们观察到的「`MCU_STATUS=1` 而 `JOB_INT_STATUS` bit31 永不置位」形态**完全一致**。这不能证明 GPU 能起来，但它是**唯一一个能同时解释「MCU 进了 ENABLED」和「GLOBAL_IF 永不可见」**的已定位代码差异。见 §3.1。
2. **`ASN_HASH` 这条线在我们这块芯片上可以正式关掉**：G610 是 architecture **10**（`GPU_ID=0xa8670004`，`(gpu_id>>28)&0xf = 10`），而上游 `panthor_gpu_l2_config_set()` 有硬门槛 `GPU_ARCH_MAJOR < 11 → 直接 return`（`panthor_gpu.c:76-79`）。我们自己的注释也已经记录了「写入后读回 bit24 不变、三个 hash 字全 0」。见 §3.2。
3. **耳机侧没有任何实机寄存器 dump**，全网（GitHub code search 268–1244 条命中）只有驱动源码与头文件，没有一份「真机读出的 0x26a9/0x26b6 值」。但我们拿到了**裸机可用的 SPMI 参考实现**（Mu-Silicium `KeypadDeviceLib.c`，纯 MMIO、无中断、无内核），以及**6.12 口径的 ACCDET 完整寄存器映射已确认与 5.10 逐字节同源**。见 §4。

---

## 1. 扫过的对象清单

### 1.1 优先级 1（用户点名，全量枚举）

| 仓库 | ★ | 语言 | 最近更新 | 描述 | 相关度 |
|---|---|---|---|---|---|
| `BkaNeko/panvk-mtk-driver` | 10 | Shell | 2026-11-13 | panvk kbase (MTK Mali) Android Vulkan 驱动构建 | **A** |
| `BkaNeko/xaga-mali-r54-port` | 12 | C | 2026-11-10 | Mali-G610 r32p1→r54p1 + GED 6.12 port | **A**（已确认，本次只补增量） |
| `BkaNeko/xaga-skyward` | 2 | Rust | 2026-11-14 | Rust 写的 Android recovery（fork） | C |
| `BkaNeko/android_kernel_modules_xiaomi_mt6895-6.12` | 0 | — | 2026-11-04 | 6.12 内核模块（fork） | **A**（含完整 gpueb 驱动树） |
| `BkaNeko/android_kernel_xiaomi_mt6895` | 0 | — | 2026-10-22 | fork，无描述 | B |
| `BkaNeko/ABK` | 0 | Kotlin | 2026-07-05 | AnyBase Kernel 构建器（fork） | C |
| `zhaoqing412/xaga-mt6895-k6.12` | 0 | C | 2026-11-08 | xaga 的 ACK 6.12 | **A**（dts + accdet 6.12 口径） |
| `zhaoqing412/fenrir` | 0 | — | 2026-09-11 | fork 自 `R0rt1z2/fenrir`，MTK bootchain 控制 | B |
| `zhaoqing412/android_kernel_modules_xiaomi_mt6895-6.12` | 1 | C | 2026-08-25 | 6.12 模块（**非 fork，含完整 gpueb 源码树**） | **A** |
| `zhaoqing412/android_kernel_oddo_mt6895` | 0 | C | 2026-08-07 | 无描述 | C |
| `zhaoqing412/android_kernel_xiaomi_mt6895_oops` | 0 | C | 2026-08-07 | 无描述 | C |
| `zhaoqing412/android_vendor_xiaomi_pearl` | 0 | — | 2026-08-01 | blob（fork） | C |
| `zhaoqing412/android_device_xiaomi_pearl` | 0 | — | 2026-08-01 | pearl 设备树（fork，WIP） | C |
| `zhaoqing412/android_kernel_xiaomi_mt6895` | 0 | — | 2026-08-01 | fork | C |
| `zhaoqing412/Droidspaces-rootfs-KDE-builder` | 0 | — | 2026-08-26 | 容器 rootfs | 无关 |
| `zhaoqing412/xiaomi_pad_system_patch_additional_module` | 0 | Smali | 2026-07-14 | 小米平板补丁 | 无关 |
| `mytiantian001/Mu-Silicium`（branch `pearl`） | 1 | Assembly | 2026-11-08 | Project Mu UEFI Ports for MediaTek MT6895 Family | **A**（裸机 SPMI 参考） |
| `mytiantian001/VIVO-OTA-Tracker` | 56 | Python | 2026-11-13 | vivo OTA 链接抓取 | 无关 |
| `mytiantian001/VIVO-OTA-Tracker-Android` | 49 | Kotlin | 2026-11-13 | 同上（Android） | 无关 |
| `mytiantian001/android_device_xiaomi_pearl_twrp` | 47 | C++ | 2026-11-11 | pearl TWRP 设备树 | C |
| `mytiantian001/iPadOS-nabu` | 19 | Python | 2026-11-12 | nabu(SM8150) 的 iPadOS+XNU（fork） | B（跨 SoC 的 XNU 方法） |
| `mytiantian001/UniXNU-nabu` | 0 | Shell | 2026-11-14 | nabu 的 UniXNU（fork） | B |
| `mytiantian001/panvk-mtk-driver` | 1 | — | 2026-09-26 | 上述 panvk 的 fork | C |
| 其余 19 个 | — | — | — | 各类 TWRP/GSI/AVD/KernelSU 构建（`yuechu`/`zircon`/`malachite`/`ricefish`/`thales` 等） | 无关 |
| `woaphone/xnu-mtk` | 0 | C | 2026-11-06 | MTK 平台通用 XNU 移植（分支 `main`/`yunluo`/`rothko`） | **B+**（同路线，无 GPU） |
| `woaphone/UniXNU-rothko` | 2 | C++ | 2026-11-11 | K70 Ultra(MT6989) 的 UniXNU 设备仓库 | **B+**（GPU 明确 planned） |
| `woaphone/UniXNU-mt6789-common` | 1 | — | 2026-11-07 | MT6789 公共支持（WIP） | C |
| `woaphone/UniXNU-mt6989-common` | 0 | — | 2026-11-06 | MT6989 公共支持 | C |
| `woaphone/UniXNU-yunluo` | 1 | — | 2026-11-07 | Redmi Pad 设备仓库 | C |
| `woaphone/mtk-mod-tee-nogz` | 6 | Python | 2026-10-17 | patch TEE 关 GenieZone，**含 xaga profile** | **B** |
| `woaphone/mtk_disable_lk_dtbo_validation` | 3 | Python | 2026-10-26 | 关 LK/bl2_ext 的 dtbo 校验 | B |
| `woaphone/mtk-disable-gz-oldv6-patchBL2-withoutPL` | 3 | Python | 2026-09-29 | NoGZ/EL2 boot-kit（bl2_ext 成员替换） | B |
| `woaphone/sprig-fix-bootable-mt6989` | 7 | C | 2026-10-14 | bl2_ext 相关 fork | C |
| `woaphone/CVE-2021-28664-PoC` | 2 | C | 2026-10-03 | 无关 PoC | 无关 |
| `woaphone/android_*_rothko` / `KernelSU` / `ABK` / `DSU-Sideloader-Plus` / `clash` / `v2rayL` / `NootedRed` / `n1ghtshade` / `edk2*` / `hwoc` / `DouYinSparkFlow` / `uboot-mt7621` / `android_kernel_mtk6771` / `compile_android_driver` / `WhyNotWin11` / `EUservextend` / `renegade-project.org` / `-` | 0 | — | — | 无关或陈旧（2020–2023） | 无关 |

> `woaphone` 结论：**34 个仓库里没有一个 GPU 仓库**。它的价值全在 bootchain（NoGZ/EL2/dtbo 校验）与 XNU-on-MTK 的**方法学**，不在 Mali。

### 1.2 优先级 2（从 star / topic / 搜索扩展出来的）

| 仓库 | ★ | 语言 | 为什么算进来 | 相关度 |
|---|---|---|---|---|
| **`ShuiLan114514/SLNeko-XNU`**（branch `Xaga`） | 16 | C | **同一台 xaga 的 HakiOS XNU 移植**，含 XNU 侧 Mali 驱动源码与 loader GPU 探针 | **A** |
| **`wxcvm/panvk-pgkm10`** | 0 | Shell | 同 SoC（MT6895/G610 MC6）的 panvk 实证，含 AFBC/sync 系列补丁 | **A-** |
| **`xiaomi-mediatek-devs/mt6895_eng_preloaders`** | 18 | — | **签名的 xaga 工程 preloader（492,172 B）** | **A** |
| `R0rt1z2/fenrir` | 616 | Python | MTK bootchain EL3 PoC（原始上游） | B |
| `xiaomi-mt6895-devs/fenrir` | 0 | Python | 上述的 fork | C |
| `xiaomi-mediatek-devs/mt6895_eng_preloaders`（同上） | 18 | — | 已列 | A |
| `UniXNU/UniXNU` | 2890 | Python | UniXNU 主仓（GPU 适配流程文档） | B |
| `ChallengeThePeak/Silicium-Rothko` | 0 | Assembly | rothko 的 UEFI（无 GPU） | C |
| `OnePlusOSS/android_kernel_5.10_oneplus_mt6895` | 35 | C | 同 SoC 上游 OSS 内核（ACE Race），可作为 mt6368/accdet 第二来源 | B |
| `MT6895-Mainline/linux` | 54 | C | 已知，任务边界外（未扫内容） | — |
| `LieppOS/LieppOS-ulefone-gq5012bf1-research` | 0 | — | 含 `reverse-engineering/audio/mt6369/registers.tsv`（寄存器→宏映射表） | B |
| `xiaomi-mediatek-devs/mt6895_eng_preloaders` | 18 | — | 已列 | A |

**不可达 / 未取内容**：无 404、无私有仓库。所有列出的仓库均实际读取成功。

### 1.3 明确「无收获」的方向（避免重复劳动）

| 方向 | 结果 |
|---|---|
| `topic:xaga` 全量（8 个仓库） | 只有 ESK/xaga dump/hakios 自身，**无 GPU 或耳机新料** |
| `search/repositories?q=mt6895`（125 个） | 有效新料只有 `SLNeko-XNU`、`panvk-pgkm10`、`eng_preloaders`、`LieppOS` |
| `woaphone` 全部 34 仓 | 无 Mali / 无 ACCDET |
| `Mu-Silicium` 全树（2272 文件） | **无任何 GPU 初始化**（详见 §5） |
| GitHub code search：`0x26b6 accdet`(328) / `ACCDET_IRQ+0x26a9`(268) / `cur_AB accdet`(524) / `mt6368 accdet`(1244) | 全部是驱动源码/头文件，**零实机 dump** |
| `ASN_HASH mali`(1960) / `GPU_L2_CONFIG`(188) / `CSF_CONFIG mali`(1512) / `JOB_INT_GLOBAL_IF`(377) | 全部是驱动源码，**无第三方逆向笔记** |

---

## 2. 扫到的文本素材（已拷入 `raw/xaga-devs/`）

```
raw/xaga-devs/
├── gpueb-ipi/
│   ├── GPUEB.zh-CN.md                      (bkaneko 的 GPUEB 全文，837 行)
│   ├── README-firmware.md                  (固件格式/OPP 表硬事实)
│   ├── gpueb-6.12-to-5.10.patch            (28 KB，backport 全 diff)
│   └── gpueb_init-from-6.12-patch.c        (从 patch 抽出的 gpueb_init.c 300 行)
├── mt6368-612/
│   ├── accdet-612.c                        (6.12 口径 mt6368-accdet.c, 98,345 B)
│   ├── accdet-510-vs-612.diff              (与 5.10 的完整 diff，733 行)
│   ├── mt6368-612.dtsi                     (6.12 的 mt6368 dtsi, 295 行)
│   ├── mt6368.dtsi                         (同上，raw 副本)
│   └── xaga-mt6895-612.dtsi                (6.12 的 xaga 板级 dtsi, 636 行)
├── slneko-xnu/
│   ├── mtkmali.h                           (裸机 Mali 上电序列 + 每条依据)
│   ├── gpu-probe.sh                        (loader GPU 探针的刷机/取日志流程)
│   └── README-SLNeko-Xaga.md               (同机 xaga 移植的状态表)
├── panvk/
│   ├── README-panvk-mtk-driver.md
│   ├── README-panvk-pgkm10.md
│   ├── panthor_fw.c                        (上游 CSF 启动全流程)
│   └── panthor_gpu_asn_hash.c              (ASN_HASH 门槛实现)
├── preloader/
│   └── preloader_xaga_eng.strings.txt      (4,745 条 strings)
├── lk-zhao/
│   ├── README-fenrir.md
│   ├── README-mtk-mod-tee-nogz.md
│   └── README-UniXNU-rothko.md
└── mtkmali/
    ├── KeypadDeviceLib.c                   (Mu-Silicium 裸机 SPMI 读实现)
    └── MemoryMapLib.c                      (pearl/MT6895 完整内存图)
```

未拷二进制（`preloader_xaga_eng.bin` 492 KB、`mali_csffw.bin` 等留在 `/tmp/xagadev/`）。

---

## 3. A 级发现 · GPU

### 3.1 🔴 `JOB_INT_MASK` 在 MCU 启动全程保持全屏蔽（最值得立刻验的一条）

**我们现在的代码**

- `hakios.mali610.c:3127-3135` `gpu_irq_mask_prestart()`：
  ```c
  mwr(kJobIntClear, 0xffffffffu);
  mwr(kJobIntMask, 0xffffffffu);     /* ← 全屏蔽 */
  ```
- 调用点：`hakios.mali610.c:4641`（在 `gpu_l2_power_on()` 之前，远早于 `start_mcu()`）。
- 轮询循环：`hakios.mali610.c:3896-3931`
  ```c
  for (spins = 0; spins < 1000000; spins++) {
      uint32_t stat = mrd(kJobIntStatus);   /* ← 读的是被 mask 之后的视图 */
      ...
      if (stat & kJobGlobalIf) { booted = true; break; }
  }
  ```
- 解屏蔽在**成功分支里**：`hakios.mali610.c:3975-3976`
  ```c
  mwr(kJobIntClear, kJobGlobalIf);
  mwr(kJobIntMask, 0u);                 /* ← 只有 booted==true 才执行 */
  ```

**上游 panthor 的顺序（对照）**

- `drivers/gpu/drm/panthor/panthor_fw.c:1136-1140`：
  ```c
  ptdev->fw->booted = false;
  panthor_irq_enable_events(&ptdev->fw->irq, ~0);   /* mask |= ~0 */
  panthor_irq_resume(&ptdev->fw->irq);              /* 写 INT_CLEAR / INT_MASK */
  gpu_write(fw->iomem, MCU_CONTROL, MCU_CONTROL_AUTO);
  ```
- `panthor_device.h:644-656` `panthor_irq_enable_events()`：`pirq->mask |= mask;` 若状态是 ACTIVE 立即 `gpu_write(pirq->iomem, INT_MASK, pirq->mask)`。
- `panthor_device.h:636-642` `panthor_irq_resume()`：`gpu_write(INT_CLEAR, mask)` + `gpu_write(INT_MASK, mask)`。
- 即：**panthor 在写 AUTO 之前就把 `JOB_INT_MASK` 写成 `~0`（全 0，全部解屏蔽）**。
- 其超时判定同样读被 mask 的视图（`panthor_fw.c:1146`）：`gpu_read(fw->irq.iomem, INT_STAT) & JOB_INT_GLOBAL_IF` —— 前提是 mask 已经放开。
- 寄存器偏移一致：`INT_RAWSTAT 0x0 / INT_CLEAR 0x4 / INT_MASK 0x8 / INT_STAT 0xc`（`panthor_device.h:571-574`），与我们的 `kJobIntRaw=0x1000 / kJobIntClear=0x1004 / kJobIntMask=0x1008 / kJobIntStatus=0x100c`（`hakios.mali610.c:364`）逐项对应。

**为什么这条能解释我们的四个症状**

| 症状 | 全屏蔽能否解释 |
|---|---|
| `MCU_STATUS(0x704)=1` (ENABLED) | 无关，mask 不影响 MCU 状态 |
| `JOB_INT_STATUS(0x100c)` bit31 从不置位 | ✅ **正是 mask 的作用**：事件仍会锁进 `RAWSTAT(0x1000)`，但被 mask 掉后 `STATUS(0x100c)` 读不到 |
| 不 HALT 不 FATAL | ✅ 固件可能早就启动并进了 idle，但我们看不到它举手，因此不会走 halt 路径 |
| `GPU_FAULT_STATUS(0x3C)=0`、`PWR_STATUS=0x3ffc` | 无关（都正常） |

**立刻可做的最小实验（不改逻辑，只改一行）**

在 `start_mcu()` 写 `MCU_CONTROL=AUTO` **之前**加：
```c
mwr(kJobIntClear, 0xffffffffu);
mwr(kJobIntMask, 0u);          /* 解屏蔽，再启动 */
```
并在轮询里**同时**打印 `kJobIntRaw(0x1000)` 与 `kJobIntStatus(0x100c)`。

**判据**

- 若 `RAWSTAT` bit31 已置而 `STATUS` 不置 → 就是 mask，实锤。
- 若 `RAWSTAT` 与 `STATUS` 都不置 → mask 不是原因，回到 §3.3。
- 我们已经有一条现成的观测支持这条假设：`hakios.mali610.c:3618-3621` 的注释里写着「masking does not stop an event being latched in GPU_IRQ_RAWSTAT, it only keeps it out of the masked view」——同一句话对 `JOB_INT` 同样成立，但代码没有对 `JOB_INT` 应用它。

> ⚠️ **未验证**：本次没有跑真机，也没有重编驱动。这是**代码级推断**，不是实测结论。它之所以排第一，是因为它是 19 轮里唯一一个「读的是一个我们主动关掉的视图」的定位差异。

**跨模型边界**：✅ 完全可跨。这是 Mali CSF 架构的寄存器语义（mask/status 关系），与 OS 无关，裸机同样成立。

---

### 3.2 `ASN_HASH` / `GPU_L2_CONFIG`：可以正式结案

| 项 | 事实 | 出处 |
|---|---|---|
| `GPU_L2_CONFIG` 偏移 | `0x48` | `panthor_gpu_regs.h:60` |
| bit24 = `GPU_L2_CONFIG_ASN_HASH_ENABLE` | `BIT(24)` | `panthor_gpu_regs.h:61` |
| `GPU_ASN_HASH(n)` 偏移 | `0x2C0 + n*4`（3 个） | `panthor_gpu_regs.h:109` |
| 写入函数 | `panthor_gpu_l2_config_set()` | `panthor_gpu.c:66-87` |
| **硬门槛** | `if (GPU_ARCH_MAJOR(gpu_id) < 11) { drm_err(...); return; }` | `panthor_gpu.c:76-79` |
| 调用位置 | `panthor_gpu_coherency_set()` 之后、**L2 上电之前** | `panthor_gpu.c:319-321` |
| 我们的 `GPU_ID` | `0xa8670004` → `arch_major = (0xa8670004>>28)&0xf = **10**` | `docs/GPU-ROUND-1.md:173` + 本地解码 |
| 我们自己的实测记录 | 「写入后 `l2_config` 读回不变、bit24 仍 0、三个 hash 字仍 0」 | `hakios.mali610.c:3200-3204` 注释 |

**结论**：G610（architecture 10）上 `GPU_ASN_HASH` 寄存器**不存在**，`GPU_L2_CONFIG` bit24 **不可写**。这条线索在 19 轮里作为「最可能」的假设（`hakios.mali610.c:430-438`）应当降级为**已排除**，除非有独立证据表明 xaga 的 G610 变体与上游不同（`[未验证]`，但我们的读回已经两次为 0）。

**跨模型边界**：✅ 可跨，纯硬件事实。

---

### 3.3 `GPU_COHERENCY_PROTOCOL` —— 上游在 L2 上电**之前**写，这一项我们无法确认是否有做

- `panthor_gpu.c:60-64`：`panthor_gpu_coherency_set()` → `gpu_write(GPU_COHERENCY_PROTOCOL, ptdev->gpu_info.selected_coherency)`
- 寄存器：`0x304`，取值 `ACE_LITE=0 / ACE=1 / NONE=31`（`panthor_gpu_regs.h:112-117`）
- 调用顺序：`panthor_gpu.c:319-321`，注释明写「Set the desired coherency mode and **L2 config before the power up of L2**」
- 我们侧：`hakios.mali610.c:428` 有 `#define kCoherencyNone 31u`，但**本次未读到写入点**（`[未验证]`：需要 grep `kCoherencyNone` 的使用处）。

**下一步**：`grep -n "kCoherencyNone" /home/lizi/IOS/hakios-mtk/mtk/mt6895/drivers/gpu/hakios.mali610.c`，确认是否在 `gpu_l2_power_on()` 之前写入 `0x304`。若没有写而硬件默认不是 NONE，L2 可能是 incoherent 的，固件第一次取指就可能拿不到自己刚写的数据。

**跨模型边界**：✅ 可跨。

---

### 3.4 CSF 启动的完整判据链（上游，可直接照做自检）

| 阶段 | 上游做什么 | 出处 |
|---|---|---|
| 1. 固件装载 | 校验 `magic`（`CSF_FW_BINARY_HEADER_MAGIC`）、`major == MAX`、`hdr.size <= iter.size`（否则 "Firmware image is truncated"） | `panthor_fw.c:803-860` |
| 2. 段遍历 | 逐条 entry：`IFACE` → `panthor_fw_load_section_entry()`；`BUILD_INFO_METADATA` → 读 build info；`CONFIG/FUTF_TEST/TRACE_BUFFER/TIMELINE_METADATA` → **跳过**；其他非 optional → 报错 | `panthor_fw.c:755-800` |
| 3. 共享区 | 必须找到 `shared_section`，否则 `"Shared interface region not found"` | `panthor_fw.c:856-860` |
| 4. L2 | `panthor_hw_l2_power_on()` 在装载字节**之前** | `panthor_fw.c:1521`（我们注释里已引用） |
| 5. 中断 | `enable_events(~0)` + `resume()` 写 `INT_MASK`，**然后**写 `AUTO` | `panthor_fw.c:1136-1140` |
| 6. 等待 | 1000 ms 内 `booted` 或 `INT_STAT & GLOBAL_IF`，否则读 `MCU_STATUS` 报 `"Failed to boot MCU (status=%s)"` | `panthor_fw.c:1142-1168` |
| 7. 判 halt | `MCU_STATUS == HALT` **且** `GLB_STATE_GET(ack) == GLB_STATE_HALT` | `panthor_fw.c:1178-1188` |
| 8. 全局接口 | `core_en_mask = shader_present`；`poweroff_timer/progress_timer/idle_timer`；`ack_irq_mask = GLB_CFG_ALLOC_EN|GLB_PING|GLB_CFG_PROGRESS_TIMER|GLB_CFG_POWEROFF_TIMER|GLB_IDLE_EN|GLB_IDLE`；`req |= GLB_IDLE_EN|GLB_COUNTER_EN`；toggle req/ack | `panthor_fw.c:1061-1090` |
| 9. MCU 置 ACTIVE | `panthor_fw_update_reqs(glb_iface, req, GLB_STATE(GLB_STATE_ACTIVE), GLB_STATE_MASK)` + `panthor_fw_ring_doorbell(CSF_GLB_DOORBELL_ID)` | `panthor_fw.c:1214-1223` |

**我们卡在第 6 步的判据上**，而第 5 步的 mask 没做（§3.1）。第 9 步的 `GLB_STATE_ACTIVE` 是固件从 HALT 出来的必要动作——如果我们从未走到第 9 步（因为第 6 步就失败），固件可能一直停在 `GLB_STATE_HALT`，表现为「ENABLED 但不干活」。这与 panthor 的 halt 语义吻合：`MCU_STATUS=HALT` 需要**两个条件同时**成立，只读 `MCU_STATUS` 会漏判。

**跨模型边界**：✅ 可跨（CSF 架构行为）。

---

### 3.5 `bkaneko` 的 GPUEB 文档：新增的硬事实（合并现有两份）

已有结论（不重复验证）之外，本次从 `docs/GPUEB.zh-CN.md`（837 行）与 `firmware/README.md` 里提取的**新增可操作项**：

| # | 事实 | 出处（文件:行） |
|---|---|---|
| 1 | **GPUEB 固件不写 SPM MTCMOS**。三份固件（stock / 950MHz / 13T）里对 SPM 基址 `0x1C001xxx` 的引用**各只有一处，且是"读"**（`lui x10,0x1c001` → `c.jal <读函数>` → `sw` 压栈给 printf） | `docs/GPUEB.zh-CN.md:305-330` |
| 2 | 三份固件都靠 **SPM 信号量**与他人协调，字符串 `[F]%s: acquire SPM_SEMA_M4 timeout` | `:332-336` |
| 3 | **13T 的 471 KB 不是一份固件**：主镜像 127.3 KB + cert1/cert2 + **322.4 KB 加密 xfile**（熵 7.999）+ cert1'/cert2' | `:575-600` |
| 4 | xfile 容器格式完整解出：`0x88168858` 主 magic / `0x89168958` 二级 magic（`=512`）/ `0x58901690` 载荷头（24 B，正文 329584 = 20599×16）/ 尾部 SHA-1 明文 | `:660-700` |
| 5 | **xaga 的 LK 里没有 `SYS_GPUEB_XFILE`**（13T 有）。原厂 lk.img md5 `1aa9b95d` 复核，`SYS_GPUEB_XFILE` 出现 **0 次**，`SYS_DPM_XFILE` 3 次 | `:700-712` |
| 6 | **13T 固件有共享内存尺寸校验**，错误串 `"shared memory size: 0x%x < status data size: 0x%x"` 在 13T 出现 1 次、stock 0 次；xaga 给 `0x1000`，13T 要 `0x4000` | `:640-660` |
| 7 | xaga 无 gpueb logger：`[GPU/EB][INFO][__gpueb_pdrv_probe:245]: gpueb no logger support.` → **固件的报错传不到 AP** | `:665-670` |
| 8 | E0 实机结论：**健康设备上 MFG1–12 就是全关**（`0x00001112`），`MFG0=0xc000224d`。三次快照（空闲/UI忙/忙后3s）完全一致 | `:480-500` |
| 9 | `ioremap+readl` 读 `0x13000000`/`0x13000140` 在健康设备上**恒为 0**（XPU/DevAPC 或 clock 未开）→ 该读法**无鉴别力** | `:505-520` |
| 10 | 原厂 kbase 导出符号里**没有** `dev_pm_domain_attach_by_name`、**没有** `genpd_*`；给 MFG 上电唯一手段是 `gpufreq_power_control()` | `:465-478` |

**这 10 条里对我们最有用的三条**：

- **#1 + #2**：GPUEB 不是「替 AP 给 MFG 上电的那个人」。它只做 DVFS，电源通过信号量请求别人。所以「12 个 `pd_mfg` 没 attach」不构成 GPU 不启动的解释——**这条线可以彻底放下**，把精力放回 kbase/CSF 侧（与 §3.1 的指向一致）。
- **#9**：任何「读 GPU 寄存器为 0 ⇒ 没上电」的推论在这块板子上都无效。我们自己如果还有类似的判据，要一并作废。
- **#7**：GPUEB 侧的错误永远看不到 → 不要指望从 GPUEB 拿诊断，诊断只能在 GPU 侧（`JOB_INT_RAWSTAT`/`MCU_STATUS`/`GLB_STATE`）。

**跨模型边界**：⚠️ 部分。`#1/#2/#9/#10` 是硬件/固件事实，可跨。`#3–#6` 是 13T 固件移植的结论，与裸机 XNU 无关（我们不用移植 13T 固件）。`#8` 是在 Android 侧用 `spmdump.ko` 得到的，**裸机我们没有 runtime PM，读数应一致但未验证**。

---

### 3.6 GPUEB 驱动的真实结构（从 6.12 源码树，比 patch 完整）

`zhaoqing412/android_kernel_modules_xiaomi_mt6895-6.12` 里有**完整**的 `drivers/gpu/mediatek/gpueb/` 源码树（不是 patch）：

```
gpueb.c 309   gpueb_ipi.c 581   gpueb_reserved_mem.c 242
gpueb_timesync.c 295   gpueb_logger.c 260   gpueb_gpumpu.c 83
gpueb_hwvoter_dbg.c 219   gpueb_debug.c 498
ghpm_wrapper.c 159   ghpm_swwa.c 482
include/{gpueb_ipi.h 42, gpueb_common.h 115, gpueb_helper.h 40,
         gpueb_reserved_mem.h 29, gpueb_logger.h 43}
```

**新发现的机制（对裸机有直接意义）**

1. **MFG0 上电走 ATF SMC，不是 AP 写 SPM**
   `ghpm_swwa.c:124-131`、`:171-178`、`:225`：
   ```c
   arm_smccc_smc(
       MTK_SIP_KERNEL_GPUEB_CONTROL,   /* a0 */
       GHPM_SWWA_SMP_OP_MFG0_ON,       /* a1 */
       0,0,0,0,0, &res);
   ```
   注释写得很清楚：`/* smc to power on mfg0 -> reset eb -> warm boot -> eb resume */`
   SMC 号：`MTK_SIP_SMC_CMD(0x530)`（`NothingOSS/.../mtk_sip_svc.h:160-161`），
   `MTK_SIP_SMC_CMD(fn)` = `ARM_SMCCC_CALL_VAL(FAST_CALL, MTK_SIP_SMC_CONVENTION, OWNER_SIP, fn)`（`:21-23`）。
   → **AP 侧（含裸机）给 MFG0 上电的正规入口是 EL3 的 SIP 0x530 / op=6**，不是直接写 `0x1C001EB8`。
2. **`gpueb_ctrl()` = SMC + 等 IPI 回执两段**
   `ghpm_wrapper.c:74-98`：先 `ghpm_ctrl(power, off_state)`，再 `wait_gpueb(event)`；任一失败就报错。
3. **`g_ghpm_support == 0` 时直接返回 0（假装成功）**
   `ghpm_wrapper.c:78-80` / `:64-66`。这正是 `docs/GPUEB.zh-CN.md:§4.4-2` 说的「`ret=0` 不说明硬件动作」的代码根因。
4. **MFG0 状态由两个 ACK 位判**：`MFG0_PWR_ACK_BITS = BIT(31)|BIT(30)`（`gpueb_common.h:15-17`），`mfg0_pwr_sta()` 要求两位都置（`gpueb_init.c:155-160`）。

**跨模型边界**：⚠️ 部分。`#1/#2` 是硬件事实，**裸机完全可用**（`smc #0` 在 EL1 或 EL2 都能发，取决于 ATF 配置）。`#3/#4` 是 Linux 驱动逻辑，只能借语义。

---

### 3.7 `panvk-mtk-driver` 到底做了什么（10★，用户点名）

**它不是一个 GPU 驱动**，而是把 Mesa 的 `panvk`（Vulkan）接到 MTK 的 **kbase 内核驱动**上，替代原厂 `vulkan.mali.so`。核心是 userspace 适配，**与 CSF MCU 启动无关**。

| 问题 | 修法 | 出处 |
|---|---|---|
| MTK gralloc 报 `DRM_FORMAT_MOD_INVALID`，实际分配 ARM AFBC(32x8\|SPARSE\|SPLIT\|YTR) | 回退到 `DRM_FORMAT_MOD_ARM_AFBC(...)` 而非 LINEAR；AFBC body 4096 对齐 | `patches/panvk_mtk.patch:321-410` |
| SF RenderEngine 崩溃 `DEVICE_LOST` | `copy_sync_payloads` 在 kbase 后端置 NULL，改 `QueueSubmit2` | `:648-789` |
| **作业超时挂起（kick 竞态）** | 去掉 userspace doorbell 快路径，**总是 kick 调度器**；超时 10s→120s | `:172-250` |
| `MEM_ALLOC_EX` ENOTTY | 回退 classic `MEM_ALLOC`（注释：某些 MTK 内核声称 CSF uAPI ≥1.9 却没实现） | `:1-56` |
| 队列优先级只有 medium | 放开 HIGH/REALTIME | 同上 |

**对我们的意义（间接但明确）**

- `patches/panvk_mtk.patch:32-34` 那条注释：**「Some MediaTek kernels report a CSF uAPI >= 1.9 yet do not implement MEM_ALLOC_EX」** → xaga 的 kbase 是 r32p1，CSF uAPI 1.18（README 明写），**但特性表与实际实现不一致**。这提示我们：不要用「版本号」推断硬件能力。
- 「doorbell 会在 active→idle 切换时丢作业」+「总是 kick」 → **CSF 的 doorbell 不是可靠唤醒**。这与 §3.4 第 9 步「`ring_doorbell(CSF_GLB_DOORBELL_ID)` 是让 MCU 看 `req` 的唯一手段」形成对照：**门铃要响，但响之前 `req` 必须先写好**。我们 `hakios.mali610.c:3852-3858` 的 doorbell 试验是在 AUTO 之后立刻写 1，那时 `req`/`ack` 还没初始化，属于「响空铃」。

**跨模型边界**：❌ 大部分不能跨（Android HAL/AFBC/gralloc 全是 Linux+Android 概念）。可跨的只有两条：**CSF doorbell 无排队语义**、**版本号≠能力**。

---

### 3.8 `wxcvm/panvk-pgkm10`：同 SoC 的第二份 panvk 实证

- 设备：OnePlus PGKM10 / **MT6895 / Mali-G610 MC6** / Android 17（`README.md:5-8`）
- 上游：`zenithblue-oss/panvk-kbase-android` RC3 + 本仓 `patches/` 019–021 + `diagnostics/` 022–030
- 与 bkaneko 那份的差异：它把 **AFBC 对齐**做成独立诊断补丁 `025-afbc-body-alignment-diagnostic.patch`，并把 **kbase sync** 做成 `020/021/027/028/029` 一组
- 结论（`README.md` 尾部）：「星穹铁道已确认实际使用新版 Vulkan 并能显示画面，但仍有闪烁/重影/错位」；「浏览器未通过验证」
- **重要交叉**：它也报告 `gralloc_extra` 被误当 DMA-BUF（`README.md:57-62`），与 bkaneko 的 AFBC 问题是同一族

**对我们的意义**：零。两份 panvk 都在「kbase 已经能跑作业」的前提下工作，**恰好是我们卡住的地方**。价值只在于确认「xaga 同 SoC 的 GPU 在 Android 下工作正常」这件事有第三方独立证据。

**跨模型边界**：❌ 不可跨。

---

### 3.9 `SLNeko-XNU`：**同一台 xaga 的 HakiOS 移植**，有裸机 Mali 源码但**只覆盖 Bifrost**

这是本次最大的意外发现，必须说清楚它的边界。

**它是什么**

- `ShuiLan114514/SLNeko-XNU`，branch `Xaga`，16★，最近更新 2026-11-14
- `README.md:3-6`：「This is the source tree for the **xaga** port of HakiOS: Apple's open-source Darwin/XNU（`xnu-8796.121.2`）booting on a Xiaomi Redmi Note 11T Pro / POCO X5 Pro（MT6895）」
- 它**从 HakiOS 的 `mtk` 分支派生**（`README.md:9-10`），即 Infinix Hot 20（MT6768 / Mali-G52 MC2）那套
- 状态表（`README.md:14-27`）：Boot/Display/Touch/OOBE/Logs/Battery/Audio/Keys 都有；**Wi-Fi/BT 进行中**；**没有 GPU 一行**

**它的 Mali 代码是什么**

| 文件 | 行数 | 覆盖的芯片 | 有无固件 |
|---|---|---|---|
| `xnu/files/bsd/dev/redwood_mali.c` | 1,146 | **MT6768 / Mali-G52 MC2 @ `0x13040000`** | **无**（Bifrost 不需要固件） |
| `xnu/files/iokit/Kernel/RedwoodMali.cpp` | 633 | 同上（IOKit 服务层） | — |
| `mtkloader/mtkmali.h` | — | 同上（loader 侧探针定义） | — |

`redwood_mali.c:6-8` 原文：
> 「A Bifrost Mali needs no firmware, so bringing it up is power and clocks and nothing else」

**所以：它对我们的 CSF 卡点没有直接答案**——G610 是 Valhall/CSF，必须跑固件。但它的**方法**可以直接搬：

1. **完整的上电顺序，每条都注明出处**（`mtkmali.h:1-25`）：
   ```
   rails   VGPU / VSRAM_GPU（PMIC，preloader 通常已开）
   clock   topckgen 的 "mfg_sel" mux，必须在任何电源域打开之前跑起来
   power   SPM 里四个域：MFG_ASYNC（到芯片其余部分的桥）、MFG、MFG_CORE0/1
           每个域带 SRAM；MFG 起来后释放总线保护
   gate    块内一个门控，"BG3D" @ mfgcfg 0x13000000
   ```
   这与我们 `hakios.mali610.c` 的 `gpu_l2_power_on()` 思路一致，但**"clock 先于 power"** 这条值得逐条比对。
2. **SPM 域的寄存器与位定义**（`mtkmali.h:37-58`，MT6768 口径，**地址不可直接搬到 MT6895**）：
   `SPM_BASE 0x10006000`、`SPM_PWR_STATUS 0x180`、`SPM_PWR_STATUS_2ND 0x184`；
   `PWR_RST_B(0) / PWR_ISO(1) / PWR_ON(2) / PWR_ON_2ND(3) / PWR_CLK_DIS(4) / PWR_SRAM_PDN(8) / PWR_SRAM_PDN_ACK(12)`。
   ⚠️ **注意**：MT6895 的 `PWR_STATUS` 是 `0xF3C`（`docs/GPUEB.zh-CN.md:§1.3` 已纠正），不是 `0x180`。位定义可能同构，**地址必须换**。
3. **`tools/gpu-probe.sh`**：一套「刷 boot_a → 通过 pmsg 请求 → 重启 → 从 `console-ramoops-0` 取 `GPU probe ==` 段」的完整流程。**这个流程我们可以照搬**，只是把探针内容换成 CSF 的。
4. **`mtkloader.c:3280-3364`** 的 GPU 探针：MMU 身份映射 → 写 `JOB_INT_MASK=0` → `JOB_INT_CLEAR=0xffffffff` → 配 JS → 提交一个 `WRITE_VALUE` 作业 → 读回目标字验证 `0xc0ffee01`。注意它**第一步就是 `JOB_INT_MASK=0`**（`:3319`），再次印证 §3.1。

**跨模型边界**：✅ 方法可跨（同为 XNU 裸机、同为 MTK、同为 `ml_io_map`+MMIO）；❌ 寄存器地址与「无需固件」的结论不可跨（G52 vs G610）。

---

## 4. A/B 级发现 · 耳机 / MT6368

### 4.1 6.12 与 5.10 的 ACCDET：**语义完全一致**，可用作交叉验证

- 文件：`zhaoqing412/xaga-mt6895-k6.12/kernel_device_modules-6.12/sound/soc/codecs/mt6368-accdet.c`（98,345 B）
- 与项目已有的 5.10 版本 diff：733 行，**差异全部是外围**：
  - 删：`tcpc_core.h`/`tcpm.h`（Type-C 依赖）、`debugfs.h`、`kernel.h`、`HEADSET_STATUS_RECORD` 那一整套宏
  - 加：`pinctrl/consumer.h`、`snprintf` 版日志缓冲、`gpiod_set_debounce`
  - 改：`snd_jack_set_key()` 的键码从 `KEY_MEDIA/MEDIA_NEXT_SCAN_CODE/MEDIA_PREVIOUS_SCAN_CODE` 改为 `KEY_PLAYPAUSE/KEY_VOLUMEDOWN/KEY_VOLUMEUP`
- **关键语义原封不动**（逐条核对，`mt6368-accdet.c` 6.12 版行号）：

| 语义 | 6.12 行号 | 与我们已确证的是否一致 |
|---|---|---|
| `res_value < 5000` → 耳机（否则 LineOut） | `:653` | ✅ 一致 |
| `ACCDET_IRQ_ADDR` 读中断位 | `:252-253`、`:979`、`:2156` | ✅ |
| `ACCDET_IRQ_CLR_ADDR` 写清 | `:997`、`:1008` | ✅ |
| `cur_AB = accdet_read(ACCDET_MEM_IN_ADDR) >> ACCDET_STATE_MEM_IN_OFFSET` | `:1645`、`:1769`、`:2953` | ✅ |
| `cur_AB &= ACCDET_STATE_AB_MASK`（`0x03`） | `:557-558` | ✅ |
| `check_cable_type()` 存在且被 IRQ 调用 | `:1765`、`:1906` | ✅（仍是有前态的状态机） |
| `headset-three-key-threshold` 从 DT 读 | `:2467-2471` | ✅ |

**结论**：我们已有的寄存器语义**不需要修订**。6.12 只是把 5.10 的 Type-C/按键记录部分剥掉了。

**跨模型边界**：✅ 寄存器语义可跨；❌ Linux 的 `regmap`/`snd_soc_jack`/`gpiod` 不可跨。

### 4.2 ACCDET 寄存器地址映射（从 `mt6368.h` 完整解出，可当裸机常量表）

`raw/mt6368.h` 里所有 MACRO 都指向 `MT6368_ACCDET_CONn`，本次把 n 全部解成绝对地址：

| 宏 | 绝对地址 | 含义 | 出处（`mt6368.h` 行） |
|---|---|---|---|
| `ACCDET_SW_EN_ADDR` | **`0x268a`** (CON2) | 软件使能 | `:4248` |
| `ACCDET_AUXADC_SEL_ADDR` | **`0x2688`** (CON0) | AUXADC 选择 | `:4208` |
| `ACCDET_HWMODE_EN_ADDR` | `0x26ad` (CON37) | 硬件模式使能 | `:4688` |
| `ACCDET_CMPEN_SW_ADDR` | `0x26b3` (CON43) | 比较器软件使能 | `:4868` |
| `AD_AUDACCDETCMPOB_ADDR` | `0x26b6` (CON46) | 比较器输出（**与 MEM_IN 同址**） | `:4973` |
| **`ACCDET_MEM_IN_ADDR`** | **`0x26b6`** (CON46) | **cur_AB bit[7:6]** | `:4993` |
| `AD_EINT0CMPMOUT_ADDR` | `0x26b8` (CON48) | EINT0 比较器输出 | `:5023` |
| `AD_EINT0INVOUT_ADDR` | `0x26bc` (CON52) | EINT0 反相输出 | `:5113` |
| `ACCDET_EN_ADDR` | `0x26c0` (CON56) | 总使能 | `:5193` |
| `ACCDET_MON_FLAG_EN_ADDR` | `0x26d1` (CON73) | 监测标志使能 | `:5428` |
| **`ACCDET_IRQ_ADDR`** | **`0x26a9`** (CON33) | **中断标志 bit0** | `:4593`，`ACCDET_IRQ_MASK_SFT = (0x1<<0)` `:4597` |
| **`ACCDET_IRQ_CLR_ADDR`** | **`0x26aa`** (CON34) | **W1C bit0** | `:4613`，`ACCDET_IRQ_CLR_SFT = 0` `:4615` |
| `RG_ACCDET_CK_PDN_ADDR` | `0x230c` (AUD_TOP_CKPDN_CON0) | 时钟门控 | `:3843` |
| `RG_ACCDET_RST_ADDR` | `0x2319` (AUD_TOP_RST_CON0) | 复位 | `:3849` |
| `RG_INT_EN_ACCDET_ADDR` | `0x231d` (AUD_TOP_INT_CON0) | 中断使能 | `:3860` |
| `RG_INT_MASK_ACCDET_ADDR` | `0x2320` (AUD_TOP_INT_MASK_CON0) | 中断屏蔽 | `:3876` |
| `RG_INT_STATUS_ACCDET_ADDR` | `0x2323` (AUD_TOP_INT_STATUS0) | 中断状态 | `:3892` |
| `RG_AUDPWDBMICBIAS0_ADDR` | `0x2527` (AUDENC_ANA_CON31) | MICBIAS0 | `:5439` |
| `RG_AUDPREAMPLON_ADDR` | `0x2508` (AUDENC_ANA_CON0) | 前置放大器 | `:5449` |
| `RG_CLKSQ_EN_ADDR` | `0x2537` (AUDENC_ANA_CON47) | 时钟方波使能 | `:5454` |
| `RG_ACCDETSPARE_H_ADDR` | `0x2534` (AUDENC_ANA_CON44) | spare | `:4150` |
| `ACCDET_STATE_MEM_IN_OFFSET` | `= ACCDET_MEM_IN_SFT` | cur_AB 移位 | `:5514` |
| `ACCDET_STATE_AB_MASK` | **`0x03`** | cur_AB 掩码 | `:5515` |

**验证**：`0x26a9` / `0x26aa` / `0x26b6` 与我们已确证的三个地址**逐一对上**（CON33 / CON34 / CON46）。
`0x26b6` 同时是 `AD_AUDACCDETCMPOB_ADDR` —— 这解释了为什么同一个寄存器既能读 `cur_AB`（bit[7:6]）又能读比较器输出（低位）。

**跨模型边界**：✅ 完全可跨，这就是裸机要用的常量表。

### 4.3 厂商的「dump 寄存器」函数：可当裸机实验的检查清单

6.12 版 `mt6368-accdet.c:241-360` `mini_dump_register()` / `:288-360` `dump_register()`。

**`mini_dump_register()` 固定打印的 12 个寄存器**（`:247-275`）：
```
ACCDET_SW_EN_ADDR(0x268a)   ACCDET_CMP_PWM_EN_ADDR
ACCDET_IRQ_ADDR(0x26a9)     ACCDET_DA_STABLE_ADDR
ACCDET_HWMODE_EN_ADDR(0x26ad)  ACCDET_CMPEN_SEL_ADDR
ACCDET_CMPEN_SW_ADDR(0x26b3)   AD_AUDACCDETCMPOB_ADDR(0x26b6)
AD_EINT0CMPMOUT_ADDR(0x26b8)   AD_EINT0INVOUT_ADDR(0x26bc)
ACCDET_EN_ADDR(0x26c0)      AD_AUDACCDETCMPOB_ADDR(0x26b6)
```
**再扫两段连续区间**（`:276-286`）：
- `RG_AUDPWDBMICBIAS0_ADDR(0x2527)` → `RG_ACCDETSPARE_H_ADDR(0x2534)`，步长 4，每行 4 个

**`dump_register()` 额外扫**（`:295-352`）：
- `0x8` → `0x10`（步长 4）
- `ACCDET_AUXADC_SEL_ADDR(0x2688)` → `ACCDET_MON_FLAG_EN_ADDR(0x26d1)`，步长 4 ← **这就是 ACCDET 全寄存器块，19 个地址**
- `RG_AUDPREAMPLON_ADDR(0x2508)` → `RG_CLKSQ_EN_ADDR(0x2537)`，步长 4
- 单点：`RG_ACCDET_CK_PDN(0x230c)`、`RG_ACCDET_RST(0x2319)`、`RG_INT_EN_ACCDET(0x231d)`、`RG_INT_MASK_ACCDET(0x2320)`、`RG_INT_STATUS_ACCDET(0x2323)`、`RG_AUDPWDBMICBIAS1`、`RG_AUDACCDETMICBIAS0PULLLOW`、`AUXADC_ACCDET_AUTO_SPL`
- 打印 `cust_pwm_deb->debounce0/1/3/4`

**对我们的直接价值**：**这就是一份「厂商认为该看哪些寄存器」的清单**。我们至今没在真机读过任何一个 ACCDET 寄存器——第一次实验就按 `0x2688..0x26d1` 步长 4 扫一遍（19 个地址），加上 `0x230c/0x2319/0x231d/0x2320/0x2323`，一共 24 个读，成本极低，收益是把「基线」这件事一次做完。

**跨模型边界**：✅ 可跨（就是 SPMI 读）。

### 4.4 🔴 Mu-Silicium 的裸机 SPMI 实现 —— **这是耳机任务最直接可用的代码**

`mytiantian001/Mu-Silicium`（branch `pearl`）`Platforms/Xiaomi/pearlPkg/Library/KeypadDeviceLib/KeypadDeviceLib.c`。
这是 **UEFI 裸机**代码：无中断、无锁、无 DMA、纯 MMIO 轮询，**与我们的执行环境同构**。

**寄存器地址（`:14-22`）**

```c
#define PMIF_BASE        0x1C804000
#define SPMI_SEND        0x0880  /* SWINF2: the channel pearl's MT6363 answers on */
#define SPMI_RDATA       0x0894
#define SPMI_VLD_CLR     0x08A4
#define SPMI_STA         0x08A8
#define SPMI_FSM_IDLE     0
#define SPMI_FSM_WFVLDCLR 6
#define MT6363_SID        4
```
与我们已知的完全一致：`0x1c804000` 基址、`0x880/0x894/0x8a4/0x8a8` 四个偏移、SWINF2、FSM 值 0/6。
（`MT6363_SID 4` 是 pearl 的副 PMIC；xaga 的 MT6368 是 **SID 5**，必须替换。）

**读函数全文（`:223-264`）**

```c
STATIC EFI_STATUS
SpmiReadByte (IN UINT16 Address, OUT UINT8 *Value)
{
  UINTN  Timeout;
  UINT32 Data, Command;

  if (SpmiFsm () == SPMI_FSM_WFVLDCLR) {
    MmioWrite32 (PMIF_BASE + SPMI_VLD_CLR, 1);
  }

  for (Timeout = 0; Timeout < 2000; Timeout++) {
    if (SpmiFsm () == SPMI_FSM_IDLE) break;
    MicroSecondDelay (10);
  }
  if (Timeout == 2000) return EFI_TIMEOUT;

  /* bit31:30 = 3 selects the extended-long read the PMIC answers; the
   * SWINF1 short-read form (1 << 30) returns a stale constant on pearl. */
  Command = ((UINT32)3 << 30) | ((UINT32)MT6363_SID << 24) | Address;
  MmioWrite32 (PMIF_BASE + SPMI_SEND, Command);

  for (Timeout = 0; Timeout < 2000; Timeout++) {
    if (SpmiFsm () == SPMI_FSM_WFVLDCLR) break;
    MicroSecondDelay (10);
  }
  if (Timeout == 2000) return EFI_TIMEOUT;

  Data = MmioRead32 (PMIF_BASE + SPMI_RDATA);
  MmioWrite32 (PMIF_BASE + SPMI_VLD_CLR, 1);
  *Value = (UINT8)(Data & 0xFF);
  return EFI_SUCCESS;
}
```

FSM 读取（`:214-219`）：
```c
return (MmioRead32 (PMIF_BASE + SPMI_STA) >> 1) & 0x7;
```

**与我们的实现逐条对照**

| 项 | Mu-Silicium（裸机，实测可用） | 我们 `hakios.mt6895_keys.c` 的注释 | 判定 |
|---|---|---|---|
| PMIF 基址 | `0x1C804000` | `0x1c804000` | ✅ |
| 通道 | SWINF2，偏移 `0x880` | 「addressing SWINF_2 and nothing else」 | ✅ |
| 命令字 opcode | **`(3 << 30)`**，并注明 `(1<<30)` 短读在 pearl 上返回陈旧常量 | 注释 `:147-151`：「`PMIF_CMD_REG(1)` 与 `PMIF_CMD_EXT_REG_LONG(3)`；用 `PMIF_CMD_REG` 会让 FSM 停在 4 而不是 WFVLDCLR」 | ✅ **独立印证** |
| SID 位置 | `SID << 24` | 同上语义 | ✅ |
| FSM 取值 | `(STA >> 1) & 0x7`，IDLE=0，WFVLDCLR=6 | 注释 `:103-106` 同 | ✅ |
| 握手 | 先 `VLD_CLR=1` 清残留 → 等 IDLE → 发命令 → 等 WFVLDCLR → 读 RDATA → `VLD_CLR=1` | 同 | ✅ |
| 超时 | 2000 × 10 µs = **20 ms** × 2 段 | — | 可参考 |

**结论**：**我们的 SPMI 路径设计是对的，而且有一份独立的裸机实现逐条对上。** 这消除了「我们的 PMIF 序列可能根本错」这个疑虑。剩下的唯一差别是 SID（5 vs 4）和「地址是 16 位扩展长读」。

**⚠️ 一个必须注意的差异**：Mu-Silicium 的 `Address` 参数是 `UINT16`，命令字里 **`address` 放低 16 位**。我们的 ACCDET 地址（`0x26a9` 等）都是 16 位，正好落在低 16 位。**不要**把 `0x26a9` 写成 `0x000026a9` 之外的形式，也不要加 `0x` 前缀解析风险（参见 `DANGER-spmi-access.md` 的 `0x5` 崩溃记录——那是 Android sysfs 接口的解析 bug，与我们自己的 PMIF 代码无关）。

**跨模型边界**：✅ **完全可跨**。这是本次对耳机任务价值最高的一项。

### 4.5 内存图：`Res GPUEB` 在 `0x7C200000`，**不是** `0x7c600000`

`Mu-Silicium/Platforms/Xiaomi/pearlPkg/Library/MemoryMapLib/MemoryMapLib.c:48`：
```c
{"Res GPUEB",          0x007C200000, 0x00200000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK},
```
周围相关项（`:44-58`）：
```c
{"Res GPU mputab",     0x0078C00000, 0x01400000, ...},
{"Res GPUEB",          0x007C200000, 0x00200000, ...},   /* 2 MiB */
{"Res 0x7c400000",     0x007C400000, 0x00400000, ...},
{"Res TEE SecMem",     0x007C800000, 0x00700000, ...},   /* TEE 安全内存 7 MiB */
```

**与我们已有结论的冲突**

- 我们：`docs/headset-wip/research/GPUEB-analysis.md` 说 xaga 的 GPUEB 共享内存是 **`0x7C600000` 起 2 MiB**（依据 `mt6895-xiaomi-xaga.dts:618-624` 的 `gpueb_mem_addr = <0x7c600000>`）
- pearl（Mu-Silicium）：`Res GPUEB` = **`0x7C200000` 起 2 MiB**
- 另有一条：`Void1315/xaga-beanpod-keybox-rkp-research` 报告 TEE 参数物理地址是 `0x7c600000`（见 `REPO-xaga-ecosystem.md`）

**判定**：`[未验证]`。两种可能是：
1. **pearl ≠ xaga**：pearl 是 Redmi Note 12T Pro（同为 MT6895 家族但板级不同），GPUEB 共享内存地址可能真的不一样。`0x7C200000` 与 `0x7C600000` 相距 4 MiB，不像笔误。
2. **我们的 `0x7c600000` 是 TEE 参数地址，不是 GPUEB 共享内存**——`Void1315` 那条独立报告支持这个怀疑。

**下一步**：在 `mt6895.dtsi` 里搜 `mblock-38-me_GPUEB_SHARED` 或 `gpueb_mem_addr`，与 xaga 真机 DT 对一遍。**这条如果搞错，固件永远读不到共享区**——属于能单独解释「MCU 起来了但什么也不做」的量级。

**跨模型边界**：✅ 可跨（物理地址是硬件事实）。

### 4.6 Mu-Silicium 的 PMIC 相关内存映射（顺带）

`MemoryMapLib.c:130-131`：
```c
{"PMIF",               0x001C804000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
{"SPMIMST",            0x001C801000, 0x00001000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
```
→ 除了 `PMIF` 从机，还有一个 **`SPMIMST`（SPMI Master）在 `0x1C801000`**。我们目前只用了 PMIF。若 FSM 卡死，`SPMIMST` 是复位/诊断入口。`[未验证]` 我们是否需要它。

### 4.7 ACCDET 的 DT 契约（xaga 侧，6.12 与 5.10 一致）

`raw/xaga-devs/mt6368-612/mt6368.dtsi:22-45`（6.12）与 `raw/mt6895.dtsi:2155-2175`（5.10）逐字段相同，只是数值写法不同（16 进制 vs 10 进制）：

```dts
accdet: accdet {
    compatible = "mediatek,mt6368-accdet";
    accdet-mic-vol = <8>;              /* 2.8 V */
    accdet-plugout-debounce = <1>;
    accdet-mic-mode = <2>;             /* low cost without in bias */
    eint_use_ext_res = <0>;
    headset-mode-setting = <0x500 0x500 1 0x1f0 0x800 0x800 0x20 0x44
                            0x4 0x1 0x5 0x3 0x3 0x5 0xe>;
    headset-use-ap-eint = <0>;
    headset-eint-num = <0>;
    headset-eint-trig-mode = <1>;
    headset-key-mode = <0>;
    headset-three-key-threshold       = <0 80 220 400>;    /* mV */
    headset-three-key-threshold-CDD   = <0 121 192 600>;
    headset-four-key-threshold        = <0 58 121 192 400>;
    io-channels = <&mt6368_adc AUXADC_ACCDET>;
    io-channel-names = "pmic_accdet";
    nvmem = <&mt6368_efuse>;
    nvmem-names = "mt63xx-accdet-efuse";
    status = "okay";
};
```

**与任务背景的核对**：`headset-three-key-threshold = <0 80 220 400>` —— **80/220/400 mV，与任务给的 xaga 3-key 值完全一致** ✅。

`mt6368.dtsi:9-11`（6.12）确认 SPMI 侧：
```dts
second_pmic@5 {
    compatible = "mediatek,mt6368";
    reg = <0x5 SPMI_USID>;      /* ← SID 5，与我们已经确证的一致 */
```

`xaga-mt6895.dtsi:619-627`（6.12）的板级覆盖只有两项：
```dts
&accdet {
    accdet-mic-vol = <8>;
    accdet-mic-mode = <2>;
};
```
→ **xaga 没有改阈值，直接用 mt6368.dtsi 的默认 80/220/400。**

**跨模型边界**：✅ 可跨（DT 数值就是硬件配置值）。

### 4.8 耳机侧「实机 dump」的搜索结论：**没有**

跑了 4 个 GitHub code search 查询（`0x26b6 accdet` 328 命中、`ACCDET_IRQ+0x26a9` 268、`cur_AB accdet` 524、`mt6368 accdet` 1244），逐条看前 20 条：

- 全部是 **驱动源码 / 头文件 / regmap 定义**：`MotorolaMobilityLLC/kernel-mtk`、`OnePlusOSS/android_kernel_5.10_oneplus_mt6895`、`NothingOSS/android_kernel_5.15_nothing_mt6886`、`Rohail33/Realking_xiaomi_xaga`、`XagaForge/android_kernel_xiaomi_mt6895`、`MT6895-Mainline/linux`、`realme-kernel-opensource/...`、`physwizz/...` 等
- **没有任何一份**日志、dump、issue、wiki 形式的「真机读出的 `0x26a9` = 0x??」
- 唯一接近的是 `LieppOS/LieppOS-ulefone-gq5012bf1-research` 的 `reverse-engineering/audio/mt6369/registers.tsv` —— 但那是 **MT6369**（不是 6368），且内容是「地址 → 宏名 → 来源文件 → 引用次数」的映射表（如 `0x230c MT6369_AUD_TOP_CKPDN_CON0 mt6369.c 15`），**不是实机值**

**结论**：**ACCDET 的实机 dump 全网没有公开的**。我们的「从未在真机读到过」不是我们漏找了，是确实没人公开过。这反而说明：**第一次读出来的值就是全网第一份**，值得记录完整。

**跨模型边界**：N/A。

---

## 5. `Mu-Silicium` 的 GPU 初始化：**一行都没有**

用户点名要查「UEFI 里怎么初始化 GPU 电源/时钟」。答案是：**没有**。证据：

1. **全树 grep**：在 `Silicon/` 与 `Platforms/` 下搜 `mali|gpueb|mfg_pwr|0x13000000`，文件级命中只有 4 个，其中 3 个是误命中（`asn1.h` 里的 `mali` 子串、Samsung/S5E9830 的 `Cmu.h`、`MshcHost.c`），唯一的真命中是 `pearlPkg/Library/MemoryMapLib/MemoryMapLib.c`（只是把 `Res GPUEB` 标成 reserved，**不初始化**）。
2. **`pearlPkg` 全目录 20 个文件**，逐个看过：`AcpiTables/`、`DeviceBuild.py`、`Drivers/HandoffQuiesceDxe/`、`Library/{KeypadDeviceLib, MemoryMapLib, PlatformBootOptionsLib}`、`pearl.dec/.dsc/.fdf`。**没有 GPU 驱动**。
3. **`Silicon/MediaTek/MT6895Pkg/`** 26 个文件，全是 UFS / USB / Keypad / PMIC keys / SimpleFb：
   ```
   Drivers/MtkUfsDxe/  Drivers/MtkUfsPassThruDxe/  Drivers/MtkUsbDxe/
   Drivers/PearlKeypadDxe/  Drivers/PearlPmicKeysDxe/  Drivers/PearlUfsDiagDxe/
   Drivers/SimpleFbDxe/  Library/PlatformSecLib/
   ```
   **无 GPU**。
4. **`pearl.dsc:145-147`** 的注释：「Custom display: MT6895 LK handoff takeover (OVL fixup)」→ `MT6895Pkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf`。**显示走 LK handoff + OVL fixup，不碰 GPU。**

**这意味着什么**

- UEFI 里**不需要**初始化 GPU：`SimpleFbDxe` 直接接管 LK 已经配好的 OVL/framebuffer。GPU 在 UEFI 阶段是**完全没被碰过**的。
- 所以「和 LK 有没有不同的做法」这个问题不存在——**两者都没做**。GPU 在整条 bootchain（preloader → LK → UEFI/kernel）里，**直到内核的 kbase/gpufreq 才第一次被访问**。
- 这反过来说明：**我们的裸机 XNU 是这条链上第一个需要在 EL1 里自己把 GPU 拉起来的实体**，没有任何前人可以抄。这也解释了为什么 19 轮都卡着——不是我们笨，是这条路没人走过。

**唯一有间接价值的东西**：`MemoryMapLib.c` 给了 MT6895 家族的**完整内存图**（60+ 条），包括 `Res GPU mputab 0x78C00000 +0x1400000`（20 MiB，GPU 页表区）、`Res GPUEB 0x7C200000 +0x200000`、`Res sspm-ap 0x7C0F0000 +0x110000`、`Res TEE SecMem 0x7C800000 +0x700000`。**这是我们做 GPU 页表分配时的地址边界参考。**

**跨模型边界**：⚠️ 内存图可跨（硬件事实）；「不需要初始化 GPU」只适用于 UEFI 阶段，**不适用于我们的 XNU 阶段**。

---

## 6. MT6895 dts 的 GPU/GPUEB 节点摘录

### 6.1 `zhaoqing412/xaga-mt6895-k6.12` 的 dts 结构（实际读到的）

6.12 把 DT 拆到了 `kernel_device_modules-6.12/arch/arm64/boot/dts/mediatek/`：

| 文件 | 行数 | 内容 |
|---|---|---|
| `xaga.dts` | 28 | 只是个 `/plugin/` 覆盖层，include root/thermal/板级/cust |
| `xaga-mt6895.dtsi` | 636 | 板级差异（充电/功放/触觉/USB/**accdet 覆盖**） |
| `xaga-mt6895-common.dtsi` | 786 | 公共部分 |
| `xaga-mt6895-root.dtsi` | — | 根节点 |
| `mt6368.dtsi` | 295 | **完整的 MT6368 节点 + accdet** |
| `cust_xaga.dtsi` | 399 | 客户定制 |

**关键观察：这三个文件里 grep `gpu|mali|mfg|gpueb`，`xaga-mt6895-common.dtsi` 命中 0 条。**
GPU/GPUEB 节点在 `kernel-6.12/arch/arm64/boot/dts/mediatek/`（主 dts 目录）或更上层的基础 dts 里。本次未定位到具体文件（`[未验证]`：6.12 的 dts 树有 60,547 个文件，recursive tree 被 GitHub 截断，需要 `git ls-tree` 全量扫）。

**已确认的**：6.12 的 `xaga-mt6895.dtsi` **完全不含 GPU 节点**，只有 accdet 覆盖（`:619-627`）。所以「6.12 的 dts 里 GPU 怎么描述」这个问题，答案在别处，本次没拿到。**这是一个明确的待办，不是「没有」。**

### 6.2 我们已有的 5.10 dts（GT 已知，此处只做交叉）

已有材料（`raw/mt6895.dtsi` 7,254 行、`raw/mt6895-xiaomi-xaga.dts`）里的关键节点，与 `docs/GPUEB-analysis.md` 一致，本次不重复。**新增的交叉验证**：

- `mt6368.dtsi:9-11` 的 `reg = <0x5 SPMI_USID>` 与我们的 SID 5 ✅
- `headset-three-key-threshold = <0 80 220 400>` 与我们的 80/220/400 mV ✅（§4.7）
- `iou`/`spmi` 的挂载点：6.12 用 `&spmi { second_pmic@5 {...} }`，与 5.10 相同

---

## 7. 平台 / bootchain（C 级，但 `eng_preloader` 是 A）

### 7.1 🔴 `xiaomi-mediatek-devs/mt6895_eng_preloaders`：**签名的 xaga 工程 preloader**

| 文件 | 大小 | sha1 |
|---|---|---|
| `preloader_aristotle_eng.bin`（aristotle/xaga，**HyperOS only**） | 499,888 B | `d9399d4faeea64cf82b2675aa6b26bd8cbd75e36` |
| `preloader_plato_eng.bin`（plato/rubens） | 493,664 B | `544f0a12fff598ed1866b695398d3b193b3ffc58` |
| **`preloader_xaga_eng.bin`（xaga，MIUI only）** | **492,172 B** | **`4a7486c4c954aedca2e585ac92e5bd640bfa6bee`** |

README 明确「**Signed** Engineering preloaders」。这是**能刷的**工程 preloader，意味着 `download mode`/`fastboot` 之外多一条恢复路径。

**我从 `preloader_xaga_eng.bin` 里挖到的（`[字节]`，4,745 条 strings）**

| 事实 | strings 行号 | 意义 |
|---|---|---|
| **`mfg_pll_ctrl` / `gpueb_pll_ctrl` / `mfgsc_pll_ctrl` 三个 PLL 控制名并存** | `:3757-3759` | preloader 阶段就把 **GPU 的三路 PLL** 都初始化了。裸机接管时它们**应该已经在跑** |
| **`XPU_PWR_STATUS: %x, MFG0 abnormal: %x` / `MFG1 abnormal`** | `:3752-3753` | preloader **检查** MFG0/MFG1 的 XPU 电源状态。有「abnormal」判定说明存在**正常范围** |
| **`spm_mtcmos_ctrl_mfg0_shutdown` / `spm_mtcmos_ctrl_mfg1`** | `:3975-3976` | preloader 有 MFG0/1 的 MTCMOS 控制函数（含 shutdown） |
| **`%s %d MTCMOS hang at %s flow step %d`** | `:3977` | MTCMOS 上电**可能挂住**，且有 flow step 编号可定位 |
| **`gpu_init_opp` / `gpu_avs_adjustment`** | `:2737` / `:2741` | preloader 做 GPU 的 OPP 初始化和 AVS 调整 |
| **`OPP[%d] critical Volt` / `OPP[%d]: efuse[%d].freq != signed-off.freq`** | `:2739-2740` | OPP 表来自 **efuse**，且会与 signed-off 值比对 |
| **`[AOC] SPM: 0x%x, 0x%x, 0x%x \| 0x%x, 0x%x, 0x%x`** | `:2196` | AOC 也读 SPM 三组寄存器 |
| **`[PMIF] [swinf:%d, cmd:0x%x, is_write:%d, slvid:%d, bytecnt:%d (write addr 0x%x=0x%x)]`** | `:4006-4007` | **preloader 有完整的 PMIF 事务日志格式** —— 若能让它 print，就是免费的 SPMI 参考 |
| `[PMIF] Latest cmd lists before reboot` | `:4013` | preloader 保存最近命令列表（崩溃后可见） |
| `[SPMI] spmi_stress_test` / `spmi_read_check` | `:3999-4002` | 有自检 |
| `mt6319_spmi_probe` / `[MT6368] Regulator register %s fail` | `:3194` / `:3210` | PMIC 栈：MT6368 主 + MT6319 副 |
| `vgpustack` | `:3267` | GPU 的 VSTACK 轨由 MT6368 供 |

**地址扫描（`[字节]`，全文件 4 字节对齐搜 `0x1C001xxx`）**

```
0x1c001000 × 8 处   0x1c001268 × 2   0x1c001400   0x1c001410   0x1c001900
0x1c001e14  0x1c001e1c  0x1c001e20  0x1c001e64  0x1c001e68  0x1c001e6c
0x1c001eb8  ← MFG0_PWR_CON（1 处，@0x41dbc）
0x1c001ebc  ← MFG1_PWR_CON（1 处，@0x4628c）
0x1c001f0c  0x1c001f24
```
`0x1c804000`（PMIF）命中 3 处：`@0x31324`、`@0x761b0`、`@0x761fc`。
其中 `@0x761b0` / `@0x761fc` 上下文有 `"FACTORY!"` 字符串，且后随 `00 10 80 1c`（= `0x1c801000`，**SPMIMST**）—— 与 §4.6 的 `SPMIMST 0x1C801000` 吻合 ✅。

**这份材料的价值**：它是**唯一一份 xaga 自己的 bootloader 二进制**。里面有：
- GPU 三路 PLL 的初始化痕迹 → 说明裸机起来时 PLL 已在跑（**不要重配 PLL**）
- MFG0/MFG1 的 MTCMOS 函数 → 说明 preloader 可能已经动过这两个域
- `gpu_init_opp` / `gpu_avs_adjustment` → GPU OPP 与 AVS 在 preloader 就设好了
- 完整的 PMIF 事务日志格式 → 可作为 SPMI 调试的参考

**跨模型边界**：✅ 完全可跨（这是硬件初始化顺序的事实）。

### 7.2 `fenrir` / `mtk-mod-tee-nogz`：bootchain 控制

| 项 | 内容 | 出处 |
|---|---|---|
| fenrir 核心漏洞 | **unlocked（seccfg）时 Preloader 跳过 `bl2_ext` 校验**，而 `bl2_ext` 在 EL3 跑并负责后续一切 | `fenrir/README.md:20-32` |
| 后果 | patch `bl2_ext` → 整条信任链崩塌 | 同上 |
| 状态 | **仓库已归档（deprecated）**，作者明说「只有 cert bypass 那部分真正有价值」 | `fenrir/README.md:1-12` |
| `bin/xaga.bin` | fenrir 里有 **xaga 的 payload** | `fenrir/bin/xaga.bin` |
| `mtk-mod-tee-nogz` 支持 xaga | `references/profiles.json` 里有 **`xaga` profile**（Redmi Note 11T Pro 的 TEE/LK 配对，按完整 SHA-256 匹配） | `README.md` 支持范围表 |
| 它做什么 | 改 `tee.img` 里 ATF 的启动交接，保持 **LK 进 EL1h、AArch64 内核交接到 EL2h**，同步 ATF/LK 共享的 GZ-info tag | 同上 |
| 三条反例 | ①`D2A21E08` 实际装入 `0x10f00000` 而非 LK 的 `0x50f00000`，正确编码 `D2AA1E08`；②原版 LK 的 EL2 入口路径会访问 `CPTR_EL3`，不能把 LK 和内核一并强制 EL2h；③只改 ATF 的 GZ getter 不改共享 tag 无效 | 同上 |

**对我们的意义**：
- 「LK 进 EL1h、内核交接到 EL2h」——**我们的 loader 是 EL1 还是 EL2？** 如果是 EL1，那 GPU 的 MMIO 访问不受 stage-2 影响；如果 ATF 把我们放在 EL2，那 stage-2 可能把 GPU 的 MMIO 或 DMA 挡掉。**这是能单独解释「寄存器读得到但设备不工作」的一个层。**
- `mtk-mod-tee-nogz` 有 xaga 的精确 profile → 若要动 TEE/EL 级别，这是唯一有 xaga 哈希的公开工具。

**跨模型边界**：⚠️ 部分。EL 级别的结论可跨（我们也在同一条 ATF 出来的路径上）；patch 工具本身不可用（我们不刷 TEE）。

### 7.3 `woaphone` 的 bootchain 工具（B 级）

| 仓库 | 做什么 | 对 xaga 的可用性 |
|---|---|---|
| `mtk_disable_lk_dtbo_validation` | patch LK + bl2_ext，强制 dtbo 校验通过 | 通用 MTK，**未验证 xaga** |
| `mtk-disable-gz-oldv6-patchBL2-withoutPL` | 只替换签名 LK 容器里的 `bl2_ext` 成员为小 AArch64 载荷（NoGZ/EL2 boot-kit） | 通用，**未验证 xaga** |
| `mtk-mod-tee-nogz` | 见上，**有 xaga profile** | ✅ 有 xaga 哈希 |

**注意**：README 明确警告「有 LK 校验的 preloader 不要直接刷，会黑砖；请签名后刷」。**我们不碰这些。**

### 7.4 别人的 XNU-on-MT6895 尝试

| 项目 | SoC | 状态 | 对我们的意义 |
|---|---|---|---|
| **`ShuiLan114514/SLNeko-XNU`**（`Xaga` 分支） | **MT6895 (xaga)** | Boot/Display/Touch/OOBE/Audio/Keys 通；**Wi-Fi/BT 进行中；GPU 无** | **同机同路线**，是唯一可直接对照的 xaga XNU 树 |
| `woaphone/xnu-mtk` | MT6789/MT6989 | 通用 XNU 移植框架，`main`/`yunluo`/`rothko` 三支 | 方法学 |
| `woaphone/UniXNU-rothko` | MT6989 | **GPU 状态 `planned`，`components = []`**，`porting.toml:128-140`：「No native GPU driver, firmware contract or device rendering trace is recorded」 | **确认：MTK + XNU 的 GPU 这条路，全世界都还没人做成** |
| `mytiantian001/iPadOS-nabu` / `UniXNU-nabu` | SM8150 | 高通 | 无关 SoC |
| `sheikhmehraann/X6871-iOS-Port` | MT6895（Infinix GT 20 Pro） | 0★，Python | `[未验证]` 未读内容 |

**`UniXNU-rothko/porting.toml:128-140` 原文（值得记下来）**：
```toml
[[area]]
id = "gpu"
status = "planned"
components = []
inputs = ["MT6989 GPU/firmware, SMMU, submission, fence and Metal client contract."]
configuration = ["The current display path uses GOP/CMD presentation and does not claim a native GPU implementation."]
procedure = ["Collect the board GPU firmware and SMMU ownership facts before selecting a driver; do not import Qualcomm GMU terminology as a MediaTek hardware fact."]
findings = ["The observed UI and display path does not by itself prove a GPU implementation or a GPU root cause for the reported lag.", "The MT6989 common repository intentionally exports no GPU, SMMU, firmware or Metal policy."]
limits = ["No native GPU driver, firmware contract or device rendering trace is recorded in this repository."]
next_action = ["Inventory the stock GPU firmware, SMMU streams and Android reference implementation."]
```
→ **另一条 MTK XNU 路线（MT6989，比我们晚）也停在同一位置：GPU = planned。** 这不是安慰，是**优先级判断依据**：整条 MTK-XNU 生态都还没解决 CSF，我们的 19 轮不是异常。

---

## 8. 明确的「下一步读哪个仓库的哪个文件」

### 8.1 GPU —— 按优先级

| # | 动作 | 文件 | 为什么 |
|---|---|---|---|
| **G1** | **改一行**：`start_mcu()` 写 `MCU_CONTROL=AUTO` 前加 `mwr(kJobIntMask, 0u)`，并同时打印 `kJobIntRaw` 与 `kJobIntStatus` | `hakios.mali610.c:3847`（AUTO 写入点）、`:3132`（mask 写入点） | §3.1，唯一能同时解释两个症状的代码差异 |
| **G2** | 确认 `GPU_COHERENCY_PROTOCOL(0x304)` 是否在 L2 上电前写入 | `grep -n "kCoherencyNone" hakios.mali610.c`，对照 `panthor_gpu.c:60-64,319-321` | §3.3 |
| **G3** | 核对 GPUEB 共享内存基址到底是 `0x7c600000` 还是 `0x7c200000` | `mt6895-xiaomi-xaga.dts` 的 `gpueb_mem_addr`、`mt6895.dtsi` 的 `mblock-38-me_GPUEB_SHARED`；对照 `MemoryMapLib.c:48` | §4.5，地址错则固件读不到共享区 |
| **G4** | 读 `panthor_fw.c:1061-1090` 的 `panthor_fw_init_global_iface()`，逐项核对我们写进共享区的 `core_en_mask / poweroff_timer / progress_timer / idle_timer / ack_irq_mask / req` | `raw/xaga-devs/panvk/panthor_fw.c`（已拷） | §3.4 第 8 步，这是「固件起来后要它干活」的必要条件 |
| **G5** | 读 `panthor_fw.c:1214-1223` 的 `panthor_fw_mcu_set_active()`，确认我们是否发过 `GLB_STATE_ACTIVE` | 同上 | §3.4 第 9 步，固件从 HALT 出来的唯一手段 |
| **G6** | 读 SLNeko 的 `mtkloader.c:3280-3364` GPU 探针 + `tools/gpu-probe.sh` 流程 | `raw/xaga-devs/slneko-xnu/`（已拷部分） | §3.9，一套现成的「刷机→取日志」GPU 实验流程 |
| **G7** | 读 `mtkmali.h:1-58` 的上电顺序与位定义 | `raw/xaga-devs/slneko-xnu/mtkmali.h`（已拷） | §3.9，逐条比对我们的 `gpu_l2_power_on()` |
| **G8**（低优先） | 读 `firmware/README.md` 的 OPP 表与 5 字节补丁 | `raw/xaga-devs/gpueb-ipi/README-firmware.md`（已拷） | 已确认，仅备查 |
| **G9**（低优先） | 若要走 MFG0 上电：读 `ghpm_swwa.c:98-180` 与 `mtk_sip_svc.h:160-161` | `/tmp/xagadev/gpueb-full/ghpm_swwa.c` | §3.6，SIP 0x530 / op=6 |

### 8.2 耳机 —— 按优先级

| # | 动作 | 文件 | 为什么 |
|---|---|---|---|
| **H1** | 照抄 `SpmiReadByte()`（24 行），把 SID 换成 5 | `raw/xaga-devs/mtkmali/KeypadDeviceLib.c:223-264`（已拷） | §4.4，**裸机可用、逐条对上** |
| **H2** | 按 `dump_register()` 的清单做第一次真机读：`0x2688..0x26d1` 步长 4（19 个）+ `0x230c/0x2319/0x231d/0x2320/0x2323`（5 个） | `mt6368-accdet.c:288-360`（6.12） | §4.3，厂商认为该看的全在这 |
| **H3** | 用 §4.2 的地址表做常量 | `raw/mt6368.h:4150-5515` | 已解完，直接抄 |
| **H4** | 核对 `0x26b6` 的位：`cur_AB = (val >> ACCDET_MEM_IN_SFT) & 0x03` | `mt6368.h:4993, 5514-5515` | §4.2 |
| **H5** | DT 阈值确认：`<0 80 220 400>` mV，xaga 未覆盖 | `mt6368-612.dtsi:36` / `raw/mt6895.dtsi:2167` | §4.7 |
| **H6**（备查） | 6.12 版 accdet 全文（与我们 5.10 语义一致） | `raw/xaga-devs/mt6368-612/accdet-612.c` | §4.1 |

### 8.3 平台 / bootchain

| # | 动作 | 文件 | 为什么 |
|---|---|---|---|
| **P1** | 查我们 loader 出来时是 EL1 还是 EL2 | 项目内 loader 源码 + `mtk-mod-tee-nogz/README.md` | §7.2，EL 级别影响 GPU DMA 可见性 |
| **P2** | 读 preloader strings 的 GPU 段（`mfg_pll_ctrl` 等）确认 PLL 已在跑 | `raw/xaga-devs/preloader/preloader_xaga_eng.strings.txt:2735-2760, 3750-3760, 3970-3980` | §7.1，**不要重配 PLL** |
| **P3**（备灾） | 记下 `preloader_xaga_eng.bin` 的 sha1 与来源 | `xiaomi-mediatek-devs/mt6895_eng_preloaders` | 恢复路径 |

---

## 9. 跨模型边界总结（Android/Linux → 裸机 XNU）

| 结论 | 能否跨 | 理由 |
|---|---|---|
| `JOB_INT_MASK` 必须在 AUTO 前清零 | ✅ | CSF 架构语义，与 OS 无关 |
| `ASN_HASH` 在 arch<11 不存在 | ✅ | 硬件事实 |
| `GPU_COHERENCY_PROTOCOL` 在 L2 前写 | ✅ | 硬件事实 |
| CSF 启动 9 步判据链（§3.4） | ✅ | CSF 架构行为 |
| GPUEB 不写 SPM MTCMOS（§3.5 #1） | ✅ | 固件反汇编结论 |
| 「读 GPU 寄存器为 0 无鉴别力」（§3.5 #9） | ✅ | XPU/clock 层面的硬件事实 |
| MFG0 上电走 SIP 0x530 / op=6 | ✅ | SMC 是 EL 级接口，EL1 可发 |
| ACCDET 寄存器地址表（§4.2） | ✅ | SPMI 从设备地址 |
| ACCDET DT 阈值 80/220/400 mV | ✅ | 硬件配置值 |
| `SpmiReadByte()` 实现（§4.4） | ✅ | 纯 MMIO，无中断/无锁/无 DMA |
| MT6895 内存图（§4.5/§5） | ✅ | 物理地址 |
| preloader 已初始化 GPU 三路 PLL（§7.1） | ✅ | bootloader 行为事实 |
| `gpueb_ctrl()` 的 `ret=0` 语义（§3.6 #3） | ⚠️ | Linux 驱动逻辑，只能借「别信返回值」这个教训 |
| GPUEB 驱动的 runtime PM / genpd 分析（§3.5 #10） | ⚠️ | 裸机没有 runtime PM，`pm_runtime_get_sync` 那套完全不适用 |
| 13T 固件移植结论（§3.5 #3–#6） | ❌ | 与我们无关（我们不移植 13T 固件） |
| panvk 的 AFBC/gralloc/sync 修复（§3.7/§3.8） | ❌ | Android HAL 专有 |
| `regmap` / `snd_soc_jack` / `gpiod`（§4.1） | ❌ | Linux 框架 |
| UEFI「不需要初始化 GPU」（§5） | ❌ | 只适用于 UEFI 阶段，我们必须在 EL1 里自己拉起来 |
| SLNeko 的「Mali 不需要固件」（§3.9） | ❌ | G52/Bifrost vs G610/CSF |

---

## 10. 未验证 / 待确认清单

| # | 项 | 状态 |
|---|---|---|
| 1 | `JOB_INT_MASK` 是 GPU 卡点的真实原因 | **未验证**（代码级推断，需一次实验） |
| 2 | `kCoherencyNone` 是否被写入 `0x304` | **未验证**（未 grep 使用点） |
| 3 | GPUEB 共享内存基址 `0x7c600000` vs `0x7c200000` | **未验证**（两份材料冲突） |
| 4 | 6.12 的 GPU 节点在哪个 dts 文件 | **未验证**（60,547 文件树被截断，需 `git ls-tree` 全量） |
| 5 | xaga 的 loader 出来时 EL1 还是 EL2 | **未验证** |
| 6 | `SPMIMST 0x1C801000` 我们是否需要 | **未验证** |
| 7 | `sheikhmehraann/X6871-iOS-Port`（MT6895 的 XNU 移植）内容 | **未读**（0★，Python，超出本次范围） |
| 8 | `OnePlusOSS/android_kernel_5.10_oneplus_mt6895` 的 accdet 是否与 xaga 有差异 | **未读**（35★，同 SoC 第二来源） |

---

## 11. 与项目已有材料的接续关系

本次**不推翻**任何已有结论。增量如下：

| 已有 | 本次增量 |
|---|---|
| `research/GPUEB-analysis.md`（493 行） | §3.5 的 10 条硬事实（固件不写 SPM、xfile 格式、LK 无 `SYS_GPUEB_XFILE`、共享内存尺寸校验、无 logger、E0 实机结论） |
| `findings/GPUEB-IPI-PROTOCOL.md` | §3.6 的完整源码树清单 + SIP 0x530/op=6 上电路径 |
| `findings/ACCDET-INIT-SEQUENCE.md` | §4.2 的完整地址映射表（22 项）、§4.3 的厂商 dump 清单、§4.4 的裸机 SPMI 实现 |
| `DANGER-spmi-access.md` | §4.4 证明我们的 PMIF 路径设计正确（与 Mu-Silicium 逐条对上） |
| `research/REPO-xaga-ecosystem.md` | §1.2 的 5 个新仓库（SLNeko/panvk-pgkm10/eng_preloaders/LieppOS/OnePlusOSS） |
| `docs/GPU-ROUND-*.md` | §3.1 的新怀疑点、§3.2 的 ASN_HASH 结案 |

---

## 12. 复现命令

```bash
export https_proxy=http://127.0.0.1:7897 http_proxy=http://127.0.0.1:7897
mkdir -p /tmp/xagadev && cd /tmp/xagadev

# 用户仓库全量枚举
for u in bkaneko zhaoqing412 mytiantian001 woaphone; do
  gh api "users/$u/repos?per_page=100&sort=updated" > repos-$u.json
  gh api "users/$u/starred?per_page=100" > star-$u.json
done

# 关键 clone
git clone --depth 1 -q https://github.com/ShuiLan114514/SLNeko-XNU.git -b Xaga SLNeko
git clone --depth 1 -q https://github.com/mytiantian001/Mu-Silicium.git -b pearl Mu-Silicium
git clone --depth 1 -q https://github.com/BkaNeko/xaga-mali-r54-port.git
git clone --depth 1 -q https://github.com/BkaNeko/panvk-mtk-driver.git
git clone --depth 1 -q https://github.com/wxcvm/panvk-pgkm10.git
git clone --depth 1 -q https://github.com/R0rt1z2/fenrir.git

# dts（6.12）
B=https://raw.githubusercontent.com/zhaoqing412/xaga-mt6895-k6.12/main/kernel_device_modules-6.12
curl -s "$B/arch/arm64/boot/dts/mediatek/mt6368.dtsi"
curl -s "$B/arch/arm64/boot/dts/mediatek/xaga-mt6895.dtsi"
curl -s "$B/sound/soc/codecs/mt6368-accdet.c"

# gpueb 全源码
B2=https://raw.githubusercontent.com/zhaoqing412/android_kernel_modules_xiaomi_mt6895-6.12/main/kernel_device_modules-6.12/drivers/gpu/mediatek/gpueb
for f in gpueb.c gpueb_ipi.c ghpm_wrapper.c ghpm_swwa.c; do curl -s "$B2/$f"; done

# preloader
curl -s -O https://raw.githubusercontent.com/xiaomi-mediatek-devs/mt6895_eng_preloaders/master/preloader_xaga_eng.bin
strings -n 5 preloader_xaga_eng.bin > preloader_xaga_eng.strings.txt

# 上游 panthor
for f in panthor_fw.c panthor_gpu.c panthor_device.h panthor_fw_regs.h panthor_gpu_regs.h; do
  curl -s -O "https://raw.githubusercontent.com/torvalds/linux/master/drivers/gpu/drm/panthor/$f"
done
```
