# ONE DEVICE719: bounded readonly character snapshot service and stock recovery

Execute ONLY after Codex explicitly sends ONE DEVICE719 release. Existing user authority
covers continued work, flashing and manual fallback. DSH exclusively owns all phone commands.
One experimental boot only, global maximum THREE fixed kernel READ10 commands, no extra trial.

P=/home/lizi/IOS/hakios-mtk
R=P/logs/ufs-service-20261117
D=/home/lizi/IOS/repo/hakios-mt6895
Fresh outputs R/device719/. Never overwrite an earlier attempt. Exact serial -s on all device
commands; public evidence must use <SERIAL>. Do not expose private GUID/GPT/reference bytes.

## Approved identities and prerequisites

- trial719/boot.img23003136 B SHA256 fd6e19ac8386804ac697564a98a1bce62b697b8fbaac2821b6033bf32f4d5862
  MD5 d54237475b4e1633f9c88720a3fb2b2e
- loader47952 B SHA256 9d2c5fb9fe534a153846f4e794da68dea8399ae03ae20371b51ad3f8b5da84cb
- payload19534232 B SHA256 c6e9b51e47a89ef83227ab9ce977040a34e24c4e1e32753e4f07c6a1d5b2c172
- kernel15339768 B SHA256 5a85ac7fed4392b1a87a9cb4f91f592fd9697cafbb572fe4d1041794c991494f
- stock P/backup/boot_a_stock_real.img67108864 B
  SHA256 c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc
  MD5 a63b2e35a7c2522cb24a6a10332b9bc8

Host review/build065b35c, independent kernel PASS5214/57 with make2/judge0, loader private
3 native rc0/byte-exact, package review00630b6, parser review030ca56/108 cases all passed.
Rehash experiment and stock immediately before flash; mismatch is stop-and-report. Do not
build/repackage/edit frozen inputs, old trials, command whitelist, geometry/reference/timeout.
Arguments are171base/231runtime/232withNUL from arguments719.json. mtk_ufs_service1, mali0,
headset0, cpus4, return150; no old read/probe/cpu or GPUEB/tone modes.

## Intentional failure path

Existing boot-note write precedes loader's verified predecessor READ and fresh719 lease.
Failure there may halt before XNU. An uncertain ACTIVE transaction or restore poisons the
lease; return guard halts BEFORE HLOG/storage/Android handover. A boot-note-only0xfeedface
record may be all that remains. Do not bypass guard, force storage, or mistake stale data
for a new runtime log. Clean IDLE/DRAINED STOP can release, active/uncertain STOP cannot.

If no usable transport after the bounded wait, notify Codex promptly for physical fastboot.
Once exact phone is in fastboot, restore stock first; after stock is back capture whatever
HLOG exists. Never issue a second experimental boot to improve evidence.

## A. Preflight

1. Establish sole phone-command ownership. Record phone identity, slot_a, boot_completed1,
   SELinuxEnforcing, boot_id, full67108864-byte boot_a stock MD5. Historical boot_id is
   f745b1c6-ee43-4abc-8ed2-06ee9629bf27; changed ID alone is not failure if phone/stock match.
2. Preserve exactly8388608 bytes from final8MiB of xnusys to pre-logbook.bin. Reconfirm size
   2685370368 and offset2676981760=0x9f8f8000 (4096-byte skip653560). Never clear/write it manually.
   Existing719 record/attempt means stop-and-report; do not silently repeat. Pre-log stays local.
3. Keep actual native rc, timeout status and reboot timestamps. Do not use tee/echo rc or a late
   polling origin as native rc or actual reboot time. No audio recording or user interaction needed.

## B. One boot and capture

4. Bounded adb reboot bootloader; confirm exact fastboot serial/slot_a. If timeout but correct
   fastboot appears, preserve both facts and continue on observed transport. If unavailable,
   notify Codex; no guessed reset, slot switch, or repeated reboot loop.
5. Flash ONLY boot_a with exact719 image, require native0+Sending/WritingOKAY. Flash failure
   means restore stock, not continued experiment. Then fastboot reboot once; record true time.
6. Monitor adb/fastboot with bounded individual calls for at most300seconds from actual reboot.
   No extra VFS/userland calls: the built-in delayed worker already performs write-open refusal,
   THREE4096-byte kernel VFS reads separated by2s, then EOF with no command. Budget is global3.
   Idle service remains CLAIMED until quiesce at return150. No init resetting/retry.
7. As soon as adb works, BEFORE waiting for boot_completed, capture exactly4198400 bytes at
   the same xnusys offset to trial719-logbook.bin and exact header-length body separately.
   Validate HLOGv1, length<=4194304, and full integer sum(body)==64-bit header sum. Preserve raw.

## C. Recover stock BEFORE optional interpretation

8. Immediately request bounded adb reboot bootloader or use available fastboot. If timeout and
   fastboot absent after30seconds, notify Codex for physical fastboot. Do not repeatedly retry.
9. Reverify stock, flash stock boot_a only, reboot; verify slot_a, boot_completed1, new boot_id,
   SELinuxEnforcing, full64MiB stock MD5 and responding SurfaceFlinger. No slot/SELinux change.
10. Report automatic vs manual recovery. Do not delay recovery for parser, formatting or archive.
    If manual fallback preceded capture, obtain existing HLOG after stock recovery instead.

## D. Separate evidence labels

Run R/review-device-719.py against preserved raw/body to a NEW output after recovery. Parser
nonzero on DIAGNOSTIC_ONLY is intentional; missing/partial/uncollected fields are not zeros.

- IMAGE_IDENTITY: approved hashes plus actual native flash/reboot records, separately verified.
- READ_SERVICE_CAPTURE: exact231-byte runtime arguments, service banner, fresh719 guard and
  collected service/gate observations. Boot note alone is NOT runtime service capture.
- VFS_READ_SERVICE: real registered root0400 character node, matching vnode dispatch, lookup0,
  then exactly3 successful kernel vn_rdwr reads, each error0/residual0/CRC28a2836c. These are
  kernel VFS calls, NOT userland read(2) evidence or persistent whole-disk block support.
- READ_BLOCK_COMPARE: for all3, service result0 and engineREAD_OK5, claimed/issued/restored/
  delivered1, full13word valid=attempted1fff, OCS0/failures0, diagnostic and block CRC28a2836c.
  This is CRC32 agreement only, not SHA256 or mathematical byte equality. Sense18 raw meaning
  remains unverified. Parser checks segment0/20, sense<=18, EHS0, residual0 and other fields.
- WRITE_OPEN_REJECTED: EROFS30, request counters0before/0after. No physical WRITE command test.
- EOF_WITHOUT_COMMAND: EOF residual4 and request counters3before/3after.
- LEASE_HELD_BETWEEN: all3 requests return clean cachedIDLE2/sharedCLAIMED1, final selftest
  confirms requests/issued/delivered3 and stillIDLE2/shared1. Per-request memory restore is NOT
  global lease return; do not confuse cachedDRAINED with any shared wire state.
- IDLE_RETURN_RELEASE: prior held-at-idle evidence plus unique actual loader audit at HLOG EOF,
  magic554c5345/version1/trial719/stateRESTORED2/allowed1. Audit is loader-generated second span.
  HLOG body therefore is NOT entirely kernel-generated. One save invocation may make multiple
  physical WRITE10 commands. Kernel quiesce uses atomics/sharedDRAM only, no logger/storage.
- LEASE_RETURN can pass after a clean early failure without proving3reads; keep labels separate.
- NORMAL_RETURN: new runtime HLOG reason0 and full checksum, separately from read/service result.
- STOCK_RECOVERY: full restored evidence and automatic/manual mode separately documented.

Any claim of normal persisted runtime HLOG after uncertain/claimed/poisoned ownership is a
contradiction: preserve it and stop future experiments. Never normalize it to a guard PASS.
Preserve panic/data-abort/STOP/guard failure context.718 remains sealed PASS;716/717 remain
NOT_PROVEN. No claim of whole-disk/filesystem/throughput/gear/lane/global ownership or713 repair.

## E. Archive after recovery

Create D/docs/headset-wip/raw/device-ufs-service-719/ with preflight/rc/timestamps, original
raw+body, observations/parser result, image identity and recovery. Narrow add-f only for these
two approved trial runtime binaries if needed; no ignore edits. Pre-log/rawGPT/GUID/reference
bytes and images remain local. Redact serial. Preserve GPUEB WIP and production/old BUILD.
No production push, stash/reset/checkout/clean/gc/prune, arbitrary MMIO/getters, clock/power/
reset/UIC, watchdog/OEMexpdb or additional experiment. Return labels and any objection.
