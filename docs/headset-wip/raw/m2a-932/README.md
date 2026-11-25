# M2a：CSG 和 CS 起来了（boot-932b）

2026-11-26 凌晨。从「MCU 在跑」到「一个 group 上有一条流」，中间隔了七轮。

## 结果

```
22.556702  mali: m2: glb groups 8 stride 0xa0
22.556713  mali: m2: cs cfg 0x100 base 0x4060000 size 0x800 | csg ep_req 0x10606 cores 6 tiler 1
22.557753  mali: m2: csg ack 0x11 after 1 ms (ep_cur 0 ep_req 0 state 0)
22.557762  mali: m2: cs ack 0x40000001 after 0 ms (fault 0 fatal 0x40)
22.557765  mali: m2: group 0 stream 0 running, ring 0x4060000
```

- `csg ack 0x11` = `CSG_STATE_START | CSG_ENDPOINT_CONFIG`，组接受了启动和端点配置
- `cs ack 0x40000001`：`bits[2:0] = 1` 是 `CS_STATE_START`，**流起来了**
- `0x40000001` 的 bit30 是 `CS_FATAL`，`cs_out->fatal = 0x40`（bit6）

**流处于 START 状态，但固件报了一个 fatal 0x40。** 这是下一步要查的。

## 七轮里修掉的东西

每一轮都是一次刷机、一次读日志、一处修正。列在这里是因为其中五个是
「看起来会工作」而实际不会的写法。

### 1. 门铃是两级通知，不是一级

`mwr(0x80000, 1)` 只是把固件叫醒。它醒来后看 `GLB_DOORBELL_REQ` 和
`GLB_DOORBELL_ACK` 是否不同——不同才说明有请求。只敲门铃不翻转 `doorbell_req`，
固件判断「没有新事件」就回去睡了。

**931f 之前 CSG 一直是 `ack 0`，就是这个。**加上翻转后立刻变成 `0x11`。

### 2. `ring` 不能放在 `shared + 0x1000`

那里是 group 和 stream 的控制块所在页。第一版把一个 4 KB 的 ring 放在那里并
`memset` 成零，等于把即将读取的描述符全部清掉。

现在 ring 在 `shared + 0x60000`，iface 页在 `0x61000`，suspend 在 `0x62000`——
都在固件自己结构（止于 `0x54000`）之后、section 末尾（`0x94000`）之前的空闲区。

### 3. `delay(1)` 是 1 微秒，不是 1 毫秒

XNU 的 `delay()` 收微秒。轮询 100 次 `delay(1)` 只等了 0.1 ms，日志时间戳
（`22.160292 → 22.162192`）可以佐证。改成 `delay(1000)` 才是 100 ms。

另外共享页在 CPU 侧是 cacheable 的，轮询前要 `FlushPoC_DcacheRegion()`，
否则读到的可能是 cache 里的旧零。

### 4. `endpoint_req` 要的是数量，不是位图

`SHADER_PRESENT` 是 `0x130013`——六个 bit，每个代表一个 core。
`endpoint_req` 要的是数字六：

```
bits[7:0]   compute cores   = 6
bits[15:8]  fragment cores  = 6
bits[19:16] tiler units     = 1
                            = 0x10606
```

第一版把 `0x130013` 的低字节直接塞进去，得到 `0x13`，等于请求十九个 compute
core 且不要别的。**组接受了这个请求（ack 0x11），但端点计数始终是 0。**

### 5. 顺序是「先流，后组」，不是「先组，后流」

`panthor_sched.c:1441`：`cs_slot_prog_locked()` 是在 `csg_slot_prog_locked()`
**里面**被调用的。所以流的全部字段先填好，再填组的端点请求，然后一次门铃启动组，
组应答之后再敲门铃启动流。

### 6. `ringbuf_input` / `ringbuf_output` 不能是零

这两个字段指向固件用来记 ring 生产/消费位置的内存：

```c
struct panthor_fw_ringbuf_input_iface  { u64 insert; u64 extract; };
struct panthor_fw_ringbuf_output_iface { u64 extract; u32 active; };
```

填零等于指向 AS0 的 VA 0——那是固件自己的向量表。**固件解引用它去记位置，
记不了就不肯离开状态零。** 这一条是让 `cs ack` 从 `0` 变成 `0x40000001` 的最后一步。

### 7. 结构体偏移必须逐字段核对

`cs_control` 只有三个字段（`features / input_va / output_va`），不是五个。
第一版多抄了 `suspend_size`，于是把后面下一组的 `features` 读成了
「suspend 460895」——`0x7085f`，正是 `cs1.features`。

`cs_input` 也不一样：`config` 在 0x04，`ringbuf_base` 在 0x10，
`ringbuf_input/output` 在 0x30/0x38。


## boot-933：流真正起来了（2026-11-26 04:11）

```
22.328132  mali: m2: cs cfg 0x100 base 0x4060000 size 0x1000 | csg ep_req 0x10606 cores 6 tiler 1
22.333335  mali: m2: after one doorbell, 1 ms: csg ack 0x11 cs ack 0x1
22.337182  mali: m2: glb db req 0x1 ack 0x1 | csg db req 0x1 ack 0x1
22.341028  mali: m2: csg cs_irq_req 0x1 state 0 ep_cur 0 ep_req 0
22.344828  mali: m2: cs fault 0 fatal 0 cmd_ptr 0
22.348634  mali: m2: irq raw 0x200 job 0x80000001 mmu 0
22.352462  mali: m2: group 0 stream 0 running, ring 0x4060000
```

**一次门铃，1 毫秒，`cs ack 0x1`，`fault 0`，`fatal 0`。**

而且 CSG+CS 都在运行的状态下 `RETURN` 仍是 **32 秒**回 Android——
`stop()` 的 `GLB_REQ.HALT` 对已启动的组和流一样干净。

### 932b 的 `0x40000001` 是假成功

那一轮 `cs ack 0x40000001` 的 bit0 确实是 `CS_STATE_START`，但 bit30 是 `CS_FATAL`，
`cs_out->fatal` 也是 `0x40`。流在「已启动」这个状态上，但固件同时报了错。
把 bit0 当成成功就漏掉了这一点。

### 932b → 933 改的三处（Neon 抓出来的）

**① `ringbuf_input` 和 `ringbuf_output` 必须各占一个 4 KiB 页**

932b 里写的是 `user_in_va` 和 `user_in_va + 0x10`——同一页里的两个偏移。
寄存器定义的原话是 *"user mode input/output **page** address"*，
kbase 里也是 `start_pfn << 12` 和 `(start_pfn + 1) << 12`。

现在是 `0x61000` 和 `0x62000` 两个独立页。

**② `ringbuf_size` 必须是 4 KiB，不是 2 KiB**

932b 写的是 `MALI_CS_RING_SIZE / 2u`。kbase 按页粒计算队列大小，
ringbuf 至少一页且必须是 2 的幂。改成 `MALI_CS_RING_SIZE`（`0x1000`）。

**③ 只敲一次门铃，不是两次**

kbase 的 `program_csg_slot()` 不敲两次：流的请求先写、组的请求后写、
两个字各翻转一次、**一次** `DOORBELL(0)` 让固件一起看。

我原来在 CSG 应答后又敲了一次——而在这个 toggle 协议里，
翻转成「已经在那里的值」不构成请求，第二次敲门铃实际上是空的。

**顺带**：读 `csg_out->doorbell_ack` 和 `glb_out->doorbell_ack` 前都要
`FlushPoC_DcacheRegion`。932b 只 invalidate 了 `glb_out`，拿旧的
`csg_out->doorbell_ack` 去算 `^ 1u`，会得到和当前值相同的数。

### `ep_cur 0 ep_req 0 state 0` 不是异常

这三个寄存器只在主机主动翻转 `CSG_REQ_STATUS_UPDATE`（bit5）之后
固件才回填。`CSG_REQ_STATE_START` 不顺带更新它们。
所以它们一直是零并不说明组没起来——判据是 `csg_out->ack` 和 `cs_out->ack`。

## 现在的状态

| 层 | 状态 |
|---|---|
| M1：固件运行 | ✅ |
| M2a：一组一流，带 ring | ✅ |
| ring 内容 | 全零（还没有命令） |
| 退出基线 | ✅ 32 秒 |

## 下一步

`cs_out->fatal = 0x40`。流在 START 状态但固件报了致命位。

要看的是：
1. `0x40` 在这个固件的 fatal 编码里是什么（可能是 ring 里没有有效命令，
   固件取不到第一条指令）
2. ring 里现在是全零——需要写入真正的命令序列（NOP 或一条 compute）

## 镜像

`boot-932b.img`，boot-args `mali=6 mali_m2=1`。
`mali_m2=0`（或不传）完全跳过这一层，M1 不受影响。
