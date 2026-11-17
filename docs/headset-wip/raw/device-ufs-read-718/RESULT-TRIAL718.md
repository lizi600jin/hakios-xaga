# Trial718: accepted bounded READ10, CRC32 reference match, stock restored

One reviewed trial, no repeat. DSH device archive fdc574d. Codex independently parsed original
HLOG/raw body and verified recovery records after stock was back. Parser native rc0/status PASS.

| Label | Result |
|---|---|
| IMAGE_IDENTITY | PASS |
| READ_EXPERIMENT_CAPTURE | PASS |
| READ_BLOCK_COMPARE | PASS, CRC32 agreement |
| LEASE_RETURN | PASS |
| NORMAL_RETURN | PASS |
| STOCK_RECOVERY | PASS, automatic |
| DIAGNOSTIC_RESPONSE_CAPTURE | PASS |

## Identity and direct runtime evidence

Image22999040 B SHA2563a3ceb1d77c9a61f289695879c077baf7ec076a8554c936aec8110afd1e61970,
MD542beb68e83eabea8a281965888380417. Loader47264 B SHA256
dc1d273481eeab99086726e90762c4dca4a30e7d631fddab31990349ce6e4b25.
Payload19533320 B SHA256a43093c1479e34debc82ebca787a3c59735aa79279a3f5ec78b686183b673aba.
Kernel15338864 B SHA25660bd6b195c2ab65ab0c5c1cb519fe3cf5af9ad64c351485ccbf59aa899fd7cfe.
Native make2 versus explicit KERNEL judge0 are distinct, with only the established symbol-set
allowance. Host engine32513 checks passed under ASan+UBSan/MSan; evidence parser58 cases passed.

Exact228-byte runtime arguments, fresh guard trial718/state0 and READ-only inherited UTRD/UPIU
were present. Kernel result READ_OK(5), claimed1/issued1/restored1/data_valid1, polls116, OCS0.
Response valid1fff/attempted1fff, failures0; thirteen little-endian words:

```
00020021 00000000 14000200 00000000 00000000 00000000 00000000 00000000
10801200 00010001 00000000 00000000 00000000
```

Decoded fields: transaction0x21/LUN2/tag0/response0/status0, EHS0, segment20, residual0,
sense-data-length18. All bounds and response conditions of the reviewed718 parser passed.
Sense contents are retained raw; no extra semantic or device-health conclusion is inferred.
DATA diagnostic accepted1 and accepted block CRC both0x28a2836c, reference0x28a2836c,4096 bytes.
The kernel's one READ10 therefore passed its bounded response, reference CRC and clean-restore
conditions. This is CRC32 agreement, not mathematical byte equality or a full-block SHA256.
Final shared state2/loader_io_allowed1 establishes lease return separately from block comparison.

## HLOG and recovery

Raw4198400 B SHA256825da97d036603a4796ffafb1b4f2554fbe9d38e3a4bad37fe743d7b8d5e8719.
Body497459 B SHA2568f0d1e8a2a545201bdcff283aabbb21c048ea00becc93acfbee29c14c705918d.
HLOGv1/reason0, full integer sum0x26f45ab matched header; error-context scan was empty.
Actual experimental reboot2026-11-17T14:50:43.319700; capture14:54:28.352026, about225 seconds,
within300-second budget. Stock flash native0/2.25 seconds with Sending/Writing OKAY; reboot0.
Full boot_a67108864 MD5a63b2e35a7c2522cb24a6a10332b9bc8, slot_a, boot_completed1,
SELinuxEnforcing, fresh boot_idf745b1c6-ee43-4abc-8ed2-06ee9629bf27 and responding SurfaceFlinger.
No manual fallback was needed or reported. Recovery preceded optional parsing/archive work.

## What changed and what remains unproved

717 directly located its sole rejection at data-segment-length20. Primary response-layout
review showed the separate fixed sense container;718 intentionally accepts bounded segment0/20,
adds sense<=18/EHS0 restrictions and collects52 response bytes. Command bytes/geometry, CRC,
timeout, lease and restore remain unchanged. This is a protocol revision, not unchanged acceptance.

716 did not record the response words; its exact rejected field remains unproved. The718 result
supports the earlier response-length hypothesis but does not reconstruct716 or prove that all
prior data/transfer failures had one exclusive cause.716 and717 remain sealed NOT_PROVEN.

No persistent block device, arbitrary/multiple blocks, partition/filesystem mount, kernel write
service, throughput, physical link rate/gear/lane, GPU state or CPU713 recovery fix is established.
Existing loader boot-note/HLOG writes remain the baseline; the new kernel command is READ-only.
Production source remains unchanged by this work; reviewed sources are provided as a separate
exact snapshot with manifests, host/pack/device evidence and explicit remaining scope.
