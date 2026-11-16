# final-source-20261116：最终源码测试包与复核索引

> 2026-11-16 20:25，DSH。**这是交付给其他开发者复现本方案的入口。**

---

## 1. 这个包是什么

**9 个源码/测试文件的完整快照**，从这份快照**可以直接跑通两个模型**，
不需要构建内核、不需要设备、不需要任何其他依赖。

| 项 | 值 |
|---|---|
| 来源 | Codex 准备的 `P/logs/profile4-low-output-20261116/delivery-source/` |
| 归档 | **原样复制，未改一个字节** |
| 文件数 | **12**（9 个 manifest 项 + MANIFEST.json + MANIFEST.sha256 + README.md） |
| **manifest 校验** | **复制前 9/9 OK、复制后 9/9 OK** |

---

## 2. 独立复跑结果（从归档布局）

**我在归档目录 `source/` 里直接跑，不是在生产树里跑：**

```
cd docs/headset-wip/raw/final-source-20261116/source
python3 mtk/tools/test-headset-profile.py --log-file … --result-file …
python3 mtk/tools/test-headset.py --log-file …
```

| 项 | 结果 |
|---|---|
| profile native rc | **0** |
| profile | **PASS 304 cases；up bus writes=113；close+restore=114** |
| profile trace | **up=161、down=139** |
| legacy native rc | **0** |
| legacy | **全部子项通过**（mode1/2/3、vendor trace、5-bit gain、retries、shared-field、repeat、rollback、worker stop、post-stop） |
| ASan/UBSan | **0 命中** |

**与 Codex 从同一包跑出的结果完全一致。**

---

## 3. 9 个文件是什么

| 文件 | 大小 | 作用 |
|---|---|---|
| `mtk/mt6895/drivers/audio/hakios.mt6895_headset.c` | 71087 B | **主驱动**：accdet + profile4 + gain31 tone |
| `xnu/files/bsd/sys/hakios.pmif.h` | 3356 B | PMIF 接口头 |
| `mtk/mt6895/drivers/core/hakios.mt6895_pmif.c` | 8432 B | **PMIF 核心**（测试需要它才能独立跑） |
| `mtk/tools/test-headset-profile.py` | 7783 B | 304 场景模型驱动器 |
| `mtk/tools/test-headset.py` | 21122 B | legacy 回归驱动器 |
| `mtk/tools/fixtures/mt6368-profile4-prefix.c` | 6053 B | 模型前置桩（可移植） |
| `mtk/tools/fixtures/mt6368-profile4-suffix.c` | 15690 B | 模型测试用例 |
| `mtk/tools/fixtures/mt6368-profile4-vendor.json` | 70412 B | vendor HP 指令序列参考 |
| `mtk/tools/fixtures/mt6368-hp-normal.json` | 14273 B | 常规 HP 路径参考 |

**后缀文件 `suffix.c` 里含本轮新增的 gain31 覆盖**：
四个非法值 `{0,29,32,UINT32_MAX}` 各断言无 PMIF 访问、
gain31 全程 pattern、七个新 gate 写逐点注入失败。

---

## 4. Codex 独立复核记录（`codex-review/`）

| 文件 | 内容 |
|---|---|
| `independent-kernel-review.json` | 内核算查 |
| `independent-package-review.json` | 打包结构、598 字节参数串 |
| `independent-device-review.json` | 设备试验复核 |
| `independent-archive-review.json` | 归档复核 |
| `user-acoustic-observation.json` | **用户听音证据与边界** |
| `model-result.json`、`delivery-profile-result.json` | 模型结果 |
| `gain31-712.patch` | **gain31 改动的完整 patch** |
| `REVIEW-AND-PLAN-712.md` | 712 的复核与设计意图 |
| `delivery-source-README.md` | 原始包说明 |
| **`AUDIO712-FINAL.md`** | **712 新录音的最终分析结论**（87–95 s 连续变调、谐波族、三条边界） |
| **`audio712-identity.json`** | 712 录音文件身份与解码参数 |
| **`audio712-independent-fft.json`** | 逐声道 Hann 16384 窗 FFT 结果 |
| **`audio712-spectrum-84-98.png`** | **84–98 s 频谱图**（独立解码的片段） |
| `audio712-envelope.json` | 包络数据 |

**另有**：[`ANDROID-PLAYBACK-CONTROL.md`](ANDROID-PLAYBACK-CONTROL.md) ——
**Android 播放链对照实验**（2026-11-16 20:24）：证明 Android 该路径能放出人耳可闻的声音。

> 🔴 **归因已收窄**：本次条件（音量 4/15、耳机路由、手机位置）与 711 录音时**未完全匹配**，
> 所以**不能断言「711 缺参考音属录音链问题」**，也不能据此确认 711 当时是否录到。
> 本次排除的是「Android 一贯不发声」，不是定位了 711 的成因。

**🔴 原始 MP3 不上传** —— 它在 `/home/lizi/下载/标准录音+6.mp3`
（4368212 B / `4a432907…`），保留在本地以避免发布环境录音。

---

## 5. 两处已修正的过度推断

**CMD 明确指出并已在本仓库修正：**

### 5.1 `Standby: no = 0` 不等于全程无 Android 声音

**原写法（已改）**：~~「✅ 无混入」~~
**现写法**：**「这是刷写前的基线快照；只证明该次采样时刻没有活动音频线程，
不能证明整个录音期间持续没有 Android 媒体声音。」**

### 5.2 `+132.5 s` 的计时起点未证实

**原写法（已改）**：~~直接当作 adb 恢复耗时~~
**现写法**：**「轮询间隔测量值，计时起点未证实与实际 reboot 时刻同一；
不能用它推出『XNU 返回变快了』或其他跨轮次时间比较。」**

**修正位置**：`docs/headset-wip/raw/device-712/README.md` 第 30 行与第 73 行附近，
原文保留，追加更正块。

---

## 6. 复现方式

```bash
cd docs/headset-wip/raw/final-source-20261116/source

# 校验完整性
sha256sum -c MANIFEST.sha256

# 跑 304 场景模型
python3 mtk/tools/test-headset-profile.py --log-file /tmp/p.log --result-file /tmp/p.json

# 跑 legacy 回归
python3 mtk/tools/test-headset.py --log-file /tmp/l.log
```

**两个命令的 native rc 都应为 0。**
**不需要内核、不需要设备、不需要 root。**

---

## 6.1 712 录音分析要点（`AUDIO712-FINAL.md`）

| 项 | 内容 |
|---|---|
| 显著片段 | 约 **87–95 s**，较弱连续变调组 |
| 谐波族 | **约 1.567 kHz** → **2.063 kHz** → **2.783 kHz** |
| 声道 | 右声道更强 |
| 静音 gap | **同样没有清晰的 1 秒重复分离** |

**三条不能越过的边界：**

1. **粗 RMS 没选中该段不是静音证据** —— 只是它低于噪声相对阈值
2. **两份录音不能测 gain30 vs 31 的衰减量** —— 麦克风几何变了、AGC 未控制
3. **全文件频谱有 MP3 解码警告** —— 84–98 s 片段是独立解码的（rc=0、stderr 空、672000 帧），
   但不能把整个压缩文件当作干净母版

---

## 7. 边界

**这个包能验证**：驱动的寄存器序列逻辑、状态机、时序、错误处理、清理路径。

**这个包不能验证**：真实硬件的电气行为、声学输出、时钟频率、SPL、DC offset。

**采样率与位深**：**用户明确留待以后，不在本轮范围。**

---

## 8. 相关入口

| 想去哪 | 看什么 |
|---|---|
| 今天做了什么 | [`../DELIVERY-20261116.md`](../DELIVERY-20261116.md) |
| 最新设备试验 | [`../device-712/README.md`](../device-712/README.md) |
| 录音分析 | [`../acoustic-source-712/README.md`](../acoustic-source-712/README.md) |
| gain31 代码审查 | [`../verify-build-pack-712/README.md`](../verify-build-pack-712/README.md) |
| 增益码与 SGEN 字段 | [`../output-prep-711/README.md`](../output-prep-711/README.md) |
