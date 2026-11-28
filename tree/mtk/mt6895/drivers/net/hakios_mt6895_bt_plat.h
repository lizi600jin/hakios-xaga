/*
 * hakios_mt6895_bt_plat.h — MT6895 Bluetooth 的平台表与骨架齐次应答。
 *
 * 对应上游 docs/headset-wip/raw/hakios-upstream/bluetooth-mtk-porting.md：
 * 骨架（redwood_mtkbt_skel.c）要的就是五个函数 —— 把芯片拉起来、把蓝牙功能
 * 打开/关掉、发一个完整 H4 包、收一个完整 H4 包；再由 "skel_quirks" 改写
 * 苹果栈认的那几条应答。
 *
 * 这里只放纯逻辑（平台表结构、六条开机命令的应答、VSC 改写），IO 留在
 * hakios.mt6895_bt.c，所以宿主测试能覆盖同一份代码。
 */
#ifndef HAKIOS_MT6895_BT_PLAT_H
#define HAKIOS_MT6895_BT_PLAT_H

#include "hakios_mt6895_bt_wmt.h"

/* ---- 骨架那一侧的表（五个函数 + 上下文 + Wi-Fi 地址） ----
 * 字段名按上游文档的 power_on / function_on / function_off / send / receive。
 */
struct xaga_bt_platform {
	int    (*power_on)(void *ctx);
	int    (*function_on)(void *ctx);
	int    (*function_off)(void *ctx);
	int    (*send)(void *ctx, const uint8_t *h4, size_t len);
	int    (*receive)(void *ctx, uint8_t *h4, size_t cap);
	void  *ctx;
	const uint8_t *wifi_address;   /* 6 字节，蓝牙地址 = 这个 +1；NULL 表示由 Wi-Fi 驱动后补 */
};

/* ---- 六条开机命令（骨架先问、必须都答 status 0 才建服务） ---- */
#define XAGA_HCI_RESET            0x0C03u
#define XAGA_HCI_READ_LOCAL_VER   0x1001u
#define XAGA_HCI_READ_BD_ADDR     0x1009u
#define XAGA_HCI_READ_FEATURES    0x1003u
#define XAGA_HCI_READ_BUFFER_SIZE 0x1005u
#define XAGA_HCI_LE_READ_BUFFER   0x2002u

/* ---- 苹果 VSC（Broadcom 专用，MTK 一律 "Unknown HCI Command"） ---- */
#define XAGA_VSC_FD48 0xFD48u
#define XAGA_VSC_FCE2 0xFCE2u
#define XAGA_VSC_FD1B 0xFD1Bu
#define XAGA_VSC_FCE9 0xFCE9u
#define XAGA_HCI_WRITE_ERR_REPORTING 0x0C5Bu
#define XAGA_HCI_READ_SUPPORTED_CMDS 0x1002u

/* 骨架文档给的取值：MT6631 报 HCI 5.2、manufacturer 0x0046 (MediaTek)。
 * 换成 Xaga 真机时应以自己控制器的真实回答为准，这里只是让闭环能跑。
 */
#define XAGA_HCI_VERSION   0x0B
#define XAGA_HCI_MANUFACT  0x0046u

/* 造一条 "status 0" 的 Command Complete。
 * 返回整帧长度（含 04 0E len 01 与 opcode），0 表示这个 opcode 不认识。
 */
static size_t
xaga_bt_cc_for(uint16_t opcode, uint8_t *out, size_t cap)
{
	size_t pl = 0;

	/* Command Complete 的 Return_Parameters 一定以 Status 开头，所以
	 * pl = 1 (status) + 该命令真正返回的数据。
	 */
	switch (opcode) {
	case XAGA_HCI_RESET:
		pl = 1;
		break;
	case XAGA_HCI_READ_LOCAL_VER:
		pl = 9;          /* status + hci_ver, hci_rev(2), lmp_ver, mfr(2), subver(2) */
		break;
	case XAGA_HCI_READ_BD_ADDR:
		pl = 7;          /* status + 6 */
		break;
	case XAGA_HCI_READ_FEATURES:
		pl = 9;          /* status + 8 */
		break;
	case XAGA_HCI_READ_BUFFER_SIZE:
		pl = 8;          /* status + acl_mtu(2), sco_mtu, acl_max(2), sco_max(2) */
		break;
	case XAGA_HCI_LE_READ_BUFFER:
		pl = 4;          /* status + le_acl_mtu(2), le_max */
		break;
	default:
		return 0;
	}

	if (cap < 6u + pl) {
		return 0;
	}
	out[0] = 0x04;                       /* H4 event */
	out[1] = 0x0E;                       /* Command Complete */
	out[2] = (uint8_t)(3u + pl);         /* ncmd(1) + opcode(2) + payload */
	out[3] = 0x01;                       /* Num_HCI_Command_Packets */
	out[4] = (uint8_t)(opcode & 0xFFu);
	out[5] = (uint8_t)(opcode >> 8);
	memset(out + 6, 0, pl);

	switch (opcode) {
	case XAGA_HCI_READ_LOCAL_VER:
		out[7]  = XAGA_HCI_VERSION;
		out[11] = XAGA_HCI_MANUFACT & 0xFFu;
		out[12] = XAGA_HCI_MANUFACT >> 8;
		break;
	case XAGA_HCI_READ_BD_ADDR:
		/* 全零开头的地址会被 bluetoothd 当成无效，给一个本地管理段的形状 */
		out[7]  = 0x00;
		out[8]  = 0x00;
		out[9]  = 0x00;
		out[10] = 0x00;
		out[11] = 0x00;
		out[12] = 0x02;
		break;
	case XAGA_HCI_READ_FEATURES:
		out[7]  = 0xFF;              /* LMP 页 0 前几字节：BR/EDR + LE 都有 */
		out[8]  = 0xFF;
		out[9]  = 0xFF;
		out[10] = 0xFE;
		out[11] = 0xFF;
		out[12] = 0xFF;
		out[13] = 0x8F;
		out[14] = 0xFE;
		break;
	case XAGA_HCI_READ_BUFFER_SIZE:
		out[7]  = 0x00;              /* ACL MTU 1024 */
		out[8]  = 0x04;
		out[9]  = 0xFF;              /* SCO MTU 255 */
		out[10] = 0x08;              /* 8 个 ACL 包 */
		out[11] = 0x00;
		out[12] = 0x04;              /* 4 个 SCO 包 */
		out[13] = 0x00;
		break;
	case XAGA_HCI_LE_READ_BUFFER:
		out[7] = 0x00;               /* LE ACL MTU 1024 */
		out[8] = 0x04;
		out[9] = 0x08;               /* 8 个 LE 包 */
		break;
	default:
		break;
	}

	return 6u + pl;
}

/*
 * skel_quirks：把控制器给的 "Unknown HCI Command" 改成苹果栈要的形状。
 * 就地改写，返回新长度；0 表示这条不需要动。
 *
 * 文档给的形状：
 *   0xFD48  1 字节
 *   0xFCE2  16 字节（子命令 0x15 / 0x36）
 *   0xFD1B  64 字节，首字节 0x51
 *   0xFCE9  无数据（给了数据下一条就不发了）
 *   其余 VSC 与 0x0C5B  无数据，status 改 done
 *   0x1002  Read_Local_Supported_Commands：把 LE 那几位 OR 进去
 */
static size_t
xaga_bt_quirk_cc(uint8_t *cc, size_t cap, size_t len)
{
	uint16_t op;
	size_t payload;

	if (cc == NULL || len < 6 || cc[0] != 0x04 || cc[1] != 0x0E) {
		return 0;
	}
	if (cc[3] != 0x01) {         /* 不是 command complete 就不碰 */
		return 0;
	}
	op = (uint16_t)(cc[4] | (cc[5] << 8));
	payload = len - 6;

	switch (op) {
	case XAGA_HCI_READ_SUPPORTED_CMDS: {
		/* payload 是 64 字节命令位图；LE Host Support 在 octet 24，
		 * LE 命令在 25..28。控制器答得出 LE 命令却把这几位留着，
		 * bluetoothd 会判 HCI initialization failed。
		 */
		if (payload < 64u) {
			return 0;
		}
		cc[6 + 24] |= 0x01;
		cc[6 + 25] |= 0xFF;
		cc[6 + 26] |= 0xFF;
		cc[6 + 27] |= 0xFF;
		cc[6 + 28] |= 0xFF;
		return len;
	}

	case XAGA_VSC_FCE2: {
		/* 子命令在命令参数里，这里只能看控制器回帧；骨架按 opcode 认，
		 * 16 字节对 0x15/0x36 都够，其他子命令多给也无害。
		 */
		size_t need = 6u + 16u;
		if (cap < need) {
			return 0;
		}
		cc[2] = (uint8_t)(3u + 16u);
		cc[6] = 0x00;
		memset(cc + 7, 0, 15);
		return need;
	}

	case XAGA_VSC_FD48: {
		size_t need = 6u + 1u;
		if (cap < need) {
			return 0;
		}
		cc[2] = (uint8_t)(3u + 1u);
		cc[6] = 0x00;
		return need;
	}

	case XAGA_VSC_FD1B: {
		size_t need = 6u + 64u;
		if (cap < need) {
			return 0;
		}
		cc[2] = (uint8_t)(3u + 64u);
		cc[6] = 0x51;
		memset(cc + 7, 0, 63);
		return need;
	}

	case XAGA_VSC_FCE9:
	case XAGA_HCI_WRITE_ERR_REPORTING:
	default: {
		/* 无数据，status 改成 done。若不是未知命令的 UIH 形状就不动。 */
		if (cc[6] != 0x01) {
			return 0;
		}
		cc[2] = 0x03;
		cc[6] = 0x00;
		return 6;
	}
	}
}

#endif /* HAKIOS_MT6895_BT_PLAT_H */
