# Primary protocol references collected before717 observation

These references do not identify716's rejected field. The actual717 response is still required.
They are upstream implementation references, not a claim that every checked field is mandated
or optional by a fully reviewed JEDEC specification. No predicate has been relaxed.

- EDK2 industry-standard UFS definitions, current file retrieved2026-11-17:
  https://github.com/tianocore/edk2/blob/master/MdePkg/Include/IndustryStandard/Ufs.h
  UTP_RESPONSE_UPIU is explicitly labelled against UFS4.0 Table10.15. It places transaction
  type/flags/LUN/tag at bytes0..3, response/status at6/7, data segment length at10..11,
  residual at12..15, reserved bytes16..31, then sense-data length and sense data at32 onward.
  The UPIU data-segment field is a different field from the request's expected DMA byte count.
- EDK2 UfsExecScsiCmds:
  https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Bus/Ufs/UfsPassThruDxe/UfsPassThruHci.c
  This implementation reads sense length, exposes SCSI status, checks response and OCS, and
  handles residual underflow. It does not equate every nonzero data-segment length with failed IO.
  Its lack of an explicit comparison of a particular header field is not proof that such a
  comparison is prohibited or unnecessary for our fixed-request experiment.
- Linux ufshcd_transfer_rsp_status:
  https://github.com/torvalds/linux/blob/master/drivers/ufs/core/ufshcd.c
  This implementation checks completion OCS and response transaction type, passes SCSI status
  to the SCSI layer and handles residual/flags. It is a reference, not an automatic recipe for
  the XNU adapter, LK-owned queue or our return lease.

If717 exposes a rejected field, decode the raw bytes first. If a field describes bytes beyond
the32-byte diagnostic header, those bytes were not captured by717. Any change to interpretation
or observation width needs new candidate/tests/review and a new numbered trial. Never re-label
716 or717 from a changed predicate after the fact.
