# Mali

Mali-G610 (CSF, Valhall, arch 10.8) kernel driver for UniXNU on RK3588. It powers the GPU,
loads the CSF firmware into the MCU address space, builds AArch64 4 KiB GPU page tables and
command-stream groups, and exposes a Panthor-style `IOUserClient` that the PanVK-based Metal
stack in [MaliMetal](https://github.com/Ken-u/MaliMetal) drives.

| Path | Contents |
| --- | --- |
| `Mali.cpp` | Power domain, clocks, reset, MCU/firmware, MMU and page tables (4 KiB pages, 2 MiB blocks from a reserved pool), command-stream groups, tiler heaps, syncobj/fence wait, buffer objects, `MaliService` and `MaliClient` |
| `mali_uapi.h` | The kernel interface shared with the user-space libdrm stand-in (selectors, structures, bind flags) |
| `mali_csffw.inc` | **Generated, not committed.** The firmware as a C array: `tools/gen-fw-inc.py` |
| `gpu_files.inc` | **Generated, not committed, optional.** Owner test binaries from MaliMetal's `usertest/build.sh` |
| `shader/store.asm` | The Valhall shader behind the M2b store test |
| `docs/notes.zh-CN.md` | Bring-up log in Chinese: hardware facts, milestones M0 to M4, measurements |

## Build

The driver is one translation unit compiled against the XNU tree. `UniXNU-evb10` carries it as `package/Mali`, and its
`scripts/build-evb10-drivers.py Mali` produces `build/mali/libMali.a`, which
`CONFIG_XNU_EXTRA_LD_ARCHIVES` force-loads. The board's display probe calls
`mali_start()` when the boot argument `mali=N` is set.

```sh
MALI_CSFFW=/path/to/mali_csffw.bin tools/gen-fw-inc.py   # writes mali_csffw.inc
```

The firmware (`mali_csffw.bin`, 274432 bytes, sha256
`8e7c821a55ca1c345cc79f61243616dd25fc35b81f090946f302886d67712df5`) is a vendor binary and is
listed in the device repository's `proprietary-files.txt`.

## Boot arguments

`mali=N` selects how far the bring-up runs: 1 power and GPU_ID, 2 firmware and MCU, 3 command
stream, 6 and 7 several groups, 8 the IOKit service with its user client. Further arguments tune
behaviour and experiments: `mali-metal`, `mali-plugin`, `mali-plugin-grant` (which processes may
open the user client), `mali-pll`, `mali-div`, `mali-npll` (the GPU clock), `mali-stacks`, `mali-l2`, `mali-leaf`, `mali-sample`,
`mali-skipslot`, `mali-postflush`, `mali-grant`, `mali-blocks`, `mali-contig`, `mali-pool`.
Each one is documented at its `PE_parse_boot_argn` call in `Mali.cpp`.

## State

Verified on the RK3588 EVB10 V10 (see its porting record): the driver brings the GPU up, runs
compute and fragment jobs, and composites the iPadOS lock screen through PanVK and
metal2vulkan. The GPU clock is fixed for the whole run (198 MHz unless `mali-pll` / `mali-div` / `mali-npll` pick another source, divider
or NPLL rate; 850 and 1000 MHz were run for the frame-rate work, which needs the GPU rail at 0.85 V from the UEFI); there is no DVFS and no interrupt-driven
completion yet (waits poll). GPU interrupts, error reporting to user space and more than eight
groups are open items.
