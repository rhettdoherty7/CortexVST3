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

## D-012: Phase 2 started before Phase 1's FL Studio check
Section 0 says not to start a phase until the previous one loads in FL Studio and passes its checklist.
Rhett asked for the visual work to start anyway. Phase 1's "done when" is still open in PROGRESS.md.

## D-013: Geometry comes from the vertex shader
There are no vertex buffers. Fullscreen passes use one triangle from `gl_VertexID`; lines are instanced
quads that fetch their endpoints from the wave textures; particles are points that read the simulation
texture. Less code, nothing to upload per frame.

## D-014: Additions to the shared uniform contract
Section 1.5's contract is kept. Added: `uPaletteLab[6]` (the palette in OKLab, so shaders can mix in
OKLab cheaply), `uSinceKick / uSinceSnare / uSinceHat`, `uKickCount / uSnareCount / uHatCount`,
`uMotion` (time that runs faster when the music is louder), `uWaveHistory`, `uSpectrumHistory`
(64 rows, eight per beat) and `uHistoryPhase`. Module parameters are the array `uP[16]` rather than
sixteen separate names. Everything is declared once in `shaders/common.glsl`.

## D-015: Module interface differs from section 5.2
`update()` and `render()` are one call, parameters arrive as `WidgetSettings` each frame, and shaders
come from a cache in the renderer that every widget shares. One module object per widget, so a module
can keep its own trails or particles. `declareParams` waits for the Phase 3 inspector.

## D-016: Web lab (Phase 0) skipped for now
The modules were written directly in GLSL 410 and judged with the `cortex_snapshot` tool, which renders
the real engine to PNG. That removes the port step the lab exists to de-risk. The lab is still useful
for quick iteration in a browser and as the future Cortex Web demo.

## D-017: Analytic anti-aliasing instead of MSAA for lines
North Star Q1 asks for MSAA 4x on line modules. Scope and Waveform Stack draw each segment as a capsule
whose edge coverage is computed in the shader, with a soft glow, blended with MAX so joints have no
bright beads. Clean edges without multisample targets. MSAA can still be added if needed.

## D-018: Particles run as a fragment-shader simulation
North Star Q1 asks for compute shaders (SSBO). OpenGL 4.1 on macOS has no compute, so the 102,400
Flowfield particles live in an RGBA32F texture updated by a fragment shader each frame and drawn as
points. Same GPU-only result, works on every target.

## D-019: The editor renders at about 60 fps, not every refresh
On this machine's 240 Hz display the canvas rendered at 240 fps and used about 80% of the integrated
GPU. It now renders every Nth refresh with N = floor(refresh / 60): 60 fps at 60, 120 and 240 Hz,
72 at 144 Hz, 82 at 165 Hz. Always at least 60 and always an even cadence, so no judder.
A user setting for higher frame rates can come later.

## D-020: The live flash limiter is one or two frames late
The limiter needs the displayed picture's luminance back on the CPU. Waiting for it would stall the GPU
(North Star Q3), so the live editor reads it through a ring of three pixel buffers and uses whatever has
finished. A flash can therefore show for one or two frames (up to about 33 ms) before it is held.
Offline renders wait for the result and are exact. Measurement uses a 4 x 4 grid; a flash must cover a
quarter of the picture, as WCAG defines it.

## D-021: Default scope mode is time, not X/Y
X/Y mode draws mono content as a single vertical line, and most beats have a mono kick and 808, so the
factory "Phosphor" scene uses the triggered time-domain trace. X/Y stays available.

## D-022: Tone maps
Film = AgX (fitted), Punchy = ACES fitted (Hill), Neutral = Khronos PBR Neutral. Each scene picks one;
all factory scenes use Film for now.

## D-023: Effects run on whole frames, not on a node graph
Each widget and the scene have an ordered list of layers, run as fullscreen passes between two ping-pong
targets. A layer at amount 0 is skipped, so unused effects cost nothing. Feedback keeps its own history
target. Bloom is not a pass: the Bloom layer raises the renderer's bloom strength and threshold.

## D-024: The 8 macros have fixed jobs
Section 6 says macros "map internally to anything". Until the inspector can edit routes, each macro has
one fixed job on every scene: 1 Intensity, 2 Speed, 3 Scale, 4 Color (palette rotation), 5 Bloom,
6 Trails, 7 Glitch, 8 Aberration. The last three drive effect slots that sit at zero in every scene,
so turning a macro up always does something visible and turning it down returns to the scene as
designed. Macro 4 stops short of a full turn so its maximum never looks the same as zero.

## D-025: Each widget keeps its own clock
Speed (and the Speed macro) changes how fast a widget's clock advances, instead of multiplying a shared
clock. Multiplying made everything jump whenever Speed moved.

## D-026: MIDI learn goes through atomics and a message-thread timer
The audio thread only records which controller moved and its value; a 30 Hz timer on the message thread
turns that into a parameter change. That keeps the audio thread free of locks and host calls. One
controller drives one slot. Scene select spreads the controller's range over the factory scenes.
The plugin now declares MIDI input, which hosts show as an event input on the effect.

## D-027: Cover-art palette
Section 5.6: k-means with k = 6, in OKLab (a Lab space with better hue behaviour than CIELAB), on a
96 x 96 copy of the image, seeded from a fixed number so the same image always gives the same palette.
The darkest cluster becomes the background and is pushed to near black; the other five keep their hue
and chroma but have their lightness spread from 0.30 to 0.90, so every visual gets a full gradient.
It replaces every widget's palette, except layers that fix their own (Night Vision, Heat Map...).

## D-028: Looks shipped
Club Neon, VHS Tape, Night Vision, Blueprint, Mono Laser, Heat Map, Ghost Trails, Terminal. Riso Print
and Glass need Halftone and Glass layers, which are Phase 6 work.

## D-029: Fonts are Instrument Sans and JetBrains Mono, shared with Interlock
Section 10 names Inter. Interlock already ships Instrument Sans (text), JetBrains Mono (numbers) and
Unbounded (display), all SIL OFL, so Cortex embeds the same files: one typographic voice across the two
products and no new download. See resources/fonts/README.md.

## D-030: The processor owns the scene; the GL thread copies it on change
The scene (with its Look and cover palette) is a `ScenePreset` held by the processor in two A/B slots
behind a spin lock, with a version counter. The interface edits it on the message thread; the canvas
copies it only when the version moves. Edits that keep the same widgets, modules and layer types are
applied in place, so trails and particles carry on while a slider moves; anything else reloads with a
crossfade. Scene select (automation, MIDI) still loads factory scenes, but only when that parameter
actually changes, so restoring a project never overwrites its saved scene.

## D-031: Nothing is drawn over the canvas
The OpenGL canvas is a native child window on Windows, so ordinary components cannot draw on top of
it. The selection outline is drawn by the renderer itself (editor-only, never in renders), the preset
browser replaces the canvas while it is open (which also stops rendering), and errors show in the top
bar instead of over the picture.

## D-032: Module thumbnails are rendered by the engine and committed
`cortex_snapshot --thumbs resources/thumbnails` renders each module alone at the moment a kick lands.
The PNGs are committed and embedded, so the rail shows the real engine's output. Re-run it when a
module's look changes.

## Open: JUCE licence
JUCE 8 is dual-licensed (AGPLv3 or a commercial licence). Selling Cortex closed-source needs a JUCE
commercial licence tier that matches RD Audio's revenue. Check before release.
