# Response718: bounded standard sense-data segment

716/717 stay sealed NOT_PROVEN.717 directly captured OCS0, transaction0x21, LUN2, tag0,
response0, SCSIstatus0, residual0 and data-segment-length20. Only the old requirement that
the segment length be zero rejected it. Diagnostic DATA CRC32 matched28a2836c, accepted0.
717 captured no bytes after the32-byte response header; its sense length/content are unknown.

## Basis and intentional change

EDK2 UTP_RESPONSE_UPIU explicitly references UFS4.0 Table10.15 and lays out a32-byte fixed
response, followed by a2-byte big-endian sense length and an18-byte sense-data array:
https://github.com/tianocore/edk2/blob/master/MdePkg/Include/IndustryStandard/Ufs.h
Its UfsExecScsiCmds collects sense data, response, SCSI status, OCS and residual independently:
https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Bus/Ufs/UfsPassThruDxe/UfsPassThruHci.c
Linux ufshcd_transfer_rsp_status likewise handles response type/status and residual:
https://github.com/torvalds/linux/blob/master/drivers/ufs/core/ufshcd.c
These primary implementations establish that a response data segment is not the4096-byte
READ DMA count; a nonzero sense container is not itself an IO failure.

718 deliberately changes the response predicate in a NEW revision. It accepts only segment0
or the fixed20-byte sense container. For segment20, its big-endian sense length must be<=18.
The parser requires EHS length0, a new conservative restriction because extended-header data
placement is not implemented. It still requires OCS0, transaction0x21, LUN2, tag0, response0,
SCSIstatus0, residual0, reference CRC and clean restore. Sense content is exposed raw; SCSI
status remains the command-status criterion. This is not a general variable-length parser.

## Collection, failures and unchanged ownership

Always collect52 bytes/13 words after the same known-completion/idle checks. Those bytes fit
inside the pre-existing512-byte response region of the1664-byte UCD. The kernel adds five
bounded CPU reads, no command/controller store or DMA range. attempted/valid now use13 bits
and only1fff permits printing all13 words. Partial/failed callback bytes stay unreported.

Failure bits0..6 retain their fields, with bit5 now rejecting lengths other than0/20. Bit7
rejects a sense length above18 when segment20; bit8 rejects nonzero EHS. Mask0 only means
accepted response when valid1fff and OCS0. Diagnostic-only CRC still does not bypass a failed
response; accepted block validity still additionally requires CRC match and successful restore.

Exact READ10/LUN2/LBA1/4096 command, all whitelist bytes, UTRLDBR1 as sole controller store,
max-poll/time bounds, full saved-memory restoration/readback, lease atomics/STOP and loader
gate remain unchanged. New private loader differs only in trial718 labels/cookie/argument.
No production edit or broader UFS clock/reset/UIC programming is involved.

## Host results and remaining hardware question

ASan+UBSan and MSan each passed engine32513 checks (reads5530/regs49/writes2908), contract983,
atomic40 and guard23. New cases cover segment20 with sense lengths0/18, unsupported lengths,
too-long sense, extended headers, all13 partial response callbacks and1024 partial DATA reads.
Real-XNU-header ARM64 platform compile and three codec builds passed. Loader three native rc0,
47264 bytes/dc1d273481eeab99086726e90762c4dca4a30e7d631fddab31990349ce6e4b25.

The actual718 payload has not been observed. No claim about its contents or accepted block
read is made before a separately reviewed one-shot trial. CRC32 agreement is not byte equality
or SHA256. Current phone is verified stock boot_id77b01ffe-6fc8-4c84-b0e8-0782fc8d6243.
