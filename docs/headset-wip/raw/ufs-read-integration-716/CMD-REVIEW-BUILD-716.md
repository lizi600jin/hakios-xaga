# Review/build716: HOST ONLY, no image packaging or device execution

DSH owns review/build/archive. Codex owns candidate changes; all current inputs are frozen in
HOST-INTEGRATION-MANIFEST.json. Do not edit them. Preserve existing evidence and production WIP.
P=/home/lizi/IOS/hakios-mtk; R=P/logs/ufs-read-path-20261117; X=R/kernel-src.

Read DESIGN-READ716.md with the corrections below, candidate platform/lease/engine/guard,
the actual patched loader, and both kernel/loader preparation reports.

## Important final integration facts

1. The active loader's logbook_boot_note is its last storage operation before XNU and leaves
   a WRITE descriptor. The new private loader calls ufs_read_guard_start AFTER that note:
   exactly one READ of logicalLBA1 via existing ufs_read(8,8,UR_DATA_PA), with independent
   CRC0x28a2836c comparison. Only after success does it initialize the shared lease.
   This loader READ is distinct from the kernel READ; kernel validates A5 prefill/zero response
   before ringing, so old GPT data is not accepted as a fresh transfer.
2. ufs_read_guard_before_storage is the FIRST statement of xnu_returned, BEFORE HLOG/gpt/UFS
   writes. Claimed/poisoned/invalid lease halts for manual fastboot; no reset/watchdog, flash
   or warm Android handover is attempted on that failure path. It may lose the current HLOG.
3. Kernel quiesce is wired after SMP-stop and beside the headset software quiesce. It sets
   software STOP and may publish POISONED to shared DRAM. No register access, waits or printf.
   Other CPUs may not all have stopped; verify CAS/publication ordering against that fact.
4. The cached phase is statically initialized once and is NEVER reset by init. Actual mapping
   uses VM_WIMG_WCOMB for fixed non-managed DRAM. No atomic operation targets Device memory.
5. The kernel's only controller store is UTRLDBR=1. No IS/clear/clock/power/UIC/base/RSR store.
   Exact fixed READ10/LUN2/LBA1/4096 command, saved memory restore and full readback are required.

## Review, native checks and private loader

- Verify the entire manifest,5210 kernel inputs, kernel integration diff and absence of BUILD.
  Relative to715:4 changed existing files and4 added driver/header files. No CPU probe.
  Frozen engine is7c2b4df4; platform bbec3f6d; guard dbd626a8 (full hashes in manifest).
- v3 engine25502 checks, atomic40, contract983 are already independently reproduced. Do not
  repeat unchanged suites without a new concern. Gate helper23 checks passed ASan/UBSan+MSan.
  Independently review the NEW actual loader placement, predecessor read and platform wiring.
- Platform ARM64 uses real XNU headers (host-checks/platform-arm64-result.json), rc0.
  Private loader compiled three native0,47264 bytes/ca0112e2. Nine legacy warnings retained.
  Check loader-candidate/compile-commands.json and preprocessed.c, not just strings.
  If reproducing, use a fresh private output directory; parse/remap ALL source and output argv
  before execution. Never replay commands into the716 candidate or713/714/715 directories.
- Report concrete blockers. Do not classify an unreachable arbitrary phase reset as a normal
  race. The engine's finish(true) failure always flows to finish(false), and STOP-CAS failure
  is exercised. Preserve the old review and withdrawn objection with corrected attribution.

## Full isolated kernel build

If no concrete blocker, from P execute XNU_SRC=absoluteX ./xnu-build.sh -k -j8.
Use X/BUILD only. Record native make rc and complete stdout/stderr with argv and timestamps.
Do not confuse pipeline/tee rc with make's rc. Explicitly set
KERNEL=X/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837
for tools/make-kernel.sh --judge of the combined log. Only the established symbol-set allowance
is permitted. Separate make rc from judge rc, and report new compiler/link errors immediately.

After build verify all5210 source hashes and no additions/deletions outsideBUILD. Record kernel
size/hash and inclusion of read/init/quiesce/guard-dependent messages and symbols. This proves
inclusion, not storage IO. Recheck the frozen candidate/loader manifests and prior artifact guards.

## Archive/return

Archive a NEW D/docs/headset-wip/raw/ufs-read-integration-716/: immutable candidate+private
loader changed-source/header snapshots, manifests, integration diff, source/ABI/order review,
model/native rc summaries and build outcome. Preserve older engine versions and their hashes.
Do not upload raw GPT, do not alter ignore rules or stage GPUEB WIP, and do not push P.
Return the kernel identity, separate rc values, guard/candidate identities, objections and commit.

No packaging/flash/device query in this command. No production edits, old BUILD/artifact
overwrite, stash/reset/checkout/clean/gc/prune, GPUEB, PMIC, watchdog or OEM expdb work.
