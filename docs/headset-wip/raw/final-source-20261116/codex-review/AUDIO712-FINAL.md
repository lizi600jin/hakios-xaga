# Final recording review and accepted scope

The user explicitly confirmed headphone output in trial712 and accepted this round as a
partial audio bring-up. Sample-rate and bit-depth work is deferred at the user's request.
No further device operation is needed for this delivery.

## Recording identity and independent analysis

Original file: `/home/lizi/下载/标准录音+6.mp3`,4368212 bytes, SHA256
`4a432907aa64cf6524f9bef1a759248c9ab56ca2da25969830d56060bf0be491`.
Its MP3 metadata says109.205300s,48k stereo. That is recorder metadata, not a codec
sample-rate/bit-depth validation. Original bytes were read-only and remained unchanged.

The full spectrogram shows a weaker continuous group of changing tones around87–95s.
The initial coarse RMS threshold did not select it because its level was below the
noise-relative threshold; the empty RMS candidate list is NOT evidence of silence.
Independent per-channel Hann16384 FFT windows clearly show harmonic families around
1.567kHz at88.2/89.6s,2.063kHz at90.6/91.6s and2.783kHz at92.6/93.6s, stronger in the
right recording channel. Those are approximate recording-domain observations, not a
calibrated codec-clock, output-frequency, distortion or SPL measurement.

As with711, there is no clear repeated one-second quiet separation in this observed
segment. The7 verified register edges therefore do not establish acoustic mute gaps.
No exact recording/logbook clock alignment was established. Microphone geometry changed,
and recorder AGC was not controlled, so the two recordings do not measure gain30-vs31
attenuation, even though the new clip is digitally weaker.

Full-file spectrogram generation emitted an MP3 header/decoder warning. The relevant
84–98s crop was independently decoded with native rc0, empty stderr and exactly672000
stereo frames at the requested48k analysis rate. Preserve the warning and original MP3;
do not present the entire compressed file as a certified clean digital master.

Artifacts: audio712-identity.json, audio712-envelope.json,
audio712-independent-fft.json, audio712-spectrum.png and audio712-spectrum-84-98.png.
The original MP3 remains local rather than publishing incidental ambient audio.

## Confirmed versus open

- USER_CONFIRMED_HEADPHONE_AUDIBLE: user said '能明确确认耳机出声' with the separated
  recording/near-ear arrangement. This is direct user observation, separate from register checks.
- PROFILE_AND_EDGE_TIMING_PASS: Codex independently checked all114 register rows, shared
  defined bits, all7 selected-gain31 edges, time intervals, complete checksum and one clean close.
- STOCK_RECOVERY: slot_a, boot_completed1, Enforcing, boot_a67108864 bytes and full MD5
  a63b2e35a7c2522cb24a6a10332b9bc8. New boot_id686cb45c-3f54-481f-ba0c-5e9945f95449.
- Open: true quiet gaps, the cause of changing recorded tones, measured gain/voltage/SPL/
  DC offset, sample rate/bit depth and a complete SoC AFE/PCM/DMA playback path.

Code31 is outside the ALSA0..30 control range. Extrapolating the TLV by one step into31
is unsupported; it is not a second equally established measurement hypothesis. The actual
TLV-direction discrepancy within0..30 remains a separate vendor-source observation.
The enum labels31 as the more attenuated reference, but its real attenuation was not measured.

Final source identity: driver71087 bytes,
SHA2568c0cf1c232d0ac4f108a9ab4693559e99cb5efeecc00753c1e2bf7e96d300a02.
The exact9-file portable source/test bundle reproduces304 model cases and legacy regression.
Production WIP and all prior raw evidence are preserved. This completes the accepted round.
