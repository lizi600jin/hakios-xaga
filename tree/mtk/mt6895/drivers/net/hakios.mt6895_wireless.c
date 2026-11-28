/*
 * The one call the kernel's driver start path makes for the MT6895 wireless side.
 *
 * WHY THIS FILE EXISTS
 *
 * CONNSYS, BTIF and en1 are three separate drivers (hakios.mt6895_connsys.c,
 * _bt.c, _cell.c), each with its own boot argument (wifi=2, bt=2, cell=2) and
 * each able to say "not asked for" without touching a register. What they do
 * *not* each have is a place in the kernel integration's init list: that file
 * (bsd/dev/hakios.input.c in the kernel integration snapshot) already has 21
 * entries and is edited by other people's work. Adding three more lines there,
 * and keeping them in the right order, is the kind of edit that silently rots:
 * a driver that runs before CONNSYS has mapped its windows reads an unmapped
 * address and aborts the kernel instead of returning.
 *
 * So the order lives here, next to the three drivers it orders, and the
 * integration calls one function:
 *
 *     hakios_mt6895_wireless_init();
 *
 * THE ORDER, AND WHY IT IS THIS ORDER
 *
 *   1. CONNSYS (wifi=2). Maps the four 0x184x windows read-only and checks the
 *      EMI physical base against the factory one. Nothing below can run first:
 *      the BTIF aliases at 0x11300B00/0x11300B80 are only meaningful once the
 *      conn MCU they belong to has been probed, and a wrong EMI base has to
 *      stop the boot before anything is loaded into it.
 *   2. BTIF (bt=2). Maps its own window at 0x1100C000 and, when asked, runs the
 *      WMT announce. It does not depend on CONNSYS being *started*, only on the
 *      base being right; the order is kept anyway because the conn MCU owns it.
 *   3. en1 (cell=2). Independent of the other two: the DPMAIF AO page at
 *      0x10214000 is powered before the modem, so it is mapped and its
 *      interrupt mask read last, when nothing above can still fail.
 *
 * Every step returns 0 both for "done" and for "not asked for", which is
 * deliberate (see the individual drivers). The first non-zero return stops the
 * chain and is handed back to the integration, so the boot log shows which
 * seam refused rather than three drivers each claiming success.
 *
 * WHAT IS NOT HERE
 *
 * wlan_drv_gen4m itself. The MAC/PHY stack is being built separately as
 * libgen4m_soc7_core.a (see docs/headset-wip/raw/wifi-gen4m-*.md); it is not
 * a start-path driver yet, because it still needs the XNU KAL layer to close.
 * When it is, its start goes after CONNSYS and before BTIF in this function --
 * it will read the same four windows this file's first step maps.
 */

#include <sys/types.h>

int hakios_mt6895_connsys_start(void);
int hakios_mt6895_bt_start(void);
int hakios_mt6895_cell_start(void);

static int gWirelessInitDone;

int
hakios_mt6895_wireless_init(void)
{
	int rc;

	/*
	 * The integration may reach this from more than one path (a boot-time
	 * init list and, later, a kext load). Each driver below is stateful
	 * (windows, worker thread) and maps fresh memory on every call, so
	 * running the chain twice would map the same windows twice and leak
	 * the first mapping. One shot, reported once.
	 */
	if (gWirelessInitDone) {
		return 0;
	}
	gWirelessInitDone = 1;

	rc = hakios_mt6895_connsys_start();
	if (rc != 0) {
		return rc;
	}

	rc = hakios_mt6895_bt_start();
	if (rc != 0) {
		return rc;
	}

	rc = hakios_mt6895_cell_start();
	if (rc != 0) {
		return rc;
	}

	return 0;
}
