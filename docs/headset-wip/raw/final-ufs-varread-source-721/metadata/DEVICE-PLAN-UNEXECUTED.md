# Trial721 revision2 device plan — PLAN ONLY

**Nothing here has been executed.** No query, no flash, no reboot, no capture. Every action
needs two separate approvals: **root technical authorization**, then the **operator's item-by-item
release**. There is no blanket approval for the list.

Historical values below were **not** re-queried this round; each is a recorded fact.

## Two corrections carried from review

1. The loader return gate does **not** restore `boot_a`. It writes the HLOG and hands the phone
   back to Android. `CMD-DEVICE-720.md` step 5 says "on failure restore stock instead of
   proceeding" and step 7 lists the stock restore as its own step.
   **A stock restore is therefore required on the success path as well**, as its own flash.
2. Recovery comes **before** analysis. Capture is taken only when it costs nothing on the clock.

## Action table

Each row is one action with its own approval and release.

| # | action | kind | root auth | operator release |
|---|---|---|---|---|
| A1 | record identity (`boot_id`, slot, SELinux, stock `boot_a` MD5) | read-only | separate | separate |
| A2 | read-only pre-logbook save of the 8 MiB `xnusys` tail (see below) | device read-only | separate | separate |
| A3 | host-side verify of the three local image hashes | **host only** | — | — |
| A4 | enter bootloader (`adb reboot bootloader`, or the operator's existing fastboot) | mode switch | separate | separate |
| A5 | `fastboot getvar current-slot` — must be `a`; abort otherwise | device read-only | separate | separate |
| A6 | **experimental flash of `boot_a`** with the r2 image | **device write #1** | separate | separate |
| A7 | `fastboot reboot` | reboot | separate | separate |
| A8 | bounded wait for adb, ≤300 s from A7 | passive | — | — |
| A9 | **restore `boot_a` to stock** — always, success or not | **device write #2** | separate | separate |
| A10 | `fastboot reboot` after the restore | reboot | separate | separate |
| A11 | verify restored stock (`boot_id`, slot `_a`, `boot_completed`, stock `boot_a` MD5) | read-only | separate | separate |
| A12 | read the log back from stock Android (see below) | device read-only | separate | separate |

**A9 is unconditional.** It runs whether the trial worked or not, and it is a second write that
must never be folded into "one experimental flash". If the operator releases only A6, the phone
stays on the trial image.

## Ordering when adb does or does not come back

The sequence is not a single chain; A8's outcome picks the branch.

- **adb works within 300 s** → A9 restores first, then A12 reads the log from stock Android.
  The log lives in flash and survives the restore, so nothing is lost by restoring first, and
  the phone is protected sooner.
- **adb never appears but fastboot does** → A9 restores immediately, then A12 reads the log from
  stock Android afterwards.
- **neither appears** → stop. Report to root and the operator for a physical recovery. **No
  repeated reboot**, and never a long press on a black screen.

Capture is therefore **not** a precondition for recovery, and recovery is never delayed to obtain
a log.

## HLOG capture (A12, and A2)

Mechanism read from the loader source, not guessed:

- the log lives in the **tail of `xnusys`**, which Android never mounts
- `XNU_LOGBOOK_SLACK = 8 MiB` reserved, `XNU_LOGBOOK_MAX = 4 MiB`
- offset = partition_start + partition_size − 8 MiB, computed by `logbook_offset()`
- header (little-endian): `magic 0x474f4c48` ("HLOG"), `version`, **`len`**, `reason`, `code`,
  `elr`, `far`, **`ticks`**, **`where`**, **`sum`** (64-bit additive checksum of the body)
- the loader's own comment gives the form:

      dd if=/dev/block/by-name/xnusys of=<target> bs=4096 skip=SN count=1025

  with `SN = offset / 4096`. Recorded geometry for this device from previous rounds:
  partition `2685370368` bytes, offset `2676981760` = `0x9f8f8000`, `SN = 653560`,
  capture **4198400 bytes** (4096 header block + 4194304 maximum body).

**A2 is a read-only pre-save** of that same 8 MiB window into a local file, so an existing trial721
record can be recognised and the stage stopped before repeating it. Raw bytes stay local.

**Clearing the logbook region is a flash write** and is not in this plan. The plan does not need
it: staleness is bounded by evidence, not by wiping (see below).

## Bounding a stale log (what the header can and cannot prove)

`ticks` and `where` do **not** by themselves identify a stale log: `where` is constant for a
fixed position, and this run's `ticks` value is not known in advance. What is used instead, in
combination:

- pre/post raw hash difference from A2 versus A12
- exactly one boot of this stage (the loader writes one header per run)
- the exact frozen boot-args line, including `mali_trial=721` and `mtk_ufs_varread=1`
- the `mt6895-ufs-varread:` prefix on all nine driver lines
- an ordinary `reason`/`code` pair, not a panic or abort code
- the full integer sum of the body equal to the header `sum`

**This still is not a wire trace**, and the plan does not claim it is.

## Privacy

The device serial appears only as `<SERIAL>`. Every argv, stdout and stderr is redacted before
anything is written to disk. `xaga-pull-log.sh` is **not** used directly: it echoes the serial,
so its output is filtered and only the redacted form is kept.

## Parsing (host only, reviewed tool)

```sh
python3 R21/root-review-721-v1/review-device-721.py <raw> \
    --body <exact-body> --arguments <args721 frozen> --output <new report>
```

`parse-hlog.py` from H1 is **disabled** and must not be used for acceptance; its reasoning is in
`host-tools/PARSE-HLOG-DISABLED.md`.

## What this stage still cannot claim

- no wire trace: LBA binding rests on exact-request readback plus source structure
- a CRC match is a checksum agreement, not byte equality with the reference
- one run says nothing about throughput, gear/lane, or the block layer

## Prohibitions that remain

No GPUEB, no physical `0x13c40000`, no `fastboot oem expdb`, no watchdog pokes, no long press on
a black screen, no `erase`, no OEM unlock changes, no getter or MMIO beyond the eight frozen
`read_reg` offsets and the single doorbell store.
