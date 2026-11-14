# UniXNU-rothko

Device repository for the Xiaomi Redmi K70 Ultra (`rothko`, MediaTek MT6989 / Dimensity 9300).
It follows UniXNU's [Port a device](https://unixnu.com/porting/) and [device repository](https://unixnu.com/porting/device-repository) format 2. The public repository contains the board contract, XNU patch series, board drivers, package kit and verification record. UEFI remains in the separate [`ChallengeThePeak/Silicium-Rothko`](https://github.com/ChallengeThePeak/Silicium-Rothko) repository and is selected through `board.toml`.

The XNU source is maintained separately in
[`woaphone/xnu-mtk`](https://github.com/woaphone/xnu-mtk). The K70U branch is
`rothko` at `fd4edc747395525a914987bb88f063b5e549ecc7`; this device repository
stores the ordered 18-patch series from immutable base
`53bc420e5acda41c1689f6690a85841393a347cf` for reproducible builds.

The port targets the iPad7,1 / iPadOS 17.7.11 / build 21H461 donor used by the established UniXNU sheng/yunluo ports. The board inventory records eight CPUs, a 13 MHz timer, MT6989 GICv3, UFS4 storage, a 1220x2712 panel and Goodix Berlin9916K touch hardware.

## Repository map

- `board.toml` — format 2 board identity, IPSW hashes, GPT/userdata windows, XNU/prelink, external UEFI contract, payload and ADT inputs.
- `feeds.toml` — published MT6989 common SoC source, local UFS driver and xnu-mtk source pins.
- `porting.toml` — official schema version 1 record with configuration, firmware, CPU, memory, storage, display, input, GPU, network, cellular, audio, power, packaging and diagnostics areas.
- `configs/rothko_defconfig` and `patches/xnu/series` — selected kernel configuration and ordered XNU patches.
- `dts/rothko.dts` — reviewable board hardware inventory and profile input. The current ADT generator emits a separate minimal tree from command-line arguments and constants; it does not parse this DTS.
- `drivers/` and `kits/rothko/` — board and device implementations, including MT6989 platform/CPU, UFS with read-only OS/SystemOS and bounded LU2 Data/Log writes, display, HID, Goodix and fixed FDVFS request code.
- `package/` and `scripts/package.sh` — board package and the official `unixnu release` wrapper.
- `docs/verification.md` — compact evidence boundary and reproducibility record.
- `unixnu.lock` — pins the XNU repository, stable branch, immutable patch base and final revision.

Large IPSW, firmware, logs, images and generated output stay outside Git. Their hashes and locations belong in the parent K70U bring-up reports or the local build record.

## Porting workflow

The repository follows the eight official stages:

1. **Collect inputs.** Hardware addresses, partition geometry, panel/touch resources, CPU topology and firmware facts are recorded in `dts/rothko.dts`, `board.toml` and `porting.toml`.
2. **Choose shared support.** The iPadOS profile, ARM64/XNU patch base, `UniXNU-mt6989-common` and reusable catalog components are selected in `board.toml`; the board DTS keeps only device-specific hardware.
3. **Create the device repository.** The format 2 files, device kit, package, patch series, workflows and local override example are tracked here. `example = false` is set.
4. **Resolve and inspect.** Run the commands below from the main UniXNU checkout and review source revisions, overrides and patch order.
5. **Prepare hardware and firmware.** Compare the board DTS inventory with the separately generated XNU ADT. The UEFI repository supplies DarwinBoot, GOP, FAT initrd support and the boot FD.
6. **Adapt drivers in dependency order.** Platform/memory, storage/root, display, input, GPU, network, audio and power each have an area in `porting.toml`; unfinished areas stay `planned`.
7. **Build and start.** Build kernel, prelink, firmware, payload and boot image through the official tools. Device observations are recorded separately from host results.
8. **Complete and publish.** Run the record checker, review the generated diff, publish the repository and add the `unixnu` GitHub topic. The UEFI checkout is required for a complete package build.

## Resolve and inspect

From the main UniXNU checkout:

```sh
./unixnu defconfig rothko_defconfig -D ../UniXNU-rothko
./unixnu fetch -D ../UniXNU-rothko
./unixnu status --json -D ../UniXNU-rothko
./unixnu tree -D ../UniXNU-rothko
python3 scripts/porting-record.py check --device ../UniXNU-rothko
```

The official record checker reports 14 structured areas. This checks record
structure; it does not build or apply the complete selected patch graph.
Shared CPU, PSCI, GICv3 and timer resources resolve from
`woaphone/UniXNU-mt6989-common`.

The 2026-11-07 offline audit found that the OS profile overrides the raw XNU
feed pin with Apple revision `d8b80295118ef25ac3a784134bcf95cd8e88109f`.
The resolved 77-patch graph is not a verified reproduction of the K70U
candidate. The separately verified 18-patch replay starts at `53bc420` and
produces exactly the source tree of `fd4edc7`.

## Build and package

`local.toml` is for source/module overrides. Build options use a separate
`[build]` file passed through `--config`; copy `build.local.toml.example` to
the ignored `build.local.toml` and set the UEFI/IPSW inputs there. Select a
Format 2 main checkout with `UNIXNU_BASE` if the default sibling `../UniXNU`
is an older source without the `release` command.

From this device checkout, inspect the release plan without fetching:

```sh
scripts/package.sh --no-deps --check --os 17 --uefi ../uefi-rothko
```

The wrapper delegates to the main checkout's official `unixnu release` command.
The full release build remains blocked: current driver recipes do not accept
`NativeMtkUfs` or `MtkCpuFreq`, the complete patch graph has not been
qualified, and the main firmware adapter invokes BaseTools directly instead
of the Rothko `DeviceBuild.py`/stuart entry. Earlier isolated bring-up builds
do not validate this new release route. See
[`docs/offline-audit-2026-11-07.md`](docs/offline-audit-2026-11-07.md).
Do not commit proprietary inputs or generated images.

## Evidence state

The device record is deliberately conservative:

- **Observed on the device:** early XNU K1-K4/MMU/serial, eight-core startup/timer/IPI activity, Goodix reports and HID queue activity, XNU UFS HS-G5 dual-lane/LU2 reads and root matching, and the activation page on the unx7 baseline.
- **Implemented but not closed on the device:** UI touch interaction, physical-key wake, long-term display/userland stability and the fixed FDVFS request's effect on perceived lag.
- **Pending:** native GPU, Wi-Fi, Bluetooth, cellular, audio and battery service.
- **Unresolved:** the later UEFI/BROM incident. The record does not claim a proven LUN0 write, preloader overwrite or safe recovery state.

See [`porting.toml`](porting.toml) for the machine-readable record and [`docs/verification.md`](docs/verification.md) for the compact evidence summary.

## References

- [Port a device](https://unixnu.com/porting/)
- [Device repository requirements](https://unixnu.com/porting/device-repository)
- [Porting record format](https://unixnu.com/porting/records)
- [MediaTek XNU source repository](https://github.com/woaphone/xnu-mtk)
- [UFS adaptation](https://unixnu.com/porting/ufs)
- [Bring-up and log capture](https://unixnu.com/porting/bring-up)

