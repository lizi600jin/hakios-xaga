/*
 * hakios_mt6895_wireless_addrs.h — 自动生成，勿手改。
 * 来源：Cyber-Mobile/xaga-qemu（hw/arm/mt6895.c、hw/misc/mtk_dpmaif.c、
 *       hw/misc/mtk_scpsys.c）
 * 生成器：tools/gen_xaga_wireless_addrs.py
 *
 * 这些地址是 MT6895 的板级事实，不是从别的机型抄的：qemu 里对它们做过
 * External Abort 与 probe 校验。写进 XNU 侧驱动前请先用真机 Android 的
 * /proc/device-tree/reserved-memory 与 /sys/kernel/debug/regmap 复核一遍。
 */
#ifndef HAKIOS_MT6895_WIRELESS_ADDRS_H
#define HAKIOS_MT6895_WIRELESS_ADDRS_H

/* ---- MMIO 窗口表（X-macro，展开成任意形状都不产生未用告警） ---- */
#define XAGA_MMIO_WINDOWS(X) \
    X(DPMAIF         , 0x1022C000ULL,  0x1000) /* dpmaif */ \
    X(DPMAIF_2       , 0x1022E000ULL,  0x1000) /* dpmaif */ \
    X(INFRA_DPMAIF   , 0x1022F000ULL,  0x1000) /* infra_dpmaif */ \
    X(BTIF           , 0x1100C000ULL,   0x100) /* btif */ \
    X(BTIF_2         , 0x11300B00ULL,    0x80) /* btif */ \
    X(BTIF_3         , 0x11300B80ULL,    0x80) /* btif */ \
    X(CONSYS         , 0x11C30000ULL,   0xA14) /* consys */ \
    X(CONSYS_2       , 0x11EA0000ULL,   0xA04) /* consys */ \
    X(GPS            , 0x18000000ULL, 0x100000) /* gps */ \
    X(CONSYS_3       , 0x18001000ULL,   0x658) /* consys */ \
    X(CONSYS_4       , 0x18003000ULL,   0x204) /* consys */ \
    X(CONSYS_5       , 0x1800E000ULL,   0x124) /* consys */ \
    X(CONSYS_6       , 0x18011000ULL,   0x138) /* consys */ \
    X(CONSYS_7       , 0x18012000ULL,    0x98) /* consys */ \
    X(CONSYS_8       , 0x18020000ULL,    0x4C) /* consys */ \
    X(CONSYS_9       , 0x18023000ULL,   0xE28) /* consys */ \
    X(CONSYS_10      , 0x1803B000ULL,    0x18) /* consys */ \
    X(CONSYS_11      , 0x18040000ULL,    0x2C) /* consys */ \
    X(CONSYS_12      , 0x18041000ULL,   0x128) /* consys */ \
    X(CONSYS_13      , 0x18042000ULL,   0x324) /* consys */ \
    X(CONSYS_14      , 0x1804B000ULL,   0x414) /* consys */ \
    X(CONSYS_15      , 0x1804D000ULL,   0x41C) /* consys */ \
    X(CONSYS_16      , 0x1804F000ULL,   0x148) /* consys */ \
    X(BT             , 0x18051000ULL,  0x1000) /* bt */ \
    X(CONSYS_17      , 0x18053800ULL,  0x1000) /* consys */ \
    X(CONSYS_18      , 0x18060000ULL,   0xC08) /* consys */ \
    X(CONSYS_19      , 0x18070000ULL,  0x8004) /* consys */ \
    X(BT_2           , 0x18800000ULL,  0x1000) /* bt */ \
    X(BT_3           , 0x18812000ULL,  0x1000) /* bt */ \
    X(WF_TOP         , 0x18400000ULL,  0x1000) /* consys-wf-top */ \
    X(WF_CFG         , 0x184B0000ULL, 0x10000) /* consys-wf-cfg */ \
    X(WF_MCU         , 0x184C0000ULL, 0x10000) /* consys-wf-mcu */ \
    X(WF_MISC        , 0x184F0000ULL, 0x20000) /* consys-wf-misc */ \
    X(BGF_CFG        , 0x18820000ULL,  0x2000) /* consys-bgf-cfg */ \
    X(BGF_SRAM       , 0x18830000ULL, 0x30000) /* consys-bgf-sram */ \
    X(BGF_TOP        , 0x18900000ULL,  0x1000) /* consys-bgf-top */ \
    X(GPS_2          , 0x18C00000ULL, 0x100000) /* gps */ \
    X(GPS_3          , 0x1C000000ULL,  0x1000) /* gps */

#define XAGA_MMIO_WINDOW_COUNT 38

/* ---- 同一批窗口的单值宏（X-macro 之外直取用） ---- */
#define XAGA_WIN_DPMAIF_BASE 0x1022C000ULL
#define XAGA_WIN_DPMAIF_SIZE 0x1000
#define XAGA_WIN_DPMAIF_2_BASE 0x1022E000ULL
#define XAGA_WIN_DPMAIF_2_SIZE 0x1000
#define XAGA_WIN_INFRA_DPMAIF_BASE 0x1022F000ULL
#define XAGA_WIN_INFRA_DPMAIF_SIZE 0x1000
#define XAGA_WIN_BTIF_BASE 0x1100C000ULL
#define XAGA_WIN_BTIF_SIZE 0x100
#define XAGA_WIN_BTIF_2_BASE 0x11300B00ULL
#define XAGA_WIN_BTIF_2_SIZE 0x80
#define XAGA_WIN_BTIF_3_BASE 0x11300B80ULL
#define XAGA_WIN_BTIF_3_SIZE 0x80
#define XAGA_WIN_CONSYS_BASE 0x11C30000ULL
#define XAGA_WIN_CONSYS_SIZE 0xA14
#define XAGA_WIN_CONSYS_2_BASE 0x11EA0000ULL
#define XAGA_WIN_CONSYS_2_SIZE 0xA04
#define XAGA_WIN_GPS_BASE 0x18000000ULL
#define XAGA_WIN_GPS_SIZE 0x100000
#define XAGA_WIN_CONSYS_3_BASE 0x18001000ULL
#define XAGA_WIN_CONSYS_3_SIZE 0x658
#define XAGA_WIN_CONSYS_4_BASE 0x18003000ULL
#define XAGA_WIN_CONSYS_4_SIZE 0x204
#define XAGA_WIN_CONSYS_5_BASE 0x1800E000ULL
#define XAGA_WIN_CONSYS_5_SIZE 0x124
#define XAGA_WIN_CONSYS_6_BASE 0x18011000ULL
#define XAGA_WIN_CONSYS_6_SIZE 0x138
#define XAGA_WIN_CONSYS_7_BASE 0x18012000ULL
#define XAGA_WIN_CONSYS_7_SIZE 0x98
#define XAGA_WIN_CONSYS_8_BASE 0x18020000ULL
#define XAGA_WIN_CONSYS_8_SIZE 0x4C
#define XAGA_WIN_CONSYS_9_BASE 0x18023000ULL
#define XAGA_WIN_CONSYS_9_SIZE 0xE28
#define XAGA_WIN_CONSYS_10_BASE 0x1803B000ULL
#define XAGA_WIN_CONSYS_10_SIZE 0x18
#define XAGA_WIN_CONSYS_11_BASE 0x18040000ULL
#define XAGA_WIN_CONSYS_11_SIZE 0x2C
#define XAGA_WIN_CONSYS_12_BASE 0x18041000ULL
#define XAGA_WIN_CONSYS_12_SIZE 0x128
#define XAGA_WIN_CONSYS_13_BASE 0x18042000ULL
#define XAGA_WIN_CONSYS_13_SIZE 0x324
#define XAGA_WIN_CONSYS_14_BASE 0x1804B000ULL
#define XAGA_WIN_CONSYS_14_SIZE 0x414
#define XAGA_WIN_CONSYS_15_BASE 0x1804D000ULL
#define XAGA_WIN_CONSYS_15_SIZE 0x41C
#define XAGA_WIN_CONSYS_16_BASE 0x1804F000ULL
#define XAGA_WIN_CONSYS_16_SIZE 0x148
#define XAGA_WIN_BT_BASE 0x18051000ULL
#define XAGA_WIN_BT_SIZE 0x1000
#define XAGA_WIN_CONSYS_17_BASE 0x18053800ULL
#define XAGA_WIN_CONSYS_17_SIZE 0x1000
#define XAGA_WIN_CONSYS_18_BASE 0x18060000ULL
#define XAGA_WIN_CONSYS_18_SIZE 0xC08
#define XAGA_WIN_CONSYS_19_BASE 0x18070000ULL
#define XAGA_WIN_CONSYS_19_SIZE 0x8004
#define XAGA_WIN_BT_2_BASE 0x18800000ULL
#define XAGA_WIN_BT_2_SIZE 0x1000
#define XAGA_WIN_BT_3_BASE 0x18812000ULL
#define XAGA_WIN_BT_3_SIZE 0x1000
#define XAGA_WIN_WF_TOP_BASE 0x18400000ULL
#define XAGA_WIN_WF_TOP_SIZE 0x1000
#define XAGA_WIN_WF_CFG_BASE 0x184B0000ULL
#define XAGA_WIN_WF_CFG_SIZE 0x10000
#define XAGA_WIN_WF_MCU_BASE 0x184C0000ULL
#define XAGA_WIN_WF_MCU_SIZE 0x10000
#define XAGA_WIN_WF_MISC_BASE 0x184F0000ULL
#define XAGA_WIN_WF_MISC_SIZE 0x20000
#define XAGA_WIN_BGF_CFG_BASE 0x18820000ULL
#define XAGA_WIN_BGF_CFG_SIZE 0x2000
#define XAGA_WIN_BGF_SRAM_BASE 0x18830000ULL
#define XAGA_WIN_BGF_SRAM_SIZE 0x30000
#define XAGA_WIN_BGF_TOP_BASE 0x18900000ULL
#define XAGA_WIN_BGF_TOP_SIZE 0x1000
#define XAGA_WIN_GPS_2_BASE 0x18C00000ULL
#define XAGA_WIN_GPS_2_SIZE 0x100000
#define XAGA_WIN_GPS_3_BASE 0x1C000000ULL
#define XAGA_WIN_GPS_3_SIZE 0x1000

/* ---- DPMAIF 寄存器（hw/misc/mtk_dpmaif.c） ---- */
#define XAGA_DPMAIF_AO_UL_INT_MASK                     (0x080U)
#define XAGA_DPMAIF_AO_UL_INT_MASK_CLR                 (0x084U)
#define XAGA_DPMAIF_AO_UL_INT_MASK_SET                 (0x088U)
#define XAGA_DPMAIF_AO_DL_INT_MASK                     (0x090U)
#define XAGA_DPMAIF_AO_DL_INT_MASK_CLR                 (0x094U)
#define XAGA_DPMAIF_AO_DL_INT_MASK_SET                 (0x098U)
#define XAGA_DPMAIF_PD_UL_IDLE_CHECK                   (0x088U)
#define XAGA_DPMAIF_PD_UL_ADD_WCNT_START               (0x0b0U)
#define XAGA_DPMAIF_PD_UL_ADD_WCNT_END                 (0x0bcU)
#define XAGA_DPMAIF_PD_DL_BAT_INIT_DONE                (0x100U)
#define XAGA_DPMAIF_PD_DL_ADD_BAT_CNT                  (0x104U)
#define XAGA_DPMAIF_PD_DL_PIT_INIT_DONE                (0x120U)
#define XAGA_DPMAIF_PD_DL_ADD_PIT_CNT                  (0x124U)
#define XAGA_DPMAIF_PD_DL_IDLE_CHECK                   (0x1b4U)
#define XAGA_DPMAIF_PD_MISC_AP_L2TISAR0                (0x400U)
#define XAGA_DPMAIF_PD_MISC_AP_IP_BUSY                 (0x450U)
#define XAGA_DPMAIF_PD_MISC_CG_EN_STATUS               (0x460U)
#define XAGA_DPMAIF_PD_MISC_HW_INIT_DONE               (0x490U)
#define XAGA_DPMAIF_PD_MISC_SRAM_INIT                  (0x494U)
#define XAGA_DPMAIF_PD_MISC_DL_RESTORE_SET             (0x498U)

/* ---- SCPSYS 寄存器（hw/misc/mtk_scpsys.c） ---- */
#define XAGA_SCPSYS_SCPSYS_STATUS_MASK                 (0xc0000000U)
#define XAGA_SCPSYS_SCPSYS_WRITABLE_MASK               (0x00000fffU)
#define XAGA_SCPSYS_SCPSYS_PWR_STATUS_OFF              (0x0f34U)
#define XAGA_SCPSYS_SCPSYS_PWR_STATUS2_OFF             (0x0f38U)
#define XAGA_SCPSYS_SCPSYS_XPU_STATUS_OFF              (0x0f3cU)
#define XAGA_SCPSYS_SCPSYS_XPU_STATUS2_OFF             (0x0f40U)
#define XAGA_SCPSYS_MCUPM_HANDSHAKE_OFFSET             (0x0eb4U)
#define XAGA_SCPSYS_MCUPM_HANDSHAKE_MASK               (0xc0002000U)
#define XAGA_SCPSYS_SPM_PDN_REQ_OFFSET                 (0x0c34U)
#define XAGA_SCPSYS_SPM_PDN_ACK_OFFSET                 (0x0c3cU)
#define XAGA_SCPSYS_SPM_PDN_MASK                       (0x00028000U)
#define XAGA_SCPSYS_SPM_MD_EXT_BUCK_ISO_OFF            (0x0404U)

#endif /* HAKIOS_MT6895_WIRELESS_ADDRS_H */
