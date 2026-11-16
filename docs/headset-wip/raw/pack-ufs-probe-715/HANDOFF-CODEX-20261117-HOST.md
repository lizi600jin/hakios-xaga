# Current handoff: CPU713 evidence and UFS714/715 host packages

2026-11-17. P=`/home/lizi/IOS/hakios-mtk`;
D=`/home/lizi/IOS/repo/hakios-mt6895`. This supplements the older CPU EOL handoff;
historical statements that713 was unbuilt/unpacked/unflashed are superseded below.

## Current state and next action

CPU713 ran once. Its HLOG and96 paired raw LUT rows were independently checked.
Android-side reboot-to-fastboot subsequently timed out. The user manually entered fastboot;
DSH restored stock and verified the whole boot_a MD5. No unattended recovery success is claimed.

UFS revision2 kernel is built and independently reviewed.714 and715 are host packages only,
never flashed.715 deliberately disables the runtime and loader GPU experiment paths while
retaining the exact714 kernel and payload. No production driver was installed.

The selected scope after recovery is HOST ONLY for the remainder of this run. No device
commands, including status queries, are needed to finish it. The user has gone to sleep;
do not start another run that may require their manual fastboot recovery.

First on resumption: read this file, `REVIEW-AND-PLAN-715.md`, the archived DSH host715 review,
and `RETURN-ANDROID-FOLLOWUP-713.md` including its final correction. Check the latest docs
commit and current task ownership before any mutation. Resolve remaining host-review objections
first. A later device experiment needs a separate concrete DEVICE plan accounting for the
actual recovery incident; neither host package is a device execution command.

## Device snapshot: last verified, not newly queried

- Original stock restored, slot `_a`, boot_completed1, SELinux Enforcing; SurfaceFlinger responds.
- boot_a67108864 bytes, MD5 `a63b2e35a7c2522cb24a6a10332b9bc8`.
- Last boot_id `c8acf0e0-ec30-4b5a-906b-fe29e1a74a9d`.
- Proof: `P/logs/mt6895-cpu-baseline-20261116/device713/D-recovery-RETRY.txt` and `D-verify.txt`.
- The first recovery attempt is preserved as failed, with adb reboot timeout and no fastboot.

Only DSH owns phone operations. Codex does not run adb/fastboot directly.

## CPU713: independently verified and limits

T=`P/logs/mt6895-cpu-baseline-20261116`.

- Kernel15321336 bytes, SHA256
  `a80231ed9dc78a82bfddc71f34685808e66ce490d70ed059e37cd108638885c3`.
  Frozen5206 inputs checked; make native2 and explicit KERNEL judge0 are separate outcomes.
- `T/trial713/boot.img`22994944 bytes, SHA256
  `127e48e9c7089e9845bfd0a3565ccacfb1b80acd3d84776b3946fa8a702b165c`.
- `T/device713/trial713-logbook.bin`4198400 bytes, SHA256
  `310c2f0a8084019c8b9f72d877b7d7f0670d7eda0b952d9dd991c8c73f073213`.
- HLOG body519004 bytes, SHA256
  `7fbecaa318c1e1496c23f1fedfe8d9e4d66ed816f141faa0587efb92345b87d8`.
  reason0, full integer sum `0x2865061` matches header; not a low-byte checksum.
- Three domains each had two equal samples and32 unique paired rows. ENABLE values were
  `0x201,0,0`; STATUS all3; request indices `0,19,28`. The exact enable==1 predicate failed.
  CPU_NOT_ENABLED describes that software predicate, not proof of physically disabled CPUs.
- Offline low12-bit vendor-table interpretation gave25/32/29 rows and selected entries
  2000/1400/500 MHz. These are table/request decodes, not measured clocks or permission to write.

See `independent-device-review.json`, `offline-lut-interpretation.json`, `RESULT-TRIAL713.md`.
Raw committed blobs in D were also checked against the host originals.

Five CPU write-protocol unknowns remain: ownership between cpufreq-hw and cpuhvfs; mapping
of four cpuhvfs tbl-off groups to domains; required MCUPM initialization; who sets STATUS;
and USRAM/ESRAM versus CSRAM overlap. They block writable frequency control or firmware/SRAM
changes. They do not invalidate the recorded read-only snapshots or their explicitly limited
offline decode. Do not bypass the strict predicate by writing ENABLE/STATUS or guessing a mask.

## UFS revision2 and package identities

R=`P/logs/ufs-probe-20261117`.

- Source `R/candidate/hakios.mt6895_ufs.c`,8138 bytes, SHA256
  `ce6aa5850fba8d318e44cdb18e2f3c9592820a12e27c1a1e3847cede730045f2`.
- Real-XNU ARM64 object13168 bytes, SHA256
  `5a725268923e76736e99bb0bc6c87c21859742ba99e3c3f21f16548110703237`.
- Kernel `R/kernel-src/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837`,15321328 bytes,
  SHA256 `ecdc9194fbcc635a63d8da6be7c5f4ef6a1c6bc7f3c1b715c9373cca9fe7da38`.
  Frozen5206 input files checked; make2 / judge0. No CPU probe grafted into this tree.
- Model:2324 cases with native0 under ASan/UBSan and MSan; real headers ARM64 compile0.
  Supplement:32 formatted CAP slot-count assertions. Kernel source unchanged afterward.
- 714 boot22994944 bytes, SHA256
  `91122cdfc72ce3d204bb6ffdd71c1bca7c470e525d28cfdd7ce19bb014231f07`.
- 715 boot22994944 bytes, SHA256
  `65bd797dce99a4f29d898c7055a25fcce207e1b3642109daae0ad217f653c994`.
- 715 loader46504 bytes, SHA256
  `c3d9c30a0c14c3e6c644fbf499fc189819b818c03080fe2beee18590cf7eafea`.
- Shared714/715 payload19515784 bytes, SHA256
  `c7d73935a1338a3e75cba6d01da536e7352c0a73dd2a51c18ae94239b1fe97c3`.

The probe reads two snapshots of nine whitelisted UFSHCI offsets. It has no register writes,
UIC/SCSI commands, queue takeover or DMA. Initialized snapshots plus attempted/valid masks
prevent failed reads printing uninitialized or callback-clobbered values. All-ones/zero-version
responses are untrusted; IS is printed but excluded from stability comparison.
FLAGS_OBSERVED is neither a queue-ownership result nor permission for I/O.
MMIO abort is not a callback false return; no hardware execution has validated the mapping.

I favor715 for a later storage diagnostic because unrelated experimental GPU software paths
are disabled. This does not establish physical GPU state or fix warm return. Existing loader
UFS uses LK's live transfer list (`UFS_USE_LK_LIST=1`, `UFS_HOST_RESET` undefined), so the old
stop/rebase/restart branch must not be copied as an automatic kernel takeover recipe.
Actual storage I/O still requires explicit ownership/DMA/stop/reclaim design, including return
to the loader's HLOG writer. This probe does not implement that driver.

## Corrections that must travel with the artifacts

1. Loader cleanup DOES call mali_power_down under BOARD_MALI_POWER before returning to Android.
   Absence of a named kernel Mali610 quiesce hook is not proof of no cleanup or of the stall cause.
2. Stock v4 signature_size is4096 at offset1580, not0 at1584. Signatures are byte-preserved;
   this does not validate them for a modified kernel. Strict validators used the correct field.
3. During714 packaging, incorrect output remapping overwrote713 intermediate loader/object
   files.713 boot.img/payload were not changed, and loader.bin was restored to its original
   recorded hash. Original object hashes were absent, so their historical identity is unproven.
   Preserve the incident. The new helper audits all outputs and uses a private directory.
4. Read-only callback types do not guarantee no writes; source audit is the basis here. Reads
   may still fault or have hardware effects. UCRDY report-only is a probe choice, not a universal
   vendor-driver rule.

## Evidence and repository ownership

D already contains CPU713 raw evidence, UFS revision2 source/build review and714 host review.
DSH completed715 artifact/source review at docs commit `90937e2` under
`docs/headset-wip/raw/pack-ufs-probe-715/`. DSH did not repeat the private loader compile;
Codex independently executed that reproduction outside the packaging workflow and matched
both objects and loader. This attribution is retained, not described as two execution runs.
Supplementary archive/corrections are being finalized; consult the latest docs report.

R contains `REVIEW-AND-PLAN-715.md`, `CMD-HOST-REVIEW-715.md`, `arguments715.json`,
`review-package-715.py`, `reproduce-loader-safe-715.py`, `audit-host-715.py`,
`independent-package-review-715.json`, `independent-loader-review-715.json`,
`POST-HOST-AUDIT-715.json`, and trial715's planned/executed argv, macros and artifact hashes.
715 was built without changing the kernel, source mirrors or production inputs.
`independent-gpu-path-review-715.json` records the complete `-E -P` preprocessor output
under `host-review/loader715-preprocessed.c`. Named loader GPU power functions and calls are
absent there. Missing debug strings alone would not prove absence of arbitrary machine code.

Latest host audit verified eight frozen production inputs, both original UFS candidate files,
six retained713/714 artifacts, five715 artifacts, the stock host image and GPUEB WIP hash.
This is not a new measurement of the phone. The source snapshots/build trees must stay sealed.

DSH existing Orca terminal: `term_ac0b07f9-7356-4783-9861-1e6ffafa30f2`.
CLI: `/home/lizi/.config/orca/linux-orca-cli-shim/orca`.
Hydrogen is closed by user instruction. It returned no review; do not wait or consume calls.

## Hard boundaries

- No stash/reset/checkout/clean/gc/prune; no production repository push.
- Preserve all pre-existing production modifications; do not overwrite any old BUILD/images.
- GPUEB WIP in D remains unchanged and unstaged, SHA256
  `9cdcb0da826e9bd561fce98aae81304789fee2742c63b10235942f028e53eb16`.
- No GPUEB0x13c40000, OEM expdb, watchdog, PMIC getter, arbitrary register scan or SRAM writes.
- No slot/SELinux changes, no clearing/writing raw HLOG, no guessed reset/power/clock sequence.
- DSH owns device operations. No device access this host phase, no unattended retry.
- User acceptance of headphone sound remains; sample rate/bit depth remain deferred. No new
  claims about audio silence, real CPU frequency, UFS I/O or complete iOS functionality.
