# 720 host-only fixed READ CAPACITY(10) codec

719 is sealed9d6488c, phone stock61d07dda/full MD5a63b2e35. This new candidate is a portable
wire codec only. It has no MMIO adapter, engine integration, kernel entry, loader change,
image, or authorized device trial. It cannot issue any command or declare IO completion.

## Wire and observation design

Construct exactly80 bytes for LUN2/tag0, command data-IN and SCSI opcode0x25, expected length8,
PRDT DBC7, fixed verified scratch addresses, inherited response/PRDT placement. All other CDB,
reserved and flags bytes are fixed. Whitelist compares every byte. There is no READ10/WRITE/
RC16/reset/retry constructor. This is an intentional NEW command class, not a revision of
719's sealed READ10 whitelist. Future integration must assert its address/layout agreement.

The response data has two big-endian32-bit values: lastLBA then logical block length. The
derived count is last+1 and capacity is computed in64 bits. This interpretation is supported
by [Linux read_capacity_10](https://github.com/torvalds/linux/blob/master/drivers/scsi/sd.c).
Opcode0x25 is independently listed in
[EDK2 Scsi.h](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/IndustryStandard/Scsi.h).

Android reference is a BLOCK-LAYER report for independently established LUN2:
127959826432bytes/4096=31240192blocks; last31240191=0x01dcafff. Root initially mistyped the
hex in the survey CMD and corrected it before any candidate. It is a comparison value,
not an already observed XNU RC10 result. Valid differing geometry remains a distinct report.
No numeric mismatch changes command completion into an IO error or overrides reported data.

Decoder accepts any nonzero block-length observation, including512/520, without authorizing
the xaga4096-byte READ path or a new block device. Zero block length is separately invalid.
0xffffffff lastLBA is reported as RC10 unresolved/saturated, not a no-medium diagnosis.
Linux's no_read_capacity_16 no-medium branch is a conditional quirk; its normal path attempts
RC16 for large capacity.720 does not construct RC16 or do fallback. Non-OK leaves output
untouched; future diagnostics must preserve the original eight payload bytes separately.

## Before any hardware integration

An eight-byte decode is not proof of completed IO. Require the inherited queue ownership,
fresh lease, accepted full response/OCS0/status0/residual0, and full restore/readback exactly
as the existing guarded path. DataSeg0/20 and bounded sense/EHS rules remain distinct from
the expected DMA data length8. Do not require response headers to be identical across calls.

Owned scratch stays4096bytes: future integration may prefill/read back all bytes, transfer8,
verify the untransferred tail as a canary, then restore ALL4096bytes plus UCD/table. The host
codec does not implement these guarantees. No direct call to old loader ufs_capacity() is
permitted: it has no active callers and includes unsafe init/error/fallback policy for this
scope. No global exclusive ownership proof follows from excluding known native drivers.

Next concrete step after independent codec review: design/fault-test a separate query engine
adapter with exact8-byte completion and tail/restore checks, without changing719, then freeze
new inputs for any kernel build. No720 kernel/image/device work is released by this document.
