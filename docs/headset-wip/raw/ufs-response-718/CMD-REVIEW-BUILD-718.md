# REVIEW-BUILD-718: host only

P=/home/lizi/IOS/hakios-mtk; R=P/logs/ufs-response-20261117; X=R/kernel-src.
Read DESIGN-RESPONSE718.md, actual candidate and diff against sealed717. Verify51 manifest
files and5210 kernel inputs. BUILD must be absent initially. Exactly3 changed sources vs717,
zero added/deleted; no production edit. Frozen716/717 and their NOT_PROVEN verdicts stay intact.

## Source/protocol review

Unlike717, this intentionally changes response acceptance using the actual717 rejected field
and primary UFS response layout. Independently review byte offsets/endian,20-byte sense
container bound, <=18 sense length, EHS0,13-word masks/collection/printing and no other relaxed
field. Full response is1fff, notff. Nonzero OCS still bypasses mask computation; missing DATA
diagnostic still means uncollected, not CRC0. Diagnostic CRC cannot override a failed response.
Verify all fixed command bytes/controller access/restore/timeout/lease/STOP paths unchanged.
One DMA READ remains one command. No data-segment-based resize of the disk transfer exists.

Independently rerun changed engine tests in a NEW private directory (ASan+UBSan/MSan), expected
32513/reads5530/regs49/writes2908. Review positive sense0/18, malformed lengths, EHS rejection,
13 partial response callbacks and1024 partial DATA failures. Unchanged atomic/guard need not
be rerun absent concern. Root contract983/atomic40/guard23 and ARM64 real XNU header compile
already passed. Root loader47264/dc1d2734, three rc0; only trial labels/cookie/arguments differ
from717. Check actual compiled/preprocessed placement, no extra native reproduction if no concern.

## Isolated kernel build if no blocker

From P run XNU_SRC=absoluteX ./xnu-build.sh -k -j8, using only X/BUILD. Record actual subprocess
rc and full logs/timestamps. Please use R/host-checks/build718/{stdout.log,stderr.log,build-rc.txt}
for the new logs. Explicit KERNEL=X/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837
for tools/make-kernel.sh --judge. Only established symbol-set allowance; keep native make rc
separate from judge rc. Report new compiler/link errors. Recheck all5210 inputs/no extras
outside BUILD and all51 manifest files after build; record kernel identity/init/quiesce/messages.
String inclusion is not IO evidence. Recheck stock/shared/wrapper and sealed716/717 artifacts.

Archive NEW D/docs/headset-wip/raw/ufs-response-718/ with manifest, changed sources, actual
diff/protocol review, private model results and build report. No raw GPT, disk GUID or image
binaries uploaded. Return blockers/identities/native rc/judge rc and docs commit.

No packaging or device query/flash in this CMD. No candidate edit, production push, old
BUILD/artifact overwrite, GPUEB WIP staging, stash/reset/checkout/clean/gc/prune,
clock/power/reset/UIC/watchdog/OEM expdb. Await separate pack/device command.
