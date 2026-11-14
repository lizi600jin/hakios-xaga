panthor 的第二个独立握手信号已找到：共享内存 output->ack 对主机 req 的位翻转作出应答，GLB_PING 是最小的一种。

1. 初始启动顺序是 panthor_fw_start → panthor_fw_init_ifaces → panthor_fw_init_global_iface（/tmp/gpueb/ml/drivers/gpu/drm/panthor/panthor_fw.c:1545、1549、1553）。init_ifaces 在启动成功后读取 version / input_va / output_va / group_num，并逐个验证 CSG/CS 接口；这不是在 GLOBAL_IF 之前等 group_init 的协议，也没有额外的 GLB magic 等待。
2. GLB_PING 的请求位为 bit8（panthor_fw.h:214），panthor_fw_ping_work 在 fw.c:1478 把 req 对应位翻成 ack 的反值，敲全局门铃，在 :1481 等共享区 ack，100ms 上限。wait_acks 在 :1357-1378 用 READ_ONCE 轮询比较 (ack & mask)==req，最后还直接检查一次，因此成功不以收到中断为必要条件。若 CPU 只改 input、不改 output，且排除陈旧缓存/旧应答，一次真实的 ack 翻转能证明固件处理过共享区请求；比单读 ENABLED 强，也比单读一个可能预置的字段强。
3. GLB_STATE 只在接口版本 >=4.1.0 才支持（fw.c:328-332），ACTIVE 编码是0，不能把初始0当固件写回的证据；也不能对当前未发布有效 version 的接口直接套用。
4. CSG 的 START/RESUME 应答也能证明固件读过请求，但在 scheduler，而非早期 fw_start：panthor_sched.c:2394-2398 配置槽并请求 START/RESUME；:1986-2001 写 req、敲组门铃、等 output->ack，100ms；:1370-1374 只有收到 START/RESUME ack 才把组标为 ACTIVE。它依赖已发布的 CSG/CS 地址、VM、挂起区和 endpoint 配置，不适合拿来救当前尚无 version 的首轮启动。
5. 我们已经有 config+ping 应答测试（hakios.mali610.c:4206-4288），req 由 output->ack 反相生成，等共享区 ack。当前失败停在 start_mcu，没走到这个函数；所以“再加一次 ping”本身不补出可用早期信号。不能追随哨兵/0 值 input_va/output_va 写内存。

GPU 下一步建议仍由主控整合取指审计：先把 CPU reset-PC=(0x010018ed & ~1)=0x010018ec 的内容、该精确VA的软件PTE、AS0实际寄存器、JOB RAW/MASK/STATUS 的证据补齐。现有 entry words 其实前两项是 vec[2]/vec[3]，后两项是 code section 基址0x01000000，并非 reset-PC 的指令。真 trial692/NOPDC日志为 reset vector SP=0x02040000 PC=0x010018ed，四项=0x010019b1 0x010019b1 0xd00cf8df 0xf832f002，只证明CPU侧缓冲区/软件构造，不证明MCU已能按AS0取指。

另：F3C终态0x3ffc、shader0可符合启动前状态、fault0、counter递增，都支持降低重复上电实验的优先级；它们仍不足以逻辑排除MCU子块门控、可见性或取指路径问题，不把“剩下唯一原因”定案。本轮不改变GPU启动行为、不加入GPUEB或门铃实验。
