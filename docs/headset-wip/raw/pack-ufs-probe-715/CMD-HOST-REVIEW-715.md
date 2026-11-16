# CMD-HOST-REVIEW-715

DSH: HOST ONLY. No adb/fastboot, device query, package replay into old directories, kernel
build, production source edits or new hardware experiment. The phone stays recovered stock.

Read REVIEW-AND-PLAN-715.md and inspect the actual715 artifacts under this directory.
Independently verify their sizes/hashes, exact unchanged714 payload/kernel, runtime arguments,
all five loader GPU macro values, and the runtime Mali init early return. Check all five
native packaging return codes and retain the nine frozen-source compiler warnings.

Review strict unpack and the private-output reproduction implementation. If reproducing,
use only a fresh private directory with every output audited before executing; never replay
recorded argv directly. Existing Codex results are PASS but should not replace source and
artifact inspection. No new probe model or full kernel rerun is needed for unchanged inputs.

Archive this review,715 argument/build/review scripts, planned and executed argv/return codes,
artifact identities, macro evidence and independent results under a new docs directory
`docs/headset-wip/raw/pack-ufs-probe-715/`. Keep image/payload/loader binaries local; do not
change ignore rules or overwrite sealed714/713 artifacts. Retain prior packaging-incident
and signature correction records. List any objections without silently changing the package.

Also archive the UPDATED `logs/mt6895-cpu-baseline-20261116/RETURN-ANDROID-FOLLOWUP-713.md`
with its final section: loader cleanup does exist under BOARD_MALI_POWER. Prior archived
kernel-hook-only findings must link this correction; absence of a hook is not proof of no
cleanup or proof of the cause of the Android return incident.

Clarify in the review that UCRDY is report-only for this probe, not a universal vendor-driver
rule. Preserve frozen probe source/test fixtures unchanged.

Commit/push only these documentation/evidence changes in D. Preserve the existing GPUEB
WIP unstaged. Report the commit, remote-sync result, independent findings and any objection.
No DEVICE release is authorized by this host command.
