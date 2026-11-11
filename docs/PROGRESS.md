# Cortex progress

Read this at the start of every session and continue from the first unchecked item.
Phases and their "done when" tests are defined in `CORTEX.md` section 12.

## Phase 1: Skeleton and analysis

- [x] JUCE CMake project: VST3 + Standalone targets build on Windows (AU target is declared for macOS)
- [ ] Builds on macOS (not tried yet: no Mac build has been run)
- [x] Audio passthrough, 0 latency, pluginval passes at strictness 10 (Windows, VST3)
- [x] `AnalysisEngine` + `AnalysisFrame` + lock-free FIFO
- [x] Kick / snare / hat detectors with sensitivity and release; Catch2 tests on four-on-floor, breakbeat and trap-hat patterns
- [x] Debug editor: hit lights, band meters, spectrum, scope, readout (plain JUCE drawing)
- [ ] **Done when:** loads in FL Studio and the hit lights match what you hear on a real Thraxx track.
      Not checked yet. Needs Rhett: install with `scripts\install.ps1`, rescan in FL Studio, play a real track.

### Measured on 2026-11-09 (Release build, this machine)

| Check | Result |
|---|---|
| Unit tests | 16 test cases, 533 assertions, all pass |
| pluginval strictness 10 | SUCCESS |
| Detector accuracy (synthetic patterns) | every expected kick, snare and hat found at 44.1 to 192 kHz and buffer sizes 32 / 333 / 4096; at most 1 extra hat in 17 |
| Detection delay after the onset | 8 to 18 ms typical, 25 ms worst case (kick landing with a snare) |
| Audio-thread CPU, 48 kHz / 256 samples | 0.54% of one core (full mix), 0.77% with pitch tracking on |

### Known gaps going into Phase 2

- Detectors are tuned on synthetic drums only. Real tracks will need the thresholds in
  `AnalysisEngine.h` (`kickOverBody`, `snareOverHat`, `hatOverNoise`) and `OnsetDetector.cpp` revisited.
- The 25 ms worst-case detection delay leaves almost nothing of the 30 ms sound-to-pixel budget for
  rendering. See D-007 for the fix to try if it shows on screen.
- "Freq Range (advanced)" and "Solo View" detector controls from section 4.2 are not built. The raw
  values Solo View needs are already exposed by `AnalysisEngine::detectorDebug()`.
- Standalone tempo detection (section 4.2) is Phase 4; until then BPM falls back to 120 without a host.
- No CI yet (section 2 asks for GitHub Actions on Windows + macOS).
- The section 6 host parameters are all declared. Since Phase 2, scene, intensity, flash limiter and
  freeze are read; macros, module and look wait for Phase 3.

## Phase 0: Web lab

- [ ] Not started. Skipped for now: modules are being written straight in GLSL and checked with the
      snapshot tool instead (see D-016).

## Phase 2: Render engine + first modules

Started before Phase 1's FL Studio check, at Rhett's request (D-012).

- [x] `gfx::Device` over JUCE OpenGLContext (OpenGL 4.1 core), shader hot reload in Debug builds
- [x] Scene / Widget / Palette / module registry, 7 factory scenes, 12 palettes
- [x] Modules 1-6: Pulse Grid, Scope, Spectrum Bars, Waveform Stack, Flowfield, Bloom Orb
- [x] FrameInterpolator (critically damped springs, frame-rate independent, idle animation)
- [x] FlashLimiter (WCAG 2.3.1 rules on a 4 x 4 grid, luminance and red flashes)
- [x] HDR pipeline: RGBA16F, 6-level bloom, AgX / ACES / Neutral tone maps, blue-noise dither
- [x] Editor shows the canvas with a scene picker, frame rate and GPU time
- [x] `cortex_snapshot` tool: renders every scene from a fixed beat to PNG and times it
- [ ] **Done when:** 60 fps at 1080p with 3 widgets on a mid-range GPU, audio thread under 1%.
      Met on this machine's integrated GPU (see below), which is weaker than the target GPUs.
      Not yet seen in FL Studio.

### Measured on 2026-11-11 (Release build, Intel Arc integrated GPU, 240 Hz display)

| Check | Result |
|---|---|
| Unit tests | 27 test cases, 934 assertions, all pass |
| pluginval strictness 10 | SUCCESS (OpenGL editor included) |
| GPU time at 1920 x 1080, one widget | 1.6 to 5.5 ms depending on module |
| GPU time at 1920 x 1080, three widgets ("Undertow") | 5.4 to 6.1 ms, under the 8 ms budget |
| Editor frame rate | 60 fps (renders every 4th refresh on the 240 Hz panel) |
| Editor GPU use, standalone, visible / minimised | about 24% / under 5% of the integrated GPU |
| Audio-thread CPU, 48 kHz / 256 samples | 0.56% of one core, 0.79% with pitch tracking |

### Known gaps going into Phase 3

- **Nothing has been looked at in a DAW yet.** Phase 1's FL Studio check is still open too.
- **One reviewer, on synthetic drums, from still frames.** The six modules have not been watched in
  motion on a real track, or compared with commercial reference footage (North Star Q5 gate 4).
- Waveform Stack lines do not hide the lines behind them (no fill), so dense stacks look busy.
- Flash limiter: in the live editor it acts one or two frames late (GPU readback without waiting).
  Offline renders (snapshot, future export) are exact. See D-020.
- Not built yet from the North Star list: MSAA (analytic anti-aliasing is used instead, D-017),
  MSDF text (glyphs are a mipmapped atlas), TAA / supersampling and motion blur (export features),
  Tracy profiling, golden-image diffs in CI, adaptive particle counts.
- Scene changes are instant: no crossfade yet.
- Widgets cannot be moved, resized or rotated from the UI; scenes are hard-coded in `engine/Scene.cpp`.
- Only 12 of the 24 factory palettes.
- The Module select, Look select and macro parameters exist but nothing reads them.
- On laptops with two GPUs the editor runs on the integrated one. Picking the discrete GPU is not handled.

## How to build

```
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Validate
```

Builds Release, runs the unit tests, then pluginval at strictness 10.

To render every scene to PNG and time it on this GPU:

```
build\tools\snapshot\cortex_snapshot_artefacts\Release\cortex_snapshot.exe --out build\snapshots
```

Set `CORTEX_STATS_LOG=<file>` before launching the standalone to log its frame rate and GPU time once a second.
Hidden test tags: `cortex_tests "[.dump]"` prints per-frame detector values, `cortex_tests "[.bench]"` prints CPU cost.
