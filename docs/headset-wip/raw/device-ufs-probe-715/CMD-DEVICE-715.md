# CMD-DEVICE-715: one attended UFS snapshot trial

2026-11-17, Codex. The user's latest instruction is:
“现在我可以兜底 可以刷机操作了”. This releases the previous host-only hold for this one
reviewed715 experiment and stock recovery. It does not release unrelated experiments.
DSH exclusively owns device commands. No audio recording or further user confirmation is needed.
Hydrogen remains closed. No new source build, package generation or production edit is needed.

P=/home/lizi/IOS/hakios-mtk
R=P/logs/ufs-probe-20261117
Use a fresh R/device715/ evidence directory. Preserve any existing attempt; never overwrite it.
Use the established phone's exact local serial and explicit -s for every adb/fastboot command.
Redact the serial as <SERIAL> in public records. Never select the first enumerated device.

## Fixed artifact identity

- R/trial715/boot.img:22994944 bytes
  SHA256 65bd797dce99a4f29d898c7055a25fcce207e1b3642109daae0ad217f653c994
  MD5 dced496c752ae0bec3de806b94a81615
- R/trial715/loader.bin:46504 bytes
  SHA256 c3d9c30a0c14c3e6c644fbf499fc189819b818c03080fe2beee18590cf7eafea
- R/trial715/payload.bin:19515784 bytes
  SHA256 c7d73935a1338a3e75cba6d01da536e7352c0a73dd2a51c18ae94239b1fe97c3
- Nested kernel:15321328 bytes
  SHA256 ecdc9194fbcc635a63d8da6be7c5f4ef6a1c6bc7f3c1b715c9373cca9fe7da38
- Recovery P/backup/boot_a_stock_real.img:67108864 bytes
  SHA256 c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc
  MD5 a63b2e35a7c2522cb24a6a10332b9bc8

These files were rehashed by Codex immediately before issuing this command. DSH must recheck
the experiment and stock files before flash. A mismatch or unknown phone is a stop condition.
715 uses the714 UFS kernel/payload with runtime mali=0 and all five loader GPU macros0.
There is no CPU probe or headset-output experiment in this trial.
The legacy -dirty kernel.ver is metadata; use artifact hashes and715 arguments for identity.

## A. Preflight and preservation

1. Establish sole device ownership. Verify serial, adb/fastboot state and slot_a. Record Android
   boot_completed, boot_id, SELinux, full boot_a size and MD5. Require stock MD5 above before
   an experimental flash. Last verified boot_id was c8acf0e0-ec30-4b5a-906b-fe29e1a74a9d;
   a changed ID alone is not failure if phone identity and complete stock baseline are verified.
2. Preserve the current final8MiB of xnusys as R/device715/pre-logbook.bin, exactly8388608 bytes.
   Reconfirm partition size2685370368 and offset2676981760=0x9f8f8000, skip653560 at4096 bytes.
   Do not clear or manually write xnusys. If it already contains this715 experiment, stop;
   do not automatically repeat the hardware trial.
3. Set bounded transport calls and return monitoring before flash. Save actual native command
   rc and timestamps, including timeout state; do not report a pipeline/echo rc as tool success.
   User is available for manual fastboot fallback, as may be needed after the713 incident.

## B. Exactly one experimental boot

4. Request adb reboot bootloader with a bounded command timeout and confirm the exact phone
   in fastboot, slot_a. If the command times out but fastboot appears, preserve both facts.
   If neither transport provides a usable path, notify Codex promptly for manual fallback.
5. fastboot -s <SERIAL> flash boot_a the exact715 boot.img. Require native0 and Sending/Writing
   OKAY. On failure, use the verified stock recovery path; do not continue as if flash succeeded.
6. fastboot -s <SERIAL> reboot once. Record actual host command timestamp, native rc and result.
7. Monitor adb/fastboot availability for at most300 seconds after that command, with bounded
   individual calls. Return setting150, watcher0 and watchdog settings remain unchanged.
   Do not run a second experiment, inject hardware writes, change slots or automate power holds.

## C. Capture immediately, then recover before optional analysis

8. As soon as adb is available, BEFORE waiting for boot_completed, read exactly4198400 bytes
   (1025*4096) at the same xnusys offset to R/device715/trial715-logbook.bin. Preserve original.
   HLOG magic/version must be valid; length<=4194304; exact body sum as a full64-bit integer
   must equal the header sum. Save the exact body separately as trial715-logbook-body.bin.
9. Do not postpone stock recovery for report formatting, interpretation or optional checks.
   Immediately request a bounded adb reboot bootloader if adb is available; use fastboot if
   already available. Recheck stock file identity, flash stock boot_a and reboot.
10. If adb reboot bootloader times out or fastboot does not appear within30 seconds, report
    the actual transport states and request manual fastboot from the available user through
    Codex. Do not repeat reboot indefinitely, issue guessed resets or assume recovery completed.
    Once the exact phone is manually in fastboot, continue the already-authorized stock recovery.
11. Verify slot_a, boot_completed1, fresh boot_id, SELinux Enforcing, responding SurfaceFlinger,
    and full boot_a67108864-byte MD5 a63b2e35a7c2522cb24a6a10332b9bc8. State whether manual
    fallback was required. Native0 alone is not a complete stock recovery proof.
12. If there is no usable transport after the bounded experiment wait, notify Codex immediately
    for manual fastboot and recover stock. Preserve any captured evidence; a failed capture
    never authorizes a new experimental boot. Recovery takes priority over diagnostics.

## D. Required observations and labels

Record IMAGE_IDENTITY, UFS_SNAPSHOT_CAPTURE, NORMAL_RETURN and STOCK_RECOVERY independently.
The observation result is not forced to FLAGS_OBSERVED and is not a storage-I/O PASS.

UFS_SNAPSHOT_CAPTURE requires:
- Exact full_arguments from R/arguments715.json present (229 bytes withoutNUL), including
  mali_trial=715, mali=0, mtk_ufs_probe=1, headset=0, cpus=4, loader_return_after=150.
- The read-only banner and exactly one base0x112b0000 summary containing symbolic/numeric
  result, valid/attempted masks for both snapshots and sdb_slots.
- Nine unique raw offsets 0x00,0x08,0x20,0x30,0x34,0x50,0x54,0x58,0x60, each with both samples.
  Offset0 is printed as 0000 by %#04x; do not reject it merely for lacking a0x prefix.
- Full capture needs valid=attempted=0x1ff/0x1ff and numeric values for all18 samples.
  FAILED/NOT_COLLECTED or map failure is incomplete capture; preserve it accurately.
- Report FLAGS_OBSERVED(0), READ_FAILED(1), CHANGING_WORDS(2), HCE_CLEAR(3), DP_CLEAR(4),
  UTRLRDY_CLEAR(5) or UNTRUSTED_RESPONSE(6). Nonzero diagnostic outcomes with complete raw
  samples are valid observations, not permission for remedial register writes.
- IS may differ autonomously; only the other eight words participate in stability.
  Check result precedence against the frozen C source, including untrusted identity responses.
- Record flags-only output and raw queue-base/doorbell/run-stop words without following the
  pointers or issuing commands. sdb_slots is decoded only for FLAGS_OBSERVED, otherwise0.

NORMAL_RETURN is HLOG reason0, independently of the probe result and later Android health.
Scan separately for panic, data abort and shutdown/stop errors. Preserve context for any hit.
No successful SCSI/read, filesystem, DMA ownership, link speed/gear/lane, physical GPU state
or fixed Android return is implied. If auto recovery succeeds, it is one observed recovery,
not evidence the earlier incident's cause is solved.

## E. Archive and return

Archive in new D/docs/headset-wip/raw/device-ufs-probe-715/ after recovery:
README, current user release/command, preflight, approved artifact hashes, actual command rc
and timestamps, exact HLOG and body, parsed observations, stock proof and limits.
Use narrow add -f only for the two exact trial binary paths if ignored. Keep pre-log locally
as in prior trials, record its size/hash/header and do not modify global ignore rules.
Redact serial, preserve old evidence and GPUEB WIP unstaged; no production repository push.
No stash/reset/checkout/clean/gc/prune, old BUILD/artifact overwrite, arbitrary MMIO/syscon
reads, UIC/SCSI commands, queue takeover, PMIC getter, GPUEB, watchdog or OEM expdb access.

Return DONE with the four labels, nine raw pairs, result/masks/slot count, HLOG length/reason/
fullsum/hashes, recovery mode and latest proof/boot_id, commit and any objection.
Do not automatically add another trial or expand from observation to storage I/O.
