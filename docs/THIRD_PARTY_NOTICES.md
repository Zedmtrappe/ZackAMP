# Third-party notices

| Component | Version | Used for | Licence | How it is included |
|---|---|---|---|---|
| JUCE | 8.0.9 (`f72bad64d29715216226685810c5196bd0d79d77`) | application framework: GUI, audio/MIDI devices, audio file decoding, JSON | Dual: **AGPLv3** or the commercial JUCE licence (see https://juce.com/get-juce and `third_party/JUCE/LICENSE.md`). Some JUCE modules contain code under other permissive licences (listed in JUCE's LICENSE.md). | fetched by `Scripts/bootstrap.sh` / CMake FetchContent; not committed |
| doctest | 2.4.11 | unit-test framework (tests only, not shipped in the app) | MIT (`third_party/doctest/LICENSE.txt`) | vendored single header |

**Not used:** no third-party DSP code, impulse responses, presets, samples, fonts or images. The
polyphase half-band design follows the published elliptic-filter method (as also used in Laurent de
Soras' HIIR) and the tone-stack transfer function follows Yeh & Smith (DAFx 2006); both are
re-implemented from the published mathematics. Biquad formulas follow the public-domain RBJ
Audio EQ Cookbook. The UI uses the system fonts provided by JUCE/the OS.

**Trademarks:** Fender, Marshall, Mesa/Boogie, Celestion and other names mentioned in the project
brief are trademarks of their owners. AMP FORGE's models, names, artwork and presets are original,
avoid those names in the product, and do not imply affiliation or endorsement.
