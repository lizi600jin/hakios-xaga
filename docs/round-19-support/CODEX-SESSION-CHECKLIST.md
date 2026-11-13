# 唤起新 codex 会话时的必做清单

**2026-11-13 用户指出**：每次唤起新 codex 会话，有一步需要**确认信任目录**，否则会话无法进行。
这一步不做，codex 会卡在 "Do you trust the files in this folder?" 之类的提示上，看起来像没响应。

---

## 1. 完整顺序（缺一步就会白等）

```
1. 换号（如果需要）
   codex-failover            # 额度耗尽时才用；--dry-run 会说"无需动作"但实际需要真跑
   rm -f ~/.local/state/codex-resume.last   # 清去重标记，否则 codex-resume 会 SKIP

2. 唤起会话
   codex-resume              # 创建新终端，并让它先读规程+交接，先回三样再动手

3. ★ 确认信任（用户 2026-11-13 点名的那一步）
   新终端里会出现信任提示。必须确认它，会话才会真正开始。
   —— 不做这一步，终端看起来在运行，但 agent 不会接受任何输入。

4. 验证会话真的活了
   orca terminal read --terminal <handle> | tail -20
   看它是否已经开始读文件、或是否仍在等信任确认

5. 发第一条实质消息
   orca terminal send --terminal <handle> --text "$(cat /tmp/msg.txt)" --enter --wait-submit 20
   ★ 注意：不加 --enter 只写到 PTY，不会提交；--wait-submit 要配 --text + --enter 一起用
```

## 2. 踩过的坑（都真实发生过）

| 坑 | 现象 | 处置 |
|---|---|---|
| **没确认信任** | 终端"running"但 agent 无响应 | ★ 必须先确认信任 |
| 消息含嵌套英文双引号 | 报 `prompt: 未绑定的变量`，甚至执行了 `X` 命令 | 用中文引号「」，或把消息写进文件再 `$(cat file)` |
| `send` 没加 `--enter` | 只显示 `input_accepted`，没有 `turn_started` | 加 `--enter`，并配 `--wait-submit` |
| `--wait-submit` 单独用 | 报 `requires --text with --enter` | 三个一起用 |
| `codex-failover --dry-run` | 明明 100% 却说"无需动作" | 跑真的 `codex-failover` |
| 忘了清 `codex-resume.last` | `codex-resume` 直接 SKIP | `rm -f ~/.local/state/codex-resume.last` |
| **额度查询报 SSL 错误** | `usable: 0`，被误判成"额度耗尽" | ★ 先测代理：`curl -sS -o /dev/null -w "%{http_code}" https://chatgpt.com`；200 才可信。代理恢复后重查即正常 |

## 3. 判断"额度耗尽"的真伪

`codex-guard` 或 `codex-quota` 说耗尽时，**先分清是额度问题还是网络问题**：

```sh
# 1) 代理通不通
curl -sS -o /dev/null -w "%{http_code}\n" https://chatgpt.com     # 期望 200

# 2) 通了再查额度
codex-quota
```

- 输出里有 `URLError: <urlopen error [SSL: UNEXPECTED_EOF...]` → **网络故障，不是额度**
- 输出里 `usable: 2` → 两个账号都能用
- 只有 HTTP 200 且 `usable: 0` 才是真的耗尽

**2026-11-13 实例**：代理 TLS 断流导致 `usable: 0`，`[codex-guard]` 报"两个账号都耗尽"。
实际是误报。代理恢复后立刻显示 `usable: 2`（system 1%、orca:2a3f54a2 85%）。

## 4. 新会话开工前必须读的三份

发给它的第一条消息里要写清顺序：

1. `COLLAB-PROTOCOL.md` —— 协作规程，**附录 A/B/C/D 是 2026-11-13 新增的五类方法性错误条款**
2. `HANDOFF-TO-CODEX-<最新>.md` —— 交接
3. 要求它**先回三样**（同意/有保留/反对 + 理由带证据 + 优化建议）**再动手**

**允许它直接驳回，不用先跑一遍**——烧一轮刷机（约 15 分钟 + 恢复）的成本高于一次讨论。
