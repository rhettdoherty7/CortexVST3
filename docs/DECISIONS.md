# Cortex decisions

Where the spec (`CORTEX.md`) was ambiguous or the build departs from it, the choice and the reason go here.

## D-001: JUCE and Catch2 come from FetchContent, not submodules
The spec's layout shows `libs/JUCE` as a submodule. Cortex instead pins JUCE 8.0.15 and Catch2 v3.16.0
with CMake `FetchContent`, exactly as Interlock does, so both projects share one setup and one local
checkout (`scripts\build.ps1` reuses Interlock's `build\_deps` when it is there).

## D-002: Visual Studio generator on Windows, not Ninja
The spec says Ninja. This machine has the VS 2022 build tools and no Ninja, and Interlock already builds
with the "Visual Studio 17 2022" generator. Nothing in the CMake files depends on the generator.

## D-003: FFT size scales with the sample rate
Spec: FFT 2048, hop 512. That is what runs at 44.1 / 48 kHz. At 88.2 / 96 kHz it is 4096 / 1024 and at
176.4 / 192 kHz it is 8192 / 2048, so the frequency resolution (23 Hz per bin) and the frame rate
(about 90 frames a second) are the same at every sample rate and the detectors need no per-rate tuning.

## D-004: Detector tests use synthesised drums, not WAV files
The spec asks for tests "on rendered test WAVs". The patterns are synthesised in memory from a fixed seed
(`tests/TestSignals.h`) instead: same coverage, deterministic, no binary files in the repo. Real-track
tuning is still the Phase 1 "done when" and has to be done by ear in FL Studio.

## D-005: Detectors work on band levels, with a neighbour-band check
Section 4.2 describes per-target spectral flux against an adaptive threshold. Implemented as:
- Flux is the rise in log-compressed band amplitude (not a per-bin sum), which is steadier on noisy bands.
- Threshold = median of the last 0.5 s x multiplier + floor, as specified, plus 30% of the recent peak
  (150 ms decay) so the tail of a hit cannot retrigger.
- A drum must out-rise the band next to it: kick vs 150-400 Hz, snare noise vs the hat band, hat vs the
  snare noise band. Without this a snare's body fires the kick and a hat's low end fires the snare.
- A kick that is a close call on its first frame waits one frame (about 10 ms) before being confirmed.

## D-006: The FFT runs on the audio thread
Section 4.3 allows this "only if it fits". Measured cost is 0.54% of one core at 48 kHz / 256 samples
(0.77% with pitch tracking), under the 1% budget, so there is no separate analysis thread. Pitch (YIN)
runs on a 12 kHz decimated copy, every other frame, and only for Bass / Vocal / Synth source types.

## D-007: Detection delay is up to 25 ms; halving the hop is the fix if it shows
With a 2048 window and 512 hop, hits are flagged 8 to 18 ms after the onset, 25 ms worst case. The
budget for sound to pixel is 30 ms. If Phase 2 shows visible lag, drop the hop to 256 (frames every
5 ms, roughly double the analysis CPU, still under 1% without pitch).

## D-008: Analysis only runs while something is reading it
Section 13: editor closed means rendering stops. The processor skips analysis entirely unless a
consumer has called `setAnalysisActive(true)` (the editor does, on open and close). Link and Record
will set the same flag when they arrive.

## D-009: `AnalysisFrame.samplePos` counts samples since `prepareToPlay`
Host sample time can jump or be missing, so frames carry the engine's own running count plus the host
PPQ. `ppq` is -1 while the host is stopped; beat and bar phase keep free-running at the last tempo so
visuals never freeze.

## D-010: The section 6 host parameters exist from day one
8 macros, scene / module / look select, intensity, flash limiter and freeze are declared in Phase 1 even
though nothing reads them yet, so the parameter list hosts see never changes. Parameter IDs are permanent.

## D-011: Plugin identity
Company "RD Audio" (from section 16), manufacturer code `Rdau`, plugin code `Crtx`, bundle id
`com.rdaudio.cortex`, VST3 category Fx / Analyzer. No MIDI input yet: it gets switched on in Phase 3
with MIDI learn.

## Open: JUCE licence
JUCE 8 is dual-licensed (AGPLv3 or a commercial licence). Selling Cortex closed-source needs a JUCE
commercial licence tier that matches RD Audio's revenue. Check before release.
