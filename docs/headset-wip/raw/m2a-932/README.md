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

## 下一步

`cs_out->fatal = 0x40`。流在 START 状态但固件报了致命位。

要看的是：
1. `0x40` 在这个固件的 fatal 编码里是什么（可能是 ring 里没有有效命令，
   固件取不到第一条指令）
2. ring 里现在是全零——需要写入真正的命令序列（NOP 或一条 compute）

## 镜像

`boot-932b.img`，boot-args `mali=6 mali_m2=1`。
`mali_m2=0`（或不传）完全跳过这一层，M1 不受影响。
