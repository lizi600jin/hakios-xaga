# DEVICE-TRIM-CAPTURE-01：vendor 实际应用 trim 的取证

> 2026-11-16 18:03–18:04，DSH。**一次已验证 framework 正常播放 + 两次合格窗口 reader。**

**🔴 通过只表示本次 vendor 播放时应用值的取证成立。
不代表真实时钟波形、DC offset 校准质量、出声或 mode4 硬件 PASS。**
**四个 trim 参数由 Codex 解码，我只回报原始字节。**

---

## 0. 结果

```
258e: 18
258f: 8b
```

**B1 与 B2 逐字节相同（`cmp` IDENTICAL）。**

| 项 | B1 | B2 |
|---|---|---|
| reader rc | **0** | **0** |
| stderr | **空** | **空** |
| 字节数 | **18** | **18** |
| 格式匹配 | **True** | **True** |
| 原始 hex | `323538653a2031380a323538663a2038620a` | **同** |

---

## 1. 传输自检（未访问寄存器）

`adb shell -T -n`（**禁 `-x`**）+ `su -c` 固定 body：

```
printf '\101\000\102'; printf 'transport-check\n' >&2; exit 7
```

| 项 | 期望 | 实测 |
|---|---|---|
| **rc** | **7** | **7** ✅ |
| **stdout hex** | `410042` | **`410042`** ✅ |
| stdout 长度 | 3 | **3** ✅ |
| **stderr** | `transport-check\n` | **`b'transport-check\n'`** ✅ |

**最终 argv**：

```
['adb', '-s', 'GAJZ4HIN99AQ69Y9', 'shell', '-T', '-n', 'su', '-c', '<shlex.quote(body)>']
```

**一次通过，无需重试。**

**reader 使用同一通道** —— 远端退出码即 `subprocess` 的 `returncode`，
**没有用 `exec-out` 的 host rc 代替。**

---

## 2. 门控与 A 基线

| 项 | 值 |
|---|---|
| boot_id | **`0ebf2d81-4822-47df-be3c-51916b1a7663`**（同一启动） |
| boot_completed | **1** |
| getenforce | **Enforcing** |
| reader/wav SHA256 | **两份 MATCH** |
| **A gate rc** | **0**（30 项齐全、29 项播放 widget + DC Trim 全 Off） |

---

## 3. 播放启动

```
am start -a android.intent.action.VIEW \
  -d file:///data/local/tmp/reference-prep01-180239.wav -t audio/x-wav
```

| 项 | 值 |
|---|---|
| rc | **0** |
| stdout | `Starting: Intent { act=android.intent.action.VIEW dat=file:///... typ=audio/x-wav }` |
| **等 5 秒后焦点** | **`com.miui.mediaviewer/com.miui.musicplayer.activity.AudioActivity`** ✅ |
| **isKeyguardShowing** | **false** ✅ |

**未用 `tinyplay`/`tinymix`，未设音量/增益/路由。**
**`am` 返回 0 只当启动返回，不是我判断播放成功的依据。**

---

## 4. B1 / B2 完整证据

### 4.1 widget 状态（每阶段 30 项齐全、无重复）

| 阶段 | total | On | Off |
|---|---|---|---|
| **A** | 30 | 0 | **30** |
| **B1-pre** | 30 | **29** | 1（`DC Trim`） |
| **B1-post** | 30 | **29** | 1 |
| **B2-pre** | 30 | **29** | 1 |
| **B2-post** | 30 | **29** | 1 |
| **C** | 30 | 0 | **30** |

**29 项播放 widget 在 reader 前后都是 `On`；`DC Trim` 始终 `Off`（只记录，不推断线程）。**

### 4.2 AudioFlinger 活动线程

| 字段 | B1 | B2 |
|---|---|---|
| **pointer** | **`0xb400007a20fcf4d0`** | **同** ✅ |
| name | `AudioOut_15` | 同 |
| **I/O handle** | **21** | **同** ✅ |
| rate / channels / mask | 48000 / 2 / 3 | 同 |
| **device** | **`0x8`（wired headphone）** | **同** ✅ |
| **Frames written** | **6764544** | **7821312** |
| **Total writes** | **3303** | **3819** |
| **delta** | — | **frames +1056768、writes +516** |

**同一 thread pointer 与 I/O handle；帧与写计数都推进。**

**没有用 `Timestamp stats n` 当帧计数；没有从不同 thread 拼字段；没有预设 `pcm0p`。**

### 4.3 每次 reader 前后 gate

| 阶段 | gate rc |
|---|---|
| `B1-pre` + flinger | **0** |
| `B1-post` | **0** |
| `B2-pre` + flinger + `--previous B1-gate.json` | **0** |
| `B2-post` | **0** |

**`--previous` 的 `advance()` 校验了 pointer/io/rate/channels/channel_mask/device 全部相同，
且 frames 与 writes 都 > 0 增量。**

---

## 5. reader 执行统计

| 项 | 值 |
|---|---|
| **总 reader 执行次数** | **2**（B1 一次、B2 一次，未超限） |
| 每进程 pread | **1 次固定 `pread(18, 86526)`**（工具内部，不可多次） |
| 补读 / `cat` / `dd` / 旧 reader / 其他 offset | **均未尝试** |

### 命令原文

```
['adb', '-s', 'GAJZ4HIN99AQ69Y9', 'shell', '-T', '-n', 'su', '-c',
 '/data/local/tmp/regmap-reader-prep01-180239.aarch64']
```

**直接执行精确绝对路径，无参数。**

---

## 6. 清理与最终状态

| 步骤 | 结果 |
|---|---|
| `am force-stop com.miui.mediaviewer` | **rc=0**（只停测试播放器） |
| 等 3 秒后采 C | 30 项 widget、完整 flinger、dumpsys audio |
| **C gate** | **rc=0**（29 项回 `Off`，全 30 项 `Off`） |
| 删两个临时文件 | **rc=0**，`ls /data/local/tmp/*prep01*` → **No such file or directory** |
| **`umount /sys/kernel/debug`** | **rc=0**，`grep -c debugfs /proc/mounts` = **0** |
| **最终 boot_id** | **`0ebf2d81-4822-47df-be3c-51916b1a7663`**（**同一启动**） |
| 最终 boot_completed | **1** |
| 最终 uptime | 68216.54 s |

**未 force/lazy 卸载、未杀系统服务、未重启、未刷写。**

---

## 7. 判据核对

| 判据 | 结果 |
|---|---|
| 传输自检正确（rc=7、3B、stderr 精确） | ✅ |
| 用户在旁 | ✅（已声明） |
| 所有身份/绑定/PREP 条件保持 | ✅ |
| **B1/B2 每次 reader 前后 29 项 widget On** | ✅ 四次全 `rc=0` |
| 活动输出设备 `0x8`、48k 双声道 | ✅ |
| **同一线程帧/写计数推进** | ✅ pointer/io 相同，+1056768 / +516 |
| **每次 native rc=0、stderr 空、18B、格式正确** | ✅ 2/2 |
| **两个原始 packet 一致** | ✅ `cmp` IDENTICAL |
| 最多两个 reader 进程 | ✅ **恰好 2** |
| 每进程只一次固定 pread | ✅ |
| 清理后同一 boot、Android 就绪 | ✅ |
| 两临时文件已删 | ✅ |
| **debugfs 回到 PREP 前未挂载状态** | ✅ 计数 0 |
| C 软件状态如实报告 | ✅ |

---

## 8. 边界

**本轮放行并使用**：标准 framework 播放及其 vendor 既有正常供电行为、
两次只读 reader、本轮资源清理。

**没有做**：不通过工具直接写 PMIC/调 cache、不调供电/增益/路由、
不运行 SGEN/profile4、不刷写、不重启、不打镜像、
不修改 SELinux/节点权限、不用 `spmi_access`/vendor dump/Audio HP ImpeDance/`tinymix` getter、
**不读完整 `registers`**。

**`AIF_RX` 的 stream inactive 未作为失败判据；
`DC Trim` 采样状态未用于推断线程运行；没有改原始数据迎合判据。**

**本轮不生成 boot-args 或镜像。**
