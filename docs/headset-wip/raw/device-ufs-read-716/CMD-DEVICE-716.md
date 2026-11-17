# DEVICE716: one attended guarded READ10 trial and stock recovery

Execute ONLY when Codex sends the explicit DEVICE716 release after independent package review.
User requested continued progress after715 and has authorized flashing/manual fallback.
DSH exclusively owns all device commands. This is one boot, one kernel command, no extra trial.

P=/home/lizi/IOS/hakios-mtk; R=P/logs/ufs-read-path-20261117.
Fresh output R/device716/. Never overwrite an existing attempt or sealed715 evidence.
Use the established phone's exact serial with explicit -s; redact public serial as <SERIAL>.

## Fixed identity

- R/trial716/boot.img22999040 bytes
  SHA256 4ca5834f555300c6616245e265923ac1897ef9d8d6d12c57638fa870afc9c057
  MD5 3abcb40dfe056bb152eee1fbbef9af57
- Loader47264 bytes, ca0112e2d6659c37a469fe60881f9a14d8de5e87008ee50cce8007572b800a13
- Payload19533288 bytes, c13981d45c8d46d9336a119420a8b8490c1d8fb3801a66b65c4bf0f0ca9bba0a
- Kernel15338824 bytes, c7f2f3222f314b82885b0567975e3af759c9b172ececd6fbefe437b65ddff6aa
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
   11aead4e-9595-4574-8e6a-6b229e287480. A changed ID alone is not failure if stock/phone match.
2. Preserve the final8MiB of xnusys, exactly8388608 bytes, to pre-logbook.bin. Reconfirm size
   2685370368, offset2676981760=0x9f8f8000,4096-byte skip653560. Never clear/write it manually.
   If any existing716 record/attempt is found, stop instead of automatically repeating.
3. Record bounded native command rc/timeouts and actual reboot timestamps. Do not substitute
   tee/echo rc or a late polling origin for native rc or the actual reboot time.

## B. One experiment and capture

4. Bounded adb reboot bootloader; confirm exact fastboot serial/slot_a. If command times out
   but correct fastboot appears, preserve both facts and continue based on observed transport.
   If neither transport works, report for manual fallback; no guessed resets or repeated reboot.
5. fastboot -s <SERIAL> flash boot_a the exact716 image, native0 + Sending/Writing OKAY required.
   On failure recover stock, do not proceed as if successful. Then fastboot reboot once.
6. Monitor for adb/fastboot with bounded individual calls for at most300 seconds from the
   actual reboot command. Keep loader_return_after150, watcher0, GPU experiment0 and watchdog
   settings unchanged. Give Codex prompt progress/failure notices.
7. When adb is available, BEFORE waiting for boot_completed, capture exactly4198400 bytes
   at the same xnusys offset to trial716-logbook.bin and the exact header-length body separately.
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
READ_EXPERIMENT_CAPTURE: exact228-byte full arguments from arguments716.json, read banner,
fresh guard magic0x554c5345/version1/trial716/state0, one result summary and final lease state.
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

## E. Archive

After recovery create D/docs/headset-wip/raw/device-ufs-read-716/ with preflight, actual command
rc/timestamps, original raw+body, parsed kernel observations, identities and recovery proof.
Use narrow add -f only for the two trial binary paths if needed; keep pre-log and raw GPT local.
Retain historical failures, model/host limits and the intentional HLOG-loss failure path.
Redact serial. No production push, old BUILD/image overwrite, GPUEB WIP staging, ignore edits,
stash/reset/checkout/clean/gc/prune, arbitrary MMIO/getters, clock/power/UIC/reset changes,
watchdog/OEM expdb or extra experiment. Return six labels and any objection, with docs commit.
