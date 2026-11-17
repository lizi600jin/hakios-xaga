# Handoff718 (read latest appended status first)

P=/home/lizi/IOS/hakios-mtk; R=P/logs/ufs-response-20261117;
D=/home/lizi/IOS/repo/hakios-mt6895. User authorized autonomous progress, DSH delegation and
flashing/manual fallback. Codex never adb/fastboot; DSH exclusively owns device commands.

## Current snapshot

716/717 each executed once, clean lease/normal HLOG/automatic stock recovery, accepted block
result NOT_PROVEN.717 uniquely identified old bit5 failure: data-segment length20. Its DATA
diagnostic CRC28a2836c matched the Android reference but accepted0. Only CRC agreement, never
byte equality.717 did not collect advertised sense payload. Final docs2d1592a,22files; Codex
independently matched committed raw/body/result/device/recovery reviews.

Current last stock boot_id77b01ffe-6fc8-4c84-b0e8-0782fc8d6243, full boot_a67108864 MD5
a63b2e35a7c2522cb24a6a10332b9bc8, slot_a, Enforcing, boot_completed1 and responding SurfaceFlinger.

718 NEW candidate accepts only response segment0 or20, sense length<=18 for20, EHS length0.
Full response52 bytes/13 words; valid/attempted mask1fff. Failure bits0..6 retain field names
(bit5 now unsupported segment), bit7 overlong sense, bit8 EHS unsupported. All existing fixed
READ10/LUN2/LBA1/4096 command bytes, CRC, restore/lease/STOP, timeout and controller access remain.
EDK2 UTP_RESPONSE_UPIU is the primary layout reference linked in DESIGN-RESPONSE718.md.
No historical predicate/result changed. Actual718 sense payload remains unobserved at write.

## Verified vs pending

Host ASan+UBSan/MSan each passed engine32513 (reads5530/regs49/writes2908), contract983,
atomic40, guard23. Real-XNU-header platform ARM64 and codecs compiled native0. Evidence parser
58 synthetic cases passed, including matching diagnostic CRC not overriding a failed response.
Candidate12 + private loader39 files frozen in HOST-INTEGRATION-MANIFEST.json;5210 kernel inputs,
exactly3 changed vs717,0added/deleted. Loader compiled three native0,47264 bytes SHA256
dc1d273481eeab99086726e90762c4dca4a30e7d631fddab31990349ce6e4b25; only cookie/labels/args differ.
Engine11027 B SHA25681aeee096c339fbbf61aa23bf248b1fa24aa3b38e4c5ce26d642d35cd3afc7df.

At write, DSH has completed source/model review and is building X=R/kernel-src in its NEW BUILD;
the native build result/kernel hash/package/device are pending. No DEVICE718 release yet.
Do not rerun prepare/compile scripts into existing fresh-only directories.

## Resume action

Read DSH terminal latest tail (without --screen if viewport is scrolled), then build718 logs.
Once DSH reports no blocker, run review-kernel-718.py (5210/51 files, explicit KERNEL judge,
MachO quiesce call), preserving native make rc2 vs judge0. If PASS run package-718.py into
fresh trial718, then review-package-718.py to a new output. Full args168/228/229, GPU0,
headset0, mtk_ufs_read1, return150, watcher0, lease1. Freeze concrete identities and request
DSH independent host package review. Only after that issue a separate ONE DEVICE718 release.
Use717 device CMD as audited template, replacing every concrete identity/path/trial/boot ID
and documenting13-word response/masks/sense bounds. Recovery before optional analysis.

On real data run review-device-718.py on original raw/body, new output only. READ_OK requires
full accepted response, CRC match and clean restore. Keep CRC32 limits. Independently verify
recovery facts/whole-partition MD5/fresh boot ID, then archive final report and source package.

## Red lines

No production push, discard of preexisting WIP, old BUILD/source/image overwrite, stash/reset/
checkout/clean/gc/prune, GPUEB0x13c40000, OEM expdb/watchdog, arbitrary MMIO/getters, clock/power/
reset/UIC expansion. No manual HLOG clear/write, no mount/throughput/extra trial. Loader's
existing boot-note/HLOG writes are baseline; kernel driver sends only fixed READ. Raw GPT/GUID
stay local. Docs GPUEB-INTEGRATION.patch9cdcb0da stays untouched/unstaged. Poisoned/claimed
return lease halts before storage: never bypass it.300-second wait from actual reboot, then
bounded stock recovery/manual fastboot if necessary. Do not assume all CPUs stopped.

DSH handle term_1af4e603-ad92-41dc-b838-e243ab05af8f via
/home/lizi/.config/orca/linux-orca-cli-shim/orca terminal read/send --terminal HANDLE --json.
Hydrogen is closed. Quota codex-quota, last usable26%five-hour at~14:38; do not ask user.

## Build/package update

Host build02b13a1 has no blocker. Independent kernel review PASS:15338864 B SHA256
60bd6b195c2ab65ab0c5c1cb519fe3cf5af9ad64c351485ccbf59aa899fd7cfe, make2/judge0,5210 inputs+51 files unchanged.
New trial718 image22999040 SHA2563a3ceb1d77c9a61f289695879c077baf7ec076a8554c936aec8110afd1e61970,
MD542beb68e83eabea8a281965888380417; loaderdc1d2734; payload19533320 SHA256
a43093c1479e34debc82ebca787a3c59735aa79279a3f5ec78b686183b673aba. Root strict package PASS.
DSH independent package review is completing. CMD-DEVICE-718.md is prepared, not yet explicitly
released at this append. Phone remains last verified stock77b01ffe. No further experiment is
allowed after the single718 without a new source/review/trial. Last quota self-check19%five-hour.

## DEVICE718 release

DSH independent pack8a3aa81 has no blockers. Codex explicitly released ONE DEVICE718 after all host/package reviews. Device work is now exclusively DSH-owned; capture/stock recovery pending. No subsequent experiment authorized by this release.

## Active runtime snapshot

Experimental718 reboot native0 at2026-11-17T14:50:43.319700. DSH is within the300-second return window; no postflight verdict yet. delivery-source-718/ is an exact23-entry/19-source snapshot, identity-checked, with no raw GPT/image/serial. Do not archive a hardware PASS before raw/body/recovery reviews.

## Final718 runtime and stock (overrides all in-flight snapshots)

718 completed once: READ_OK5/claimed1/issued1/restored1/data_valid1/polls116/OCS0. Full response
valid=attempted1fff,failures0,segment20,sense18,EHS0,residual0. Block CRC28a2836c matches reference,
CRC32 scope only. HLOGreason0/fullsum26f45ab. Raw4198400 SHA256825da97d036603a4796ffafb1b4f2554fbe9d38e3a4bad37fe743d7b8d5e8719;
body497459 SHA2568f0d1e8a2a545201bdcff283aabbb21c048ea00becc93acfbee29c14c705918d.
Stock recovered automatically; full boot_a MD5a63b2e35a7c2522cb24a6a10332b9bc8, slot_a,
boot_completed1, Enforcing, fresh boot_idf745b1c6-ee43-4abc-8ed2-06ee9629bf27, SurfaceFlinger responds.
DSH initial device commitfdc574d. Root independent-device/recovery reviews PASS; see RESULT-TRIAL718.md.
No more hardware work is released. Final source/document delivery is host-only via CMD-DELIVER-UFS-718.md.
716 exact rejected field remains unknown;717 bit5 is directly captured; both old NOT_PROVEN stay.
Next technical work should start with a host-only persistent read-only block-service/lifetime design,
not unreviewed clock/reset changes or writable CPU DVFS. Current one-shot engine is not that service.
