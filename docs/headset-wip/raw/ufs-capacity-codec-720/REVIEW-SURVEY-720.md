# Codex review of capacity survey720

719 is sealed and phone stays stock. No720 kernel integration or device release exists.

Accepted: ufs_capacity is defined but not called in the active preprocessed loader. Hardcoded
4096/GPT probing is not an RC10 runtime result. Android blockdev capacity is a separate layer's
observation, not an in-kernel controller response. A different observed geometry must remain
visible and must not be converted into a transport error or silently replaced by Android's
number. Suggested causes such as reserved areas/different LUN are unverified, not findings.

Two corrections are required:

1.0xffffffff in READ CAPACITY(10) is not unconditional evidence of no medium. Linux's
read_capacity_10 no_read_capacity_16 branch is a conditional device-quirk path, while its
normal sd_read_capacity path attempts RC16 when the derived count exceeds0xffffffff.
For720 it must be reported as an unresolved RC10/saturated value, with NO automatic RC16,
retry, reset or no-medium claim. A separate SCSI status/sense response would be needed for a
medium-absence diagnosis. Primary source:
[Linux sd.c](https://github.com/torvalds/linux/blob/master/drivers/scsi/sd.c), functions
read_capacity_10 and sd_read_capacity. Local corresponding evidence is in
/tmp/mt6895-mainline/drivers/scsi/sd.c:2863-2874 and2915-2934.

2.The mistyped0x01dcbfff differs from0x01dcafff by4096 in an LBA number. That is4096 logical
blocks, or16777216bytes at4096bytes/block, not one logical block. The correct expected
lastLBA remains31240191=0x01dcafff. Root's initial hex transcription error was caught before
candidate use; preserve its record and this units correction.

The statement that only the first8bytes may be prefilled is also too strong. The command's
advertised expected data length and PRDT length must be8, but the owned4096-byte scratch
may be fully prefilled and read back as a canary, then fully restored. Future integration
must define and test this; it is not a rule to skip tail verification.

Root's pure codec will separate geometry decoding from Android-reference comparison. A
parsed512-byte logical block report can be observed without authorizing the xaga4096-byte
READ path. Geometry mismatch is a separate observation. No fallback changes media/queue state.
