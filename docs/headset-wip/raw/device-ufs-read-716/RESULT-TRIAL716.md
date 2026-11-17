# Trial716: command completed, response predicate failed; block read NOT_PROVEN

2026-11-17. One device run by DSH, independently reviewed by Codex. Phone automatically
restored stock; no repeat of716. Keep this result unchanged when adding future diagnostics.

| Label | Result |
| --- | --- |
| IMAGE_IDENTITY | PASS |
| READ_EXPERIMENT_CAPTURE | PASS |
| READ_BLOCK_COMPARE | NOT_PROVEN |
| LEASE_RETURN | PASS |
| NORMAL_RETURN | PASS |
| STOCK_RECOVERY | PASS, automatic |

The approved boot22999040 bytes/SHA2564ca5834f555300c6616245e265923ac1897ef9d8d6d12c57638fa870afc9c057
ran with the exact228-byte716 arguments. The fresh guard was magic0x554c5345/version1/trial716/state0.
Both inherited metadata masks were0xff. UTRD points to0x60101000; UPIU decodes to READ10,
LUN2/LBA1,4096 bytes. Thus the new loader's predecessor read left the expected READ descriptor.

Kernel summary at18.940487:
`COMPLETED_ERROR(3) claimed=1 issued=1 restored=1 data_valid=0 polls=117`.
Sampled OCS0; final shared state2, loader_io_allowed1. The original memory was restored and
the lease returned. No block CRC was emitted. This is NOT a successful block comparison.

Inference from the frozen platform/engine path: valid fixed-address response reads return
true or fault, and there was no abort/abnormal return. With OCS0 and result3, the response
predicate did not accept the packet.716 did not log the eight response words, so no particular
field or root cause is established. Do not weaken a predicate based on this inference alone.

Raw4198400 bytes SHA256
`f8b7b52dd29f19e81a65a096989d723a088e31d8cbb700251b6fe9fcf7a9b1ea`;
body497672 bytes SHA256
`1f2a34cbde7db4433aaca5a30ae3ed8bd78460501457bd383d3c036d7efd57ea`.
HLOGv1 reason0, full sum0x26f7ecc=40861388 exactly matches the64-bit header.
The independent parser returned DIAGNOSTIC_ONLY/rc1 as designed, with39 synthetic parser
checks passed. rc1 is the diagnostic status, not a checksum/format-parser failure.

The actual reboot command was2026-11-17T14:02:52.655427; capture started14:06:33.509541.
The later polling observation is not treated as the reboot origin or compared with other runs.

Recovery: adb reboot bootloader native0, observed fastboot transport, verified stock flash
native0/OKAY, reboot native0. Whole boot_a67108864-byte MD5 is
`a63b2e35a7c2522cb24a6a10332b9bc8`, slot_a, boot_completed1, Enforcing and responding
SurfaceFlinger. New boot_id `396b9126-ffbd-4892-a201-2b559ebad0a0`.
No manual fallback was required. See independent-recovery-review-716.json and original transcripts.

Evidence is archived by DSH at330727c. Codex's additional result/recovery review accompanies
the original raw capture. No raw Android GPT upload, production edit, GPUEB change or extra trial.

Next minimal action: a NEW diagnostic revision/trial may log response raw words, collected
masks and exact failing predicate, plus a separately labelled diagnostic data CRC after known
completion. Preserve all current command/lease/restore acceptance and the716 NOT_PROVEN result.
Do not claim SCSI data success, byte equality, filesystem/block-device support, throughput,
link mode or a fix for713. Any subsequent hardware run needs its own reviewed command.
