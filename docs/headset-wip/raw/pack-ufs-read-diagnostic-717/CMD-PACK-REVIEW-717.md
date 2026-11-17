# PACK-REVIEW-717: host only

After completing REVIEW-BUILD-717 and reporting any concrete blocker, independently review
the already packaged717. Do not rebuild or repackage. P=/home/lizi/IOS/hakios-mtk;
R=P/logs/ufs-read-diag-20261117. Read independent-kernel-review-717.json,
independent-loader-review-717.json, package-717.py and review-package-717.py.

## Frozen identities

| Artifact | Bytes | SHA256 |
|---|---:|---|
| trial717/boot.img | 22999040 | c275625ff431ee93b8fdc87284c1336bd631d7721af13415deba124392496699 |
| trial717/loader.bin | 47264 | 3c88cd069e000b48d0eac8be9c5199b9395da68355ee4bfeae17b91049e7f566 |
| trial717/payload.bin | 19533320 | bb40824042997ba968afb5ad3be162f67e48df4d23ac5565d6ed0e32b216f49c |
| Nested kernel | 15338864 | 07b016feb866c4347d72a3915ef2dce15045bfa2d0628d3d1f9eb5ff385506d6 |

Image MD5 69dea280d97bdca4267c114f1ca537b8. mkpayload/chainboot native rc0/0.
Arguments base168/runtime228/withNUL229; unique NUL body. mali_trial717, mtk_ufs_read1,
headset0, mali0, cpus4, loader_return_after150; no CPU/UFS probe, headset output or GPU options.
Five BOARD_MALI macros remain0, BOARD_UFS_READ_LEASE1, watcher0; no watchdog changes.

## Independent checks

Run the read-only review-package-717.py on trial717 to your own fresh private JSON output,
not Codex's independent-package-review-717.json. Read/check the native package argv and source.
Check stock header diff only kernel_size; Android Image and ramdisk unchanged; v4 signature
size at1580 is4096 and bytes preserved (not new cryptographic validity); loader differs only
at its documented header patches; all padding, nested kernel, ramdisk/kernel.ver and24-byte
XNUP terminator/exact EOF. Verify argument uniqueness/lengths using the actual loader body.

Only the trial cookie and diagnostic observations change relative716. Whitelist, response
acceptance, restore, timeout and guarded lease remain unchanged. diagnostic-only CRC is never
an accepted block result when response failure bits are nonzero. Review the new parser/test
as evidence tooling if helpful:49 synthetic cases passed, not hardware evidence.

No additional native loader reproduction absent a concrete concern; source equivalence and
native compile were already reviewed. Verify artifacts and sealed716 engine/image not changed.
Archive new D/docs/headset-wip/raw/pack-ufs-read-diagnostic-717/ with identities, review results,
package argv and limits; image binaries and raw GPT remain local. Return blockers and commit.

No device query/flash, source edits, overwrite, production push, extra experiment, GPUEB WIP
staging, stash/reset/checkout/clean/gc/prune, controller/clock/reset/UIC/watchdog work.
Await a separate explicit DEVICE717 release; this CMD authorizes host review only.
