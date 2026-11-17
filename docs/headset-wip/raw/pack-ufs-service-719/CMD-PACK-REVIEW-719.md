# CMD-PACK-REVIEW-719

Host only. Independently inspect the newly created trial719 package. Do not rebuild,
repackage, edit frozen inputs, query a device, or launch a trial. Keep718 sealed.

R=/home/lizi/IOS/hakios-mtk/logs/ufs-service-20261117
D=/home/lizi/IOS/repo/hakios-mt6895

## Identities

- trial719/boot.img23003136 B SHA256 fd6e19ac8386804ac697564a98a1bce62b697b8fbaac2821b6033bf32f4d5862
  MD5 d54237475b4e1633f9c88720a3fb2b2e
- trial719/loader.bin47952 B SHA256 9d2c5fb9fe534a153846f4e794da68dea8399ae03ae20371b51ad3f8b5da84cb
- trial719/payload.bin19534232 B SHA256 c6e9b51e47a89ef83227ab9ce977040a34e24c4e1e32753e4f07c6a1d5b2c172
- nested kernel15339768 B SHA256 5a85ac7fed4392b1a87a9cb4f91f592fd9697cafbb572fe4d1041794c991494f

## Review

1. Read package-719.py and review-package-719.py. Run the pure read-only verifier with
   output in a NEW host-review-mine719 path; do not overwrite independent-package-review-719.json.
2. Independently confirm stock header differences only kernel_size bytes8..11; signature_size
   at1580 is4096 and original signature/ramdisk unchanged byte-for-byte; embedded frozen
   Android Image unchanged; loader only documented10 header bytes patched; all padding zero.
   Preservation is not cryptographic verification of the stock signature.
3. Validate nested XNUP kernel/ramdisk/version,24-byte terminator and exact EOF. Locate actual
   NUL-terminated171-byte base args in the actual loader exactly once, not printf copies.
   Full runtime231 bytes,232 with NUL. Service1,trial719,headset0,mali0,cpus4,return150;
   old read/probe/cpu/tone/gpueb arguments absent. Legacy dirty version is metadata only.
4. Read independent-kernel-review-719.json (5214/57 checked; make2 and explicit judge0)
   and independent-loader-review-719.json (all40 source files checked, private native3rc0,
   byte-exact newloader reproduction, actual first gate and two-span wiring checked).
   No extra loader reproduction needed absent a concrete concern. This719 loader is a
   real gate/audit change, not merely a cookie change; do not reuse718's old equivalence claim.
5. Keep sealed718 image/kernel and frozen57 inputs unchanged. No edits to service/state/engine,
   whitelist/READ geometry/reference/timeout, no expansion of device scope.

## Archive and report

Create D/docs/headset-wip/raw/pack-ufs-service-719/ containing thisCMD, concise review,
the three independent reviews, review scripts and your independent result, and textual
artifact records. Image binaries/raw GPT/GUID/pre-log remain local. Only add exact new
documentation paths; GPUEB WIP must remain unstaged. Preserve production WIP and no
stash/reset/checkout/clean/gc/prune or production push. Commit and push docs only.

Report any blocker with concrete source/hash/field evidence. Successful host verification
is not runtime registration/VFS/lease evidence. Wait for a separate ONE DEVICE719 release.
