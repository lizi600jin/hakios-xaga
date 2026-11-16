# Trial710: register configuration and shutdown criteria

The experiment uses the already audited profile4 kernel, actual vendor-applied trim
L=3/0 and R=17/3, fixed codec-local 48k SGEN, hifi=0, gain31. It does not change gain,
run a regulator experiment, configure the SoC AFE/PCM/DMA, or establish acoustic output.
The phone remains exclusively operated by DSH for flashing, collection and stock recovery.

## Evidence and identity

Preserve a bounded copy of the previous dedicated xnusys logbook region BEFORE reboot.
Do not wipe the region or the partition: freshness is established by the new trial arguments
and validated header/body, not by erasing data. The loader will perform its normal log writes.
The current dedicated region is expected to start at relative offset 0x9f8f8000, the final
8 MiB of xnusys. Confirm the real partition size and compare the computed start before reads.
Never write outside that region. No manual xnusys write is authorized in this experiment.

Preserve the raw header/window and decoded body without filtering or rewriting bytes.
HLOG must have magic HLOG, version1, 0 < length <=4 MiB, sufficient captured bytes and
an independently recomputed matching additive checksum. reason0 is normal kernel return;
boot-note/checkpoint/exception/rescue records must be labeled separately.
A matching checksum alone does not establish trial identity or successful shutdown.

The log must show trial710, headset=4 and hp_trim_l=3/hp_trim_r=17/hp_fine_l=0/hp_fine_r=3
in the actual boot arguments, plus the profile4 caller-supplied trim line L=3/0 R=17/3.
No behavioral argument may be duplicated or truncated. Preserve full boot-argument lines.
The approved flashed image and its nested kernel must match the independently reviewed hashes.

## Up configuration

Require an actual SID5 chip ID 0x68 and VAUD18 bit0=1 in this boot, plus classification
`headphone without mic`. Prior Android playback or earlier XNU values are not substitutes.
Do not bypass a failed precondition or reuse synthetic trim if profile4 refuses the capture.

Require `profile4 captured 38 field groups`, exactly one complete successful on-readback
set for the 38 managed SID/register pairs, and
`profile4 codec configuration verified; muted SGEN, physical output unmeasured`.
For every on line, recompute `(read & check) == expected` and require `verified`.
Check the exact register set/masks against the frozen, hash-verified profile4 source.
Explicit critical on fields include:

- SID9 079e mask04=04; non-owned bits must match the restored readback.
- SID5 238e mask01=01; 2389 mask01=01; 23c8 ownedfc=80.
- SID5 258e=18 and 258f=8b, exact bytes derived from the captured trim.
- SID5 260b/260c mask1f=1f; 2588=ff, 258a=03, 2589=3a.
- SID5 230c mask66=00; 2537 mask01=01; 2388 mask01=01; 2488=01.

`verified` in these logs means readback agreement with the driver's defined masks.
It does not prove oscillator activity, electrical necessity, DC offset or audible output.

## Down and restoration

Require an HP-off-shape line with 2588=00, 258a=00, (2589&0f)=0 and (25a2&01)=0,
and `verified`, before upstream-supply closure is accepted.
Require complete off and restored sets, 38 unique managed SID/register pairs each,
with the exact defined/owned/check masks expected for each stage. Recompute every
printed masked comparison. For each pair, non-owned defined bits from on/off reads
must match the final restored read, using the printed keep mask.

Require `profile4 off and 38 original field groups restored: verified`, a verified close
attempt, and `restored 36 configuration bytes: ok` for ACCDET (the known reference count;
if the count differs, inspect every cause rather than silently substituting another number).
No headset failure, PMIF error, failed readback, failed/partial cleanup, retained pending
ownership, unexpected four-pole reclassification, or worker stop timeout may be hidden.
A recovered retry, if any, must be separately reported and does not qualify as a clean run.
Missing/truncated/reordered/duplicated stage evidence is INSUFFICIENT, not a PASS.

## Recovery and result labels

Every outcome requires restoring the stock boot_a image and checking its full-partition
MD5 a63b2e35a7c2522cb24a6a10332b9bc8, slot_a and Android boot_completed=1.
Save available trial logs before initiating stock recovery. Do not wait for boot_completed
before collecting the trial log: the chain-return Android environment may be unhealthy.

Use bounded commands and phase budgets. If Android/fastboot is not reachable or a recovery
command fails/times out, preserve current evidence and stop to report the required physical
recovery. No long power-button hold on a black screen, repeated blind protocol probes,
unbounded polling, watchdog activation or forbidden OEM commands.

Report identity, UP_READBACK, DOWN_RESTORE, normal return and STOCK_RECOVERY separately.
Do not label a precondition refusal as a successful profile4 run. Do not infer sound,
electrical silence, true clock waveforms, calibration quality, or complete analog behavior.
Do not renumber/rejudge historical 705/707 failures or 709's insufficient source chain.
