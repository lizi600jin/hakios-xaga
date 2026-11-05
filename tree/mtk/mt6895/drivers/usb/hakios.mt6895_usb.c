/*
 * The USB device port of the Redmi Note 11T Pro (xaga), on the MT6895's MTU3 controller.
 * Only with boot-arg usb=2: every address below belongs to this phone.
 *
 * Why this is written from scratch: nothing in this tree has a USB stack. `find xnu-src -ipath
 * "*usb*"` returns nothing, xnu/files has no usb header, and drivers/ has no usb directory -- so
 * unlike the touch screen, there is no file here to correct. The references that matter are
 *
 *  1. Linux drivers/usb/mtu3/ (6794 lines) -- the register map, the device-mode bring-up order
 *     and the EP0 state machine, all of which this file follows. mtu3_hw_regs.h is the authority
 *     for every offset and bit below.
 *  2. The phone's own LK (work-xaga/dump/lk_a.img): a *complete, working* MTU3 device-mode
 *     implementation -- U2 pullup, EP0 SETUP handling, the QMU ring -- and it is what fastboot
 *     runs. "[USB] U2 pullup D%s", "[USB] SETUP packet len %d != 8?", "[USB][QMU] %s Active
 *     Now!" are its strings. Same SoC, same registers, same QMU.
 *  3. The sweet port's XNU-side USB patches (/tmp/sweet/xnu/patches/0072..0081): not the
 *     registers (that is dwc3) but the shape of a gadget inside this kernel -- how it hands
 *     mbufs to ifnet, how it reports link state, how it copies the log out.
 *
 * Two stages, because getting the controller to answer at all is the part that can fail in ways
 * nothing else reports on. This file is stage one: it takes the port over far enough to exchange
 * control transfers, and it prints every SETUP packet it sees. A host that enumerates us proves
 * the clocking, the phy, the pullup and the EP0 FIFO path all work; the descriptor set is a plain
 * CDC-ECM function so that what the host asks for is the same in both stages. Stage two turns the
 * bulk endpoints the descriptor promises into a network link for sys/hakios.net.h.
 *
 * The starting position is deliberately the one the loader leaves, the way hakios.ufshc.c inherits
 * LK's UTP task list instead of building its own. On this phone the loader has *already* brought
 * the controller up for fastboot and never shut it down: usb_probe() reads
 *
 *   ip version 0x1006 (gen2 qmu); dev cap 0x101 (u3 ports 1)
 *   pw sts1 0x1003d0f [sys125 1 u2mac 0 ref 1 syspll 1 sleep 0]; sts2 0x1; pw2 0
 *   u2 ctrl 0x48 [pdn 0 dis 0 otg 0]; otg sts 0 [vbus valid 0]
 *   device conf 0x6000000 [speed 0 addr 6]; ep0 csr 0x40; misc 0x80000003
 *   phy acr0 0x1e1e1900 [intr en 0]
 *
 * -- clocks running, device domain powered, the port out of reset, and MISC_CTRL's VBUS_FRC_EN
 * set, which is the loader forcing VBUS present so that fastboot works on a charge-only cable
 * (the controller will not pull up D+ without it). Since the port is already live, this driver
 * does not reset it; it clears the stale address, arms EP0, and pulls up.
 */
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <pexpert/pexpert.h>
#include <sys/hakios.input.h>
#include <sys/systm.h>

extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

/* The three windows the device tree gives this controller (xaga_live.dts: usb0@11201000). */
#define USB_MAC_PHYS    0x11201000      /* reg-names "mac", 0x2e00 bytes */
#define USB_MAC_SIZE    0x2e00
#define USB_IPPC_PHYS   0x11203e00      /* reg-names "ippc", 0x100 bytes */
#define USB_IPPC_SIZE   0x100

/*
 * Two experiments that each look plausible and that the register dumps rule out. Both are kept
 * because the reasoning is what matters if this ever has to be revisited: a device reset and a
 * port power cycle both look like the responsible thing to do when taking a controller over, and
 * both destroy state this port needs -- including VBUS_VALID, which software cannot restore.
 */
#ifndef USB_DEVICE_RESET
#define USB_DEVICE_RESET 0
#endif
#ifndef USB_PORT_CYCLE
#define USB_PORT_CYCLE 0
#endif

/*
 * The vendor's own cold start: ssusb_ip_sw_reset() then mtu3_hw_init().
 *
 * Everything tried so far has been "configure the state the loader left behind", and the loader
 * leaves a device port that has been deliberately disconnected (power mgmt 0x60a1, softconn 0)
 * with the phy's DTM1 cleared. Android never inherits that state, because on a normal boot LK
 * hands over to a kernel that runs ssusb_ip_sw_reset() + mtu3_hw_init() from scratch. The switch
 * below does the same: reset the whole IP (xHCI and U3D), power the device domain back up, then
 * rebuild the controller the way mtu3_core.c does.
 */
#ifndef USB_IP_SW_RESET
#define USB_IP_SW_RESET 0
#endif
#ifndef USB_PHY_DTM0_RESET
#define USB_PHY_DTM0_RESET 1
#endif

/*
 * Switch the U2 PLL on. Nothing in phy-mtk-xsphy.c does, and the frequency meter has been
 * returning fm_out = 0 on every run since this driver first ran, which says the ring oscillator
 * it measures was never running.
 */
#ifndef USB_PHY_PLL
#define USB_PHY_PLL 1
#endif

#ifndef USB_PHY_BRING_UP
#define USB_PHY_BRING_UP 1
#endif

/* The USB2 phy of port 0 (xaga_live.dts: usb2-phy@11e40000, a mediatek,xsphy). */
/* the Type-C DP/USB selector: bit 11 chooses, per usb_dp_selector.ko's switch_set(1)/(2) */
#define USB_DP_SEL_PHYS		0x10005600u
#define USB_DP_SEL_SIZE		0x10u
#define USB_DP_SEL_USB		(1u << 11)

#define USB_PHY_PHYS    0x11e40000
#define USB_PHY_SIZE    0x400

/* ---- MAC registers, relative to mac_base. mtu3_hw_regs.h:14-18 ---- */
#define SSUSB_DEV_BASE          0x0000u
#define SSUSB_EPCTL_CSR_BASE    0x0800u
#define SSUSB_USB3_SYS_CSR_BASE 0x1400u
#define SSUSB_USB2_CSR_BASE     0x2400u

#define U3D_LV1ISR       (SSUSB_DEV_BASE + 0x0000u)
#define U3D_LV1IER       (SSUSB_DEV_BASE + 0x0004u)
#define U3D_LV1IESR      (SSUSB_DEV_BASE + 0x0008u)
#define U3D_LV1IECR      (SSUSB_DEV_BASE + 0x000cu)
#define U3D_EPISR        (SSUSB_DEV_BASE + 0x0080u)
#define U3D_EPIER        (SSUSB_DEV_BASE + 0x0084u)
#define U3D_EPIECR       (SSUSB_DEV_BASE + 0x008cu)
#define U3D_EPIESR       (SSUSB_DEV_BASE + 0x0088u)   /* enable-set; mtu3_hw_regs.h:32 */
#define U3D_EP0CSR       (SSUSB_DEV_BASE + 0x0100u)
#define U3D_RXCOUNT0     (SSUSB_DEV_BASE + 0x0108u)
#define U3D_FIFO0        (SSUSB_DEV_BASE + 0x0300u)
#define U3D_CAP_EP0FFSZ  (SSUSB_DEV_BASE + 0x0c04u)
#define U3D_CAP_EPINFO   (SSUSB_DEV_BASE + 0x0c10u)
#define U3D_MISC_CTRL    (SSUSB_DEV_BASE + 0x0c84u)

#define U3D_DEVICE_CONF          (SSUSB_EPCTL_CSR_BASE + 0x0000u)
#define U3D_EP_RST               (SSUSB_EPCTL_CSR_BASE + 0x0004u)
#define U3D_DEV_LINK_INTR_ENABLE (SSUSB_EPCTL_CSR_BASE + 0x0050u)
#define U3D_DEV_LINK_INTR        (SSUSB_EPCTL_CSR_BASE + 0x0054u)

#define U3D_LTSSM_CTRL           (SSUSB_USB3_SYS_CSR_BASE + 0x0010u)
#define U3D_USB3_CONFIG          (SSUSB_USB3_SYS_CSR_BASE + 0x001cu)
#define U3D_LINK_STATE_MACHINE   (SSUSB_USB3_SYS_CSR_BASE + 0x0134u)
#define U3D_LINK_POWER_CONTROL   (SSUSB_USB3_SYS_CSR_BASE + 0x0210u)
#define U3D_U3U2_SWITCH_CTRL     (SSUSB_USB3_SYS_CSR_BASE + 0x0170u)
#define U3D_POWER_MANAGEMENT     (SSUSB_USB2_CSR_BASE + 0x0004u)
#define U3D_DEVICE_CONTROL       (SSUSB_USB2_CSR_BASE + 0x000cu)
#define U3D_USB2_TEST_MODE       (SSUSB_USB2_CSR_BASE + 0x0014u)
#define U3D_COMMON_USB_INTR_ENABLE (SSUSB_USB2_CSR_BASE + 0x0018u)
#define U3D_COMMON_USB_INTR      (SSUSB_USB2_CSR_BASE + 0x001cu)
#define U3D_LINK_RESET_INFO      (SSUSB_USB2_CSR_BASE + 0x0024u)
#define U3D_USB20_LPM_PARAMETER  (SSUSB_USB2_CSR_BASE + 0x0044u)
#define U3D_QFCR                 (SSUSB_DEV_BASE + 0x0428u)
#define U3D_USB20_MISC_CONTROL   (SSUSB_USB2_CSR_BASE + 0x004cu)
#define U3D_USB20_OPSTATE        (SSUSB_USB2_CSR_BASE + 0x0060u)

/* ---- EP0CSR, mtu3_hw_regs.h:110-124 ---- */
#define EP0_SENDSTALL   (1u << 25)
#define EP0_FIFOFULL    (1u << 23)
#define EP0_SENTSTALL   (1u << 22)
#define EP0_DPHTX       (1u << 20)
#define EP0_DATAEND     (1u << 19)
#define EP0_TXPKTRDY    (1u << 18)
#define EP0_SETUPPKTRDY (1u << 17)
#define EP0_RXPKTRDY    (1u << 16)
#define EP0_MAXPKTSZ_MSK 0x3ffu
#define EP0_MAXPKTSZ(x)  ((x) & EP0_MAXPKTSZ_MSK)
#define EP0_W1C_BITS    (~(EP0_RXPKTRDY | EP0_SETUPPKTRDY | EP0_SENTSTALL))
#define EP0ISR          (1u << 0)   /* mtu3_hw_regs.h:110 */
#define SETUPENDISR     (1u << 16)  /* mtu3_hw_regs.h:108 */
#define SSUSB_DEV_SPEED_CHG_INTR (1u << 0)  /* mtu3_hw_regs.h:286 */

/* ---- device conf, EP reset, power management ---- */
#define DEV_ADDR_MSK    (0x7fu << 24)
#define DEV_ADDR(x)     ((0x7fu & (x)) << 24)
#define SSUSB_DEV_SPEED(x) ((x) & 0x7u)
#define EP0_RST         (1u << 0)

#define SUSPENDM_ENABLE (1u << 0)
#define HS_ENABLE       (1u << 5)
#define SOFT_CONN       (1u << 6)
#define LPM_HRWE        (1u << 10)
#define USB3_EN         (1u << 0)
#define LPM_U3_ACK_EN   (1u << 0)       /* U3D_USB20_MISC_CONTROL */

/* U3D_COMMON_USB_INTR / _ENABLE -- the cable-facing interrupts, mtu3_hw_regs.h:428-437 */
#define SUSPEND_INTR    (1u << 0)
#define RESUME_INTR     (1u << 1)
#define RESET_INTR      (1u << 2)
#define SOF_INTR        (1u << 3)
#define CONN_INTR       (1u << 4)
#define DISCONN_INTR    (1u << 5)

#define SW_U1_ACCEPT_ENABLE  (1u << 8)
#define SW_U2_ACCEPT_ENABLE  (1u << 9)
#define SW_U1_REQUEST_ENABLE (1u << 0)
#define SW_U2_REQUEST_ENABLE (1u << 1)
#define SOFT_U3_EXIT_EN      (1u << 2)
#define U2U3_AUTO_SWITCH     (1u << 10)
#define FORCE_FS             (1u << 5)
#define SOFTCON_CLR_AUTO_EN  (1u << 0)
#define WTCHRP_MSK           (0xfu << 16)

/*
 * The LPM fields. These were wrong here for a while -- every shift was off, which is why writing
 * them changed nothing: LPM_BESLDCK landed in LPM_BESLCK's bits and the stall bits were written
 * into the middle of LPM_MODE. mtu3_hw_regs.h:401-445 is the only authority:
 *
 *     LPM_BESL_STALL   BIT(14)          LPM_HRWE        BIT(10)
 *     LPM_BESLD_STALL  BIT(13)          LPM_MODE(x)     (x & 0x3) << 8
 *     LPM_RWP          BIT(11)          SOFT_CONN       BIT(6)
 *     HS_ENABLE        BIT(5)           ISO_UPDATE      BIT(7)
 *     SUSPENDM_ENABLE  BIT(0)           RESUME          BIT(2)
 *
 *     LPM_BESLCK_U3(x) (x & 0xf) << 12
 *     LPM_BESLCK(x)    (x & 0xf) << 8
 *     LPM_BESLDCK(x)   (x & 0xf) << 4
 */
#define LPM_BESLD_STALL   (1u << 13)
#define LPM_BESL_STALL    (1u << 14)
#define LPM_RWP           (1u << 11)
#define LPM_MODE(x)       (((x) & 0x3u) << 8)
#define ISO_UPDATE        (1u << 7)
#define RESUME            (1u << 2)
#define LPM_BESLCK_U3(x)  (((x) & 0xfu) << 12)
#define LPM_BESLCK(x)     (((x) & 0xfu) << 8)
#define LPM_BESLDCK(x)    (((x) & 0xfu) << 4)

/* U3D_DEVICE_CONTROL: the session bit the OTG switch sets. mtu3_dr.c:toggle_opstate() */
#define DC_HOSTREQ       (1u << 1)
#define DC_SESSION       (1u << 0)
#define U2_PORT_RG_IDDIG (1u << 12)
#define U2_PORT_FORCE_IDDIG (1u << 11)

#define SUSPEND_INTR     (1u << 0)
#define RESUME_INTR      (1u << 1)
#define RESET_INTR       (1u << 2)
#define SSUSB_DEV_SPEED_CHG_INTR (1u << 0)

#define MISC_VBUS_ON     (1u << 1)
#define MISC_VBUS_FRC_EN (1u << 0)

/* ---- IPPC registers, mtu3_hw_regs.h:478-541 ---- */
#define IP_PW_CTRL0     0x0000u
#define SSUSB_IP_SW_RST (1u << 0)
#define SSUSB_IP_DEV_PDN (1u << 0)
#define IP_PW_CTRL1     0x0004u
#define IP_PW_CTRL2     0x0008u

#define SSUSB_IP_HOST_PDN (1u << 0)
#define IP_PW_STS1      0x0010u
#define IP_PW_STS2      0x0014u
#define IP_DEV_CAP      0x0028u
#define DEV_RST_CTRL    0x0098u
#define DEV_SW_RST      (1u << 0)
#define U3_CTRL_0P      0x0030u
#define U2_CTRL_0P      0x0050u
#define IP_TRUNK_VERS_REG 0x00a4u      /* U3D_SSUSB_HW_SUB_ID; 0x1006 on this phone */
#define OTG_STS         0x0018u         /* U3D_SSUSB_OTG_STS -- 0x00a8 is the PRB area */

#define IP_SLEEP_STS        (1u << 30)
#define U3_MAC_RST_B_STS    (1u << 16)
#define REF_RST_B_STS       (1u << 8)
#define SYSPLL_STABLE       (1u << 0)
#define SYS125_RST_B_STS    (1u << 10)
#define U2_MAC_SYS_RST_B_STS (1u << 0)

#define U3_PORT_SSP_SPEED (1u << 9)
#define U3_PORT_DUAL_MODE (1u << 7)
#define U3_PORT_HOST_SEL  (1u << 2)
#define U3_PORT_PDN       (1u << 1)
#define U3_PORT_DIS       (1u << 0)

#define U2_PORT_VBUSVALID (1u << 9)
#define U2_PORT_OTG_SEL   (1u << 7)
#define U2_PORT_HOST      (1u << 2)
#define U2_PORT_PDN       (1u << 1)
#define U2_PORT_DIS       (1u << 0)
#define U2_PORT_HOST_SEL  (U2_PORT_VBUSVALID | U2_PORT_HOST)

#define IP_TRUNK_VERS(x)   (((x) >> 16) & 0xffffu)
#define DEV_U3_PORT_NUM(x) ((x) & 0xffu)
#define SSUSB_VBUS_VALID   (1u << 9)

/*
 * ---- USB2 phy registers, relative to phy_base. phy-mtk-xsphy.c:24-26,49-70 ----
 *
 * These are NOT at the offsets their names suggest. The phy has three banks and the registers that
 * matter sit in the third one:
 *
 *   SSUSB_SIFSLV_MISC      0x000
 *   SSUSB_SIFSLV_U2FREQ    0x100
 *   SSUSB_SIFSLV_U2PHY_COM 0x300   <- every ACRn and DTM1 is an offset inside this
 *
 * which is why the first version of this file wrote 0x018 and read back the same value: that is
 * the MISC bank, where nothing is there (ACR5 read 0xffffffff, the signature of an unimplemented
 * register). The device tree's usb2-phy0@11e40000 reg is <0x11e40000 0x400>, so port_base is
 * phy_base and these offsets apply directly.
 */
#define SSUSB_SIFSLV_U2PHY_COM 0x300u
#define XSP_USBPHYACR0  (SSUSB_SIFSLV_U2PHY_COM + 0x000u)
#define XSP_USBPHYACR1  (SSUSB_SIFSLV_U2PHY_COM + 0x004u)
#define XSP_USBPHYACR4  (SSUSB_SIFSLV_U2PHY_COM + 0x020u)
#define XSP_USBPHYACR5  (SSUSB_SIFSLV_U2PHY_COM + 0x014u)
#define XSP_USBPHYACR6  (SSUSB_SIFSLV_U2PHY_COM + 0x018u)
#define XSP_U2PHYDTM0   (SSUSB_SIFSLV_U2PHY_COM + 0x068u)
#define XSP_U2PHYDTM1   (SSUSB_SIFSLV_U2PHY_COM + 0x06cu)

/*
 * The three bits the vendor's power_on touches that phy-mtk-xsphy.c never mentions. Its
 * u2_phy_instance_power_on() is four lines upstream; in phy-mtk-xsphy.ko it is nine register
 * writes plus a 129 ms settle, and three of those writes are what connect the port.
 *
 *   ACR4 bits 8..9   cleared          the "usb20 gpio mode" pair, which parks the pads
 *   DTM0 bits 18, 26 cleared          and DTM0 masked down to 0xff41c30b
 *   ACR6 bit 30      set              upstream only ever writes bit 20 and bit 23 here
 *
 * Read from the module in the TWRP tree (recovery/root/lib/modules/phy-mtk-xsphy.ko), which is
 * the same silicon and therefore a better authority than a driver that has no MT6895 support.
 */
#define P2A4_RG_USB20_GPIO_MSK  0x300u

/* DTM0 fields, from phy-mtk-tphy.c:93-106 (the same registers, the same silicon) */
#define P2D0_FORCE_UART_EN  (1u << 26)
#define P2D0_FORCE_SUSPENDM (1u << 18)
#define P2D0_RG_DATAIN      (0xfu << 10)
#define P2D0_RG_XCVRSEL     (0x3u << 4)
/* P2C_DTM0_PART_MASK, phy-mtk-tphy.c:106-110: the force bits plus the pulldown and termsel bits */
#define P2D0_PART_MASK      ((1u << 23) | (1u << 21) | (1u << 20) | (1u << 19) | \
                             (1u << 17) | (1u << 7) | (1u << 6) | (1u << 2))
#define P2D0_CLR_MSK            0x00be3cf4u    /* bits 18..31 as the vendor leaves them */
#define P2A6_RG_BIT30            (1u << 30)
#define P2D_RG_UART_EN           (1u << 16)

/*
 * The frequency meter sits in its own bank (SSUSB_SIFSLV_U2FREQ 0x100). u2_phy_slew_rate_calibrate()
 * runs a ring oscillator against it to work out what the HS transmitter's slew rate should be. On a
 * freshly reset phy every one of these reads 0, which is how we know the vendor's power_on path has
 * never run here.
 */
#define SSUSB_SIFSLV_U2FREQ   0x100u
#define XSP_U2FREQ_FMCR0   (SSUSB_SIFSLV_U2FREQ + 0x00u)
#define XSP_U2FREQ_MMONR0  (SSUSB_SIFSLV_U2FREQ + 0x0cu)
#define XSP_U2FREQ_FMMONR1 (SSUSB_SIFSLV_U2FREQ + 0x10u)

#define P2F_RG_FREQDET_EN (1u << 24)
#define P2F_RG_CYCLECNT   (0xffffffu)
#define P2F_RG_FRCK_EN    (1u << 8)
#define P2F_USB_FM_VALID  (1u << 0)

#define XSP_REF_CLK          26u
#define XSP_SLEW_RATE_COEF   17u
#define XSP_SR_COEF_DIVISOR  1000u
#define XSP_FM_DET_CYCLE_CNT 1024u

#define P2A0_RG_INTR_EN        (1u << 5)
#define P2A0_RG_U2PLL_FORCE_ON (1u << 15)
#define P2A0_USB20_PLL_PREDIV  (0x3u << 6)

#define XSP_USBPHYACR2         (SSUSB_SIFSLV_U2PHY_COM + 0x008u)
#define P2A2_RG_U2PLL_BW       (0x7u << 19)
#define P2A2_RG_SIF_U2PLL_FORCE_EN (1u << 18)

/* U3P_U2PHYA_RESV and RESV1 hold the U2 PLL divider, phy-mtk-tphy.c:81-87 */
#define XSP_U2PHYA_RESV        (SSUSB_SIFSLV_U2PHY_COM + 0x030u)
#define XSP_U2PHYA_RESV1       (SSUSB_SIFSLV_U2PHY_COM + 0x044u)
#define P2R_RG_U2PLL_FBDIV_26M 0x1bb13bu
#define P2R_RG_U2PLL_REFCLK_SEL (1u << 5)
#define P2R_RG_U2PLL_FRA_EN    (1u << 3)

#define XSP_U2PHYDCR0          (SSUSB_SIFSLV_U2PHY_COM + 0x060u)
#define P2C_RG_SIF_U2PLL_FORCE_ON (1u << 24)
#define P2A1_RG_INTR_CAL       (0x1fu << 19)
#define P2A1_RG_VRT_SEL        (0x7u << 12)
#define P2A1_RG_TERM_SEL       (0x7u << 8)
#define P2A5_RG_HSTX_SRCAL_EN  (1u << 15)
#define P2A5_RG_HSTX_SRCTRL    (0x7u << 12)
#define P2A6_RG_BC11_SW_EN     (1u << 23)
#define P2A6_RG_OTG_VBUSCMP_EN (1u << 20)
#define P2D_RG_VBUSVALID       (1u << 5)
#define P2D_RG_SESSEND         (1u << 4)
#define P2D_RG_AVALID          (1u << 2)
#define P2D_FORCE_IDDIG        (1u << 9)
#define P2D_RG_IDDIG           (1u << 1)

/*
 * The tuning the phone's own device tree gives this phy (xaga_live.dts, usb2-phy0@11e40000):
 * mediatek,efuse-intr = <0x26>, eye-vrt = <0x02>, eye-vrt-host = <0x07>.
 * phy-mtk-xsphy.c:u2_phy_props_set() writes the ones that exist for a USB2 phy -- efuse_intr into
 * ACR1's INTR_CAL, eye_vrt into ACR1's VRT_SEL. (There is no discth property in xsphy; that one
 * belongs to the older mtk-musb phy, and eye-vrt-host only matters in host mode.)
 */
#define BOARD_EFUSE_INTR   0x26u
#define BOARD_EYE_VRT      0x02u

/* ---- the windows ---- */
static vm_offset_t usb_mac;
static vm_offset_t usb_ippc;
static vm_offset_t usb_phy;
static vm_offset_t usb_dp_sel;

static inline uint32_t
mac_rd(uint32_t at)
{
	return *(volatile uint32_t *)(usb_mac + at);
}

static inline void
mac_wr(uint32_t at, uint32_t value)
{
	*(volatile uint32_t *)(usb_mac + at) = value;
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void
mac_set(uint32_t at, uint32_t bits)
{
	mac_wr(at, mac_rd(at) | bits);
}

static void
mac_clear(uint32_t at, uint32_t bits)
{
	mac_wr(at, mac_rd(at) & ~bits);
}

static void
mac_update(uint32_t at, uint32_t mask, uint32_t value)
{
	mac_wr(at, (mac_rd(at) & ~mask) | (value & mask));
}

static inline uint32_t
ippc_rd(uint32_t at)
{
	return *(volatile uint32_t *)(usb_ippc + at);
}

static inline void
ippc_wr(uint32_t at, uint32_t value)
{
	*(volatile uint32_t *)(usb_ippc + at) = value;
	__asm__ volatile ("dsb sy" ::: "memory");
}

static inline uint32_t
phy_rd(uint32_t at)
{
	return *(volatile uint32_t *)(usb_phy + at);
}

static inline void
phy_wr(uint32_t at, uint32_t value)
{
	*(volatile uint32_t *)(usb_phy + at) = value;
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void
phy_set(uint32_t at, uint32_t bits)
{
	phy_wr(at, phy_rd(at) | bits);
}

static void
phy_clear(uint32_t at, uint32_t bits)
{
	phy_wr(at, phy_rd(at) & ~bits);
}

static void
phy_update(uint32_t at, uint32_t mask, uint32_t value)
{
	phy_wr(at, (phy_rd(at) & ~mask) | (value & mask));
}

/* ---- the EP0 FIFO: the one endpoint the CPU moves by hand ---- */

static void
ep0_read_fifo(void *dst, uint32_t length)
{
	volatile uint32_t *fifo = (volatile uint32_t *)(usb_mac + U3D_FIFO0);
	uint8_t *out = dst;
	uint32_t words = length >> 2;
	uint32_t index = 0;

	/* mtu3_gadget_ep0.c:ep0_read_fifo(), the same split, for the same reason */
	for (uint32_t i = 0; i < words; i++) {
		uint32_t value = fifo[0];
		out[index + 0] = (uint8_t)(value & 0xff);
		out[index + 1] = (uint8_t)((value >> 8) & 0xff);
		out[index + 2] = (uint8_t)((value >> 16) & 0xff);
		out[index + 3] = (uint8_t)((value >> 24) & 0xff);
		index += 4;
	}
	if ((length & 0x3u) != 0) {
		uint32_t value = fifo[0];
		for (uint32_t i = 0; i < (length & 0x3u); i++) {
			out[index + i] = (uint8_t)((value >> (8 * i)) & 0xff);
		}
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void
ep0_write_fifo(const void *src, uint32_t length)
{
	volatile uint32_t *fifo = (volatile uint32_t *)(usb_mac + U3D_FIFO0);
	const uint8_t *in = src;
	uint32_t words = length >> 2;
	uint32_t index = 0;

	for (uint32_t i = 0; i < words; i++) {
		uint32_t value = (uint32_t)in[index] |
		    ((uint32_t)in[index + 1] << 8) |
		    ((uint32_t)in[index + 2] << 16) |
		    ((uint32_t)in[index + 3] << 24);
		fifo[0] = value;
		index += 4;
	}
	if ((length & 0x3u) != 0) {
		uint32_t value = 0;
		for (uint32_t i = 0; i < (length & 0x3u); i++) {
			value |= (uint32_t)in[index + i] << (8 * i);
		}
		fifo[0] = value;
	}
	__asm__ volatile ("dsb sy" ::: "memory");
}

/* ---- the descriptors ----
 *
 * A CDC-ECM function (USB CDC 1.2 section 5.4 / ECM 1.2 section 5.2), which is what a host binds
 * without a driver of its own: Linux cdc_ether, macOS AppleUSBCDCECMData, Windows usbnet. The
 * three interface numbers, the two endpoint addresses and the string indices are all fixed here,
 * so the same bytes answer the same questions in both stages.
 *
 *   ep0         control
 *   ep1 IN      the interrupt endpoint the CDC communications interface requires, 16 bytes
 *   ep2 OUT     bulk, the host's frames
 *   ep2 IN      bulk, ours
 */
#define USB_EP0_MAXPACKET_HS    64

struct usb_device_descriptor_local {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
} __attribute__((packed));

/*
 * The fastboot identity, on purpose, for this round.
 *
 * The phone reaches this host through a VMware USB arbitrator, and the only two identities that
 * arbitrator has ever handed over are the loader's 18d1:d00d and Android's 2717:ff08. A brand new
 * 1209:0a10 is exactly the case where nothing arrives at the guest and the guest-side dmesg stays
 * silent, which is what has been happening. Borrowing fastboot's numbers makes the arbitrator's
 * known-good path the one under test; a real vendor id can go back in once enumeration is seen.
 */
#define HAKIOS_USB_VID 0x18d1
#define HAKIOS_USB_PID 0xd00d

static const struct usb_device_descriptor_local usb_device_descriptor = {
	.bLength = 18,
	.bDescriptorType = 0x01,
	.bcdUSB = 0x0200,
	.bDeviceClass = 0x02,           /* CDC, at the device level */
	.bDeviceSubClass = 0x00,
	.bDeviceProtocol = 0x00,
	.bMaxPacketSize0 = USB_EP0_MAXPACKET_HS,
	.idVendor = HAKIOS_USB_VID,
	.idProduct = HAKIOS_USB_PID,
	.bcdDevice = 0x0100,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,
};

/*
 * The whole configuration, in the order the host walks it: configuration, the communications
 * interface with its header and call-management descriptors, the data interface, then the three
 * endpoints. bmCapabilities = 0x00 on the call management descriptor says the device does not
 * multiplex: one ECM channel, which is the only thing a link needs.
 */
static const uint8_t usb_configuration[] = {
	/* configuration descriptor */
	0x09, 0x02,                     /* bLength, CONFIGURATION */
	0x4c, 0x00,                     /* wTotalLength = 76 */
	0x02,                           /* bNumInterfaces = 2 */
	0x01,                           /* bConfigurationValue */
	0x00,                           /* iConfiguration */
	0x80,                           /* bmAttributes: bus powered */
	0x32,                           /* bMaxPower = 100 mA */

	/* interface 0: the communications class interface */
	0x09, 0x04, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0x00,
	/* CDC header functional descriptor */
	0x05, 0x24, 0x00, 0x10, 0x01,
	/* CDC call management functional descriptor: no call management of its own */
	0x05, 0x24, 0x01, 0x00, 0x01,
	/* CDC abstract control management: line coding, send break, network connection */
	0x04, 0x24, 0x02, 0x06,
	/* CDC union: interface 0 governs interface 1 */
	0x05, 0x24, 0x06, 0x00, 0x01,
	/* the interrupt endpoint the communications interface needs, though nothing is sent on it */
	0x07, 0x05, 0x81, 0x03, 0x10, 0x00, 0x10,

	/* interface 1, alternate 0: the data interface with no endpoints, which is what a host
	 * selects first and what it falls back to when it stops the link */
	0x09, 0x04, 0x01, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x00,
	/* interface 1, alternate 1: where the bulk pair lives, 512 bytes each (high speed) */
	0x09, 0x04, 0x01, 0x01, 0x02, 0x0a, 0x00, 0x00, 0x00,
	0x07, 0x05, 0x02, 0x02, 0x00, 0x02, 0x00,
	0x07, 0x05, 0x82, 0x02, 0x00, 0x02, 0x00,
};

/* The strings. 0x0409 is English (United States), which every host accepts. */
static const uint8_t usb_string_zero[] = {
	0x04, 0x03, 0x09, 0x04,
};

static const char *const usb_strings[] = {
	NULL,                           /* 0 is the language list */
	"HakiOS",                       /* 1, manufacturer */
	"HakiOS phone",                 /* 2, product */
	"HAKIOS0001",                   /* 3, serial */
	"HAKIOS CDC",                   /* 4, the communications interface */
	"HAKIOS data",                  /* 5, the data interface */
};

/* ---- the control endpoint ---- */

struct usb_setup {
	uint8_t request_type;
	uint8_t request;
	uint16_t value;
	uint16_t index;
	uint16_t length;
} __attribute__((packed));

#define REQ_TYPE_MASK   0x60u
#define REQ_TYPE_STANDARD 0x00u
#define REQ_TYPE_CLASS    0x20u
#define REQ_RECIP_MASK  0x1fu

#define REQ_GET_STATUS        0x00
#define REQ_CLEAR_FEATURE     0x01
#define REQ_SET_FEATURE       0x03
#define REQ_SET_ADDRESS       0x05
#define REQ_GET_DESCRIPTOR    0x06
#define REQ_GET_CONFIGURATION 0x08
#define REQ_SET_CONFIGURATION 0x09
#define REQ_GET_INTERFACE     0x0a
#define REQ_SET_INTERFACE     0x0b

#define DESC_DEVICE        0x01
#define DESC_CONFIGURATION 0x02
#define DESC_STRING        0x03
#define DESC_DEVICE_QUALIFIER 0x06

/* what the host told us, kept for the log and for stage two */
static uint8_t usb_address;
static uint8_t usb_configuration_value;
static uint8_t usb_alternate;
static unsigned usb_setups;
static unsigned usb_descriptors;
static bool usb_configured;

/*
 * Sending an EP0 packet is two register writes and one FIFO fill: load the FIFO, then set
 * TXPKTRDY. The controller clears it when the host has taken the packet, which is what the
 * caller waits for. mtu3_gadget_ep0.c:ep0_tx_state() does the same three steps.
 */
static bool
ep0_send(const void *data, uint32_t length)
{
	uint32_t csr;

	if (length != 0) {
		ep0_write_fifo(data, length);
	}
	csr = mac_rd(U3D_EP0CSR) & EP0_W1C_BITS;

	/*
	 * A short packet ends the transfer; a full one does not. The controller needs to be told
	 * which, and the distinction is EP0_DATAEND: with a zero-length reply the host is waiting
	 * for the status stage, so DATAEND goes with the (empty) packet.
	 */
	if (length < USB_EP0_MAXPACKET_HS) {
		mac_wr(U3D_EP0CSR, csr | EP0_TXPKTRDY | EP0_DATAEND);
	} else {
		mac_wr(U3D_EP0CSR, csr | EP0_TXPKTRDY);
	}

	for (unsigned i = 0; i < 200000u; i++) {
		uint32_t now = mac_rd(U3D_EP0CSR);

		if ((now & EP0_TXPKTRDY) == 0) {
			return true;
		}
	}
	return false;
}

/*
 * The status stage of a transfer whose data went the other way: no payload, just the handshake.
 * ep0_do_status_stage() in the reference driver, verbatim.
 */
static void
ep0_status(void)
{
	uint32_t csr = mac_rd(U3D_EP0CSR) & EP0_W1C_BITS;

	mac_wr(U3D_EP0CSR, csr | EP0_SETUPPKTRDY | EP0_DATAEND);
}

static void
ep0_stall(void)
{
	uint32_t csr = mac_rd(U3D_EP0CSR) & EP0_W1C_BITS;

	mac_wr(U3D_EP0CSR, csr | EP0_SENDSTALL | EP0_SETUPPKTRDY);
}

/* A reply that is shorter than the host asked for, which is how a descriptor is trimmed. */
static uint32_t
reply_length(uint32_t available, uint16_t wanted)
{
	if (wanted == 0 || wanted > available) {
		return available;
	}
	return wanted;
}

static void
handle_get_descriptor(const struct usb_setup *setup)
{
	uint8_t kind = (uint8_t)(setup->value >> 8);
	uint8_t index = (uint8_t)(setup->value & 0xff);

	switch (kind) {
	case DESC_DEVICE:
		ep0_send(&usb_device_descriptor,
		    reply_length(sizeof(usb_device_descriptor), setup->length));
		break;

	case DESC_CONFIGURATION: {
		uint32_t total = sizeof(usb_configuration);

		/*
		 * wLength of 9 is how a host asks for the configuration header alone before it
		 * knows how long the whole thing is; replying with the full set is allowed and
		 * saves a round trip, but a host that asked for 9 and got 75 will ignore the rest.
		 * Replying with exactly what was asked keeps both behaviours happy.
		 */
		ep0_send(usb_configuration, reply_length(total, setup->length));
		break;
	}

	case DESC_STRING:
		if (index == 0) {
			ep0_send(usb_string_zero,
			    reply_length(sizeof(usb_string_zero), setup->length));
		} else if (index < (sizeof(usb_strings) / sizeof(usb_strings[0]))) {
			const char *text = usb_strings[index];
			uint8_t buffer[64];
			uint32_t length = 0;

			while (text[length] != '\0' && (2 * length + 2) < sizeof(buffer)) {
				buffer[2 + 2 * length] = (uint8_t)text[length];
				buffer[3 + 2 * length] = 0;
				length++;
			}
			buffer[0] = (uint8_t)(2 + 2 * length);
			buffer[1] = DESC_STRING;
			ep0_send(buffer, reply_length(2 + 2 * length, setup->length));
		} else {
			ep0_stall();
		}
		break;

	case DESC_DEVICE_QUALIFIER:
		/*
		 * Only a high-speed capable device is asked this, and only when the host wants to
		 * know whether it also does full speed. A stall is the answer the specification
		 * prescribes for a device that has one speed; Linux's composite layer sends a
		 * qualifier that says so, which is friendlier, so this sends one.
		 */
		{
			static const uint8_t qualifier[] = {
				0x0a, 0x06, 0x00, 0x02, 0x00, 0x00, 0x00,
				0x40, 0x01, 0x00,
			};
			ep0_send(qualifier, reply_length(sizeof(qualifier), setup->length));
		}
		break;

	default:
		ep0_stall();
		return;
	}
	usb_descriptors++;
}

static void
handle_setup(const struct usb_setup *setup)
{
	uint8_t type = setup->request_type & REQ_TYPE_MASK;

	usb_setups++;

	/*
	 * Every packet is printed. There are about a dozen in a full enumeration, so the cost is
	 * nothing, and when the host stops half way the last line says exactly where.
	 */
	printf("usb: setup %u: type %02x req %02x value %04x index %04x length %u\n",
	    usb_setups, setup->request_type, setup->request, setup->value,
	    setup->index, setup->length);

	if (type == REQ_TYPE_STANDARD) {
		switch (setup->request) {
		case REQ_GET_DESCRIPTOR:
			handle_get_descriptor(setup);
			return;

		case REQ_SET_ADDRESS: {
			uint32_t conf;

			usb_address = (uint8_t)(setup->value & 0x7fu);
			conf = mac_rd(U3D_DEVICE_CONF) & ~DEV_ADDR_MSK;
			conf |= DEV_ADDR(usb_address);
			mac_wr(U3D_DEVICE_CONF, conf);
			printf("usb: address %u\n", usb_address);
			ep0_status();
			return;
		}

		case REQ_SET_CONFIGURATION:
			usb_configuration_value = (uint8_t)(setup->value & 0xff);
			usb_configured = usb_configuration_value != 0;
			printf("usb: configuration %u\n", usb_configuration_value);
			ep0_status();
			return;

		case REQ_GET_CONFIGURATION: {
			uint8_t value = usb_configuration_value;

			ep0_send(&value, reply_length(1, setup->length));
			return;
		}

		case REQ_SET_INTERFACE:
			usb_alternate = (uint8_t)(setup->value & 0xff);
			printf("usb: interface %u alternate %u\n",
			    setup->index & 0xff, usb_alternate);
			ep0_status();
			return;

		case REQ_GET_INTERFACE: {
			uint8_t value = usb_alternate;

			ep0_send(&value, reply_length(1, setup->length));
			return;
		}

		case REQ_GET_STATUS: {
			uint8_t value[2] = { 0, 0 };

			ep0_send(value, reply_length(2, setup->length));
			return;
		}

		case REQ_CLEAR_FEATURE:
		case REQ_SET_FEATURE:
			/*
			 * Only remote wakeup and endpoint halt exist here, and this first stage has
			 * no endpoints to halt yet. Acknowledging keeps the host's enumeration
			 * going, which is what this stage is for.
			 */
			ep0_status();
			return;

		default:
			break;
		}
	}

	/*
	 * A class request from the CDC host driver (SET_LINE_CODING, SET_CONTROL_LINE_STATE) is
	 * accepted without a payload: this link needs no line settings, and refusing them makes
	 * some hosts give up on the function. wLength of zero is a status stage; one with data
	 * still owes the device a read of the OUT packet before the handshake.
	 */
	if (type == REQ_TYPE_CLASS && setup->length == 0) {
		ep0_status();
		return;
	}

	printf("usb: stall on type %02x req %02x\n", setup->request_type, setup->request);
	ep0_stall();
}

/*
 * One pass of the EP0 state machine. Polled rather than interrupt-driven: the interrupts this
 * controller raises need GIC lines programmed and an ISR in the kernel's exception path, and the
 * first question is whether the register path works at all. A poll of EP0CSR answers that with
 * no other moving parts, and 200 us is far below the time a host waits for a reply.
 */
static bool
ep0_poll(void)
{
	uint32_t csr = mac_rd(U3D_EP0CSR);

	if ((csr & EP0_SETUPPKTRDY) == 0) {
		return false;
	}

	{
		struct usb_setup setup;
		uint32_t count = mac_rd(U3D_RXCOUNT0);

		for (unsigned i = 0; i < sizeof(setup); i++) {
			((uint8_t *)&setup)[i] = 0;
		}
		ep0_read_fifo(&setup, count > sizeof(setup) ? sizeof(setup) : count);

		/*
		 * The setup packet is done with: TXPKTRDY is cleared and so is SETUPPKTRDY, which
		 * is what tells the controller the FIFO may be reused. ep0_read_setup() does this
		 * with one read-modify-write of the W1C-safe bits only.
		 */
		mac_wr(U3D_EP0CSR, csr & EP0_W1C_BITS);

		handle_setup(&setup);
	}
	return true;
}

/* ---- bring-up ---- */

static void
usb_probe(void)
{
	uint32_t sts1 = ippc_rd(IP_PW_STS1);
	uint32_t sts2 = ippc_rd(IP_PW_STS2);
	uint32_t vers = ippc_rd(IP_TRUNK_VERS_REG);
	uint32_t dev_cap = ippc_rd(IP_DEV_CAP);
	uint32_t u2 = ippc_rd(U2_CTRL_0P);
	uint32_t otg = ippc_rd(OTG_STS);
	uint32_t conf = mac_rd(U3D_DEVICE_CONF);
	uint32_t ep0 = mac_rd(U3D_EP0CSR);
	uint32_t misc = mac_rd(U3D_MISC_CTRL);
	uint32_t pm = mac_rd(U3D_POWER_MANAGEMENT);

	printf("usb: ip version %#x (%s); dev cap %#x (u3 ports %u)\n",
	    IP_TRUNK_VERS(vers), IP_TRUNK_VERS(vers) >= 0x1003u ? "gen2 qmu" : "old qmu",
	    dev_cap, DEV_U3_PORT_NUM(dev_cap));
	printf("usb: pw sts1 %#x [sys125 %u ref %u syspll %u sleep %u]; sts2 %#x; u2 ctrl %#x [pdn %u dis %u]\n",
	    sts1, (sts1 & SYS125_RST_B_STS) ? 1u : 0u, (sts1 & REF_RST_B_STS) ? 1u : 0u,
	    (sts1 & SYSPLL_STABLE) ? 1u : 0u, (sts1 & IP_SLEEP_STS) ? 1u : 0u,
	    sts2, u2, (u2 & U2_PORT_PDN) ? 1u : 0u, (u2 & U2_PORT_DIS) ? 1u : 0u);
	if (usb_dp_sel != 0) {
		uint32_t v = *(volatile uint32_t *)(uintptr_t)usb_dp_sel;
		printf("usb: dp selector %#x [usb %u]\n", v, (v & USB_DP_SEL_USB) ? 1u : 0u);
	}
	printf("usb: at handover: opstate %#x ltssm %#x dev ctl %#x power %#x softconn %u\n",
	    mac_rd(U3D_USB20_OPSTATE), mac_rd(U3D_LINK_STATE_MACHINE),
	    mac_rd(U3D_DEVICE_CONTROL), mac_rd(U3D_POWER_MANAGEMENT),
	    (mac_rd(U3D_POWER_MANAGEMENT) & SOFT_CONN) ? 1u : 0u);
	printf("usb: otg sts %#x [vbus %u]; device conf %#x [speed %u addr %u]; ep0 csr %#x\n",
	    otg, (otg & SSUSB_VBUS_VALID) ? 1u : 0u, conf, SSUSB_DEV_SPEED(conf),
	    (conf & DEV_ADDR_MSK) >> 24, ep0);
	printf("usb: power mgmt %#x [softconn %u suspendm %u hs %u]; misc %#x [vbus frc %u on %u]\n",
	    pm, (pm & SOFT_CONN) ? 1u : 0u, (pm & SUSPENDM_ENABLE) ? 1u : 0u,
	    (pm & HS_ENABLE) ? 1u : 0u, misc,
	    (misc & MISC_VBUS_FRC_EN) ? 1u : 0u, (misc & MISC_VBUS_ON) ? 1u : 0u);
}

/*
 * Take the port over, keeping what the loader set up. Nothing here resets the controller: it is
 * live, its clocks are running, and a reset would put the phy through a power sequence this
 * driver has no business repeating (the three syscon resets did exactly that to the UFS
 * controller and it never came back). What is cleared is state that belonged to fastboot:
 *
 *   the device address (6, left from LK's own enumeration) -- a host would refuse to talk to a
 *   device that answers to an address it never assigned
 *   EP0's stale CSR bits
 *   the interrupts, which this stage does not use and which would otherwise sit pending
 *
 * and what is set is what makes the port answer a host at all: the high-speed function, the
 * automatic U2/U3 switch, and the D+ pullup.
 */
/*
 * u2_phy_slew_rate_calibrate(), the half of the vendor's power_on path that runs after the vbus
 * comparator is set. A ring oscillator is fed into the phy's frequency meter, the result is compared
 * against the 26 MHz reference, and the answer goes into the HS transmitter's slew control.
 *
 * Skipped when the device tree supplies mediatek,eye-src, which xaga's does not -- so on this board
 * it always runs, and the fact that every U2FREQ register reads 0 at handover says it never has.
 */
static void
usb_phy_slew_rate_calibrate(void)
{
	uint32_t fm_out, tmp, calib;

	/* enable USB ring oscillator */
	phy_set(XSP_USBPHYACR5, P2A5_RG_HSTX_SRCAL_EN);
	delay(1);

	/* enable free run clock */
	phy_set(XSP_U2FREQ_FMMONR1, P2F_RG_FRCK_EN);

	/* set cycle count as 1024 */
	phy_update(XSP_U2FREQ_FMCR0, P2F_RG_CYCLECNT, XSP_FM_DET_CYCLE_CNT);

	/* enable frequency meter */
	phy_set(XSP_U2FREQ_FMCR0, P2F_RG_FREQDET_EN);

	for (uint32_t spin = 0; spin < 200u; spin++) {
		if (phy_rd(XSP_U2FREQ_FMMONR1) & P2F_USB_FM_VALID) {
			break;
		}
		delay(10);
	}

	fm_out = phy_rd(XSP_U2FREQ_MMONR0);

	/* disable frequency meter */
	phy_clear(XSP_U2FREQ_FMCR0, P2F_RG_FREQDET_EN);

	/* disable free run clock */
	phy_clear(XSP_U2FREQ_FMMONR1, P2F_RG_FRCK_EN);

	if (fm_out != 0u) {
		/* (1024 / FM_OUT) x reference clock frequency x coefficient */
		tmp = XSP_REF_CLK * XSP_SLEW_RATE_COEF;
		tmp = (tmp * XSP_FM_DET_CYCLE_CNT) / fm_out;
		calib = (tmp + XSP_SR_COEF_DIVISOR / 2u) / XSP_SR_COEF_DIVISOR;
	} else {
		/* if FM detection fail, set default value */
		calib = 3u;
	}

	/* set HS slew rate */
	phy_update(XSP_USBPHYACR5, P2A5_RG_HSTX_SRCTRL, calib << 12);

	/* disable USB ring oscillator */
	phy_clear(XSP_USBPHYACR5, P2A5_RG_HSTX_SRCAL_EN);

	printf("usb: slew: fmcr0 %#x fmmonr1 %#x fm_out %u calib %u; acr5 %#x\n",
	    phy_rd(XSP_U2FREQ_FMCR0), phy_rd(XSP_U2FREQ_FMMONR1),
	    fm_out, calib, phy_rd(XSP_USBPHYACR5));
}

/*
 * The phy's reference clock. The device tree names it "ref" under usb2-phy0@11e40000 and Linux
 * enables it in mtk_phy_init() before touching any register. Its gate lives in the apmixed block,
 * which this loader has no driver for, so report where it stands instead of pretending to set it.
 */
static void
usb_phy_ref_clock_report(void)
{
	printf("usb: phy ref clk gate is in apmixed; dtm1 %#x acr0 %#x (live phy has bit5 set)\n",
	    phy_rd(XSP_U2PHYDTM1), phy_rd(XSP_USBPHYACR0));
}

/*
 * The phy, which the loader leaves in whatever state fastboot put it. phy-mtk-xsphy.c splits this
 * in two, and both halves matter:
 *
 *   mtk_phy_init()     u2_phy_instance_init(): turn the BC1.1 path off, turn the interrupt path on;
 *                      u2_phy_props_set(): the four tuning values the device tree carries
 *   mtk_phy_power_on() u2_phy_instance_power_on(): OTG_VBUSCMP_EN, then force VBUSVALID and AVALID
 *                      in the debug/test register
 *                      u2_phy_slew_rate_calibrate(): the ring oscillator measurement
 *
 * The second half is the one that decides whether the port connects at all. A USB phy that does not
 * see VBUS refuses to drive D+, no matter what the controller's soft-connect bit says -- the first
 * version of this file set SOFT_CONN and then read back "device conf 0 ... opstate 0", which is a
 * port whose phy still believes there is no cable. Android forces these two bits as well, which is
 * why its own driver works on the same board.
 */
static void
usb_phy_bring_up(void)
{
	printf("usb: phy before: acr0 %#x acr1 %#x acr4 %#x acr5 %#x acr6 %#x dtm0 %#x dtm1 %#x\n",
	    phy_rd(XSP_USBPHYACR0), phy_rd(XSP_USBPHYACR1), phy_rd(XSP_USBPHYACR4),
	    phy_rd(XSP_USBPHYACR5), phy_rd(XSP_USBPHYACR6),
	    phy_rd(XSP_U2PHYDTM0), phy_rd(XSP_U2PHYDTM1));

	/*
	 * DTM0, which phy-mtk-xsphy.c never touches and phy-mtk-tphy.c:829 opens with.
	 *
	 * The loader hands the phy over with dtm0 = 0x2be00d4, and bit 18 of that is
	 * P2C_FORCE_SUSPENDM: the phy is held in suspend. A phy in suspend does not drive D+, so the
	 * port can have every controller register matching Android's working dump -- soft connect
	 * set, vbus forced, dev ctl 0x99 -- and the host still sees nothing, which is exactly the
	 * wall this driver spent several rounds against.
	 *
	 * tphy clears FORCE_UART_EN (26) and FORCE_SUSPENDM (18) first, then the transceiver select,
	 * the data-in field and the DTM0 part mask. Same order here; xsphy simply omits the block
	 * because on MT6989-and-later the boot chain is expected to have done it.
	 */
	/*
	 * Leave DTM0 alone. The bit table above comes from phy-mtk-tphy.c, and this is an xsphy
	 * part: the two IP blocks do not lay DTM0 out the same way, and the vendor's xsphy module
	 * never writes this register at all. It matters -- the loader hands over dtm0 = 0x2be00d4
	 * and Android enumerates with exactly that value. Clearing the transceiver select, the
	 * data-in field and the part mask turned it into 0x2000000, which is a phy the vendor
	 * never runs, and the port stopped reporting to the host.
	 *
	 * The one clear the vendor module does perform is bit 26 inside power_on, kept below.
	 */
#if USB_PHY_DTM0_RESET == 2
	/*
	 * The narrow clear: bit 26 and bit 18 and nothing else.
	 *
	 * The wide clear below leaves dtm0 = 0x2000000. That is not a value the vendor's own driver
	 * ever produces -- phy-mtk-xsphy.ko, disassembled, never writes DTM0 at all, and the loader
	 * hands over 0x2be00d4. So both of the two states tried so far are states no working phone
	 * runs in: 0x2be00d4 holds the phy in suspend, and 0x2000000 throws away the transceiver
	 * select and the data-in field along with it.
	 *
	 * This variant keeps everything the loader set and lifts only the suspend, which is
	 * 0x2be00d4 & ~(bit18|bit26) = 0x2ba00d4. Worth one round because it is the smallest step
	 * that both lets the phy leave suspend and leaves the rest of the loader's configuration
	 * intact.
	 */
	phy_clear(XSP_U2PHYDTM0, P2D0_FORCE_UART_EN | P2D0_FORCE_SUSPENDM);
#elif USB_PHY_DTM0_RESET
	phy_clear(XSP_U2PHYDTM0, P2D0_FORCE_UART_EN | P2D0_FORCE_SUSPENDM);
	phy_clear(XSP_U2PHYDTM0, P2D0_RG_XCVRSEL | P2D0_RG_DATAIN | P2D0_PART_MASK);
	phy_clear(XSP_U2PHYDTM1, P2D_RG_UART_EN);
#endif

	/* mtk_phy_init(): the BC1.1 path off (it answers charger types, which this port does not
	 * want to be asked about), the interrupt path on */
	phy_clear(XSP_USBPHYACR6, P2A6_RG_BC11_SW_EN);
	phy_set(XSP_USBPHYACR0, P2A0_RG_INTR_EN);

	/* u2_phy_props_set(), the fields that exist for a USB2 phy */
	phy_update(XSP_USBPHYACR1, P2A1_RG_INTR_CAL, BOARD_EFUSE_INTR << 19);
	phy_update(XSP_USBPHYACR1, P2A1_RG_VRT_SEL, BOARD_EYE_VRT << 12);

	/*
	 * u2_phy_instance_power_on(), transcribed from phy-mtk-xsphy.ko rather than from the driver.
	 *
	 * Upstream that function is two writes. In the module -- same silicon, built by the people who
	 * make the part -- it is ten, and the eight extras are the difference between a port whose
	 * registers all match Android's working dump and a port that never reports a single event:
	 *
	 *   dtm0 &= ~(1 << 26)
	 *   dtm1 &= ~(1 << 16)          dtm1 &= ~(1 << 18)
	 *   acr4 &= ~0x300              the "usb20 gpio mode" pair, which parks the pads
	 *   dtm1 &= 0xff41c30b          masks it down to the vendor's notion of a clean DTM1
	 *   acr6 &= ~(1 << 23)
	 *   acr6 |=  (1 << 20)          the vbus comparator -- enabled, not disabled
	 *   dtm1 &= ~0x30 ; dtm1 |= 0x24
	 *   acr6 = (acr6 & 0x3fffffff) | (1 << 30)
	 *
	 * This file previously cleared bit 20 of acr6, reasoning that with no PMIC access the
	 * comparator would compare against a VBUS nobody had confirmed. The module enables it, and
	 * bit 30 appears nowhere in the driver at all.
	 */
	phy_clear(XSP_U2PHYDTM0, 1u << 26);
	phy_clear(XSP_U2PHYDTM1, (1u << 16) | (1u << 18));
	phy_clear(XSP_USBPHYACR4, P2A4_RG_USB20_GPIO_MSK);
	phy_wr(XSP_U2PHYDTM1, phy_rd(XSP_U2PHYDTM1) & 0xff41c30bu);
	phy_clear(XSP_USBPHYACR6, P2A6_RG_BC11_SW_EN);
	phy_set(XSP_USBPHYACR6, P2A6_RG_OTG_VBUSCMP_EN | P2A6_RG_BIT30);
	/* dtm1 = (dtm1 & ~0x30) | 0x24, which sets AVALID (bit 2) as well as VBUSVALID (bit 5).
	 * A masked update cannot express this: 0x24 is not a subset of 0x30, so bit 2 would stay
	 * cleared by the 0xff41c30b mask above. */
	phy_wr(XSP_U2PHYDTM1, (phy_rd(XSP_U2PHYDTM1) & ~0x30u) | 0x24u);

	/* the module waits ~3.4 s worth of counts here before it returns */
	delay(1000);

	/*
	 * This port is a device and only a device, so the phy is told so rather than left to read
	 * the ID pin: PHY_MODE_USB_DEVICE in phy-mtk-xsphy.c:u2_phy_instance_set_mode() is exactly
	 * FORCE_IDDIG | RG_IDDIG. Without it an OTG phy sits waiting for the ID pin to settle and
	 * never enters the device role on its own.
	 */
	phy_set(XSP_U2PHYDTM1, P2D_FORCE_IDDIG | P2D_RG_IDDIG);

	/*
	 * The U2 PLL, which nothing so far has switched on.
	 *
	 * The frequency meter inside usb_phy_slew_rate_calibrate() has read fm_out = 0 on every run,
	 * which is it failing to measure anything rather than measuring zero. It measures the phy's
	 * ring oscillator against the 26 MHz reference, and a ring oscillator that is not running
	 * produces no count. The oscillator lives behind the U2 PLL.
	 *
	 * phy-mtk-tphy.c switches that PLL on in two places, and phy-mtk-xsphy.c has neither:
	 *
	 *   u2_phy_instance_init()     ACR2 |= PA2_RG_SIF_U2PLL_FORCE_EN   (bit 18)
	 *   u2_phy_pll_26m_set()       ACR0 PREDIV = 0, ACR2 BW = 3,
	 *                              RESV = 0x1bb13b (the 26 MHz feedback divider),
	 *                              RESV1 |= FRA_EN | REFCLK_SEL
	 *
	 * u2_phy_pll_26m_set() is gated on tphy->pdata->sw_pll_48m_to_26m upstream, so it runs only
	 * on the parts whose PLL is fed 48 MHz and has to be told to divide down to 26. This board
	 * wires the phy's "ref" clock straight to clk26m -- see usb2-phy0@11e40000 in the device
	 * tree -- so the divider value is right for a 26 MHz input either way, and writing it is
	 * harmless if the boot chain already did.
	 *
	 * ACR0 bit 15, PA0_RG_U2PLL_FORCE_ON, is not in that list. It is the "run the PLL whether or
	 * not anything asked for it" bit, and it is the one adb-hosted board bring-up always sets on
	 * this IP. Without a PLL there is no transceiver, and a transceiver-less phy cannot present
	 * the 1.5 kOhm pull-up on D+ no matter what SOFT_CONN says -- which is the one explanation
	 * left that fits every reading so far.
	 */
#if USB_PHY_PLL
	phy_update(XSP_USBPHYACR0, P2A0_USB20_PLL_PREDIV, 0u);
	phy_set(XSP_USBPHYACR2, P2A2_RG_SIF_U2PLL_FORCE_EN);
	phy_update(XSP_USBPHYACR2, P2A2_RG_U2PLL_BW, 3u << 19);
	phy_wr(XSP_U2PHYA_RESV, P2R_RG_U2PLL_FBDIV_26M);
	phy_set(XSP_U2PHYA_RESV1, P2R_RG_U2PLL_REFCLK_SEL | P2R_RG_U2PLL_FRA_EN);
	phy_set(XSP_USBPHYACR0, P2A0_RG_U2PLL_FORCE_ON);
	delay(200);
	printf("usb: pll: acr0 %#x acr2 %#x resv %#x resv1 %#x dcr0 %#x\n",
	    phy_rd(XSP_USBPHYACR0), phy_rd(XSP_USBPHYACR2), phy_rd(XSP_U2PHYA_RESV),
	    phy_rd(XSP_U2PHYA_RESV1), phy_rd(XSP_U2PHYDCR0));
#endif

	/* the tail of mtk_phy_power_on() */
	usb_phy_slew_rate_calibrate();
	usb_phy_ref_clock_report();

	printf("usb: phy after:  acr0 %#x acr1 %#x acr4 %#x acr5 %#x acr6 %#x dtm0 %#x dtm1 %#x\n",
	    phy_rd(XSP_USBPHYACR0), phy_rd(XSP_USBPHYACR1), phy_rd(XSP_USBPHYACR4),
	    phy_rd(XSP_USBPHYACR5), phy_rd(XSP_USBPHYACR6),
	    phy_rd(XSP_U2PHYDTM0), phy_rd(XSP_U2PHYDTM1));
}

/*
 * Program EP0's max packet size.
 *
 * mtu3_core.c:mtu3_ep0_setup() is four lines -- read EP0CSR, clear EP0_MAXPKTSZ_MSK, or in the
 * width, write back the W1C-safe bits -- and mtu3_link_isr() calls it after every bus reset,
 * because the reset clears the field. The host reads bMaxPacketSize0 out of this same field
 * during the first GET_DESCRIPTOR, so a wrong value makes the first control transfer fail and
 * the host drops the port without printing anything on this side, which is exactly what the
 * log showed before this existed.
 */
static void
ep0_configure(void)
{
	uint32_t csr = (mac_rd(U3D_EP0CSR) & ~EP0_MAXPKTSZ_MSK) & EP0_W1C_BITS;

	mac_wr(U3D_EP0CSR, csr | EP0_MAXPKTSZ(USB_EP0_MAXPACKET_HS));
}

static void
usb_take_over(void)
{
	uint32_t csr;

#if USB_IP_SW_RESET
	/*
	 * ssusb_ip_sw_reset() (mtu3_plat.c:180) and then the enable half of mtu3_hw_init().
	 *
	 * The reset pulse clears the whole IP, including the device domain's power bit, so the
	 * sequence has to be reset -> power device domain back on -> rebuild. MISC_CTRL's VBUS force
	 * bits do not survive it either, so they are re-asserted right after, which is the order the
	 * register dumps demand: the force bits are what the working port has, and everything that
	 * clears them has to come before them.
	 */
	ippc_wr(IP_PW_CTRL0, ippc_rd(IP_PW_CTRL0) | SSUSB_IP_SW_RST);
	delay(1);
	ippc_wr(IP_PW_CTRL0, ippc_rd(IP_PW_CTRL0) & ~SSUSB_IP_SW_RST);
	delay(10);
	printf("usb: ip sw reset; pw0 %#x pw1 %#x pw2 %#x sts1 %#x\n",
	    ippc_rd(IP_PW_CTRL0), ippc_rd(IP_PW_CTRL1), ippc_rd(IP_PW_CTRL2),
	    ippc_rd(IP_PW_STS1));

	/* mtu3_device_enable(): the device domain comes back up, the host domain stays down */
	ippc_wr(IP_PW_CTRL2, ippc_rd(IP_PW_CTRL2) & ~SSUSB_IP_DEV_PDN);
	ippc_wr(IP_PW_CTRL1, ippc_rd(IP_PW_CTRL1) | SSUSB_IP_HOST_PDN);
	delay(10);

	/* u3: clear DIS|PDN|HOST_SEL and mark the port dual-mode; u2: clear DIS|PDN|HOST_SEL */
	{
		uint32_t u3 = ippc_rd(U3_CTRL_0P);
		u3 &= ~(U3_PORT_DIS | U3_PORT_PDN | U3_PORT_HOST_SEL);
		u3 |= U3_PORT_DUAL_MODE;
		ippc_wr(U3_CTRL_0P, u3);
	}
	{
		uint32_t u2 = ippc_rd(U2_CTRL_0P);
		u2 &= ~(U2_PORT_DIS | U2_PORT_PDN | U2_PORT_HOST_SEL);
		ippc_wr(U2_CTRL_0P, u2);
	}
	/* ssusb_check_clocks() */
	{
		uint32_t check = SYS125_RST_B_STS | REF_RST_B_STS | SYSPLL_STABLE;
		uint32_t sts1 = 0;
		for (unsigned i = 0; i < 200u; i++) {
			sts1 = ippc_rd(IP_PW_STS1);
			if ((sts1 & check) == check) {
				break;
			}
			delay(1000);
		}
		printf("usb: after reset: sts1 %#x [sys125 %u ref %u syspll %u u3mac %u]; u2 %#x u3 %#x\n",
		    sts1, (sts1 & SYS125_RST_B_STS) ? 1u : 0u, (sts1 & REF_RST_B_STS) ? 1u : 0u,
		    (sts1 & SYSPLL_STABLE) ? 1u : 0u, (sts1 & U3_MAC_RST_B_STS) ? 1u : 0u,
		    ippc_rd(U2_CTRL_0P), ippc_rd(U3_CTRL_0P));
	}

	/* mtu3_device_reset(): the device half's own reset, before anything is configured in it */
	ippc_wr(DEV_RST_CTRL, ippc_rd(DEV_RST_CTRL) | DEV_SW_RST);
	delay(1);
	ippc_wr(DEV_RST_CTRL, ippc_rd(DEV_RST_CTRL) & ~DEV_SW_RST);
	delay(10);
	printf("usb: device part reset; otg sts %#x [vbus %u]\n",
	    ippc_rd(OTG_STS), (ippc_rd(OTG_STS) & SSUSB_VBUS_VALID) ? 1u : 0u);
#endif

	/* fastboot's address is not ours */
	/*
	 * The device reset, and it has to come first. The previous version of this file put it
	 * after the configuration, and the register dump shows what that costs: the reset undid
	 * everything --
	 *
	 *     power mgmt 0x60a1      (0x64e1 before the reset)
	 *     dev ctl 0x81           (0x99 before the reset)
	 *     otg sts ... [vbus 0]   (vbus 1 before the reset)
	 *
	 * mtu3_hw_init() runs mtu3_device_reset() before mtu3_device_enable() and before
	 * mtu3_regs_init(), which is the only order that makes sense: a reset is the thing that
	 * puts the controller into a known state, so nothing configured before it survives.
	 */
#if USB_DEVICE_RESET
	ippc_wr(DEV_RST_CTRL, ippc_rd(DEV_RST_CTRL) | DEV_SW_RST);
	delay(1);
	ippc_wr(DEV_RST_CTRL, ippc_rd(DEV_RST_CTRL) & ~DEV_SW_RST);
	delay(10);
	printf("usb: dev rst done; otg sts %#x [vbus %u]\n",
	    ippc_rd(OTG_STS), (ippc_rd(OTG_STS) & SSUSB_VBUS_VALID) ? 1u : 0u);
#else
	/*
	 * No reset, on purpose, and this is the whole point of the switch.
	 *
	 * OTG_STS bit 9 is VBUS_VALID, and it is not something software can write: it is what the
	 * phy's comparator reports, and the loader hands the port over with it set --
	 *
	 *     otg sts 0x16c016c0 [vbus 1]      the value the loader leaves
	 *     otg sts 0x4c004c0  [vbus 0]      the value after SSUSB_DEV_SW_RST
	 *
	 * A device reset drops it and it does not come back, MISC_CTRL's VBUS force bits
	 * notwithstanding. A port whose VBUS_VALID is clear will never pull D+ up no matter what
	 * SOFT_CONN says, which is exactly the symptom: every register matches Android's working
	 * dump and opstate stays 0.
	 *
	 * Android's own dump has VBUS_VALID set, and Android never resets the controller from a
	 * running state either -- it configures what the bootloader handed it. So this build
	 * configures what the bootloader handed us.
	 */
	printf("usb: no dev rst; otg sts %#x [vbus %u]\n",
	    ippc_rd(OTG_STS), (ippc_rd(OTG_STS) & SSUSB_VBUS_VALID) ? 1u : 0u);
#endif

	/*
	 * The reset takes MISC_CTRL's VBUS force bits with it, and they have to come back: the
	 * register dump from the phone while Android is running its own adb port reads
	 *
	 *     MISC_CTRL = 0x80000003      (DMA_ADDR_36BIT | VBUS_ON | VBUS_FRC_EN)
	 *
	 * which is bit for bit what the loader leaves there. So the working state on this board
	 * keeps VBUS forced on -- there is no VBUS detect to wait for on a board that is powered
	 * from the same cable -- and the vendor's clrbits() for these two is what its own hardware
	 * does not need. Written straight after the reset, because the reset is what clears them.
	 */
	mac_set(U3D_MISC_CTRL, MISC_VBUS_FRC_EN | MISC_VBUS_ON);
	printf("usb: misc ctrl %#x [vbus frc %u on %u]; otg sts %#x [vbus %u]\n",
	    mac_rd(U3D_MISC_CTRL),
	    (mac_rd(U3D_MISC_CTRL) & MISC_VBUS_FRC_EN) ? 1u : 0u,
	    (mac_rd(U3D_MISC_CTRL) & MISC_VBUS_ON) ? 1u : 0u,
	    ippc_rd(OTG_STS), (ippc_rd(OTG_STS) & SSUSB_VBUS_VALID) ? 1u : 0u);

	mac_wr(U3D_DEVICE_CONF, 0);
	/*
	 * mtu3_regs_init()'s last line, and it is not a tuning knob: this IP reads IP_TRUNK_VERS
	 * 0x1006, and 0x1003 is where the second generation QMU appeared, so the QMU has to be told
	 * to use the new descriptor format. Left unwritten it keeps whatever format the loader's
	 * generation set up.
	 */
	mac_wr(U3D_QFCR, 0xffffffffu);
	printf("usb: qfcr %#x (gen2 qmu); vbus frc bits now %#x\n",
	    mac_rd(U3D_QFCR), mac_rd(U3D_MISC_CTRL));

	/* no interrupts: this stage polls, and pending ones would only confuse the readings */
	mac_wr(U3D_LV1IECR, 0xffffffffu);
	mac_wr(U3D_EPIECR, 0xffffffffu);
	mac_wr(U3D_COMMON_USB_INTR_ENABLE, 0);
	mac_wr(U3D_DEV_LINK_INTR_ENABLE, 0);
	mac_wr(U3D_LV1ISR, mac_rd(U3D_LV1ISR));
	mac_wr(U3D_EPISR, mac_rd(U3D_EPISR));
	/*
	 * Link and EP1 interrupts are the only path by which mtu3 ever re-arms EP0 after a bus
	 * reset -- mtu3_core.c:mtu3_ep0_setup() lives at the tail of mtu3_link_isr(), and the
	 * reset that clears EP0's max packet field is delivered through that same interrupt.
	 * Leaving both masked, as this stage did, means the field stays at whatever the reset
	 * left it and the host's first GET_DESCRIPTOR gets no answer.
	 */
	mac_wr(U3D_DEV_LINK_INTR_ENABLE, SSUSB_DEV_SPEED_CHG_INTR);
	mac_wr(U3D_EPIESR, EP0ISR | SETUPENDISR);

	/* EP0 out of whatever state fastboot left it in */
	mac_wr(U3D_EP_RST, EP0_RST);
	{
		unsigned i;

		for (i = 0; i < 10000u; i++) {
			if ((mac_rd(U3D_EP_RST) & EP0_RST) == 0) {
				break;
			}
		}
	}
	csr = mac_rd(U3D_EP0CSR) & EP0_W1C_BITS;
	mac_wr(U3D_EP0CSR, csr | EP0_SENTSTALL);

	ep0_configure();

	/*
	 * Port 0 is left exactly as the loader left it, and that is not laziness: the register dump
	 * the phone gives from debugfs while Android is running its own adb device port says
	 *
	 *     SSUSB_U2_CTRL_0P = 0x00000048
	 *
	 * which is what the first probe of this driver read too -- the loader's value. Android needs
	 * no OTG_SEL, no FORCE_IDDIG and no RG_IDDIG in it, and setting them is what the previous
	 * version of this file did; that version also read back opstate 0, exactly as the untouched
	 * one had. The port is already in device mode: HOST_SEL (VBUSVALID | HOST) is clear, PDN and
	 * DIS are clear, so there is nothing to switch and every forced bit was a change for the
	 * worse.
	 *
	 * What was actually missing is below: the phy, and the LPM settings the link reads once it
	 * starts training.
	 */
	printf("usb: u2 ctrl %#x [pdn %u dis %u hostsel %u otg %u]; u3 ctrl %#x [dis %u pdn %u hostsel %u dual %u]\n",
	    ippc_rd(U2_CTRL_0P),
	    (ippc_rd(U2_CTRL_0P) & U2_PORT_PDN) ? 1u : 0u,
	    (ippc_rd(U2_CTRL_0P) & U2_PORT_DIS) ? 1u : 0u,
	    (ippc_rd(U2_CTRL_0P) & U2_PORT_HOST_SEL) ? 1u : 0u,
	    (ippc_rd(U2_CTRL_0P) & U2_PORT_OTG_SEL) ? 1u : 0u,
	    ippc_rd(U3_CTRL_0P),
	    (ippc_rd(U3_CTRL_0P) & U3_PORT_DIS) ? 1u : 0u,
	    (ippc_rd(U3_CTRL_0P) & U3_PORT_PDN) ? 1u : 0u,
	    (ippc_rd(U3_CTRL_0P) & U3_PORT_HOST_SEL) ? 1u : 0u,
	    (ippc_rd(U3_CTRL_0P) & U3_PORT_DUAL_MODE) ? 1u : 0u);

	/*
	 * The U3 half of mtu3_device_enable(). This is the difference that matters, and it is the
	 * one thing in this file that was reading back visibly wrong: the loader leaves
	 *
	 *     SSUSB_U3_CTRL_0P = 0x0000000f      DIS | PDN | HOST_SEL all set, bit 3 set
	 *
	 * while Android's own working device port reads back 0x00000088, which is the same register
	 * with those three bits clear and DUAL_MODE set. A U3 domain that is powered down and
	 * disabled holds down the whole SSUSB block: the U2 half can look configured -- and ours
	 * did, DEVICE_CONTROL 0x99 and POWER_MANAGEMENT 0x64e1 matching Android exactly -- while
	 * USB20_OPSTATE never leaves 0.
	 *
	 * mtu3_core.c:mtu3_device_enable() does exactly this, then sets DUAL_MODE for OTG.
	 */
	{
		uint32_t u3 = ippc_rd(U3_CTRL_0P);
		u3 &= ~(U3_PORT_DIS | U3_PORT_PDN | U3_PORT_HOST_SEL);
		u3 |= U3_PORT_DUAL_MODE;
		ippc_wr(U3_CTRL_0P, u3);
		delay(10);
		printf("usb: u3 ctrl now %#x [dis %u pdn %u dual %u]\n",
		    ippc_rd(U3_CTRL_0P),
		    (ippc_rd(U3_CTRL_0P) & U3_PORT_DIS) ? 1u : 0u,
		    (ippc_rd(U3_CTRL_0P) & U3_PORT_PDN) ? 1u : 0u,
		    (ippc_rd(U3_CTRL_0P) & U3_PORT_DUAL_MODE) ? 1u : 0u);
	}


	/*
	 * Everything below this point is the second half of mtu3_csr_init() and mtu3_set_speed(),
	 * replayed because the power cycle above reset them.
	 */
	mac_wr(U3D_USB3_CONFIG, mac_rd(U3D_USB3_CONFIG) & ~USB3_EN);
	mac_set(U3D_POWER_MANAGEMENT, HS_ENABLE);
	mac_clear(U3D_LINK_POWER_CONTROL, SW_U1_REQUEST_ENABLE | SW_U2_REQUEST_ENABLE);
	mac_set(U3D_LINK_POWER_CONTROL, SW_U1_ACCEPT_ENABLE | SW_U2_ACCEPT_ENABLE);
	printf("usb: ltssm ctrl before %#x\n", mac_rd(U3D_LTSSM_CTRL));
	mac_clear(U3D_LTSSM_CTRL, SOFT_U3_EXIT_EN);
	printf("usb: ltssm ctrl after  %#x (Android, working, reads 0x223)\n",
	    mac_rd(U3D_LTSSM_CTRL));
	mac_set(U3D_USB2_TEST_MODE, U2U3_AUTO_SWITCH);
	mac_set(U3D_U3U2_SWITCH_CTRL, SOFTCON_CLR_AUTO_EN);
	mac_clear(U3D_LINK_RESET_INFO, WTCHRP_MSK);
	mac_set(U3D_POWER_MANAGEMENT, LPM_HRWE);

	/*
	 * The two LPM fields that are in Android's register dump and were missing here. link_isr()
	 * writes LPM_BESLDCK(0xf) | LPM_BESLCK(4) | LPM_BESLCK_U3(0xa) and then sets the stall bits
	 * once it knows the speed; USB20_MISC_CONTROL's LPM_U3_ACK_EN is the other half. Android
	 * reads back POWER_MANAGEMENT 0x64f1 and USB20_MISC_CONTROL 0x01, so both are part of a
	 * link that works.
	 */
	mac_update(U3D_USB20_LPM_PARAMETER, 0xfff0u,
	    LPM_BESLDCK(0xf) | LPM_BESLCK(4) | LPM_BESLCK_U3(0xa));
	mac_set(U3D_POWER_MANAGEMENT, LPM_BESL_STALL | LPM_BESLD_STALL);

	/*
	 * Bit 4, which no Linux header names.
	 *
	 * Android's working dump reads POWER_MANAGEMENT 0x64f1 and this build reads 0x64e1 from the
	 * same SOFT_CONN | SUSPENDM_ENABLE | HS_ENABLE | LPM_HRWE | LPM stall bits -- the single
	 * difference is bit 4. Whatever the vendor calls it, it is part of a port that enumerates
	 * and not part of one that does not.
	 */
	mac_set(U3D_POWER_MANAGEMENT, 1u << 4);

	/*
	 * Power the host controller controller down.
	 *
	 * Android's working dump has SSUSB_IP_PW_CTRL1 == 1, i.e. SSUSB_IP_HOST_PDN set: the xHCI half
	 * of this IP is off while the device half runs. Ours was left as the loader had it, which is
	 * the state fastboot needs, and a live host controller on the same port pair is at least one
	 * plausible reason the port never leaves opstate 0.
	 */
	ippc_wr(IP_PW_CTRL1, ippc_rd(IP_PW_CTRL1) | SSUSB_IP_HOST_PDN);
	printf("usb: host pdn %#x; pw sts1 %#x\n", ippc_rd(IP_PW_CTRL1), ippc_rd(IP_PW_STS1));
	mac_set(U3D_USB20_MISC_CONTROL, LPM_U3_ACK_EN);

	/*
	 * Port 0 power cycle, the way mtu3_dr.c:ssusb_port0_switch(..., tohost = false) does it:
	 * PDN and DIS on, PDN and DIS off, HOST_SEL clear. Same two writes the earlier version of
	 * this file made, but placed here -- after the device reset and after the U3 domain came
	 * up, before the phy -- and without the OTG_SEL/FORCE_IDDIG bits, which Android's working
	 * dump does not carry and which only forced the port into the wrong role.
	 *
	 * What follows is the part that was missing: ssusb_check_clocks(). Toggling PDN clears the
	 * reset trees it feeds, and a port whose clocks have not returned yet reads as disconnected
	 * however the rest of the registers look.
	 */
#if USB_PORT_CYCLE
	{
		uint32_t u2 = ippc_rd(U2_CTRL_0P);

		ippc_wr(U2_CTRL_0P, u2 | U2_PORT_PDN | U2_PORT_DIS);
		delay(10);
		u2 = ippc_rd(U2_CTRL_0P);
		u2 &= ~(U2_PORT_PDN | U2_PORT_DIS);
		u2 &= ~U2_PORT_HOST_SEL;
		ippc_wr(U2_CTRL_0P, u2);
	}
#else
	/*
	 * Not cycling the port either. The cycle does come back clean -- clocks 0x13d0f, sys125,
	 * ref, u3mac and syspll all up -- but it also resets the U2 domain's configuration, and the
	 * dump shows it taking three things with it:
	 *
	 *     power mgmt 0x60e1   (LPM_HRWE and the LPM stall bits gone)
	 *     dev ctl    0x81     (DC_SESSION gone)
	 *     otg sts    [vbus 0] (VBUS_VALID gone)
	 *
	 * The last one is fatal and unrecoverable, so the port is left alone.
	 */
	printf("usb: no port cycle; u2 ctrl %#x; clocks %#x\n",
	    ippc_rd(U2_CTRL_0P), ippc_rd(IP_PW_STS1));
#endif

	{
		uint32_t sts = 0;

		for (uint32_t i = 0; i < 200u; i++) {
			sts = ippc_rd(IP_PW_STS1);
			if ((sts & SYS125_RST_B_STS) && (sts & REF_RST_B_STS) &&
			    (sts & U3_MAC_RST_B_STS)) {
				break;
			}
			delay(1);
		}
		printf("usb: clocks after cycle %#x [sys125 %u ref %u u3mac %u syspll %u]; u2 ctrl %#x\n",
		    sts,
		    (sts & SYS125_RST_B_STS) ? 1u : 0u,
		    (sts & REF_RST_B_STS) ? 1u : 0u,
		    (sts & U3_MAC_RST_B_STS) ? 1u : 0u,
		    (sts & SYSPLL_STABLE) ? 1u : 0u,
		    ippc_rd(U2_CTRL_0P));
	}

	/*
	 * The Type-C switch, before anything else.
	 *
	 * usb_dp_selector.ko is 16 KB and its whole job is a single bit: switch_set(1) sets bit 11 of
	 * 0x10005600 and switch_set(2) clears it. A port left on the other side of that bit has its
	 * D+/D- going somewhere other than the phy, which looks exactly like a dead pullup.
	 */
	if (usb_dp_sel != 0) {
		uint32_t before = *(volatile uint32_t *)(uintptr_t)usb_dp_sel;
		*(volatile uint32_t *)(uintptr_t)usb_dp_sel = before | USB_DP_SEL_USB;
		__asm__ volatile("dsb sy" ::: "memory");
		printf("usb: dp selector %#x -> %#x (usb %u)\n", before,
		    *(volatile uint32_t *)(uintptr_t)usb_dp_sel,
		    (*(volatile uint32_t *)(uintptr_t)usb_dp_sel & USB_DP_SEL_USB) ? 1u : 0u);
	}

	/*
	 * The phy is left exactly as the loader handed it over, and this is the correction that
	 * matters most in this file.
	 *
	 * The reading taken before any write, on a port that fastboot was enumerating with seconds
	 * earlier, is
	 *
	 *     phy before: acr0 0x46002a acr1 0x844400 acr5 0x40801000 acr6 0x442 dtm1 0
	 *
	 * acr6 is 0x442 -- BC11_SW_EN and two calibration fields -- with no OTG_VBUSCMP_EN in it at
	 * all, and dtm1 is zero. That is a phy in the state phy_init() leaves it: the vendor driver
	 * only sets OTG_VBUSCMP_EN and forces VBUSVALID/AVALID later, in power_on(). fastboot never
	 * runs power_on and enumerates fine anyway, which says the pullup on this board does not
	 * depend on either bit.
	 *
	 * The earlier version of this file ran the full bring-up (acr6 0x100442, dtm1 0x226) on the
	 * theory that a forced VBUSVALID is what makes the controller see a cable. It is the reading
	 * closest to the vendor's power_on path and it is also the one state that was never
	 * demonstrated to work here, so it is behind USB_PHY_BRING_UP instead of being the default.
	 */
#if USB_PHY_BRING_UP
	usb_phy_bring_up();
#else
	printf("usb: phy left as the loader had it\n");
	for (uint32_t off = 0x300u; off <= 0x380u; off += 4u) {
		printf("usb: phy %#x = %#x\n", off, phy_rd(off));
	}
	for (uint32_t off = 0x100u; off <= 0x120u; off += 4u) {
		printf("usb: freq %#x = %#x\n", off, phy_rd(off));
	}
#endif

	/*
	 * Announce a session. mtu3_dr.c:toggle_opstate() is these two bits and nothing else:
	 *
	 *     setbits(U3D_DEVICE_CONTROL, DC_SESSION);
	 *     setbits(U3D_POWER_MANAGEMENT, SOFT_CONN);
	 */
	mac_set(U3D_DEVICE_CONTROL, DC_SESSION);

	/*
	 * Soft disconnect and reconnect, and this is the one experiment that tells the two possible
	 * failures apart.
	 *
	 * The device has been reporting opstate 0x12 and a detected speed of 3 (high speed) while the
	 * host logs nothing for the whole window and not a single SOF arrives. Both cannot be true of
	 * a bus that is actually connected, so one of the two is being reported by a MAC that is not
	 * talking to anything.
	 *
	 * Dropping SOFT_CONN removes the D+ pullup, and putting it back a tenth of a second later
	 * re-announces the device. If the pullup really reaches the cable the host has to log a
	 * disconnect followed by a new device. If the host stays silent, the pullup is being swallowed
	 * somewhere before the connector -- the Type-C side, where mt6375-tcpc owns the cc lines and
	 * the D+/D- mux, and where nothing in this kernel has ever spoken i2c.
	 */
	printf("usb: soft disconnect (power mgmt %#x)\n", mac_rd(U3D_POWER_MANAGEMENT));
	mac_clear(U3D_POWER_MANAGEMENT, SOFT_CONN);
	delay(100000);
	mac_set(U3D_POWER_MANAGEMENT, SOFT_CONN | SUSPENDM_ENABLE);
	delay(100000);
	printf("usb: soft connect again (power mgmt %#x)\n", mac_rd(U3D_POWER_MANAGEMENT));

	printf("usb: dev ctl %#x; otg sts %#x [vbus %u]; power mgmt %#x\n",
	    mac_rd(U3D_DEVICE_CONTROL), ippc_rd(OTG_STS),
	    (ippc_rd(OTG_STS) & SSUSB_VBUS_VALID) ? 1u : 0u,
	    mac_rd(U3D_POWER_MANAGEMENT));

	/*
	 * The pullup. This is the moment a host notices a device was plugged in, so everything above
	 * has to be in place first. mtu3_hs_softconn_set(): SOFT_CONN and SUSPENDM_ENABLE together,
	 * because the controller will not hold the line up while it thinks it may suspend.
	 */
	mac_set(U3D_POWER_MANAGEMENT, SOFT_CONN | SUSPENDM_ENABLE);

	printf("usb: powered up; power mgmt %#x [softconn %u suspendm %u hs %u lpm %u], waiting for the host\n",
	    mac_rd(U3D_POWER_MANAGEMENT),
	    (mac_rd(U3D_POWER_MANAGEMENT) & SOFT_CONN) ? 1u : 0u,
	    (mac_rd(U3D_POWER_MANAGEMENT) & SUSPENDM_ENABLE) ? 1u : 0u,
	    (mac_rd(U3D_POWER_MANAGEMENT) & HS_ENABLE) ? 1u : 0u,
	    (mac_rd(U3D_POWER_MANAGEMENT) & LPM_HRWE) ? 1u : 0u);
}

/*
 * What the port does after take-over, printed as it happens.
 *
 * The host half of this is unambiguous and was the thing worth learning: right after the loader
 * hands over, the PC logs
 *
 *     usb 3-2: USB disconnect, device number 66
 *
 * and then nothing at all for the rest of the window. So the host is waiting for a device to
 * appear, and the port never pulls D+ up again. Which of the many bits above is responsible is
 * not something to guess at -- COMMON_USB_INTR has CONN, DISCONN, RESET, SUSPEND and RESUME, and
 * if the phy ever sees a cable at all one of them will fire. Turning them on and reporting is
 * what separates "the phy sees nothing" from "the phy sees it and the controller ignores it".
 */
static void
usb_watch_events(void)
{
	uint32_t seen = 0;

	mac_wr(U3D_COMMON_USB_INTR_ENABLE, DISCONN_INTR | CONN_INTR | SOF_INTR |
	    RESET_INTR | RESUME_INTR | SUSPEND_INTR);

	/* re-assert the pullup every round: the U2 MAC may not latch it out of the loader's state */
	mac_set(U3D_POWER_MANAGEMENT, SOFT_CONN | SUSPENDM_ENABLE);

	printf("usb: test mode %#x misc %#x opstate %#x host pdn %#x\n",
	    mac_rd(U3D_USB2_TEST_MODE), mac_rd(U3D_USB20_MISC_CONTROL), mac_rd(U3D_USB20_OPSTATE),
	    ippc_rd(IP_PW_CTRL1));

	/*
	 * FORCE_FS stays off.
	 *
	 * It was added to answer "is the pullup reaching the cable at all", and it did answer that:
	 * with it set the device reported a bus reset it could not have received, because the host
	 * logged nothing for the whole window. A phy held in a test mode drives its own SE0, which
	 * the U2 MAC reads back as RESET_INTR -- a reset nobody sent. So the test was measuring
	 * itself. HS_ENABLE in POWER_MANAGEMENT already asks for a high speed chirp; let the phy
	 * run that instead of pinning it.
	 */
	printf("usb: test mode %#x, letting the phy chirp\n", mac_rd(U3D_USB2_TEST_MODE));

	printf("usb: watching; intr now %#x enable %#x\n",
	    mac_rd(U3D_COMMON_USB_INTR), mac_rd(U3D_COMMON_USB_INTR_ENABLE));

	uint32_t live_at = 0xffffffffu;
	uint32_t sofs = 0;

	for (uint32_t round = 0; round < 3000u; round++) {
		uint32_t intr = mac_rd(U3D_COMMON_USB_INTR);
		uint32_t opstate = mac_rd(U3D_USB20_OPSTATE);

		/*
		 * Serve EP0 while watching. The host sends GET_DESCRIPTOR right after the reset,
		 * and a SETUPPKTRDY nobody reads just sits there until the host gives up. Polling
		 * here keeps the first control transfer moving instead of stalling for the length
		 * of this loop.
		 */
		ep0_poll();

		if (intr & ~seen) {
			printf("usb: [%u us] intr %#x%s%s%s%s%s%s\n",
			    round * 1000u, intr,
			    (intr & DISCONN_INTR) ? " disconn" : "",
			    (intr & CONN_INTR) ? " conn" : "",
			    (intr & SOF_INTR) ? " sof" : "",
			    (intr & RESET_INTR) ? " reset" : "",
			    (intr & RESUME_INTR) ? " resume" : "",
			    (intr & SUSPEND_INTR) ? " suspend" : "");
			mac_wr(U3D_COMMON_USB_INTR, intr);      /* W1C */
			seen |= intr;

			/*
			 * A bus reset clears EP0's max packet field, so put it back. This is what
			 * mtu3_link_isr() does at the end, and a reset is the only event that can
			 * arrive before the first SETUP.
			 */
			if (intr & RESET_INTR) {
				ep0_configure();
				printf("usb: reset seen; ep0 csr %#x maxpacket %u\n",
				    mac_rd(U3D_EP0CSR), mac_rd(U3D_EP0CSR) & EP0_MAXPKTSZ_MSK);
			}
		}

		if (live_at == 0xffffffffu && (round < 800u ? (round % 50u) == 0u : (round % 500u) == 0u)) {
			printf("usb: [%u us] opstate %#x ltssm %#x dev conf %#x power %#x ep0 %#x lv1 %#x\n",
			    round * 1000u, opstate, mac_rd(U3D_LINK_STATE_MACHINE),
			    mac_rd(U3D_DEVICE_CONF), mac_rd(U3D_POWER_MANAGEMENT),
			    mac_rd(U3D_EP0CSR), mac_rd(U3D_LV1ISR));
		}
		/*
		 * Do not leave once the port reports a speed.
		 *
		 * A speed of 3 is high speed, and that is not something a phy can produce on its own: it
		 * takes a host reset followed by a chirp K/J handshake. So the host is talking to us. What
		 * comes next is the host asking for the device descriptor over EP0, and that is where the
		 * run has been stopping with ep0 csr still 0x40 -- SETUPPKTRDY never set, so no SETUP ever
		 * arrived.
		 *
		 * SOF counts tell the two possible reasons apart. A host that is still running the bus
		 * sends a start of frame every millisecond and SOF_INTR keeps coming back; a host that
		 * gave up stops and the bus goes to suspend instead.
		 */
		if (intr & SOF_INTR) {
			sofs++;
		}

		if (opstate != 0u && live_at == 0xffffffffu) {
			live_at = round * 1000u;
			printf("usb: the port is live at %u us\n", live_at);
		}

		if (live_at != 0xffffffffu && (round % 200u) == 0u) {
			printf("usb: [%u us] live %u sofs %u ep0 %#x epi %#x lv1 %#x opstate %#x\n",
			    round * 1000u, live_at, sofs, mac_rd(U3D_EP0CSR),
			    mac_rd(U3D_EPISR), mac_rd(U3D_LV1ISR), opstate);
		}
		/*
		 * One millisecond per round, and this number matters.
		 *
		 * The rounds used to be 250 ms apart. The host asks for the device descriptor within a
		 * millisecond or two of the bus reset and gives up after a few tens of milliseconds, so a
		 * poll every 250 ms can miss the only SETUP that is ever sent -- which is exactly the
		 * reading that kept coming back: ep0 csr 0x40, SETUPPKTRDY never set. delay() takes
		 * microseconds here, so 1000 is one millisecond.
		 */
		delay(1000);
	}
}

static void
usb_thread(__unused void *parameter, __unused wait_result_t result)
{
	uint32_t last_conf = 0xffffffffu;

	usb_probe();
	usb_take_over();
	usb_watch_events();

	for (;;) {
		uint32_t conf;
		bool worked = false;

		for (unsigned i = 0; i < 50u; i++) {
			if (ep0_poll()) {
				worked = true;
			}
			delay(200);
		}

		/*
		 * The speed the controller reports changes when the host resets the bus, which is
		 * the first thing it does after seeing the pullup. Printing it once per change is
		 * the cheapest way to tell "the host saw us" from "the cable is not connected".
		 */
		conf = mac_rd(U3D_DEVICE_CONF);
		if (conf != last_conf && !worked) {
			printf("usb: device conf %#x [speed %u addr %u]; opstate %#x\n",
			    conf, SSUSB_DEV_SPEED(conf), (conf & DEV_ADDR_MSK) >> 24,
			    mac_rd(U3D_USB20_OPSTATE));
			last_conf = conf;
		}
	}
}

void
hakios_mt6895_usb_init(void)
{
	int which = 0;
	thread_t thread = THREAD_NULL;

	if (!PE_parse_boot_argn("usb", &which, sizeof(which)) || which != 2) {
		return;
	}

	usb_mac = ml_io_map(USB_MAC_PHYS, USB_MAC_SIZE);
	usb_ippc = ml_io_map(USB_IPPC_PHYS, USB_IPPC_SIZE);
	usb_phy = ml_io_map(USB_PHY_PHYS, USB_PHY_SIZE);
	usb_dp_sel = ml_io_map(USB_DP_SEL_PHYS, USB_DP_SEL_SIZE);
	if (usb_mac == 0 || usb_ippc == 0 || usb_phy == 0 || usb_dp_sel == 0) {
		printf("usb: cannot map the controller (mac %#lx ippc %#lx phy %#lx)\n",
		    (unsigned long)usb_mac, (unsigned long)usb_ippc, (unsigned long)usb_phy);
		return;
	}

	if (kernel_thread_start(usb_thread, NULL, &thread) != KERN_SUCCESS) {
		printf("usb: cannot start the thread that runs the port\n");
		return;
	}
	thread_deallocate(thread);
}
