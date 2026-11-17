# Trial720: independent in-kernel RC10 geometry observation

One experimental boot and one kernel READ CAPACITY(10),LUN2. Root independently reviewed
original HLOG/body and DSH stock-recovery records: query/data/geometry/lease/normalreturn PASS;
Android comparison MATCH. No retry,RC16,variableREAD or block-device registration occurred.

## Observations

Result5/claimed1/issued1/memory_restored1/query_valid1,polls279. OCS0 and full13-word response
valid=attempted1fff/failures0. Transaction21,LUN2,tag0,response/status0,EHS0,segment0,residual0.
Raw valid=attempted=changed3,words `ffafdc01 00100000` correspond to bytes
`01 dc af ff 00 00 10 00`. Independent big-endian decoding gives:

| Field | Observed |
|---|---:|
| LastLBA | 31240191 (0x01dcafff) |
| Logical block bytes | 4096 |
| Logical blocks | 31240192 |
| Capacity bytes | 127959826432 |
| Android block-layer comparison | MATCH |

This is an independent query observation compared with the earlier Android report. Android's
number was not substituted for the received payload. The earlier drafthex0x01dcbfff was an
arithmetic transcription error corrected before code/hardware use; it is not an alternative
measurement. Query completion,geometry decoding and reference comparison remain separate.

Tail complete1/ok1/checked1022/first_badffffffff covers offsets8..4095 of the4096-byte scratch.
The entire table/UCD/DATA was restored/readback-verified before finish. End of init:cached
control2/sharedCLAIMED1/deferred1. Actual loader audit at HLOG EOF:magic554c5345/version1/
trial720/stateRESTORED2/allowed1. The audit is loader-generated,not a kernelquiesce log.
Kernelquiesce uses cachedatomics/alignedsharedDRAM only; allCPUstop is not a safety premise.

## Identity and capture

| Artifact | Bytes | SHA256 |
|---|---:|---|
| boot.img | 22999040 | 87722d3250f6fd3d9ca16280d5af5397830eb6b7b8a610f2d22b9c4d2ed5b085 |
| loader.bin | 47984 | 91e6e113f7209a9ffd7534c7152e577f3cac64fcdfdf305dbff49eb72aa0861a |
| payload.bin | 19533144 | d0b202c08e41bec468ad9e10737b3723d41be07071d6ccf0b1fbfdf0c5ba7487 |
| kernel | 15338680 | 179efc781d17b41721a2d9cd5e83fb23d9b279f44c253aff27ddc00604bca19f |
| raw HLOG | 4198400 | b68599bcff24d894144c5fed6191b83d97bf5d04e6dc34d4fa50969982c4d117 |
| body | 497300 | cba560ba20e5c41da5940ef937fe8abb3165fe9fb7cf27ff5642f653f81580fc |

HLOGv1/reason0/fullinteger sum40839762=0x26f2a52. Raw/body identity and full64-bit checksum
verified,not a low-byte check.232-byte runtime arguments and fresh720guard matched. Kernel
source5220 and46runtime/loader files verified. Make native2 and explicitKERNELjudge0 remain
separate; private loader reproduction3native0/byte-exact. Signature retention is structural,
not cryptographic validation. Historical wrongjudge command is preserved as an invocation error.

## Recovery

Actual reboot2026-11-17T20:47:27.941216,capture around20:51:12,approximately225seconds,within
300second budget. Separate adb observation around+219seconds is not a performance claim.
Stock recovery was automatic:adb rebootbootloader0,correctfastboot,stockflash0/OKAY,reboot0.
Verifiedslot_a,boot_completed1,SELinuxEnforcing,67108864-byte boot_a/fullMD5
a63b2e35a7c2522cb24a6a10332b9bc8,newboot_id76b7316c-cf63-4be1-9e44-91de266963bf,
and a responding SurfaceFlinger. No manual fallback or additional experiment was needed.

## Limits and retained evidence

One geometry query does not establish variable-LBA IO,a persistent whole-disk block device,
userland consumer,filesystem,throughput,gear/lane,global controller exclusivity,CPU713 or GPU
repair. Tailcanary covers sampled bytes,not all possible physicalDMA corruption. Host false-
return faults do not model realMMIOabort recovery. Sense contents remain uninterpreted.
0xffffffff remains unresolved/saturated,not no-medium; nofallback/autoRC16 is implemented.
One HLOGsavecall may issue multiple WRITE10commands in the existing loaderwriter.

Codec23295checks and engine9511faultpositions/1022tail corruptions passed both sanitizers;
65955732engine assertions include perIOchecks,not independent case count. Root privately
corrected two mislabeled adapter tests (trueSTOPinstead oftimeout; fullstatus2response instead
ofpartialreadclaim),without changing runtimeC.24scenarios per sanitizer and byte-exactnative
ARM64object passed.86syntheticparser cases/hostformatchecks are separate from this hardware.
Initial compile/manifest/test wording mistakes remain recorded,not counted as passed runs.
718/719 remainPASS;716/717 remainNOT_PROVEN and716's exact rejected field remainsunknown.
Production tree was not merged/pushed; oldimages/source/BUILD and GPUEBWIP are preserved.
