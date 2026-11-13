# 对 web 端回复的核对

> 对象：HiFi 侧 web 模型就 `XAGA_JACK_FOR_IOS.md` 与本项目材料给出的建议
> 日期：2026-11-14
> 方式：逐条对照真机探测结果与厂商 `.ko` 的动态符号依赖

---

## 一、它说对的部分（3 条）

| 主张 | 核对结果 |
|---|---|
| HiFi 侧的 3.5mm 适配只在软件层读 `AUDIO_DEVICE_OUT_WIRED_HEADPHONE`，不参与检测 | ✅ 与 `probe192.sh` 的实际行为一致，对 iOS 确实无直接价值 |
| 之前说"没有 tinymix"是旧记录，现在有了 | ✅ 已实测 `/system/bin/tinymix` 存在，51672 B |
| 当前 `Headphone Plugged In` 是 `Off`，另有 `Audio HP ImpeDance Setting` 控件 | ✅ 与真机一致 |
| 没有耳机就拿不到插拔对比的寄存器值，不能编造 | ✅ 态度正确；本项目也是这么处理的（全部标「未验证」） |

---

## 二、它说错的部分（1 条，但是根本性的）

### 2.1 「让 iOS 监听 mt6895-mt6368 Headset Jack 这个 input 节点，就能拿到插入状态」

**这条在本项目里做不到，有两条独立的阻断。**

#### 阻断一：HakiOS 没有 Linux input 子系统

HakiOS 是 XNU 移植，不是 Linux。查证：

```
grep -rn "dev/input" /home/lizi/IOS/hakios-mtk/drivers /home/lizi/IOS/hakios-mtk/mtk --include="*.c"
→ 无任何结果
```

XNU 侧的输入通道是自定义的 `/dev/hakios-touch`（`xnu/files/bsd/sys/hakios.input.h:18`），
记录格式是 `struct hakios_touch`，不是 `struct input_event`。
`/dev/input/eventN`、`EV_SW`、`SW_HEADPHONE_INSERT` 这套是 Linux 的，HakiOS 里不存在。

→ 「监听 input 节点」需要一个 Linux input 子系统。HakiOS 没有，得先写一个。

#### 阻断二：那个 input 节点本身不是 accdet 直接创建的

这是更关键的一点。查 `mt6368-accdet.ko` 的动态符号依赖：

```
aarch64-linux-gnu-objdump -r mt6368-accdet.ko | grep -oE "snd_[a-z_]+|input_[a-z_]+" | sort -u
→ snd_jack_set_key
  snd_soc_card_jack_new
  snd_soc_component_set_jack
  snd_soc_jack_report
```

**没有任何 `input_*` 符号，`input_register_device` 命中 0 次。**

那个 `/dev/input/event4`（`mt6895-mt6368 Headset Jack`）是 **ALSA 的 `snd_jack` 框架**
在内部创建的。调用链是：

```
accdet 驱动
  └─ snd_soc_jack_report()        ← 报告插入状态
       └─ snd_jack 框架
            └─ input_register_device()  ← 这里才创建 /dev/input/eventN
```

**直接证据**：`/usr/src/linux-headers-7.0.0-42/include/sound/jack.h:61-72`

```c
61  struct snd_jack {
62      struct list_head kctl_list;
63      struct snd_card *card;
64      const char *id;
65  #ifdef CONFIG_SND_JACK_INPUT_DEV
66      struct input_dev *input_dev;      /* ← snd_jack 就是一个 input_dev 的包装 */
67      struct mutex input_dev_lock;
68      int registered;
69      int type;
70      char name[100];
...
72  #endif /* CONFIG_SND_JACK_INPUT_DEV */
```

即 `snd_jack` 本身**没有自己的设备节点**，它只是给 `input_dev` 套了一层。
那个 `/dev/input/event4` 是 `input_dev` 的节点，`snd_jack` 负责往里写 `EV_SW` 事件。

也就是说：**要拿到那个 input 节点，得先把整套 ASoC（`snd_soc_card_jack_new` /
`snd_soc_jack_report` / `snd_jack`）和 Linux input 核心（`input_register_device`）
一起搬过来。** 而 HakiOS 一行都没有。

→ 「监听 input 节点」不是绕开寄存器工作的捷径，它依赖的东西比直接读寄存器多得多。

#### 正确的最小路径

直接读 `0x26a9` 的 bit0，一条 SPMI 读事务即可。本项目已解出并验证到寄存器级：

```
reg = spmi_read8(sid=5, 0x26a9);
plugged = reg & 0x01;
```

`0x26a9` 未插时实测 = `0x00`（真机基线）；bit0=1 表示插入（从 `accdet_irq_handle` 反汇编解出）。
这需要的是 **SPMI 读**——这个项目的按键驱动已经跑通了。

### 2.2 「iOS 侧不需要自己去配置 ACCDET 寄存器」

**部分错。** 分两种情况：

| 场景 | 是否需要配置 ACCDET |
|---|---|
| MT6368 已被 LK/前次 Android 初始化过，且状态保持 | 可能不需要，读数即可 |
| XNU 冷启动，MT6368 处于复位态 | **必须**初始化，否则 `0x26a9` 不反映真实插拔 |

而且初始化序列本项目已经**全部解出并真机对拍通过（19/19）**，见 `ACCDET-DECODE.md`。
12 个 `accdet_init_once` 的写 + 11 个 `accdet_init` 的写，可以直接照抄，不存在"不需要配"的问题，
只存在"要不要配"的运行时判断。

### 2.3 「ACCDET 寄存器只在需要主动控制时才用（强制重检、改阻抗阈值）」

**错。** 寄存器不是可选的增强项，它是**唯一的真实数据源**：

- `0x26a9` bit0 —— 插入状态本身
- `0x26b6` / `0x26b8` —— ADC 读数，三态（耳机/耳麦/line-out）的判据
- `0x26a3` / `0x26a4` —— 阈值与 mic-bias

input 节点（如果存在）只是这些寄存器值的**下游派生**。绕开寄存器去读派生值，
在 HakiOS 里既没有那个派生层，也没有意义。

---

## 三、它建议的"下一步"的问题

它建议有耳机时跑：

```sh
adb shell su -c 'getevent -pl'
adb shell su -c 'tinymix "Headphone Plugged In"'
adb shell su -c 'cat /proc/asound/card0/pcm3p/sub0/status'
```

这三条**已经在 stock Android 上跑过了**（见 `DEVICE-PROBE-STOCK.md`），
其中第 1 条的输出已经完整记录（`getevent -pl` 的 event3/event4 全量能力位）。
它们只能证明 Android 侧的行为，对 XNU 侧没有增量信息。

**真正缺的是 XNU 侧的读数**：HakiOS 自己通过 SPMI 读 `0x2688`–`0x26d3`，
与 Android 基线（`accdet-baseline-dump.txt`，130 个寄存器）逐字节比对。
这一步不需要耳机，工具已经写好并自检通过（`accdet-compare.py`，9 个用例）。

---

## 四、结论

| 它的建议 | 采纳 |
|---|---|
| 复用 HiFi 的软件层适配 | ❌ 它自己也说无价值，同意 |
| 监听 input 节点拿插入状态 | ❌ **错误**。HakiOS 无 Linux input，且该节点由 ASoC `snd_jack` 创建，依赖比读寄存器多得多 |
| 不需要配置 ACCDET 寄存器 | ⚠️ 分情况。冷启动必须初始化；序列已解出并 19/19 对拍 |
| 寄存器只在主动控制时用 | ❌ **错误**。寄存器是唯一真实数据源 |
| 无耳机时无法推进 | ❌ **错误**。寄存器值已全部解出并对拍，只读验证不需要耳机 |
| 有耳机时的探测命令 | ⚠️ 已跑过，对 XNU 无增量 |

**它最大的贡献**是诚实地说明了"HiFi 侧软件适配对 iOS 无价值"，
并且没有在缺数据时编造——这两点是对的。

**它最大的问题**是把 Android 的框架层（Linux input + ASoC jack）当成了跨平台可复用的东西。
HakiOS 是裸机 XNU，那一层完全不存在；反过来，本项目已经把那一层**底下**的寄存器级事实全部拿到了。
