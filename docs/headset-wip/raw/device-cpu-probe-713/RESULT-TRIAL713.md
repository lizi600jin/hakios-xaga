# CPU713 result — 2026-11-17
Codex independent host review and DSH device/recovery evidence.

## Outcome

Image identity and three-domain raw CPU capture verified. XNU handed back with HLOG reason0.
The Android-side reboot-to-fastboot command subsequently timed out. The user manually entered
fastboot before DSH could restore stock. This is NOT an unattended recovery pass.

Stock recovery is complete:
boot_a67108864 B, MD5 a63b2e35a7c2522cb24a6a10332b9bc8; slot_a; boot_completed1;
SELinux Enforcing; SurfaceFlinger responds; boot_id c8acf0e0-ec30-4b5a-906b-fe29e1a74a9d.
Evidence: device713/D-recovery.txt, D-recovery-RETRY.txt, D-verify.txt.
No further device work in this run; remaining work is host-only.

## Identity

boot.img22994944 B:
127e48e9c7089e9845bfd0a3565ccacfb1b80acd3d84776b3946fa8a702b165c.
Kernel15321336 B:
a80231ed9dc78a82bfddc71f34685808e66ce490d70ed059e37cd108638885c3.
Raw HLOG4198400 B:
310c2f0a8084019c8b9f72d877b7d7f0670d7eda0b952d9dd991c8c73f073213.
Body519004 B:
7fbecaa318c1e1496c23f1fedfe8d9e4d66ed816f141faa0587efb92345b87d8.
Header reason0, sum0x2865061; complete integer sum(body) equals the header sum.
Runtime arguments539 B match the approved string.

independent-package-review.json: strict stock/image/XNUP/arguments checks passed.
independent-loader-review.json: frozen loader mirror matches712, independent rebuild byte-identical.
independent-device-review.json: three unique summaries, 96 unique paired rows, full checksum verified.
These are separate from the stock recovery evidence.

## Raw CPU observations

All paired samples were identical.

| Domain | Base | enable raw | status raw | request index | candidate result |
|---|---|---|---|---|---|
| 0 | 0x11bc10 | 0x201 | 3 | 0 | 3 |
| 1 | 0x11bd30 | 0 | 3 | 19 | 3 |
| 2 | 0x11be50 | 0 | 3 | 28 | 3 |

Candidate result3 means its exact enable==1 predicate did not pass.
It does NOT establish that the CPU cluster is physically disabled, the firmware absent,
or its voltage/clock unusable. The raw0x201 includes bit0 plus another bit; do not erase
that other bit by applying a guessed enable write.

## Offline LUT interpretation

Using only the frozen vendor low12-bit frequency interpretation on the captured words:
- domain0: 25 nonzero descending rows, 2000..450 MHz; requested row0 maps to2000.
- domain1: 32 nonzero descending rows, 2850..200 MHz; requested row19 maps to1400.
- domain2: 29 nonzero descending rows, 2850..500 MHz; requested row28 maps to500.

These counts agree with the earlier Android sysfs table counts. This supports the table
layout interpretation, not an actual CPU frequency measurement or a safe write protocol.
Full arrays and limits: offline-lut-interpretation.json.

## Recovery precision

At 00:50:24 DSH had captured the experiment's HLOG.
At 00:50:30 adb reboot bootloader timed out, and bounded fastboot checks did not find it.
At 00:53:46 fastboot was observed after the user intervened.
At 00:53:53 stock flash began; it completed in2.18s, native rc0, Sending/Writing OKAY.
Stock reboot was issued at00:53:55; later full verification passed.

An available adbd does not mean the phone is "only in kernel mode".
What is established: XNU return, an Android-side command channel that had allowed capture,
a timed-out reboot command, missing fastboot, and recovery after manual entry.
The cause of the reboot timeout and its relationship to the experiment remain unproven.

## Next host work

Clarify the MTK control/firmware ownership and enable-word semantics before proposing writes.
Do not simply mask0x201 or copy the vendor enable write into XNU.
Preserve the successful raw capture and the recovery failure together.

UFS work continues as an isolated diagnostic candidate. No CPU/UFS frequency, power,
clock, queue or filesystem changes have been authorized for execution by a later artifact
merely existing on disk. This run remains host-only.
