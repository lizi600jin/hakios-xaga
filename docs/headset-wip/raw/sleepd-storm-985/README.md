# 每秒一万次的 XPC 查询：一个文件权限把 UI 拖垮

2026-11-27，`boot-985b` 成为新基线。

## 一、症状

操作者反馈「特别卡」：滑动 18 fps、打开 App 2-3 秒、输入延迟。

一路查下来排除了：

| 假设 | 实测 | 结论 |
|---|---|---|
| CPU 主频太低 | 999/699 MHz → 提到 1424/2836 | **无效**，帧率一点没变 |
| 像素太多 | 1080x2460 → 828x1792（少 1.85 倍） | **无效** |
| GPU 没用上 | `0 us waiting for the GPU` 每窗口都是 | GPU 从未被调用 |
| 我们的 framebuffer 驱动慢 | 每帧 1.28 ms | 不是瓶颈 |
| `backboardd` 合成慢 | 只占 3.3% CPU | 不是瓶颈 |

## 二、找真凶：`ustacks=top`

`osfmk/arm64/user_exception_trace.c` 自带采样剖析器，boot-arg 就能开：

```
ustacks=top ustacks_at=60 ustacks_ms=5000
```

输出：

```
top: in the last 5001 ms the processor was busy 176% of the time
top:  117.8%  SpringBoard[6]
top:   43.4%  launchd.ios[1]
top:    8.2%  kernel_task[0]
top:    3.3%  backboardd[10]
```

**连续 6 次采样，数值几乎不动** —— 这不是正常负载，是死循环。

### `ustacks=SpringBoard` 抓栈

```
slide = 0x150a4000
thread 0xd6 (running): 0x1d68f6edc 0x1d68fb2ac 0x19bef56c8 ...
```

符号化后：

```
_SBSystemAppMain -> _UIApplicationMain -> _GSEventRunModal
-> _CFRunLoopRunSpecific -> __CFRunLoopRun
-> _mach_port_extract_member -> __kernelrpc_mach_port_extract_member_trap
```

**主线程在 `CFRunLoop` 里疯狂做 `mach_port_extract_member`。**

## 三、找到服务名：loader_log

`bsd/dev/arm/km.c:379`:

```c
/* What user space writes to /dev/console also goes to the loader's log. */
loader_log_putc((char)*cp);
```

**`launchd` 的警告全在 `xnusys` 的 logbook 里，`RETURN` 后能拉出来。**

```
mtk/tools/xaga-pull-log.sh <serial>
```

### 计入 `Last log repeated` 倍率后的真实次数

logbook 里有 1008 条 `Last log repeated N times`，需要把倍率归到前一条日志上：

```python
counts = Counter(); last = None
for ln in lines:
    m = re.search(r'Last log repeated (\d+) times', ln)
    if m:
        n = int(m.group(1))
        if last: counts[last] += n
        continue
    m2 = re.search(r'failed lookup: name = ([A-Za-z0-9_.-]+)', ln)
    if m2:
        last = m2.group(1); counts[last] += 1
```

结果：

```
  1,009,200  com.apple.sleepd.sleepserver    ← 99.9%
        659  com.apple.commcenter.xpc
        189  com.apple.mobileactivationd
```

**每秒一万次。** 每次 XPC 往返冻结主线程约 5-10 ms，正好是那 96.7 ms 的来源。

## 四、修法：加一个按需启动的 plist

`/System/Library/PrivateFrameworks/SleepDaemon.framework/sleepd`（78,144 B）一直在系统卷里，
只是 `userland/root/LaunchDaemons/` 里没有它的 plist。

加一个按需启动的（`mkjobs.py --rest` 的规则：去掉 `RunAtLoad` / `KeepAlive` / `LaunchEvents`）：

```json
{
 "EnablePressuredExit": true,
 "EnableTransactions": true,
 "EnvironmentVariables": {"OS_ACTIVITY_ENABLE_DYNAMIC": "1"},
 "Label": "com.apple.sleepd",
 "MachServices": {
  "com.apple.private.alloy.sleep.classd-idswake": true,
  "com.apple.sleepd.sleepserver": true
 },
 "POSIXSpawnType": "Interactive",
 "ProgramArguments": ["/System/Library/PrivateFrameworks/SleepDaemon.framework/sleepd"],
 "StandardErrorPath": "/dev/console",
 "UserName": "mobile"
}
```

## 五、踩的坑：`umask 0002` 让 plist 变成 0664

第一版（`boot-985`）没生效。原因是 `cp` 时继承了 `umask 0002`，文件成了 `0664`。

用 `apfs.py stat` 读镜像，拿到铁证：

```
boot-985 镜像:
  /LaunchDaemons/com.apple.sleepd.plist → mode 100664 (rw-rw-r--)   ← launchd 静默拒载
  /LaunchDaemons/com.apple.lsd.plist    → mode 100644 (rw-r--r--)   ← 正常

boot-985b 镜像:
  /LaunchDaemons/com.apple.sleepd.plist → mode 100644 (rw-r--r--)   ← 修好
```

**`launchd` 对任何 `g+w` / `o+w` 的 plist 直接静默拒载，不报错。**
所以 `MachServices` 表里没有这个名字，查询返回 `error = 3: No such process`。

`userland/build.sh:554-555` 早就写了这条规则：

> The modes launchd and the kernel care about, set outright: directories 0755, files 0644,
> programs 0755, nothing writable by group or other. mkapfs.py takes the modes from this tree.

### 防线

`tools/apfs/mkapfs.py` 已加检查：打包 `LaunchDaemons / LaunchAgents / LaunchAngels /
NanoLaunchDaemons` 下的 `*.plist` 时，检测到 `mode & 0o022` 就警告并剥离写位。

## 六、结果

| 项 | boot-984g | boot-985b | 变化 |
|---|---|---|---|
| **SpringBoard CPU** | 117.8% | **14.2%** | **-88%** |
| **launchd.ios CPU** | 43.4% | **2.7%** | **-94%** |
| **总忙碌** | 177% | **58%** | **-67%** |
| **backboardd** | 3.3% | 7.9% | 在真干活了 |
| **failed lookup 总数** | 1,010,283 | **953** | **-99.9%** |
| **sleepd 查询** | 1,009,200 | **0** | 归零 |
| **sleepd 进程** | 未启动 | PID 26，0:00.09 | ✅ |
| **RETURN** | 32s | 32s | 稳定 |

### 剩下的 953 次

```
523  com.apple.commcenter.xpc          ← 需要真实基带，不会消失（5 次/秒，可忽略）
189  com.apple.mobileactivationd       ← 需要激活
 31  com.apple.coremedia.routingcontext.xpc
```

## 七、教训

1. **`ustacks=top` 是零成本的第一步** —— 一眼看出「在忙」还是「在等」，比读源码快得多。
2. **`grep -c` 会漏掉重复计数** —— logbook 的 `Last log repeated N times` 让真实次数大了 1500 倍。
3. **文件权限能静默杀死一个服务** —— `launchd` 拒载 `0664` 的 plist 一句话都不说。
4. **`apfs.py stat` 能直接看镜像里的 mode** —— 不用刷机就能验证权限是否对。
5. **提频和降分辨率都无效时，说明瓶颈不在算力** —— 那时候就该往「在等什么」的方向查。

## 八、镜像

```
golden/boot-985b-golden.img
  sha256 ae41a7eed9352cc3b5f42f5481bc3d66405c0fb139ebaddeea76f9ee64ca9671
  md5    451023290103666816d9409bd30d500c
  23109632 B

boot-args:
  cpus=8 fbwidth=1080 fbheight=2460 mali=6 mali_trial=740
  ufs=2 var_hfs=xnuvar usb=2 tfa_probe=1 spk_init=1
  cpufreq_probe=1 cpufreq_mhz=2850
  ustacks=top ustacks_at=60 ustacks_ms=5000
  （无 -v）
```
