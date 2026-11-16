# PREP-710-HOST 任务 1：冻结免费供电/SGEN 实现的可复核基线

> 2026-11-16 13:45，DSH。**未构建、未打镜像、未刷机、未播放。
> 未修改 `hakios-mtk` 源码，未用脏树覆盖隔离树。**

---

## 1. trial 709 产物（实测，非转述）

路径：`/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/trial-709-mt6685-read/`

| 文件 | 字节 | SHA256（实测） | 与 build.json |
|---|---|---|---|
| `boot.img` | **22990848** | `f4ff9732081e44a3cbac283631ce54dd9dbccec848eb5adee8a348af762fe413` | **一致** |
| `payload.bin` | **19515336** | `5ac894d6b6debbd608ac10750477ab0395e301af9819a8f7b54f32793f978539` | **一致** |
| `loader.bin` | **48712** | `f4c83012f5f60d0f5305aae54a8502d132cb3294adc513aa97776cd81fdd4343` | **一致** |
| `build.json` | 1623 | — | — |
| `mtkloader.o` | 91360 | — | — |
| `start.o` | 8648 | — | — |

**另有**：`boot.img` md5 = **`2e6b93bf83f7f60446754efe8e080a34`**，与 build.json 的 `image_md5` 一致。

**内核**：`c17cf881745511a3c2aa0225ca7a28f3e7b65e853688e13839319d8e6515010d`
（该内核二进制仍在隔离树 `BUILD/obj/.../mach.development.bcm2837`，**实测 hash 与 build.json 一致**）

---

## 2. 🔴 当前隔离树的八类输入 hash

路径：`/home/lizi/IOS/hakios-mtk/logs/msgbuf-1m-20261115/kernel-src/`

| # | 类别 | 文件 | SHA256（当前实测） |
|---|---|---|---|
| 1 | headset | `bsd/dev/hakios.mt6895_headset.c` | `c66e9f5ea6507298504c377c15d77db8d52c2ffa88ce4dd868b384e2655bf492` |
| 2a | pmif 头 | `bsd/sys/hakios.pmif.h` | `a39ec0a930f282bf627e074ed6bc55b666a43c009831ca3fbd8f26b094ba8367` |
| 2b | pmif 实现 | `bsd/dev/hakios.mt6895_pmif.c` | `57a5d61dec1a3d9ec649e8c2505e0294e2dee11a34660365d35c2027d607b433` |
| 3 | stubs | `bsd/dev/hakios.stubs.c` | `dd0fc309c171c929a5f4415cad3865062e173dff6fdcf9556c3b5b797533b0f4` |
| 4 | audio 头 | `bsd/sys/hakios.audio.h` | `17631a9fe6ba7c62bbe7bd2f3a5d4fc3c1a4c7dc3837414915f5be1a3e235191` |
| 5 | loader_return | `osfmk/arm64/loader_return.c` | `82811c1031463bb7e16f114871a8c899f0bd414f207e04bf13ffd7fd59f501cc` |
| 6 | keys | `bsd/dev/hakios.mt6895_keys.c` | `1992fdb7e90ea1b107d19f601666bc520662d91d777e262a4d4db8c59c20b4e3` |
| 7 | mali610 | `bsd/dev/hakios.mali610.c` | `b6bf3483b35ae93e61d5d574a13e79506c4e89caeb57dbeab555c2197c50ef45` |
| 8 | 构建配置 | `config/MASTER` | `f876755013a9ca15f684fda3c8d19f7251ab23b0e636b5b4175b1f312e9166e6` |

---

## 3. 🔴 与 709 历史记录的关联：**部分 INSUFFICIENT**

### 3.1 709 的 build.json **没有记录源文件 hash**

**它只有：** `kind`、`baseline`、`single_variable`、`write_still_blocked`、
`actual_loader_args`、`argument_bytes`、`kernel_sha256`、`payload_sha256`、
`loader_sha256`、`image_md5`、`image_sha256`、`image_bytes`、`flashed`、
`criteria`、`boundary`。

**没有源文件清单。** 所以**没有直接的逐文件 hash 对应**。

### 3.2 唯一可用的历史源清单是 15:33 的，**早于 707/708/709**

`logs/msgbuf-1m-20261115/build-source-sha256.json`（5204 项，mtime 15:33）

**对比结果：**

| 文件 | 15:33 清单 | 当前 | 判断 |
|---|---|---|---|
| `bsd/dev/hakios.mt6895_pmif.c` | `57a5d61d…` | `57a5d61d…` | **相同** |
| `bsd/dev/hakios.mt6895_headset.c` | `0cd8352f…` | **`c66e9f5e…`** | **不同** |
| `bsd/sys/hakios.pmif.h` | `3ba3ec42…` | **`a39ec0a9…`** | **不同** |

**两处不同正好对应 707/708/709 的改动**（headset.c 加 CLKSQ 上下电与 SID9 探测、
pmif.h 加 SID9 只读白名单）。

**所以：15:33 那份清单不能作为 709 的输入证据。**

### 3.3 三层间接支持（**都不是逐字节证明**）

| # | 证据 | 强度 |
|---|---|---|
| 1 | **709 的内核二进制仍在，实测 hash `c17cf881…` 与 build.json 一致** | **强** —— 说明这个内核确实是 709 产物 |
| 2 | **该内核里含 `MT6685 audclk 0x079e` 字符串 2 处** | **强** —— 证明它含 709 的探测代码 |
| 3 | **时间戳链**：`pmif.h` mtime 23:15:45 → `headset.c` 23:16:07 → 内核 23:16:18 → 刷机 23:17:14 | **弱** —— 时间戳不能证明内容一致 |

### 3.4 结论

| 项 | 判断 |
|---|---|
| 709 的**产物** hash | **PASS**（四个实测一致） |
| 709 的**内核** hash | **PASS**（实测一致） |
| 709 的**源文件** hash | **🔴 INSUFFICIENT** —— build.json 没记录，15:33 清单早于改动 |
| 当前源是否为 709 输入 | **🔴 未证明** —— 只有间接支持（内核含 709 特征串 + 时间戳链），**未做逐字节验证** |

**按指令：不把"当前文件一样大/同时间"当一致证据。我没有这么做，
但也没有更强的证据可用，所以如实标 INSUFFICIENT。**

### 3.5 若要补上这个缺口，可行的做法（未执行）

**重建验证**：用当前树重新构建，看内核 hash 是否仍为 `c17cf881…`。
**若一致，则当前源确实能产出 709 的内核**（但**仍不排除**源被改动后恰好产出相同二进制，
虽然概率极低）。

**本任务不构建，所以没做。这是留给下一轮的一个可选项。**
