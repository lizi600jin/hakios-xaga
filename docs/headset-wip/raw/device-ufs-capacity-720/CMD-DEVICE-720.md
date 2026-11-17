# ONE DEVICE720: one guarded RC10 query, then immediate stock recovery

Execute ONLY when Codex explicitly releases ONE DEVICE720 AFTER independent package review.
DSH owns all phone commands. One experimental boot, maximum ONE kernel RC10 command. No retry,
extra query, variable READ, RC16, block registration or other experiment. User authority persists.

P=/home/lizi/IOS/hakios-mtk;R=P/logs/ufs-capacity-20261117
NEW output R/device720/. Exactphone serial-s required, redact public serial. Never overwrite.

## Identities

- boot.img22999040 B SHA25687722d3250f6fd3d9ca16280d5af5397830eb6b7b8a610f2d22b9c4d2ed5b085
  MD5 4c9baf359f9ee1760345191583227a46
- loader47984 B SHA25691e6e113f7209a9ffd7534c7152e577f3cac64fcdfdf305dbff49eb72aa0861a
- payload19533144 B SHA256d0b202c08e41bec468ad9e10737b3723d41be07071d6ccf0b1fbfdf0c5ba7487
- kernel15338680 B SHA256179efc781d17b41721a2d9cd5e83fb23d9b279f44c253aff27ddc00604bca19f
- stock P/backup/boot_a_stock_real.img67108864 B
  SHA256c5f8e7bde8e9f1d824d0c7915f7d701adce8142656a82c20e10ccb75e15e01fc
  MD5a63b2e35a7c2522cb24a6a10332b9bc8

Rehash experiment and stock immediately before flashing. Mismatch stops work. Sources46/5220
are frozen and reviewed; native make2 and explicit judge0 are separate. No rebuild/repackage.
Parameters172base/232runtime/233NUL,capacity1/trial720,mali0,headset0,cpus4,return150. Oldservice/
read/probe/cpu/gpueb/tone absent. No audio input/user setup is needed.

## Intentional guard failure

Existing boot-note WRITE precedes verified loader predecessor READ and fresh720 lease. Failed
predecessor read may halt before XNU. Uncertain DMA/restore/ACTIVE STOP poisons; loader halts
BEFORE storage/HLOG/Android handover. Only0xfeedfacebootnote may remain. Never bypass guard,
force storage, change reset/clock/watchdog, or call staleHLOGnew. CleanDRAINED/IDLE STOP may
release; active STOP may not. Query geometry mismatch itself is not a transport fault.

If no transport after bounded wait, notify Codex promptly for physical fastboot. Restore stock
as soon as exactfastboot is available; capture remainingHLOG afterward if needed. No second boot.

## A. Preflight

1.Establish sole phone-command ownership. Record identity,slot_a,boot_completed1,SELinuxEnforcing,
boot_id/full64MiBboot_a stockMD5. LaststockID61d07dda-534d-4dfb-b1a8-6cd07de3c272; a changedID
alone is not failure if device/stock match. No new blockdev capacity/reference query is needed.
2.Preserve final8MiB xnusys as pre-logbook.bin8388608bytes. Reconfirm partition2685370368,
offset2676981760=0x9f8f8000/4096skip653560. Never manuallyclear/write. Existing720attempt means
stop rather than repeat. Prelog/rawGPT/GUID/reference bytes staylocal.
3.Record native rc,timeout status and actualreboot timestamps, nottee/echo rc or latepollorigin.

## B. Single boot, capture, recover BEFORE analysis

4.Bounded adb reboot bootloader,confirmexactfastboot/slot_a. Timeout plus observedtransport may
continue with both facts preserved; unavailabletransport needs notification, no endless retry.
5.Flash ONLYboot_a exact720image,native0+Sending/WritingOKAY; onfailure restorestock instead of
proceeding. Fastbootrebootonce; recordactualcommandtime. Monitor atmost300s fromthatreboot,
bounded individualcalls. No otherkernelquery; synchronousinit already issues its oneRC10.
6.Assoon asadbworks, BEFOREboot_completedwait, capture4198400bytes atsameoffset to
trial720-logbook.bin and exactheaderlength body. CheckHLOGv1,0<len<=4194304, fullinteger
sum(body)==64bitheadersum. Preserveoriginalraw. Captureonly, no optionalinterpretationyet.
7.Immediately bounded adb reboot bootloader/usefastboot. Timeout+absentfastboot after30s means
notifyCodex for manualfallback; do notkeeprebooting. Rehashstock,flashstockboot_aonly,reboot.
8.Verify slot_a,boot_completed1,newbootID,SELinuxEnforcing,67108864-byte boot_a/fullMD5stock,
SurfaceFlingerresponse. Distinguishautomatic/manual. Never delayrecoveryforparser/archive.

## C. Independent labels after recovery

Run review-device-720.py preservedraw/body to NEWoutput;86syntheticcases and actualHOST format
checks are not priorhardwareevidence. DIAGNOSTIC_ONLY intentionally exits1; do not turnmissing
fields intozeros or claimmissingrawdata was parsed. Preservefullresponse13words andraw2words.

IMAGE_IDENTITY: approvedhashes andactualnativeflash/rebootrecords.
QUERY_CAPTURE: exact232-byteargs/banner/fresh720guard andcollectedoutcome; refusaldiagnostic
can be captured without issuing aquery. Bootnotealone doesNOTestablishruntimequery/normalreturn.
QUERY_DATA_ACCEPTED: QUERY_OK5,claimed/issued/restored/query_valid1,polls>0,OCS0,fullresponse
valid=attempted1fff/failures0,rawvalid=attempted=changed3,tailcomplete/ok1,checked1022,
first_badffffffff,finalcleanIDLE2/sharedCLAIMED1/deferred1. Rawwordequalprefill is intentionally
unproven, even if legitimate; neverforce it. Tailcanary coversobservedlocationsonly.
CAPACITY_OBSERVATION: acceptedquery plusdecode0 andrawbigendianlastLBA/blockbytes,64bit
blocks=last+1 andcapacity=blocks*blockbytes. Detailedprintedvalues mustagreewithraw8.
ANDROID_COMPARISON: MATCH or DIFFERENT forvalidparsedgeometry versuspreviousAndroidblock-layer
127959826432bytes/4096. DIFFERENT is anindependentobservation, NOT anIOfailure orpermissionto
substituteAndroidvalues. Lastffffffff =>decode2/UNRESOLVED, no-mediumclaimforbidden; zero-block
decode3invalidgeometrybutseparatequerycompletion. NeithertriggersRC16/retry/reset/fallback.
LEASE_HELD_UNTIL_IDLE: afterquery cleancontrol2/shared1. Memoryrestoreperquery is notouterreturn.
IDLE_RETURN_RELEASE/LEASE_RETURN: actualacceptedloaderauditmagic554c5345/version1/trial720/
stateRESTORED2/allowed1 uniquelyatHLOGEOF. Quiesce iscachedatomics/sharedDRAM only. Audit is
loader-generatedsecondspan, soentirebody isnotkernel-generated. Onesavecallcanissue multiple
physicalWRITE10s. Poisoned/claimed persistednormalHLOG iscontradiction, notguardPASS.
NORMAL_RETURN: newruntimeHLOGreason0/fullintegersum separatelyfromquery/geometry.
STOCK_RECOVERY: restored64MiBMD5/slot/boot/SELinux/SurfaceFlinger andautomatic/manualmode.

NoCRC32comparison is used toclaimcapacity-byteidentity; directlydecode/reportonly8querybytes.
Sensecontents remainuninterpreted; OCS/response/status/residual/EHS/segmentrules remainbounded.
No whole-disk/userland/filesystem/throughput/gear/lane/globalownership/CPU713/GPUclaim follows.
719/718PASSand716/717NOT_PROVENunchanged. Scanpanic/dataabort/stop/guardcontext; noautotrial.

## D. Archive

Afterstockrecovery create docs/headset-wip/raw/device-ufs-capacity-720/ withthisCMD,identity,
actualnativecommands/timestamps,preflight,capture,recovery,originalraw+body andparserresult.
Only twoapprovedtrialruntimebins may be narrowlyadd-f. Prelog/GPT/GUID/reference/imagesstaylocal,
ignoreunchanged. Redactserial, GPUEBWIPunstaged. No productionpush/oldBUILDorimageoverwrite/
stash/reset/checkout/clean/gc/prune,arbitraryMMIO/getters,clock/power/reset/UIC/watchdog/OEMexpdb.
Reportlabels,discrepancies andcommit. Phone mustendstock before optionalreportwork.
