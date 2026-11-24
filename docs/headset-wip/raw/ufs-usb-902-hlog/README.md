# Stage 1：调试基建（`hakid` + 600 秒窗口），以及 `/dev/hlog` 未解决

2026-11-24 夜。按 `stage1_decisions.md` 执行，镜像 `boot-902` 系列。

## 一、做到了

### `hakid`：从主机远程在手机上跑程序

```
$ python3 haki_cli.py ping
  HAKID 1 port 2222 root /private/var/hakios
  PONG 49   (10.9 ms round trip)

$ python3 haki_cli.py exec "net_test show"
  net_test: en0: flags 0x8843 up running, type 0x6, address 02:00:00:00:68:02
  net_test: en0: inet 10.0.0.2 netmask 255.255.255.0
  EXIT 0
```

**没有刷机、没有等 200 秒、没有 `dd` 读日志。**

协议：`PING` / `EXEC` / `PUT` / `GET` / `LS` / `LOG` / `RETURN`
实现：`libmini` 静态编译，34 KB，给 `libmini` 补了 `listen()` / `accept()` / `sockaddr_in`。

### 600 秒窗口

```
19:33:15  刷入 boot-902.img
19:44:11  自动返回 Android
```

**10 分 56 秒，正好是 600 秒窗口加启动开销。** 之前 150 秒的窗口不够调试。

### 静态 ARP

主机侧 `ip neigh replace 10.0.0.2 lladdr 02:00:00:00:68:02 nud reachable`：

```
rtt min/avg/max/mdev = 2.149/3.220/4.292/1.071 ms
```

之前首包要 2003 ms（ARP 广播超时）。

### `/dev/hlog` 和 `/dev/hakios-return` 节点出现了

```
LS /dev
  file 145 hlog
  file 147 hakios-return
```

**关键是 `devfs_make_node()`，不是 `cdevsw_add()` 单独能做到的。**

```c
hlog_major = cdevsw_add(-1, &hlog_cdevsw);
devfs_make_node(makedev(hlog_major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0444, "hlog");
```

需要 `#include <miscfs/devfs/devfs.h>`。这个在 `hakios.audio.c` 里有先例。

---

## 二、没解决的：`/dev/hlog` 读出来的内容是零

### 现象

```
LOG 命令 → 5792 字节，全部是 0x00
```

不是空格，是零字节。

### 已确认的

`msgbuf` 确实是内核日志的落点：

```c
// osfmk/kern/printf.c:846
void conslog_putc(char c) {
    if (!disableConsoleOutput) console_write_char(c);
#ifdef MACH_BSD
    if (!kernel_debugger_entry_count) log_putc(c);   // ← 这里
#endif
}

// bsd/kern/subr_log.c:570
void log_putc_locked(struct msgbuf *mbp, char c) {
    mbp->msg_bufc[mbp->msg_bufx++] = c;
    if (mbp->msg_bufx >= mbp->msg_size) mbp->msg_bufx = 0;
}
```

`loader_return.c` 也直接用 `msgbufp`（第 333 行有 `extern struct msgbuf *msgbufp;`）。

### 我试过的三种读法

1. **从 `msg_bufx` 线性读** → 全零。因为 `msg_bufx` 是"下一个要写的位置"，
   它后面的区域还没写过，就是零。
2. **从 `msg_bufx` 绕环一圈，跳过 `\0`** → 全零。理论上应该能读到内容。
3. **加诊断输出看 `magic` / `size` / `bufx` / `produced`** → 返回 0 字节，
   诊断没出来，可能是缓存逻辑（`hlog_linear_tick` 判断）写错了。

### 需要判断的

**`msg_bufx` 在我的读取时刻可能确实是 0。** 如果内核的 printf 输出没有经过
`conslog_putc`，或者 `disableConsoleOutput` 为真，msgbuf 就是空的。

而 HLOG 落盘的内容是**完整的**（之前每次都能从 UFS 读到 4000+ 行）。

**所以有一个可能：这个项目的 `printf` 走的不是 `conslog_putc`，而是另一条路**
（比如直接写 `debug_buf_ptr`，`loader_return.c` 第 323 行有
`extern char *debug_buf_base, *debug_buf_ptr;`）。

**如果是这样，`/dev/hlog` 应该读 `debug_buf_base` 而不是 `msgbufp`。**

这一点我确认不了，需要知道这个移植版的 printf 到底写到哪。

---

## 三、两个自己造成的 bug（已修）

### ① `waitif` 死等（902b 网络完全不通）

```c
while (!show(interface, true) && ...) { usleep(200000); }
```

`show(iface, quiet)` 返回的是**"有 IPv4 地址"**，不是"接口存在"。
而配地址的动作在 `waitif` 之后——逻辑上永远等不到。

现象：主机 `RX = 0`，一个包都收不到。

修法：加 `interface_exists()`，判断 `AF_LINK` 条目是否存在。

### ② `hakid` 单线程卡死（902 里失联）

`LOG` 命令里 `pread` 如果阻塞，整个守护进程就卡住——单线程，一个请求卡死全部。

修法：`O_NONBLOCK` + 轮询 + 静默计时上限。

---

## 四、构建系统的坑（重要，后面会反复遇到）

这份隔离构建树 `logs/ufs-apfs-fstree-root-736/` **不会**执行
`tools/hakios-config.py kernel`，因为 `make-kernel.sh` 里有这个条件：

```bash
if [ -d "$default_tree" ] && [ "$build_tree" = "$(cd "$default_tree" && pwd -P)" ]; then
    xnu/sync-drivers.sh "$build_tree"
    python3 tools/hakios-config.py kernel --tree "$build_tree"
fi
```

**只有在默认树上才会同步驱动列表和配置。**

具体表现：

- 往 `bsd/conf/files` 加新文件 → **无效**
- 往 `config/MASTER.hakios` 加 option → **无效**
- 往 `BUILD/.../config.DEVELOPMENT_ARM64` 加 option → **无效**（会被覆盖）

**唯一可靠的加代码方式：把代码折进已经编译的 `.c` 文件。**

所以 `/dev/hlog` 的三百多行是写在 `hakios.mt6895_usb.c` 里的。
后面加 GPU 探针、基带探针时会反复遇到这个限制。

---

## 五、下一步的三个选项

**A. 先解决 `/dev/hlog` 的数据来源**

需要知道这个移植版的 `printf` 写到哪。候选：
- `msgbufp->msg_bufc`（我试过，全零）
- `debug_buf_base` / `debug_buf_ptr`（`loader_return.c` 引用了）
- 别的自定义缓冲

**B. 跳过实时日志，先做别的**

`hakid` 的 `EXEC` 已经能用，靠它 + `CAT /dev/xxx` 能查很多东西。
实时日志不是必需品，只是让调试更舒服。

**C. 做 NAT（任务 1.4，纯主机侧零风险）**

`iptables` masquerade + 手机默认路由，验证 HakiOS 的外网连通性。

**我倾向 A**，但要一个信息：`printf` 的输出到哪。

---

## 六、文件

| 文件 | 内容 |
|---|---|
| `usb-902-hlog.diff` | USB 驱动相对 Trial800 的全部改动，含 300 行 hlog |
| `net-902.diff` | `hakios.net.c` 的 `IFF_UP` 改动 |
| `hakid.c` | 设备侧守护进程，约 600 行 |
| `haki_cli.py` | 主机侧 CLI |
| `mini.h` | `libmini` 加了 `listen`/`accept`/`sockaddr_in` |
| `com.hakios.hakid.plist` | `KeepAlive=true`，`ThrottleInterval=5` |
| `com.hakios.usbecm.plist` | 改用 `waitif` |

镜像：`boot-902e.img`（当前 `boot_a`）
回退：`boot-904i` 之前的 `boot-901b.img`、`boot-804i.img`、`boot-796.img`
