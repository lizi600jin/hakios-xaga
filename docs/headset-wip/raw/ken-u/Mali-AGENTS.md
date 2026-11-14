# Agent instructions

## Responsibility

Maintain the Mali-G610 kernel driver and its user interface. The Vulkan/Metal user-space stack is
[MaliMetal](https://github.com/Ken-u/MaliMetal); board wiring, boot arguments and the display
integration are in `UniXNU-evb10`.

## Required reading

1. This README and `docs/notes.zh-CN.md` (hardware facts and measured behaviour).
2. [Driver repositories](https://unixnu.com/drivers/repositories), the [component catalog](https://unixnu.com/reference/components) and the [GPU porting guide](https://unixnu.com/porting/gpu).
3. [Device repository](https://unixnu.com/porting/device-repository) and [device tree](https://unixnu.com/porting/device-tree).
4. For kernel work: [source and patches](https://unixnu.com/xnu/source-and-patches) and the [patch reference](https://unixnu.com/xnu/patch-reference).

## Rules

- `mali_uapi.h` is a contract with MaliMetal's `darwin-drm` shim. Change both sides together and keep the structure sizes explicit.
- Never read GPU registers while power domain 12 is off; it can hang the bus. Check the PMU repair status first.
- Inline assembly targets the Darwin assembler: separate instructions with `"\n\t"`, never `;`.
- Do not commit the firmware or the generated `.inc` files.
- Keep user-visible boot arguments documented at their `PE_parse_boot_argn` call.
- Report commands, source revisions and results; keep hardware observations separate from host or build results.
