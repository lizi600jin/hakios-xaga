# M2b：GPU 执行了第一条指令（boot-935）

2026-11-26 凌晨。GPU 第一次真正取指、执行、把结果写回 DRAM。

## 结果

```
22.246475  mali: m2: after one doorbell, 1 ms: csg ack 0x11 cs ack 0x1
22.246492  mali: m2: group 0 stream 0 running, ring 0x4060000
22.247627  mali: m2b: step A: extract 64 (want 64) active 1 after 1 ms
22.247635  mali: m2b: before step B: sync 0x1000000000000000 cycles 0
22.248768  mali: m2b: step B: extract 128 (want 128) active 1 after 1 ms
22.248772  mali: m2b: step B: sync 0x10000000cafebabe (want 0x10000000cafebabe)
                       cycles 0x2c1b975
22.248783  mali: m2b: PASS -- the GPU executed instructions and wrote memory
```

`sync` 从 `0x1000000000000000` 变成 `0x10000000cafebabe`，
`cycles` 从 0 变成 `0x2c1b975`——**这两个地址在 DRAM 里，是 GPU 自己写的。**

两处写入来自两条不同的指令：

- `STORE_STATE.cycles` 把 GPU 的硬件周期计数器写进 `cycles`
- `SYNC_ADD64` 把 `0xCAFEBABE` 加到 `sync`

`RETURN` 仍是 **34 秒**（M2a 时是 32 秒，多出的两秒是这一轮真的执行了工作）。

## 分两步验证的理由

**Step A 是八个 NOP。** NOP 里没有可能出错的东西，所以只要 `extract` 从 0 走到 64，
就说明取指路径本身是通的——prefetch、解码、extract 推进，全都在工作。

**Step B 才是真指令。** 到这一步才有东西可能失败，而失败的原因就被 Step A 排除了一大半。

## 命令编码

从 `panthor_sched.c:3218-3265` 逐位核对：

```
MOV48 rX:rX+1, imm64      (1 << 56) | (reg << 48) | imm
MOV32 rX+2, imm32         (2 << 56) | (reg << 48) | imm
STORE_STATE.cycles        (40 << 56) | (reg << 40) | (1 << 32)
SYNC_ADD64.system_scope   (51 << 56) | (addr_reg << 40) | (val_reg << 32) | 1
ERROR_BARRIER             (47 << 56)
NOP                       0
```

每条指令一个 64 位小端字，opcode 在 bits `[63:56]`。
一次提交要补 NOP 到 64 字节边界，因为预取器按 cache line 读。

`user_input->insert` 和 `user_output->extract` 都是**字节偏移**，不是指令条数。

Step B 的序列：

```
MOV48 r0:r1, cycles_va     ; 硬件周期计数器的目的地
STORE_STATE.cycles [r0:r1]
MOV48 r0:r1, sync_va       ; 第二个目的地
MOV48 r2:r3, 0xCAFEBABE
SYNC_ADD64 [r0:r1], r2:r3  ; 加到那个字上
ERROR_BARRIER
NOP
NOP
```

## 那个让 934 看起来「执行了但没效果」的 bug

`cmds` 指针没加 64。

Step A 消费了偏移 0..63，Step B 的八条真指令被写回**偏移 0**，
而 64..127 里还是 `memset` 留下的全零 NOP。于是 CEU 老老实实执行了八条 NOP，
`extract` 推进到 128，日志看起来像「ring 被消费了但写入没落地」。

**这个失败模式特别值得记住**：`extract` 推进是真的，它证明取指在工作；
但它**不**证明你写进去的东西被执行了。两者之间隔着一个「写到哪个偏移」的问题。

## 三个「恒为 0 但正常」的寄存器

这一路上有三个字段一直是零，每一个都让我怀疑过：

| 字段 | 为什么是 0 |
|---|---|
| `csg_out->status_endpoint_current` | 只在主机翻转 `CSG_REQ_STATUS_UPDATE` 后才回填 |
| `cs_out->status_cmd_ptr` | 只在 `STATUS_UPDATE` / suspend / fault 时由固件刷新 |
| `MGFCG_TIMESTAMP` | 这轮的 `hardware 0`；GPU 的 cycle counter 在别的寄存器里 |

判据应该用 `csg_out->ack`、`cs_out->ack`、`user_output->extract`、以及**内存里的值**。

## 现在的状态

| 层 | 状态 |
|---|---|
| M1 固件运行 | ✅ |
| M2a 一组一流 + ring | ✅ |
| M2b 取指 + 执行 + 写内存 | ✅ |
| M2c compute shader | ⬜ 下一步 |
| 退出基线 | ✅ 34 秒 |

## 镜像

`boot-935.img`，boot-args `mali=6 mali_m2=1`。
`mali_m2=0` 完全跳过 M2a/M2b。
