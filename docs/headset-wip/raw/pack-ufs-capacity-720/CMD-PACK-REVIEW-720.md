# CMD-PACK-REVIEW-720

Host-only independent package review. Do not rebuild/repackage/edit frozen inputs or query
the phone. Root already packaged into NEWtrial720 after independent kernel/provider/loader
review. Finish any outstanding build/parser archive before this task; no source rework needed.

R=/home/lizi/IOS/hakios-mtk/logs/ufs-capacity-20261117

- boot.img22999040 B SHA25687722d3250f6fd3d9ca16280d5af5397830eb6b7b8a610f2d22b9c4d2ed5b085
  MD5 4c9baf359f9ee1760345191583227a46
- loader47984 B SHA25691e6e113f7209a9ffd7534c7152e577f3cac64fcdfdf305dbff49eb72aa0861a
- payload19533144 B SHA256d0b202c08e41bec468ad9e10737b3723d41be07071d6ccf0b1fbfdf0c5ba7487
- kernel15338680 B SHA256179efc781d17b41721a2d9cd5e83fb23d9b279f44c253aff27ddc00604bca19f

Read package-720.py/review-package-720.py and run the read-only verifier to a NEW private
output path. Independently check stock header changes confined to kernel_size@8..11,
signature_size4096@1580, unchanged ramdisk/signature/AndroidImage and padding; retaining the
signature does not validate it cryptographically. Loader differs only documented10 header
bytes after chainboot, XNUP3entries+24byte terminator/exactEOF. Expected payload terminator
at19533120, but compute rather than trust. Embedded kernel matches reviewed identity.

Arguments172base/232runtime/233NUL, unique NUL-terminated body and eachkeyonce. Only capacity1,
trial720,mali0,headset0,cpus4,return150; oldservice/read/probe/cpu/tone/gpueb absent. Dirty
kernel.ver is legacy metadata, not this kernel's identity. Sources46/5220 stay frozen.

Read three independent reviews. Kernel make2/explicitjudge0 remain separate. Loader private
3rc0/byte-exact91e6e113/40inputs; from719 onlycookie/prefix plusargv change. Gate-first order,
verified predecessor READ, actual double-read snapshot and audit-last-span were already719
behavior; do not relabel them as new720 changes. Writer unchanged. No extra reproduction
needed absent concrete concern. Image structure isn't hardware geometry/query proof.

Archive NEW docs/headset-wip/raw/pack-ufs-capacity-720/ with CMD/scripts/reviews/privatecheck/
artifact records; docs-onlycommit/push, GPUEBWIPunstaged. Images/rawGPT/GUID/prelogstaylocal.
No productionpush/oldsourceorBUILDoverwrite/reset/stash/checkout/clean/gc/prune. Report any
blocker or PASS and wait for separateONEDEVICE720. No phone operation authorized by thisCMD.
