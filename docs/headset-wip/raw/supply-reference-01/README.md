# HOST-SUPPLY-REFERENCE-01：供电参考封存

> 2026-11-16 14:23，DSH。**仅主机：读取、离线模拟、docs 新增。
> 未追加构建/镜像/播放/刷机，未触碰设备，未改生产驱动，未 push `hakios-mtk`。**

---

## 0. 一句话

**审定文字已原样归档；参考脚本从原路径重跑 `rc=0`，
两个 profile 的 widget 闭包与 up/down 操作数与审定完全一致；九份参考文件已封存并逐字节校验。**

**这仍是软件参考，不是 hardware PASS，也没有任何供电/SGEN 生产补丁。**

---

## 1. 审定文字（原样归档）

| 项 | 值 |
|---|---|
| 文件 | **`OBSERVE02-DECISIONS.md`** |
| 来源 | `hakios-mtk/logs/headset-chain-20261116/OBSERVE02-DECISIONS.md` |
| 字节 | **8139** |
| SHA256 | **`a1e2103906230c8a658cba02dd41396a8d6908159452b2474b0d60d7c294ad33`** |
| 逐字节一致 | **`cmp` 通过（IDENTICAL）** |

**它包含三个问题的审定结论、播放证据重算、供电回调参考、下一实现阶段约束、
参考构建与工具修复。**

### 链接的旧审定文件

| 文件 | 内容 |
|---|---|
| `../verify-wrapper-710/README.md` | wrapper 修复复核与参考构建差异修正 |
| `../dapm-observe-02/README.md` | idle→播放→idle 对照原始记录 |
| `../prep-710-host/BASELINE-INPUTS.md` | 709 源链 INSUFFICIENT 的记录 |
| `../findings/DAPM-CORRECTIONS-AUDITED.md` | 六项审定修正文案 |

### 历史状态不变

- **trial 705 / 707 的 FAIL 保持不变**，不事后重判
- **709 历史源链仍为 `INSUFFICIENT`**，不追认

---

## 2. 参考脚本重跑（从原路径）

| 项 | 值 |
|---|---|
| 命令 | **`python3 logs/headset-chain-20261116/trace-supplies.py`** |
| cwd | `/home/lizi/IOS/hakios-mtk` |
| **真实退出码** | **`0`** |
| stderr | 空 |
| Python | **3.14.4**（`/usr/bin/python3`） |
| Unicorn | **2.1.4** |
| 脚本 SHA256 | **`c38d130114a6ad178021c6f2789e889b2e9859ffc9ceb889d14c5a4bc66fb30b`** |

**它只离线模拟本地 `.ko`，不访问设备。**

### stdout 原文

```
normal_aif1_hp {'external_regulator': 2, 'external_dcxo': 2, 'update': 157, 'read': 1, 'write': 55, 'delay': 90} module callbacks and modeled scheduling completed
internal_sgen_hp {'external_regulator': 2, 'external_dcxo': 2, 'update': 163, 'write': 63, 'delay': 90} module callbacks and modeled scheduling completed
```

### 核对结果：五项数字全部吻合

| profile | widget 闭包 | up | down | 合计 |
|---|---|---|---|---|
| **`normal_aif1_hp`** | **39** | **165** | **142** | **307** |
| **`internal_sgen_hp`** | **44** | **173** | **147** | **320** |
| **审定声称** | **39 / 44** | **165 / 173** | **142 / 147** | — |

**两个侧面互相印证：**

1. `stdout` 的分项求和：`normal` 2+2+157+1+55+90 = **307**，
   `sgen` 2+2+163+63+90 = **320** —— 与 up+down 之和一致
2. `supply-callback-trace.json` 里按 `phase` 字段拆分：
   `normal` up=165 / down=142，`sgen` up=173 / down=147

**`calls` 数组长度就是 307 / 320，按 `phase` 分为 `up` / `down` 两组。**

### 🔴 操作数的含义（按审定，不作外推）

**操作数包含 callback 的读/写/update/delay 和外部调用边界；
不是硬件总线事务数。**

---

## 3. 封存的九份参考文件

**全部逐字节校验通过（`cmp` IDENTICAL）。**

| 文件 | 字节 | SHA256 |
|---|---|---|
| `trace-supplies.py` | **10786** | `c38d130114a6ad178021c6f2789e889b2e9859ffc9ceb889d14c5a4bc66fb30b` |
| `supply-callback-trace.json` | **169815** | `245c8a007304c628e2febeb69e2b8e0d70629eea505bc20451d1d446a6c89954` |
| `SUPPLY-CALLBACK-TRACE.md` | **39647** | `3c50632a769b79fa04003ff03fc2816e37770023ce028c414f4bf1cb2c0a0446` |
| `codec-dapm.json` | **108047** | `0800fb4d756d4c8f14908a48ba89aac32cb0ddd881a57d9b397004f9c4d8d665` |
| `extract-dapm.py` | **6852** | `fb9e60700830675d3e5f2838ee9bd27fce0090f46cdc4aa6b1a15ec32e542504` |
| `observe02-review.json` | **4300** | `60731b33a460facb5a076a174eb937387af1e5826e845fd286835227a2dd689f` |
| `machine-adda-identity.json` | **797** | `077cb0099ce102ea6b49f39d5fc8f9f4d45c60f955dc4e0fa8d3883ed48ccd76` |
| `compare-reference-kernels.py` | **5265** | `886b4dffeffb3d22342bccefffaad63287b3232ab893f714be025c167cb98f63` |
| `reference-kernel-diff.json` | **5001** | `4d4543b2e641aa57c2880b3ed71c39e9b54d17646ce190fa2a8c1bdfb35edff1` |

### 原路径

**全部来自**：`/home/lizi/IOS/hakios-mtk/logs/headset-chain-20261116/`

### 所用固定源码 commit

**`0a8d13c1a180fed82803f56337a0b065ff704fb4`**
（`XagaForge/android_kernel_xiaomi_mt6895`）

**trace 记录的 DAPM 核心源码**：
`logs/headset-dapm-review-20261115/sound__soc__soc-dapm.c`，
SHA256 **`4347e3150fc1ceb1065bd4f3607c692f14a9e3f8852ea42611d0fe533ac1c7d2`**

### 所用 ko 身份

| 项 | 值 |
|---|---|
| 路径 | `docs/headset-wip/ko/snd-soc-mt6368.ko` |
| **SHA256** | **`4ce5cab06d3d29aa3e2ccfbd8044d68d691b27f3b4e336332bfb41ee6e479475`** |

**这与 `snd-soc-mt6368.ko` 在设备 `/vendor/lib/modules` 的实测 hash 一致**
（见 `../dapm-preflight-20261115/` 与 `hakios-mtk/logs/headset-dapm-review-20261115/EVIDENCE-709A-SOURCES.md`）。

---

## 4. 🔴 重跑入口与可移植性边界

### 原始脚本有本机路径依赖

**`trace-supplies.py` 里写死了本机绝对路径**（ko 与 soc-dapm.c 的位置）。
**我没有改脚本去适配路径。**

**重跑入口（需在原机、原目录布局下）：**

```bash
cd /home/lizi/IOS/hakios-mtk
python3 logs/headset-chain-20261116/trace-supplies.py
```

**必备前置：**

| 依赖 | 路径 |
|---|---|
| MT6368 模块 | `/home/lizi/IOS/repo/hakios-mt6895/docs/headset-wip/ko/snd-soc-mt6368.ko` |
| DAPM 核心源码 | `/home/lizi/IOS/hakios-mtk/logs/headset-dapm-review-20261115/sound__soc__soc-dapm.c` |
| Python | 3.x（本次 3.14.4） |
| Unicorn | 2.x（本次 2.1.4） |

### 🚫 不宣称"拷走即为可移植包"

**这些文件拷到本目录后能读、能核对，但 `trace-supplies.py` 直接运行仍依赖上述绝对路径。**

**我没有改脚本去消除这个依赖** —— 按指令，改原脚本适配路径不在本任务范围。

---

## 5. 包内清单：参数哨兵与模型边界（照审定文案）

> **以下四条直接引用审定文字，我不替它推导。**

### 5.1 合成 trim 哨兵只用于参数轨迹

**trim 使用人工哨兵 `3/7`、`fine 1/2`，只验证动态参数流向，绝不能用于设备。**

`profile_assumptions.trim_sentinels`：

```json
{"left": 3, "right": 7, "fine_left": 1, "fine_right": 2}
```

**真实 trim 未取到时不得使用哨兵或宣称校准完成。**

### 5.2 regulator / DCXO 是外部模型边界

**外部 `regmap`/`delay`/`logging`/`regulator`/`DCXO` 均有明确模型边界。**

**参考尚未覆盖真实稳压器动作、MT6685 总线写。**

### 5.3 DAC 源选择 `238e` bit0 是前置条件，control put 未执行

**`DAC In Mux` 的控制是 `238e` bit0，Normal Path=0、Sgen=1。**

**轨迹将选择状态列作前置条件，没有执行 control put；**
**以后驱动必须对它单独快照、选择、恢复，不能漏掉后宣称 SGEN 已生效。**

### 5.4 SGEN 两个 widget 共享 `23c8`，PRE 回调后需合并 `mask=c0, val=80`

**SGEN DL Enable 与 SGEN MUTE 共享 `23c8`，其 `PRE_PMU` 回调会先将该 byte 清零；
DAPM 然后一次性以 `mask=c0`、`value=80` 写回，保留 `DAC_EN=1`、`MUTE=0`。**

**若先写 enable，再单独调用会清零整 byte 的回调，最后只清 mute 位，就会漏掉 enable。
单独照抄宏列表会造成可复现的软件遗漏。**

### 5.5 共同假设

`hp_mux=2`、`hifi=0`、`rate=48000`、`gain=31`（静音参考）。

**对 gain 寄存器建模为已观测到的低五位读回，未把全部 byte 当可持久保存的 RAM。**

---

## 6. 判据核对

| 判据 | 结果 |
|---|---|
| 原脚本 `rc=0` | ✅ **0** |
| 两 profile 统计符合审定数字 | ✅ **39/165/142**、**44/173/147** |
| 归档文件与原文件 hash 逐项一致 | ✅ **九份全部 `cmp` IDENTICAL** |
| 来源与限制完整 | ✅ 本节 §3/§4/§5 |
| wrapper 7 项通过的证据保留 | ✅ 见 `../verify-wrapper-710/README.md`（**未重跑真实构建**） |

---

## 7. 边界（严格按审定）

| 不做 | 说明 |
|---|---|
| 不把人工 trim 参数当设备实值 | 哨兵 3/7、fine 1/2 只验证参数流向 |
| 不把 RAM 读回模型当设备实值 | gain 只建模低五位读回 |
| 不为 `AIF_RX` 的 inactive 安排额外播放 | **已由 core 对象语义解释** |
| 不从 `DC Trim` 四点 Off 得出未校准结论 | 四次采样不覆盖瞬态，也不反映异步线程 |
| 不通过逐 bit 关钟做必要性筛查 | **完整 vendor 依赖先保留** |
| 不扩大 wrapper 改动 | 共享 `hakios-kernel` 出口语义保留；参考构建继续直调 `xnu-build` + 显式 `KERNEL --judge` |

### 没有的东西

- **没有供电/SGEN 生产驱动补丁**
- **没有 hardware PASS**
- **710 未打包、不可刷**

---

## 8. 本任务的原始证据（在 `hakios-mtk`，不在本仓）

`/home/lizi/IOS/hakios-mtk/logs/host-supply-reference-01-20261116-142214/`

| 文件 | 内容 |
|---|---|
| `env.txt` | Python 3.14.4、unicorn 2.1.4、脚本 hash |
| `trace-stdout.txt` | 原始 stdout |
| `trace-stderr.txt` | 空 |
| `trace-rc.txt` | 真实退出码 0 |
| `profile-verify.txt` | 五项数字核对（widget/up/down） |
| `archive-manifest.txt` | 十份封存件的完整 size + SHA256 |
