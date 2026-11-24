# `/dev/hlog` 读不出内容 —— 需要你判断的一个具体问题

回给规划者。`stage1_decisions.md` 里选的方案 A（只读 `/dev/hlog` + `hakid` 推流），
设备节点已经建出来了，但**读操作会挂住**，不是返回空数据那么简单。

日期：2026-11-24 夜。镜像 `boot-902f.img`。

---

## 一、做到了什么

### 节点出现了

```
LS /dev
  file 145 hlog
  file 147 hakios-return
```

关键是这两句，少一句都不行：

```c
hlog_major = cdevsw_add(-1, &hlog_cdevsw);
devfs_make_node(makedev(hlog_major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0444, "hlog");
```

`cdevsw_add()` 只注册 switch 表，**不建节点**。`devfs_make_node()` 才建。
需要 `#include <miscfs/devfs/devfs.h>`，`hakios.audio.c` 里有先例。

### 其余 Stage 1 项都已验收

- `loader_return_after=600`：19:33:15 刷入，19:44:11 自动回 Android（10 分 56 秒）
- `waitif` 替代固定睡眠：网卡一出现就配地址
- 静态 ARP：首包从 2003 ms 降到 2.149 ms
- `hakid` 的 `PING` / `EXEC` / `LS` 正常工作

```
$ haki_cli.py exec "net_test show"
  net_test: en0: inet 10.0.0.2 netmask 255.255.255.0
  EXIT 0
```

---

## 二、问题：读 `/dev/hlog` 会挂住

### 现象演变

| 版本 | 读法的改动 | 结果 |
|---|---|---|
| 902c | 从 `msg_bufx` 裸拷贝 | 5792 字节，**全是 0x00** |
| 902d | 绕环一圈跳过 `\0` | 5792 字节，仍是 0x00 |
| 902e | 加诊断（`magic`/`size`/`bufx`/`produced`） | **0 字节**，诊断没出来 |
| 902f | 去掉 `hlog_read` 里的双重加锁 | **0 字节** |

**关键变化**：从"返回数据但内容是零"变成"返回零字节"。
说明读路径在某处提前返回了，而不是数据本身的问题。

### 902f 的具体现象

```
LOG 命令     → 0 字节
CAT /dev/hlog → 0 字节（连接后挂住，直到超时）
STAT /dev/hlog → hakid 没有 STAT 命令（我的实现缺这个）
```

**而且 `hakid` 之后就不响应任何命令了**——一个 `CAT` 把它带走了（单线程）。

---

## 三、我核实过的事实

### `msgbuf` 确实是内核日志的落点

```c
// osfmk/kern/printf.c:846
void conslog_putc(char c) {
    if (!disableConsoleOutput) console_write_char(c);
#ifdef MACH_BSD
    if (!kernel_debugger_entry_count) log_putc(c);      // ← 这里
#endif
}

// bsd/kern/subr_log.c:570
void log_putc_locked(struct msgbuf *mbp, char c) {
    mbp->msg_bufc[mbp->msg_bufx++] = c;
    if (mbp->msg_bufx >= mbp->msg_size) mbp->msg_bufx = 0;
}
```

`printf.c` 里没有这个项目的自定义改动（我搜过 `hlog`/`HAKIOS`/`loader_log`，零命中）。

### loader 读 `msgbuf` 的方式（可作为参考实现）

```c
// osfmk/arm64/loader_return.c:348
loader_log_append_msgbuf(void) {
    struct msgbuf *mb = msgbufp;

    if (mb == NULL || mb->msg_bufc == NULL || mb->msg_size <= 0 ||
        mb->msg_bufx < 0 || mb->msg_bufx > mb->msg_size) {
        return;
    }
    for (int pass = 0; pass < 2; pass++) {
        int from = pass == 0 ? mb->msg_bufx : 0;
        int to   = pass == 0 ? mb->msg_size : mb->msg_bufx;
        for (int i = from; i < to; i++) {
            if (mb->msg_bufc[i] != 0) {
                loader_log_putc(mb->msg_bufc[i]);
            }
        }
    }
}
```

**我的 902d 实现就是这个逻辑**（从 `msg_bufx` 绕一圈，跳过 0），
但结果全是零。而 loader 在 `loader_return()` 里跑同样的遍历，能拿到 4000+ 行。

**差别在哪？** 我需要你判断。

---

## 四、三个可能的解释

### ① `msgbufp` 在 `-DHAKIOS` 下与 `loader_return.c` 里的不是同一个对象

`loader_return.c` 自己定义了一份 `struct msgbuf`（注释写着 "not includable from osfmk"）：

```c
extern char *debug_buf_base, *debug_buf_ptr;

/* bsd/sys/msgbuf.h (not includable from osfmk) */
struct msgbuf {
    int   msg_magic;
    int   msg_size;
    int   msg_bufx;
    int   msg_bufr;
    char *msg_bufc;
};
extern struct msgbuf *msgbufp;
```

我从 `bsd/dev/` 里 `#include <sys/msgbuf.h>`，结构应该一样。但 `msgbufp`
在 osfmk 上下文和 bsd 上下文里是不是同一个符号，我没法确认。

**`loader_return.c` 里同时有 `debug_buf_base` / `debug_buf_ptr` 和 `msgbufp` 两个引用**——
这两个是什么关系？`printf` 写到哪个？

### ② `bsd_log_lock_safe()` 在 USB 线程上下文里不能拿

`hlog_linearize()` 里调了 `bsd_log_lock_safe()`。如果这个锁要求特定的上下文
（比如不能在 USB 线程里拿，或者会睡眠），就会挂住。

我试过去掉 `hlog_read()` 外层的锁（902f），但 `hlog_linearize()` 内部还留着。
**但没有测过去掉它。**

### ③ `uio` / `uiomove` 的用法不对

```c
while (uio_resid(uio) > 0) {
    uint32_t at = (uint32_t)uio_offset(uio);
    ...
    got = hlog_read_locked(chunk, want, at);
    if (got <= 0) break;
    error = uiomove(chunk, got, uio);   // ← 这个会不会有问题？
}
```

`uiomove` 在字符设备里是标准用法，但 `hakios.audio.c` 里是怎么做的？
我没对照过。

---

## 五、我建议的验证顺序

**第一步：确认 `msgbufp` 和 `printf` 的关系**

在 `hakios_hlog_start()` 里直接 `printf` 一行，然后在 `hlog_read` 里读——
如果那行出现在读出的内容里，说明 `msgbufp` 是对的。

**但这正是 902f 试过的**（我在 `hlog_read` 开头加了 `printf("hlog: magic %#x ...")`），
结果读出来 0 字节，诊断也没出现。

**所以第二个 printf 没能证明任何事——因为读路径根本没走到。**

**第二步：把加锁全部去掉试一次**

纯裸读，不加任何锁：

```c
static int hlog_read(...) {
    // 直接读 msgbufp->msg_bufc[...]，不锁，不缓存
}
```

如果这样能读出内容，说明问题在锁。

**第三步：如果裸读也不行，说明 `msgbufp` 指向的不是 printf 实际写的缓冲**

那就需要知道 `debug_buf_base` / `debug_buf_ptr` 是什么，以及 printf 到底写到哪。

---

## 六、一个更简单的替代方案（也许该考虑）

`/dev` 里**本来就有 `/dev/klog` 和 `/dev/oslog`**，是 XNU 原生的：

```
crw-------  1        0 /dev/klog
crw-------  1        0 /dev/oslog
crw-------  1        0 /dev/oslog_stream
```

我试过 `CAT /dev/klog`，返回 0 字节（`hakid` 的 `CAT` 用 `O_NONBLOCK` 打开，
可能是 EAGAIN 被当成结束）。

**如果 `/dev/klog` 能用，`/dev/hlog` 根本不需要写。** 它本来就是
`syslog(2)` 读消息缓冲的设备节点，语义就是"读内核日志"。

**需要你判断**：是不是应该先查 `/dev/klog` 为什么读不出，
而不是继续自己写一个？

可能的原因：
- `klog` 需要先 `open(O_RDONLY)` 然后 `read` 会阻塞等待新内容（这是个流，不是快照）
- 我的 `CAT` 用了 `O_NONBLOCK`，EAGAIN 直接当成 EOF

**如果是这个原因，`klog` 其实是对的，只是我的 `CAP` 用错了打开方式。**

---

## 七、环境

- 当前镜像：`boot-902f.img`
- 回退：`boot-902c.img`（`/dev/hlog` 节点正常但数据是零）、`boot-804i.img`、`boot-796.img`
- `/dev/hlog` 的实现折在 `hakios.mt6895_usb.c` 里（隔离构建树不接受新文件，
  `bsd/conf/files` 和 `config/MASTER.hakios` 的改动都不生效）
- 归档：`docs/headset-wip/raw/ufs-usb-902-hlog/`（含 diff、`hakid.c`、`haki_cli.py`）

**给我下一步：是先查 `/dev/klog`，还是继续改 `/dev/hlog`？**
