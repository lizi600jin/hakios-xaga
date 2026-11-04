#!/bin/bash
#
# Put a partition for the system image on the XAGA, once, without touching anything else.
#
#   HAKIOS_SERIAL=<serial> mtk/tools/xaga-carve.sh --gib 2.5          # work out and show the plan
#   HAKIOS_SERIAL=<serial> mtk/tools/xaga-carve.sh --gib 2.5 --write  # and actually write it
#
# Why this is its own script and not part of xaga-cycle.sh: changing the GPT is the one thing
# here that cannot be undone by flashing a boot image. Every other step in this port either
# restarts into Android on its own or is fixed by `fastboot flash boot_a backup/boot_a_magisk.img`;
# a broken partition table leaves the phone with no by-name links and no fastboot slot to flash.
# So it reads, computes, and prints -- and needs --write before it puts anything on the disk.
#
# What it does, in order:
#   1. Asks the disk how big it is, and how big its logical blocks are. The XAGA answers 4096,
#      not 512: reading LBA 1 with bs=512 comes back all zeros, because LBA 1 is byte 4096.
#      Getting this wrong is silent -- the numbers all parse, they just describe nothing.
#   2. Reads the primary GPT (blocks 0..33) and the backup (the last 34 blocks) into backup/gpt/.
#      Both dumps are made on the phone and pulled: piping `adb shell su -c dd` into a file
#      corrupts bytes (a pty is not a pipe), which is how the first attempt produced 3235 nonzero
#      bytes of noise instead of a partition table.
#   3. Runs gpt-carve.py, which re-checks both copies' checksums, moves the boundary of userdata
#      back by --gib and puts xnusys in the space. It writes <dump>.new beside each dump.
#   4. Verifies the two .new images agree with each other and with the disk's size.
#   5. With --write only: pushes them back with dd conv=notrunc and syncs. The printout is the
#      exact commands, so they can be run by hand if this script is not what is wanted.
#
# After this, run mtk/tools/system-flash.sh to put userland/system.apfs into xnusys.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=${HAKIOS_SERIAL:?set HAKIOS_SERIAL to the serial adb devices shows}
GIB=${XAGA_SYS_GIB:-2.5}
NAME=${XAGA_SYS_PART:-xnusys}
VICTIM=${XAGA_SYS_SHRINK:-userdata}
WRITE=${XAGA_SYS_WRITE:-0}
OUT=${XAGA_GPT_OUT:-$ROOT/backup/gpt}
LUN=${XAGA_LUN:-/dev/block/sdc}          # the disk the GPT is on, not a partition of it

while [ $# -gt 0 ]; do
    case "$1" in
        --gib)    GIB=$2; shift 2 ;;
        --name)   NAME=$2; shift 2 ;;
        --shrink) VICTIM=$2; shift 2 ;;
        --lun)    LUN=$2; shift 2 ;;
        --write)  WRITE=1; shift ;;
        -h|--help) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "xaga-carve: unknown argument $1" >&2; exit 1 ;;
    esac
done

say() { echo "xaga-carve: $*"; }
die() { echo "xaga-carve: $*" >&2; exit 1; }

command -v adb >/dev/null || die "no adb on PATH"
adb -s "$S" get-state >/dev/null 2>&1 || die "no phone at $S (adb devices)"
adb -s "$S" shell "su -c id" >/dev/null 2>&1 || die "no root on $S: this needs su"

mkdir -p "$OUT"

# ---- 1. the disk, and the size of a logical block ----
TOTAL_BYTES=$(adb -s "$S" shell "su -c 'blockdev --getsize64 $LUN'" | tr -d '\r')
case "$TOTAL_BYTES" in ''|*[!0-9]*) die "could not read the size of $LUN (got '$TOTAL_BYTES')" ;; esac
say "disk: $TOTAL_BYTES bytes ($(python3 -c "print('%.2f' % ($TOTAL_BYTES / 2**30))") GiB)"

# The header is at LBA 1, so its offset IS the block size. 4096 is tried first because that is
# what this phone answers; 512 stays for a disk that turns out to be the ordinary kind.
LBA=0
for cand in 4096 512; do
    if adb -s "$S" shell "su -c 'dd if=$LUN bs=$cand skip=1 count=1 2>/dev/null'" | head -c 8 | grep -q "EFI PART"; then
        LBA=$cand; break
    fi
done
[ "$LBA" != 0 ] || die "no GPT header at LBA 1 read as 512 or 4096 bytes: $LUN is not a GPT disk"
TOTAL=$((TOTAL_BYTES / LBA))
say "logical block $LBA bytes, $TOTAL blocks"

# ---- 2. read both copies (on the phone, then pull: a pty would corrupt them) ----
TAIL=34                                  # protective MBR + header + a 128-entry array
SKIP=$((TOTAL - TAIL))
adb -s "$S" shell "su -c 'dd if=$LUN of=/data/local/tmp/gpt-primary.bin bs=$LBA count=$TAIL 2>/dev/null'" >/dev/null
adb -s "$S" shell "su -c 'dd if=$LUN of=/data/local/tmp/gpt-secondary.bin bs=$LBA skip=$SKIP count=$TAIL 2>/dev/null'" >/dev/null
adb -s "$S" pull /data/local/tmp/gpt-primary.bin "$OUT/gpt-primary.bin" >/dev/null
adb -s "$S" pull /data/local/tmp/gpt-secondary.bin "$OUT/gpt-secondary.bin" >/dev/null
adb -s "$S" shell "su -c 'rm -f /data/local/tmp/gpt-*.bin'" >/dev/null
say "read $TAIL blocks from each end into $OUT"

# ---- 3. work out the new layout ----
python3 "$ROOT/mtk/tools/gpt-carve.py" "$OUT/gpt-primary.bin" "$OUT/gpt-secondary.bin" "$TOTAL" \
    --shrink "$VICTIM" --name "$NAME" --gib "$GIB" --lba-size "$LBA"

# ---- 4. the two images have to agree with each other, and with the disk ----
python3 - "$OUT/gpt-primary.bin.new" "$OUT/gpt-secondary.bin.new" "$TOTAL" "$LBA" "$NAME" <<'PY' || exit 1
import struct, sys, binascii
pri_p, sec_p, total, lba, name = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]

def load(path, first):
    """The header and array of one copy, both located through the blocks the header itself names."""
    d = open(path, "rb").read()
    i = d.find(b"EFI PART")
    if i < 0:
        sys.exit(f"gpt-carve: {path}: no header")
    sig, rev, hsize, hcrc, _, mylba, altlba, first_u, last_u = struct.unpack_from("<8sIIIIQQQQ", d, i)
    body = bytearray(d[i:i + hsize])
    struct.pack_into("<I", body, 0 + 16, 0)
    if binascii.crc32(bytes(body)) & 0xffffffff != hcrc:
        sys.exit(f"gpt-carve: {path}: header CRC does not hold")
    entlba, nent, esz, entcrc = struct.unpack_from("<QIII", d, i + 72)
    eo = (entlba - first) * lba
    ents = d[eo:eo + nent * esz]
    if binascii.crc32(ents) & 0xffffffff != entcrc:
        sys.exit(f"gpt-carve: {path}: entry array CRC does not hold")
    return ents, nent, esz, mylba, altlba, first_u, last_u

pc = load(pri_p, 0)
sc = load(sec_p, total - 34)
if pc[0] != sc[0]:
    sys.exit("gpt-carve: the two copies describe different partitions")
if (pc[3], pc[4]) != (1, total - 1) or (sc[3], sc[4]) != (total - 1, 1):
    sys.exit(f"gpt-carve: header LBAs are wrong ({pc[3]}/{pc[4]} and {sc[3]}/{sc[4]}, total {total})")
if pc[5:] != sc[5:]:
    sys.exit("gpt-carve: the two copies disagree on the usable range")

ents, nent, esz = pc[0], pc[1], pc[2]
found = False
for i in range(nent):
    e = ents[i * esz:(i + 1) * esz]
    if e[0:16] == b"\0" * 16:
        continue
    nm = e[56:128].decode("utf-16-le").rstrip("\0")
    if nm == name:
        st, en, _ = struct.unpack_from("<QQQ", e, 32)
        print(f"xaga-carve: {name} at {st}..{en} ({(en - st + 1) * lba / 2**30:.2f} GiB), block {st * lba}")
        found = True
if not found:
    sys.exit(f"gpt-carve: {name} is not in the new table")
print(f"xaga-carve: both copies verified against a {total}-block disk of {lba}-byte blocks")
PY

# ---- 5. write, only when asked ----
if [ "$WRITE" != 1 ]; then
    say "nothing written. The commands, if you would rather run them yourself:"
    printf '  adb -s %s push %s /data/local/tmp/gpt-primary.bin\n' "$S" "$OUT/gpt-primary.bin.new"
    printf '  adb -s %s push %s /data/local/tmp/gpt-secondary.bin\n' "$S" "$OUT/gpt-secondary.bin.new"
    printf "  adb -s %s shell \"su -c 'dd if=/data/local/tmp/gpt-primary.bin of=%s bs=%s count=%s conv=notrunc; sync'\"\n" \
        "$S" "$LUN" "$LBA" "$TAIL"
    printf "  adb -s %s shell \"su -c 'dd if=/data/local/tmp/gpt-secondary.bin of=%s bs=%s seek=%s count=%s conv=notrunc; sync'\"\n" \
        "$S" "$LUN" "$LBA" "$SKIP" "$TAIL"
    printf '  adb -s %s reboot\n' "$S"
    say "or run this again with --write"
    exit 0
fi

say "writing. If anything goes wrong here the phone's partition table is gone:"
say "  the backup of what is there now is $OUT/gpt-primary.bin and $OUT/gpt-secondary.bin"
adb -s "$S" push "$OUT/gpt-primary.bin.new" /data/local/tmp/gpt-primary.bin >/dev/null
adb -s "$S" push "$OUT/gpt-secondary.bin.new" /data/local/tmp/gpt-secondary.bin >/dev/null
adb -s "$S" shell "su -c 'dd if=/data/local/tmp/gpt-primary.bin of=$LUN bs=$LBA count=$TAIL conv=notrunc'" >/dev/null 2>&1 || \
    die "writing the primary failed; the old copy is in $OUT and the disk still has it"
adb -s "$S" shell "su -c 'sync'"
adb -s "$S" shell "su -c 'dd if=/data/local/tmp/gpt-secondary.bin of=$LUN bs=$LBA seek=$SKIP count=$TAIL conv=notrunc'" >/dev/null 2>&1 || \
    die "writing the backup failed; the primary is already new, run this again before rebooting"
adb -s "$S" shell "su -c 'sync'"
adb -s "$S" shell "su -c 'rm -f /data/local/tmp/gpt-*.bin'" >/dev/null
say "written. Check before rebooting:"
say "  adb -s $S shell \"su -c 'ls -l /dev/block/by-name/$NAME'\""
say "then: adb -s $S reboot"
