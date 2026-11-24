# Stage 1 首次上机结果与三个阻塞问题

回给规划者。按 roadmap 走完了 Stage 1 的任务 1.1 和 1.2（设备侧 `hakid`、主机侧 `haki_cli.py`），
上机实测。**核心目标达成，但暴露出三个需要你判断的问题。**

日期：2026-11-24 晚。镜像 `boot-901b.img`。

---

## 一、做到了什么

### `hakid` 在手机上跑起来了

```
$ python3 haki_cli.py ping
  HAKID 1 port 2222 root /private/var/hakios
  PONG 49   (10.9 ms round trip)
```

### 从主机远程执行了手机上的程序

```
$ python3 haki_cli.py exec "net_test show"
  net_test: lo0: flags 0x8049 up running, type 0x18
  net_test: lo0: inet 127.0.0.1 netmask 255.0.0.0
  net_test: lo0: inet6 ::1
  net_test: lo0: inet6 fe80::1
  net_test: en0: flags 0x8843 up running, type 0x6, address 02:00:00:00:68:02
  net_test: en0: inet 10.0.0.2 netmask 255.255.255.0

  EXIT 0
```

**这些信息是从主机经 USB 拿到的。没有刷机、没有等 200 秒、没有 `dd` 读日志。**

Stage 1 验收标准 1（35 秒内 `exec` 返回）实际约 50 秒达成（`en0` 在 24 秒配地址，
加 26 秒等待 + 连接），略超但可用。

### 链路质量

稳态 RTT 1.676 / 3.652 / 6.341 ms（min/avg/max），PIO 路径本身没问题。

---

## 二、问题 1：镜像里没有 shell，也没有 `ls`

`EXEC` 第一版按 roadmap 的写法走 `/bin/sh -c`，返回 `EXIT 127`——**镜像里没有 shell。**

整个用户态只有四个程序：

```
/redwood/bin/child
/redwood/bin/hakid
/redwood/bin/net_test
/redwood/bin/var_store
```

改成了直接 `execve` + 按空格切参数，并把裸名字在 `/redwood/bin`、`/bin`、`/usr/bin`
里查找。`net_test show` 就是这样跑通的。

但**依然缺基础工具**：

```
$ haki_cli.py exec "ls /dev"
  ERR ls: not found in /redwood/bin or /bin
```

**需要你判断**：

1. 是往镜像里加一套基础工具（`ls`、`cat`、`ps`、`df`、`uname`……），
   还是让 `hakid` 内置这些查询？内置的好处是不占空间、不依赖外部二进制；
   坏处是每个新查询都要改 `hakid` 重新刷机。
2. 如果加工具，从哪儿来？Darwin 的 `/bin` 和 `/usr/bin` 需要 dyld 和一堆库，
   这套 ramdisk 里没有。要静态编译（`libmini` 风格）还是别的路子？

---

## 三、问题 2：`loader_return_after=150` 太短

实测时间线：

```
19:07:02  刷入，重启
19:07:24  网卡出现（约 22 秒）
19:07:50  配好地址，可以 ping
19:09:26  已过 144 秒，设备开始准备退出
```

**实际可用窗口只有约 100 秒。** 而 `hakid` 的用途恰恰是"慢慢调试"——
每次连接、执行、看输出、改主意、再执行，100 秒根本不够。

刚才测文件传输时出现 50% 丢包，查下来是**当时正好在退出前的窗口里**，
不是数据通路的问题。

**需要你判断**：

1. 窗口调到多少合适？我倾向 **300 秒**（5 分钟），代价是每轮刷机的等待变长。
2. `RETURN` 命令已经实现了（写 `/dev/hakios-return`），但**那个设备节点不存在**
   （`ERR no return device`）。要不要在内核里加一个？这样窗口可以设得很长
   （比如 600 秒），测完主动退出，两头都不亏。

---

## 四、问题 3：实时日志（任务 1.3）的前提和 roadmap 设想不同

roadmap 的方案 A 说「内核暴露只读字符设备或只读接口读取 HLOG 环形缓冲增量」。

我核实了 HLOG 的实际机制：

```
内核 printf
    ↓
写到内核内存里的环形缓冲（struct logbook_hdr + 文本）
    ↓
内核退出时，loader 把缓冲写到 UFS 的 SYSTEM_PART 末尾（653560+ 块）
    ↓
下次启动 Android 后，用 dd 读出来
```

**环形缓冲只在内核内存里，从来不是一个可读的磁盘文件。**
所以方案 A 和方案 B 都必须改内核——这点 roadmap 判断对了。

但有两个细节值得先说清楚：

1. **缓冲的结构**（loader 侧定义）：
   ```c
   struct logbook_hdr {
       uint32_t magic;      /* 0x474f4c48 "HLOG" */
       uint32_t version;
       uint64_t len;        /* bytes of log that follow */
       uint64_t reason, code, elr, far;
       uint64_t ticks, where, sum;
   };
   ```
   文本在 `where` 偏移处。如果要在运行时读，需要知道这块内存在内核里的虚拟地址。

2. **方案 B（UDP 直发）的难点**：内核要发 UDP 需要自己构造 mbuf 并注入协议栈。
   我查了现有代码，没有任何驱动从内核直接发包，`hakios.net.c` 只提供被动的
   `transmit()` 回调。要写的话得用 `mbuf_allocpacket` + 手写 UDP/IP/Ethernet 头，
   或者走 `net_output()`。

**一个可能更简单的路子**：既然 `en0` 的发送路径已经通了，
能不能在内核里把日志文本包成一个 raw Ethernet 帧（ethertype 自定义），
直接调 `hakios_net` 的发送队列？主机侧用 `tcpdump` 或一个小 Python 脚本收。
这样绕开整个 IP/UDP 栈，代码量小很多。

**需要你判断**：走方案 A、方案 B，还是我提的 raw Ethernet 帧？

---

## 五、另外三件小事

### ① `IFF_UP` 时序

`com.hakios.usbecm.plist` 里 `wait 24` 之后才配地址。这是凭经验设的，
因为端点要等主机选 alt 1。

更稳的做法是**轮询等待 `en0` 存在**，而不是固定睡眠。需要 `hakid` 之外再加个小工具，
或者让 `net_test` 的 `waitaddr` 支持"等接口出现"。

### ② `ping` 第一次很慢

```
第一轮: rtt min/avg/max = 1.494/988.018/2003.429 ms
```

第一个包要等 ARP 解析，超时重试。后续正常。
`haki_cli.py` 已经会自动配地址，但没有预热 ARP，可以加一步。

### ③ `hakid` 的 KeepAlive

现在是 `KeepAlive=false`，崩了不会自动重启。调试期间可能想要 `true`，
但要在意它会不会掩盖崩溃。

---

## 六、下一步的三个选项

按 roadmap 的顺序，Stage 1 还差任务 1.3（实时日志）和 1.4（NAT）。

但我判断**应该先把问题 2（窗口）解决**，否则后面每一步调试都在 100 秒里抢时间。

三个可能的顺序：

**A. 先扩窗口 → 补 `ls` 等基础工具 → 再做实时日志 → 最后 NAT**
好处：每一步都在更舒服的环境里做。

**B. 先做实时日志（跳过窗口问题）**
好处：日志能实时看，窗口短一点也能接受。
坏处：改内核本身要一轮刷机，且日志通道没建好之前还是得靠 HLOG。

**C. 先做 NAT（纯主机侧，零风险）**
好处：能立刻验证 HakiOS 的外网连通性（DNS/TLS/路由完整度），
也许能发现别的子系统问题。
坏处：和调试基建关系不大。

**我的倾向是 A**，理由是窗口是后面所有事情的前提。

---

## 七、环境和文件

- 当前镜像：`trial736/boot-901b.img`
- 回退：`boot-804i.img`（USB 稳定版）、`boot-796.img`
- 设备侧：`/home/lizi/IOS/hakios-mtk/userland/src/hakid.c`（约 600 行）
- 主机侧：`/home/lizi/usb-test/haki_cli.py`
- launchd：`com.hakios.hakid.plist`（0644，`RunAtLoad`）
- `libmini` 补了 `listen()` / `accept()` / `sockaddr_in`（系统调用号 106 / 30）

一轮刷机仍是 200 秒，内核编译 2 分钟。
