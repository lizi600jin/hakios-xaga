# VERIFY-PROFILE4-HOST-01：profile4 独立复核与新隔离树主机构建

> 2026-11-16 15:28，DSH。**全程主机操作。
> 未 adb/fastboot、未挂 debugfs、未播放、未读写设备、未打包镜像/payload、未刷写。**

**结论只能用 HOST MODEL / BUILD 表述 —— 不是 hardware PASS，不是出声 PASS，不是已校准。**

---

## 0. 六句话

**一、8 项生产输入 hash 全部一致，0 失败。**

**二、两套主机测试都通过：profile `rc=0`（271 cases / up 113 / close+restore 114 /
trace up 161 / down 139，五项全中）、legacy `rc=0`。**

**三、新隔离树相对封存基底：新增 0、消失 0、恰好 2 项不同（`headset.c` 与 `pmif.h`），
且两者 hash 与 `production-inputs.sha256` 一致。**

**四、真实构建 `rc=2`（既有的 kext symbolset），编译前后非 BUILD 输入零变化；
显式 `KERNEL --judge` 返回 `rc=0`，判词只豁免既有的 symbolset。**

**五、新内核 `cac498da96406cc4f660e2cc717dbcf93b259ae1d864de6f30a9e920ed03401d`，
15321160 字节（比 709 的 15320872 多 288 字节）。**

**六、709 kernel / hakios-kernel / wrapper / 709 镜像四项全部未变。**

---

## 1. 8 项生产输入核验（任务 1）

来源：`hakios-mtk/logs/headset-profile-20261116/production-inputs.sha256`
在 `hakios-mtk` 根目录逐文件核验，**未改任何代码**。

| # | 文件 | 结果 |
|---|---|---|
| 1 | `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c` | **OK** `3c79386a…` |
| 2 | `xnu/files/bsd/sys/hakios.pmif.h` | **OK** `f4d9a964…` |
| 3 | `mtk/tools/test-headset.py` | **OK** `2228331b…` |
| 4 | `mtk/tools/test-headset-profile.py` | **OK** `afb21db5…` |
| 5 | `mtk/tools/fixtures/mt6368-profile4-prefix.c` | **OK** `75971b91…` |
| 6 | `mtk/tools/fixtures/mt6368-profile4-suffix.c` | **OK** `99ba5f30…` |
| 7 | `mtk/tools/fixtures/mt6368-profile4-vendor.json` | **OK** `81a836bf…` |
| 8 | `mtk/tools/fixtures/mt6368-hp-normal.json` | **OK** `a8858f97…` |

**失败项：0。** 原文见 `input-verify.txt`。

### 环境

| 项 | 值 |
|---|---|
| Python | **3.14.4**（`/usr/bin/python3`） |
| clang | **21.1.8** |
| ld.lld | **21.1.8** |
| xnu-clang | 同 21.1.8（需 `source xnu-env.sh`） |

---

## 2. 两套主机测试（任务 2）

### 2.1 profile 测试

```bash
python3 mtk/tools/test-headset-profile.py \
  --log-file <E>/profile.log --result-file <E>/profile-result.json
```

| 项 | 期望 | 实测 | 结果 |
|---|---|---|---|
| 真实退出码 | 0 | **0** | ✅ |
| cases | 271 | **271** | ✅ |
| up bus writes | 113 | **113** | ✅ |
| close+restore bus writes | 114 | **114** | ✅ |
| trace up | 161 | **161** | ✅ |
| trace down | 139 | **139** | ✅ |

**`model_summary` 原文**：
```
PASS 271 cases; up bus writes=113; close+restore bus writes=114
```

**`trace_events`**：`{"up": 161, "down": 139}`

**stderr 空。`profile.log` 2580890 B。**

**`limitations` 原文（四条）**：

```
Field-normalized writes/delays, not electrical clock or audio validation
VAUD18 must already be enabled; the profile does not switch its regulator
Initial shared-field patterns and trim values are synthetic model inputs
Oracle DAPM ties use declaration order; no physical hardware tested
```

### 2.2 legacy 测试

```bash
python3 mtk/tools/test-headset.py --log-file <E>/legacy.log
```

| 项 | 值 |
|---|---|
| 真实退出码 | **0** |
| stderr | 空 |
| `legacy.log` | 177844 B |

**stdout 原文（覆盖范围）**：

```
production accdet: mode1 no writes, 19 stock config bytes, EINT gating, mic button,
unplug, 3-pole timer, W1C; HP matches independent vendor instruction trace,
5-bit gain readback, bounded stereo retries, shared-field corruption,
repeat insertion, fault rollback, worker stop and zero-access post-stop passed
```

**注意：这里只覆盖 mode 1/2/3 与既有 HP 路径，不含 profile4（那是上面那套测试）。**

### 2.3 ASan/UBSan

**审定的 `IMPLEMENTATION-REVIEW.md` §主机验证写明「ASan/UBSan 下 271 个用例通过」。**

**我这边看到的证据是**：`rc=0`、stderr 空、`profile.log` 2.5 MB。
**日志里是否含 sanitizer 输出，我没有逐行搜** —— 按审定的说法记录，不额外推断。

---

## 3. 新隔离树的构建输入（任务 3）

### 3.1 基底核验

| 项 | 值 |
|---|---|
| 基底 | `logs/build-reference-710-20261116-135551/inputs`（**只读**） |
| 文件数 | **5205** |
| manifest 核验 | `sha256sum -c` **rc=0**，**OK 5205 行、FAILED 0 行** |

### 3.2 新树

| 项 | 值 |
|---|---|
| 路径 | **`logs/profile4-kernel-src-20261116-1525`** |
| 文件数 | **5205** |
| 大小 | 89M |
| **BUILD 存在** | **NO**（复制时就没有） |

### 3.3 只替换两项

| 文件 | 封存基底 → 新树 | 与 production-inputs 对应项 |
|---|---|---|
| **`bsd/dev/hakios.mt6895_headset.c`** | `c66e9f5e…` → **`3c79386ac367ba81fe02774fbb8d37a6475845432a644ff91fdcb56a98c279ce`** | **与第 1 项一致** ✅ |
| **`bsd/sys/hakios.pmif.h`** | `a39ec0a9…` → **`f4d9a964e304ea14686359b85e75560325e9d47ee2ecb5614aae538f9e293d78`** | **与第 2 项一致** ✅ |

**源：当前生产树的同名文件。**

### 3.4 与封存 5205 项逐个比较

| 项 | 数量 |
|---|---|
| **新增文件** | **0** |
| **消失文件** | **0** |
| **内容不同** | **2**（恰好是上面那两项） |

### 3.5 编译前的关键共享文件 hash

| 文件 | SHA256 |
|---|---|
| `config/MASTER` | `f876755013a9ca15f684fda3c8d19f7251ab23b0e636b5b4175b1f312e9166e6` |
| `config/MASTER.hakios` | `dfe75d2845c7aa8236a0986dd9af960a727d1cb923ba20c338df7140a7b89a26` |
| `bsd/dev/hakios.mt6895_pmif.c` | `57a5d61dec1a3d9ec649e8c2505e0294e2dee11a34660365d35c2027d607b433` |
| `bsd/dev/hakios.stubs.c` | `dd0fc309c171c929a5f4415cad3865062e173dff6fdcf9556c3b5b797533b0f4` |
| `bsd/dev/hakios.mt6895_keys.c` | `1992fdb7e90ea1b107d19f601666bc520662d91d777e262a4d4db8c59c20b4e3` |
| `osfmk/arm64/loader_return.c` | `82811c1031463bb7e16f114871a8c899f0bd414f207e04bf13ffd7fd59f501cc` |
| `bsd/dev/hakios.mali610.c` | `b6bf3483b35ae93e61d5d574a13e79506c4e89caeb57dbeab555c2197c50ef45` |
| `bsd/sys/hakios.audio.h` | `17631a9fe6ba7c62bbe7bd2f3a5d4fc3c1a4c7dc3837414915f5be1a3e235191` |

**`config/MASTER` 与 709 时相同（`f8767550`）—— msgbuf 1 MiB 配置仍在。**

---

## 4. 真实构建（任务 4）

```bash
cd /home/lizi/IOS/hakios-mtk
XNU_SRC=/home/lizi/IOS/hakios-mtk/logs/profile4-kernel-src-20261116-1525 ./xnu-build.sh -k -j8
```

**未使用 `make-kernel.sh` 正常构建分支。**

| 项 | 值 |
|---|---|
| 开始 | 2026-11-16T15:26:04+08:00 |
| 结束 | 2026-11-16T15:27:18+08:00 |
| **耗时** | **74 秒** |
| **真实退出码** | **`2`** |
| stdout | `build-stdout.log`，**124870 B** |
| stderr | `build-stderr.log`，**76191 B** |

### 4.1 编译后复算输入（必须零变化）

| 项 | 值 |
|---|---|
| 非 BUILD 文件数（编译后） | **5205** |
| 新增 | **0** |
| 消失 | **0** |
| **内容变化** | **0** |

**编译前后输入零变化 ✅**

### 4.2 新内核

| 项 | 值 |
|---|---|
| 路径 | `<新树>/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837` |
| **字节** | **15321160** |
| **SHA256** | **`cac498da96406cc4f660e2cc717dbcf93b259ae1d864de6f30a9e920ed03401d`** |

**对比 709 的 15320872 字节 → 多 288 字节**（符合新增 profile4 代码的量级）。

### 4.3 judge（显式 KERNEL）

```bash
KERNEL=<新树>/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837 \
  ./tools/make-kernel.sh --judge <stdout+stderr 合并日志>
```

| 项 | 值 |
|---|---|
| **judge 退出码** | **`0`** |
| **判词原文** | **`make-kernel: the log shows a built kernel; only the kext symbol-set checks failed`** |

### 4.4 错误分类（确认只容许既有 symbolset）

日志里 `error:`/`错误` 共 **12 行**，全部属于 `check_all_exports` / `symbolset` 链：

| 错误 | 次数 |
|---|---|
| `错误 2` | 4 |
| `错误 1` | 4 |
| 「错误目标 `do_config_all`/`config_all`/`all`/`default` 并未重新制作」 | 各 1 |

**规律**：

- `make[4]: *** [.../config/Makefile:121：check_all_exports] 错误 1`
- `make[4]: *** [.../Libkern.symbolset] 错误 1`
- 随后 `config_all` → `all` → `default` 逐级传播

**🔴 源码编译错误 `*.c:line:col: error:` 计数为 0。**

**没有来源不明的失败。**

### 4.5 保留与不复制

**新树 BUILD 保留在原位**（不移动、不删除）。
**未复制到 `hakios-kernel`。**

---

## 5. 既有产物核验（任务 5）

| 项 | 期望 | 实测 | 结果 |
|---|---|---|---|
| 709 kernel | `c17cf881745511a3…` | **`c17cf881745511a3c2aa0225ca7a28f3e7b65e853688e13839319d8e6515010d`** | ✅ |
| `hakios-kernel` | `67f9f933117f9bc2…` | **`67f9f933117f9bc2b1fb6c9566cd032079a83dab218825ecb838a385d15f77e1`** | ✅ |
| `tools/make-kernel.sh` | `95247bb011c136d2…` | **`95247bb011c136d22ca70b485a3388e46720464b6b40f3842bac2acdee80dbd3`** | ✅ |
| 709 镜像 | `f4ff9732081e44a3…` | **`f4ff9732081e44a3cbac283631ce54dd9dbccec848eb5adee8a348af762fe413`** | ✅ |

---

## 6. 判据核对

| 判据 | 结果 |
|---|---|
| 8 项输入核验通过 | ✅ **8/8** |
| 两套主机测试均通过 | ✅ **profile rc=0、legacy rc=0** |
| profile JSON 的被测输入与清单一致 | ✅ 见 `profile-result.json` 与 `input-verify.txt` |
| ASan/UBSan 无错误 | ⚠️ **按审定文字记录**（rc=0、stderr 空；我未逐行搜 sanitizer 输出） |
| 新隔离树恰好两项变化 | ✅ **新增 0、消失 0、不同 2** |
| 编译前后输入零变化 | ✅ **0** |
| 内核实际新生成 | ✅ **`cac498da…`，15321160 B** |
| 显式 KERNEL judge 仅豁免既有 symbolset | ✅ **judge rc=0，源码错误 0** |

**full build 完成（内核已链接），所以可以报链接通过。**

---

## 7. 边界（照审定，我不新增解释）

### 可以说的

- **HOST MODEL 结果**：271 用例、字段归一化后 up=161/down=139 与冻结 oracle 逐项相同
- **BUILD 结果**：新树能编译并链接出内核，既有 symbolset 之外无错误

### 不能说的

| 不能说 | 依据 |
|---|---|
| hardware PASS | 全程主机 |
| 出声 PASS | 未播放、无设备 |
| 已校准 | trim 3/7、fine 1/2 是**合成参数** |
| 时钟真实振荡 | 模型不验证电气 |
| 实际模拟输出正确 | 同上 |

### 具体限制（照审定原文）

| # | 限制 |
|---|---|
| 1 | **trim 3/7、fine 1/2 只在主机模型；没有设备用这些值。我没有生成任何带这些值的 boot-args、镜像或设备命令。** |
| 2 | **VAUD18 必须已经开启，本实现不切换该 regulator。** |
| 3 | **gain31 不是电气静音保证。** |
| 4 | **参考时序匹配不证明物理振荡和实际模拟输出。** |
| 5 | oracle 的 DAPM 同排序键采用声明顺序，此假设与物理结果均未验证 |
| 6 | 持续总线故障下软件无法保证硬件关闭 |

### 历史状态不变

- **709 历史源链 `INSUFFICIENT`** —— 本轮不追认
- **705 / 707 的 FAIL** —— 不重判
- **本轮 host kernel 不是可刷的 trial710** —— 没有打包、没有镜像

---

## 8. 未触碰的东西

| 项 | 状态 |
|---|---|
| 旧 709 冻结树 | **未改**（kernel 仍 `c17cf881`） |
| 默认构建树 | **未改** |
| 共享 `hakios-kernel` | **未碰**（仍 `67f9f933`） |
| `sync-drivers` | **未调用** |
| GPUEB | **未引入** |
| wrapper | **未改**（仍 `95247bb0`） |
| `push hakios-mtk` | **未做** |
| 现有 BUILD 移动/删除 | **未做** |
| `reset`/`clean`/`gc`/`prune` | **未做** |
| 既有 WIP | **未覆盖** |

---

## 9. 原始证据位置（在 `hakios-mtk`，不入本仓）

`/home/lizi/IOS/hakios-mtk/logs/profile4-host-01-20261116-152501/`

| 文件 | 大小 |
|---|---|
| `profile.log` | 2580890 B |
| `legacy.log` | 177844 B |
| `build-stdout.log` | 124870 B |
| `build-stderr.log` | 76191 B |
| `build-combined.log` | 供 judge 用 |
| `base-manifest-check.txt` | 177583 B（5205 行逐项 OK） |

**新树**：`/home/lizi/IOS/hakios-mtk/logs/profile4-kernel-src-20261116-1525/`（含 BUILD）
