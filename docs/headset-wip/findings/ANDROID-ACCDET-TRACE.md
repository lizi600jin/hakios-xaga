# Android 侧抓 MT6368 ACCDET 寄存器访问序列 — 方案设计与实测边界

> 目标：在**原厂 Android** 上抓到 MT6368（SPMI bus 0 / USID 5）accdet 模块的真实寄存器访问序列，
> 作为裸机 XNU 侧复现的 ground truth。
>
> 交付物：
> - 本文件（方案 + 命令 + 解析方法）
> - `android-accdet-trace.sh`（`sh -n` / `dash -n` 通过，`selftest` 11/11）
> - `accdet-trace-parse.py`（`--selftest` 24/24，另有 130 寄存器真实往返验证）
>
> 日期：2026-11-15。所有「未验证」都显式标注。

---

## 0. 结论先行（六条）

1. **hook 目标是 `regmap_read` / `regmap_write` / `regmap_update_bits_base`（vmlinux 里的 regmap 核心），
   不是驱动自己的 `accdet_read` / `accdet_write`。** 后者在 `.ko` 里**有符号**（`T`，地址 0x1968/0x1a60/...），
   但**驱动内部零处调用它们** —— 全部被编译器内联展开。实测 `bl` 指令指向它们 **0 次**，
   `R_AARCH64_CALL26` 重定位指向它们 **0 条**。挂在它们上面的 kprobe **永远不会触发**。
   → 这是本报告最重要的一条，详见 §2.3。
2. **最好的抓取手段不是 kprobe，是内核自带的 regmap tracepoint。**
   `drivers/base/regmap/trace.h` 定义了 `regmap_reg_read` / `regmap_reg_write`，
   带 `name`（`regmap_name(map)`）+ `reg` + `val` —— **真实值**，零指针解引用、零偏移猜测。
   源码位置 `regmap.c:2733`（read）/ `:1969`（write）。
3. **SID 过滤键是字符串 `0-05`。** SPMI 设备名的构造是
   `dev_set_name(&sdev->dev, "%d-%02x", ctrl->nr, sdev->usid)`（`drivers/spmi/spmi.c:65`），
   `regmap_name()` 返回 `dev_name(map->dev)`。MT6363 是 `0-04`。真机路径证据
   `.../spmi-0/0-05/mt6368-accdet`（`DEVICE-PROBE-STOCK.md:572`）与之一致。
4. **`regmap_update_bits` 不能作为 kprobe 目标** —— 它是 `include/linux/regmap.h:1075` 的
   `static inline`，没有 out-of-line 符号。要挂 `regmap_update_bits_base`（`EXPORT_SYMBOL_GPL`）。
5. **无耳机也能抓到东西，但抓不到「插入检测」。** probe 里挂了 10 秒定时器
   （`accdet_init_timer`，`ACCDET_INIT_WAIT_TIMER = 10*HZ`，`:206` / `:3496`），
   到点跑 `accdet_init()` → `accdet_init_debounce()` → `accdet_init_once()`
   （`delay_init_timerhandler` `:3219-3227` 排队，`delay_init_work_callback` `:3211-3217` 执行）。
   这一段**不需要耳机**，是真实的初始化写序列。插入/按键/拔出的序列**必须插耳机**。
6. **真机环境关键约束（本次未能实测，改用已有证据）**：见 §1，**已标注哪些是实测、哪些待补**。

---

## 1. 真机环境侦察

### 1.1 本次侦察被阻塞（如实记录）

任务下发时的前提是「原厂 Android，`sys.boot_completed=1`，adb 可用」。**实际不是。**

```
$ adb devices -l          # 唯一没带 -s 的一条：它就是要列出全部设备
List of devices attached
                      ← 空

$ lsusb | grep -iE "18d1|xiaomi"
Bus 003 Device 042: ID 18d1:d00d Google Inc. Xiaomi Mi/Redmi 2 (fastboot)

$ fastboot devices
<serial>	 fastboot

$ fastboot getvar product
product: xaga
```

设备停在 **fastboot**，不在 Android。原因有记录（`findings/lk-dump-20261114-2322/README.md`）：
前一轮 `fastboot oem expdb` 把 LK 的 fastboot 会话搞死，设备自愈重启后停在 fastboot（n=2）。

**中途一条有用的观测**：`fastboot getvar product` 从挂死（`< waiting for any device>`，rc=124）
变为 `product: xaga`，同时 `lsusb` 设备号 **041 → 042**（USB 重新枚举）。
→ **结论是「fastboot 协议层活了、可以刷机」，不是「可以 reboot」。**
本轮设备持有者是 Codex（正在刷 `headset=2` 镜像），按协作协议 §4 我**停手不碰设备**。

因此 §1 的其余内容**全部来自仓内已有的真机探测记录**（`DEVICE-PROBE-STOCK.md`，2026-11-14），
不是本次采集。**这些项目前仍是「历史实测」，本轮未复验** —— 报告里逐条标了。

### 1.2 环境事实（来自 `DEVICE-PROBE-STOCK.md`，本轮未复验）

| 项 | 值 | 出处 |
|---|---|---|
| 内核 | `5.10.237-Fuutao-Qn_miao-gc5e20589c`，aarch64，PREEMPT，clang 22.0.0 | `:52-59` |
| root | **有**。`adb shell` **落地即 uid=0**，SELinux context `u:r:ksu:s0`（KernelSU），`/system/bin/su` 存在 | `:63-84` |
| `getenforce` | Enforcing | `:63-84` |
| tracefs | **已挂载**，在 **`/sys/kernel/tracing`**（`type tracefs (rw,seclabel,relatime,gid=3012)`） | `:766-769` |
| debugfs | **未挂载**（`/sys/kernel/debug` 空，`/d` 不存在） | `:1462-1472` |
| `readtracefs` 组 | gid 3012 在 adb shell 的 groups 里 → 读 tracefs 不需额外授权 | `:68` |
| accdet 驱动 | **模块** `mt6368_accdet`，`coresize=61440`，`initsize=0` → 是 `.ko` 加载 | `:524-549` |
| 驱动名 | `pmic-codec-accdet`（`platform_driver.driver.name`，`mt6368-accdet.c:3585`） | 源码 |
| SPMI 路径 | `1c804000.spmi/spmi-0/0-05/mt6368-accdet` | `:572` |
| `state` / `dump_reg` 权限 | **0444 可读** | `:570-580` |
| `set_reg` / `set_headset_mode` / `start_debug` | 0200 只写（**本方案不碰**） | `:578-580` |
| 当前 `state` | `0`（无耳机） | `:596-600` |
| `ACCDET_IRQ` (97) / `ACCDET_EINT0` (98) 计数 | **都是 0** → 自启动以来从未触发过插拔 | `:849-862` |

### 1.3 本次**未能**确认的项（重要，标「未验证」）

以下都是 kprobe 方案的前提，本轮**没有实测**：

| 项 | 状态 | 影响 |
|---|---|---|
| `CONFIG_KPROBE_EVENTS` 是否开 | **未验证**。vendor defconfig 里搜不到（`xaga.config` / `xiaomi_mt6895.config` 都是空的 fragment），`gki_defconfig` 只有 `CONFIG_KPROBES=y`（不含 `_EVENTS`） | 决定方案 A 是否可用 |
| `/sys/kernel/tracing/kprobe_events` 是否存在 | **未验证** | 同上 |
| `/sys/kernel/tracing/events/regmap/` 是否存在 | **未验证** | 决定首选方案是否可用 |
| `available_tracers`（是否含 `function_graph`） | **未验证** | 决定方案 B 是否可用 |
| `kptr_restrict` / `perf_event_paranoid` 值 | **未验证** | 影响 kallsyms 可读性（但我们是 root，`kptr_restrict=1/2` 对 root 无效） |
| `kallsyms` 里 accdet 符号是否可见 | **未验证**（`mt6368_accdet` 是模块，加载后才有符号） | 影响能否按名挂 kprobe |

> **一条可用的间接证据**：`findings/GPUEB-IPI-PROTOCOL.md:1045-1160` 里写过一套 kprobe 方案，
> 说明团队此前认为本机 kprobe 可用，但**那也是一份未执行的方案**，不是实测。

**`probe` 子命令就是为补这一节设计的** —— 设备回到 Android 后跑一次即可全部填上：

```sh
HAKIOS_SERIAL=<serial> ./android-accdet-trace.sh probe
```

---

## 2. 要 hook 的函数

### 2.1 驱动内 regmap 调用点（源码 grep，共 6 处直接调用）

`raw/mt6368-accdet.c` 里 `regmap_*` 的直接调用**只有 6 处**，全部集中在 390-436 行的包装函数里：

| 行号 | 调用 | 所在函数 |
|---|---|---|
| 394 | `regmap_read(accdet->regmap, addr, &val)` | `accdet_read` |
| 408 | `regmap_write(accdet->regmap, addr, wdata)` | `accdet_write` |
| 413 | `regmap_update_bits(accdet->regmap, addr, mask<<shift, data<<shift)` | `accdet_update_bits` |
| 421 | `regmap_update_bits(accdet->regmap, addr, BIT(mask), BIT(shift))` | `accdet_update_bit` |
| 428 | `regmap_update_bits(accdet->regmap, addr, mask<<shift, 0)` | `accdet_clear_bits` |
| 435 | `regmap_update_bits(accdet->regmap, addr, BIT(mask), 0)` | `accdet_clear_bit` |

`regmap_bulk_read` / `regmap_bulk_write` / `regmap_multi_reg_*` / `regmap_raw_*`：**驱动里 0 处**（grep 无命中）。

### 2.2 包装函数及其调用量（源码层）

| 包装函数 | 定义行 | 源码调用点数 | 语义 |
|---|---|---|---|
| `accdet_read(addr)` | 390 | 72 | 读单字节，返回 val |
| `accdet_read_bits(addr, shift, mask)` | 398 | 4 | 读 + 取位 |
| `accdet_write(addr, wdata)` | 406 | 47 | 写单字节 |
| `accdet_update_bits(addr, shift, mask, data)` | 411 | 27 | RMW |
| `accdet_update_bit(addr, shift)` | 417 | 75 | 置位（RMW） |
| `accdet_clear_bits(addr, shift, mask, data)` | 425 | 3 | 清位（RMW） |
| `accdet_clear_bit(addr, shift)` | 431 | 60 | 清位（RMW） |

调用点最多的宿主函数（源码层，可用于 ftrace `set_graph_function`）：

| 函数 | 行 | 寄存器操作数 |
|---|---|---|
| `config_digital_init_by_mode()` | 2726 | 12 写 + 12 update_bit + 16 update_bit/clear_bit |
| `accdet_init_once()` | 2984 | 19 写 + 4 读 + 5 update_bit + 2 clear_bits |
| `accdet_set_debounce()` | 1940 | 11 写 + 4 update_bits |
| `adjust_eint_digital_setting()` | 1358 | 14 clear_bit + 2 update_bit |
| `config_eint_init_by_mode()` | 2901 | 10 update_bit + 4 clear_bit |
| `dump_register()` / `cat_register()` / `mini_dump_register()` | 487 / 555 / 438 | 纯读（dump 路径，会污染 trace） |
| `accdet_irq_handle()` | 2379 | 3 读 + 1 read_bits（**中断路径**） |
| `check_cable_type()` | 2001 | 1 读（`cur_AB` 判定） |

### 2.3 ★ 关键：包装函数**不可 hook**（二进制实测）

这一节是本次的核心否证，两条独立证据。

**证据 1：符号存在，但无人调用。**

```
$ nm ko/mt6368-accdet.ko | grep -E "accdet_(read|write|update|clear)"
0000000000001b84 T accdet_clear_bit
0000000000001b38 T accdet_clear_bits
0000000000001968 T accdet_read
00000000000019d8 T accdet_read_bits
0000000000001ae8 T accdet_update_bit
0000000000001a9c T accdet_update_bits
0000000000001a60 T accdet_write
```

都是 `T`（全局 text 符号）。但**统计 `bl` 指令的目标**：

```
$ for sym in accdet_read accdet_write accdet_update_bits accdet_update_bit accdet_clear_bit accdet_clear_bits accdet_read_bits; do
    n=$(aarch64-linux-gnu-objdump -d --section=.text ko/mt6368-accdet.ko | grep -cE "bl\s+0 <$sym>")
    echo "$sym $n"
  done
accdet_read 0
accdet_write 0
accdet_update_bits 0
accdet_update_bit 0
accdet_clear_bit 0
accdet_clear_bits 0
accdet_read_bits 0
```

**全部 0 次。** 而同期：

```
regmap_read                86
regmap_write               60
regmap_update_bits_base   131
```

**证据 2：重定位表（独立于 objdump）。**

```
$ readelf -rW ko/mt6368-accdet.ko | grep "R_AARCH64_CALL26" | grep -cE "accdet_(read|write|update_bits|update_bit|clear_bit|clear_bits)\b"
0
# 注：grep -c 计数为 0 时退出码是 1，别把 rc=1 当成命令出错
```

再把重定位按符号名归类，与 objdump 的 `bl` 统计逐项对照（两套工具、同一结论）：

```
  131  regmap_update_bits_base
   86  regmap_read
   60  regmap_write
   ...
  accdet_read            CALL26=0
  accdet_write           CALL26=0
  accdet_update_bits     CALL26=0
  accdet_update_bit      CALL26=0
  accdet_clear_bit       CALL26=0
  accdet_clear_bits      CALL26=0
  accdet_read_bits       CALL26=0
```

**131 / 86 / 60 与 objdump 的 `bl` 计数完全一致** → 两套独立工具互相印证。

**解释**：`accdet_read` 等是**裸 `inline`**（不是 `static inline`），C99 inline 语义要求编译器
在每个翻译单元提供一个 out-of-line 定义（这就是 `.ko` 里那些 `T` 符号的来源），
但**每次调用都内联展开**，所以符号成了「存在但无引用」的死代码。

**后果**：`p:acc_r accdet_read ...` 会**注册成功**（符号找得到），`enable` 也返回成功，
**但永远不会有事件** —— 这是最坏的一种失败：看起来在工作，实际什么都没抓到。

**对照**：`accdet_read` 函数**体内部**确实有 `bl regmap_read`（`0x19a4`），
说明这个 out-of-line 版本本身是完整可用的，只是没人调它。

**即使内联结论在 5.10.237 上不成立，结论也不变** —— 挂 regmap 核心本来就比挂包装函数更好，
理由与内联无关：

| 维度 | 挂包装函数 | 挂 regmap 核心 |
|---|---|---|
| SID 过滤 | ❌ `accdet->regmap` 是全局变量，kprobe 里取不到（要读 `$arg0` 再解引用，脆弱） | ✅ tracepoint 自带 `name=0-05` |
| `update_bits` 的语义 | ❌ `accdet_update_bits(addr, shift, mask, data)` 只能看到 `addr`，mask 与 data 要另算 | ✅ `mask` / `val` 是独立参数 |
| 覆盖完整性 | ⚠️ 只覆盖本驱动 | ✅ 顺带看到 codec、ADC 等其它模块访问同一 PMIC |

所以 §2.3 的作用是「**说明为什么不能**挂包装函数」，而不是「只有这一个理由」。

> **边界**：`ko/` 里的 .ko vermagic 是 `5.10.226-android12-9-g8e1a18c8f218`，
> 真机内核是 `5.10.237-Fuutao-Qn_miao-gc5e20589c` —— **不是同一个构建**（`DEVICE-PROBE-STOCK.md:1430` 已记录）。
> 但 `raw/mt6368-accdet.c` 与厂商树 `git cat-file` 出来的**逐字节相同**（md5 `fa14eec9092e82842141a9c1548746a9`），
> 且两者都是 clang 编的。**内联决策由源码的 `inline` 关键字 + `-O2` 决定，不依赖 5.10.226 还是 .237。**
> 若要绝对确定，设备上跑一次 `grep -c " accdet_read$" /proc/kallsyms` + 挂一个探针看是否触发即可（§5.3 的 `probe` 已包含）。

### 2.4 ★ 真正的 hook 目标

regmap 核心的三个函数（vmlinux，`EXPORT_SYMBOL_GPL`，builtin）：

| 函数 | 原型 | ARM64 参数位置 | 出处 |
|---|---|---|---|
| `regmap_read` | `(struct regmap *map, unsigned int reg, unsigned int *val)` | `x0=map`, `x1=reg`, **`x2=&val`（指针）** | `drivers/base/regmap/regmap.c:2752` |
| `regmap_write` | `(struct regmap *map, unsigned int reg, unsigned int val)` | `x0=map`, `x1=reg`, **`x2=val`（值）** | 同 `:1984` |
| `regmap_update_bits_base` | `(struct regmap *map, unsigned int reg, unsigned int mask, unsigned int val, bool *change, bool async, bool force)` | `x0=map`, `x1=reg`, `x2=mask`, `x3=val` | 同 `:3107` |

**注意 `regmap_read` 的 `val` 是出参指针** —— 入口 probe 拿到的 `x2` 是地址不是值。
所以：
- **写**：入口 probe `reg=%x1 val=%x2` 即可拿到真实写入值。
- **读**：入口只能拿到地址；**值要用 `kretprobe` + `$arg2` 解引用**，或用**内置 tracepoint**（§2.5）。
  `kretprobe` 能取 `$argN`（`kernel/trace/trace_kprobe.c:778-880` 里 `TPARG_FL_RETURN` + `fetchargs` 路径支持）。

### 2.5 ★★ 最优路径：内核自带 regmap tracepoint

`drivers/base/regmap/trace.h` 里：

```c
DECLARE_EVENT_CLASS(regmap_reg,
    TP_PROTO(struct regmap *map, unsigned int reg, unsigned int val),
    TP_STRUCT__entry(
        __string( name, regmap_name(map) )
        __field( unsigned int, reg )
        __field( unsigned int, val )
    ),
    TP_printk("%s reg=%x val=%x", __get_str(name), __entry->reg, __entry->val)
);
DEFINE_EVENT(regmap_reg, regmap_reg_write, ...);
DEFINE_EVENT(regmap_reg, regmap_reg_read,  ...);
DEFINE_EVENT(regmap_reg, regmap_reg_read_cache, ...);
```

插桩位置（`regmap.c`）：

| tracepoint | 位置 | 触发条件 |
|---|---|---|
| `regmap_reg_write` | `:1969` | `_regmap_write` 里，**在 `map->reg_write` 之前** |
| `regmap_reg_read` | `:2733` | `_regmap_read` 里，`ret == 0` 之后（**带真实读回值**） |
| `regmap_reg_read_cache` | — | 命中软件 cache 时 |

**为什么比 kprobe 好**：
- `val` 直接就是真实值（读也是），**不用解引用指针、不用猜偏移**。
- 自带 `name` = regmap 名 = **SID 过滤键**（§2.6）。
- 不用写 `kprobe_events`，只开关 `events/regmap/*/enable`。
- 内核自带，不受 kprobe 是否编译影响（只要 `CONFIG_TRACEPOINTS`，GKI 必开）。

**唯一风险**：`events/regmap/` 目录是否存在，**未验证**（§1.3）。`probe` 会先查。

**`update_bits` 会完整覆盖**：`regmap_spmi_ext` 的 `regmap_bus` **没有** `.reg_update_bits`
（`regmap-spmi.c:197-202` 只有 read/write/gather_write），且 `spi_regmap_config` 是
`REGCACHE_NONE`（`mtk-spmi-pmic.c:545-551`，没有 `.cache_type`）→
`regmap_volatile()` 在 `cache_ops == NULL` 时返回 `true`（`regmap.c:158-161`）→
`_regmap_update_bits` 走 else 分支：`_regmap_read` + `_regmap_write`
（`regmap.c:3066-3083`）→ **读前像与写后像两个 tracepoint 都会出**。

### 2.6 SID 过滤：为什么是 `0-05`

```c
/* drivers/spmi/spmi.c:65 */
dev_set_name(&sdev->dev, "%d-%02x", ctrl->nr, sdev->usid);

/* drivers/base/regmap/internal.h:277-283 */
static inline const char *regmap_name(const struct regmap *map) {
    if (map->dev) return dev_name(map->dev);
    return map->name;
}
```

`ctrl->nr = 0`（`spmi-0`），`usid = 5` → **`dev_name = "0-05"`** → `regmap_name = "0-05"`。
MT6363 在 USID 4 → `0-04`。

**真机印证**：`/sys/bus/platform/devices/mt6368-accdet` 的路径是
`.../spmi-0/0-05/mt6368-accdet`（`DEVICE-PROBE-STOCK.md:572`）。

另有旁证：该 regmap 由 `drivers/mfd/mtk-spmi-pmic.c:571` 的
`devm_regmap_init_spmi_ext(sdev, &spmi_regmap_config)` 创建，`reg_bits=16 val_bits=8`，
`max_register=0xffff`，`fast_io=true`。accdet 用 `dev_get_regmap(pdev->dev.parent, NULL)`
（`mt6368-accdet.c:3307`）拿到**同一个** map 指针 → 名字必然一致。

### 2.7 PMIF 层（第三备选，仅在 SID 过滤失效时用）

`drivers/spmi/spmi-mtk-pmif-core.c`（**builtin**，`obj-$(CONFIG_SPMI_MTK_PMIF)`）：

| 函数 | 行 | 签名 | SID 位置 |
|---|---|---|---|
| `pmif_spmi_read_cmd` | 376 | `(ctrl, opc, sid, addr, buf, len)` | **`x2=sid`** |
| `pmif_spmi_write_cmd` | 448 | `(ctrl, opc, sid, addr, buf, len)` | **`x2=sid`** |

优点：`sid` 是**数字 5**，过滤最硬；`addr` 也是数字。
缺点：**读的 `buf` 是出参指针**，拿不到值；且这一层看不到「逻辑寄存器」语义之前的样子。
**只在 `0-05` 名字过滤不好用时作为兜底。**

---

## 3. 三方案对比

| 方案 | 手段 | 能拿到什么 | 优点 | 缺点 | 本机可用性 |
|---|---|---|---|---|---|
| **A. kprobe** | `kprobe_events` on `regmap_read` / `regmap_write` / `regmap_update_bits_base` | 地址 + 值（写）；地址（读，值需 kretprobe 解引用） | 精确；可按 `$arg1` 打 reg 条件过滤 | 需 root + `CONFIG_KPROBE_EVENTS`；读值要解指针；**不能挂 `accdet_*` 包装函数**（已证 0 调用） | **未验证**（§1.3） |
| **B. ftrace function_graph** | `set_graph_function` → accdet 函数 | 只有调用图与耗时，**没有寄存器值** | 零配置、零风险 | 拿不到地址和值 → **对本任务价值有限** | **未验证** |
| **C. 直接读 sysfs / debugfs** | 轮询 `pmic-codec-accdet/{state,dump_reg}` | **全量寄存器快照**（130 个，含 accdet + AUDDEC_ANA） | **已实测可读**；零风险；不需 root（0444） | 只有「状态」没有「序列」；驱动 `dump_reg` 本身**会读 130 次寄存器**（污染 trace，且耗 ~ms）；无耳机时值固定 | **已证实可用**（`DEVICE-PROBE-STOCK.md:570-580`） |
| **★ D. regmap tracepoint** | `events/regmap/regmap_reg_{read,write}/enable` | **地址 + 值 + regmap 名（SID）** | 带真实值；自带 SID 过滤；不用定义 probe | 依赖 `events/regmap/` 存在 | **未验证**（首选，`probe` 先查） |

### 3.1 推荐

**优先级：D（regmap tracepoint）> A（kprobe）> C（dump 快照）> B（function_graph）**

理由：
- **D** 同时满足「真实值」+「SID 过滤」+「零偏移猜测」，是唯一**不需要任何反汇编假设**的方案。
  脚本默认走 D，`probe` 检测不到时自动退到 A。
- **A** 是 D 的完整备份。`regmap_read` 的读值需要 `kretprobe` + 指针解引用，
  `$arg2` 在 kretprobe 上的支持取决于内核版本，**D 不可用时才值得折腾**。
- **C** 不提供序列，但**能立刻给出一份 ground truth 快照**（已有 130 寄存器 baseline），
  且**不需要 root**。它是「保底能拿到东西」的方案，也是**唯一已实测可用**的。
- **B** 对「读回正确值」这个目标**帮不上**，只在「确认某函数有没有被调用」时有用。
  但注意 §2.3 —— 用 B 确认 `accdet_read` 是否被调用，会得到「没被调用」的**正确**结论
  （因为真的没被调用），如果拿它当「驱动没工作」的证据就会误判。

### 3.2 方案 C 的具体可用性（已实测，最高置信度）

```sh
# 不需要 root（0444），不需要 tracefs
adb -s "$S" shell 'cat /sys/bus/platform/drivers/pmic-codec-accdet/state'
adb -s "$S" shell 'cat /sys/bus/platform/drivers/pmic-codec-accdet/dump_reg'
```

`dump_reg` 的读取路径（`mt6368-accdet.c:718-731` → `cat_register()` `:555`）会做 **130 次 `accdet_read`**，
即 130 次真实 SPMI 事务。**这本身会产生 register 访问**，
所以**不要在 trace 开启期间 cat `dump_reg`** —— 会把自己读进 trace 里，污染数据。
（脚本的 `start` 里没有这一步，这是刻意的。）

---

## 4. 可执行命令序列

### 4.1 前置

```sh
source ~/.dsh/env.sh
export HAKIOS_SERIAL=<你的序列号>      # adb devices 里那一列
```

> 所有脚本内部都用 `adb -s "$HAKIOS_SERIAL"`，不依赖「只有一台设备」。

### 4.2 无耳机版（现在就能跑）

这一段抓的是**开机 init 段**。驱动在 probe 里挂 10 秒定时器（`mt6368-accdet.c:3496`），
到点由 `delay_init_timerhandler`（`:3219-3227`）排队、`delay_init_work_callback`（`:3211-3217`）执行
`accdet_init()` + `accdet_init_debounce()` + `accdet_init_once()`，这是一段**真实且不需要耳机**的写序列（44 写 + 4 校验）。

但注意：**如果手机已经开机很久，这段早就跑完了。** 三个选择：

**(a) 直接抓「现在发生的事」** —— 10 秒空载通常只有零星访问（甚至 0 条）。
有价值的是：任何 audio HAL 的动作（播一声通知音、切一次通话音量）都会碰 codec。

```sh
cd docs/headset-wip
S=$HAKIOS_SERIAL

# 1) 侦察（只读，先确认哪条路可用）
./android-accdet-trace.sh probe

# 2) 开 trace
./android-accdet-trace.sh start --kb 8192

# 3) 制造活动：播一段声音（不插耳机也能触发 codec 上电/下电）
adb -s "$S" shell 'cmd media_session volume --stream 3 --set 5' 2>/dev/null
adb -s "$S" shell 'am start -a android.intent.action.VIEW -d file:///system/media/audio/notifications/On_Time.ogg' 2>/dev/null
sleep 5

# 4) 关 + 落盘
./android-accdet-trace.sh stop

# 5) 拉回 + 解析
./android-accdet-trace.sh pull
./android-accdet-trace.sh parse
```

**(b) 抓开机 init（信息量最大）** —— 需要重启一次，**但要先确认不会启到 HakiOS**：

```sh
# 只在确认 boot_a 是原厂时才做（MASTER-STATE.md 记录 md5 a63b2e35... 为原厂）
adb -s "$S" reboot
# 立刻开始轮询，抓住 boot 后的 init 窗口
for i in $(seq 1 60); do
  adb -s "$S" wait-for-device 2>/dev/null && break
  sleep 1
done
./android-accdet-trace.sh start --kb 16384
sleep 25        # 覆盖 10s 定时器 + 余量
./android-accdet-trace.sh stop
./android-accdet-trace.sh pull && ./android-accdet-trace.sh parse
```

> `start` 要在 adb 一上线就执行。若 tracefs 里 `tracing_on` 在开机时被别的服务动过，
> `start` 会先 `echo 0` 再 `echo 1`，不影响。

**(c) 只要快照（零风险，已实测可行）**：

```sh
adb -s "$S" shell 'cat /sys/bus/platform/drivers/pmic-codec-accdet/dump_reg' > accdet-dump-now.txt
# 与 accdet-baseline.json 对拍
python3 accdet-compare.py accdet-dump-now.txt --baseline accdet-baseline.json
```

### 4.3 有耳机版（用户拿到耳机后跑这一句）

**一句话命令**：

```sh
cd ~/IOS/repo/hakios-mt6895/docs/headset-wip && HAKIOS_SERIAL=<serial> ./android-accdet-trace.sh run --secs 40
```

跑起来后按顺序做（脚本会等 40 秒）：

| 时刻 | 动作 |
|---|---|
| 0s | 什么都不做（空载基线） |
| 5s | **插耳机** |
| 10s | 等（让 debounce + 首次 ADC 跑完） |
| 15s | **按一下耳机按键**（音量键或播放键） |
| 20s | 等 |
| 25s | **再按一下**（不同键，用于对比 key 阻抗分档） |
| 30s | **拔耳机** |
| 35s | 等（plugout debounce） |
| 40s | 脚本自动 stop + pull + parse |

对应的逐条命令（想手动控制时序时用）：

```sh
cd docs/headset-wip
./android-accdet-trace.sh start --kb 16384
sleep 5                       # 空载基线
# >>> 现在插耳机 <<<
sleep 3                       # 插入 + debounce + 首次 ADC
# >>> 按一下耳机按键 <<<
sleep 2
# >>> 拔耳机 <<<
sleep 3
./android-accdet-trace.sh stop
./android-accdet-trace.sh pull
./android-accdet-trace.sh parse
```

### 4.4 环形缓冲与落盘大小估算

| 项 | 值 | 依据 |
|---|---|---|
| 单条 trace 记录 | 约 100–130 字节文本 | `regmap_reg_write: 0-05 reg=26b6 val=ff` + 时间戳/CPU/任务名 |
| 一次插入事件 | 估计 50–150 条（init 44 写 + ADC 轮询 + IRQ 清） | 源码 `accdet_init_once` 的写数 |
| 一次 init 段 | 约 50–80 条 | 同上 |
| 40 秒插拔实验 | **估计 < 2000 条 ≈ 200 KB** | 空载时几乎无流量 |
| `buffer_size_kb` 建议 | **8192（8 MB/CPU）** | 远超需要，但零成本；8 核最多 64 MB |
| `--kb 16384` | 16 MB/CPU | 开机 init + 插拔一起抓时用 |

> 缓冲是 **per-CPU** 的（`buffer_size_kb` 注释为 per-cpu），8 核 × 8 MB = 64 MB 上限。
> 手机 RAM 足够，不会 OOM。**若只抓插拔，4 MB 就够。**

**落盘位置**：设备侧 `/data/local/tmp/accdet-trace-<stamp>.txt`，主机侧 `./accdet-trace-<stamp>/`。

---

## 5. 结果解析

### 5.1 trace 长什么样

**方案 D（regmap tracepoint）**：

```
# tracer: nop
#
#           TASK-PID     CPU#  ||||   TIMESTAMP  FUNCTION
#              | |         |   ||||       |         |
     kworker/u16:1-812   [003] ....  1478.232024: regmap_reg_write: 0-05 reg=26a9 val=0
     kworker/u16:1-812   [003] ....  1478.232050: regmap_reg_read: 0-05 reg=26b6 val=ff
     kworker/u16:1-812   [003] ....  1478.232100: regmap_reg_write: 0-05 reg=26aa val=1
       mt6368-codec-801  [001] ....  1478.232200: regmap_reg_read: 0-05 reg=26b7 val=0
     kworker/u16:1-812   [003] ....  1478.232400: regmap_reg_read: 0-04 reg=1c2a val=3   ← MT6363，要排除
```

字段：`<任务名>-<pid> [<cpu>] <flags> <时间戳>: <事件名>: <regmap名> reg=<addr> val=<val>`

**方案 A（kprobe）**：

```
     kworker/u16:1-812   [003] ....  1478.232024: accdet_w: (regmap_write+0x0/0x20) reg=26a9 val=0
     kworker/u16:1-812   [003] ....  1478.232050: accdet_r: (regmap_read+0x0/0x20) reg=26b6
     kworker/u16:1-812   [003] ....  1478.232200: accdet_u: (regmap_update_bits_base+0x0/0x20) reg=26b6 mask=3f val=3f
```

### 5.2 怎么提取寄存器读写对

`accdet-trace-parse.py` 做三件事：

1. **按 `0-05` 过滤**掉 MT6363（`0-04`）的流量，并**统计丢了多少**（不静默丢弃）。
2. 抽 `(reg, value, rw)` **三元组序列**，保持 trace 顺序。
3. 折叠成 `accdet-baseline.json` 同格式（`regions.accdet/auddec/misc` + `values`），
   附带 `sequence` 全历史 + `diff_vs_baseline` 对拍结果。

```sh
# 基本用法
python3 accdet-trace-parse.py accdet-trace-xxx/accdet-trace-xxx.txt

# 指定输出 + 不打印统计
python3 accdet-trace-parse.py trace.txt --out parsed.json --quiet

# 只解析 kprobe 格式
python3 accdet-trace-parse.py trace.txt --format kprobe

# 自测（不需要真机、不需要 trace）
python3 accdet-trace-parse.py --selftest
```

输出**格式**示例（`stderr` 是统计，`stdout`/`--out` 是 JSON）。
> ⚠️ 下面这段是**格式示意**，数字是为展示三个字段的关系而编的，不是真机结果：

```
parsed accdet-trace-20261115-003000/accdet-trace-20261115-003000.txt
  lines                    2003
  matched                  1877
  kept                     1801
  dropped_other_device     76          ← 被排除的 MT6363 流量
  dropped_unparsed         0
  dropped_wide             0
  names seen: 0-05(1801), 0-04(76)
  vs baseline: 96 same, 12 differ, 22 trace-only, 0 baseline-only
wrote parsed.json (130 registers)
```

### 5.3 三个必须注意的解析陷阱

1. **`dropped_wide` > 0 是危险信号**：说明某条记录的 `val` 超过 8 位（PMIC 寄存器是单字节）。
   这通常意味着 kprobe 把**指针**当值打了出来（`regmap_read` 的 `x2` 是 `&val`）。
   → 改用 regmap tracepoint，或给读操作加 kretprobe。
2. **`regmap_reg_read_cache`** 表示值来自软件 cache，不是总线。本平台是 `REGCACHE_NONE`，
   正常不该出现；**若出现，说明抓的不是我们以为的那个 map**。
3. **`dump_reg` 会污染 trace**：`cat .../dump_reg` 会引发 130 次真实读。抓 trace 期间**不要 cat**。

### 5.4 与 baseline 对拍

```sh
python3 accdet-compare.py <解析出的 dump 文本> --baseline accdet-baseline.json
```

`accdet-baseline.json` 是 **130 个寄存器**（accdet 76 + auddec 48 + misc 6）的真机快照，
来自 `dump_reg`，无耳机。语义自洽：`0x26b6 = 0xff` → bit[7:6] = 3 = **拔出**。

---

## 6. 与「已确证的寄存器语义」的对应

抓到的 trace 里，下面几条是**判读插入状态的唯一依据**（语义已由厂商源码确证，本报告不重复验证）：

| 寄存器 | 位 | 名字 | 在 trace 里怎么看 |
|---|---|---|---|
| `0x26a9` | bit0 | `ACCDET_IRQ` | **中断标志，不是插入状态**。读到 1 只说明「有中断待处理」 |
| `0x26aa` | bit0 | `ACCDET_IRQ_CLR` | W1C。trace 里应看到 `reg=26aa val=01` 的写，紧跟 `26a9` 的读回 0 |
| `0x26b6` | bit[7:6] | `ACCDET_MEM_IN` = `cur_AB` | `>>6 & 3`：0=三段，1=四段耳麦，3=拔出，2 本平台不用 |
| `0x26b7` | bit[2:0] | `ACCDET_STATE` | 状态机。**驱动全文一次都没读过**（`grep ACCDET_STATE_ADDR` 只命中 `mt6368.h:4998` 定义行）→ trace 里**不该出现** `reg=26b7`；若出现，说明有别的东西在读 |

**预期 trace 形状**（插入事件，按源码推）：

```
write 26aa = 01          ← clear_accdet_int()
read  26a9               ← 确认 IRQ 已清
write 2688..            ← accdet_init() 的 44 写（含 0x268a/0x268c/0x268e...）
read  26b6               ← check_cable_type() 读 cur_AB
       ↑ 期望 bit[7:6] == 0（三段）或 1（四段）
write 26a9 / read 26a9   ← ...
```

**注意**：`cur_AB` **不是**「已插入」布尔 —— `check_cable_type()` 有前态 + `eint_sync_flag` 门控，
MIC_BIAS 下 `cur_AB==0` 表示**按键按下**而非三段耳机（`MASTER-STATE.md` §3.2）。
解析时不要把它当布尔值。

---

## 7. 交付物

| 文件 | 说明 | 验证 |
|---|---|---|
| `findings/ANDROID-ACCDET-TRACE.md` | 本文件 | — |
| `android-accdet-trace.sh` | POSIX sh，`probe`/`start`/`stop`/`pull`/`parse`/`run`/`selftest` | `sh -n` ✅ `dash -n` ✅ `selftest` 11/11 ✅ |
| `accdet-trace-parse.py` | trace → baseline 格式 JSON | `--selftest` 24/24 ✅；130 寄存器真实往返 130/130 ✅ |

### 7.1 脚本子命令

```sh
HAKIOS_SERIAL=<serial> ./android-accdet-trace.sh <cmd> [opts]

  probe     只读侦察：root / tracefs / kprobe / regmap tracepoint / 驱动形态 / 当前 state
  start     装填   [--mode regmap|kprobe|auto] [--kb N]
  stop      停 + 快照到设备侧文件
  pull      拉到 ./accdet-trace-<stamp>/
  parse     调 accdet-trace-parse.py 生成 JSON
  run       start + sleep + stop + pull + parse
  selftest  离线断言（不需要设备）
```

### 7.2 安全边界（脚本的写操作只有这些）

- 写 `/sys/kernel/tracing/{tracing_on, buffer_size_kb, trace, kprobe_events, events/*/enable}`
- **不碰** `spmi_access`（`selftest` 有专门断言，见下）
- **不碰** `set_reg` / `set_headset_mode` / `start_debug`（都是 0200 写口，本方案不用）
- **不** `adb root`（会重启 adbd，打断在跑的 capture）；只用现有 shell 权限
- **不**卸载/加载模块、不改任何控制硬件的 sysfs

`selftest` 里的两条回归守卫：

```
ok   no redirect into spmi_access in this script
ok   no hard-coded serial
```

第一条已用合成坏行验证过**确实会触发**（不是永远返回 ok 的空断言）。

### 7.3 selftest 实际输出

```
$ python3 accdet-trace-parse.py --selftest
ok   regmap: keeps exactly the MT6368 lines
ok   regmap: order preserved
ok   regmap: drops 0-04 (MT6363)
ok   regmap: 0-04 registers never reach output
ok   regmap: drops >8-bit values
ok   regmap: 0x2688 absent from output
ok   regmap: ignores non-regmap lines
ok   bucket: accdet window 0x2688-0x26d3
ok   bucket: auddec window 0x2508-0x2537
ok   bucket: misc catches 0x1108
ok   bucket: total matches sum
ok   kprobe: parses six probes
ok   kprobe: _w is a write
ok   kprobe: _r is a read
ok   kprobe: _u keeps the pre-image as a read
ok   fold: last value wins
ok   fold: sequence keeps every sample
ok   fold: kprobe last read wins
ok   diff: same value counted
ok   diff: differing value reported
ok   diff: baseline-only reported
ok   diff: trace-only reported
ok   empty: no sequence
ok   empty: stats still honest

24 checks, 0 failed
selftest: all assertions passed
```

```
$ ./android-accdet-trace.sh selftest
ok   adbd refuses without HAKIOS_SERIAL
ok   serial is passed with -s
ok   trace_root prefers /sys/kernel/tracing
ok   trace_root falls back to debugfs path
ok   trace_root fails when nothing is mounted
ok   pick_mode prefers regmap
ok   pick_mode falls back to kprobe
ok   explicit mode overrides detection
ok   pick_mode fails when neither exists
ok   no redirect into spmi_access in this script
ok   no hard-coded serial

11 checks, 0 failed
selftest: all assertions passed
```

### 7.4 真实数据往返验证（不是构造数据）

把 `accdet-baseline.json` 的 130 个寄存器转成 trace 文本喂回解析器：

```
wrote 130 lines
parsed /tmp/baseline-roundtrip.txt
  lines                    131
  matched                  130
  kept                     130
  dropped_other_device     0
  dropped_unparsed         0
  dropped_wide             0
  names seen: 0-05(130)
  vs baseline: 130 same, 0 differ, 0 trace-only, 0 baseline-only
wrote /tmp/roundtrip.json (130 registers)
```

**130/130 一致，零丢失。** 这验证了「reg 《—》 trace 文本 《—》 baseline JSON」这条链路的双向正确性。

---

## 8. 待补清单（设备回到 Android 后）

按优先级：

1. `./android-accdet-trace.sh probe` —— 一次填满 §1.3 全部空白，**并且决定走 D 还是 A**。
2. **验证 §2.3 的内联结论在真机上成立**：
   ```sh
   adb -s "$S" shell 'grep -cE " (accdet_read|accdet_write|accdet_update_bits)$" /proc/kallsyms'
   # 期望 3（符号在）；再挂一个 accdet_read 探针，做任何操作后看是否触发（期望 0 次）
   ```
   若不触发 → 内联结论在 5.10.237 上同样成立，本报告 §2.3 升级为「真机实测」。
3. 无耳机抓一次 init（§4.2b），确认 `accdet_init_once` 的 44 写能被完整捕获。
4. 用户拿到耳机后跑 §4.3 那一句。
5. 拿到插入 trace 后，用 `0x26b6` bit[7:6] 判读 `cur_AB`，与 XNU 侧对拍。

---

## 9. 附录：关键源码出处

| 事实 | 出处 |
|---|---|
| MT6368 在 SPMI USID 5 | `arch/arm64/boot/dts/vendor/mediatek/mt6368.dtsi`（`second_pmic@5`，`reg = <0x5 SPMI_USID>`） |
| accdet 节点参数 | `raw/mt6895.dtsi:2155-2185` |
| regmap 创建 | `drivers/mfd/mtk-spmi-pmic.c:571`（`devm_regmap_init_spmi_ext`） |
| regmap 配置 | 同 `:545-551`（`reg_bits=16, val_bits=8, max_register=0xffff, fast_io=true`） |
| accdet 取 regmap | `raw/mt6368-accdet.c:3307`（`dev_get_regmap(pdev->dev.parent, NULL)`） |
| 6 个 regmap 调用点 | 同 `:394, 408, 413, 421, 428, 435` |
| 包装函数定义 | 同 `:390, 398, 406, 411, 417, 425, 431` |
| 初始化定时器 | 同 `:206`（`10*HZ`）、`:3496`（`mod_timer`）、`:3219-3227`（timerhandler）、`:3211-3217`（work callback，真正执行 init） |
| `dump_reg` 属性 | 同 `:718-731`、`:826-844`（`DRIVER_ATTR_RW`） |
| `state` 属性 | 同 `:796-806`（`DRIVER_ATTR_RO`） |
| SPMI 设备命名 | `drivers/spmi/spmi.c:65` |
| `regmap_name()` | `drivers/base/regmap/internal.h:277-283` |
| regmap tracepoint 定义 | `drivers/base/regmap/trace.h`（`regmap_reg` class，write/read/read_cache） |
| tracepoint 插桩点 | `drivers/base/regmap/regmap.c:1969`（write）、`:2733`（read） |
| `regmap_read/write` 定义 | 同 `:2752` / `:1984` |
| `regmap_update_bits` 是 static inline | `include/linux/regmap.h:1075-1079` |
| `regmap_update_bits_base` | `regmap.c:3107`（`EXPORT_SYMBOL_GPL`） |
| `regmap_volatile` 语义 | `regmap.c:147-162` |
| `regcache_write` REGCACHE_NONE 直返 | `regcache.c:264-268` |
| regmap_spmi_ext 无 `.reg_update_bits` | `regmap-spmi.c:197-202` |
| PMIF read/write cmd | `spmi-mtk-pmif-core.c:376` / `:448` |
| PMIF 是 builtin | `drivers/spmi/Makefile`（`obj-$(CONFIG_SPMI_MTK_PMIF) += spmi-mtk-pmif.o`） |
| `$argN` 在 kretprobe 可用 | `kernel/trace/trace_kprobe.c:778-880` |
