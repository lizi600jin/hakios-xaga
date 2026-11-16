# DEVICE-REGMAP-IDENTITY-01：运行实例 notes 身份取证（只读）

> 2026-11-16 17:54，DSH。**本轮仅执行 CMD 列出的只读步骤。**
> **未挂 debugfs、未推/执行 reader、未播放、未改音量/耳机状态、未读 PMIC 寄存器。**
> **未 adb root、未关 SELinux、未改节点权限、未加载/卸载模块。**
> **未 fastboot/重启/刷写/分区写入/镜像打包。**

---

## 0. 结论

**三份 notes 全部读取成功（`rc=0`），大小与 SHA256 与封存件逐字符一致，
`cmp` 3/3 `IDENTICAL`，三个 build-id 全部匹配。**

**🔴 这只证明本轮运行实例的这三份身份元数据与封存件匹配。
不宣称整个运行内存逐字节相同，不宣称寄存器电气状态正确，也没有取得 trim。**
**这不是 hardware PASS，也不是设备放行。**

---

## 1. 设备与被试点

| 项 | 值 |
|---|---|
| host 时间 | **2026-11-16T17:53:51+08:00** |
| `adb devices -l` | `GAJZ4HIN99AQ69Y9  device usb:1-1 product:xaga model:22041216C device:xaga transport_id:1` |
| **serial 匹配** | ✅ **正是登记的 `GAJZ4HIN99AQ69Y9`** |
| 状态 | `device`（非 `unauthorized`、非 offline） |

**只对这一台操作，没有猜另一台。**

## 2. 运行状态

| 项 | 值 |
|---|---|
| `id` | `uid=0(root) … context=u:r:ksu:s0` |
| **`sys.boot_completed`** | **`1`** ✅ |
| `ro.boot.slot_suffix` | **`_a`** |
| **`boot_id`（前）** | **`0ebf2d81-4822-47df-be3c-51916b1a7663`** |
| `uptime`（前） | `66703.94` 秒 |
| `uname -a` | `Linux localhost 5.10.237-Fuutao-Qn_miao-gc5e20589c #0 SMP PREEMPT Thu Feb 20 00:00:00 CST 2025 aarch64 Toybox` |
| `getenforce` | **`Enforcing`** |

**`boot_completed=1` 才继续。**

**`uname` 只作上下文** —— 它与备份 Image 的 banner 一致，
**但不以 `uname`/`vermagic` 代替 build-id。**

---

## 3. 三份 notes 读取（`adb exec-out`，不过 PTY）

| 文件 | 命令 | **rc** | 字节 | stderr |
|---|---|---|---|---|
| `kernel.notes` | `adb exec-out "cat /sys/kernel/notes"` | **0** | **60** | 空 |
| `mtk_spmi_pmic.notes` | `adb exec-out "cat /sys/module/mtk_spmi_pmic/notes/.note.gnu.build-id"` | **0** | **36** | 空 |
| `regmap_spmi.notes` | `adb exec-out "cat /sys/module/regmap_spmi/notes/.note.gnu.build-id"` | **0** | **36** | 空 |

**stdout 原样存主机，未转文本、未合并 stderr、每条 rc 单独保存。**

### 路径存在性（读取前只读 `ls`）

```
-r--r--r-- 1 root root 60 /sys/kernel/notes
-r--r--r-- 1 root root 36 /sys/module/mtk_spmi_pmic/notes/.note.gnu.build-id
-r--r--r-- 1 root root 36 /sys/module/regmap_spmi/notes/.note.gnu.build-id
```

**三个路径都存在，权限 `r--r--r--`，未改权限。**

---

## 4. 逐字节 cmp 与 hash（3/3 IDENTICAL）

| 文件 | 实测字节 | 期望 | **cmp** | **SHA256（实测）** |
|---|---|---|---|---|
| **`kernel.notes`** | **60** | 60 | **IDENT** | **`0203fb7ac2483fe8cb5ce8cffab4daf67044f9dc69f54f34ef9d2edd70798847`** |
| **`mtk_spmi_pmic.notes`** | **36** | 36 | **IDENT** | **`d2e241e3ee565d2dc3d1ddacd3ba8bc3dfbc2a0365778d7a3f483d64756c113a`** |
| **`regmap_spmi.notes`** | **36** | 36 | **IDENT** | **`7d919564de7f200e9a447be133187c60c12c92c72d015bb25a381c671c7a6070`** |

**cmp 通过数：3 / 3**

**三个 SHA256 与 `expected-notes.json` 逐字符相同。**

---

## 5. build-id 机械解析（从原始字节，不靠 uname）

```
file                      bytes build-id found                               match
--------------------------------------------------------------------------------------------
kernel.notes                 60 76cc81c2a436243ca099b361b84defb940552ea2     OK
mtk_spmi_pmic.notes          36 9119ad181d63b4f694a561e4a2d367647ae78371     OK
regmap_spmi.notes            36 031912cdcf9827082872fc9f52f1921f380e621c     OK

全部匹配: True
```

**每个文件恰好一个 `NT_GNU_BUILD_ID`（type=3）note，无多余候选。**

---

## 6. 前后同一启动核验

| 项 | 前（17:53:55） | 后（17:54:28） | 结果 |
|---|---|---|---|
| **`boot_id`** | `0ebf2d81-4822-47df-be3c-51916b1a7663` | **同** | **SAME** ✅ |
| **`sys.boot_completed`** | `1` | **`1`** | **SAME** ✅ |
| `uptime` | 66703.94 s | 66736.67 s | **+32.73 s**（单调增长，未重启） |

**同一启动：True**

---

## 7. 判据核对

| 判据 | 结果 |
|---|---|
| 正确 serial 且 Android 就绪 | ✅ `GAJZ4HIN99AQ69Y9`、`boot_completed=1` |
| 本轮前后 `boot_id` 相同、`boot_completed=1` | ✅ 见 §6 |
| 三份 notes 读取 `rc=0` | ✅ 3/3 |
| 大小 / hash 匹配 | ✅ 3/3 |
| **`cmp` IDENTICAL（3/3）** | ✅ **3/3** |
| **称"本轮运行 notes 身份与封存件匹配"** | ✅ **可以这样称** |

---

## 8. 边界（严格）

### 只证明

- **三份身份元数据匹配**：kernel notes、`mtk_spmi_pmic` notes、`regmap_spmi` notes

### 不宣称

| 不宣称 | 原因 |
|---|---|
| **整个运行内存逐字节相同** | 只读了三份 notes |
| **寄存器电气状态正确** | 未读任何 PMIC 寄存器 |
| **已取得 trim** | 完全没碰 trim |
| **hardware PASS / 设备放行** | 本轮只做只读身份取证 |

### 本轮没做的事

未挂 debugfs、未推/执行 reader、未播放、未改音量、未动耳机插拔状态、
未读 PMIC 寄存器、未 adb root、未关 SELinux、未改节点权限、
未加载/卸载模块、未 fastboot/重启/刷写/分区写入/镜像打包、
未碰 `spmi_access`/vendor dump/Audio HP ImpeDance getter。

---

## 9. 证据文件

| 文件 | 内容 |
|---|---|
| `host-time.txt` | host 时间 |
| `adb-devices.txt` | `adb devices -l` |
| `device-state.txt` | 读取前状态（含 boot_id / uptime / uname / getenforce） |
| `device-state-after.txt` | 读取后复查 |
| `paths-check.txt` | 三个路径的存在性与权限 |
| **`kernel.notes` / `mtk_spmi_pmic.notes` / `regmap_spmi.notes`** | **原始二进制** |
| `*.notes.rc` / `*.notes.stderr` | 每条真实 rc 与 stderr |
| `cmp-results.txt` | cmp + size + SHA256 表 |
