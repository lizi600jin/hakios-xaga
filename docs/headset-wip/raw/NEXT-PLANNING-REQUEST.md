# HakiOS × MT6895（红米 Note 11T Pro / xaga）—— USB 之后的路线规划请求

写给规划者。这份文档是自包含的：读完它应该足够判断下一步做什么，不需要看任何聊天记录。

日期：2026-11-24。

---

## 一、这个项目是什么

把 XNU（苹果的内核）移植到红米 Note 11T Pro 上运行。不是模拟器，是真机原生启动。

- 设备：红米 Note 11T Pro（型号 22041216C，代号 xaga），
  SoC 是 MT6895Z/TCZA，市场名天玑 8100，arm64
- 内核：XNU，来自 Apple 开源的 xnu-11215 一带，改造成能在 MTK 平台上跑
- 引导链：LK（联发科的 little kernel）→ 自制的 mtkloader → XNU
- 用户态：Darwin 的二进制（launchd、SpringBoard、backboardd 等），从 iOS 16.5 的固件里取
- 现在能进到 iOS 桌面，SpringBoard 渲染正常

构建树在 `~/IOS/hakios-mtk/logs/ufs-apfs-fstree-root-736/`，
调度脚本在 `~/IOS/hakios-mtk/coordinator/loader-build.json`，
仓库在 `~/IOS/repo/hakios-mt6895`（GitHub 私有）。

---

## 二、已经做到的（都有实测证据）

### 存储

`/private/var` 挂在一个 64 GiB 的 HFS+ 卷上，持久化，重启后数据还在。
卷名 `RedwoodVar`，`totalBlocks=16777216`、`blockSize=4096`、空闲 63.63 GiB。

实现方式：在 UFS 的 `userdata` 分区尾部切出 64 GiB 建新分区 `xnuvar`，
由自定义的 `hakios.mt6895_ufs_twoblock.c` 驱动提供块设备，
`hakios.xnusys_blk.c` 在上面实现 `/dev/xnuvar` 字符设备与块设备。

### USB —— 这是刚刚完成的部分

**完整枚举 + 双向网络，全部实测通过。**

```
ping:   4 transmitted, 4 received, 0% packet loss
        rtt min/avg/max/mdev = 1.563/1.980/2.360/0.297 ms

TCP:    手机 10.0.0.2:49152 <-> 主机 10.0.0.1:8000
        内核日志: SYN_SENT → ESTABLISHED → LAST_ACK
                  rtt 3.750 ms, bytes in/out 241/56, so_error 0
        主机日志: "GET / HTTP/1.0" 200 -
        手机日志: net_test: 241 bytes in 0.01 s, the answer begins "HTTP/1.0 200 OK"
```

设备描述符里 `iProduct = "HakiOS phone"`，Windows 设备管理器能看到这个名字。

接口布局（CDC-ECM）：

```
interface 0   class 02/06/00   Communications / Ethernet Networking / 0
              CDC Header (5)
              CDC Ethernet Networking (13): wMaxSegmentSize 1514, iMACAddress 6
              CDC Union (5): 0 → 1
              EP1 IN interrupt, 16 字节, bInterval 8
interface 1   alt 0: 无端点
              alt 1: EP2 OUT + EP2 IN, bulk, 512 字节
```

主机侧（Linux）`cdc_ether` 绑定成功，出现网卡 `enx020000006801`，
MAC 是描述符里给的 `02:00:00:00:68:01`；手机侧 `en0` 是 `02:00:00:00:68:02`。

### 其他

- GPU：Mali-G610 有驱动（`gpu/hakios.mali610.c`），能出画面但不完整
- 触摸：nvt（Novatek）驱动在跑
- 显示：fb 走 MTK 的 disp 路径，SpringBoard 能创建 IOSurface 并渲染

---

## 三、USB 这一层的实现细节（规划时需要知道的约束）

### 硬件

MT6895 的 USB 控制器是 MTU3（`0x11201000`），PHY 是 xsphy（`0x11e40000`）。
USB 和 UFS 完全独立：UFS 在 `0x112bc000` / `0x112b8000`。

### 现在的数据通路是 PIO，不是 DMA

每个包通过 CPU 拷进/拷出 FIFO 寄存器（`U3D_FIFO2`，偏移 `0x0320`）。
`DMAREQEN`（`TX2CSR0` bit 29）保持 0。

- 优点：简单，已经验证，八种长度（1/2/3/511/512/513/1024/1514）全部正确
- 缺点：慢。512 字节一个包，每包一次 CPU 搬运

控制器支持 QMU（Queue Management Unit），`qfcr 0x1ff01ff` 说明是 gen2 qmu 模式。
`TXQCSR2` / `RXQCSR2` 在 `0x0520` / `0x0620`，有 `ACTIVE`（bit15）和 `STOP`（bit4）。
**没有实现 QMU，用了它就要写 DMA ring、descriptor、中断处理。**

### 几个踩过的坑（避免重复）

1. **`U3D_EP_RST`（`0x0804`）是电平保持寄存器，不是自清零脉冲。**
   写 1 之后必须显式写 0 才释放。等它自己变 0 会永远等下去，
   而端点会一直停在复位态——`EP0CSR` 还能读出配置，但 `SETUPPKTRDY` 永不锁存。
   这个坑让项目卡了好几轮。

2. **`EP0_TXPKTRDY` 靠轮询抓不到。** 写进去之后硬件瞬间处理掉，
   读回来就是 0。判断完成只能看 `EP0ISR`（`EPISR` bit 0）。
   同理 EP2 的 IN 完成看 `EPISR` bit 2，OUT 看 bit 18。

3. **`EP_RST` 的方向位分布和 `EPISR` 相反**：
   IN 在 `bit 16+n`，OUT 在 `bit n`。

4. **PHY 的 DTM0/DTM1 寄存器很容易搞混。**
   厂商模块 `phy-mtk-xsphy.ko` 里 `dtm0 &= ~(1<<18)` 是清 `FORCE_SUSPENDM`。
   写成 `dtm1` 就会让 PHY 一直挂在挂起态，D+ 不拉，主机什么也看不到。
   正确的 9 步在 `docs/headset-wip/raw/ufs-usb-801-802/` 和 803-804 那两份文档里。

5. **`IFF_UP` 必须自己设。** XNU 里它只在 `SIOCSIFADDR` 里被设置，
   没有进程跑 `ifconfig` 就永远不会设，协议层就不把帧交给驱动的 `transmit()`。

6. **launchd 要求 plist 权限 `0644`**，`0664` 会被拒绝并报
   `bad ownership/permissions`。

### 自定义 loader 的一个已知问题（已修）

loader 在把控制权交还给 Android 之前会做一次 UFS 读取审计，
lease 结构在 `0x60100000`（`UR_LEASE_PA`），四个字段 magic/version/trial/state。

原来的逻辑：state 必须是 `UR_UNUSED`(0) 或 `UR_RESTORED`(2)。
XNU 侧 `ur_atomic_finish(restored=false)` 会写 `UR_POISONED`(3)，
而 `POISONED` 之后没有任何合法路径回到 `UNUSED`，
于是 loader 每次启动都停在 `for(;;) wfe`，要人手按住音量下+电源进 fastboot 才能救。

已修：把「保护存储」和「允许启动」分开，只有 state 仍是 `CLAIMED`(1) 时才等。

---

## 四、现在要规划什么

USB 网络通了之后，**下一步做什么，按什么顺序**。

### 候选 A：爱思助手 / iOS 服务栈

操作者有台 iPhone 管理工具「爱思助手」（`cn.i4Tools`，Linux deb，已装在这台机器上）。
它依赖 `libimobiledevice` + `libusbmuxd`，走的是 Apple 的 usbmux/lockdown/AFC 协议。

现在的缺口：

```
① USB 枚举                    ✅ 有
② Apple VID/PID (05ac:xxxx)    ❌ 我们是 18d1:d00d
③ usbmuxd 认到设备             ⬜ 系统装了 /usr/sbin/usbmuxd，没运行
④ lockdownd 握手               ❌ 内核里没有
⑤ AFC 文件服务                 ❌ 没有
```

③④⑤ 要在 XNU 里实现一整套 iOS 服务栈。lockdown 需要配对证书交换，
AFC 是文件传输协议，两个都不小。

**已识别的风险**：改 VID/PID 会破坏现在能用的 ECM 链路
（`18d1:d00d` 是网络能通的前提，改了主机侧驱动可能就不绑了）。

### 候选 B：用现有的 TCP 做实际可用的工具

既然 ping 和 TCP 都通了，可以在上面跑：

- 文件传输（scp / 自制的 rsync-lite）
- 一个 shell（把 `redwood/bin` 里现有的二进制暴露给主机）
- 端口转发

优点：工作量小，能立刻用起来。
缺点：不如 AFC 那样和 iOS 生态原生整合。

### 候选 C：基带

原定目标之一。设备现在没有蜂窝网络，不能打电话。

MT6895 的 modem 是外挂的，走 `hakios.cell.c`（IPA 路径）。
这块完全没开始，估计是三个候选里最难的。

### 候选 D：GPU

Mali-G610 现在能出画面但可能有缺失。`gpu/hakios.mali610.c` 存在。
「能出画面」和「渲染完整」是两回事，需要具体验收标准。

### 候选 E：把 USB 从 PIO 换成 QMU

性能提升。现在是每包 512 字节的 CPU 搬运，QMU 能到接近线速。
但要写 DMA ring 和 descriptor 处理，工作量中等偏大，
而且现在 PIO 已经能用。

---

## 五、需要回答的问题

1. **五个候选的优先级排序**，理由是什么？
   判断标准应该是「对日常可用的贡献」还是「技术风险」还是别的？

2. **如果选 A（爱思助手）**：
   - 值不值得为一个调试工具写一整套 lockdownd + AFC？
   - VID/PID 换成 05ac 之后，ECM 网络会不会失效？有没有两全的办法
     （比如一个设备两个配置，或者复合设备）？

3. **如果选 B（TCP 工具）**：
   - 什么样的工具最有用？文件传输、shell、还是端口转发？
   - 需要在内核侧做什么，还是纯用户态就够？

4. **基带和 GPU 的难度排序**：
   哪个更接近「能做到」？有没有可以先验证的一小步？

5. **有没有我没想到的选项？**
   比如某个中间状态，能同时推进两件事。

---

## 六、环境和约束

- **迭代速度**：一轮刷机约 200 秒（loader 的 `loader_return_after=150`），
  内核增量编译约 2 分钟。所以一轮完整验证约 4-5 分钟，可以快速试错。
- **回退**：`boot-796.img` 是 USB 之前的稳定版（`/private/var` 正常）。
  当前 `boot_a` = `boot-804i.img`。备份齐全，BROM 深刷兜底。
- **调试手段**：内核日志通过 HLOG 机制写到 UFS 的固定块（653560+），
  下次启动时由 loader 落盘，然后从 Android 侧用 `dd` 读出来。
  所以每次都要「刷机 → 等 200 秒 → 回安卓 → 读日志」。
- **测试脚本**：主机侧有 `/home/lizi/usb-test/ep2_probe.py`（PyUSB），
  可以在 XNU 运行期间操作端点。
- **用户态工具**：`net_test`（自制，支持 `show` / `addr` / `dhcp` / `tcp` /
  `resolve` / `fetch` / `reach` / `wait` 等子命令），在镜像的 `/redwood/bin/` 下。
- **launchd job**：`com.hakios.usbecm.plist` 现在会在开机 24 秒后给 `en0`
  配 `10.0.0.2/24`，然后连主机的 `10.0.0.1:8000` 做一次 TCP 测试。

---

## 七、附属信息：文档在哪

```
docs/headset-wip/raw/ufs-usb-798-799/   五层问题的排查过程
docs/headset-wip/raw/ufs-usb-800/       EP0 枚举打通的根因
docs/headset-wip/raw/ufs-usb-801-802/   EP2 与 PIO 回环
docs/headset-wip/raw/ufs-usb-803-804/   ECM 描述符、ping、TCP、loader 死锁
```

每份都有 README（过程）、diff（改动）、log（原始日志）。

上一份提交：`c9bcd09 USB 网络跑通：ping 与 TCP 双向全过`
