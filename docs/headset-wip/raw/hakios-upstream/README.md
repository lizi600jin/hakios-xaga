# HakiOS 上游文档与工具（`oosexgang/hackios-xnu-kernel`，`mtk` 分支）

> 2026-11-15 22:15 抓取。**这是我们项目的上游原始项目**，
> 它做过另一台 MTK 手机（Infinix Hot 20 X6826B / MT6768）的完整移植。
>
> 本地 `~/IOS/hackios` 停在 `8e61187`（`main` 分支），
> 上游 `main` 已到 `63fc2b46`，**`mtk` 分支更新到 11-10**（比 `main` 的 11-06 新）。

---

## 1. 抓下来的文件

| 文件 | 大小 | 内容 |
|---|---|---|
| **`mtk-porting.md`** | 17962 B | **MTK 移植全流程**，从 stock Android 到 home screen |
| **`metal.md`** | 71088 B | **Metal/GPU 的完整分析** —— 边界在哪、怎么实现 |
| `air.md` | 47738 B | AIR 着色器格式分析（456 个系统模块） |
| `from-a-clone.md` | 6196 B | 从零 clone 到跑起来的每条命令 |
| `bluetooth-mtk-porting.md` | 9874 B | 蓝牙移植 |
| `camera-mtk-porting.md` | 5956 B | 相机移植 |
| **`gpu-probe.sh`** | 1664 B | **官方 GPU 探针脚本** |

---

## 2. 🔴 最重要的三个发现

### 2.1 GPU 的官方路径与我们不同

`mtk-porting.md:136` 原文：

> **the GPU** (`tools/gpu-probe.sh`, then `redwood_mali.c`, with
> **Mesa's panfrost in user space**)

**上游用 Mesa panfrost（用户态）+ 一个简单的内核驱动 `redwood_mali.c`，
不是我们现在走的 panthor（内核态完整 CSF 驱动）。**

`[DSH推断]` 这可能意味着：**上游没试图在内核里启动 CSF 固件，而是把 GPU 交给
用户态的 Mesa 通过已存在的接口访问**。如果成立，这是一条比我们当前路线短得多的路。
**但这需要 Codex 核实** —— 特别是 `redwood_mali.c` 到底做到什么程度。

### 2.2 Metal 的边界「很窄」

`metal.md` 开头：

> **边界很窄。** 内核里已经有 `IOAcceleratorES` 服务（故意留空），
> 所以只缺它上面的两个字符串属性和一个包含私有 `_MTLDevice` 子类的 bundle。

**具体五步**（`metal.md` §1）：

1. `MTLRegisterDevices` 用 `IOServiceMatching("IOAcceleratorES")`
2. 对每个服务调 `getMetalPluginClassForService`，读 `MetalPluginName` 属性
3. 拼出 `/System/Library/Extensions/<name>.bundle` 并打开
4. 读 `MetalPluginClassName`，**必须 `isSubclassOfClass:_MTLDevice`** 否则丢弃
5. `[[class alloc] initWithAcceleratorPort:服务]`

**关键**：`MTLCreateSystemDefaultDevice` 拿到空列表时**静默返回 nil**，不打印任何东西。
**这解释了为什么 iOS 里 GPU 不可用却没有任何错误信息。**

### 2.3 上游用 ramoops 取日志，不是自定义 logbook

`gpu-probe.sh` 的机制：

```bash
adb shell 'su -c "printf DARWIN-REDWOOD:GPU-PROBE > /dev/pmsg0"'
adb reboot
adb shell 'su -c "cat /sys/fs/pstore/console-ramoops-0"' > logs/gpu-probe.log
```

**流程**：Android 往 `/dev/pmsg0` 写请求 → loader 读到并擦除 → 启动 XNU →
XNU 把日志写进 ramoops → 重启 → Android 从 `/sys/fs/pstore/` 读。

---

## 3. 我们设备上的可行性核查（已实测）

| 项 | 状态 |
|---|---|
| `/dev/pmsg0` | ✅ **存在**（`c-w--w--w- 1 root log 253,0`） |
| `/sys/fs/pstore/pmsg-ramoops-0` | ✅ **存在**（65524 B） |
| **`console-ramoops-0`** | ❌ **不存在** |
| ramoops 在 cmdline | ✅ `mem_address=0x48090000`、`mem_size=0xe0000`、**`pmsg_size=0x10000`**、**`console_size=0x40000`** |

**解读**：ramoops **已经配好了三个区**（pmsg/console/其他），
但 `console-ramoops-0` 没出现在 pstore 里，说明**当前 Android 内核没把 console 区注册进 pstore**，
或者它的 backend 名字不同。

**`[DSH推断]` 如果让 XNU 直接往 `0x48090000 + pmsg_size` 那块 console 区写，
就能用上游那套成熟的取日志流程** —— 比我们现在的自定义 logbook 更省事，
而且 `gpu-probe.sh` 那套「一次请求、一次重启」的循环可以直接复用。

**但这需要核实**：那块内存现在归谁、Android 会不会覆盖它、XNU 怎么写进去。
**这是 Codex 的事。**

---

## 4. `mtk-porting.md` 里对我们直接有用的部分

### 4.1 「什么是你的、什么不是」

| 每台手机相同的（已写好） | 你要自己找并写的 |
|---|---|
| 内核本体、GICv3、PSCI、loader_return、platform expert | loader 的板级常量 |
| 镜像格式与打包（`chainboot.py`）、运行循环 | 处理器顺序、哪些是大核 |
| iOS 侧（`ios165/fetch.sh`、`userland/build.sh`、注入库） | **触摸控制器、GPU 的电源序列、按键、射频、相机** |
| 日志经 ramoops 回来的路（`mtkloader/mtkdbg.h`） | ramoops 区地址、LK framebuffer 地址 |

**注意**：**GPU 的电源序列明确列在「你要自己写」那一侧。**

### 4.2 手机必须允许的五件事

1. 解锁 bootloader + 能写一个分区
2. **Android 下有 root**（运行循环要 `su`）
3. **Android 内核里有 ramoops**（`console-ramoops-0` 重启后存在）
4. **一条不依赖你正在写的分区的退路**（A/B 机型 recovery 在 boot 里，
   `boot_a` 挂死就没有 fastboot；X6826B 用 mtkclient 救）
5. 系统镜像的空间（X6826B 是 4 GiB 的 `xnusys`，从 `userdata` 尾部切出来）

**第 4 条我们现在靠用户手动回 fastboot，这是同一条原则。**

---

## 5. 证据边界

| 已核实 | 未核实 |
|---|---|
| 六份文档 + 一个脚本，全部 HTTP 200 | **`redwood_mali.c` 到底做到什么程度** |
| 我们设备有 `/dev/pmsg0` 和 pmsg-ramoops-0 | `console-ramoops-0` 为何不存在 |
| ramoops 三区已配好 address/size | XNU 能否安全写进 console 区 |
| `metal.md` 的 Metal 五步流程 | 那个 `_MTLDevice` 子类 bundle 是否已存在 |
