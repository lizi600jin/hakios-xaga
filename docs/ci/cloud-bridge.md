# 云电脑接入：这套仓库实际需要什么

本文是「云电脑合规接入与受控协作指南」在本仓库的落地版本：只写这个仓库里确实存在的文件、确实需要的信息，不重复通用原则。

仓库：`https://github.com/lizi600jin/hakios-xaga`

---

## 一、三条路，这个仓库选哪条

| 方案 | 适用场景 | 这个仓库的判断 |
|---|---|---|
| A 认证 Web IDE / Web 终端 | 需要实时看输出、手改代码 | 可行但要人工在场，联调期可用，长期跑测试不合适 |
| B1 GitHub Self-Hosted Runner | 无人值守跑 `check-wireless`、内核编译、QEMU 冒烟 | **推荐**。已有 `.github/workflows/wireless.yml`，零入站端口 |
| B2 Drive 目录 + 白名单执行器 | 云电脑在内网、完全不开 Web 入口 | **推荐备选**。已有 `tools/cloud-job-runner.sh`，同样零入站端口 |

A 与 B 不冲突：日常用 B1 跑回归，需要盯着看某一次失败时临时开 A。

---

## 二、B1：工作流已经写好，只剩注册 Runner

`.github/workflows/wireless.yml` 的两个触发条件都是**刻意收窄**的：

- `workflow_dispatch`：人手动选动作（`check_wireless` / `check_and_xnu` / `qemu_smoke` / `all`）；
- `push` 到 `ci/wireless-*` 分支。

**没有 push-to-main 触发**。理由写在工作流注释里，也值得在这里重复一次：这个工作流在既是构建机又是开发机的机器上执行仓库里的命令，用分支名决定它什么时候跑，等于给 CI runner 开了一个立足点。

云电脑侧只需两步：

1. 用**非 root** 用户注册 self-hosted runner（`Settings → Actions → Runners`），label 带 `linux`；
2. 内核编译需要的那台机器上设环境变量 `XNU_SRC`（指向内核源码目录）。**不设也不影响**：工作流会打一条 `::warning::` 并跳过内核步骤，其余照跑。

工作流本身不读 secrets、不打印环境、不执行 dispatch 输入之外的东西。

---

## 三、B2：Drive 目录协议（下边这套是强制的，不是建议）

```
<同步目录>/
├── inbox/<job-id>/          AI 写入
│   ├── job.json             只有两个键
│   └── <name>.patch         可选，apply_and_check 用
└── outbox/<job-id>/         云电脑写回（同一目录就地写，由同步工具带回来）
    ├── status.json
    └── build.log
```

`job.json` 只有两个键，两个都是字符串：

```json
{"action": "check_wireless"}
{"action": "apply_and_check", "patch": "fix-connsys-emi.patch"}
```

动作白名单（`tools/cloud-job-runner.sh` 里就是这四个，多一个都会被拒绝）：

| action | 做什么 | 超时 |
|---|---|---|
| `check_wireless` | `sh tools/check-wireless.sh` | 600 s |
| `apply_and_check` | `git apply --check` → `git apply` → 再跑检查 | 60 + 60 + 600 s |
| `build_xnu` | 到 `$XNU_SRC` 跑 `make -j BUILD_LOCK=1` | 1800 s |
| `qemu_smoke` | `sh tools/qemu-smoke.sh` | 600 s |

退出码语义（`status.json` 里的 `exit_code`）：

| 码 | 含义 | 对方该怎么处理 |
|---|---|---|
| 0 | 成功 | 取 `build.log` 的结论行 |
| 1 | 以 root 运行，被拒 | 修 runner 用户，不要重试 |
| 2 | 动作不在白名单 / 参数非法 / 路径逃逸 / 补丁是符号链接 | **不要重试**，改 `job.json` |
| 3 | 补丁打不上 | 重新生成补丁，不要在云电脑上手动改树 |
| 其他 | 检查或编译失败 | 看 `build.log`，按失败项改代码 |

**为什么不用 `eval` 也不解析任意命令字符串**：`job.json` 里只有动作名，其余参数按路径处理、必须落在 job 目录内（`inside_job()` 用 `pwd` 规范化后再做前缀比较，`../../etc/passwd` 这种会被拒）。补丁另有一条独立限制：**是符号链接就直接拒**（`-L`），因为 `-f` 对软链返回真，而前缀比较只规范化了父目录，指到外部的链接会绕过它。脚本内所有命令都是写死的 `timeout <秒> <程序> <参数...>`，从不交给 shell 二次解析。

**幂等与重试**：一个 `job-id` 只执行一次，这一点脚本里有硬判断——`status.json` 已存在就直接 `echo "already done: ..."` 并 `exit 0`，不读 `job.json`、不碰树。这条不是可选的礼貌：靠 watcher 轮询的队列，第二次执行会把已经打上的补丁再打一遍，`git apply` 失败报 `exit 3`，看上去像补丁坏了，实际只是同一单被执行了两次。要重跑就换新 `job-id`，不要去删旧的 `status.json`。`status.json` 里带 `head`（当时的 commit 短号），所以"这个结果对应哪个树"是可查的，不需要靠目录名猜。

**日志留存**：`build.log` 是完整 stdout+stderr，每条命令前有一行 `== 命令 ==`。保留期由同步工具定，仓库里不放日志。

---

## 四、QEMU 冒烟：它会诚实地 SKIP

`tools/qemu-smoke.sh` 只做三件事：

1. 找 `xaga-qemu` 的 checkout（参数、`QEMU_DIR`、或仓库旁边/`xaga-work/` 下）；
2. 跑 `-M help` 确认 `mt6895` 还在（这是这个 fork 唯一有价值的机器模型）；
3. 逐个查 `mt6895-consys` / `mt6895-dpmaif` / `mt6895-btif` 是否被建模——**没建模只报告，不算失败**，因为这是建模缺口不是构建失败。

只有在 `fw/LUA_1` 和 `data/lu2.img` 都在时才会真的 boot 15 秒；否则打印 `SKIPPED` 并 `exit 0`。仓库里没有这两份厂商 dump，所以**这个测试在大多数环境里都会 SKIP**，这是预期行为，不是配置错误。

顺带记一个上游坑：`xaga-qemu` 的 `hw/arm/trace-events` 用 `%llx` 打印 `uint64_t`，在 LP64 上必然 `-Werror=format`，configure 必须加 `--disable-werror`。

---

## 五、云电脑上线自检（对齐指南第三节，落到这个仓库）

- [ ] 非 root 专用用户，无免密 `sudo`，`id -u` 不为 0（`cloud-job-runner.sh` 自己也会拒绝 root）；
- [ ] 只授权 `hakios-xaga` 与 `xaga-qemu` 两个目录，工作区内无宿主机凭证；
- [ ] B1：runner label 含 `linux`，`XNU_SRC` 按机器情况决定设或不设；
- [ ] B2：同步目录只含 `inbox/` 与 `outbox/`，同步工具不做双向覆盖；
- [ ] 白名单与超时：不需要额外加固，`cloud-job-runner.sh` 已内置；
- [ ] 日志：`build.log` + `status.json` 每次都写，失败也写；
- [ ] 一键熔断：删 runner / 停同步 / 停服务，三条任一即可断开。

---

## 六、需要云电脑回报的三件事

1. **所选方案**：A / B1 / B2；
2. **入口**：B1 给 runner 名称与 label，B2 给同步目录名；A 给 HTTPS 地址（**不要带密码**，密码由用户在浏览器接管时自己输）；
3. **基线**：`hakios-xaga` 当前 commit（本文写作时的基线是 `e085b71`）、`sh tools/check-wireless.sh` 是否通过、`qemu-system-aarch64` 是否已构建。

这三条回来之后，下一个补丁可以直接按 B1/B2 走完整闭环，不需要再确认运行环境。
