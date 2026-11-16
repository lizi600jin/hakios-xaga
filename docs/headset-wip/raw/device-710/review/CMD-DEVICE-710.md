# CMD-DEVICE-710 — one approved profile4 trial and mandatory stock recovery

Codex, 2026-11-16. The user is beside the phone and explicitly authorized continuing,
including flashing. Codex will announce the approved image before delivering this CMD.
No additional confirmation is required. DSH alone owns all device/protocol operations
from precheck through log collection and restoration. Do not delegate the device or start
other workers. Codex will inspect host evidence and will not issue adb/fastboot commands.

## Approved artifacts and scope

Repository root: `/home/lizi/IOS/hakios-mtk` (P). No production push.

The ONLY approved trial image is:
`P/logs/profile4-device-20261116/trial710/revision2/boot.img`

- Size 22994944 bytes.
- SHA256 `ca161c1bdb002540a2454e8222ef7306255e175108ead9fffe48d995aebe56b1`.
- MD5 `b016ef1708b46830e9f74787bb169815`.
- Nested kernel SHA256 `cac498da96406cc4f660e2cc717dbcf93b259ae1d864de6f30a9e920ed03401d`.
- Payload SHA256 `e29f55eba59aa2569d9d440d0f8fe3f71b0861a819e7e46242a5ad11b741eb7d`.
- Loader SHA256 `daa05b1736fafcb24499a8b063b20d9d94ee2403f95e46014ae02b2676cad18d`.

Stock recovery image: `P/backup/boot_a_stock_real.img`, 67108864 bytes,
SHA256 `c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc`,
MD5 `a63b2e35a7c2522cb24a6a10332b9bc8`.

The first trial710 payload/boot image WITHOUT the 24-byte XNUP terminator is REJECTED.
Never flash it, overwrite it, or use a root/default image as a fallback.

Host review evidence: `independent-pack-r2-review.json`, `independent-loader-macros.json`,
and `independent-loader-build/commands.json` alongside this CMD. Codex independently
rebuilt the frozen loader with identical output and native rc0 for both compiles/link.
Recheck exact approved image/stock hashes immediately before any trial/recovery flash.

Write new execution evidence only under a fresh
`P/logs/profile4-device-20261116/device710/` directory (explicit permission for device logs,
scripts and readback evidence there). Preserve all previous raw evidence and failed outputs.
Archive relevant text/raw evidence in a new docs directory and push only the document repo.

## Experiment and fixed criteria

Read `TRIAL710-CRITERIA.md` beside this CMD before touching the device.
Fixed headset=4, 48k internal codec SGEN, hifi0, gain31, real trim L=3/0 R=17/3,
trial label710 and loader_return_after=150. Do not change any register value, boot argument,
gate, retry count, capture criterion or image. No automatic second trial.
VAUD18 must already be enabled in THIS XNU boot; the profile only checks it.
An initial four-pole classification or any precondition refusal is not a successful run.

## Phase A: fresh precheck and preservation, no writes

1. Confirm no other operator is using the phone. Select only the serial from the handoff,
   use `HAKIOS_SERIAL` in scripts and keep the literal serial out of committed files.
   Check Android device connection, slot `_a`, boot_completed1 and current boot_id.
   Expected pretrial boot_id is `0ebf2d81-4822-47df-be3c-51916b1a7663`.
   Recheck boot_a's full-partition MD5 equals a63b2e35... and size67108864.
   Any unexpected identity/slot/boot change or hash mismatch: STOP before reboot/flash.
2. Read xnusys partition size, compute size minus8388608, require it equals
   2676981760 (0x9f8f8000; block4096 skip653560). Stop on a discrepancy.
3. Save exactly the existing final8MiB xnusys region to a NEW host pre-logbook.bin,
   separated stdout/stderr/native rc, verify exact byte count and SHA256, and inspect its
   header/body when valid. Preserve even an invalid previous record. If it already has
   trial710 execution evidence, STOP rather than launch a duplicate trial.
   Use read-only dd, e.g. bs4096 skip653560 count2048 to stdout through the audited
   binary-safe shell channel (`adb shell -T -n su -c ...`, no `-x`). No manual xnusys write,
   no clearing, no whole-partition dump. No debugfs mount, PMIC read/write or audio getter.
4. The same three-pole headphones should remain connected and OFF THE EARS. The user has
   already been told this. Do not require a new reply to repeat the existing authorization.
   If the user reports a changed/unsafe physical setup, pause the dependent trial steps.

## Phase B: exactly one trial boot_a flash

1. With the approved hashes still matching and baseline logs saved, reboot to bootloader.
   Bound adb reboot at10s and fastboot enumeration at30s.
2. Issue exactly the planned `fastboot -s $HAKIOS_SERIAL flash boot_a <approved revision2>`
   with native timeout60s, preserving complete stdout/stderr/rc. This is the actual trial
   flash, never a connection probe. Do not flash another partition, change slots or disable AVB.
3. Require flash success before `fastboot ... reboot` (timeout10s). If a flash was issued
   but failed/was ambiguous, do not retry it; preserve results and proceed only to the
   planned stock recovery when the device connection permits, or report physical recovery.
4. Record host times and poll USB/adb in bounded phases (5s intervals, each command <=10s,
   maximum290s after trial reboot for the chain return). Do not blindly resend reboots or
   restart the trial. The driver is active for the existing150s return window. Expect
   Android handback to be potentially unhealthy because of the pre-existing GPU issue.

## Phase C: collect before waiting for Android readiness

As soon as adb/root can execute commands, capture the current HLOG window immediately;
do NOT wait for boot_completed or SurfaceFlinger first. Read exactly1025 blocks of4096
bytes at skip653560 (4198400 bytes), stdout directly to a new host file, separated stderr
and native rc; timeout20s. Verify exact count before parsing. If the brief return window
allows, also preserve the loader's dedicated log from the pre-confirmed final8MiB region,
but do not delay the first HLOG/body capture for it.

Decode with the existing xaga-logbook.py and independently check the raw header magic,
version, bounded length and additive body checksum. Save the original header/window, body,
checksums and native rc. Do not replace bytes with decoded/filtered text or splice old tails.
Fresh trial710 identity, full real trim, up/off/restored readbacks and shutdown are judged
only using the fixed criteria. Preserve the whole headset block and the whole raw body.

If adb never returns, or collection fails, preserve all available host evidence and identify
the current USB mode. A reachable fastboot can be used for the already planned STOCK flash;
it is not necessary to rerun the trial to obtain a log. If neither mode permits recovery,
STOP and report precisely what physical recovery is needed. Never long-hold power on a
black screen or issue an OEM dump command. No unbounded wait or protocol retry loop.

## Phase D: stock recovery for every issued trial flash, regardless of verdict

1. Once available logs are safely on the host, use the functioning adb connection to
   reboot bootloader (10s). If already in fastboot, use that existing connection.
2. Recheck the exact STOCK image hash and issue the planned
   `fastboot -s $HAKIOS_SERIAL flash boot_a <stock image>` (60s), then one reboot (10s)
   after confirmed success. Do not use flashing as a protocol probe or repeat a failed flash.
3. Bound Android-ready polling at180s, 5s intervals and per-command timeout10s.
   Check slot_a, boot_completed1, boot_id, SELinux state and SurfaceFlinger presence.
   Recheck full boot_a MD5 a63b2e35a7c2522cb24a6a10332b9bc8 (and SHA256 if feasible).
4. A restore failure/timeout must be reported immediately, with evidence preserved and
   the current connection mode. Wait for physical recovery rather than hiding it as a PASS.
   If the trial flash was never issued, avoid an unnecessary stock write and return Android
   with a bounded reboot when possible.

## Report, documents and boundaries

Return actual image hash/flash rc/time, raw log paths/hashes, header fields/checksum,
full actual boot args and decoded trim, counts/fields for all three38-pair stages,
HP-off shape, ACCDET restore count, any error/retry/refusal, normal-return status and
stock-recovery hash/readiness. Separate identity, UP_READBACK, DOWN_RESTORE,
NORMAL_RETURN and STOCK_RECOVERY. Do not claim sound, electrical silence, DC-offset
quality, measured clock oscillation or a complete audio driver from these checks.

In the docs, prominently mark the original pack-710 README/candidate as rejected and
superseded by revision2; do not leave its historical PASS language as the current candidate.
Preserve original failure evidence. Do not commit serials, modify ignore rules globally,
stage GPUEB WIP, or silently omit required raw files. Original HLOG is required evidence.

No spmi_access, vendor codec dump, tinymix getter, Audio HP ImpeDance getter, SELinux or
permission changes, GPUEB0x13c40000 access, PMIC014b writes or0240 RMW, PMIF controller
reset/gating, OEM expdb, watchdog enablement, force/lazy unmount, system-service killing,
stash/reset/checkout/clean/gc/prune, mixed-tree rebuild or production push.

Risks: real PMIC audio/clock transitions and codec SGEN output (gain31 does not guarantee
electrical silence), bus/driver failure, Android chain-return failure and boot_a modification.
Mitigations are the fixed gates, exact reviewed artifact, off-ear headphones, bounded one-trial
execution, early log preservation, stock backup/recovery and the physically present user.
After reporting this one trial and recovery, stop for Codex's independent review.
