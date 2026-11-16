# PACK-711：trial711 短音实验镜像打包

> 2026-11-16 19:25，DSH。**仅主机打包。未碰设备、未播放参考 WAV、未刷写。**

**🔴 打包完成不等于硬件通过。等 Codex 审阅并明确指定镜像后才可刷。**

---

## 0. 产物

| 项 | 值 |
|---|---|
| **boot.img** | **22994944 B** |
| **SHA256** | **3f57a80680e1eb7952ed49f6a149ec61fb2ab97a88937390395305a4f3b88fa6** |
| **MD5** | **0810f7b33b17cbd5036890ce9d8e66ba** |
| payload.bin | **19515720 B** / `51ff852c263858e77ca892ba4131aa92e7a14d7bfdc7cab87e8e5de9bcca0935` |
| loader.bin | **48896 B** / `8762f81490b078745f4cc3dce738aa076b7c7f14582c08e18c2897ff00c318b7` |

**路径**：`/home/lizi/IOS/hakios-mtk/logs/profile4-tone-20261116/trial711/`

---

## 1. Codex 对两条反对意见的处置（保留原答复）

| # | 我的反对意见 | Codex 答复 |
|---|---|---|
| **1** | `C_MUTE_SW_CTL` bit6 切断哪一级不明，数字静音能否形成干净 gap 需实测 | **"We do NOT assume where its hardware cut point is or that it yields an acoustically clean gap. The phone recording is intended to measure whether the three requested intervals/gaps are audible. A c0/80 register sequence alone cannot satisfy the acoustic criterion."** |
| **2** | tone 完成提前关闭，后 123 秒空闲 | **"Early close after 2000ms wait plus three 1000ms intervals and two 1000ms gaps is intentional. The existing 150s kernel return timer is retained. All remaining time is deliberately idle with the profile restored."** |

**Codex 明确：两项均不要求改代码。**

**补充约束（Codex 原文）**：**"Only successful `tone edge ... verified` records count; a failed attempted pulse number is not an emitted pulse or an acoustic observation."**
→ 正好对应我上轮提的次要观察（`pulse++` 在 gate 之前），**Codex 已明确失败不算已发射脉冲**。

---

## 2. 固定输入核验

| 输入 | 期望 | 实测 |
|---|---|---|
| **新内核** | 15321256 B / `24df55bf…` | ✅ 一致 |
| stock boot | 67108864 B / `c5f8e7bd…` / md5 `a63b2e35…` | ✅ 一致 |
| 参考 ramdisk | `78985f67…` | ✅ 一致 |
| 参考 kernel.ver（48 B 含 NUL） | `363b5257…` | ✅ 一致 |

**loader 源**：从 `trial710/loader-src` **复制**（37 文件，原快照未改）
- `mtkloader.c` **263793 B / `60647f896d02708984388e0c60051824dbae6327d180385a8ac7d754652af3e7`** ✅
- `start.S` 16953 B、`link.ld` 770 B、`board.h` 48016 B、`identity.h` 8271 B

**未复用 trial710 的旧二进制参数**，只复用其结构化 argv 模板。

---

## 3. 参数串（522 字节 base）

```
rd=md0 -v cpus=4 buttons=2 touchscreen=3 keylog=1 key_scan=1 fbwidth=1080 fbheight=2460 fb_disp=1 mali=5 mali_cold=1 mali_preas0=0 mali_low=0 mali_trace=1 mali_witness=0 mali_ptvendor=1 mali_scclk=1 mali_scupd=1 mali_sc26=0 mali_cacheflush=0 mali_resetonly=0 mali_cohnone=0 mali_hwissues=0 mali_stackpwron=0 mali_mcuwin=0 mali_mcuseq=0 mali_doorbell=0 mali_pdcsettle=0 mali_statussem=0 mali_assorder=1 mali_trial=711 mali_nopdc=1 headset=4 hp_trim_l=3 hp_trim_r=17 hp_fine_l=0 hp_fine_r=3 hp_tone=1 loader_return_after=150
```

| 检查 | 结果 |
|---|---|
| **base 长度** | **522 字节** ✅ |
| **含 NUL** | **523** |
| **运行时（+ + 47 字符 ver）** | **582** |
| **含终止 NUL** | **583 < 608** ✅ 余量 25 |
| `BOOT_LINE_LENGTH` | **608**（源码确认） |

### 本体定位（重要）

**`loader.bin` 里该串出现 3 次**：

| # | offset | 后随字节 | 判定 |
|---|---|---|---|
| 1 | `0x9884` | `\n` | printf 格式串片段（`boot-args: %s`） |
| 2 | `0x9e0c` | `\n` | printf 格式串片段（`boot args: %s`） |
| **3** | **`0xa9d2`** | **`NUL`** | **`XNU_BOOT_ARGS` 本体** ✅ |

**本体逐字节与期望 BASE 相同：True。**

### 本体内的关键项唯一性（各 1 次）

`mali_trial=711`、`headset=4`、**`hp_tone=1`**、`hp_trim_l=3`、`hp_trim_r=17`、
`hp_fine_l=0`、`hp_fine_r=3`、`loader_return_after=150`、`mali_nopdc=1`

**顺序**：`hp_tone=1` 在 offset 489，`loader_return_after=150` 在 499 → **顺序正确** ✅

### 禁止项（全 0）

`mali_trial=710`、`mali_trial=709`、`headset=3`、`headset=1`、`hp_tone=2`、`gpueb`

---

## 4. 编译与链接（native rc 全 0）

| 步骤 | native rc | 结果 |
|---|---|---|
| `start.o` | **0** | — |
| `mtkloader.o` | **0** | — |
| `ld.lld` → `loader.bin` | **0** | **48896 B** |

**完整 argv/stdout/stderr/rc 在 `compile-commands.json`。**
**`BOARD_HEADER` 参数值含字面双引号（Python argv 传递，无 shell 引号问题）。**

---

## 5. payload（mkpayload.py --no-version，未手写）

```
python3 xnuloader/mkpayload.py -o <T>/payload.bin --no-version \
  kernel=<新内核> ramdisk=<ref-ramdisk> kernel.ver=<ref-kernel.ver>
native rc = 0
```

| # | 名称 | 大小 | SHA256 | 与 trial710 |
|---|---|---|---|---|
| 0 | **kernel** | **15321256** | **`24df55bf…`** | **CHANGED（预期）** |
| 1 | ramdisk | 4194304 | `78985f67…` | **SAME** ✅ |
| 2 | kernel.ver | 48 | `363b5257…` | **SAME** ✅ |
| 3 | **\<终止\>** | 0 | — | **SAME** ✅ |

**reserved 全为 0；kernel 的 8 字节 padding 全零；24 字节零终止项在 `pos=19515696`；
`exact EOF: True`。**

---

## 6. chainboot 与严格解包

```
python3 mtk/tools/chainboot.py <stock> <loader> <out> <payload> --partition-size 64M
native rc = 0
22994944 bytes (program 48896, Image 42201364, payload 19515720, kernel gz 21112803)
```

### 验收 1：Android header

| 项 | 值 |
|---|---|
| magic | `ANDROID!` |
| 总长 | **22994944 < 64 MiB** |
| **前 4096 与 stock 差异** | **仅 4 字节，位置 `[8,9,10,11]`** = `kernel_size` ✅ |

### 验收 2：stock 其他组件未动

| 项 | 结果 |
|---|---|
| `ramdisk_size` | **1867873 vs 1867873 SAME** ✅ |
| `header[24:40]` | **相同** ✅ |
| **cmdline `44..1580`** | **相同** ✅ |
| `signature_size 1580..1584` | **相同** ✅ |

### 验收 3：解压链

| 项 | 结果 |
|---|---|
| blob | 61979240 B |
| **loader 段差异** | **仅 10 字节**：`0x12`、`0x13`、`0x28–0x2b`、`0x30–0x33`（chainboot 文档的 arm64 header 补丁）✅ |
| 填充区 `[48896, 0x40000)` | **全零** ✅ |
| **stock Image @ `0x40000`** | **逐字节相同** ✅ `12d255cd…` |
| **payload @ 42463520** | **逐字节相同** ✅ |

### 验收 4：嵌套 XNUP

**kernel `24df55bf`、ramdisk `78985f67`、kernel.ver `363b5257`、终止项 24 B 全零、exact EOF。**

### 验收 5：loader header 补丁值

| 偏移 | 值 | 期望 |
|---|---|---|
| `0x10` | `0x3c00000` | — |
| `0x28` res2 | **42201364** | stock Image 长度 ✅ |
| `0x30` res3 | **`0x287f120`** | payload 偏移 ✅ |

---

## 7. 既有产物守门（全部未变）

| 项 | 值 |
|---|---|
| old profile4 kernel | **`cac498da96406cc4…`** ✅ |
| old 709 kernel | **`c17cf881745511a3…`** ✅ |
| shared `hakios-kernel` | **`67f9f933117f9bc2…`** ✅ |
| wrapper | **`95247bb011c136d2…`** ✅ |
| **trial710 revision2 boot.img** | **`ca161c1bdb002540…`** ✅ 未覆盖 |
| **trial710 revision2 payload** | **`e29f55eba59aa256…`** ✅ |
| **trial710 loader.bin** | **`daa05b1736fafcb2…`** ✅ |
| GPUEB WIP | **`9cdcb0da826e9bd5…`** ✅ |

---

## 8. 关于 `reference-711.wav`（本轮未用）

| 项 | 值 |
|---|---|
| 大小 | **1536044 B** |
| SHA256 | **`b994ddba0465ab54e6d33944f49373e8539b51601e7b392c7e2c2eee96408a29`** |
| 规格 | 8 s、48k、stereo 16-bit、**997 Hz** 脉冲在 2–3 / 4–5 / 6–7 s、PCM peak **2048/32768**、短 fade |

**🔴 本轮未播放、未推送。** 它只用于后续验证麦克风位置与录音质量，
**不建立与 SGEN 电平等价或频率等价的结论**。

---

## 9. 边界

**本轮没有**：adb/fastboot、生产代码编辑或 push、旧证据覆盖、
共享/默认树内核输出、stash/reset/checkout/clean/gc/prune、GPUEB 扩展。

**未播放参考 WAV、未刷写、未碰设备。**

**等 Codex 审阅本镜像并给出明确设备 CMD 与录音开始提示后，才进入下一阶段。**
