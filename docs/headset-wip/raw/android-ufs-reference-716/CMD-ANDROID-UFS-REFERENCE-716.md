# Android UFS reference716: read-only baseline, no flash

DSH only. User requested continued progress after715 and has granted device work.
Finish the host queue review corrections, then collect this bounded Android reference.
No experimental boot, register access, SCSI passthrough, block-device write or service change.

Use the established serial explicitly for every adb call. Keep phone identifiers out of public
records. Record current boot_id and transport/device identity; last stock boot_id was
11aead4e-9595-4574-8e6a-6b229e287480. If the device is not the known phone in usable stock
Android, report and stop; do not flash or reboot merely for this baseline.

Create a fresh `P/logs/ufs-read-path-20261117/android-reference/`, preserving any previous files.
Use adb exec-out and a root read command so the binary is written directly to the HOST;
do not create a temporary file on the phone.

1. Resolve `/dev/block/by-name/xnusys` to its canonical partition node. Identify its parent
   whole-disk node from sysfs, not an assumed `sdc` letter. Record the sysfs device path and
   SCSI address/LUN correspondence. The target must be the LUN2 device the loader uses.
2. Read the parent's logical block size with `blockdev --getss`, and byte size with
   `blockdev --getsize64`. Record native command return codes and exact output.
   A geometry other than4096 or an unestablished LUN2 correspondence is a stop-and-report
   condition for the current fixed-read candidate, not permission to guess another disk.
3. Read exactly the first65536 bytes of that whole disk, with a bounded command timeout,
   directly to host `lun2-first64k.bin`. This covers primary GPT metadata only. No wider read.
4. Save exact byte count, SHA256 and CRC32 of the complete capture and of logical block1
   (offset4096,length4096). Parse the GPT signature, header length and header/partition-array
   CRCs with bounds checks. Keep the raw reference LOCAL; archive only identities, geometry,
   validated GPT structure and command/return records. Do not expose disk GUIDs unnecessarily.
5. Do not infer link speed or actual clock from Android block geometry. Do not perform a
   benchmark, mount, write, ioctl passthrough, memory getter or any UFS MMIO read.

Report the block geometry, established LUN mapping, reference identities/CRCs and any objection.
The phone remains stock; no flash or reboot is authorized by this command.
