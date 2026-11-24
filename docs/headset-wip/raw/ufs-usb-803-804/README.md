# USB 网络跑通：ping 与 TCP 双向（Trial803–804）

2026-11-24。**主机 `10.0.0.1` 与手机 `10.0.0.2` 之间，ICMP 和 TCP 全部通过 USB 线往返。**

## 一、结果

**ping**
```
64 bytes from 10.0.0.2: icmp_seq=4 ttl=64 time=1.56 ms
4 packets transmitted, 4 received, 0% packet loss
rtt min/avg/max/mdev = 1.563/1.980/2.360/0.297 ms
```

大包三档全过（对应 512 / 513 / 1514 字节帧）：
```
-s 470:  0% packet loss
-s 471:  0% packet loss
-s 1472: 0% packet loss
```

**TCP** — 手机主动连主机的 HTTP 服务：

手机侧（`net_test` 输出）
```
net_test: addr en0 = 10.0.0.2/24: set
net_test: en0: flags 0x8843 up running, type 0x6, address 02:00:00:00:68:02
net_test: en0: inet 10.0.0.2 netmask 255.255.255.0
net_test: tcp 10.0.0.1 port 8000: connected in 0.01 s
net_test: tcp 10.0.0.1: 241 bytes in 0.01 s, the answer begins "HTTP/1.0 200 OK"
```

内核协议栈
```
tcp connect outgoing: [10.0.0.2:49152 <-> 10.0.0.1:8000] interface: en0 (skipped: 0)

SYN_SENT     SYN in/out: 0/1
ESTABLISHED  SYN in/out: 1/1
LAST_ACK     Duration 0.12 sec  Conn_Time 0.4 sec
             bytes in/out: 241/56  rtt 3.750 ms  so_error: 0
             SYN in/out: 1/1  FIN in/out: 1/0  RST in/out: 0/0
```

主机侧
```
10.0.0.2 - - [24/Nov/2026 18:44:10] "GET / HTTP/1.0" 200 -
```

**三处独立记录一致：手机说连上了，内核说握手完成，主机说收到了请求。**

## 二、阶段 2 的四个 trial 全部通过

| Trial | 内容 | 证据 |
|---|---|---|
| 801 | EP2 配置与生命周期 | `maxpkt 512/512`、`type bulk`、`segsize 9`、`DMAREQEN 0`、`EP_RST` 释放、`EPIER 0x40004` |
| 802 | PIO 回环，八种长度 | `1/2/3/511/512/513/1024/1514` 全过，包长分布与设计表逐项吻合 |
| 803 | ECM 描述符与 `cdc_ether` | `Driver=cdc_ether`，主机出现 `enx020000006801` |
| 804 | 接 `hakios_net`，双向流量 | ping 0% 丢包，TCP 200 且 241 字节返回 |

## 三、803：ECM 描述符（两个 bug 导致 probe 返回 EINVAL）

### MAC 字符串多两个字符

```c
"02000000006801"   /* 14 个字符 */
"020000006801"     /* 12 个字符，正确 */
```

`usbnet.c:get_ethernet_addr()` 要求正好十二位十六进制，否则 `-EINVAL`，
也就是 dmesg 里那句 `probe with driver cdc_ether failed with error -22`。

### 字符串索引错位

MAC 字符串原本插在索引 5 的位置，而描述符里 `iMACAddress = 6`。
那会读到 `"HAKIOS data"`，同样 `-EINVAL`。

两个都修了才绑定成功。

## 四、804：五个 bug，一个比一个隐蔽

### ① `void` 函数当 `bool` 用 → 内核在 23 秒崩溃

```c
if (usb_service_once(mac_rd(U3D_COMMON_USB_INTR), &last_intr)) {   // 返回 void
```

Trial799 时这个函数有 `bool` 返回值，Trial802 重写时去掉了返回值但留下了这个用法。
C 里把 `void` 用在条件位置是未定义行为，**clang 插了 `brk #0x1`**。

崩溃位置：`_usb_thread + 0x30`，反汇编：

```
0xfffffff00750a6e0:  bl   _usb_service_once
0xfffffff00750a6e4:  brk  #0x1
```

每轮都停在同一个地方，就在 `usb_thread` 主循环第一圈。

### ② `ecm_transmit()` 从不被调用 → 栈不发帧

`net.c:net_output()` 里：

```c
if (length <= HAKIOS_NET_FRAME_MAX && n->up) {
    sent = n->link->transmit(packet, length);
}
```

`n->up` 是 true，但帧还是不出来。真正的原因是 **`IFF_UP`**：
它只在 `SIOCSIFADDR` 里被设置，也就是要有个进程跑 `ifconfig en0 up`。
没有进程做这件事，协议层就不把帧交给 `net_output()`。

修法：链路 up 时自己设。

```c
if (up) {
    ifnet_set_flags(n->ifp, IFF_UP | IFF_RUNNING, IFF_UP | IFF_RUNNING);
} else {
    ifnet_set_flags(n->ifp, 0, IFF_UP | IFF_RUNNING);
}
```

理由写进了注释：这是一条存在时间等于端点存在时间的 USB 链路，
没有哪个进程适合替它做这个决定。

### ③ 手机侧 `en0` 没有 IP 地址 → 收到了也不回

即使 `IFF_UP` 设好，`en0` 没有地址，ARP 层仍然没有源地址可写。
所以收到 109 个帧，发出 0 个。

解法：给 `net_test` 加 `addr` 子命令，走 `SIOCAIFADDR`（ifconfig 用的同一个请求）。

```c
error = ioctl(s, SIOCAIFADDR, &request);
```

### ④ launchd 拒绝加载 plist → 权限不对

```
com.apple.xpc.launchd <Error>: Caller specified a plist with bad ownership/permissions
com.apple.xpc.launchd <Error>: Failed to bootstrap path: /redwood/LaunchDaemons/com.hakios.usbecm.plist
```

`cp` 保留了源文件的 `0664`，launchd 要 `0644`。

### ⑤ `build.sh` 在第 157 行 `rm -rf root`，我的 copy 全被删

`root/` 每次重建。而且 `PREBUILT=1` 走的分支里没有编译 `net_test`，
所以 `[ -f root/bin/net_test ]` 判断为假，plist 也没进去。

解法：直接往重建后的 `root/` 里放文件，再手工 `mkapfs.py` 生成镜像。

## 五、顺带修好的 loader 死锁

之前每轮刷机后设备都会停在

```
ufs-read guard: return audit refused; no HLOG/Android IO; manual fastboot required
```

要人手动按住音量下+电源进 fastboot 才能救。

原因是一个设计上的死锁：

```
进 XNU 前  ur_guard_initialize()              → state = UR_UNUSED = 0
XNU 退出   ur_atomic_finish(restored=false)   → state = UR_POISONED = 3
loader 返回 ur_guard_format_return() 返回 0   → for(;;) wfe
```

`POISONED` 之后没有任何合法路径回到 `UNUSED`：初始化在进 XNU 前跑，
下毒在退出时写，两者之间没有清理机会。所以毒过一次，之后每次启动都卡。

修法：把「保护存储」和「允许启动」分开。存储已经被上面的检查保护过了，
只有 lease 仍是 `CLAIMED`（真的有人在写）时才值得等。

```c
if (ufs_return_snapshot.state == UR_CLAIMED) {
    for (;;) { __asm__ volatile("wfe"); }
}
out("ufs-read guard: lease not usable (state=");
out_hex(ufs_return_snapshot.state);
out("); storage untouched, booting anyway\n");
```

顺带修了一个隐蔽问题：`ufs_return_snapshot` 只在观察成功时才被赋值，
失败时是零值，那样 `state` 读出来是 `0`（`UR_UNUSED`），`CLAIMED` 判断永远不成立。

## 六、文件

| 文件 | 内容 |
|---|---|
| `usb-803-804.diff` | USB 驱动相对 Trial800 的改动 |
| `net-804.diff` | `hakios.net.c` 的 `IFF_UP` 改动 |
| `loader-lease-fix.diff` | loader 的 lease 死锁修复 |
| `com.hakios.usbecm.plist` | 那两条 launchd job |
| `tcp-804i.log` | 57 条 TCP 与接口日志 |
| `hakios.mt6895_usb.c` | 111 KB |
| `hakios.net.c` | 16 KB |

镜像：`trial736/boot-804i.img`

## 七、下一步

USB 的「能 ping、能 TCP」已经做到。剩下的是让它成为日常可用的工具：

- **DHCP 而不是静态地址**：现在 `10.0.0.2` 写死在 plist 里
- **Windows 侧**：`UsbNcm.sys` 或 `cdc_ether` 的等价驱动
- **AFC / lockdownd**：爱思助手那类工具要的东西，比网络栈重得多
- **QPIO / 更快的批量传输**：现在是 PIO，每包 512 字节

基带和 GPU 仍是原定目标，与 USB 独立。
