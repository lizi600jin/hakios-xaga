#!/bin/sh
# The wireless/Bluetooth/modem work, checked without a phone.
#
# Three layers, cheapest first, so a broken header is reported before anything
# is linked against it:
#
#   1. every header that goes into the kernel compiles on its own, -Werror.
#      A header with an unresolved type is a header that will fail inside
#      somebody else's translation unit, at the least convenient moment.
#   2. the three drivers compile against a stub kernel (HAKIOS_HOST_TEST):
#      ml_io_map hands out RAM, PE_parse_boot_argn reads a table. This catches
#      signature and type mistakes in code that cannot run here.
#   3. the same sources are linked into one binary with the real protocol
#      implementations, and the logic that does not need hardware is exercised:
#      the EMI guard, nvdata parsing, the ROM-patch container check, and en1's
#      DHCP/ARP answers.
#
# What none of this covers is the phone: BTIF's register offsets, the EMI base,
# the firmware itself. Those are named in the runbook, not tested here.
#
# Usage: tools/check-wireless.sh [build-dir]   (default /tmp/hakios-wireless)

set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
NET="$ROOT/tree/mtk/mt6895/drivers/net"
OUT="${1:-/tmp/hakios-wireless}"
CC=${CC:-cc}

mkdir -p "$OUT"

echo "== 1. headers, each on its own (-Werror) =="
for h in hakios_mt6895_wireless_addrs.h \
         hakios_mt6895_connsys_nvdata.h \
         hakios_mt6895_bt_wmt.h \
         hakios_mt6895_cell_en1.h; do
	$CC -std=c11 -Wall -Wextra -Werror -I "$NET" -fsyntax-only -x c "$NET/$h"
	echo "   ok  $h"
done

echo "== 2. drivers, against a stub kernel (-Werror) =="
for c in hakios.mt6895_connsys.c hakios.mt6895_bt.c hakios.mt6895_cell.c; do
	$CC -std=c11 -Wall -Wextra -Werror -DHAKIOS_HOST_TEST -I "$NET" \
		-fsyntax-only "$NET/$c"
	echo "   ok  $c"
done

echo "== 3. logic test =="
$CC -std=c11 -Wall -Wextra -O2 -DHAKIOS_HOST_TEST -I "$NET" \
	-o "$OUT/xaga_wireless_host_test" "$ROOT/tools/xaga_wireless_host_test.c"
"$OUT/xaga_wireless_host_test"

echo
echo "all wireless checks passed (host side; the phone is a separate step)"
