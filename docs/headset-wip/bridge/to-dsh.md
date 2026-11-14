
## [REPORT] 2026-11-15 00:24 — B2/B3/B4/B8 离线通过，B1 等设计
状态: open

- 已改：keys chan 上界；三份读事务在失败时仅对 FSM=6 补一次 VLDCLR 并返回错；Mali 写只等 IDLE、不做读 ACK；三份映射长度 0x8ff 且访问器只放行已确认 SWINF_2 寄存器；headset init 已移出 SID4 成功分支（B5）。
- 三份生产 C 的主机模型回归通过：`python3 mtk/tools/test-pmif-transactions.py`；ASan/UBSan 下覆盖遗留 ACK、ACK 丢失后的恢复、输出值有效性、Mali 写路径和越界拒绝。证据：`/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-transactions-test.log`。
- `tools/make-kernel.sh -j8` exit 0；`/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-independent-status.json` 保存内核 SHA256 与证据边界，构建原文见 pmif-independent-build.log。此版仍没有 B1，不作为刷机候选。
- 已读 XNU-LOCK-AVAILABILITY.md 的现有 hakios_spmi_lock 结论，未先行选择或实现锁，等 DSH 交付串行化设计。
- GPUEB 源码核对和生产 inline C 反例：`/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/gpueb-review/REVIEW.md`、counterexample-result.json；旧 RX 会被直接认作本次 ACK，冻结 now_ms 时无独立迭代上界，均仅为主机模型结果。集成应落实 stale-RX 前置、真实屏障/延时及超时后的歧义处理，不把 IRQ_CLR 当作取消 TX。
- 当前没有运行中的构建，未执行设备命令；手机已按用户交接归 Codex，首轮仍是 SID5 可读性与未插入基线，刷前另报产物和恢复方案。

---

## [REPORT] 2026-11-15 00:28 — B2/B3/B4/B8/B5 逐项复核摘要
状态: open

本轮没有改 B1 的锁，也没有新建锁；没有设备命令或刷机。下面行号对应当前文件及不可变复核快照 `/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-review/tree/`，SHA256 清单在 pmif-review/source-sha256.json。

| 项 | 当前改动 | 当前源码位置 |
|---|---|---|
| B2 | `chan >= PMIF_CHAN_COUNT` 或空输出指针，先返回 false，再做寄存器地址算术；恢复 helper 也检查 chan | keys.c:244、:246、:223 |
| B3 | keys / headset / mali 的 IDLE、读应答、读 ACK 后等待失败，检查 FSM；仅 FSM=6 时补一次 VLDCLR=1，并有界等 IDLE，然后仍返回失败；输出值只在完整读事务成功后发布；没有 SW_RST | keys.c:223、:262、:273、:284；headset.c:126；mali610.c:3057 |
| B4 | Mali 写 WDATA→ACC 后只等 IDLE，成功直接返回；不等 WFVLDCLR、不读 RDATA、不发读 ACK；读路径保留 WFVLDCLR→RDATA→ACK→IDLE | mali610.c:3072、:3089 |
| B8 | 三份 `PMIF_SIZE` 请求均改 0x8ff；同时给访问器加寄存器白名单，未知偏移读返回 0xffffffff、写不发 MMIO；Mali 原有 INIT_DONE(+0) 读取已移除；映射向整页取整仍不代表访问许可 | keys.c:93、:192、:204；headset.c:28、:89；mali610.c:3007、:3029、:3038 |
| B5 | headset init 移至 SID4 初次读的整个 if/else 之后，仍在同一 keys worker 内，不再被 SID4 成功条件门控 | keys.c:507、:508 |

复核命令（不碰设备）：

```sh
python3 /home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-review/tree/mtk/tools/test-pmif-transactions.py
```

生产 C + ASan/UBSan：keys、mali、headset 均通过；包括 chan=3、非法偏移、遗留 FSM=6、一次 ACK 丢失、失败时输出值不变、Mali 写不等待 RX 等反例；原文 `/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-transactions-test.log`。

`tools/make-kernel.sh -j8` exit 0，原文 `/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-independent-build.log`；wrapper 只接受既有 kext symbolset 失败，编译/链接无新错误。内核文件 `/home/lizi/IOS/hakios-mtk/logs/resume-sol-20261114-231152/pmif-independent-kernel.bin`，15286976 字节，SHA256 `bf926ae19a2782a63db987e5e3668363b48c67158460b65c22b41c6fb521b84b`。

边界：以上是离线代码和模型证据；B1 仍未修，B6/B7 待串行化设计处理；没有把此内核当作上机候选。

---
