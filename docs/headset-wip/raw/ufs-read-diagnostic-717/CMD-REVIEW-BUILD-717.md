# REVIEW-BUILD-717: host only

P=/home/lizi/IOS/hakios-mtk; R=P/logs/ufs-read-diag-20261117; X=R/kernel-src.
Read DESIGN-DIAGNOSTIC717.md and HOST-INTEGRATION-MANIFEST.json. Review actual source and
diff against sealed716 at P/logs/ufs-read-path-20261117, never edit either frozen candidate.

## Review

Verify51 manifest files, kernel-inputs.sha2565210 files, and no BUILD before compiling.
Relative716: exactly3 changed kernel source files, zero added/deleted. No CPU probe changes.
Focus on unchanged response acceptance vs the new failure mask, attempted/valid bookkeeping,
and diagnostic CRC after known completion. A rejected packet must still leave data_valid0.
Review full vs partial response printing; OCS!=0 must not be misread as failure-mask0 acceptance.
Review tests for the eight callback-failure positions and1024 DATA failures. Root host counts:
engine32384/contract983/atomic40/guard23 under both ASan+UBSan and MSan; all native rc0,
stderr empty. Independently rerun changed engine checks in your own new private output directory.
Unchanged atomic/guard need no extra repetition without a concrete concern.

Platform real-XNU-header compile is rc0. Private loader47264 bytes SHA256
3c88cd069e000b48d0eac8be9c5199b9395da68355ee4bfeae17b91049e7f566, three native rc0.
Check compile-commands/planned-commands/preprocessed source. Its only source differences are
trial717 cookie and labels. Gate order, READ predecessor, CRC28a2836c and safe halt are unchanged.
No further loader reproduction is needed unless you find a concrete concern. If needed, remap
every source/output first and compile only in a fresh private directory, never replay old -o.

## Build if no concrete blocker

Execute from P: XNU_SRC=absoluteX ./xnu-build.sh -k -j8. Use only X/BUILD, absent at start.
Record actual subprocess rc, complete stdout/stderr, argv and timestamps; do not use a pipeline's
last rc as make rc. Explicitly set KERNEL=X/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837
for tools/make-kernel.sh --judge of the combined log. Only the existing symbol-set allowance
is permitted. Separate native make rc from judge rc and report any new compiler/link error.

Afterward verify all5210 source hashes and no new/deleted files outside BUILD; verify the51
candidate/loader files unchanged. Return kernel size/hash, new response/diagnostic messages,
init/quiesce inclusion and separate rc values. Strings demonstrate inclusion only.
Recheck stock/shared/wrapper and older716 artifacts without writing them.

## Archive and boundaries

Archive new D/docs/headset-wip/raw/ufs-read-diagnostic-717/ with frozen changed sources,
manifest, diff, reviews, test summaries, build output identity, and specific objections.
Do not upload raw GPT or any disk GUID. Preserve716 NOT_PROVEN and all earlier evidence.
No packaging/flash/device query in this command, no production edits/push, no old BUILD/artifact
overwrite, no GPUEB WIP staging, stash/reset/checkout/clean/gc/prune, MMIO expansion,
reset/clock/UIC/watchdog/OEM expdb work. Await separate package/device CMD after your report.
