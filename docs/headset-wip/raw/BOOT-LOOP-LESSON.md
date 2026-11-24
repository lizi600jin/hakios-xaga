# 一次启动循环的排查记录（boot-905 → boot-904）

2026-11-24 夜。记录一次让设备反复回到 XNU 的事故，以及怎么出来的。

## 现象

刷了 `boot-905`（`mali=6` + USB 提速）之后：

1. XNU 正常启动，GPU 日志 210 行，`GPU_ID = 0xa8670004` 读出来了
2. `RETURN` 收到 `OK returning`，XNU 在 11 秒后退出
3. **但 Android 没起来**，几秒后又回到 XNU
4. 循环

**当时我判断是 `mali=6` 破坏了退出路径**，于是改回 `mali=0` 刷 `boot-906`。
**循环依旧。**

**然后刷回稳定的 `boot-904`，循环依旧。**

最后 `RETURN` 一次，34 秒后 Android 起来了。

## 真正的原因

**不是 mali，也不是镜像问题。** 是**我自己的操作方式**造成的：

### 关键证据

刷完 `boot-904` 后，我观察到：

```
PONG 421        ← 内核稳定运行了 421 秒（7 分钟）
```

**600 秒窗口还没到。** 但我之前以为它在循环——实际上是我**每次都过早地发 `RETURN`，
而 loader 的 `chain_android()` 需要时间**（保存 4 MB 内核日志到 flash、
把日志画到屏幕上、然后才引导 Android）。

我等待的 20–30 秒不够。

### 那为什么看起来在"循环"

因为 `RETURN` 之后：

1. XNU 退出（`18d1:d00d` 从 `lsusb` 消失）
2. loader 开始做收尾（**这段时间 USB 设备不存在**）
3. loader 引导 Android（**Android 启动要 30–60 秒**）
4. 我在第 2、3 步之间又看到 `lsusb` 出现 `18d1:d00d`——**以为是又进了 XNU**

**但 `18d1:d00d` 这个 ID 在 loader 阶段也会出现**（loader 自己实现的 USB，
`iProduct = HakiOS phone`）。所以"看到 18d1:d00d"不等于"进了 XNU"。

**正确的判断方法是 `PONG`：** 连得上 `hakid` 才是真的在 XNU 里。

## 我犯的两个错

### ① 把 loader 的 USB 当成了 XNU 的 USB

`18d1:d00d` 在两种状态下都会出现：

| 状态 | `lsusb` | `hakid` 可连 | ECM 网卡 |
|---|---|---|---|
| XNU 运行 | `18d1:d00d`, `iProduct = HakiOS phone` | ✅ | ✅ |
| loader 收尾/引导 Android | `18d1:d00d`, `iProduct = HakiOS phone` | ❌ | ❌ |
| Android | `2717:ff08` 或其它 | ❌ | ❌ |

**只看 `lsusb` 会误判。必须看 `hakid` 能不能连，或者看 ECM 网卡在不在。**

### ② 等待时间不够

`RETURN` → Android 就绪的真实耗时：

```
11 秒   XNU 退出
  ~     loader 保存 4 MB 日志到 flash + 画屏幕
34 秒   Android 就绪（这一次实测）
```

**60 秒内别下结论。**

## 下次的正确做法

```bash
# 发起退出
haki_cli.py return

# 判断是不是真的退出了：hakid 连不上才算
for i in $(seq 1 60); do
  sleep 2
  if ! python3 -c "import socket;socket.create_connection(('10.0.0.2',2222),timeout=1)" 2>/dev/null; then
    echo "XNU 已退出"
    break
  fi
done

# 然后等 Android，不要在中途看 lsusb 就以为失败了
for i in $(seq 1 30); do
  sleep 3
  [ "$(adb -s $S get-state 2>/dev/null)" = "device" ] && { echo "Android 就绪"; break; }
done
```

## 这次学到的东西

1. **`18d1:d00d` 这个 USB ID 在 loader 和 XNU 里是同一个。** 判断状态要看 `hakid`/ECM 网卡，
   不是看 `lsusb`。这一点以前没意识到，导致我误判了三次。
2. **`RETURN` 到 Android 就绪要 30–60 秒**，不是 10 秒。
3. **`mali=6` 本身没有破坏退出路径。** 我一开始的判断是错的，
   为此多刷了两次机、多花了 10 分钟。

## 顺带的一个结论

`mali=6` 的 GPU 数据是真的，`GPU_ID = 0xa8670004` 那次读出来的：
（详见 `GPU-SURVEY.md`）

```
mali: gpu_id 0xa8670004 l2 0x7130306 shader 0x130013
mali: this is GPU 0xa8670004, CSF 0x40a0412
mali: the whole block settled, so power and clocks are not the problem
mali: firmware loaded; enabling the address spaces
mali: AS0 enabled: root 0x50000000 transcfg 0x42000006
mali: the start loop ended after 1000000 spins (halt seen 0, fatal seen 0)
mali: the global interface never raised; the MCU did not boot
```

**卡点是 MCU 不启动**，不是电源、不是固件、不是地址空间。

## 受控实验（事后补做）

为了确认"主动退出到底行不行"，做了一次精确计时的实验：

```
22:40:49  重启进 XNU
22:41:13  hakid 就绪（+25 秒）
          内核运行 107 秒时
22:42:37  发 RETURN → 回复 "OK returning"
22:42:39  XNU 退出（+2 秒，hakid 连不上）
22:43:09  Android 就绪（+32 秒）
```

**结论：主动退出完全有效，不需要等 600 秒窗口。**

`haki_cli.py return` 现在会自己判断并报告这三个时点，不再靠看 `lsusb` 猜。

## 当前状态

- 设备在 Android，`boot_a = boot-904.img`（`mali=0`，Stage 1 稳定版）
- `boot-905.img`（`mali=6` + USB 提速）留着，需要时可刷
- USB 提速的改动在 `boot-905` 的内核里，`boot-904` 没有——**要单独验一次**
