# Trial717: response predicate localized; block result remains NOT_PROVEN

One reviewed diagnostic trial was executed and automatically recovered to stock. No repeat.
DSH docs device commit d9d81b6. Codex independently parsed the preserved raw/body and reviewed
the recovery records after stock recovery. DIAGNOSTIC_ONLY intentionally gives parser rc1.

| Label | Result |
|---|---|
| IMAGE_IDENTITY | PASS |
| READ_EXPERIMENT_CAPTURE | PASS |
| READ_BLOCK_COMPARE | NOT_PROVEN |
| LEASE_RETURN | PASS |
| NORMAL_RETURN | PASS |
| STOCK_RECOVERY | PASS, automatic |
| DIAGNOSTIC_RESPONSE_CAPTURE | PASS |

## Direct runtime evidence

Image22999040 B SHA256c275625ff431ee93b8fdc87284c1336bd631d7721af13415deba124392496699;
kernel15338864 B SHA25607b016feb866c4347d72a3915ef2dce15045bfa2d0628d3d1f9eb5ff385506d6.
Fresh guard717/state0 and exact228-byte runtime arguments were present. Inherited UTRD/UPIU
each had valid0xff and matched the reviewed READ-only predecessor. One kernel request was
issued, polls113, OCS0, result COMPLETED_ERROR(3), claimed1/issued1/restored1/data_valid0.

Response valid0xff/attempted0xff, failures0x20. Eight little-endian words:

```
00020021 00000000 14000200 00000000 00000000 00000000 00000000 00000000
```

Decoded header: transaction0x21, LUN2, tag0, response0, status0, data-segment-length20,
residual0. The sole rejected716/717 predicate is bit5: bytes10..11 were required to be zero.
This identifies717's rejected condition. It does not recover716's missing response bytes or
by itself establish which protocol interpretation should replace that condition.

Diagnostic-only DATA CRC32 was0x28a2836c, expected0x28a2836c,4096 bytes, accepted0. This is CRC32
agreement with the independently checked Android LUN2/LBA1 reference. It is not mathematical
byte equality or a full-block SHA256 comparison. Matching CRC did not override the response
rejection and did not change data_valid. No accepted block comparison is claimed for717.

The raw capture4198400 B SHA256e3c87f1a5836791c918da899b953197f861781c36a88ecd138038572f99da19a.
Body496731 B SHA256234c8b7284805420daf43ea4b0a6d813c223bb05454bdf5acdc897e389f10b9a.
HLOGv1/reason0/full integer sum0x26e44bc matches the header. Error-context scan was empty.
Final shared state2/loader_io_allowed1 confirms clean lease return separately from block result.

## Return and stock

Actual experimental reboot2026-11-17T14:31:07.007904; capture14:34:40.480719, about213.5 seconds,
within the300-second budget. Stock flash native0,2.14 seconds, Sending/Writing OKAY; reboot0.
Full boot_a67108864 B MD5a63b2e35a7c2522cb24a6a10332b9bc8, slot_a, boot_completed1,
SELinuxEnforcing, fresh boot_id77b01ffe-6fc8-4c84-b0e8-0782fc8d6243 and responding SurfaceFlinger.
No manual fallback was needed or reported. Recovery preceded optional parsing/archive work.

## Next-stage boundary

717 captured32 response bytes; it did not capture the advertised20-byte response data segment.
EDK2's UFS response structure places sense length/data after that header; see the separately
recorded primary protocol references. A new candidate should bound and observe this payload
before accepting it. Keep fixed command/LUN/LBA/geometry, lease/restore/timeout, and old717/716
verdicts. No persistent block device, filesystem, throughput, link parameters, GPU state or
CPU713 recovery fix follows from this trial.
