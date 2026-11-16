# Return-to-Android follow-up after713 — host findings only
2026-11-17, Codex. No device commands were run for this analysis.

## Observed incident

CPU713's original HLOG was captured with reason0 and a valid full checksum.
Android-side adbd was usable for that capture. The later adb reboot bootloader timed out
and fastboot did not appear within the bounded attempt. The user manually entered fastboot;
DSH then flashed the verified stock boot_a and verified full MD5, boot completion and UI service.

Do not label the whole run an unattended recovery success.
The cause of the Android-side stall/reboot timeout is not established.
Read-only probe source does not itself prove that no hardware side effects can occur on a bus read,
and the incident must not be attributed to or excluded from CPU probing without evidence.

## Relevant source facts

P/mtk/loader/mtkloader.c:
- reset_board invokes psci_system_reset and wdt_reset.
- The BOARD_RETURN_TO_ANDROID path in xnu_returned instead calls chain_android.
- chain_android copies and jumps to the retained stock Android Image with LK's DT.
  This is a warm handover, not evidence of a cold hardware reset.

P/xnu/files/osfmk/arm64/loader_return.c:
- loader_return stops other CPUs and calls the MT6895 headset/legacy storage/Adreno/etc quiesce hooks.
- No hakios_mali610 stop/quiesce hook was found in this path.
- The current Mali-G610 source defines its startup worker but has no matching named stop/quiesce API.

P/mtk/mt6895/drivers/gpu/hakios.mali610.c:
- gMaliTrial is a numeric experiment label; its occurrences inspected here are logging/argument
  parsing, not a trial713-specific hardware branch.
- CPU713 retained mali=5 and the prior GPU experiment parameters. It was not a CPU-only machine
  configuration, even though the newly added probe only read CPU performance-domain words.

These facts identify a handover review area. They do not prove that Mali caused the observed stall.
The worker's actual exit and the hardware state it left require a complete trace before any fix.

## What to do before another unattended device run

1. Preserve the recovery incident and full evidence next to the successful CPU capture.
2. Review whether future CPU/storage diagnostics should use a GPU-disabled baseline rather
   than carrying the existing experimental Mali startup. That would be an intentional new
   comparison condition and must get a distinct artifact identity and review.
3. Confirm all loader prepower/macro paths before assuming mali=0 alone removes GPU activity.
   Prepower was0 in713; BOARD_MALI_POWER was still compiled, so inspect actual guarded calls.
4. Do not invent a hardware shutdown sequence, reset offsets or watchdog fallback.
   A missing quiesce hook does not justify guessed power-domain/PLL/PMIC stores.
5. A later real storage driver must also return queue ownership and DMA state to the loader
   before the loader writes HLOG. That requires a separately tested stop/reclaim design.

## Current operational boundary

The device is recovered stock and will not be touched again in this run.
UFS714 can be built and packaged on the host as a review artifact; it has no DEVICE command.
Its current package intentionally preserves713's GPU parameters for provenance. This does not
settle the next hardware baseline or release it for unattended execution.

Host work may continue on driver semantics, models, packaging and evidence.
No renewed user confirmation is being requested here; this is the selected host-only scope
after the actual recovery incident.
