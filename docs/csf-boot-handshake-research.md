# Mali CSF firmware boot handshake — research report

Sources actually read (all local copies under `/tmp/csf/`):

| Tag | Path | Provenance |
|---|---|---|
| **P-master** | `/tmp/csf/panthor_fw.c` (1565 lines), `panthor_fw.h`, `panthor_fw_regs.h`, `panthor_device.h` | torvalds/linux master, `git.kernel.org/.../plain/drivers/gpu/drm/panthor/*` |
| **P-7.0** | `/tmp/csf/linux-source-7.0.0/drivers/gpu/drm/panthor/` (panthor_fw.c 1533 lines) | local linux-source-7.0.0 tree |
| **KB** | `/tmp/csf/kb_rk.c` = rockchip-linux/kernel `develop-5.10` `drivers/gpu/arm/bifrost/csf/mali_kbase_csf_firmware.c` (3496 lines); `/tmp/csf/k_mali_kbase_csf.c` (3449); `/tmp/csf/kb_csf_regs.h`; `/tmp/csf/kb_cfg.c` | vendor DDK r41-ish in rockchip tree |
| **FW** | `/tmp/csf/csf_fw.bin`, 258048 bytes, the same `mali_csffw.bin` | parsed + disassembled by this session |

**P-master and P-7.0 differ** (different IRQ API, different line numbers). I cite both, labelled.

---

## §0 First: your "version word at 0x4002000" premise is off by 0x2000

Parsing the actual image (`/tmp/csf/csf_fw.bin`, entry 9, the `shared` section at VA `0x04000000`, 8192 bytes of init data at file offset `0x3d000`):

```
GLB control block, shared offset 0x000 …… i.e. VA 0x04000000
  version        @0x00 = 0x00000000
  features       @0x04 = 0x00000000
  input_va       @0x08 = 0x04002000     <-- this is your "0x4002000"
  output_va      @0x0c = 0x04003000     <-- this is your "0x4003000"
  group_num      @0x10 = 0x00000008
  group_stride   @0x14 = 0x000000a0
  perfcnt_size   @0x18 = 0x00000000
  instr_features @0x1c = 0x00000071
```

So:

* **The version word is `0x04000000`** (shared section offset 0), *not* `0x04002000`.
* `0x04002000` is `GLB_INPUT_VA` (the global *input* block: `req`, `ack_irq_mask`, `doorbell_req`, timers, `core_en_mask`).
* `0x04003000` is `GLB_OUTPUT_VA` (the global *output* block: `ack`, `doorbell_ack`, `halt_status`, …).

kbase agrees: `GLB_VERSION 0x0000`, `GLB_INPUT_VA 0x0008`, `GLB_OUTPUT_VA 0x000C` at **KB** `kb_csf_regs.h:182-189`, relative to the shared section base. panthor agrees structurally: **P-master** `panthor_fw.h:194-203`,

```c
struct panthor_fw_global_control_iface {
	u32 version;
	u32 features;
	u32 input_va;
	u32 output_va;
	u32 group_num;
	u32 group_stride;
	u32 perfcnt_size;
	u32 instr_features;
};
```

and **P-master** `panthor_fw.c:1023`: `glb_iface->control = ptdev->fw->shared_section->mem->kmap;` — i.e. the control block is at *offset 0 of the shared section*, whose start is forced to `0x04000000` (**P-master** `panthor_fw.c:203`, `#define CSF_MCU_SHARED_REGION_START 0x04000000ULL`).

Your driver's own struct is right (`hakios.mali610.c:2675-2678`) and `gShared->mem.kva` *is* offset 0, so the sentinel at `+0` is being written and read at the correct VA. The `round7 GLB:` print is echoing `input_va`/`output_va`, not the version address — nothing is reading the wrong address.

**Consequence:** the sentinel surviving at `0x04000000` genuinely means "the firmware never executed the instruction that publishes the version", it is not an addressing bug.

---

## §1 The exact boot sequence

### panthor (P-master `panthor_fw.c:1132-1165`)

```c
static int panthor_fw_start(struct panthor_device *ptdev)
{
	struct panthor_fw *fw = ptdev->fw;
	bool timedout = false;

	ptdev->fw->booted = false;
	panthor_irq_enable_events(&ptdev->fw->irq, ~0);
	panthor_irq_resume(&ptdev->fw->irq);
	gpu_write(fw->iomem, MCU_CONTROL, MCU_CONTROL_AUTO);

	if (!wait_event_timeout(ptdev->fw->req_waitqueue,
				ptdev->fw->booted,
				msecs_to_jiffies(1000))) {
		if (!ptdev->fw->booted &&
		    !(gpu_read(fw->irq.iomem, INT_STAT) & JOB_INT_GLOBAL_IF))
			timedout = true;
	}

	if (timedout) {
		static const char *const status_str[] = {
			[MCU_STATUS_DISABLED] = "disabled",
			[MCU_STATUS_ENABLED] = "enabled",
			[MCU_STATUS_HALT] = "halt",
			[MCU_STATUS_FATAL] = "fatal",
		};
		u32 status = gpu_read(fw->iomem, MCU_STATUS);

		drm_err(&ptdev->base, "Failed to boot MCU (status=%s)",
			status < ARRAY_SIZE(status_str) ? status_str[status] : "unknown");
		return -ETIMEDOUT;
	}
	return 0;
}
```

P-7.0 is the same thing with the older IRQ API (`P-7.0 panthor_fw.c:1109-1140`):

```c
	ptdev->fw->booted = false;
	panthor_job_irq_resume(&ptdev->fw->irq, ~0);
	gpu_write(ptdev, MCU_CONTROL, MCU_CONTROL_AUTO);
```

The `~0` mask means: write `JOB_INT_CLEAR = ~0`, then `JOB_INT_MASK = ~0` (**P-master** `panthor_device.h:636-642`):

```c
static inline void panthor_irq_resume(struct panthor_irq *pirq)
{
	guard(spinlock_irqsave)(&pirq->lock);
	pirq->state = PANTHOR_IRQ_STATE_ACTIVE;
	gpu_write(pirq->iomem, INT_CLEAR, pirq->mask);
	gpu_write(pirq->iomem, INT_MASK, pirq->mask);
}
```

Note in the new API `panthor_request_job_irq(..., mask = 0)` initially (**P-7.0** `panthor_fw.c:1483`), so the MCU-start path *is* what unmasks everything.

Register values (**P-master** `panthor_fw_regs.h`):

| Symbol | Addr / value |
|---|---|
| `MCU_CONTROL_BASE` | `0x700` |
| `MCU_CONTROL` | `0x0` → absolute `0x700` |
| `MCU_CONTROL_ENABLE` | `1` |
| `MCU_CONTROL_AUTO` | `2` ← **the only value used to start** |
| `MCU_CONTROL_DISABLE` | `0` |
| `MCU_STATUS` | `0x4` → absolute `0x704`; `DISABLED 0, ENABLED 1, HALT 2, FATAL 3` |
| `JOB_INT_BASE` | `0x1000` (RAWSTAT/`CLEAR`/`MASK`/`STAT` = `+0/4/8/c`) |
| `JOB_INT_GLOBAL_IF` | `BIT(31)` |
| `JOB_INT_CSG_IF(x)` | `BIT(x)` |
| `CSF_DOORBELL(i)` | `0x80000 + i*0x10000` |
| `CSF_GLB_DOORBELL_ID` | `0` |

### Numbered sequence, with the extra steps panthor does that kbase does too

1. **Create the MCU VM**, 4 GB (`for_mcu` → `min_va=0, va_range=SZ_4G`, comment "CSF MCU is a cortex M7, and can only address 4G") — **P-7.0** `panthor_mmu.c:2458-2465`.
2. **Reserve `0x04000000..0x08000000` for firmware-owned shared/kernel VA** — **P-master** `panthor_fw.c:1525-1528`:
   ```c
   fw->vm = panthor_vm_create(ptdev, true,
                              0, SZ_4G,
                              CSF_MCU_SHARED_REGION_START,   /* 0x04000000 */
                              CSF_MCU_SHARED_REGION_SIZE);   /* 0x04000000 */
   ```
3. **Load + map firmware sections** (`panthor_fw_load`, `panthor_fw_load_section_entry`), including the "interface at `0x04000000` must be SHARED" rule (**P-master** `panthor_fw.c:592-597`).
4. **`panthor_vm_active(fw->vm)`** — programs `AS_TRANSTAB` etc. for AS0 (**P-master** `panthor_fw.c:1539`; body **P-7.0** `panthor_mmu.c:737-760`).
5. **`ptdev->fw->booted = false`**, `JOB_INT_CLEAR = ~0`, `JOB_INT_MASK = ~0`, IRQ line live.
6. **`gpu_write(MCU_CONTROL, MCU_CONTROL_AUTO)`** = write `2` to `0x700`.
7. **Wait for `JOB_INT_GLOBAL_IF` (bit 31 of `JOB_INT_STAT`/`RAWSTAT`)**, 1000 ms, then re-check `INT_STAT` once before declaring timeout.
8. **`panthor_fw_init_ifaces()`** — *after* the IRQ — reads `version`, `input_va`, `output_va`, `group_num`, `group_stride` and offsets from the shared section (**P-master** `panthor_fw.c:1010-1059`).
9. **`panthor_fw_init_global_iface()`** — host writes `core_en_mask`, timers, `ack_irq_mask`, then `GLB_IDLE_EN|GLB_COUNTER_EN`, toggles `GLB_CFG_ALLOC_EN|GLB_CFG_POWEROFF_TIMER|GLB_CFG_PROGRESS_TIMER`, and **rings the GLB doorbell** (**P-master** `panthor_fw.c:1085`: `gpu_write(ptdev, CSF_DOORBELL(CSF_GLB_DOORBELL_ID), 1);`).

**Important ordering fact:** panthor does **not** ring the GLB doorbell before starting the MCU. The doorbell in step 9 is a *post-boot* request-channel kick, not part of the boot handshake. There is no "doorbell then start" and no `JOB_INT_MASK` write tied to doorbells.

### kbase (KB `mali_kbase_csf_firmware.c`)

```c
static void wait_for_firmware_boot(struct kbase_device *kbdev)
{
	...
	/* Firmware will generate a global interface interrupt once booting
	 * is complete
	 */
	remaining = wait_event_timeout(kbdev->csf.event_wait, kbdev->csf.interrupt_received == true,
				       wait_timeout);
	if (!remaining)
		dev_err(kbdev->dev, "Timed out waiting for fw boot completion");

	kbdev->csf.interrupt_received = false;
}
```
— **KB:334-354**, and the armbian copy has the same comment at **KB-arm:273-275**.

```c
static void enable_mcu(struct kbase_device *kbdev)
{
	...
	/* Trigger the boot of MCU firmware, Use the AUTO mode as
	 * otherwise on fast reset, to exit protected mode, MCU will
	 * not reboot by itself to enter normal mode.
	 */
	kbase_reg_write32(kbdev, GPU_CONTROL_ENUM(MCU_CONTROL), MCU_CONTROL_REQ_AUTO);
}
```
— **KB:356-365**; `boot_csf_firmware()` = `enable_mcu(); wait_for_firmware_boot();` — **KB:367-372**.

kbase's load order — **KB:2642-2654**:

```c
	/* Make sure L2 cache is powered up */
	kbase_pm_wait_for_l2_powered(kbdev);

	/* Load the MMU tables into the selected address space */
	ret = load_mmu_tables(kbdev);
	if (ret != 0)
		goto err_out;

	boot_csf_firmware(kbdev);

	ret = parse_capabilities(kbdev);   /* reads GLB_VERSION here */
```

kbase's explicit "shared region is at a fixed VA" comment — **KB:893-898**:

```c
	/* Start location of the shared interface area is fixed and is
	 * specified in firmware spec, and so there shall only be a
	 * single entry with that start address.
	 */
	if (virtual_start == (KBASE_REG_ZONE_MCU_SHARED_BASE << PAGE_SHIFT))
		kbdev->csf.shared_interface = interface;
```

**No separate "shared region pointer" register exists in either driver.** The shared region's address is communicated *only* by mapping the firmware's sections at their declared VAs. NOT FOUND: any register that takes the shared base.

---

## §2 The version word

**Address:** `0x04000000` (shared section offset `0x0000`), field `version`, `u32`.
**kbase name:** `GLB_VERSION`, offset `0x0000` — **KB** `kb_csf_regs.h:182`.

Read in kbase — **KB:1264-1270**:

```c
static u32 get_firmware_version(struct kbase_device *kbdev)
{
	struct kbase_csf_firmware_interface *interface = kbdev->csf.shared_interface;
	u32 *shared_info = interface->kernel_map;

	return shared_info[GLB_VERSION / 4];
}
```

and validated in `parse_capabilities()` — **KB:1282-1289**:

```c
	/* The version number of the global interface is expected to be a
	 * non-zero value. If it's not, the firmware may not have booted.
	 */
	iface->version = get_firmware_version(kbdev);
	if (!iface->version) {
		dev_err(kbdev->dev, "Version check failed. Firmware may have failed to boot.");
		return -EINVAL;
	}
```

Read in panthor — **P-master** `panthor_fw.c:1019-1028`:

```c
	if (!glb_iface->control->version) {
		drm_err(&ptdev->base, "Firmware version is 0. Firmware may have failed to boot");
		return -EINVAL;
	}

	glb_iface->input = iface_fw_to_cpu_addr(ptdev, glb_iface->control->input_va,
						sizeof(*glb_iface->input));
```

Version encoding — **P-master** `panthor_fw.c:209-213`:

```c
#define CSF_IFACE_VERSION(major, minor, patch)	\
	(((major) << 24) | ((minor) << 16) | (patch))
#define CSF_IFACE_VERSION_MAJOR(v)		((v) >> 24)
#define CSF_IFACE_VERSION_MINOR(v)		(((v) >> 16) & 0xff)
#define CSF_IFACE_VERSION_PATCH(v)		((v) & 0xffff)
```

kbase uses the same shape: `GLB_VERSION_PATCH_SHIFT 0 / MINOR_SHIFT 16 / MAJOR_SHIFT 24` — **KB** `kb_csf_regs.h:277-297`.

**What the firmware actually writes (disassembled from your image).** In code section 1 (VA `0x01000000`), at VA `0x01003008`-`0x01003012`:

```
1002fce:  movw  r0, #0x0            ; 0x00000000
1002fd2:  movt  r0, #0x400          ; r0 = 0x04000000  <-- shared section base
...
1003008:  movs  r1, #0x0
100300a:  dsb   sy
100300e:  movt  r1, #0x101          ; r1 = 0x01010000
1003012:  str   r1, [r0]            ; *0x04000000 = 0x01010000
```

`0x01010000` = `CSF_IFACE_VERSION(1, 1, 0)` = **interface 1.1.0**. (Checker: with correct Thumb-2 decode, `f2c0 1101` at `0x100300e` is `movt r1,#0x101`, and `r0` is not re-written between `0x1002fd2` and `0x1003012` — verified by a full def-use scan of that window.)

So: **version is published to `0x04000000`; nothing in the image ever writes `0x04002000`'s word 0 as a version.** The image ships `version = 0` and `features = 0`, and *everything else* in the GLB control block is pre-baked — which is precisely why the static block looks plausible and why only `version`/`features` can prove execution.

Also written at boot: `0x04000018` (`GLB_PRFCNT_SIZE`) at `0x01002fd6` (`str r1,[r0,#0x18]`), value derived from a counter-width computation.

**When:** before `JOB_INT_GLOBAL_IF` is raised — the raise site (see §3) is a separate helper, and both drivers read the version only *after* the interrupt arrives and treat 0 as "failed to boot". Ordering contract: **publish version → then raise GLOBAL_IF**.

---

## §3 What `JOB_INT_GLOBAL_IF` (bit 31) signals

**Answer: (b) — only after the firmware has finished initialising.** It is the firmware's "boot complete" announcement, not a start-of-execution ack and not a job notification.

### kbase, explicit comment + code

**KB:345-348** (inside `wait_for_firmware_boot`):

```c
	/* Firmware will generate a global interface interrupt once booting
	 * is complete
	 */
```

**KB** `mali_kbase_csf.c:3083-3132`:

```c
void kbase_csf_interrupt(struct kbase_device *kbdev, u32 val)
{
	...
	do {
		u32 csg_interrupts = val & ~JOB_IRQ_GLOBAL_IF;
		...
		kbase_reg_write32(kbdev, JOB_CONTROL_ENUM(JOB_IRQ_CLEAR), val);
		...
		if (val & JOB_IRQ_GLOBAL_IF) {
			const struct kbase_csf_global_iface *const global_iface =
				&kbdev->csf.global_iface;

			kbdev->csf.interrupt_received = true;

			if (!kbdev->csf.firmware_reloaded)
				kbase_csf_firmware_reload_completed(kbdev);
```

and `kbase_csf_firmware_reload_completed()` then re-reads the version to confirm the firmware really rebooted — **KB:2142-2165**:

```c
void kbase_csf_firmware_reload_completed(struct kbase_device *kbdev)
{
	u32 version;
	...
	/* Check firmware rebooted properly: we do not expect
	 * the version number to change with a running reboot.
	 */
	version = get_firmware_version(kbdev);

	if (version != kbdev->csf.global_iface.version)
		dev_err(kbdev->dev, "Version check failed in firmware reboot.");
	...
	/* Tell MCU state machine to transit to next state */
	kbdev->csf.firmware_reloaded = true;
	kbase_pm_update_state(kbdev);
}
```

That is decisive: the *first* thing kbase does on GLOBAL_IF is check that the version word is populated. So GLOBAL_IF and "version published" are the same event.

### panthor

**P-master** `panthor_fw.c:1098-1125`:

```c
static void panthor_job_irq_handler(struct panthor_irq *pirq, u32 status)
{
	struct panthor_device *ptdev = pirq->ptdev;
	u32 duration;
	u64 start = 0;

	if (tracepoint_enabled(gpu_job_irq))
		start = ktime_get_ns();

	gpu_write(ptdev->fw->irq.iomem, INT_CLEAR, status);

	if (!ptdev->fw->booted && (status & JOB_INT_GLOBAL_IF))
		ptdev->fw->booted = true;

	wake_up_all(&ptdev->fw->req_waitqueue);

	/* If the FW is not booted, don't process IRQs, just flag the FW as booted. */
	if (!ptdev->fw->booted)
		return;

	panthor_sched_report_fw_events(ptdev, status);
	...
}
```

P-7.0 identical minus tracing — **P-7.0** `panthor_fw.c:1092-1106`:

```c
static void panthor_job_irq_handler(struct panthor_device *ptdev, u32 status)
{
	gpu_write(ptdev, JOB_INT_CLEAR, status);

	if (!ptdev->fw->booted && (status & JOB_INT_GLOBAL_IF))
		ptdev->fw->booted = true;

	wake_up_all(&ptdev->fw->req_waitqueue);

	/* If the FW is not booted, don't process IRQs, just flag the FW as booted. */
	if (!ptdev->fw->booted)
		return;

	panthor_sched_report_fw_events(ptdev, status);
}
PANTHOR_IRQ_HANDLER(job, JOB, panthor_job_irq_handler);
```

After boot, bit 31 keeps its meaning as "global channel has news" and is dispatched into the scheduler — **P-7.0** `panthor_sched.c:1880-1883`:

```c
	if (events & JOB_INT_GLOBAL_IF) {
		sched_process_global_irq_locked(ptdev);
		events &= ~JOB_INT_GLOBAL_IF;
	}
```

So: **(a) is false** (MCU_STATUS=ENABLED is exactly the state that does *not* imply execution — see panthor's own error path printing `status=1` on failure), **(c) is false** (jobs use `JOB_INT_CSG_IF(x)`, `BIT(x)`, plus the per-CS ring buffers), **(b) is correct**.

### Verified raise site in your firmware

Helper at VA `0x01008fe6`/`0x0100901e` (code section 1), the "notify host" routine:

```
100901e:  dsb   sy
1009022:  movw  r0, #0x1000
1009026:  mov.w r1, #0x80000000
100902a:  movt  r0, #0x4000        ; r0 = 0x40001000
100902e:  str   r1, [r0]           ; *0x40001000 = 0x80000000  == JOB_INT_RAWSTAT |= BIT(31)
```

`0x40000000` is the **GPU register window in the MCU's address space** (confirmed: 37 sites form `0x4000xxxx`, and the constants land exactly on documented registers — `0x40000100` `GPU_SHADER_PRESENT`, `0x40000110` `TILER_PRESENT`, `0x40000140` `SHADER_READY`, `0x40000180` `SHADER_PWRON`, `0x40000200` `SHADER_PWRTRANS`, `0x40000240` `SHADER_PWRACTIVE`, `0x40000304` `GPU_COHERENCY_PROTOCOL`, `0x40001000` `JOB_INT_RAWSTAT`, `0x40003000`/`0x40003020`/`0x40003034` = `GPU_CONTROL_MCU` block). Register names from **P-7.0** `panthor_regs.h:90-124, 138-143`.

Callers: `0x0100020c` (early init path) and `0x0100a02e`. So the firmware raises GLOBAL_IF by writing `BIT(31)` to `JOB_INT_RAWSTAT` at GPU VA `0x04001000`, after a `dsb sy`.

A sibling helper (`0x01008fe6`-`0x01009010`) pushes the GLB ack state into the MCU aperture: `r3 = 0x04002000`, `+0x1000` → store to `0x04003000` = `GPU_CONTROL_MCU`.

---

## §4 Host-side prerequisites before boot

Nothing exotic. Both drivers require exactly:

1. **The firmware's sections mapped at their declared VAs in the MCU VM**, with the shared section at `0x04000000` and marked SHARED. Enforced: **P-master** `panthor_fw.c:592-597`:
   ```c
	if (hdr.va.start == CSF_MCU_SHARED_REGION_START &&
	    !(hdr.flags & CSF_FW_BINARY_IFACE_ENTRY_SHARED)) {
		drm_err(&ptdev->base,
			"Interface at 0x%llx must be shared", CSF_MCU_SHARED_REGION_START);
		return -EINVAL;
	}
   ```
   There is **no register** to program with the shared base. NOT FOUND: any "shared region pointer" register in panthor, panthor_fw_regs.h, or kbase.

2. **AS0 programmed and active** (`AS_TRANSTAB`, `AS_TRANSCFG` = `AARCH64_4K`, memattr table) — **P-7.0** `panthor_mmu.c:737-745`, and panthor does it *before* `MCU_CONTROL` (**P-master** `panthor_fw.c:1539-1543`).

3. **L2 powered** before loading/starting — **P-master** `panthor_fw.c:1521`, **KB:2642-2643** (`kbase_pm_wait_for_l2_powered`).

4. **`JOB_INT_MASK` unmasked** (all ones = enabled) before/at the `MCU_CONTROL` write — see §1.

5. **What must NOT be required:** no config magic, no host-filled config structure before boot. Confirmed from the image: the `config` section (VA 0, flags `0x00000009` = read-only, cached, 2836 bytes) is the firmware's own read-only data — the vector table is at its start (`SP=0x02040000`, `reset=0x010018ed`) — and the 5 `CONFIG` *entries* (type 1) are **host-tunable knobs, not boot prerequisites**. kbase parses them into a list and only writes them if a sysfs/devicetree override exists:

   * entry format, **KB** `mali_kbase_csf_firmware_cfg.c:305-338`:
     ```c
	const char *name = (char *)&entry[3];
	...
	config->address = entry[0];
	config->min = entry[1];
	config->max = entry[2];
	memcpy(config->name, name, name_len);
     ```
     i.e. `{ u32 addr; u32 min; u32 max; char name[]; }`, where `addr` is the **firmware VA of the word to patch**.
   * your image's five entries: `0x80000000` "Compute iterator suspend stage skip mask" (max `0x7`), `0x84000000` "Fragment iterator …", `0x88000000` "Tiler iterator …", `0xc80a0000` "Log verbosity" (max 3, *updatable*).
   * **`kbase_csf_firmware_cfg_init()` runs *after* `boot_csf_firmware()`** — **KB:2650 vs 2684**. So config entries are read post-boot, and panthor ignores them entirely (**P-master** `panthor_fw.c:784-789`: `case CSF_FW_BINARY_ENTRY_TYPE_CONFIG: … return 0;`).
   * **NOT FOUND:** `CSF_CFG_CA_QUEUE_SIZE`, `CSF_CFG_FW_SUSPEND` in this DDK. This tree's knobs are the iterator-suspend masks + log verbosity + a `quirks_ext` workaround word (`CSF_FIRMWARE_CFG_WA_CFG0_ENTRY_NAME`, **KB** `cfg.c:349`). They are **not** queue-size/suspend config. That naming belongs to a different/newer DDK.

---

## §5 PROT sections

### The panthor code you asked for

**P-master** `panthor_fw.c:586-590`:

```c
	if (hdr.flags & CSF_FW_BINARY_IFACE_ENTRY_PROT) {
		drm_warn(&ptdev->base,
			 "Firmware protected mode entry is not supported, ignoring");
		return 0;
	}
```

**P-7.0** `panthor_fw.c:582-586` — same, one word different ("entry not be supported"):

```c
	if (hdr.flags & CSF_FW_BINARY_IFACE_ENTRY_PROT) {
		drm_warn(&ptdev->base,
			 "Firmware protected mode entry not be supported, ignoring");
		return 0;
	}
```

The `return 0` means "skip this entry entirely": no BO, no mapping, no `shared_section` assignment. Panthor's `CSF_FW_BINARY_IFACE_ENTRY_PROT` is `BIT(5)` — **P-master** `panthor_fw.c:107`.

### kbase does NOT unconditionally skip

**KB:756-763**:

```c
	if (flags & CSF_FIRMWARE_ENTRY_PROTECTED)
		protected_mode = true;

	if (protected_mode && kbdev->csf.pma_dev == NULL) {
		dev_dbg(kbdev->dev,
			"Protected memory allocator not found, Firmware protected mode entry will not be supported");
		return 0;
	}
```

and if an allocator *is* registered it really allocates protected memory and maps it — **KB:778-787**:

```c
	if (protected_mode) {
		if (!reuse_pages) {
			pma = kbase_csf_protected_memory_alloc(kbdev, phys, num_pages_aligned,
							       is_small_page);
			...
		}
	}
```

The allocator is an **external platform driver** (`kbdev->csf.pma_dev`, from a platform device; **KB** `mali_kbase_csf_protected_memory.c:45-46, 72-106`), and the region gets `KBASE_REG_PROTECTED` (**KB:460-461**).

### Your firmware's three PROT entries

From the image: entries 6/7/8, flags `0x0000002b`, VA `0x03000000-0x0303e000`, `0x0303e000-0x0303f000`, `0x0303f000-0x03040000`, **`data.start == data.end` (no bytes at all)**, `ZERO` flag **clear**.

Note: `0x2b` = `RD(1) | WR(2) | CACHED(8) | PROT(0x20)`, **`ZERO` (bit 31) is NOT set**. So the only correct fill is zero — 256 KB of zeroed, writable, cached memory.

### Correct behaviour, and what breaks

* **Skip (panthor's choice) is only safe for panthor's supported parts.** For *your* image it is a live hazard, because the firmware's reset path touches PROT memory (see below).
* **kbase's behaviour with no allocator** = also skip (returns 0), same as panthor, only a `dev_dbg`.
* **Map normally** = what your driver does, and it is the closest to correct *provided the pages are zero-filled, writable, and cacheable*. Nobody in-tree maps them with a special attribute except through the pma.
* **What breaks if you map them normally:** nothing attributable to the mapping itself — `KBASE_REG_PROTECTED` only matters when a real protected-memory allocator exists (it is about securing the pages from the CPU/GPU, needed for DRM protected content, not for boot). But note the flag combination: PROT pages in this image are `rw- cached` with **no `ZERO` bit**, so a skipped mapping leaves `0x03000000-0x03040000` **unmapped**, and an unmapped data access inside the MCU produces **no MMU interrupt and no fault address** — exactly the silent-hang shape you are seeing.

**Concrete hazard found in the disassembly** — reset handler tail, VA `0x01001918`-`0x01001936`:

```
1001918:  ldr   r0, [pc, #0x7c]     ; 0x01001998 -> 0x40003034  (GPU_CONTROL_MCU+0x34)
100191a:  ldr   r4, [r0]
100191c:  and   r4, r4, #0x80       ; bit 7
1001920:  mov   r0, r4
1001922:  bl    0x1003126
1001926:  bl    0x10019d8
100192a:  bl    0x10019e2
100192e:  bl    0x1001a2a
1001932:  cbz   r4, 0x1001982       ; if bit7 clear  -> skip everything below
1001934:  ldr   r0, [pc, #0x64]     ; 0x0100199c -> 0x0302bc78   <-- INSIDE PROT SECTION 6
1001936:  ldrb  r1, [r0]
1001938:  cbz   r1, 0x1001982
100193a:  ldr   r0, [pc, #0x64]     ; 0x010019a0 -> 0x03800000   <-- NOT IN ANY DECLARED SECTION
100193c:  ldr   r1, [pc, #0x64]     ; 0x010019a4 -> 0x02000000
100193e:  ldr   r2, [pc, #0x68]     ; 0x010019a8 -> 0x0202c210
1001940:  ldrd  r3, r4, [r0], #8
1001944:  strd  r3, r4, [r1], #8
        ... copies 0x03800000 -> 0x02000000 while r1 <= 0x0202c210
1001964:  ldr   r2, [pc, #0x44]     ; 0x010019ac -> 0x0202c944
        ... then zeroes 0x02000000..0x0202c944
```

Two things fall out of this:

1. `0x0302BC78` is **inside PROT section 6**. If you skip PROT mapping (panthor-style), this `ldrb` is a silent data abort the moment `GPU_CONTROL_MCU+0x34` bit 7 reads set.
2. `0x03800000` is **not covered by any of the 10 declared sections** (they are `0x0`, `0x1000000`, `0x2000000-0x2040000`, `0x2800000-0x2840000`, `0x3000000-0x3040000`, `0x4000000-0x4094000`). Read-only from it would fault identically. This looks like a suspend/protected-buffer restore path guarded by the flag byte at `0x0302BC78` — i.e. it only runs if the memory says "restore", so a *zeroed* PROT mapping keeps it inert, while an *unmapped* PROT mapping faults on the guard read itself.

Practical recommendation: **map the three PROT sections as ordinary zeroed, writable, cached pages** (what your driver already does) — that is equivalent to kbase-without-pma-plus-a-zero-fill and strictly safer than skipping. Make sure the zero-fill happens even though `ZERO` is clear (the entries carry no data; without an explicit memset you get whatever the allocator gave you, and `mem_alloc_low()` does memset — good).

---

## §6 CSF shared memory layout

Derived from **your image** (authoritative for this firmware) plus kbase's register header.

### Top level — `[shared section base + 0x04000000]`

| Offset | Size | Contents | Source |
|---|---|---|---|
| `0x0000` | `0x20` | GLB control block: `version, features, input_va, output_va, group_num, group_stride, perfcnt_size, instr_features` | image @0x0000; **KB** `kb_csf_regs.h:182-189` |
| `0x0020`-`0x0FFF` | — | padding (all zero in image) | image |
| `0x1000` | `0xA0` per group × 8 | CSG control blocks (`GROUP_CONTROL_0`), stride from `group_stride` | image; **KB** `kb_csf_regs.h:190-193` |
| `0x1000+0x40` | `0x0C` per stream × 8 | CS control blocks inside each group (`STREAM_CONTROL_0`), stride from `stream_stride` | image; **KB** `kb_csf_regs.h:208-211` |
| `0x2000` | — | GLB **input** block (`input_va`) | image |
| `0x3000` | — | GLB **output** block (`output_va`) | image |
| `0x4000` | `0x1000` per group | CSG **input** blocks (CSG0 at `0x4000`, stride `0x1000`) | image |
| `0xC000` | `0x1000` per group | CSG **output** blocks (CSG0 at `0xC000`) | image |
| `0x14000` | `0x1000` per stream | CS **input** blocks (CS0/CSG0 at `0x14000`) | image |
| `0x54000` | `0x1000` per stream | CS **output** blocks (CS0/CSG0 at `0x54000`) | image |
| up to `0x94000` | | declared span (606208 B), only `0x2000` of init data | image entry 9 |

Constants in panthor: `CSF_GROUP_CONTROL_OFFSET 0x1000`, `CSF_STREAM_CONTROL_OFFSET 0x40` — **P-master** `panthor_fw.c:215-216`, and the arithmetic is identical (**P-master** `panthor_fw.c:956-957`):

```c
	u64 iface_offset = CSF_GROUP_CONTROL_OFFSET +
			   ((u64)csg_idx * glb_iface->control->group_stride);
```

### `struct csf_glb` control (`0x04000000`)

| Off | Field | This image |
|---|---|---|
| `0x00` | `version` | `0` → firmware must write `0x01010000` |
| `0x04` | `features` | `0` |
| `0x08` | `input_va` | `0x04002000` |
| `0x0C` | `output_va` | `0x04003000` |
| `0x10` | `group_num` | `8` |
| `0x14` | `group_stride` | `0xA0` |
| `0x18` | `perfcnt_size` | `0` |
| `0x1C` | `instr_features` | `0x71` |

### `csf_group` / CSG control block (`+0x1000 + n*0xA0`)

| Off | Field | CSG0 value |
|---|---|---|
| `0x00` | `features` | `0` |
| `0x04` | `input_va` | `0x04004000` |
| `0x08` | `output_va` | `0x0400C000` |
| `0x0C` | `suspend_size` | `0` |
| `0x10` | `protm_suspend_size` | `0` |
| `0x14` | `stream_num` | `8` |
| `0x18` | `stream_stride` | `0xC` |

(Matches **KB** `kb_csf_regs.h:200-207`: `GROUP_FEATURES 0`, `GROUP_INPUT_VA 4`, `GROUP_OUTPUT_VA 8`, `GROUP_SUSPEND_SIZE 0xC`, `GROUP_PROTM_SUSPEND_SIZE 0x10`, `GROUP_STREAM_NUM 0x14`, `GROUP_STREAM_STRIDE 0x18`, `STREAM_CONTROL_0 0x40`.)

### `csf_stream` / CS control block (`+0x1040 + n*0xC` within CSG0)

| Off | Field | CS0/CSG0 |
|---|---|---|
| `0x00` | `features` | `0` |
| `0x04` | `input_va` | `0x04014000` |
| `0x08` | `output_va` | `0x04054000` |

(**KB** `kb_csf_regs.h:195-198`.)

### GLB input/output blocks

GLB input (`0x04002000`), per **P-master** `panthor_fw.h:205-261`: `req`(0), `ack_irq_mask`(4), `doorbell_req`(8), `progress_timer`(0x10), `poweroff_timer`(0x14), `core_en_mask`(0x18, u64), `perfcnt_*`, `idle_timer`.
GLB output (`0x04003000`), **P-master** `panthor_fw.h:269-277`: `ack`(0), `doorbell_ack`(8), `halt_status`(0x10), `perfcnt_status`(0x14), `perfcnt_insert`(0x18).
`GLB_INPUT_VA/GLB_OUTPUT_VA` register names (**KB** `kb_csf_regs.h:184-185`); `GLB_REQ 0x0`, `GLB_ACK_IRQ_MASK 0x4`, `GLB_ACK 0x0` (**KB** `:214, 256`).

---

## §7 Ranked next checks — MCU_STATUS=ENABLED, no GLOBAL_IF, version untouched

Work down this list; each item is a distinct falsifiable hypothesis.

1. **Confirm which address you are polling.** The version is at `0x04000000`, not `0x04002000`. If any readback path uses `+0x2000` it will never change no matter what the firmware does. (Your `round7 GLB:` line prints `raw->version` from `kva+0`, so this is probably already right — verify from the log that the `version %#x` in that line, not the `in %#x`, is the one compared to the sentinel.)

2. **`GPU_CONTROL_MCU+0x34` bit 7 → PROT read at `0x0302BC78`.** Read `0x04003034` and check bit 7. If set, the firmware's first post-init act is `ldrb` from PROT section 6. If you map PROT → fine; if you skip it → silent abort, no `MMU_INT`, no fault address, `MCU_STATUS` stays 1, version never written. **This exactly reproduces your symptom.** Cheapest decisive test: `mrd(0x40003034)` and compare against a build where you also pre-set a witness value at `0x04000004` (`features`, which the firmware is also expected to write) — if neither moves, the core died before `0x01003012`.

3. **Instrument the two firmware-side "I am alive" landmarks that do *not* depend on my disassembly reading.** Write a sentinel into `0x04000018` (`perfcnt_size`, which the firmware stores at `0x01002fd6`) and into `0x04000004` (`features`). Three independent words written at three different points in the boot give you a progress bar: `0x18` earliest, `0x00` last, `0x04` somewhere between.

4. **Section 0's executable bit.** Entry 0 (`config`, flags `0x00000009` = `RD|CACHED`, **no `EX`**) holds the vector table: `SP=0x02040000`, `reset=0x010018ed`. Your code marks all non-`EX` sections `XN`, so VA 0 and every page of entry 0 is non-executable — but the CPU only *reads* `[0]` and `[4]` there, then branches into section 1 (`0x010018ed`, entry 1, flags `0x0000000d` = `RD|WR|EX|CACHED`). So `XN` on section 0 is harmless **provided** the two 32-bit data reads at VA `0`/`4` are permitted and coherent — that is the `mali_fwnc` / cache-mode question, and your own comments (`hakios.mali610.c:1112-1121`, `1159-1164`) already flag it. Test with `mali_fwnc=1` (everything non-cacheable) as the single-variable experiment.

5. **Leaf coverage / granule.** Your `pt_map()` indexes level-3 with `(va >> 12) & 0x1ff`, i.e. a 4 KiB granule, while `PT_LEAF_GRANULE` is `0x1000` — good. But `PAGE_SIZE` is 16 KiB on this kernel, and the *span* arithmetic (`mem_alloc`, `mem_pa`) rounds with `PAGE_MASK`. Verify that for section 1 (63560 bytes of data, span `0x10000`) every one of the 16 leaves is present, and that `mem_pa(va - va_start)` for the last leaf does not exceed the allocation. A missing leaf under the reset vector is the classic silent-hang (your own comment at `1186-1189` describes exactly this failure mode surviving in the "witness" experiment).

6. **PROT pages must exist and be zeroed, and their PTE must be writable.** `0x2b` → `RD|WR|CACHED|PROT`, no `ZERO`. `mem_alloc_low()` does `memset(...,0,size)` — confirm PROT sections actually come from that path and that `PTE_RDONLY` is **not** set for them (your code sets `PTE_RDONLY` when `!(flags & SEC_WR)`; `0x2b` has WR so this is fine, but verify at runtime with the printed `attrs`).

7. **`0x03800000` is a hole.** If check #2 shows bit 7 set *and* the byte at `0x0302BC78` is non-zero for any reason (stale memory, aliasing, a previous run's data), the firmware will `ldrd` from unmapped `0x03800000`. Make sure PROT is genuinely zero at boot, on every run, before `MCU_CONTROL`.

8. **Coherency of the shared section.** Command entry 9 is `CACHE=3` (`cached_coherent`), `SHARED`. kbase would need ACE and downgrades to `NON_CACHEABLE(5)` on a `COHERENCY_NONE` platform — **KB:463-495**:
   ```c
	/* Substitute uncached coherent memory for cached coherent memory if
	 * the system does not support ACE coherency. */
	if ((cache_mode == CSF_FIRMWARE_CACHE_MODE_CACHED_COHERENT) &&
	    (kbdev->system_coherency != COHERENCY_ACE))
		cache_mode = CSF_FIRMWARE_CACHE_MODE_UNCACHED_COHERENT;
	...
	if (cache_mode == CSF_FIRMWARE_CACHE_MODE_UNCACHED_COHERENT) ... {
		WARN_ON(!is_shared);
		mem_flags |= KBASE_REG_SHARE_BOTH |
			     KBASE_REG_MEMATTR_INDEX(KBASE_MEMATTR_INDEX_SHARED);
	}
   ```
   Your `IDX_NC = 5` choice matches that downgrade. Just make sure the CPU-side `mem_flush()` before start actually covers `0x04000000` and that you re-read the version with a cache-invalidate (or read through an uncached alias). A cached-no-snoop CPU view of `0x04000000` would show the sentinel forever even if the firmware wrote it.

9. **`JOB_INT_RAWSTAT` vs `JOB_INT_STAT`.** Read `0x1000` (raw) as well as `0x100c` (masked). Note that `JOB_INT_MASK` semantics here are **1 = enabled** (panthor writes `0` to mask everything, `~0` to unmask everything — **P-master** `panthor_device.h:418, 436-441`). Your `gpu_irq_mask_prestart()` writes `0xffffffff` to `JOB_INT_MASK` (`hakios.mali610.c:3131-3132`), which is "all enabled" — correct. But if anything later writes `0` there, `JOB_INT_STAT` reads 0 while `RAWSTAT` still latches bit 31; that would look like "no GLOBAL_IF" while the firmware actually booted. Always cross-check `0x1000`.

10. **Finally: a witness stub.** Your driver already has the right primitive (`MALI_WITNESS_CODE_VA 0x1000000`, `start_mcu()` path). Point the reset vector at a 6-instruction stub that writes `0x1` to `0x04000000` and `0x80000000` to `0x04001000` and then spins. If the sentinel at `0x04000000` changes to `1`, the MMU + vector fetch + register writes all work and the fault is inside the vendor image; if it stays `0xff5aa5f0`, the problem is upstream of the firmware (mapping, coherency, or core not fetching) and no amount of firmware-side theory will help. This is the highest-information single experiment on the list.

---

## Appendix — things explicitly NOT FOUND

* Any register taking the shared-region base address (panthor, panthor_fw_regs.h, kbase). The shared region's address is conveyed purely by mapping sections at their declared VAs.
* `CSF_CFG_CA_QUEUE_SIZE`, `CSF_CFG_FW_SUSPEND`, or a `CSF_CONFIG` structure the host must fill before boot, in either panthor or this kbase DDK.
* A boot-time config magic check on the host side.
* Any read of shared offset `0x0FF0` in panthor or kbase — the firmware stores `0x80000000` there (`str.w r0,[r8,#0xff0]`, `0x0100301c`) right after publishing the version, but I cannot map that offset to a documented field; it is *not* `JOB_INT`-related (the actual raise is the `0x04001000` store in §3).
* `mali_kbase_gpu_regmap_csf.h` / `mali_kbase_regmap_csf.h` in the rockchip tree (404). kbase's `MCU_CONTROL_REQ_AUTO` numeric value therefore cited from panthor (`MCU_CONTROL_AUTO == 2`); kbase only ever uses the symbolic AUTO.
* The `Ken-u/Mali` XNU port was **not** fetched (no network path tried successfully within this session's budget); the local mirror at `docs/headset-wip/raw/ken-u/Mali.cpp` (3155 lines) was not diffed against this boot sequence.
