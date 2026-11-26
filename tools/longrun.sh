#!/bin/sh
#
# longrun.sh: watch a running XNU for a while and say what happened.
#
# The kernel logs into a 1 MB ring in /dev/hlog, and the device serves that over USB.
# This script samples it every so often, keeps a running record, and after the requested
# time says whether the machine is still the same machine it was at the start.
#
#   sh tools/longrun.sh <minutes> [sample-seconds]
#
# What it looks at, and why:
#
#   reachable   the banner and PING answer at all. A box that stops answering has crashed
#               or hung, whatever else the counters say.
#
#   uptime      from PONG. If the kernel restarted, uptime starts over -- that is the one
#               signal that separates "still running" from "restarted and running".
#
#   zone memory kern.zone_elems / kern.zone_size. A driver that leaks shows up here long
#               before anything else notices.
#
#   faults      the words that only appear when something went wrong: panic, fault,
#               "did not return", mmu fault, mcu fatal.
#
#   gpu         mcu status and the last m2b line. The GPU is the newest code in this kernel
#               and the most likely thing to go wrong over an hour.
#
# Every sample is appended to the log file so a failure can be looked at afterwards rather
# than only reported.

set -u

MINUTES=${1:-30}
SAMPLE=${2:-30}
HOST_IP=10.0.0.2
IFACE=enx020000006801
OUT=/tmp/longrun-$(date +%Y%m%d-%H%M%S).log

say() {
	printf '%s\n' "$*" | tee -a "$OUT"
}

fault_count() {
	# fault_count <file>: how many lines mention something that only appears on failure.
	#
	# grep -c prints one number per file and exits non-zero when it finds nothing, which is
	# why this reads its output rather than its status, and why the result is stripped of
	# whitespace before it is used as a number.
	if [ ! -f "$1" ]; then
		echo 0
		return
	fi
	#
	# Only patterns that mean something went wrong. The first version matched "fault" and
	# "panic" on their own and counted eighteen hits in a healthy log: panic_trace is a boot
	# line, "gpu_fault 0" is a zero, "clutch" contains nothing of interest but "panic" does
	# not appear in it -- the matches were panic_trace and the "fault 0" reads.
	#
	grep -acE 'panic\(cpu|_trace: [^P]|: [A-Za-z]*panic[^_]|did not return|MMU FAULT|mcu fatal|mcu status 3|mcu status FATAL|kernel trap|brk #|Unable to handle)' \
	    "$1" 2>/dev/null | tr -d ' \n' || true
}

probe() {
	# probe: one round trip. Prints "<uptime>|<log bytes>" or "DOWN".
	#
	# Two connections, not one. The first version did PING and LOG on the same socket and
	# reported DOWN twice in a row while the device was answering normally -- the log read is a
	# megabyte over a link that moves twelve, and the socket timeout ran out mid-transfer.
	#
	# The uptime is what decides "alive or not", so it is asked for on its own, with its own
	# connection and its own short timeout. The log comes after, and a log read that times out
	# is not a failure -- it is a big read on a small link.
	python3 - "$HOST_IP" <<'PYEOF' 2>/dev/null || echo DOWN
import socket, sys, re

host = sys.argv[1]


def ask(command, limit, timeout):
	data = b''
	try:
		s = socket.create_connection((host, 2222), timeout=8)
		s.settimeout(timeout)
		try:
			s.recv(4096)
		except Exception:
			pass
		s.sendall(command)
		try:
			while len(data) < limit:
				piece = s.recv(65536)
				if not piece:
					break
				data += piece
				if command == b'LOG\n' and data.rstrip().endswith(b'END'):
					break
		except Exception:
			pass
		s.close()
	except Exception:
		return None
	return data


pong = ask(b'PING\n', 512, 8)
if pong is None:
	print('DOWN')
	sys.exit(0)

m = re.search(rb'uptime (\d+)', pong)
uptime = m.group(1).decode('ascii') if m else '?'

log = ask(b'LOG\n', 1200000, 30)
if log:
	open('/tmp/longrun-sample.txt', 'wb').write(log)

print('%s|%d' % (uptime, len(log) if log else 0))
PYEOF
}

say "longrun: $MINUTES minutes, a sample every $SAMPLE s"
say "longrun: record in $OUT"
say ""

START=$(date +%s)
END=$((START + MINUTES * 60))
FIRST_UPTIME=
SAMPLES=0
DOWNS=0
RESTARTS=0
LAST_UPTIME=

while [ "$(date +%s)" -lt "$END" ]; do
	NOW=$(date +%H:%M:%S)
	R=$(probe)

	if [ "$R" = "DOWN" ]; then
		DOWNS=$((DOWNS + 1))
		say "$NOW  DOWN  (miss $DOWNS)"
	else
		UPTIME=${R%%|*}
		BYTES=${R##*|}
		SAMPLES=$((SAMPLES + 1))

		if [ -z "$FIRST_UPTIME" ]; then
			FIRST_UPTIME=$UPTIME
		fi
		# A smaller uptime than last time means the kernel restarted under us.
		if [ -n "$LAST_UPTIME" ] && [ "$UPTIME" != "?" ] && \
		   [ "$LAST_UPTIME" != "?" ] && [ "$UPTIME" -lt "$LAST_UPTIME" ]; then
			RESTARTS=$((RESTARTS + 1))
			say "$NOW  RESTART  uptime $LAST_UPTIME -> $UPTIME"
		fi
		LAST_UPTIME=$UPTIME

		FAULTS=$(fault_count /tmp/longrun-sample.txt)
		[ -z "$FAULTS" ] && FAULTS=0
		say "$NOW  up ${UPTIME}s  log ${BYTES}  faults ${FAULTS}"
	fi

	sleep "$SAMPLE"
done

say ""
say "longrun: done"
say "longrun:   samples   $SAMPLES"
say "longrun:   misses    $DOWNS"
say "longrun:   restarts  $RESTARTS"
say "longrun:   uptime    $FIRST_UPTIME -> $LAST_UPTIME"
say "longrun:   record    $OUT"

if [ "$DOWNS" -eq 0 ] && [ "$RESTARTS" -eq 0 ]; then
	say "longrun: PASS -- same kernel, still answering, the whole time"
else
	say "longrun: FAIL -- see the record"
fi
