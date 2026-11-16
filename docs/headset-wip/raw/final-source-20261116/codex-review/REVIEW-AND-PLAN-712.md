# Trial712: gain31 and physical acoustic-source localization

User authorizes another test after verification. One headphone will be beside the second
phone's microphone; the other will be near, but outside, the user's ear. The tested phone
should be as far from both headphones as its cable allows, to help separate speaker noise.

## What711 established

Codex independently verified the original HLOG: complete integer checksum, exact image/
arguments,38 on/off/restored pairs, stable shared defined bits,7 verified tone edges,
2.048s initial wait, then1.028/1.013/1.013/1.011/1.017s intervals, one clean close and36
ACCDET bytes restored. Stock recovery is verified. The user reports a loud three-part
sound during XNU but was uncertain whether it came from the headphone or phone speakers.
This is subjective audible evidence with SOURCE UNCONFIRMED, not a headphone-output PASS.

The actual recording `/home/lizi/下载/标准录音+5.mp3` is11143945 bytes, SHA256
`654c41faf6a0aa58e3ded654cbc8ad8088f8855303128b90d8542d37d955da1e`.
Codex independently decoded it and made a spectrogram plus per-window FFT. The prominent
recording segment around71.2–78.35s is continuous with changing tonal content, not three
one-second bursts separated by one-second quiet gaps. Its observed harmonic families are
approximately1.418kHz initially,2.262kHz next and3.032kHz last. The right recording channel
is much stronger than the left. These are recording observations, not calibrated codec
frequencies, source localization, SPL or output-distortion measurements.

No precise recording-clock/logbook alignment has yet been established. Do not blame MP3
or AGC without evidence. The bit6 register sequence cannot be equated with an acoustic mute.
Do not claim either that the recorded segment proves bit6 is ineffective or that an
unrelated speaker source has been excluded. Preserve this discrepancy for a later mute/
frequency experiment after identifying the physical source.

## Controlled change

The original hp_tone=1 implementation remains the default with gain30. New optional
`hp_tone_gain=31` selects only the vendor's more attenuated reference code. Only30 and31
are accepted; all other supplied values are refused before any PMIF access. Both gain
fields are checked against the selected code on every edge. The requested gate pattern,
initial wait, amp1/freq0, early close and150s kernel-return window are unchanged.

We deliberately do NOT change bit7, SDM mute bits, frequency, amplitude, routing or rate
in this trial. An audible difference cannot establish electrical attenuation from the
enum names alone. It will help identify the source under the new physical arrangement.
There is no public arbitrary-gain input or automatic level increase.

Codex passed ASan/UBSan304 cases, the legacy regression and ARM64 object compilation.
The original oracle remains161/139, baseline writes113/114. Added coverage rejects0/29/
32/UINT32_MAX before PMIF, exercises the whole gain31 pattern and injects a transient
failure at each of its7 actual additional gate writes (the gain writes are idempotent).

Exact new8-input manifest and before-session snapshots are stored beside this file.
The711 frozen tree/kernel/image and its original recording/log evidence remain preserved.
