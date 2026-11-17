# Diagnostic717

716 is sealed: COMPLETED_ERROR(3), OCS0, data_valid0, clean restored lease, normal return,
automatic stock recovery. Its missing response words prevent identifying the rejected field.
READ_BLOCK_COMPARE remains NOT_PROVEN. Do not reinterpret716 as successful disk IO.

717 adds observations to that exact engine, with a new trial cookie and private source trees.
The fixed READ10 LUN2/LBA1/one4096-byte block, whitelist, sole UTRLDBR1 store, mappings,
predecessor read/CRC, lease/STOP ordering, restore/readback, timeout and loader gate are unchanged.
716 engine7c2b4df4 and all old evidence stay intact.

## New observations

The response reader marks attempted before each read and valid only after success. It exposes
eight little-endian words only with valid==0xff. Unread words remain zero; a failed callback's
written value is not copied. No raw word is invented from a failed read.

After OCS0 and eight collected words, response_failures describes EXACTLY the716 predicate:

| Bit | Rejected716 condition |
|---|---|
| 0 | response byte0&0x3f is not0x21 |
| 1 | byte2 is not LUN2 |
| 2 | byte3 is not tag0 |
| 3 | byte6 is not response0 |
| 4 | byte7&0x3f is not status0 |
| 5 | byte10 or byte11 is nonzero |
| 6 | bytes12..15 big-endian residual is nonzero |

Mask0 is interpretable only with a full response and OCS0. A missing observation does not
mean acceptance. A nonzero mask still gives COMPLETED_ERROR and data_valid0.

After known completion and idle verification,717 also reads the fixed4096-byte scratch DATA
even if this response predicate failed. This adds bounded CPU reads, no new DMA/controller
command. A separate diagnostic_valid is set only after collecting all1024 words and CRC32.
The diagnostic-only CRC is not an accepted block result. No data CRC is emitted after a partial
diagnostic read. Only the unchanged response predicate plus existing CRC/restore success can
produce READ_OK/data_valid1. This revision does not relax a predicate to obtain success.

## Host checks

ASan/UBSan and MSan each: engine32384 checks, contract983, atomic40, loader guard23. The
engine covers every read/register/write failure position, all eight partial response reads
(including a callback writing deadbeef before returning false), and all1024 partial diagnostic
DATA reads. Differential mutations check that the new failure mask preserves716 acceptance.
ARM64 engine/contract/atomic codec builds and the platform's real XNU-header compile passed.
Private loader compiled three native rc0; legacy warning text is retained.

Kernel input count5210; changes relative716 are exactly the platform log, engine diagnostics,
and contract trial cookie. Loader changes are the cookie and two trial labels. No production edit.
Full kernel build, image packaging and hardware execution require subsequent distinct records.

## Return and limitations

Loader gate failure intentionally halts without a storage write or warm return. Manual fastboot
may be needed; it can leave only the prior boot note. One reviewed DEVICE command is needed
before any717 phone action. User previously authorized flashing and manual fallback.
Device currently stock boot_id396b9126-ffbd-4892-a201-2b559ebad0a0, boot_a MD5a63b2e35a7c2522cb24a6a10332b9bc8.
Known-complete DMA/idle criteria remain the716 criteria; a diagnostic CRC alone does not
establish a correct response, persistent block service, filesystem or throughput.
