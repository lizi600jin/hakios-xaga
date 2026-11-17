# REVIEW-BUILD-719: frozen platform/loader integration, HOST ONLY

P=/home/lizi/IOS/hakios-mtk; R=P/logs/ufs-service-20261117; X=R/kernel-src. Source changes owned
by Codex. Verify HOST-INTEGRATION-MANIFEST.json and5214 inputs before any build, no BUILD initially.
Relative718:5 changed existing files,4 added source/header files,0deleted. Old source files
remain as evidence but old mt6895_ufs/read modules are not compiled or initialized in719.

Core8 is unchanged from c9a48c8 review. No need to repeat its34121/2500-schedule tests absent
a concrete concern. New actual platform C and loader helpers are the review scope.

## Platform/provider

Read DESIGN-SERVICE719.md and actual hakios.mt6895_ufs_service.c. Verify baseline provider
checks idle, inherited READ and all table/UCD/DATA twice before us_open; cached control is never
reset. Native HAKIOS_UFS compile conflicts with the service. This excludes a known competitor;
exclusive loader handoff/no other kernel UFS user is still an explicit integration precondition.

Only ACTIVE can write table/UCD/DATA; ring checks ACTIVE and shared CLAIMED before the sole
UTRLDBR1 store. finish(true) only marks DRAINED; the outer shared lease stays CLAIMED between
requests. Caller-private cached copy precedes IDLE. uiomove occurs outside service workspace.
The regular callback mutex serializes clients, never taken by quiesce. Quiesce is us_stop
only: no controller access/logger/wait. Clean IDLE/DRAINED close can publish RESTORED; ACTIVE
STOP poisons. Observe actual stop/load/store race reasoning, not an all-CPUs-stopped assumption.

Registration starts cdev allocation at-24 per this tree's API comment. Node0400 is a virtual
4096-byte LBA1 snapshot, not D_DISK/bdevsw. Open-for-write/write fail, EOF/invalid range no IO,
and global request budget3 under the mutex cannot be reset or exceeded, including failures.
Review kernel worker's actual vnode identity/read dispatch, VNOP_OPEN FWRITE refusal and
three vn_rdwr calls at2-second intervals, then EOF without another request. This is a planned
kernel VFS exercise, not yet userland/runtime proof. No broader LBA/command/geometry is permitted.

Host actual-C shim tests passed16 scenarios under ASan+UBSan/MSan. They use the LOCAL reference
file only as fake DMA data; do not upload it. Real-XNU ARM64 compile is separately recorded,
zero diagnostics. Reproduce the platform shim in YOUR fresh private copy/output if useful;
run-platform-v4-719.py holds exact argv/scenarios (change all outputs before running).
Preserve Codex's results. Its early fixture pointer-arithmetic UBSan issue is corrected; the
old fixture source is preserved. Do not present that early diagnostic run as a sanitizer PASS.

## Loader/return witness

Private loader47952 B SHA2569d2c5fb9fe534a153846f4e794da68dea8399ae03ae20371b51ad3f8b5da84cb,
three native rc0. Mirror40 inputs. Check actual preprocessed/source call order, not strings:
last boot-note WRITE -> unchanged verified predecessor READ -> fresh719 lease -> jump.
FIRST xnu_returned statement remains strict guard. It double-reads and saves the accepted
actual lease snapshot, formats a bounded audit, and halts before storage on either failure.
The existing HLOG save call uses kernel log plus audit as2 spans. The writer itself is unchanged.
Audit is last, retained by existing tail policy; reason/code and full checksum include both.
No kernel logger in quiesce, no extra HLOG record/address or guard bypass. One save call is
not one physical WRITE10; counts can change with length. Verify formatting/layout/capacity.

Host return snapshot/format23481 checks, snapshot range36894 checks, exact extracted unchanged
span-writer87 checks each passed ASan+UBSan/MSan. Span backend verified tail/audit/sum/header-last
and failed body/header writes; no physical DMA alignment/electrical claim. Source extraction
identity is in host-checks/span-writer/extraction.json. Early missing mock extern/pointer-offset
diagnostics are preserved separately. No need to rerun unchanged suites absent concrete concern.
Private native loader reproduction, if needed, must remap ALL inputs/outputs before compilation.

## Isolated kernel build if no concrete blocker

From P: XNU_SRC=absoluteX ./xnu-build.sh -k -j8, only X/BUILD. Record actual subprocess native rc,
timestamps, full stdout/stderr in R/host-checks/build719/{stdout.log,stderr.log,build-rc.txt}.
Explicit KERNEL=X/BUILD/obj/DEVELOPMENT_ARM64_BCM2837/mach.development.bcm2837 for judge. Only
established symbol-set allowance, separate native make rc from judge rc. New errors are blockers.
Verify5214 inputs/no extras outside BUILD, all frozen files, new init/quiesce symbols/call-site,
node/held-lease/request messages; old mt6895_ufs/read objects absent. Inclusion isn't runtime proof.
Recheck stock/shared/wrapper/718 kernel-image and protected WIP without changing them.

Archive NEW D/docs/headset-wip/raw/ufs-service-integration-719/ with exact changed sources,
manifest, reviews, native/model summaries and build identity/limitations. No raw GPT/GUID/images.
Return actual blockers/kernel identity/separate rc/commit. No packaging, device query/flash,
candidate edits, production push, old overwrite, GPUEB WIP staging, forbidden git commands,
clock/reset/UIC/watchdog/OEM expdb. Await separate package and ONE DEVICE719 commands.
