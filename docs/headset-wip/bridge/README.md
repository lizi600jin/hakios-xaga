# 直达通道：Codex ⇄ DSH 主控

> **Codex 不能主动向 DSH 发消息，但可以写文件。DSH 每 30–60 秒轮询本目录。**
> 这是双方唯一的异步对话通道。**用它代替"在心里想但说不出口"。**

---

## 1. Codex → DSH：写 `to-dsh.md`

**任何时候**有下面这些情况，就往 `docs/headset-wip/bridge/to-dsh.md` **追加**一段：

- 需要 DSH 派子代理查东西（`[FANOUT]`）
- 需要 DSH 做决策（`[DECIDE]`）
- 需要手机（`[DEVICE]`）
- 有阻塞（`[BLOCKED]`）
- 有反对意见（`[OBJECT]`，例如觉得 DSH 的结论错了）
- 阶段性结论（`[REPORT]`，DSH 会转进 findings/）

### 格式（严格照抄，否则解析不到）

```markdown
## [类型] YYYY-MM-DD HH:MM — 一句话标题
状态: open

正文，可以多行。带文件:行号。

---
```

`状态` 只有 `open` / `done` 两种。DSH 处理完会把 `open` 改成 `done` 并在下面追加回复。

### 类型说明

| 类型 | 用途 | DSH 的反应 |
|---|---|---|
| `[FANOUT]` | 要调研 | 派子代理，结果写 `findings/`，回贴路径 |
| `[DECIDE]` | 要拍板 | 给结论 + 理由，或反问 |
| `[DEVICE]` | 要手机 | 按 §4 交接仪式给 |
| `[BLOCKED]` | 卡住 | 优先处理 |
| `[OBJECT]` | 反对 | 复核，可能推翻自己 |
| `[REPORT]` | 阶段结论 | 转进 `findings/` |

---

## 2. DSH → Codex：写 `to-codex.md`

同样的格式，同样的类型。Codex 每个回合开始时读一次（**这是 Codex 的义务**）。

---

## 3. 已读位置标记

每方处理完对方文件后，在 `read-receipts.md` 追加一行：

```
codex read to-codex.md up to "## [DECIDE] 2026-11-14 23:50"
dsh   read to-dsh.md   up to "## [FANOUT] 2026-11-14 23:52"
```

避免重复处理。

---

## 4. 和轮询的关系

- DSH 侧：已启动后台轮询（见 `bridge-watch.sh`），发现 `to-dsh.md` 有新 `open` 就处理
- Codex 侧：**每个回合开始读一次 `to-codex.md`**，这是硬性要求，写进 `COLLAB-PROTOCOL.md`

---

## 5. 为什么不用别的方式

| 方式 | 为什么不行 |
|---|---|
| Codex 直接调用 `orca terminal send` 给我 | Codex 的沙箱里没有指向我终端的 handle，且会打断我的轮次 |
| 我实时读 Codex 终端输出 | 可以读，但只能读到 TUI 渲染结果，长文本会被折叠成 `+ N lines`，不可靠 |
| 共享内存 / socket | 双方进程生命周期不匹配，容易死锁 |

**文件信箱是唯一可靠方案**：不打断任何一方，有完整历史，可 diff，可 git 追踪。
