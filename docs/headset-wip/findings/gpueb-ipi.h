/* -*- c -*-
 * gpueb-ipi.h — MT6895 GPUEB mailbox / IPI 协议常量与结构
 *
 * 状态：待集成草稿。不在 /home/lizi/IOS/hakios-mtk 内，不参与现有构建。
 * 目标：可直接 #include 进 XNU 的 Mali 驱动，提供常量 + 结构 + 内联原语。
 *
 * 设计约束
 *   1. 不依赖任何内核 API。没有 readl/writel/mb/udelay 的默认定义，
 *      由调用方通过 GPUEB_MMIO_* / GPUEB_BARRIER / GPUEB_UDELAY 注入。
 *   2. 所有地址、slot、位定义都有源码出处（见各条注释的 §编号，
 *      对应同目录 GPUEB-IPI-PROTOCOL.md）。
 *   3. 纯轮询，零中断。不注册 GIC，不依赖 completion。
 *
 * 证据来源
 *   [ML]  MT6895-Mainline/linux branch 7.2-mt6895-xiaomi-xaga
 *   [VND] 厂商 5.10 树 drivers/gpu/mediatek/gpueb/
 *   [DTS] xaga 真机反编译 DT
 *   [IOMEM] xaga 真机 /proc/iomem
 */

#ifndef _GPUEB_IPI_H_
#define _GPUEB_IPI_H_

/* ═══════════════════════════════════════════════════════════════════════
 * 0. 移植层：调用方必须提供这三个宏（或保留默认）
 * ═══════════════════════════════════════════════════════════════════════ */

/* MMIO 读写。XNU 侧传进已映射的虚拟地址；这里只做解引用。 */
#ifndef GPUEB_MMIO_READ32
#  define GPUEB_MMIO_READ32(va)        (*(volatile unsigned int *)(va))
#endif
#ifndef GPUEB_MMIO_WRITE32
#  define GPUEB_MMIO_WRITE32(va, v)    (*(volatile unsigned int *)(va) = (v))
#endif

/* 内存屏障。写 SRAM 之后、写 IRQ_SET 之前必须有。 */
#ifndef GPUEB_BARRIER
#  define GPUEB_BARRIER()              __asm__ __volatile__("" ::: "memory")
#endif

/* 微秒延时。轮询间隔 10 µs，与 [ML] mtk_tinysys_ipi.c:21 ipi_delay() 一致 */
#ifndef GPUEB_UDELAY
#  define GPUEB_UDELAY(us)             do { (void)(us); } while (0)
#endif

#ifndef GPUEB_NULL
#  define GPUEB_NULL                   ((void *)0)
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * 1. 物理地址（[DTS] xaga_live.dts:4895-4898, [IOMEM] dump/iomem.txt:97-101）
 *
 *   13c5fd80-13c5ffff : 13c40000.gpueb mbox0_base   ← 640 B 消息 SRAM
 *   13c62000-13c62003 : 13c40000.gpueb mbox0_send
 *   13c62004-13c62007 : 13c40000.gpueb mbox0_set
 *   13c62074-13c62077 : 13c40000.gpueb mbox0_clr
 *   13c62078-13c6207b : 13c40000.gpueb mbox0_recv
 *
 * ⚠ 0x13c5fd80 是消息 SRAM，不是寄存器块。
 * ⚠ 0x13c40000 (gpueb_base) 严禁触碰 —— 门控未确认。
 * ⚠ 0x13c60000..0x13c61fff 未映射（iomem 无条目），不要按偏移推算过去。
 * ═══════════════════════════════════════════════════════════════════════ */

#define GPUEB_PHYS_BASE             0x13c40000u   /* 禁读，仅作记录 */
#define GPUEB_PHYS_SIZE             0x22000u

#define GPUEB_MBOX_SRAM_PHYS        0x13c5fd80u   /* 消息 SRAM 基址 */
#define GPUEB_MBOX_SRAM_SIZE        0x280u        /* 640 B = 160 slots × 4 B */

#define GPUEB_MBOX_SEND_PHYS        0x13c62000u   /* RO: TX busy  (bitmap) */
#define GPUEB_MBOX_SET_PHYS         0x13c62004u   /* WO: raise IRQ (bitmap) */
#define GPUEB_MBOX_CLR_PHYS         0x13c62074u   /* WO: clear IRQ (bitmap) */
#define GPUEB_MBOX_RECV_PHYS        0x13c62078u   /* RO: RX pending (bitmap) */

/* 共享内存：mblock-38-me_GPUEB_SHARED，2 MiB 预留；
 * MEM_ID_GPUFREQ 是其中第一块 0x1000（[DTS]:4899 gpueb_mem_table = <0 0x1000>, <1 0x180000>） */
#define GPUEB_SHARED_PHYS           0x7c600000u
#define GPUEB_SHARED_SIZE           0x200000u     /* 2 MiB 总预留 */
#define GPUFREQ_MEM_SIZE            0x1000u       /* MEM_ID_GPUFREQ 实际大小 */
#define GPUFREQ_STATUS_MEM_SZ       0x400u        /* status 区，[VND] gpufreq_v2.h:19 */
#define GPUFREQ_DEBUG_MEM_SZ        (GPUFREQ_MEM_SIZE - GPUFREQ_STATUS_MEM_SZ)  /* 0xc00 */

/* GPUEB 的 MMIO 在固件内是 +0x10000000 重映射（[FW] GPUEB-RE.md:101）。
 * AP 侧不需要，这里只作记录，防止有人误用。 */
#define GPUEB_FW_MMIO_BIAS          0x10000000u

/* 中断（我们不注册，仅记录以便排查） */
#define GPUEB_IRQ_SPI               516           /* DT interrupts <0 0x204 4> */
#define GPUEB_IRQ_GIC_INTID         (GPUEB_IRQ_SPI + 32)   /* = 548 */

/* ═══════════════════════════════════════════════════════════════════════
 * 2. ctl bank 内部偏移（权威：[ML] drivers/mailbox/mtk-gpueb-mailbox.c:22-25）
 *
 *   绝对地址 = GPUEB_MBOX_*_PHYS，下面给的是相对 0x13c62000 的偏移。
 *   全部是 1 bit / 通道 的位图，不是队列指针。
 * ═══════════════════════════════════════════════════════════════════════ */

#define GPUEB_MBOX_CTL_TX_STS       0x00u   /* RO, 1 = 上一条还没被取走 */
#define GPUEB_MBOX_CTL_IRQ_SET      0x04u   /* WO, 写 BIT(chan) 触发 */
#define GPUEB_MBOX_CTL_IRQ_CLR      0x74u   /* WO, 写 BIT(chan) 清位/应答 */
#define GPUEB_MBOX_CTL_RX_STS       0x78u   /* RO, 1 = 对方给我发了消息 */

/* ═══════════════════════════════════════════════════════════════════════
 * 3. 通道表（[DTS]:4891-4894）
 *
 *   send_table = <id mbox slots>          recv_table = <id mbox slots recv_opt>
 *   slot 分配：[VND] gpueb_ipi.c:250-274 —— TX 全排完才排 RX，共用一个累加器
 *
 *   recv_opt：0 = MBOX_RECV_MESSAGE（普通消息），1 = MBOX_RECV_ACK（这是应答）
 * ═══════════════════════════════════════════════════════════════════════ */

#define GPUEB_MBOX_SLOT_SIZE        4u       /* [VND] mtk-mbox.h:18 */
#define GPUEB_MBOX_TOTAL_SLOTS      160u     /* [DTS] mbox_size = <160> */

/* 通道 ID（同时也是 mbox 的 pin_index / bit 号） */
enum gpueb_ipi_chan {
    GPUEB_CHAN_FAST_DVFS_EVENT = 0,
    GPUEB_CHAN_GPUFREQ         = 1,   /* ★ 我们只用这一条 */
    GPUEB_CHAN_SLEEP           = 2,
    GPUEB_CHAN_TIMER           = 3,
    GPUEB_CHAN_FHCTL           = 4,
    GPUEB_CHAN_CCF             = 5,
    GPUEB_CHAN_GPUMPU          = 6,
    GPUEB_CHAN_FAST_DVFS       = 7,
    GPUEB_CHAN_NUM             = 8
};

/* TX slot / 字节偏移（AP → GPUEB） */
#define GPUEB_TX_SLOT_FAST_DVFS_EVENT   0u    /* 0x000,  4 slots / 16 B */
#define GPUEB_TX_SLOT_GPUFREQ           4u    /* 0x010,  8 slots / 32 B ★ */
#define GPUEB_TX_SLOT_SLEEP            12u    /* 0x030,  3 slots / 12 B */
#define GPUEB_TX_SLOT_TIMER            15u    /* 0x03c,  3 slots / 12 B */
#define GPUEB_TX_SLOT_FHCTL            18u    /* 0x048,  9 slots / 36 B */
#define GPUEB_TX_SLOT_CCF              27u    /* 0x06c,  4 slots / 16 B */
#define GPUEB_TX_SLOT_GPUMPU           31u    /* 0x07c,  6 slots / 24 B */
#define GPUEB_TX_SLOT_FAST_DVFS        37u    /* 0x094,  6 slots / 24 B */
/* TX 总占用 43 slots → RX 从此处开始 */

/* RX slot / 字节偏移（GPUEB → AP）—— ★ 注意不是和 TX 同号 */
#define GPUEB_RX_SLOT_FAST_DVFS_EVENT  43u    /* 0x0ac,  4 slots / 16 B */
#define GPUEB_RX_SLOT_GPUFREQ          47u    /* 0x0bc,  8 slots / 32 B ★★ */
#define GPUEB_RX_SLOT_SLEEP            55u    /* 0x0dc,  1 slots /  4 B */
#define GPUEB_RX_SLOT_TIMER            56u    /* 0x0e0,  1 slots /  4 B */
#define GPUEB_RX_SLOT_FHCTL            57u    /* 0x0e4,  1 slots /  4 B */
#define GPUEB_RX_SLOT_CCF              58u    /* 0x0e8,  4 slots / 16 B */
#define GPUEB_RX_SLOT_GPUMPU           62u    /* 0x0f8,  1 slots /  4 B */
#define GPUEB_RX_SLOT_FAST_DVFS        63u    /* 0x0fc,  6 slots / 24 B */
/* RX 结束 69 slots (0x114) < 160 slots 上限 */

/* GPUFREQ 通道的绝对字节偏移（相对 GPUEB_MBOX_SRAM_PHYS） */
#define GPUEB_GPUFREQ_TX_OFF          (GPUEB_TX_SLOT_GPUFREQ * GPUEB_MBOX_SLOT_SIZE)  /* 0x010 */
#define GPUEB_GPUFREQ_RX_OFF          (GPUEB_RX_SLOT_GPUFREQ * GPUEB_MBOX_SLOT_SIZE)  /* 0x0bc */

/* GPUFREQ 通道的绝对物理地址（省掉运行时加法，便于日志打印） */
#define GPUEB_GPUFREQ_TX_PHYS        (GPUEB_MBOX_SRAM_PHYS + GPUEB_GPUFREQ_TX_OFF)   /* 0x13c5fd90 */
#define GPUEB_GPUFREQ_RX_PHYS        (GPUEB_MBOX_SRAM_PHYS + GPUEB_GPUFREQ_RX_OFF)   /* 0x13c5fe3c */

/* ═══════════════════════════════════════════════════════════════════════
 * 4. 消息（32 字节 / 8 × u32）
 *    [ML] drivers/gpu/mediatek/gpueb/include/gpufreq_ipi.h:51-78
 *
 *    struct {
 *        u32 cmd_id;               // u32[0] off 0x00
 *        u32 target;               // u32[1] off 0x04
 *        union { ... };            // u32[2..7] off 0x08..0x1c
 *    };                            // sizeof == 32, GPUFREQ_IPI_DATA_LEN == 8
 *
 *    ⚠ addr 子结构是「两个 u64 在前，两个 u32 在后」：
 *        u64 status_base;  u64 debug_base;  u32 status_size;  u32 debug_size;
 *      不是 base/size/base/size 交错！
 * ═══════════════════════════════════════════════════════════════════════ */

#define GPUFREQ_IPI_MSG_WORDS       8u
#define GPUFREQ_IPI_MSG_BYTES       (GPUFREQ_IPI_MSG_WORDS * 4u)   /* 32 */

/* 命令号（[ML] gpufreq_ipi.h:29-49） */
enum gpufreq_ipi_cmd {
    CMD_INIT_SHARED_MEM    = 0,    /* ★ 必须先发，且只发一次 */
    CMD_GET_FREQ_BY_IDX    = 1,
    CMD_GET_POWER_BY_IDX   = 2,
    CMD_GET_OPPIDX_BY_FREQ = 3,
    CMD_GET_LEAKAGE_POWER  = 4,
    CMD_SET_LIMIT          = 5,
    CMD_POWER_CONTROL      = 6,    /* ★ 上电 */
    CMD_COMMIT             = 7,
    CMD_GET_DEBUG_OPP_INFO = 8,
    CMD_GET_DEBUG_LIMIT_INFO = 9,
    CMD_GET_WORKING_TABLE  = 10,
    CMD_GET_SIGNED_TABLE   = 11,
    CMD_GET_LIMIT_TABLE    = 12,
    CMD_SWITCH_LIMIT       = 13,
    CMD_FIX_TARGET_OPPIDX  = 14,
    CMD_FIX_CUSTOM_FREQ_VOLT = 15,
    CMD_SET_STRESS_TEST    = 16,
    CMD_SET_AGING_MODE     = 17,
    CMD_SET_GPM_MODE       = 18,
    CMD_SET_TEST_MODE      = 19,
    CMD_NUM                = 20
};

/* target 取值（[VND] gpufreq_v2.h:78-83） */
enum gpufreq_target {
    TARGET_DEFAULT = 0,
    TARGET_GPU     = 1,
    TARGET_STACK   = 2,
    TARGET_INVALID = 3
};

/* 电源状态（[VND] gpufreq_v2.h:85-88） */
enum gpufreq_power_state {
    POWER_OFF = 0,
    POWER_ON  = 1
};

/* 32 字节消息。用 union 保证既能按字看也能按字段看。 */
union gpufreq_ipi_msg {
    unsigned int w[GPUFREQ_IPI_MSG_WORDS];

    struct {
        unsigned int cmd_id;   /* w[0] */
        unsigned int target;   /* w[1] */
        union {
            int          oppidx;        /* CMD_COMMIT / 应答 */
            int          return_value;  /* ★ 应答的返回值，读这里（w[2]） */
            unsigned int freq;
            unsigned int volt;
            unsigned int power;
            unsigned int power_state;   /* ★ CMD_POWER_CONTROL 用（w[2]） */
            unsigned int mode;
            struct {                    /* CMD_INIT_SHARED_MEM 用（w[2..7]） */
                unsigned long long status_base;   /* w[2..3] */
                unsigned long long debug_base;    /* w[4..5] */
                unsigned int       status_size;   /* w[6]    */
                unsigned int       debug_size;    /* w[7]    */
            } addr;
            struct {
                unsigned int freq;
                unsigned int volt;
            } custom;
            struct {
                unsigned int limiter;
                int          ceiling_info;
                int          floor_info;
            } setlimit;
        } u;
    } f;
};

/* ═══════════════════════════════════════════════════════════════════════
 * 5. 共享状态区（struct gpufreq_shared_status 的前 32 个字）
 *    [ML] gpufreq_ipi.h:80-114 —— 字段顺序即内存顺序
 *
 *    ⚠ GPUEB 固件写，AP 只读。唯一例外：谁都不写，全靠固件。
 *    ⚠ power_count == 0 不能证明「没上过电」（原厂 Android 也是 0）。
 *    ⚠ power_control 是「使能开关」标量，不是位域：
 *         != 0 → GPU 电源归 GPUEB 管；== 0 → AP 自己管。
 *       没有「上电位」也没有「shader power 位」。
 * ═══════════════════════════════════════════════════════════════════════ */

/* status 区字段的字节偏移（已用 gcc offsetof 验证） */
#define GPUSTAT_OFF_CUR_OPPIDX_GPU      0x00u
#define GPUSTAT_OFF_CUR_OPPIDX_STACK    0x04u
#define GPUSTAT_OFF_OPP_NUM_GPU         0x08u
#define GPUSTAT_OFF_OPP_NUM_STACK       0x0cu
#define GPUSTAT_OFF_CUR_FGPU            0x10u
#define GPUSTAT_OFF_CUR_FSTACK          0x14u
#define GPUSTAT_OFF_CUR_VGPU            0x18u
#define GPUSTAT_OFF_CUR_VSTACK          0x1cu
#define GPUSTAT_OFF_CUR_VSRAM_GPU       0x20u
#define GPUSTAT_OFF_CUR_VSRAM_STACK     0x24u
#define GPUSTAT_OFF_CUR_POWER_GPU       0x28u
#define GPUSTAT_OFF_CUR_POWER_STACK     0x2cu
#define GPUSTAT_OFF_MAX_POWER_GPU       0x30u
#define GPUSTAT_OFF_MAX_POWER_STACK     0x34u
#define GPUSTAT_OFF_MIN_POWER_GPU       0x38u
#define GPUSTAT_OFF_MIN_POWER_STACK     0x3cu
#define GPUSTAT_OFF_CUR_CEILING_GPU     0x40u
#define GPUSTAT_OFF_CUR_FLOOR_GPU       0x44u
#define GPUSTAT_OFF_CUR_CEILING_STACK   0x48u
#define GPUSTAT_OFF_CUR_FLOOR_STACK     0x4cu
#define GPUSTAT_OFF_CUR_C_LIMITER_GPU   0x50u
#define GPUSTAT_OFF_CUR_F_LIMITER_GPU   0x54u
#define GPUSTAT_OFF_CUR_C_LIMITER_STACK 0x58u
#define GPUSTAT_OFF_CUR_F_LIMITER_STACK 0x5cu
#define GPUSTAT_OFF_POWER_CONTROL       0x60u   /* ★ 使能开关，非位域 */
#define GPUSTAT_OFF_DVFS_STATE          0x64u   /* ★ 见 enum gpufreq_dvfs_state */
#define GPUSTAT_OFF_SHADER_PRESENT      0x68u   /* ★ 固件写的 shader 核位图 */
#define GPUSTAT_OFF_POWER_COUNT         0x6cu   /* ★ 引用计数，0 不代表没上过电 */
#define GPUSTAT_OFF_AGING_ENABLE        0x70u
#define GPUSTAT_OFF_AVS_ENABLE          0x74u
#define GPUSTAT_OFF_SB_VERSION          0x78u
#define GPUSTAT_OFF_PTP_VERSION         0x7cu

/* dvfs_state 位（[VND] gpufreq_v2.h:69-76） */
#define DVFS_FREE                   0x00u
#define DVFS_DISABLE                (1u << 0)
#define DVFS_POWEROFF               (1u << 1)   /* ★ 上电后固件应清掉这一位 */
#define DVFS_DEBUG_KEEP             (1u << 2)
#define DVFS_AVS_KEEP               (1u << 3)
#define DVFS_AGING_KEEP             (1u << 4)

/* shader_present 位（[VND] gpufreq_mt6895.h:81-97 + gpufreq/v1 mt6885:30-38）
 *
 * ⚠ T0C1 / T4C0 / T4C1 的位索引在公开代码里没有定义（MT6885 v1 头无 T4）
 *   → 完整 6 核值未知，需真机 dump 确认。
 *   下面只给已确证的三个位。 */
#define SHADER_T0C0                 (1u << 0)     /* 已确证 */
#define SHADER_T1C0                 (1u << 1)     /* 已确证 */
#define SHADER_T5C0                 (1u << 17)    /* 已确证 */
#define SHADER_MFG3_STACK0_PARTIAL  (SHADER_T0C0)              /* T0C1 未知 */
#define SHADER_MFG4_STACK1          (SHADER_T1C0)
#define SHADER_MFG5_STACK2_UNKNOWN  (0u)                       /* T4C0|T4C1 未知 */
#define SHADER_MFG6_STACK4          (SHADER_T5C0)
#define SHADER_CORE_NUM             6u

/* 已确证的下界掩码：若 shader_present 连这三位都没有，一定不对 */
#define SHADER_PRESENT_KNOWN_BITS \
    (SHADER_MFG4_STACK1 | SHADER_MFG6_STACK4)

/* ═══════════════════════════════════════════════════════════════════════
 * 6. 超时与错误码
 * ═══════════════════════════════════════════════════════════════════════ */

#define IPI_TIMEOUT_MS_DEFAULT      10000u   /* [VND] gpueb_ipi.h:10，厂商默认 10 s */
#define IPI_POLL_INTERVAL_US        10u      /* [ML] mtk_tinysys_ipi.c:21 udelay(10) */

/* 建议值：启动路径上不要用 10 s，见报告 §7.4 */
#define GPUEB_TMO_INIT_SHARED_MS    1000u
#define GPUEB_TMO_POWER_CTRL_MS     1000u

enum gpueb_status {
    GPUEB_OK              =  0,
    GPUEB_ETXSTUCK        = -1,   /* TX_STS bit 一直为 1，上一条没被取走 */
    GPUEB_ETIMEOUT        = -2,   /* 等应答超时 */
    GPUEB_EREFUSED        = -3,   /* 收到应答但 return_value < 0 */
    GPUEB_EBADPARAM       = -4,
    GPUEB_ENOTREADY       = -5    /* 预检判定 GPUEB 不在 */
};

/* ═══════════════════════════════════════════════════════════════════════
 * 7. 内联原语（纯轮询，零中断）
 *
 *    全部以 "基址 + 偏移" 形式接受已映射的虚拟地址，
 *    这样同一份代码能跑在 XNU 的 ml_io_map 结果上。
 * ═══════════════════════════════════════════════════════════════════════ */

/* 通道 n 的位 */
#define GPUEB_BIT(n)                (1u << (n))

/* 7.1 TX 是否空闲。返回非 0 = 忙，不能发。 */
static inline int
gpueb_tx_busy(unsigned long ctl_va)
{
    return (GPUEB_MMIO_READ32(ctl_va + GPUEB_MBOX_CTL_TX_STS)
            & GPUEB_BIT(GPUEB_CHAN_GPUFREQ)) != 0;
}

/* 7.2 RX 是否有待收。返回非 0 = 有待收。 */
static inline int
gpueb_rx_pending(unsigned long ctl_va)
{
    return (GPUEB_MMIO_READ32(ctl_va + GPUEB_MBOX_CTL_RX_STS)
            & GPUEB_BIT(GPUEB_CHAN_GPUFREQ)) != 0;
}

/* 7.3 触发（raise）—— 必须在写完 SRAM 且 GPUEB_BARRIER() 之后调用 */
static inline void
gpueb_irq_raise(unsigned long ctl_va)
{
    GPUEB_MMIO_WRITE32(ctl_va + GPUEB_MBOX_CTL_IRQ_SET,
                       GPUEB_BIT(GPUEB_CHAN_GPUFREQ));
}

/* 7.4 清位（clear / ack） */
static inline void
gpueb_irq_clear(unsigned long ctl_va)
{
    GPUEB_MMIO_WRITE32(ctl_va + GPUEB_MBOX_CTL_IRQ_CLR,
                       GPUEB_BIT(GPUEB_CHAN_GPUFREQ));
}

/* 7.5 把 32 字节消息逐字写入 TX slot。
 *     逐字写（不是 memcpy）——[ML] mtk-gpueb-mailbox.c:136-143 明确要求。 */
static inline void
gpueb_msg_write(unsigned long sram_va, const union gpufreq_ipi_msg *m)
{
    unsigned int i;
    for (i = 0; i < GPUFREQ_IPI_MSG_WORDS; i++)
        GPUEB_MMIO_WRITE32(sram_va + GPUEB_GPUFREQ_TX_OFF + i * 4u, m->w[i]);
}

/* 7.6 从 RX slot 逐字读出 32 字节消息。
 *     ⚠ 顺序：[ML] 是先读体后清位；[VND] 是先清位后读体。
 *       这里跟 [ML]（mainline 审过）。 */
static inline void
gpueb_msg_read(unsigned long sram_va, union gpufreq_ipi_msg *m)
{
    unsigned int i;
    for (i = 0; i < GPUFREQ_IPI_MSG_WORDS; i++)
        m->w[i] = GPUEB_MMIO_READ32(sram_va + GPUEB_GPUFREQ_RX_OFF + i * 4u);
}

/* 7.7 一次性构造两条命令的消息（编译期常量友好） */
static inline void
gpueb_build_init_shared_mem(union gpufreq_ipi_msg *m,
                            unsigned long long status_pa,
                            unsigned long long debug_pa,
                            unsigned int status_sz,
                            unsigned int debug_sz)
{
    unsigned int i;
    for (i = 0; i < GPUFREQ_IPI_MSG_WORDS; i++)
        m->w[i] = 0;
    m->f.cmd_id               = (unsigned int)CMD_INIT_SHARED_MEM;
    m->f.target               = (unsigned int)TARGET_DEFAULT;
    m->f.u.addr.status_base   = status_pa;
    m->f.u.addr.debug_base    = debug_pa;
    m->f.u.addr.status_size   = status_sz;
    m->f.u.addr.debug_size    = debug_sz;
}

static inline void
gpueb_build_power_control(union gpufreq_ipi_msg *m, int power_on)
{
    unsigned int i;
    for (i = 0; i < GPUFREQ_IPI_MSG_WORDS; i++)
        m->w[i] = 0;
    m->f.cmd_id             = (unsigned int)CMD_POWER_CONTROL;
    m->f.target             = (unsigned int)TARGET_DEFAULT;
    m->f.u.power_state      = power_on ? (unsigned int)POWER_ON
                                       : (unsigned int)POWER_OFF;
}

/* 7.8 应答解析 */
static inline int
gpueb_ack_return(const union gpufreq_ipi_msg *m)
{
    return m->f.u.return_value;      /* w[2]，有符号 */
}

/* 7.9 共享状态读取（sram_va = 共享内存 status 区映射地址） */
static inline unsigned int
gpueb_status_read(unsigned long status_va, unsigned int off)
{
    return GPUEB_MMIO_READ32(status_va + off);
}

/* ═══════════════════════════════════════════════════════════════════════
 * 8. 需要调用方实现的高层函数（原型；实现见报告 §10）
 * ═══════════════════════════════════════════════════════════════════════ */

/*
 * 一次完整的请求-应答事务。纯轮询，零中断。
 *
 *   sram_va   GPUEB_MBOX_SRAM_PHYS 的映射
 *   ctl_va    GPUEB_MBOX_SEND_PHYS  的映射（四个寄存器相邻，一次映射 0x80 够）
 *   msg       8 个字，函数内部会写 TX slot
 *   ack       输出，收到应答时填 ack_return()；可传 GPUEB_NULL
 *   now_ms    调用方提供的毫秒时钟
 *   timeout_ms 单次事务总超时
 *
 * 返回 GPUEB_OK / GPUEB_ETXSTUCK / GPUEB_ETIMEOUT
 *
 * 失败语义：
 *   ETXSTUCK  — 未写任何东西，无副作用
 *   ETIMEOUT  — 已写 SRAM + 已触发，函数内部会 gpueb_irq_clear() 回滚
 */
static inline int
gpueb_ipi_xfer(unsigned long sram_va,
               unsigned long ctl_va,
               const union gpufreq_ipi_msg *msg,
               int *ack,
               unsigned int (*now_ms)(void),
               unsigned int timeout_ms)
{
    unsigned int deadline;

    /* 1. 等 TX 空闲 */
    deadline = now_ms() + timeout_ms;
    while (gpueb_tx_busy(ctl_va)) {
        if ((int)(now_ms() - deadline) >= 0)
            return GPUEB_ETXSTUCK;
        GPUEB_UDELAY(IPI_POLL_INTERVAL_US);
    }

    /* 2. 写 SRAM */
    gpueb_msg_write(sram_va, msg);

    /* 3. 屏障 —— 必须 */
    GPUEB_BARRIER();

    /* 4. 触发 */
    gpueb_irq_raise(ctl_va);

    /* 5. 轮询 RX */
    deadline = now_ms() + timeout_ms;
    for (;;) {
        if (gpueb_rx_pending(ctl_va))
            break;
        if ((int)(now_ms() - deadline) >= 0) {
            gpueb_irq_clear(ctl_va);      /* 回滚，避免下次误判 */
            return GPUEB_ETIMEOUT;
        }
        GPUEB_UDELAY(IPI_POLL_INTERVAL_US);
    }

    /* 6. 读应答（先读后清，跟 [ML]） */
    {
        union gpufreq_ipi_msg rx;
        gpueb_msg_read(sram_va, &rx);
        if (ack)
            *ack = gpueb_ack_return(&rx);
    }

    /* 7. 清位 */
    gpueb_irq_clear(ctl_va);

    return GPUEB_OK;
}

/* ═══════════════════════════════════════════════════════════════════════
 * 9. 调试辅助（可选，编译期开关）
 * ═══════════════════════════════════════════════════════════════════════ */

#ifdef GPUEB_DEBUG
/*
 * 打印共享状态区前 32 个字。这是「零风险观测面板」：
 * 0x7c600000 是普通 DRAM 保留区，读它不会挂机。
 */
static inline void
gpueb_dump_status(unsigned long status_va, void (*pr)(const char *, ...))
{
    static const char *const names[32] = {
        "cur_oppidx_gpu", "cur_oppidx_stack", "opp_num_gpu", "opp_num_stack",
        "cur_fgpu", "cur_fstack", "cur_vgpu", "cur_vstack",
        "cur_vsram_gpu", "cur_vsram_stack", "cur_power_gpu", "cur_power_stack",
        "max_power_gpu", "max_power_stack", "min_power_gpu", "min_power_stack",
        "cur_ceiling_gpu", "cur_floor_gpu", "cur_ceiling_stack", "cur_floor_stack",
        "cur_c_limiter_gpu", "cur_f_limiter_gpu",
        "cur_c_limiter_stack", "cur_f_limiter_stack",
        "power_control", "dvfs_state", "shader_present", "power_count",
        "aging_enable", "avs_enable", "sb_version", "ptp_version"
    };
    unsigned int i;
    for (i = 0; i < 32; i++)
        pr("gpueb[%02u] %-20s = 0x%08x (%d)\n", i, names[i],
           GPUEB_MMIO_READ32(status_va + i * 4u),
           (int)GPUEB_MMIO_READ32(status_va + i * 4u));
}
#endif /* GPUEB_DEBUG */

#endif /* _GPUEB_IPI_H_ */
