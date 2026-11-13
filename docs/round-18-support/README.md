# Round 18 support: offline firmware analysis

Round 18 produced **no hardware results**. Trial 693 was not run: the executor rejected the
`skipprot` criterion before any flashing, and the round pivoted to offline analysis of the
firmware image that already ships in `drivers/gpu/hakios.mali610_fw.h`.

Everything here is derived from that 258048-byte image and from logs already published in
`docs/round-8-support/`, `docs/round-17-support/` and `docs/round-6-support/`.
No adb, no fastboot, no MMIO, no driver change.

## What to read first

| File | Why |
|---|---|
| `COLLAB-PROTOCOL.md` | **Appendices A–D are new.** Five classes of methodological error made this round, each with a mandatory rule. Written from 12 concrete mistakes. |
| `HANDOFF-TO-CODEX-2026-11-14-0030.md` | Newest handoff. Splits findings into "confirmed", "not proven" and "closed". |
| `PUBLIC-REF-CROSSCHECK-2026-11-13.md` | What three public references say, compared item by item against our firmware. |
| `PDC-FINDING-2026-11-13.md` | PDC enable writes vs readback, including three self-corrections. |
| `FINDING-version-word-path-2026-11-13.md` | Where the firmware publishes its interface version, and why the sentinel staying put is not proof that the MCU never ran. |
| `INDEX-2026-11-13.md` | Document map plus the running list of refuted claims. |

## Scripts

All are read-only and re-runnable. `extract_fw.py` regenerates `mali_csffw.bin` from the C
header; that binary is deliberately **not** committed, because it is 258 KB regenerated from a
file already in the tree.

| Script | Purpose |
|---|---|
| `extract_fw.py` | Rebuild the firmware binary from `hakios.mali610_fw.h`, verify SHA256. |
| `parse_fw.py` / `iface.py` | Decode the entry table: interface, configuration, trace-buffer, metadata. |
| `mpu_unicorn.py` | **Emulate** the MPU setup at `0x1003126` with unicorn and dump the 13 regions actually programmed. This is emulation output, not a hardware readback. |
| `sweep.py` / `rec.py` | Literal-load and reachability sweeps over the code section. |
| `dis1.py` / `dis2.py` / `dis3.py` / `g2.py` | Targeted disassembly of the reset entry, the version publish, and the MMIO writers. |
| `mmio.py` | Enumerate MMIO reads and writes the firmware itself performs. |
| `cover.py` | Disassembly coverage check — how much of the code section actually got decoded. |

## What this round did and did not establish

Confirmed:
- The firmware publishes `0x01010000` at `0x04000000`. Per panthor's
  `CSF_IFACE_VERSION_MAJOR/MINOR/PATCH`, that is interface version **1.1.0**, and it matches
  the minimum version panthor accepts. The RK3588 build of the same G610 reports 1.1.1.
- `0x04000000` sits in an MPU region marked Non-cacheable, so that store does not stop in the
  MCU's own data cache.

Not established:
- That the MCU never executed. A store that reaches the bus can still fail translation, and a
  private fault would land in `SCB->CFSR`, which nothing here reads. That last point is itself
  unproven and must not be quoted as fact.

Closed (do not re-open without new evidence):
- `skipprot` — the firmware entry branches on a bit that reads 0, so the mapping change cannot
  affect the boot path.
- A full `fwlog` channel — entering the writer reads the MMIO cycle counter and waits for
  consistency, which changes the thing being measured.
- The `0x202bbe0` probe — it lives in a write-back region with the D-cache enabled.
