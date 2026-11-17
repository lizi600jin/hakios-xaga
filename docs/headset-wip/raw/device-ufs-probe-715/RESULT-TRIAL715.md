# Trial715: UFS controller snapshot observed in XNU

2026-11-17, Codex independent host review of DSH's single device run.
The user explicitly returned and authorized flashing with manual fallback available.
CMD-DEVICE-715.md released the earlier host-only hold for this trial and stock recovery.

## Result and scope

The UFS snapshot is complete: both reads of all nine whitelisted words were captured,
valid and attempted masks are0x1ff/0x1ff, and all nine pairs match.
The frozen probe classified them FLAGS_OBSERVED(0), with sdb_slots32.
XNU's persisted HLOG has reason0 and a valid full integer checksum.

This establishes a successful observation of these controller registers at the probe point
inside XNU. It is not a UFS storage driver, SCSI command success, DMA ownership result,
filesystem result or proof of any physical link speed/gear/lane mode.

## Fixed input and actual execution

- boot.img22994944 bytes, SHA256
  `65bd797dce99a4f29d898c7055a25fcce207e1b3642109daae0ad217f653c994`.
- Nested kernel15321328 bytes, SHA256
  `ecdc9194fbcc635a63d8da6be7c5f4ef6a1c6bc7f3c1b715c9373cca9fe7da38`.
- Exact229-byte runtime argument string matches arguments715.json.
  mali=0, mtk_ufs_probe=1, headset=0, mali_trial=715, cpus=4, loader_return_after=150.
- Stock baseline was verified before flash, including full64MiB boot_a MD5.
- Experimental flash and reboot native return codes were0; Sending/Writing were OKAY.
  Actual reboot command timestamp:2026-11-17T12:58:15.735208, from reboot.meta.
- The old HLOG was preserved and identified as713, without715 evidence. No duplicate trial.
  Pre-log8388608 bytes, SHA256
  `02617b08129c353ff1ea8cc33d468bd3be50587afba2f6dd030760ee761e26c1`.

No timing comparison is inferred from a later polling origin. This package uses the same
UFS kernel/payload as714 with the reviewed loader/runtime GPU experiment paths disabled.
That comparison condition does not isolate the cause of713's Android return incident.

## Raw controller observations

Summary logged at XNU-relative18.420547; flags report at18.421293.
Log timestamps are software timestamps, not measurements of physical clock signals.

| Offset | Field | First sample | Second sample |
| --- | --- | --- | --- |
| 0x00 | CAP | 0x3587071f | 0x3587071f |
| 0x08 | VER | 0x200 | 0x200 |
| 0x20 | IS | 0x15 | 0x15 |
| 0x30 | HCS | 0x10f | 0x10f |
| 0x34 | HCE | 0x1 | 0x1 |
| 0x50 | UTRLBA | 0x51266400 | 0x51266400 |
| 0x54 | UTRLBAU | 0 | 0 |
| 0x58 | UTRLDBR | 0 | 0 |
| 0x60 | UTRLRSR | 0x1 | 0x1 |

The report's flags-only decode is dp1,utrlrdy1,ucrdy1,enabled1.
CAP[4:0]+1 decodes to32 legacy single-doorbell transfer slots.
No transfer descriptor at0x51266400 was followed or altered. Doorbell0 is only the sampled
word, not proof that ownership was transferred to XNU or that the queue is safe to reuse.
IS was equal here, but is excluded from stability checks because it may change independently.

## Original HLOG identity and independent checks

- Raw R/device715/trial715-logbook.bin:4198400 bytes, SHA256
  `ff2fca7f0b63ac5d14e7d56124d9619621d430d1ef5de8891ad64d77c3c32e14`.
- Body R/device715/trial715-logbook-body.bin:494290 bytes, SHA256
  `ce8fbf536fc7ebdebb66407e871a294bbf1a6af2f9863ed7229c6b4333787098`.
- HLOG v1, reason0, sum(body)=0x26b7a7c=40598140, matching the full64-bit header sum.
- The saved body exactly equals the header-length slice of the original capture.
- Exact715 arguments, one read-only banner/summary/flags line and nine unique raw pairs
  match the frozen C result precedence. No incomplete sample was accepted as numeric data.
- Independent scan found no lines matching the selected panic/data-abort/stop-failure patterns.
  This is a bounded log scan, not a universal hardware-error guarantee.
- Parser regressions passed35 synthetic cases before reviewing the hardware capture, including
  corrupt checksums, wrong identity, incomplete reads, duplicate/missing rows, enum precedence,
  IS-only changes, all-ones responses and32-slot formatted decoding.

See independent-device-review-715.json, review-device-715.py, test-review-device-715.py and
device-parser-tests-715.json. The parser itself never accesses a device or edits raw evidence.

## Recovery

The captured log was secured before waiting for full Android readiness.
DSH then requested adb reboot bootloader; native0, fastboot observed within its bounded wait.
The verified original stock boot_a was flashed with native0 and Sending/Writing OKAY,
then rebooted with native0 at2026-11-17T13:02:09.620610.
The user did not need to manually enter fastboot for this recovery path.

The final proof in device715/D-verify.txt was independently checked: slot_a,
boot_completed1, SELinux Enforcing, full boot_a67108864-byte MD5
`a63b2e35a7c2522cb24a6a10332b9bc8`, and responding SurfaceFlinger.
New boot_id: `11aead4e-9595-4574-8e6a-6b229e287480`, different from the preflight ID.
See independent-recovery-review-715.json. Thus IMAGE_IDENTITY, UFS_SNAPSHOT_CAPTURE,
NORMAL_RETURN and STOCK_RECOVERY have separate supporting evidence and all pass their
specified criteria. No storage-I/O PASS is claimed.
One successful recovery does not show that the earlier713 incident is fixed or identify its cause.

## Next stage

Prefer host work on an explicit kernel/loader UFS queue-ownership and DMA contract before any
real storage command. The active loader path inherits LK's list, as confirmed by its frozen
UFS_USE_LK_LIST setting; the observation above does not replace that protocol design.
Validate address reservation, descriptor lifetime, cache/coherency rules, bounded completion
and loader reclaim before proposing a separate read-only storage-I/O trial.

Do not change clocks, power, reset, UIC state or queue bases just because the flags were seen.
No extra trial, register write, SCSI operation, production edit or GPUEB work was authorized
by the completed715 snapshot command. Existing historical failures and713 manual recovery
remain preserved with their original limits.
