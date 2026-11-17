# DEVICE718: one guarded bounded response READ10 trial and stock recovery

Execute ONLY when Codex sends the explicit DEVICE718 release after independent package review.
User authorized continued work and flashing/manual fallback;716 is sealed NOT_PROVEN.
DSH exclusively owns all device commands. This is one boot, one kernel command, no extra trial.

P=/home/lizi/IOS/hakios-mtk; R=P/logs/ufs-response-20261117.
Fresh output R/device718/. Never overwrite an existing attempt or sealed715 evidence.
Use the established phone's exact serial with explicit -s; redact public serial as <SERIAL>.

## Fixed identity

- R/trial718/boot.img22999040 bytes
  SHA256 3a3ceb1d77c9a61f289695879c077baf7ec076a8554c936aec8110afd1e61970
  MD5 42beb68e83eabea8a281965888380417
- Loader47264 bytes, dc1d273481eeab99086726e90762c4dca4a30e7d631fddab31990349ce6e4b25
- Payload19533320 bytes, a43093c1479e34debc82ebca787a3c59735aa79279a3f5ec78b686183b673aba
- Kernel15338864 bytes, 60bd6b195c2ab65ab0c5c1cb519fe3cf5af9ad64c351485ccbf59aa899fd7cfe
- Stock P/backup/boot_a_stock_real.img67108864 bytes
  SHA256 c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc
  MD5 a63b2e35a7c2522cb24a6a10332b9bc8

Rehash experiment and stock immediately before flash. Identity mismatch is stop-and-report.
Do not rebuild/repackage, alter the whitelist, expected CRC or geometry to force a result.

## Special failure path: explicit manual fallback, no unsafe HLOG

The loader performs its existing boot-note write, then its own verified GPT read and lease
initialization. The kernel issues its separate fixed READ only after preflight/pre-fill checks.
If the predecessor read fails, the loader halts before XNU. If DMA/restore becomes uncertain,
the kernel poisons its lease; on return the loader halts BEFORE HLOG/storage/Android handover.
Both failure paths may leave only the reason0xfeedface boot note in flash, not a runtime HLOG.
That is intentional. Do not bypass the guard, force storage access or report the old log as new.

When there is no usable transport after the bounded wait, notify Codex immediately for the
available user's manual fastboot. Once the exact phone is in fastboot, recover stock; after
stock Android is back, capture the existing HLOG for diagnosis even if it is only the boot note.
Never start a second experimental boot to obtain a better log.

## A. Preflight

1. Establish sole ownership. Record adb/fastboot identity, slot_a, boot_completed1, SELinux
   Enforcing, boot_id and full boot_a67108864-byte stock MD5. Last verified stock ID is
   77b01ffe-6fc8-4c84-b0e8-0782fc8d6243. A changed ID alone is not failure if stock/phone match.
2. Preserve the final8MiB of xnusys, exactly8388608 bytes, to pre-logbook.bin. Reconfirm size
   2685370368, offset2676981760=0x9f8f8000,4096-byte skip653560. Never clear/write it manually.
   If any existing718 record/attempt is found, stop instead of automatically repeating.
3. Record bounded native command rc/timeouts and actual reboot timestamps. Do not substitute
   tee/echo rc or a late polling origin for native rc or the actual reboot time.

## B. One experiment and capture

4. Bounded adb reboot bootloader; confirm exact fastboot serial/slot_a. If command times out
   but correct fastboot appears, preserve both facts and continue based on observed transport.
   If neither transport works, report for manual fallback; no guessed resets or repeated reboot.
5. fastboot -s <SERIAL> flash boot_a the exact718 image, native0 + Sending/Writing OKAY required.
   On failure recover stock, do not proceed as if successful. Then fastboot reboot once.
6. Monitor for adb/fastboot with bounded individual calls for at most300 seconds from the
   actual reboot command. Keep loader_return_after150, watcher0, GPU experiment0 and watchdog
   settings unchanged. Give Codex prompt progress/failure notices.
7. When adb is available, BEFORE waiting for boot_completed, capture exactly4198400 bytes
   at the same xnusys offset to trial718-logbook.bin and the exact header-length body separately.
   Validate HLOGv1, length<=4194304, complete integer sum(body)==header64-bit sum. Preserve raw.

## C. Recover stock BEFORE optional analysis

8. Immediately request bounded adb reboot bootloader (or use available fastboot). If it times
   out and fastboot is absent after30 seconds, notify Codex for manual fastboot. No endless retry.
9. Verify stock file again, flash stock boot_a only, reboot, and verify slot_a, boot_completed1,
   fresh boot_id, SELinux Enforcing, full64MiB stock MD5 and responding SurfaceFlinger.
   If manual fallback was needed before capture, capture existing HLOG now, after recovery.
10. State clearly whether automatic or manual recovery was used. Do not postpone recovery for
    report formatting, interpretation, CRC comparisons or archive work.

## D. Six independent labels and strict interpretation

IMAGE_IDENTITY: approved artifacts and actual native flash/reboot identity.
READ_EXPERIMENT_CAPTURE: exact228-byte full arguments from arguments718.json, read banner,
fresh guard magic0x554c5345/version1/trial718/state0, one result summary and final lease state.
Inherited UTRD/UPIU raw metadata must be valid-mask0xff before interpreting their eight words.

READ_BLOCK_COMPARE passes ONLY for READ_OK(5), claimed1/issued1/restored1/data_valid1, sampled
OCS0, block crc32=expected=0x28a2836c and bytes4096. This is CRC32 agreement with Android's
independently validated LUN2/LBA1 reference, not a full SHA256 or byte-for-byte equality proof.
No raw disk GUID or GPT block dump is needed in public evidence.

LEASE_RETURN passes for a borrowed request only with restored1 and final shared state2 /
loader_io_allowed1. If no request was borrowed, report NOT_BORROWED rather than a restore PASS.
REFUSED/NO_LEASE/PREPARE_FAILED/COMPLETED_ERROR/REFERENCE_MISMATCH remain distinct diagnostic
outcomes, not a storage-read PASS. Do not widen predicates or add commands based on them.

NORMAL_RETURN: new runtime HLOG reason0 with full checksum, separately from read outcome and
later Android health. A boot-note-only reason0xfeedface is NOT normal runtime return/capture.
If a new runtime HLOG claims a poisoned/claimed final lease yet normal loader IO occurred,
report the contradiction and stop future work; do not silently call it a successful guard.

STOCK_RECOVERY: verified full stock evidence above, with manual/automatic mode distinguished.

Scan panic, data-abort and stop/guard failure context, preserving exact lines. No block device,
filesystem mount, throughput, link speed/gear/lane, GPU physical state or713 fix is established.

## New718 bounded response and diagnostics

This NEW revision intentionally changes response acceptance. Read DESIGN-RESPONSE718.md:
segment length is0 or20; for20 the sense-data length is<=18; EHS length must be0. Command
bytes, disk geometry, CRC, controller stores, lease/restore/timeouts remain unchanged.
716 and717 remain sealed NOT_PROVEN. Do not re-label either with the new predicate.

The raw response is52 bytes/13 little-endian words; attempted/valid masks must both be1fff
before interpreting all words. Record the exact raw words, failure bits and any diagnostic CRC.
Header bits0..6 retain field names; bit5 now unsupported segment, bit7 overlong sense, bit8 EHS.
A zero failure mask means response acceptance only with full valid1fff AND sampled OCS0.
Decode bytes10..11(segment),32..33(sense length) as big-endian; EHS byte8. Sense contents are
raw observations; SCSI status0 remains required. No partial/uncollected word may be invented.

READ_BLOCK_COMPARE additionally requires this full accepted response and the six-label
READ_OK conditions above. Any failure bit, partial response, CRC mismatch or failed restore
keeps the appropriate diagnostic result and NOT_PROVEN. Matching diagnostic CRC/accepted0
never overrides rejection. Missing diagnostic line means no complete observation, not CRC0.
DIAGNOSTIC_RESPONSE_CAPTURE is an additional label for full response/OCS0, separate from IO PASS.
Run R/review-device-718.py against preserved raw/body to a NEW output AFTER stock recovery;
DIAGNOSTIC_ONLY intentionally exits1. It has58 host synthetic cases, not prior hardware evidence.

## E. Archive

After recovery create D/docs/headset-wip/raw/device-ufs-read-718/ with preflight, actual command
rc/timestamps, original raw+body, parsed kernel observations, identities and recovery proof.
Use narrow add -f only for the two trial binary paths if needed; keep pre-log and raw GPT local.
Retain historical failures, model/host limits and the intentional HLOG-loss failure path.
Redact serial. No production push, old BUILD/image overwrite, GPUEB WIP staging, ignore edits,
stash/reset/checkout/clean/gc/prune, arbitrary MMIO/getters, clock/power/UIC/reset changes,
watchdog/OEM expdb or extra experiment. Return six labels and any objection, with docs commit.
