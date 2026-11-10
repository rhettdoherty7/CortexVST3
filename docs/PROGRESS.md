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
- The section 6 host parameters (macros, scene, module, look, intensity, flash limiter, freeze) are
  declared but nothing reads them until Phases 2 and 3.

## Phase 0: Web lab

- [ ] Not started. Can run in parallel with Phase 2.

## Phase 2: Render engine + first modules

- [ ] Not started.

## How to build

```
powershell -ExecutionPolicy Bypass -File scripts\build.ps1 -Validate
```

Builds Release, runs the unit tests, then pluginval at strictness 10.
Hidden test tags: `cortex_tests "[.dump]"` prints per-frame detector values, `cortex_tests "[.bench]"` prints CPU cost.
