# 团队分工（2026-11-15 20:55 起生效，下一轮执行）

> 用户观察：**DSH 交给 Codex 的数据多数会被找出错误打回**，
> 等于 Codex 要读一遍再推演校错，多花一整轮。
> 要求：分工明确、一步到位、1+1>2。

---

## 0. 依据：DSH 的错误有固定模式

本轮（11-15 16:00 后）DSH 写的 6 份文档里，被 Codex 找出并修正的错误：

| 文档 | 修正处数 |
|---|---|
| `MCU-IS-CORTEX-M.md` | 15 |
| `TRIAL-696-WITNESS-RESULT.md` | 12 |
| `ANCHOR-CRITERION-RETRACTED.md` | 9 |
| `MT6368-HP-SEQUENCE.md` | 8 |
| `UNIXNU-GPU-METHOD.md` | 7 |
| `RETRACT-735-BYTE-LOG.md` | 6 |

### 四类固定错误

| 类型 | 实例 |
|---|---|
| **二进制语义读错** | `regmap_update_bits(map, reg, mask, val)` 的 mask/value 顺序搞反，45 步表一半是错的 |
| **从「没有证据」推因果** | 735 字节 → 「XNU 早期就死了」（实际是 loader 的 `logbook_boot_note()` 预写） |
| **硬件块搞混** | `0x2508–0x2537` 当成 `AUDDEC`，实际是 `AUDENC` |
| **把清单项当缺口** | 「interconnect votes 缺」—— 主线 GPU 节点根本没有 `interconnects` |

**共同点：凡是要从二进制或硅片细节推出精确结论，DSH 不可靠。**

---

## 1. Codex 独占的领域

**这些任务由 Codex 一步做完，DSH 不先提取再交给它复核。**

| 类别 | 说明 |
|---|---|
| **二进制反汇编与语义提取** | 给出 `objdump` 命令和函数地址即可，Codex 自己提取 |
| **寄存器语义 / 位域 / 时序推断** | |
| **硬件块归属判断** | 哪个地址属于哪个 IP 块 |
| **驱动代码** | 生产代码 |
| **架构决策** | |

### 关键改变

| | 旧做法 | 新做法 |
|---|---|---|
| 反汇编任务 | DSH 提取 45 步表格 → Codex 逐条核对 → 发现 4 处错 | **DSH 只给 `objdump` 命令和函数地址** → Codex 自己提取 |
| 代价 | 多一整轮往返 | 一步到位 |

---

## 2. DSH 独占的领域

**这些是今天验证过可靠的部分。**

| 类别 | 今天的证据 |
|---|---|
| **广域信息搜集** | 找到 `MT6895-Mainline/linux`、`yukino-xnu` 组织、`panvk-mtk-driver` 三条有效线索 |
| **下载物核验** | 9 个主线源码文件全部 HTTP 200 + SPDX 头 + 关键符号校验 |
| **日志取证** | 提取、`sum` 校验、header 字段解析；发现 `reason` vs `sum` 的区别 |
| **实验打包** | 单变量保证、payload hash 链；7 轮零失误 |
| **设备操作与恢复** | 刷机/恢复/取日志 7 轮，零变砖 |
| **判据工具化** | `judge-701.py` 自检 5/5、`accdet-check.py` 自检 4/4 |
| **文档与仓库** | |

---

## 3. 交叉验证点（DSH 不能完全退出）

**DSH 换领域验证 Codex，不碰它的强项。**

| DSH 验证什么 | 为什么不验证别的 |
|---|---|
| **它引用的公开源码是否真的存在、是否那个版本** | 它可能凭记忆说某个上游文件有某个内容，这类错误 DSH 查得到 |
| **它说的日志行是否真在日志里** | 机械核对，DSH 可靠 |
| **它改的代码是否真编译过、产物 hash 对不对** | 可验证 |
| ~~二进制反汇编对不对~~ | **不做** —— 这是 DSH 的错误来源 |
| ~~寄存器语义~~ | **不做** |

**理由**：完全退出验证就没有人接住 Codex 的错误。但验证要换到 DSH 可靠的领域。

---

## 4. 仍然不变的规则

| 项 | 规则 |
|---|---|
| 文件归属 | `hakios-mtk/**` 只有 Codex 能改；`repo/hakios-mt6895/docs/**` 只有 DSH 能改 |
| 仓库 | `hakios-mtk` **永不 push** |
| 设备 | DSH 负责刷机/恢复；Codex 不碰设备 |
| 刷机规则 | 单变量、先报备、无论结果都恢复原厂并验证 md5 |
| 硬约束 | 黑屏后不长按电源、`fastboot oem expdb` 会杀 USB、不碰 Android `spmi_access` sysfs、不读 `0x13c40000` |
| 汇报风格 | 只报数字与证据边界；标「未验证」/「未证明」；观测与推断不混 |

---

## 5. 通信

| 方向 | 方式 |
|---|---|
| DSH → Codex | `orca terminal send --terminal <Codex句柄> --text "..." --enter` |
| Codex → DSH | `orca terminal send --terminal term_67e563b9-0361-4d66-ab3b-c0e072ae63b6 --text "..." --enter` |
| 前缀约定 | `[ASK]` 提问、`[DONE]` 完成、`[OBJ]` 反对意见、`[DSH]` / `[DSH→Codex]` 标识来源 |

**句柄会失效** —— 每次发送前若不确认，先 `orca terminal list` 查当前句柄。

---

## 6. 本轮例外

**`headset=3` 由 Codex 正在写，不打断。**
新分工从**下一轮任务**开始执行。

---

## 7. Codex 断线的处置（2026-11-15 21:35 实战验证）

**现象**：Codex 会话显示 `Reconnecting... 4/5 (52m 02s)`，
错误为 `Stream disconnected: Transport error: network error: error decoding response body`。
**这不是会话失效，是网络流断了。**

### 判断顺序

| # | 动作 | 判据 |
|---|---|---|
| 1 | 看当前节点 `curl --unix-socket $SOCK "http://localhost/proxies/%E8%8A%82%E7%82%B9%E9%80%89%E6%8B%A9"` | 看 `now` 是哪个 |
| 2 | 测真实端点 `curl -w "%{http_code}" https://chatgpt.com/backend-api/codex/responses` | **`405` = 通**，空/000 = 断 |
| 3 | 换节点 `PUT .../proxies/节点选择 {"name":"新加坡-优化-GPT"}` | — |
| 4 | 发一条 `ping` 给会话 | `orca terminal send ... --text 'ping' --enter` |

### 🔴 重要陷阱

**Clash 的延迟测试接口对这些 GPT 节点会报错或超时，但实际通道是通的。**

```bash
# 这个会失败，但不代表节点不能用
curl ".../proxies/台湾-优化2-GPT/delay?timeout=8000&url=https://chatgpt.com"
# -> {"message":"An error occurred in the delay test"}
```

**要判断通道，用第 2 步的端点测试（405），不要用延迟测试。**

### 可用的 GPT 节点（8 个）

`台湾-优化2-GPT`、`新加坡-优化-GPT`、`新加坡-优化2-GPT`、
`美国LA-优化-GPT`、`美国LA-优化2-GPT`、`美国LA-优化3-GPT`、`英国-优化-GPT`。

**2026-11-15 21:35 实测**：`台湾-优化2-GPT` 端点通但 Codex 流断；
换 `新加坡-优化-GPT` 后 Codex 立刻从 `Reconnecting` 回到 `Working`。

