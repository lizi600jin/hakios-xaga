# UFS715: host review and next-stage choice

2026-11-17, Codex. HOST ONLY. No DEVICE command has been issued for714 or715.

CPU713 captured three domains and96 paired raw LUT rows, with a valid HLOG and reason0.
Its later Android-side reboot command timed out; the user manually entered fastboot before
DSH restored and verified stock. This was not unattended recovery success.
The current phone remains stock; no device commands followed that recovery in this host phase.

## Why a second host package

UFS714 intentionally retained713's experimental GPU parameters. It remains preserved and
unflashed. UFS715 uses the identical UFS probe kernel and byte-identical payload, with:

- Runtime `mali=0`, removing the other Mali experiment arguments except the715 identity label.
- Loader `BOARD_MALI_POWER`, `PREPOWER`, `EARLY`, `RETURN`, `STOP` all explicitly0.
- The same four CPUs,150-second return setting, watcher0 and `headset=0`.

The runtime init returns immediately for `mali=0` before creating its worker. The loader
preprocessor output has no `mali_leave_powered` or `mali_power_down` references with these
five macros0. This removes those experimental software paths; it does not establish the
physical GPU state left by LK or fix the unproven cause of713's warm-return incident.

The full handover review includes the loader cleanup call guarded by BOARD_MALI_POWER.
The kernel's lack of a named Mali610 quiesce hook does not establish absence of GPU cleanup.
See the appended correction in `../mt6895-cpu-baseline-20261116/RETURN-ANDROID-FOLLOWUP-713.md`.

## Artifact identity

All paths below are under `logs/ufs-probe-20261117/trial715/`.

| Artifact | Bytes | SHA256 |
| --- | ---: | --- |
| boot.img | 22994944 | 65bd797dce99a4f29d898c7055a25fcce207e1b3642109daae0ad217f653c994 |
| loader.bin | 46504 | c3d9c30a0c14c3e6c644fbf499fc189819b818c03080fe2beee18590cf7eafea |
| payload.bin | 19515784 | c7d73935a1338a3e75cba6d01da536e7352c0a73dd2a51c18ae94239b1fe97c3 |
| nested kernel | 15321328 | ecdc9194fbcc635a63d8da6be7c5f4ef6a1c6bc7f3c1b715c9373cca9fe7da38 |

Boot MD5: `dced496c752ae0bec3de806b94a81615`.
Probe source remains8138 bytes, SHA256
`ce6aa5850fba8d318e44cdb18e2f3c9592820a12e27c1a1e3847cede730045f2`.
No new kernel build or probe source change was needed for715.

## Independently checked by Codex

1. Strict unpack passed: unchanged stock Android Image and ramdisk, only documented header
   patches, exact payload match, three XNUP entries plus the mandatory24-byte zero terminator
   and exact EOF. The nested kernel has the approved UFS hash.
2. The stock v4 signature_size field at1580 is4096. Those signature bytes are preserved;
   preservation is not proof of a valid signature for the changed kernel. The earlier
   claim that signature_size was0 read padding at1584 and was corrected separately.
3. Base arguments169 bytes, runtime229, withNUL230<=608; the exact NUL-terminated base body
   occurs once. No CPU-frequency, headset-output or extra GPU experiment argument remains.
4. Recorded compile/link/payload/chainboot native return codes are all0. Loader compilation
   retains nine warnings in the frozen sources; this is not a zero-warning build.
5. An independent private-directory compile reproduced both objects and loader byte for byte.
   The frozen loader source mirror matches713. Every output is audited before compilation;
   the old713/714 directories are not replay targets.
6. The five explicit macro values were checked in both compile argv and preprocessor output.
   Runtime early-return source was inspected at hakios_mali610_init.

Evidence: `independent-package-review-715.json`, `independent-loader-review-715.json`,
`trial715/macro-review.json`, `trial715/compile-commands.json`, `arguments715.json`.
The probe's prior2324 sanitizer cases and32 supplementary formatted slot-count assertions
remain applicable to the unchanged kernel source;715 did not rerun a different probe model.

## What remains unverified

Neither714 nor715 has run on hardware. Mapping may fault; callback failure is not abort
recovery. Read-only source does not guarantee that hardware reads have no side effects.
No queue ownership, valid DMA addresses, actual storage I/O, link mode, gear/lane state,
clock or power state is established. FLAGS_OBSERVED would report only the chosen flags.
UCRDY is report-only in this probe, not a universal claim about every vendor driver's checks.
There is no XNU UFS storage driver here; this is a finite snapshot probe.

## Selected next step

I prefer715 as the future storage diagnostic candidate because it removes unrelated GPU
experiment paths. It intentionally changes that baseline and does not isolate the cause
of713's return incident.714 remains the preserved prior-parameter comparison.

Finish an independent DSH host review and archive these exact artifacts and corrections.
Stop device work for this run. A later hardware trial needs its own reviewed DEVICE plan,
including the known manual-recovery dependency; no automatic retry or fallback writes.
Do not add UIC commands, SCSI, clocks, reset, syscon reads or queue takeover to this probe.

No stash/reset/checkout/clean/gc/prune, no production push or GPUEB changes, no old BUILD
or sealed artifact overwrite, and no direct device commands by Codex.
