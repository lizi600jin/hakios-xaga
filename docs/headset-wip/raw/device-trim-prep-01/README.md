# DEVICE-TRIM-PREP-01：正常播放取证环境准备

> 2026-11-16 18:02，DSH。**本轮只准备，未播放、未执行 reader。**
> **放行范围仅限本 CMD 列出的只读与临时挂载/推送步骤。**

**🔴 只能称「采集环境准备完成」。没有取得 trim，不是 hardware PASS。**

---

## 0. 五项门控全部通过

| 门控 | 期望 | 实测 | 结果 |
|---|---|---|---|
| serial | `GAJZ4HIN99AQ69Y9` | 同 | ✅ |
| **boot_id（同一启动）** | `0ebf2d81-4822-47df-be3c-51916b1a7663` | **同** | ✅ |
| `sys.boot_completed` | 1 | **1** | ✅ |
| SELinux | Enforcing | **Enforcing** | ✅ |
| **driver basename** | `mtk-spmi-pmic` | **同** | ✅ |
| **compatible** | `mediatek,mt6368` + NUL | **`6d 65 ... 38 00`** | ✅ |
| **name** | `mtk-spmi-pmic\n` | **`6d 74 6b 2d 73 70 6d 69 2d 70 6d 69 63 0a`** | ✅ |
| **range** | `0-ffff\n` | **`30 2d 66 66 66 66 0a`** | ✅ |
| **三 cache 不存在** | 不存在 | **`ENOENT`** | ✅ |

---

## 1. SID 5 绑定（任务 2）

### uevent 原文

```
DRIVER=mtk-spmi-pmic
OF_NAME=second_pmic
OF_FULLNAME=/spmi@1c804000/second_pmic@5
OF_COMPATIBLE_0=mediatek,mt6368
OF_COMPATIBLE_N=1
MODALIAS=of:Nsecond_pmicT(null)Cmediatek,mt6368
```

### 符号链接

```
driver  -> ../../../../../bus/spmi/drivers/mtk-spmi-pmic
of_node -> ../../../../../firmware/devicetree/base/spmi@1c804000/second_pmic@5
```

### compatible（保留 NUL 原始字节）

```
od -c: m e d i a t e k , m t 6 3 6 8 \0
od -x: 6d 65 64 69 61 74 65 6b 2c 6d 74 36 33 36 38 00
```

**地址 `0x1c804000` 与 SID `5` 都与历史记录一致，未换 SID/路径。**

---

## 2. debugfs 与 regmap 元数据（任务 3）

### 挂载所有权

| 项 | 值 |
|---|---|
| 挂载前 `grep -c debugfs /proc/mounts` | **0**（未挂载） |
| 命令 | `mount -t debugfs debugfs /sys/kernel/debug` |
| **rc** | **0** |
| stderr | 空 |
| 挂载后条目 | `debugfs /sys/kernel/debug debugfs rw,seclabel,relatime 0 0` |
| **归属** | **`this-round`（本轮挂载，我负责）** |

### 目录列举（成功）

```
access      registers
name        range
```

**只有 4 项，无 `cache_only`/`cache_bypass`/`cache_dirty`。**

### name / range 精确匹配

```
name  od -c: m t k - s p m i - p m i c \n      hex: ...6d 69 63 0a
range od -c: 0 - f f f f \n                   hex: 30 2d 66 66 66 66 0a
```

### 三 cache 缺失（不是 `test -e`，是真的 open 失败）

| 文件 | 结果 |
|---|---|
| `cache_only` | `cat: No such file or directory`（**ENOENT**），rc=1 |
| `cache_bypass` | **同上** |
| `cache_dirty` | **同上** |

**目录列举的 grep 计数为 0；三者都是 ENOENT 而非 EACCES。**

**未读取 `registers`，未读 `access`，未读其他 regmap map。**

---

## 3. DAPM idle 基线（任务 4）

**路径**：`/sys/kernel/debug/asoc/mt6895-mt6368/mt6368-sound/dapm/`

| 项 | 结果 |
|---|---|
| **采到** | **30 项**（零不可读） |
| **On/Off 统计** | **30 Off** |
| **29 项播放 widget** | **全部 Off** ✅ |
| `DC Trim` | **Off**（只记录采样状态，不用它推断校准线程） |

**没有任何一项非 Off，所以不需要"等 3 秒再取一次"。**

### framework 状态

| 项 | 值 |
|---|---|
| `dumpsys audio` | 966 行 |
| `dumpsys media.audio_flinger` | 759 行 |
| **活动音频 track**（`Standby: no` 计数） | **0**（无活动音频） |
| `mCurrentFocus` | `com.miui.home/...Launcher` |
| `mFocusedApp` | `com.miui.home/.launcher.Launcher` |
| `isKeyguardShowing` | **false**（已解锁） |
| `mDreamingLockscreen` | false |

### 媒体音量（只记录，未修改）

`STREAM_MUSIC`: `Muted: false`、`Muted Internally: false`、`Min: 0`、`Max: 15`

### 播放器入口（只读 resolve-activity）

```
com.miui.mediaviewer/com.miui.musicplayer.activity.AudioActivity
```

**与历史记录一致。本轮未 `am start`、未做解锁操作。**

---

## 4. 两个临时文件（任务 5）

### 主机先核验

| 文件 | 字节 | SHA256 | 核验 |
|---|---|---|---|
| `regmap-reader.aarch64` | **4232** | `63688efccd5728b0155a782eb299db979d6150b2096b4dacbc1e083429aa30e1` | ✅ |
| `reference-48k-stereo-60s.wav` | **11520044** | `c6586d5173c366fc66c37a71743919774b643f4776d6d2bccb53e088baeaba97` | ✅ |

### 设备路径（全新唯一名，推送前确认不存在）

| 项 | 值 |
|---|---|
| **reader** | **`/data/local/tmp/regmap-reader-prep01-180239.aarch64`** |
| **wav** | **`/data/local/tmp/reference-prep01-180239.wav`** |
| reader 权限 | `-rwxr-xr-x`（**0755**） |
| wav 权限 | `-rw-r--r--`（**0644**） |

### 设备侧 SHA256（逐字一致）

```
63688efccd5728b0155a782eb299db979d6150b2096b4dacbc1e083429aa30e1  /data/local/tmp/regmap-reader-prep01-180239.aarch64
c6586d5173c366fc66c37a71743919774b643f4776d6d2bccb53e088baeaba97  /data/local/tmp/reference-prep01-180239.wav
```

**两份都 `MATCH`。**

### 🔴 reader 执行次数 = 0

**本轮没有运行 reader，连 `--help`/`--version` 都没跑** ——
该工具忽略参数，**任何运行都会尝试固定 pread**，所以一次都没执行。

**播放次数 = 0。**

---

## 5. 当前保留状态（等下一轮播放采集 CMD）

| 项 | 状态 |
|---|---|
| **debugfs** | **仍挂载，归属 `this-round`** |
| **reader 临时文件** | **保留** `/data/local/tmp/regmap-reader-prep01-180239.aarch64` |
| **wav 临时文件** | **保留** `/data/local/tmp/reference-prep01-180239.wav` |
| boot_id | **仍 `0ebf2d81-4822-47df-be3c-51916b1a7663`**（同一启动） |
| `boot_completed` | **1** |
| uptime | 67243.31 s |

**没有删除任何东西，因为各项门控全部成功。**

---

## 6. 判据

| 判据 | 结果 |
|---|---|
| 同一 boot | ✅ |
| 正确 SID5 绑定 | ✅ driver `mtk-spmi-pmic`、compatible `mediatek,mt6368` |
| `name`/`range` 精确匹配 | ✅ 逐字节 |
| 成功目录枚举中无三 cache | ✅ 4 项，无 cache |
| 30 项 widget 均能读取 | ✅ 30/30 |
| 29 项播放 widget 为 Off 基线 | ✅ |
| 媒体音量只记录 | ✅ 未修改 |
| 播放器入口身份一致 | ✅ |
| 两个临时普通文件设备 hash 与主机一致 | ✅ 两份 MATCH |
| **reader 执行次数 0、播放次数 0** | ✅ |
| **只能称采集环境准备完成** | ✅ **本文档如此表述** |

---

## 7. 边界

### 本轮新增放行并使用

临时 debugfs 挂载、指定绑定/元数据/DAPM 与 framework 状态读取、
两个临时文件推送与常规文件权限设置。

### 没有做

**未读任何 PMIC 寄存器**、**未执行 reader 或 vendor dump**、
未碰 `spmi_access`、未碰 Audio HP ImpeDance getter、
未读 `tinymix` 控件 getter、未改 regmap/cache 标志、未改 SELinux、
未改设备节点权限、未改路由/音量、未动耳机插拔状态、未重启、未刷写、未打包镜像。

**没有运行旧 `1d96bdd5` reader。**
**没有因元数据与期望不同而猜新 offset 或其他 map**（本轮元数据全部匹配，无需此判断）。
