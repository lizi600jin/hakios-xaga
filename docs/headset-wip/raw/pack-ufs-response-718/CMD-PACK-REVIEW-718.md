# PACK-REVIEW-718: host only

P=/home/lizi/IOS/hakios-mtk; R=P/logs/ufs-response-20261117. Review the already packaged image;
do not rebuild/repackage. Read DESIGN-RESPONSE718.md and independent kernel/loader/package reviews.

| Artifact | Bytes | SHA256 |
|---|---:|---|
| trial718/boot.img | 22999040 | 3a3ceb1d77c9a61f289695879c077baf7ec076a8554c936aec8110afd1e61970 |
| trial718/loader.bin | 47264 | dc1d273481eeab99086726e90762c4dca4a30e7d631fddab31990349ce6e4b25 |
| trial718/payload.bin | 19533320 | a43093c1479e34debc82ebca787a3c59735aa79279a3f5ec78b686183b673aba |
| nested kernel | 15338864 | 60bd6b195c2ab65ab0c5c1cb519fe3cf5af9ad64c351485ccbf59aa899fd7cfe |

Image MD542beb68e83eabea8a281965888380417. Native mkpayload/chainboot rc0/0.
Actual loader argument body is uniquely NUL terminated, base168/runtime228/withNUL229;
trial718, mtk_ufs_read1, mali0, headset0, cpus4, return150, lease1, GPU macros0, watcher0.

Independently run review-package-718.py into your own new private output, preserving Codex's
JSON. Verify stock header only kernel_size differs, unchanged Android Image/ramdisk,4096-byte
signature preserved (not cryptographic validity), documented10 loader header bytes only,
zero padding, nested kernel/reference ramdisk/kernel.ver,24-byte XNUP terminator and exact EOF.
Check actual unique argument body/lengths/key uniqueness and structured native package argv.

This NEW revision intentionally changes response acceptance: segment0-only becomes0-or20
with bounded sense<=18, plus new EHS0 restriction. Do not call its response predicate unchanged;
the command whitelist/READ geometry/lease/restore/timeout are unchanged.716/717 remain NOT_PROVEN.
Full response is13 words/valid1fff. Failure-mask0 establishes acceptance only with full valid
AND sampled OCS0. Even a full response with OCS!=0 has an unevaluated zero failure mask.
Sense bytes are exposed raw; SCSI status is the command-status criterion. No new storage claim
before the separate718 hardware evidence.58 synthetic parser cases are host checks only.

No extra loader reproduction absent concrete concern. Recheck sealed716/717 artifacts and
frozen sources unchanged. Archive NEW D/docs/headset-wip/raw/pack-ufs-response-718/ with CMD,
identities, native argv/logs, independent reviews/limits; binary images and raw GPT stay local.
Return blockers and commit. No device query/flash, source edit, production push, old overwrite,
GPUEB WIP staging, stash/reset/checkout/clean/gc/prune, clock/reset/UIC/watchdog/OEM expdb.
Wait for a separate explicit ONE DEVICE718 release after successful independent package review.
