# PACK-REVIEW716: host audit only

DSH: read REVIEW-AND-PLAN-716.md, inspect the staged trial716 artifact identities and exact
arguments against arguments716.json. Packaging is already done; do not repeat it or rebuild.

Independently inspect package-716.py and review-package-716.py. Run the strict verifier to a
NEW report path; never overwrite Codex's independent result. Cross-check actual boot header
signature_size at1580 is4096 and byte-preserved, frozen stock Android Image, nested kernel,
mandatory24-byte XNUP terminator/exactEOF and unique NUL-terminated168-byte argument body.

Audit loader source/candidate identity against the prior independent build/reproduction and
HOST-INTEGRATION-MANIFEST. Root's independent-loader-review-716.json also proves disabled
guard +715 parameters reproduce the715 binary. No additional reproduction is needed absent
a concrete concern; never replay compile commands into old or candidate directories.

Archive a new D/docs/headset-wip/raw/pack-ufs-read-716/ with arguments, host review, independent
kernel/loader/package results, packaging argv/native rc/identities, scripts and the rejected
preflight explanation. Preserve the original failure record rather than hiding it. Images stay
local, raw GPT stays local, GPUEB WIP stays unstaged. No production push, no ignore changes.

Please also correct any integration-review explanation that dismisses a load/store STOP race
by assuming other CPUs stopped. They may not all stop. The actual fail-closed argument is that
STOP prevents the clean phase CAS and loader storage/Android handover remains denied. Likewise,
the revised engine's OCS freshness is not established by DBR0 alone.

No DEVICE operation is authorized by this host command. Return independent findings, any
blocker and docs commit. Codex will then release a separate single DEVICE716 command.
