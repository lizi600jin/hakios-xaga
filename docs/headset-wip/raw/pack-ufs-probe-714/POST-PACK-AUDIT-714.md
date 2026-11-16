# UFS714 post-package audit — Codex, 2026-11-17

## Host artifact verdict

Strict structural/provenance validation passed. No device execution has occurred.

- boot.img22994944 B
  SHA25691122cdfc72ce3d204bb6ffdd71c1bca7c470e525d28cfdd7ce19bb014231f07
  MD5c230bf743ce03e8799e404b593c2b7f7
- kernel15321328 B
  SHA256ecdc9194fbcc635a63d8da6be7c5f4ef6a1c6bc7f3c1b715c9373cca9fe7da38
- loader48768 B
  SHA2560623637e7d34256a5163e6d886bd7b2f0f0e3e545d9b78ed68b509590384c89a
- payload19515784 B
  SHA256c7d73935a1338a3e75cba6d01da536e7352c0a73dd2a51c18ae94239b1fe97c3
- args base479/runtime539/withNUL540, including only the UFS probe.
- Original source, old713 image/payload and production guards remain the reviewed identities.
- Actual714 objects and loader match an independent recompilation from the frozen source mirror.

Reports: independent-kernel-review.json, independent-package-review.json,
independent-loader-review.json, independent-signature-review.json.
A host PASS is not a UFS runtime, storage-read, filesystem or unattended-recovery result.

## Signature correction affecting the earlier713 documentation

Header v4's signature_size is at byte1580; header_size is1584. Reading the word at1584 reads
padding, which is0 in these images. The earlier713 statement "signature_size0, no signature"
was wrong and needs a retained-history correction.

Stock,713 and714 all declare4096 signature bytes:
- stock offset17190912;
- both experimental images offset22990848;
- all begin AVB0 and have identical bytes,
  SHA256692c87adcc6ad5fcf0592878a6fe56f13e0da9dfade792f1e8aa1f03e34623c5.

The independent structural validators already read byte1580 and compared these4096 bytes.
This checks byte preservation, not cryptographic validation of a modified kernel.
Reference layout:
https://android.googlesource.com/platform/system/tools/mkbootimg/+/refs/heads/main/include/bootimg/bootimg.h

## Packaging incident and provenance

DSH reported that its first714 compiler replay left relative output paths pointing at713 and
overwrote713's start.o, mtkloader.o and loader.bin. This happened during host packaging after
the device had already been restored stock; it cannot explain the preceding recovery incident.

The original713 final boot.img127e48e9 and payload871508dc were not overwritten.
The713 loader was regenerated with its original recorded commands and matches its original
reviewed hash78611b0f. Codex independently checked those three final identities again.

The two original713 .o files had no recorded hashes. Their historical byte identity cannot be
re-established merely by saying the replay succeeded. Keep this provenance limitation.
No device/image identity conclusion depends on claiming those intermediates stayed untouched.

## Concrete prevention used by the independent audit

reproduce-loader-safe.py parses path-bearing arguments instead of performing one blanket
substring replacement. It rewrites compiler sources, BOARD_HEADER, linker script/objects and
all three -o outputs to a private temporary directory, then audits ALL outputs before running
any command. Its fresh loader matches714 byte-for-byte.

Actual714 object hashes, now recorded and reproduced:
- start.o e42598afd6c57632fa396ff21ef6961a9feb73b21bc560110add8e4fa84000e3
- mtkloader.o 5c69cbc49a351689f569888427e0252d72622e7b706c0bbe2e7b69bc05dd7efa

Future packaging must resolve both relative and absolute paths and validate the complete output
set before the first compiler is launched. Do not replay a recorded output destination blindly.

## Next work and operational hold

The user is resting and the phone is stock. This run remains host-only; there is no DEVICE714 CMD.
Before selecting a later device baseline, read RETURN-ANDROID-FOLLOWUP-713.md from the CPU log
directory and review warm return/recovery plus the retained experimental GPU startup.
Do not assume this image's existence settles GPU/recovery behavior.

CPU713's raw LUTs and the UFS diagnostic candidate are concrete progress, not a complete CPU
governor or kernel storage driver. No frequency, voltage, clock, SCSI or queue write has been
introduced by either probe.
