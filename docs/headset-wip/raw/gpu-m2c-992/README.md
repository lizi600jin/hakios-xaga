# GPU 第一次执行真实 shader：M2c 与 Task 2

2026-11-27，`boot-991` 和 `boot-992`。Mali-G610 在 XNU 下第一次有了自己的页表，
第一次跑起了 Valhall compute shader。

## 一、起点

`boot-990` 之前 GPU 只走到 M2b：手工构造 5 条 CSF 指令，把 `0xcafebabe` 写进
预先约定的固定地址。**它不能访问任意内存，所以跑不了任何真实程序。**

## 二、M2c：AS1 页表（`boot-991`，`mali_m2c=1`）

给用户态地址空间 `AS1` 建独立的 `mali_vm`，按 VM 分配和回收页表；
用 `kmem_alloc` 分配命令环和结果缓冲区，每个 16 KiB XNU 页拆成四个 4 KiB PTE；
`AS0` 继续专供固件；把 Group 0 的 `csg_in->config` 从固定 `0` 改为选定 AS 槽位。

```
mali: m2c: AS1 enabled root PA 0x19f324000 ring PA 0x19f321000 result PA 0x19f31f000
mali: m2b: step B: sync 0x10000000cafebabe (want 0x10000000cafebabe) cycles 0x19a8e59
mali: m2c: AS1 faultstatus 0 faultaddr 0 mmu_raw 0x20000 gpu_fault 0
mali: m2c_as1_high_pa: PASS
```

三个 PA 全在 6.49 GiB（> 4 GiB）。这一步验证 MMU 的高位寻址能力。

`mmu_raw 0x20000` 是 bit 17 `REQ_COMPLETE` 正常锁存。

## 三、Task 2：`RUN_COMPUTE` 真 shader（`boot-992`，`mali_m2c=2`）

### shader 字节码

Valhall v10，3 条指令 24 字节：

```
SHADDX.u64 r0, u0, r60.w0, shift:0x2     ; r0:r1 = out_va + tid * 4
IADD.u32 r2, u2, ^r60                    ; r2 = 0xdead0000 + tid
STORE.i32.slot0.end @r2, ^r0, offset:0   ; out[tid] = r2; wait+end
```

`r60` 是硬件预加载的 `global_invocation_id.x`（`Preload.R60`）。

用 Mesa 25.0.7 自带的汇编器生成：

```bash
cd mesa-25.0.7/src/panfrost/compiler/valhall
python3 asm.py m2c_compute.va m2c_compute.bin
```

输出：

```
code[0] = 0x01a3c00020273c80ull
code[1] = 0x00a0c20000007c82ull
code[2] = 0x7861420218000040ull
```

### 汇编器语法

| 规则 | 说明 |
|---|---|
| 助记符全大写 | `MOV.i32` 不是 `mov` |
| staging 寄存器用 `@` 前缀，排第一位 | `LOAD.i32 @, r0, offset:0x0` |
| 立即数格式 `<name>:<value>` | `offset:0x0` |
| modifier 必需 | `LOAD.i32` 缺 `.slot0` 报 Missing required modifier slot |

指令结构查询：

```python
from valhall import valhall_parse_isa
(instructions, immediates, enums, typesize, safe_name) = valhall_parse_isa()
```

### 描述符

- `SPD`（32 B, `+0x100`）：`word0 = 0x18`（Type=8 Shader | Stage=1 Compute），
  `word1 = 0x1000`（Preload.R60），`word2..3 = shader_va`
- `TSD`（32 B, `+0x140`）：**`word1 = 31`** = `log2(MALI_LOCAL_STORAGE_NO_WORKGROUP_MEM)`，
  **全 0 会让硬件误判有 WLS**
- `FAU`（`+0x180`）：`fau[0] = out_va`，`fau[1] = 0xdead0000`
- `SRT`：设 0（不访问纹理/图像）

shader 代码页必须是 `PTE_RDONLY` **不带 `PTE_XN`**；ring 和结果页带 `PTE_XN`。

### CSF 指令流

```
REQ_RESOURCE(Compute=1)
  -> SET_SB_ENTRY(ep=2, other=0)
  -> 填 SRT / FAU / SPD / TSD / attr_off / wg(4,1,1) / offset(0,0,0) / grid(4,1,1)
  -> RUN_COMPUTE(opcode=4, inc=4, axis=X)
  -> WAIT(0xff) -> MOV32 r74, 0
  -> FLUSH_CACHE2(opcode=36, L2=CI, LSC=CI, Other=Inv, slot=0)
  -> WAIT(slot 0) -> SYNC_ADD64(+0x12345678) -> ERROR_BARRIER
```

24 指令补齐 256 B，`insert = 384`。

### 结果

```
mali: m2c: AS1 enabled root PA 0x1baa0c000 ring PA 0x1baa09000 result PA 0x1baa07000 shader PA 0x1baa02000
mali: m2c: step C compute: extract 384 (want 384) after 1 ms | shader_ready 0x130013 ep_cur 0
mali: m2c: step C sync 0x10000000dd331136 (want 0x10000000dd331136) matched 16/16
mali: m2c: step C cs ack 0x1 fault 0 fatal 0 cmd_ptr 0 | irq raw 0x600 job 0x80000001 mmu 0x20000 fs 0
mali: m2c_compute: PASS
```

`shader_ready 0x130013` = 低 16 位 3 核 + 高 16 位 3 核 = **6 个 Shader Core 全上电**。

`matched 16/16` = 16 个线程（4 workgroup × (4,1,1)）全部把 `0xdead0000 + tid` 写对。

四个 PA 全在 **6.92 GiB**。

## 四、验收汇总

| 项 | `boot-991` | `boot-992` |
|---|---|---|
| AS1 高位 PA | 6.49 GiB | 6.92 GiB |
| shader PA | — | 6.92 GiB |
| 6 核上电 | — | `0x130013` |
| 写入验证 | `0xcafebabe` | `matched 16/16` |
| fault | 全 0 | 全 0 |
| RETURN | 30 s | 31 s |

## 五、boot-arg 分级

```
mali_m2c=0   -> 基线（AS0 M2b，等价 boot-990）
mali_m2c=1   -> M2c（AS1 高位 PA）
mali_m2c=2   -> Task 2（AS1 + shader 执行）
```

## 六、镜像

```
golden/boot-991-m2c-pass.img
  sha256 beb5e96f73eaa13bf98a24df19bec06fa4c9f33c0c6a5b1cb16e2cd1f603250f

golden/boot-992-compute-pass.img
  sha256 91ee276c05a2f142e841977021d623cb3c89508496d7205d91790a64de9dc4a9
```

## 七、教训

1. 用上游的汇编器，不手写 ISA —— Mesa 的 `asm.py` 加 `v10.xml` 保证位级正确
2. `TSD.word1 = 31` 是坑 —— 全 0 会被误判成有 WLS
3. shader 页不能带 `PTE_XN` —— 否则取指就 fault
4. 四次刷机四个里程碑（M2b -> M2c -> Task 2），每次单变量，归因清楚
