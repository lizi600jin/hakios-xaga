# 服务集成 719：平台/loader 审查与隔离内核构建

> 2026-11-17 19:15，DSH。**仅主机。未打包、未碰设备、未改候选。**
> CMD 身份：`fb2116699456d0811a128b2b9d951b346b948ffe0e1f58b9298345ea146e79f2`

**核心 8 文件保持 `c9a48c8` 原样，未重复其 34121/2500 测试（无具体疑虑）。**

---

## 0. 结果

| 项 | 值 |
|---|---|
| **MANIFEST** | **57/57 全匹配** |
| **kernel inputs** | **5214 / 5214 OK** |
| **diff vs 718** | **5 变化 + 4 新增 + 0 删除** ✅ |
| **四个旧对象** | **全部缺席** ✅ |
| **make native rc** | **2** |
| **judge native rc** | **0** |
| **新内核** | **15339768 B** |
| **SHA256** | **`5a85ac7fed4392b1a87a9cb4f91f592fd9697cafbb572fe4d1041794c991494f`** |
| **具体阻塞** | **无** |

---

## 1. 输入与 diff

### 1.1 清单

| 项 | 值 |
|---|---|
| status | `FROZEN_FOR_HOST_REVIEW` |
| files | **57** |
| **校验** | **57/57** ✅ |
| `kernel_manifest_sha256` | **`8fbd125a1b8a535e...`**（= `kernel-inputs.sha256` **文件自身**的 hash）|
| **`sha256sum -c`** | **rc=0，5214 OK，0 FAILED** |
| 构建前 BUILD | **不存在** ✅ |

### 1.2 五个变化文件

| 文件 |
|---|
| `bsd/conf/files` |
| `bsd/dev/hakios.input.c` |
| `bsd/dev/ufs_read_contract.h` |
| `bsd/sys/hakios.input.h` |
| `osfmk/arm64/loader_return.c` |

### 1.3 四个新增文件

| 文件 |
|---|
| **`bsd/dev/hakios.mt6895_ufs_service.c`**（17785 B / 383 行）|
| `bsd/dev/ufs_service_engine.h` |
| `bsd/dev/ufs_service_frontend.h`（692 B）|
| `bsd/dev/ufs_service_lease.h` |

### 1.4 🔴 旧模块不参与编译

**`bsd/conf/files:705`：**

```
bsd/dev/hakios.mt6895_ufs_service.c        standard
```

**旧的仍是 `optional hakios_ufs`（第 148-149 行），且 `CONFIG_HAKIOS_UFS` 未开（config 第 39 行）。**

**构建后验证四个旧对象**全部缺席**：`hakios.ufs.o`、`hakios.ufshc.o`、
`hakios.mt6895_ufs.o`、`hakios.mt6895_ufs_read.o`。**

---

## 2. 平台 provider 审查

### 2.1 ✅ baseline 双检（`clean_baseline`，第 160-167 行）

```c
static bool clean_baseline(const struct ur_io *io)
{
    return ur_idle(io) && ur_copy_from(io,UR_TABLE,workspace.table,8) &&
        ur_copy_from(io,UR_UCD,workspace.ucd,UR_UCD_BYTES/4) && ur_previous_read(&workspace) &&
        ur_copy_from(io,UR_DATA,workspace.data,UR_BLOCK_BYTES/4) &&
        ur_same(io,UR_TABLE,workspace.table,8) && ur_same(io,UR_UCD,workspace.ucd,UR_UCD_BYTES/4) &&
        ur_same(io,UR_DATA,workspace.data,UR_BLOCK_BYTES/4) && ur_idle(io);
}
```

**逐项核对 CMD 要求：**

| 要求 | 实现 |
|---|---|
| **检查 idle** | **两次**（开头 + 结尾）✅ |
| **继承 READ** | `ur_previous_read` ✅ |
| **table 检查** | `ur_copy_from` + `ur_same` ✅ |
| **UCD 检查** | `ur_copy_from` + `ur_same` ✅ |
| **DATA 检查** | `ur_copy_from` + `ur_same` ✅ |
| **三块都做两遍** | ✅ **每块都 copy_from + same** |

### 2.2 ✅ cached control 从不重置

**`static struct us_lease lease = {{0,service_publish,&platform}}`（第 52 行）—— 静态初始化一次。**
**全文件无 `lease.control =` 直接赋值。（`us_lease` 的原子操作都在头文件里。）**

### 2.3 ✅ 只有 ACTIVE 能写

```c
static bool service_write_word(void *context,enum ur_memory region,uint32_t off,uint32_t value)
{
    volatile uint32_t *word=service_word(context,region,off);
    if (!word || us_control(&lease)!=US_ACTIVE) return false;   /* ← 精确 ACTIVE */
    *word=value;return true;
}
```

**🔴 用 `!= US_ACTIVE` 而不是掩码比较 —— **任何其他 phase 都拒绝写入**。**

### 2.4 ✅ ring 双条件

```c
static bool service_ring(void *context)
{
    if (us_control(&lease)!=US_ACTIVE || shared(p)->state!=UR_CLAIMED) return false;
    *(volatile uint32_t *)(p->regs+0x58)=1u; /* sole controller store, one per admitted READ */
    service_barrier(NULL);return true;
}
```

**→ **cached ACTIVE** 且 **shared CLAIMED** 两个条件都满足才写 `0x58`。**

### 2.5 ✅ finish(true) 只标 DRAINED

```c
static bool service_finish(void *unused,bool restored)
{
    if (!restored) { us_poison(&lease);return false; }
    return us_mark_drained(&lease);      /* ← 不是 release，是 mark_drained */
}
```

**→ 外层 shared 租约**保持 CLAIMED**，符合设计。**

### 2.6 ✅ quiesce 只调 us_stop（第 329-330 行）

```c
void hakios_mt6895_ufs_service_quiesce(void)
{ us_stop(&lease); } /* only cached atomic control and aligned shared-DRAM publication */
```

**🔴 单行实现 —— 无控制器访问、无 logger、无等待。** 与 CMD 要求完全一致。

### 2.7 调用点（`loader_return.c:377`）

```c
psci_smp_stop_others();
hakios_mt6895_headset_quiesce();
hakios_mt6895_ufs_service_quiesce(); /* software STOP/shared lease only; no controller access */
```

---

## 3. VFS 前端审查

### 3.1 预算 3（第 45 行）

```c
#define US_MAX_REQUESTS 3u
```

**第 177 行：**

```c
if (requests>=US_MAX_REQUESTS) { lck_mtx_unlock(&service_lock);return EAGAIN; }
uint32_t index=requests++; /* global budget includes failed attempts; never reset */
```

**🔴 关键：`requests++` 在**任何引擎调用之前**发生 —— **失败的尝试也计数**，且注释明确「never reset」。**

### 3.2 FWRITE 拒绝（第 228 行）

```c
if (flags&FWRITE) return EROFS;
```

### 3.3 三次读 + 2 秒间隔 + EOF（第 278-306 行）

```c
error=VNOP_OPEN(node,FWRITE,ctx);
if (!error) (void)VNOP_CLOSE(node,FWRITE,ctx);   /* 先验证 FWRITE 被拒 */
error=VNOP_OPEN(node,FREAD,ctx);
for (uint32_t i=0;i<US_MAX_REQUESTS;i++) {
    ...
    error=vn_rdwr(UIO_READ,node,(caddr_t)copy,UR_BLOCK_BYTES,0,UIO_SYSSPACE,IO_NOCACHE,...);
    ...
    if (i+1<US_MAX_REQUESTS) delay_for_interval(2000,NSEC_PER_MSEC);   /* 2 秒间隔 */
    error=vn_rdwr(UIO_READ,node,(caddr_t)copy,4,UR_BLOCK_BYTES,UIO_SYSSPACE,IO_NOCACHE,...);
}
```

**→ 三次 4096 字节读 + 2 秒间隔 + 偏移 `UR_BLOCK_BYTES` 处的 4 字节读（应为 EOF）。**

### 3.4 `us_plan_read`：纯计算，不触设备

```c
if (!plan || offset < 0 || offset > UR_BLOCK_BYTES || residual < 0) return US_PLAN_INVALID;
if (!residual || offset == UR_BLOCK_BYTES) return US_PLAN_EOF;
plan->offset = (uint32_t)offset;
uint32_t left = UR_BLOCK_BYTES - plan->offset;
plan->bytes = (uint64_t)residual < left ? (uint32_t)residual : left;
return US_PLAN_READ;
```

**→ 偏移/长度计算 + EOF 判定，**无任何 IO**。**

---

## 4. 节点注册

**第 367-369 行：**

```c
service_major=cdevsw_add(-24,&service_cdevsw);
devfs_make_node(makedev(service_major,0),DEVFS_CHAR,UID_ROOT,GID_WHEEL,0400,"mt6895-ufs-lba1");
```

| 项 | 值 | 符合 |
|---|---|---|
| cdev 起点 | **-24** | ✅ CMD 说的「per this tree's API comment」|
| 权限 | **0400** | ✅ |
| 类型 | `DEVFS_CHAR` | ✅ 不是 `D_DISK`/`bdevsw` |
| 名称 | `mt6895-ufs-lba1` | ✅ |

---

## 5. 构建

| 项 | 值 |
|---|---|
| 命令 | `XNU_SRC=<绝对> ./xnu-build.sh -k -j8` |
| **make native rc** | **2** |
| 编译错误 | **0** |
| undefined | **0** |
| `CC hakios.input.o` | ✅ |
| `CC loader_return.o` | ✅ |
| **`CC hakios.mt6895_ufs_service.o`** | ✅ |
| **judge native rc** | **0** |
| 判词 | `only the kext symbol-set checks failed` |

**🔴 make rc=2 与 judge rc=0 分开记录。**

---

## 6. 内核与编入

**15339768 B / `5a85ac7fed4392b1a87a9cb4f91f592fd9697cafbb572fe4d1041794c991494f`**

**22 条 `mt6895-ufs-service` 消息，含：**

| 消息 | 含义 |
|---|---|
| `bounded readonly LUN2 LBA1 bytes4096 budget3; guarded handoff required` | 入口声明 |
| `node registered path=%s major=%d mode=0400 snapshot_bytes4096` | 注册成功 |
| **`opened baseline_verified=1 control=%#x shared=%u`** | **baseline 已验证** |
| `invalid or competing mode; no access` | 拒绝竞争 |
| `baseline/open refused; no request` | fail-closed |
| `node registration failed; close without requests` | 失败路径 |

**符号 `hakios_mt6895_ufs_service_init` / `_quiesce` 都在。**

**🔴 字符串只证明编入，不是运行时证据。**

---

## 7. 构建后复查与守卫

| 项 | 结果 |
|---|---|
| 5214 输入 | **rc=0，全 OK** |
| BUILD 外文件数 | 5214 |
| **57 冻结文件** | **57/57** ✅ |
| **四个旧对象** | **全部缺席** ✅ |

| 守卫 | 值 |
|---|---|
| stock | `c5f8e7bd` / md5 `a63b2e35` ✅ |
| shared / wrapper | `67f9f933` / `95247bb0` ✅ |
| **718 kernel** | **`60bd6b19`** 未写 ✅ |
| **718 boot** | **`3a3ceb1d`** 未写 ✅ |
| 生产 headset | `8c0cf1c2` ✅ |

---

## 8. 我的意见

### 8.1 无具体阻塞

**平台 provider、VFS 前端、节点注册、quiesce、init 接线都与 CMD 描述一致，我逐项核对未发现缺陷。**

### 8.2 三处我认为设计得好的

| # | 设计 | 为什么好 |
|---|---|---|
| **1** | **`service_write_word` 用 `!= US_ACTIVE`** | 精确匹配而非掩码，**任何其他 phase（含 DRAINED+STOP）都拒绝写** |
| **2** | **预算在引擎调用**之前**递增** | **失败的尝试也算**，不能通过失败重置预算 |
| **3** | **quiesce 只有一行 `us_stop`** | 无控制器、无 logger、无等待 —— 最容易出错的地方做到了最简 |

### 8.3 两条范围外的确认（同前轮）

**① `clean_baseline` 的真实性依赖 `ur_copy_from`/`ur_same` 所调用的
`service_read_word` 的诚实性。**
**② `service_publish` 的返回值语义决定 loader 能否看到 `UR_POISONED`。**

**这两条现在有平台实现了，但**运行时行为**仍未验证。**

### 8.4 术语（承接上轮更正）

**CMD 明确：「One `save` call is not one physical `WRITE10`; counts can change with length.」**

**我记下：`save_log_spans_to_flash` 调用一次 ≠ 物理 WRITE(10) 一次。**
**本轮的 span-writer 测试（87 checks）验证的是 tail/audit/sum/header-last 与失败写入，
**不涉及物理 DMA 对齐或电气层面**。**

---

## 9. 边界

**未做**：打包、设备查询/刷写、候选编辑、生产 push、旧物覆盖、GPUEB WIP staging、
clock/reset/UIC/watchdog/OEM expdb。

**🔴 未复跑核心 8 的 34121/2500 测试（无具体疑虑）。未复跑 span-writer 87 项。**
**所有行为描述来自源码与 CMD 引用的主机测试，**未在硬件上验证**。**

**等独立的打包与 ONE DEVICE719 命令。**
