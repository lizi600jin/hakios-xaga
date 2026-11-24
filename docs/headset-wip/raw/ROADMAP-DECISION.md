# 路线选择：继续做到爱思助手能识别，还是转做别的

写给规划者。Stage 1 已完成（见 `ufs-usb-904-stage1-done/README.md`）。
现在需要决定下一步往哪走。这份文档把**当前还缺什么**列清楚，以便判断。

日期：2026-11-24。设备：红米 Note 11T Pro（xaga），MT6895Z / 天玑 8100。
镜像基线：`boot-904.img`。

---

## 一、现在已经能用的（实测）

### 存储

`/private/var` 挂在 64 GiB 的 HFS+ 卷 `RedwoodVar` 上，持久化，重启后数据还在。

### USB

完整枚举 + CDC-ECM 网络，双向：`ping` 0% 丢包（RTT 1.8–4.4 ms），
TCP 往返（手机连主机 HTTP 服务，`HTTP/1.0 200 OK`，241 字节）。

### USB 吞吐（今晚实测）

```
协商速度      480 Mbps (High Speed)
上行 4 MB     399 KB/s  (0.39 MB/s)   = 理论上限的 80%
下行 4 MB     406 KB/s  (0.40 MB/s)   = 理论上限的 81%
ICMP          0% 丢包，RTT 1.452 / 3.099 / 12.974 ms
TCP 往返      16–20 ms
错误统计      0 errors, 0 dropped
```

**利用率只有 0.67%，而且我知道为什么** —— 两个原因都在我自己的代码里：

**① `usb_thread` 每 1 毫秒才服务一次**

```c
for (unsigned i = 0; i < 50u; i++) {
    usb_service_once(...);
    delay_for_interval(1, NSEC_PER_MSEC);   // ← 这一睡就是天花板
}
```

**② `ep2_in_service` 每次只发一个 512 字节包**

```c
chunk = ep2_tx_len - ep2_tx_off;
if (chunk > EP2_MPS) chunk = EP2_MPS;   // 512
...
return;   // 等下一轮
```

**512 字节 ÷ 1 毫秒 = 500 KB/s 上限。实测 399–406，正好是 80%。**

数学完全对得上。

**改进空间**：

| 改法 | 预期吞吐 | 风险 |
|---|---|---|
| 每个循环发满一个 mbuf（1514 字节） | ~1.4 MB/s | 低，改 `chunk` 上限 |
| 把 1 ms 降到 100 µs | ~14 MB/s | 中，CPU 占用上升 |
| 改中断驱动（去掉轮询） | 更高 | 中，要看 MTU3 中断路径 |
| 启用 QMU/DMA | 接近 480 Mbps | 高，Neon 说留到最后 |

**这是 Stage 1 的遗留项**：功能通了，但性能只有该有的 1/100。
对"推个探针程序、抓个 framebuffer"够用（1 MB 文件 2.6 秒），
但对"推几百 MB 的系统镜像"就是不可接受。

### 调试基建（今晚完成的）

```
实时内核日志    /dev/hlog，从开机第 19 秒起，含 ASCII 金丝雀诊断头
远程执行       hakid 监听 10.0.0.2:2222，跑 /redwood/bin 下的程序
文件传输       1 MB push/pull，SHA256 一致
主动退出       /dev/hakios-return，RETURN 后 10 秒干净回安卓
窗口           600 秒，可提前退出
```

### 显示与输入

SpringBoard 能渲染，触摸（Novatek nvt）能用，音量键能用。

---

## 二、还缺什么 —— 按子系统

### ① GPU：能出画面，但 GPU 从来没参与

**现状**（HLOG 原文）：

```
hakios: SpringBoard: no GPU draws, vibrant views are plain (the lock screen's clock is white)
MiniIOMobileFramebuffer: 49 frames in 5043 ms (9 a second), 1477 us each,
                         after 0 us waiting for the GPU
```

- `after 0 us waiting for the GPU` —— **没有任何一帧等过 GPU**，说明是纯 CPU 合成
- `no GPU draws` —— SpringBoard 自己检测到没有 GPU 绘制路径
- 帧率 ~9 fps，纯 CPU 软件渲染的结果

**`hakios.mali610.c` 是什么状态**：文件存在，编进了内核（`HAKIOS_MALI610`），
但**没有一条 mali 的日志**说明它初始化过。需要查它是 stub 还是半成品。

**缺的具体东西**（我判断，待确认）：

- Mali-G610 的 IOMMU / 页表设置（GPU 要访问系统内存）
- job slot 调度（Mali 的 `JOB_SUBMIT` 接口）
- fence 与同步（CPU 等 GPU 完成、GPU 等 CPU 写数据）
- 用户态驱动接口（iOS 这边是 `IOAccelerator` + `AGX` 那一套，需要自己实现一层）

**这是原定目标里最接近"能做到"的**：有驱动文件、有出画面的基础、
现在还有实时日志和文件传输可以辅助测绘。

### ② 基带：一行代码都没跑过

**现状**：`hakios_cell_init` / `hakios_ipa_init` **都不是完整实现**
（`.config` 里没编，`hakios.stubs.c` 里是空函数）。

**HLOG 里唯一的痕迹**是 SpringBoard 报的界面错误：

```
hakios: cellular: _UIStatusBarDataCellularEntry has no such method
```

这是 UI 层缺少方法，不是基带驱动。

**缺的具体东西**：

- MT6895 的 modem 是**外挂**的（不在 SoC 里），走 PCIe 或共享内存
- `mpss`（Modem Processor SubSystem）的固件加载
- `ipa`（IP Accelerator）数据面：AP 和 MD 之间的包转发
- `ccci`（Cross-Core Communication Interface）：AP 和 MD 的控制通道
- 上层：`CommCenter` 需要和基带服务对话（iOS 的 `basebandd` / `CommCenter`）
- SIM 卡、射频校准、网络注册

**这是六个方向里最难的**，而且失败时往往没有日志（modem 挂了就是挂了）。

### ③ CPU：能用，但可能没用满

**现状**：

```
boot-args: cpus=4
```

只用了 4 个核。天玑 8100 是 8 核（4×A78 + 4×A55）。

**`hakios.cpufreq.c` 存在但是 stub** —— 没有变频，可能固定在一个频率。

**缺的具体东西**：

- 8 核启动（PSCI 的 CPU_ON 对后 4 个核）
- EAS / 调度域拓扑（大小核的 `cpu_topology`）
- 变频（`cpufreq` 的 DVFS 表、MTK 的 `mtk_cpufreq` 接口）
- 热管理联动（降频）

**优先级低**：4 核已经能跑 iOS，不是瓶颈。

### ④ 电池：stub

`hakios_battery_init` 是空函数。`hakios.battguard.c` 有实现但不知道做到哪。

**实际影响**：iOS 的状态栏看不到电量，可能还有充电相关的界面异常。

**缺的**：MT6363 PMIC 的 `FGADC`（电量计）读取、充电状态、温度。

### ⑤ 其他 stub（`hakios.stubs.c` 里 71 个空函数）

从 HLOG 和代码看，这些是空的：

| 子系统 | 状态 | 影响 |
|---|---|---|
| `speaker` | stub | 没有声音输出 |
| `torch` | stub | 闪光灯不能用 |
| `haptic` | stub | 没有震动 |
| `camera` | stub | 相机打不开 |
| `cpufreq` | stub | 没有变频 |
| `battery` | stub | 看不到电量 |
| `wifi` | 有实现文件，未编入 | 没有 Wi-Fi |
| `bt` | 有实现文件，未编入 | 没有蓝牙 |
| `adsp` | 有实现文件，未编入 | 音频 DSP 没跑 |
| `mic` | 有实现 | 麦克风可能能用 |

**音频有个半成品**：`/redwood/hal/HakiosAudio.driver` 存在，
`hakios.audio.c` 有实现，但 `speaker` 是 stub —— 录音可能有，放音没有。

### ⑥ 摄像头

`hakios.camera.c` 有实现（4000+ 行），但 `hakios_camera_init` 在 stub 列表里没出现，
说明可能编了但没跑。需要实测。

---

## 三、现在的问题：接下来做什么

### 选项 A：继续做，直到爱思助手能识别

**需要补的链条**：

```
① USB 枚举                    ✅ 有
② 数据端点 + ECM 网络          ✅ 有
③ Apple VID/PID (05ac:xxxx)   ❌ 要改描述符
④ usbmuxd 认到设备             ⬜ 系统装了 /usr/sbin/usbmuxd，没运行
⑤ lockdownd 握手               ❌ 没有
⑥ AFC 文件服务                 ❌ 没有
```

**Neon 之前的判断**（`stage1_decisions.md` Stage 4）：

> 不应在 XNU 内核手搓 `lockdownd`/AFC；应先在现有 TCP 上验证 iOS 16.5 原生
> 用户态守护进程，最后再改 USB 复合设备。

**我核实了一下这个思路**：

`.config` 里没有编 `HAKIOS_CELL` 等基带驱动，但**用户态**是完整的 iOS 16.5
Darwin 环境——`/redwood/LaunchDaemons` 里有 23 个 plist，包括 SpringBoard、
backboardd、containermanagerd 等。

**问题**：`lockdownd` 和 `afcd` **不在**这 23 个里。查一下它们在哪。

（这一点我需要在文档外核实，规划时请考虑：如果它们不在系统镜像里，
那"复用原生用户态"这条路就不通，只能自己写。）

**我的估价**：lockdownd 的配对协议（TLS + 证书交换）+ AFC 的协议解析，
纯用户态实现大约 2000–3000 行 C。加上 VID/PID 复合设备改造和
usbmuxd 的 udev 干扰问题，**风险最高的是 udev 那条**（改 VID 后可能破坏现在能用的 ECM）。

### 选项 B：转做 GPU

**理由**：

- 这是**唯一一个"用户能直接看见差别"**的方向。现在是 9 fps 的软件渲染，
  GPU 一旦工作，界面流畅度是数量级的改变
- 已经有驱动文件和出画面的基础
- **今晚刚做的调试基建恰好是为这个准备的**：抓 framebuffer 要通过文件传输，
  看 GPU 状态要实时日志
- 风险可控：GPU 挂了不会影响已经能用的东西

**第一步**：测绘。搞清楚 `hakios.mali610.c` 里哪些是 stub、哪些 ioctl 没实现、
为什么一条 mali 日志都没有。

### 选项 B2：先把 USB 吞吐提上来

**现状**：0.4 MB/s，是可持续值的 1/100（480 Mbps 理论 60 MB/s）。

**理由**：

- **成本最低**。我上面已经查出瓶颈在哪——`usb_thread` 每 1 ms 一轮 +
  每轮只发 512 字节。改 `chunk` 上限和降低延迟，**可能几行代码就翻三倍**
- **对后面每一项都有用**：抓 framebuffer、推大文件、传日志，
  0.4 MB/s 会一直是绊脚石
- 风险可控，改坏了回到 `boot-904`

**但**：它不是"功能"，用户感知不到。界面还是 9 fps。

### 选项 C：先补基础体验（电池、声音、震动）

**理由**：这些是"日常用起来别扭"的地方。没有电量显示、没有声音、没有震动。

**代价**：每个都是独立的小工程，加起来时间不少，而且不会让界面变流畅。

### 选项 D：把核数从 4 提到 8

**理由**：现在的 4 核可能限制性能。天玑 8100 是 8 核。

**但**：如果 GPU 还是软件渲染，加核帮助有限。

---

## 四、我的判断（供参考，不是结论）

**我倾向 B（GPU）并顺手做掉 B2 的低风险部分，理由三条：**

1. **可见性最高**。现在 9 fps，GPU 通了可能是 30–60 fps。这是用户能立刻感受到的。
2. **基建刚就位**。今晚做的日志/执行/传文件，正好是 GPU 测绘需要的东西。
   如果现在转去做爱思助手（用户态协议），这套基建用不上。
3. **风险最低**。GPU 出问题不影响存储、USB、触摸这些已经能用的。

**对 A（爱思助手）的顾虑**：

- 它是**调试工具**，不是日常功能。手机能跑 iOS 桌面了，但读文件还要靠爱思，
  这个价值不如"界面流畅"
- 需要改 VID/PID，**可能破坏现在能用的 ECM**（Neon 提过 udev 干扰的风险）
- lockdownd + AFC 是从零写协议栈，工作量不亚于 GPU

**但有一个反驳**：如果 lockdownd/afcd 真的在系统镜像里（只需拉起），
那 A 的成本会低很多。**这需要先查清楚。**

**关于 B2（USB 吞吐）**：它便宜、对后面都有用，但不产生用户能看见的变化。
如果决定先做 GPU，B2 可以顺手做掉（改 `chunk` 上限那两行），
不必单独占一轮。

---

## 五、需要你判断的

1. **A 还是 B？**
   我的倾向是 B，但如果你认为"能被爱思识别"对后续调试的价值更大，A 也合理。

2. **如果要查清 lockdownd 是否存在，怎么查？**
   我现在有的手段：`hakid` 的 `EXEC` 能跑 `/redwood/bin` 下的程序、
   `LS` 能列目录、`CAT` 能读文件。
   但 `/redwood` 下只有 4 个程序，系统二进制在哪、叫什么，我不知道。

3. **GPU 测绘的第一步应该是什么？**
   我打算：先查 `hakios.mali610.c` 里有多少 stub，再看为什么没有 mali 日志，
   然后决定是补 ioctl 还是先做 IOMMU。

4. **有没有我漏掉的选项？**
   比如先把 8 核开了，或者先补电池（影响状态栏观感）。

---

## 六、环境

- 一轮刷机 200 秒 + 600 秒窗口（可提前 `RETURN`）
- 内核增量编译 2 分钟
- 现在有实时日志，**改内核后不需要等窗口结束就能看效果**
- 回退镜像齐全：`boot-904.img`（当前）、`boot-804i.img`、`boot-796.img`
- `hakios.stubs.c` 里 71 个空函数是"还缺什么"的索引
