# CMD-REVIEW-BUILD-720

Host-only isolated kernel build now authorized after root provider review. No device queries,
packaging or flashing. User's general authorization persists; each hardware trial is separate.

P=/home/lizi/IOS/hakios-mtk
R=P/logs/ufs-capacity-20261117
X=R/kernel-src

## Source and review

Root independent-adapter-review-720.json PASS for runtime driver03d08064,21frozen v2 entries,
24scenarios under both sanitizers, real XNU-header object byte-exacte98899fe/64536bytes.
Root changed TWO HOST tests in its private source only: stop_at_ring now actually calls
us_stop (old test merely timed out), and partial_response was renamed SCSI_CHECK_CONDITION
with status2 (old one was full response/nonzero status, not partial read). Runtime C remains
unchanged. Read independent-adapter-review720-v2/test-corrections.json and corrected private
test source. Append these limitations/corrections to v2 docs and archive root reports/source;
do NOT edit frozen adapter-v2. Real partial-read false returns are covered by engine faults;
MMIO faults do not become false on the real provider. Preserve original mislabeled tests.
Root's first reviewer compile omitted US_HOST_TEST and failed; source/rc1 retained separately,
then corrected independent-adapter-review720-v2 succeeded. Do not count that initial failure.

HOST-INTEGRATION-MANIFEST binds46 runtime/loader input files, arguments and full5220 source
manifest. Verify all, BUILD absent before build. Relative sealed719:4changed integration
files,6added sources/headers,0removed. Old service source remains but is not compiled/called.
Runtime support contract720 differs only cookie; host model support719 is NOT imported into
kernel. Header namespaces bsd/dev/{ufs_capacity,support} must match candidate relative includes.

Code path: init freshguard -> cleanbaseline -> us_open -> exactus_admit -> oneuc_run -> clean
DRAINED -> us_copy_done -> heldIDLE/shared1 until quiesce. ACTIVE STOP poisons, cleanDRAINED
STOP cannot be poisoned afterward. quiesce onlyus_stop. Known native UFS config remains off;
global exclusive ownership is still a prerequisite, not established by absence of known paths.

Reviewed codec/engine already passed; do not rerun them absent concern. New command is ONLY
RC10LUN2/tag0/8bytes/DBC7, fullscratch4096canary/restore, no variableREAD/WRITE/RC16/reset/retry.
Accepted query / geometry decode / Android comparison remain separate. No node/userland/block
registration. Current loader47984bytes/91e6e113, native3rc0; only two source headers from719
change (cookie and audit prefix). MainC/writer/gate unchanged; verify actual preprocessed order.

## Parser independent review

In a NEW private directory copy review-device-720.py/test-review-device-720.py/arguments720.json
and independently run86 synthetic cases. Review geometryDIFFERENT can still be observedPASS,
saturated unresolved cannot become no-medium or geometryPASS, and unaccepted raw/tail masks
cannot create geometry. Exactreturn audit must be atHLOGEOF. Root also fed11 actualHOST logs
plus synthetic audit through this parser; that is format testing only, no720runtime evidence.
Archive parser and result with this integration review; no phone operation or raw disk needed.

## Build if no concrete blocker

FromP, env XNU_SRC=absoluteX ./xnu-build.sh -k -j8, outputs onlynewX/BUILD. Native subprocess
rc/timestamps/fullstdout+stderr to R/host-checks/build720/{stdout.log,stderr.log,build-rc.txt}.
Explicit KERNEL=X/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837 for judge.
Only pre-existing symbolset allowance; separate make native rc from judge rc, no native-success
claim forrc2. Any new compile/link failure stops dependent work with logs preserved.

Verify5220 source inputs/no extras or changes outsideBUILD,46frozeninputs/argumenthash,
newcapacityinit/quiescesymbols and callsite, raw/tail/geometry/refusal messages. Oldservice/read/
probe/nativeufs objects absent, noCPUprobe. Recheck stock/shared/wrapper/719image+kernel/WIP.
Compiled strings are inclusion only. No hardware geometry/whole-disk proof follows from build.

Archive NEW docs/headset-wip/raw/ufs-capacity-integration-720/ with exact new source,metadata,
root/private reviews,parser86 and build identity/separate rc. Docs-only commit/push; GPUEBWIP
unstaged, no rawGPT/GUID/images/pre-log. No stash/reset/checkout/clean/gc/prune or productionpush.
Report blockers or kernel identity. Wait for package review and separateONEDEVICE720 release.
