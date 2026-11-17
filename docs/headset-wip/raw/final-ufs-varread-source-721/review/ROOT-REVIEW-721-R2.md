# Trial721 revision2 root review

Status: HOST_REVIEW_PASS. Device execution remains unauthorized. The user ended this round at host verification and repository delivery; no device query, flash, reboot or capture was performed.

The original 721-D images are preserved. The sole approved old-document exception redacted a serial literal on line 8 of DEVICE-PLAN-721.md; its old/new hashes are recorded without the value. Existing production and documentation WIP fingerprints are unchanged.

## Source and native kernel evidence

All 5227 current kernel inputs match the original frozen input manifest. R20's 5220 actual inputs remain unchanged in their original tree. Comparing R21 against R20 gives 5217 byte-identical entries, 3 changed integration bindings, 7 added runtime files and 0 removed entries. The old bsd/dev/support files remain R20 bytes; the new runtime is isolated under bsd/dev/ufs_varread/.

Codec SHA256: 6c7e531ec4b52f90a7a75a454fbb614263ed9b88b6ac74d4f783cdccb83d8098.
Engine SHA256: 19c2b8d0204693ed7181dafa6a4b64683fceaae7b682477cef1b2b4aa5fc79e1.
Provider SHA256: ec7ed55749da6c8e0d035967d7a8dc2ebb4c45062192fa16693dd2110ad61a5f. Its sole approved difference from reviewed adapter v2 confines the publication observer declaration and update to US_HOST_TEST. The production object has no observer symbol and no undefined reference helper.

The kernel (15339856 bytes, SHA256 784500d791bf2a623d24ad81ed460b4c0cdf25143f8beade8a2d4729eed342cb) and provider object (75072 bytes, SHA256 d67d9243efd85a5b78fe5284db07b4ff01e9259b11c3384ff05fb46dc49e07b6) are reused from the original 721-D wrapper build. Neither was rebuilt in the H1/H2 correction rounds.

Native make rc remains 2. The old judge rc 0 is invalid because its log path did not exist. The original judge on real stderr returns 1 after misclassifying two localized unfinished-job notices. Root manually classified all original failure lines: the sole leaf is check_all_exports, with only its expected ancestor propagation. The replacement private judge pins complete stdout/stderr/kernel hashes and reads the real native rc file. Root independently checked one genuine frozen positive and nine negative cases; DSH recorded one positive and fifteen negatives. The corrected private judge rc is 0, separately from native make rc 2. No production judge was edited.

## Loader and package

The sole functional loader source correction changes the return audit prefix from mt6895-ufs-capacity to mt6895-ufs-varread. The private board header is byte-identical to the reviewed R20 header. Corrected argv use existing private input paths, preserve -Os and write into a fresh build directory.

Root independently replayed both clang commands and ld.lld, changing output paths only. All three native rc values are 0. The resulting start.o, mtkloader.o and loader.bin are byte-identical to DSH outputs. The loader contains exactly one varread return-gate prefix and no capacity return-gate prefix.

| artifact | bytes | SHA256 |
|---|---:|---|
| boot.img | 23003136 | 498a99a40094c1f941ce598e061b1eb599690acf531eca25e402b270acf6472e |
| loader.bin | 48152 | 04b36c195f25781ca9265a5ee0ecf56f1706d6f11524619fb61b5f35a0fd9b31 |
| payload.bin | 19534312 | 9d8880fe1b134dda8412568047ba1816535b92f61e2fa8783eaf10ac05e747b7 |

Root used the independent structural verifier to check the Android v4 layout, unchanged stock header except kernel_size, stock Android Image/ramdisk/signature, alignment and padding, no trailers, XNUP ordering/termination and nested kernel identity. The payload remains at 0x287f120; the loader differs from its packaged form only in the documented ten patched bytes. The payload is byte-identical to the old payload and is an independent file inode. Full boot arguments reconstruct from the nested version and remain 239 base bytes / 299 full bytes / 300 including NUL, within the 608-byte limit. The r2 package script was separately rerun: rc 0, **50 actual PASS checks** (the original 46 plus two kernel-identity checks and two audit-prefix checks).

The final local manifest verifies all 181 named files. It references the original 1071605-byte input manifest by full SHA256 1bd8a1f3e5dad675e87785da5efcf95565485d841657671f6f02762094f67882 rather than claiming the small r2 summary contains the full input lists.

## Synthetic evidence and device hold

The first replacement parser was rejected after independent false-positive probes and is disabled. The existing reviewed HLOG parser is unchanged. An unmarked synthetic fixture that incorrectly received hardware PASS fields was retained only as rejected local evidence. The corrected fixture contains the exact SYNTHETIC TRIAL721 HOST FORMAT FIXTURE marker and the real formatter's audit output. Root invoked the actual parser CLI: rc 1, status FORMAT_PASS_ONLY, NORMAL_RETURN NOT_HARDWARE_EVIDENCE, and no top-level runtime/lease/reference hardware PASS fields. That CLI rc is expected for synthetic-only evidence.

The device plan is an unexecuted draft, not a device release. It now distinguishes an experimental boot_a flash from the required stock restoration, including restoration on success. Loader return does not restore stock. Any future device work still requires a separately reviewed concrete action sequence, confirmed transport/recovery conditions, root authorization and the operator's individual releases. No device result exists for trial721.

LBA binding rests on exact-request readback and source structure, not a wire trace. CRC agreement is a checksum observation, not reference byte equality. This host review establishes no physical DMA freshness, cache exclusion, throughput, gear/lane, block-layer or filesystem capability.
