# Trial716 host review and guarded single-block read plan

2026-11-17, Codex. User requested continued progress and has authorized flashing with fallback.
No716 hardware run has occurred. Host build and packaging are complete; separate DEVICE command
is still required after independent package review. Keep715 and its successful evidence sealed.

## Approved host identity

R=/home/lizi/IOS/hakios-mtk/logs/ufs-read-path-20261117

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| trial716/boot.img | 22999040 | 4ca5834f555300c6616245e265923ac1897ef9d8d6d12c57638fa870afc9c057 |
| trial716/loader.bin | 47264 | ca0112e2d6659c37a469fe60881f9a14d8de5e87008ee50cce8007572b800a13 |
| trial716/payload.bin | 19533288 | c13981d45c8d46d9336a119420a8b8490c1d8fb3801a66b65c4bf0f0ca9bba0a |
| kernel | 15338824 | c7f2f3222f314b82885b0567975e3af759c9b172ececd6fbefe437b65ddff6aa |

boot MD5 `3abcb40dfe056bb152eee1fbbef9af57`.
Base arguments168 bytes, runtime228, withNUL229<=608.
mali=0,headset=0,cpus4,loader_return_after150,watcher0; all five loader GPU macros0.
BOARD_UFS_READ_LEASE1; mtk_ufs_read1; trial716. No old mtk_ufs_probe or CPU probe argument.

The private loader source differs from715 in exactly one C file and two new headers. The new
kernel input tree has5210 files: four changes and four additions relative to715, no deletions.
No production source changed. Engine v3 remains frozen at7c2b4df4; platform atbbec3f6d.

## Independent checks completed by Codex

- All5210 kernel inputs and51 candidate/loader source identities match their frozen manifests.
  No new/deleted files outsideBUILD. Native make2 and explicit KERNEL judge0 recorded separately.
- The compiled loader_return.o has an actual undefined reference to the new quiesce function;
  the final kernel defines init/quiesce and gPhysBase/gPhysSize. Symbol strings alone were not
  used to establish the hook's compilation. This still does not establish runtime execution.
- Active legacy UFS/UFShc objects are absent; kernel storage-return functions are empty stubs.
  They do not issue another UFS request before the new loader gate.
- Private loader reproduction matched ca0112e2 byte for byte. With the guard disabled and715
  arguments, the new private source reproduces the original715 loader c3d9c30a byte for byte.
- Strict unpack passed: approved kernel nested exactly, frozen ramdisk/version,24-byte zero
  XNUP terminator and exact EOF, stock Android Image/ramdisk/signature bytes preserved, only
  documented loader/header patches. Signature byte preservation is not crypto validation.
- Engine25502 checks under ASan/UBSan and MSan; atomic publication40; codec983; loader gate23.
  Platform compiles against real XNU headers. No host model is claimed as hardware evidence.
- One packaging preflight failed on my mistaken argv-length assertion(9 rather than8). No
  packaging subprocess ran; the lone copied loader and failing script are preserved under
  trial716-plan-rejected-1. Corrected code audits plans before creating final output.

## Critical integration behavior

The existing loader writes boot_note just before kernel entry, leaving a WRITE descriptor.
The new loader then reads LUN2/LBA1 through its existing driver into the known scratch buffer,
compares CRC32 with the Android reference, and only then initializes the lease. The kernel's
inherited-descriptor predicate can thus remain READ-only. The loader read is NOT the kernel test.
Kernel response zeroing and A5 data prefill are read back before its independent request.

The first statement of xnu_returned checks the lease before any HLOG/GPT/storage operation.
UNUSED or RESTORED permits storage. Invalid/CLAIMED/POISONED halts for manual fastboot,
without writing HLOG or warm-starting Android. A failed predecessor read also halts.
These failure paths can lose the current HLOG and are deliberate, not automatic recovery claims.

STOP is retained in an atomic cached software phase. The phase is never reset toUNUSED during
init. Quiesce may publish POISONED to shared uncached DRAM, without controller accesses/waits.
Late memory stores after a STOP observation race are not excused by assuming all CPUs stopped:
the safety argument is that lease return cannot win and the loader is denied IO/handover.

## Comparison baseline and acceptance

Independent Android reference: LUN2 established through sysfs, logical block4096. Primary GPT
header/array CRCs passed bounds and content checks. LBA1 SHA256
`f94a6807b4afbc7899d3621f570108eec4ffb5504a332198d0e05293f029449c`, CRC32 `0x28a2836c`.
The raw reference remains local. No backup GPT was read or is needed for this one-block test.

Hardware acceptance is separate for image identity, experiment capture, block CRC comparison,
lease return, normal HLOG return and stock recovery. READ_OK must include claimed1/issued1/
restored1/data_valid1, fresh successful OCS, expected4096-byte CRC, final shared state2 and
loader_io_allowed1. Diagnostic refusal/completed error is preserved but not a block-read PASS.

Only CRC32 of the kernel data is logged; it cannot establish full SHA256 or mathematical
byte-for-byte equality. No mounted filesystem, persistent block driver, throughput, link mode
or fix of713's recovery incident follows. On uncertainty, preserve evidence and recover stock;
never loosen whitelist, modify clocks/reset or automatically repeat a trial.
