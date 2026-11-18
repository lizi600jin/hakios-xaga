# 721 可变 LBA 读取：主机修正交付

> **历史限定**：本文记录的是 **721-D 主机阶段**的状态 —— 当时尚未执行任何 trial721 设备动作，
> 因此文中「没有硬件 PASS」只对那一刻成立。
> **实机验收已在其后单独完成**，见 [Trial721 实机验收结果](raw/device-ufs-varread-721/RESULT-TRIAL721.md)。

本轮完成 revision2 主机修正、独立复审和源码归档，没有执行 trial721 设备动作，也没有硬件 PASS。

实验目标仍为 LUN 2、每次 1 个 4096 字节 READ(10)，固定顺序 1 → 2 → 15 → 1、总预算 4，任何失败不重试。没有设备节点、块层或文件系统能力声明。

修复了 loader 返回审计前缀仍写 capacity 的问题；改用绑定真实完整日志与 kernel hash 的私有 judge，原生 make rc=2 保持原数；旧计划中的设备序列号已按单项例外脱敏。弱解析器与漏标的合成结果均被拒绝，最终合成报告真实返回 FORMAT_PASS_ONLY。

root 独立重放两次 clang 和一次 ld.lld，三个 rc 均为 0，产物与 DSH 逐字节相同。5227 项输入与原冻结清单一致；R20 的 5220 项输入原树未动，R21 相对它为改 3、增 7、删 0。codec、engine 未变，provider 仅有已批准的 US_HOST_TEST 观察变量限定差异，旧 support 与 R20 相同。新验包脚本实际通过 50 项检查，独立核对了完整 Android Image、padding、XNUP 与内嵌 kernel 身份。

| 本地冻结物 | 字节 | SHA256 |
|---|---:|---|
| kernel（复用原 721-D） | 15339856 | 784500d791bf2a623d24ad81ed460b4c0cdf25143f8beade8a2d4729eed342cb |
| boot.img | 23003136 | 498a99a40094c1f941ce598e061b1eb599690acf531eca25e402b270acf6472e |
| loader.bin | 48152 | 04b36c195f25781ca9265a5ee0ecf56f1706d6f11524619fb61b5f35a0fd9b31 |
| payload.bin | 19534312 | 9d8880fe1b134dda8412568047ba1816535b92f61e2fa8783eaf10ac05e747b7 |

[源码、完整输入清单与独立审查](raw/final-ufs-varread-source-721/README.md)归档在专用目录，镜像与原始私密数据仅留本地。构建路径、工具和每阶段 rc 均有记录，旧无效 judge0 没有被当作有效证明。

设备方案保留为未执行草案：loader 归还控制并不会恢复 boot_a，成功与否都需另做原厂恢复。用户要求本轮主机交付后收尾，root 与 DSH 不再推进设备阶段。CRC 相等仅表示校验和一致；没有 wire trace、缓存排除、物理 DMA 新鲜度、性能或块层结论。

**以上为 721-D 主机阶段收尾时的状态。** 设备阶段随后在单独授权下执行完毕：
两次独立刷写（实验镜像与原厂恢复各一次）、四次 READ(10) 完成、原厂恢复经验证。
实际发生的过程、哈希、各步 rc 与过程偏差见
[Trial721 实机验收结果](raw/device-ufs-varread-721/RESULT-TRIAL721.md)。
