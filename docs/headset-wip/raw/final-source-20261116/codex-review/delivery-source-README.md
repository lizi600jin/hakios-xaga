# Final tested profile4/tone source snapshot

These9 files are byte-for-byte snapshots of the inputs tested with304 ASan/UBSan model
cases and used for the final trial712 driver. See MANIFEST.json / MANIFEST.sha256.
The main driver SHA256 is8c0cf1c232d0ac4f108a9ab4693559e99cb5efeecc00753c1e2bf7e96d300a02.

The source layout is preserved so the host models can run directly from this directory,
without depending on the mixed production checkout or its ignored logs:

```
python3 mtk/tools/test-headset-profile.py
python3 mtk/tools/test-headset.py
```

Python3 and a compatible clang/ASan/UBSan runtime are required. Profile output should be
PASS304 cases, baseline writes113/114 and normalized vendor events161/139. The common
PMIF core is included as the ninth file because both models need its actual implementation.
Synthetic model trim is only a host-test fixture, never a device default.

The actual device experiment used headset4, hp_tone1, hp_tone_gain31 and explicit real
trim3/17/0/3. Without hp_tone, the older fixed reference path remains. With hp_tone,
only gain codes30/31 are supported; any other explicit value is rejected before PMIF.
VAUD18 is checked, not taken over by a regulator implementation.

This is the tested driver/test-input source bundle, not a standalone XNU distribution
or complete audio stack. Kernel build provenance is the separately verified5205-file
isolation base with only headset.c and pmif.h replaced; the SDK, full base, ramdisk and
images are not included in this small portable host-model bundle.

The user confirmed headphone sound in trial712. This does not verify sample rate, bit
depth, voltage/SPL, DC offset, real silent gaps or the complete AFE/PCM/DMA path. The old
runtime phrase 'muted SGEN' is a historical diagnostic label, not a measured silence claim;
gain31 was audibly present during the final test. Keep these limits when reusing the code.

Production WIP was preserved. These explicit files are delivered through the document
repository's source archive; the production repository itself was not pushed upstream.
