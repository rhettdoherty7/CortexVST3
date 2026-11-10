# CORTEX — Audio-Reactive Visual Engine (Build Spec)

> Drop this file in the root of the Cortex repo as `CLAUDE.md`. Claude Code reads it as the project brief.
> Owner: Rhett Doherty (Thraxx / RD Entertainment). Sibling project: **Interlock** (mixing plugin) — keep the visual language consistent.

---

## 0. How to work on this project (instructions for Claude Code)

- Build in the **phases in §12, in order**. Don't start a phase until the previous one compiles, loads in FL Studio, and passes its checklist.
- At the start of each session: read this file, check `docs/PROGRESS.md`, continue from the first unchecked item. Update `PROGRESS.md` at the end of every session.
- **The North Star section (★) is the top priority.** Quality and performance beat feature count every time.
- Prefer small, compiling commits. Never leave `main` in a non-building state.
- **Real-time safety is non-negotiable** (see §4.3). If a change touches `processBlock`, re-check the rules.
- When something in this spec is ambiguous, pick the simplest option that keeps the architecture in §3 intact, note the decision in `docs/DECISIONS.md`, and keep going.
- Everything must be original: original shader code, module names, presets, UI art. Don't copy code, assets, names, or preset content from any commercial product.

---

---

## ★ NORTH STAR — "Looks like a $100 plugin"

**This section overrides everything else.** Cortex must look and run like a premium, industry-standard product next to the top paid visualizers and pro VJ tools. A feature that isn't polished doesn't ship — cut scope before cutting quality. Fewer modules that look incredible > many modules that look average.

### Q1. Visual quality standards (every module, every layer, every export)
- **HDR pipeline**: render internally in **RGBA16F** linear light. Tonemap at the end (**AgX or ACES fitted**, user-selectable "Film / Punchy / Neutral"). Never clamp to 8-bit mid-chain.
- **Correct color**: linear-space blending, sRGB output, palettes defined in sRGB and converted to linear before use. Gradients interpolated in OKLab, not RGB (no muddy midpoints).
- **Zero banding**: blue-noise dither before the 8-bit output and before video encode.
- **Zero aliasing**: MSAA 4× for line/geometry modules (Scope, Ribbon, Harmonics, Arc), analytic anti-aliased SDF lines, mipmapped textures, **TAA or supersampling** option on export.
- **Pro bloom**: physically-based multi-mip bloom (dual-filter / Kawase down-up chain, 6+ mips, energy-conserving), never a cheap single blur.
- **Motion quality**: frame-rate-independent motion (everything uses `dt`), critically-damped spring smoothing on audio-driven values (no jitter, no twitch), optional per-pixel **motion blur** on export.
- **Particles**: GPU compute only (SSBO), 100k+ at 60 fps; soft particles, additive HDR, depth-sorted when needed.
- **Typography**: SDF/MSDF font rendering — crisp at any size and in 4K.
- **Composition**: every factory scene has intentional framing, negative space, a focal point, and a color story. No "default demo" looks. Safe areas respected for 9:16 (UI overlays of TikTok/Reels).
- **Reactivity feel**: hits must land *on* the beat (< 30 ms sound→pixel), with anticipation/overshoot easing. Reactivity that's too twitchy or too mushy is a bug.

### Q2. Export quality (what people actually post)
- Render at target res (1080p / 1440p / 4K), 30 or 60 fps, **offline and deterministic** — export quality is never limited by real-time GPU speed.
- Export-only quality boosts: 2× supersampling, max particle counts, motion blur, highest bloom mips.
- Encoding: H.264 High profile / HEVC Main10, **CRF/CQ-based high bitrate** presets tuned so YouTube/IG recompression still looks clean (e.g. YouTube 4K ≈ 45–60 Mbps, 1080p ≈ 16–20 Mbps), BT.709 tagged, AAC 320 kbps or ALAC/PCM audio, `+faststart`.
- Audio in the export is **bit-perfect** to the source file, sample-accurately aligned to frame 0.

### Q3. Optimization (non-negotiable)
- **Audio thread**: < 1% CPU per instance, zero allocations/locks (see §4.3). Bypass analysis bands nobody's using.
- **GPU frame budget @1080p60 (mid-range GPU: GTX 1660 / RX 6600 / M1)**: ≤ 8 ms total for the default scene. Each module declares its cost; UI shows a live GPU-ms meter in Debug.
- **No CPU↔GPU stalls**: persistent mapped buffers / PBO ring (3 deep) for uploads and readbacks (Spout, Syphon, export), fences instead of `glFinish`, no `glGetError` in Release.
- Zero per-frame heap allocations on the render thread; pooled render targets and textures.
- **Adaptive quality**: if frame time exceeds budget for 1 s, step down internal res (100→75→50% + upscale) and particle counts; step back up when headroom returns. Never drop below 60 fps in the editor on target hardware.
- Editor closed / plugin hidden / window minimized → rendering fully stops (0% GPU).
- Multiple instances share one GL context + shader cache (Link hub), not one context each.
- Shaders precompiled/cached on first run; no hitch when switching modules (warm-up during preset load, crossfade hides swaps).
- **Profile every phase**: Tracy (CPU + GPU zones) integrated in Debug builds. Use RenderDoc for frame captures.

### Q4. UI polish
- 60 fps UI animations, GPU-accelerated; resizes smoothly; perfect at 100–300% Windows scaling.
- Every control has hover/pressed/focus states, tooltips, double-click-to-reset, Ctrl-drag fine adjust, and value readouts.
- Live **preset thumbnails** (animated on hover) in the browser.
- Nothing ever looks "debug": no default JUCE widgets, no unstyled dialogs, no placeholder text in Release.

### Q5. Quality gates (a module/feature is "done" only if ALL pass)
1. Meets Q1 + its performance budget on target hardware (Tracy capture attached to the PR/commit notes).
2. **Golden-image tests**: deterministic render of fixed frames from a fixed test beat; CI diffs against approved references to catch visual regressions.
3. **Perf regression test** in CI: fails if GPU/CPU time grows > 10% vs last baseline.
4. Side-by-side review against reference footage from top commercial visualizers / pro motion-graphics reels: "Would someone pay $100 for this?" If not obviously yes, iterate or cut.
5. Exported 4K and 9:16 clips reviewed on a phone and on YouTube after upload (recompression check).
6. Tested on: NVIDIA, AMD, Intel iGPU (Windows), Apple Silicon (Mac).

Claude Code: when building any visual, invoke Rhett's visual skills (§1.5) and aim for the best-looking version first, then optimize to budget. Log every quality/perf trade-off in `docs/DECISIONS.md`.

---

## 1. What Cortex is

Cortex turns audio into real-time, beat-reactive visuals. It runs as:

1. **Cortex (plugin)** — VST3 (Win/Mac) + AU (Mac). Sits on any track or the master in FL Studio / Ableton / etc. Passes audio through untouched, analyzes it, and draws visuals in its editor window.
2. **Cortex Standalone** — same engine, no DAW. Drop in a beat (WAV/MP3/FLAC) or listen to an audio interface/mic. Main job: preview visuals and **export beat videos**.
3. **Cortex Link** — multiple plugin instances (kick, 808, hats, vox…) each drive their *own* visual layer, composited in one window and rendered together.

Outputs: on-screen, full-screen to a second display, **Spout / Syphon** live video to OBS / Resolume / TouchDesigner / VDMX, optional **NDI**, and **MP4 export** (up to 4K) synced to the music for Reels / TikTok / YouTube.

### Positioning (the "why")
The concept is proven (one-time-purchase audio-reactive visualizers exist at ~$29, ~$39, and ~$75–99). Cortex combines the best ideas across the category: drum-aware per-track visuals, a big module library, grid mode, layered post-FX, and live video out + export. Cortex's angle:
- **Producer-first**: per-track drum-aware reactivity (kick/snare/hat detection, not just "volume").
- **Beat-first**: built for producers making beats — tempo-locked to the project, visuals per drum/808 track, and fast export of beat videos for YouTube type beats, IG, and TikTok.
- **Scope**: Cortex is for **producing and promoting beats only**. No DJ, setlist, or live-show features. Don't add them.
- **Promo-first**: drop in cover art → auto palette → export a vertical clip with audio in one click.
- **Safety-first**: built-in photosensitive flash limiter (most competitors only put a warning in their Terms).

---

## 1.5 Using Rhett's installed Claude skills (visual R&D workflow)

Rhett has Claude skills for writing visuals in code (Three.js/WebGL, Babylon.js, PixiJS, GSAP, React Three Fiber, shader-style art). Use them like this:

1. **Design each module first in the web lab** (`lab/`): a small Vite + Three.js page that plays an audio file, runs a JS port of the analysis (Web Audio `AnalyserNode` + the same kick/snare/hat logic), and renders the module. Invoke the relevant skill (e.g. `threejs-webgl`, `pixijs-2d`) when building or iterating on a module's look here. Fast to tweak, instant preview in a browser.
2. **Write the core of every module as plain GLSL** (fragment/vertex/compute shaders with a shared uniform contract, §5.2). In the lab, Three.js only hosts the shader (`ShaderMaterial` / fullscreen quad / points). That keeps the GLSL portable.
3. **Port to the native engine**: the same `.frag`/`.vert` files drop into `shaders/` and run under the JUCE OpenGL engine. Only the host code changes, not the art.
4. Shared uniform contract for every shader: `uTime, uDt, uRes, uKick, uSnare, uHat, uRms, uFlux, uCentroid, uBands[8], uSpectrum (sampler2D 128×1), uWave (sampler2D 256×2), uBeatPhase, uBarPhase, uPalette[6], uIntensity, uSpeed, uScale, uColorShift` + module params `uP0…uP15`.

Rule: if a look can only be done with a Three.js-specific feature (its scene graph, post-processing package, etc.), rewrite it in GLSL before it ships in the plugin. The web lab is for R&D; the product is native.

> Also later possible: a free **Cortex Web** demo (browser version, 3 modules) built straight from the lab, as a marketing funnel.

## 2. Tech stack

| Area | Choice |
|---|---|
| Language | C++20 |
| Framework | **JUCE 8** (plugin + standalone wrappers, APVTS, OpenGL context) |
| Build | CMake (JUCE CMake API), Ninja; MSVC on Windows, Xcode/Clang on macOS |
| Graphics | OpenGL 4.1 core (max Mac supports) via `juce::OpenGLContext`, GLSL 410. Abstract behind `gfx::Device` so Metal/Vulkan can come later |
| DSP | `juce::dsp::FFT`, custom onset detection |
| Live video out | Spout2 SDK (Win), Syphon framework (Mac), NDI SDK (optional, Phase 6, check license) |
| Video export | Pipe raw RGBA frames to **FFmpeg** (`h264_nvenc`/`h264_amf`/`h264_qsv`/`h264_videotoolbox`, fallback `libx264`). Ship FFmpeg as a separate LGPL build; user-replaceable |
| Tests | Catch2 for DSP/analysis; `pluginval` (strictness 10) in CI |
| CI | GitHub Actions: Windows + macOS builds, pluginval, artifact upload |

Primary dev/test host: **FL Studio** (Windows). Secondary: Ableton Live, Reaper, Logic (Mac).

---

## 3. Architecture

```
┌──────────── AUDIO THREAD (processBlock) ────────────┐
│ passthrough → AnalysisEngine.push(samples)            │
│   - ring buffer → FFT (2048, hop 512, Hann)           │
│   - band energies, onsets, envelopes, pitch, stereo   │
│   - writes AnalysisFrame → lock-free SPSC FIFO        │
│   - stamps each frame with host PPQ + sample time     │
└───────────────────────────┬──────────────────────────┘
                            │ (lock-free, no alloc)
┌──────────── RENDER THREAD (OpenGL) ──────────────────┐
│ FrameInterpolator (smooth to display refresh)         │
│ ModRouter: AnalysisFrame → param modulations          │
│ Scene: Module stack → Layer FX chain → Safety limiter │
│ → screen / Spout / Syphon / NDI / Exporter            │
└───────────────────────────┬──────────────────────────┘
┌──────────── MESSAGE THREAD (UI) ──────────────────────┐
│ Editor UI, preset browser, APVTS, Link hub            │
└───────────────────────────────────────────────────────┘
```

### Source layout
```
Cortex/
  CMakeLists.txt
  CLAUDE.md
  docs/ PROGRESS.md DECISIONS.md PRESET_FORMAT.md
  libs/ JUCE/ (submodule) spout2/ syphon/ catch2/
  src/
    core/        PluginProcessor.* PluginEditor.* Params.* Version.h
    analysis/    AnalysisEngine.* OnsetDetector.* BandSplitter.* PitchTracker.* AnalysisFrame.h
    engine/      Scene.* ModRouter.* FrameInterpolator.* Palette.* Clock.*
    gfx/         Device.* Shader.* RenderTarget.* Mesh.* ParticleBuffer.*
    modules/     ModuleBase.h ModuleRegistry.* <one folder per module>
    layers/      LayerBase.h LayerRegistry.* <one file per layer>
    safety/      FlashLimiter.*
    link/        LinkHub.* LinkClient.* LinkProtocol.h
    output/      SpoutSender.* SyphonSender.* NdiSender.* VideoExporter.* FullscreenWindow.*
    standalone/  SystemAudioCapture_Win.* SystemAudioCapture_Mac.* FilePlayer.*
    ui/          LookAndFeel.* widgets/ panels/
    presets/     PresetManager.* CoverArtPalette.*
  shaders/       *.vert *.frag *.comp (embedded via BinaryData)
  resources/     fonts/ icons/ factory_presets/
  tests/
```

---

## 4. Audio analysis (the heart of it)

### 4.1 `AnalysisFrame` (POD, fixed size, ~1–2 KB)
```cpp
struct AnalysisFrame {
  double  ppq;            // host musical position (or -1 if none)
  double  bpm;            // host tempo (or detected tempo in standalone)
  int64_t samplePos;
  float   rms, peak, lufsShort;           // overall level
  float   bands[8];       // sub, bass, low-mid, mid, high-mid, presence, brilliance, air (smoothed, 0..1)
  float   spectrum[128];  // log-spaced, dB-normalized 0..1
  float   waveL[256], waveR[256];         // decimated scope data
  float   kick, snare, hat;               // onset envelopes 0..1 (fast attack, tunable release)
  bool    kickHit, snareHit, hatHit;      // single-frame triggers
  float   flux;           // spectral flux (overall "something happened")
  float   centroid;       // brightness 0..1
  float   pitchHz, pitchConf;             // monophonic pitch (YIN), for 808/bass/vocal tracks
  float   stereoWidth, correlation;       // for Lissajous-style modules
  float   barPhase, beatPhase;            // 0..1, from PPQ (tempo-locked motion)
};
```

### 4.2 Detection
- **Band splitter**: FFT bins grouped into 8 log bands; per-band attack/release smoothing (defaults 5 ms / 120 ms).
- **Onsets**: per-target spectral flux with adaptive threshold (median of last ~0.5 s × sensitivity + floor).
  - Kick: 40–120 Hz flux, min retrigger 80 ms.
  - Snare: 150–400 Hz body + 2–6 kHz noise flux, both must fire.
  - Hats: 7–16 kHz flux, min retrigger 40 ms.
  - User controls per detector: **Sensitivity**, **Release**, **Freq Range** (advanced), **Solo View** (shows a live meter + hit flashes so the user can tune it).
- **Per-track mode**: if an instance is on a single drum track, user sets **Source Type** = Full Mix / Kick / Snare / Hats / Bass / Vocal / Synth. That re-weights detectors (e.g., on a Kick track, any onset = kickHit).
- **Pitch**: YIN on a 2048 window, only when Source Type is Bass/Vocal/Synth (CPU saving).
- **Tempo**: use host BPM/PPQ when available. Standalone: onset-based tempo estimation on the loaded file + manual BPM entry/tap.

### 4.3 Real-time rules (audio thread)
- No allocation, locks, file I/O, logging, or GL calls in `processBlock`.
- Preallocate everything in `prepareToPlay`. FFT runs on the audio thread only if it fits; otherwise push raw samples to a FIFO and analyze on a dedicated analysis thread.
- Audio is **always passed through bit-identical**. Cortex never alters sound. Report 0 latency.
- Analysis must work at any buffer size (32–4096) and sample rate (44.1–192 kHz).

---

## 5. Rendering engine

### 5.1 Scene model
- A **Scene** = ordered list of **Widgets**. Each widget has: a Module, a rect (position/size/rotation on canvas), blend mode (normal/add/screen/multiply), opacity, its own Layer FX chain, and an **audio source** (this instance, or a Link channel).
- After all widgets: global Layer FX chain → **FlashLimiter** → output.
- Canvas aspect presets: 16:9, 9:16 (Reels/TikTok), 1:1, 4:5, custom. Internal render resolution independent of window size.
- **Display modes**:
  - **Single** — one full-canvas widget.
  - **Grid** — up to 36 tiles (1×1 … 6×6), each with its own module, preset, and audio response. Tiles can be **linked** (share params/seed, move together) or independent.
  - **Free** — widgets placed anywhere (default for Link sessions).

### 5.2 Module interface
```cpp
class ModuleBase {
public:
  virtual ~ModuleBase() = default;
  virtual juce::String id() const = 0;            // stable, used in presets
  virtual void declareParams(ParamSchema&) = 0;    // all params modulatable
  virtual void init(gfx::Device&) = 0;
  virtual void update(const AnalysisFrame&, const ModValues&, float dt) = 0;
  virtual void render(gfx::Device&, RenderTarget&, const Palette&) = 0;
  virtual void resize(int w, int h) {}
  virtual void release() = 0;
};
```
Every module must: meet the ★ North Star standards and its GPU budget, use the active **Palette**, and expose at least **Intensity, Speed, Scale, Color Shift** plus module-specific params.

### 5.3 Modules (original Cortex designs)
MVP = ★. Ship the ★ ones first; the rest are later phases.

| # | Name | Description | Key reactive hooks |
|---|---|---|---|
| ★1 | **Pulse Grid** | Text/glyph grid. User can type custom characters or a word | kick = density jump, hats = glyph scramble |
| ★2 | **Scope** | Phosphor oscilloscope, X/Y or time mode, glow trails | stereo wave, correlation |
| ★3 | **Spectrum Bars** | Log spectrum as bars, mirrored/radial/linear, per-band or mono color | spectrum[] |
| ★4 | **Waveform Stack** | Layered scrolling waveforms with depth | waveL/R, kick = layer push |
| ★5 | **Flowfield** | GPU particles (compute or transform feedback) driven by curl noise | bands → turbulence, kick = burst |
| ★6 | **Bloom Orb** | Soft glowing core with light streaks/flares | rms = size, snare = flare |
| 7 | **Liquid Metal** | Raymarched metaball/ferrofluid surface with spikes | bass = spike height, kick = ripple |
| 8 | **Recursion** | Shader fractal (Mandelbox/kaleido), zoom locked to bars | barPhase, centroid → detail |
| 9 | **Harmonics** | Lissajous/harmonograph curves | pitchHz → frequency ratio, width |
| 10 | **Ribbon** | Single tensioned line that whips on hits | flux, kick |
| 11 | **Arc** | Lightning strikes with branching, length by frequency | each onset spawns a strike |
| 12 | **Swarm** | 50k–100k point cloud that morphs into a word/logo (e.g. "THRAXX") | kick = scatter, release = reform |
| 13 | **Terrain** | Spectrogram as 3D topographic landscape scrolling toward camera, contour lines + note/Hz readout | spectrum history |
| 14 | **Tunnel** | Infinite tunnel, speed locked to BPM | beatPhase, kick = flash ring |
| 15 | **Cover Shatter** | Uses the loaded cover art image: displaces, slices, glitches it | kick = shatter, bands = displacement |
| 16 | **Lava** | Slow metaball blobs, lows drive motion, highs add sparkle; "Trip" knob pushes it psychedelic | bands[0–1] = flow, hats = accents |
| 17 | **Starfield** | Warp-speed stars + light strobes; can trace the outline of a loaded image/logo | kick = warp jump, rms = speed |
| 18 | **Rain Code** | Falling glyph columns, speed/density on tempo, custom font | hats = new columns, kick = flash |
| 19 | **Dead Channel** | Analog TV static, scanline tearing, signal dropout, chroma jitter | snare = dropout, flux = jitter |
| 20 | **Bitgrid** | 8-bit quantized grid, limited palette, color cycling, edge wrap | spectrum → cell heights |
| 21 | **8-Bit Arcade pack** | Several original retro-game-style mini visuals (paddle rally, invader rows, maze runner) on a CRT field. Original designs only, no real game assets/names | kick = shots/bounces |
| 22 | **Plasma Beams** | Bending light beams/plasma waves, can outline an image | bands → bend, kick = flare |
| 23 | **Wordmark** | Custom text (producer tag, beat title) that pulses & glitches; lines cycle on bars or by automation | kick = pulse, snare = glitch |
| 24 | **Color Wash** | Flat full-screen color fields/gradients (good for LED walls / lighting looks), ~12 templates | rms = brightness, bands = hue |

**Custom content (applies across modules):** drag-and-drop images (PNG/JPG/SVG) and text onto the canvas; load any `.ttf` / `.otf` font for Pulse Grid, Rain Code, Wordmark, HUD.

### 5.4 Layers (post-FX, stackable on any widget or global)
Bloom · Film Grain · Vignette · Chromatic Aberration · Scanlines/CRT · Dither (1-bit/ordered) · Exposure/Contrast · Gradient Map · Lens Warp · Pixelate · Feedback/Echo trails · Mirror/Kaleido · Glitch (block displacement) · Halftone · Edge Glow · HUD Overlay (BPM, bar counter, track name).

Grouped in the UI as:
- **Texture**: Grain, Noise, CRT, Threshold
- **Dither**: Ordered, 1-bit, plus **shape dither** (pixels replaced by custom shapes: dots, stars, hexes, petals, or any glyph from a loaded font)
- **Track**: motion-tracking overlays that find the brightest/most-moving areas and draw boxes, brackets, crosshairs, labels, terminal readouts on them (Basic, Bracket, Glow, Terminal, Data)
- **Glass**: lens refraction (single orb, 2×2, full-screen, cross, burst, petal)
- **Color / Optics**: Bloom, Vignette, Chromatic, Exposure, Gradient Map, Halftone, Edge Glow
- **Motion**: Feedback/Echo, Mirror/Kaleido, Glitch, Pixelate

Every layer param can be modulated (see §6).

### 5.5 Looks
A **Look** = saved global layer stack (one-click). Ship ~10 originals: *Club Neon, VHS Tape, Night Vision, Blueprint, Riso Print, Mono Laser, Heat Map, Glass, Ghost Trails, Terminal*.

### 5.6 Palettes
- 24 factory palettes (5 colors each + background).
- **Cover-art palette**: load PNG/JPG → k-means (k=6) in Lab space → sorted by luminance → saved as a palette + optional background image for Cover Shatter.

---

## 6. Modulation & automation

- **ModRouter**: any param ← any source. Sources: kick, snare, hat, each band, rms, flux, centroid, pitch, beatPhase, barPhase, LFO ×2 (tempo-synced), Random (S&H on beat), MIDI CC.
- Each route: amount (−1..1), curve (lin/exp/log), smoothing.
- **Host automation**: expose **8 Macro knobs** + Scene select + Module select + Look select + Intensity master + Flash Limiter as APVTS params (FL Studio can't handle hundreds of params gracefully; macros map internally to anything). Plus a "Freeze" toggle.
- **MIDI learn** on every macro and on scene switching (standalone + plugin).
- **Scene changes on the arrangement**: automate Scene select in the DAW (e.g., switch scene at the drop) with a crossfade time in beats.
- **Beat tag text**: Wordmark/HUD can show producer tag, beat title, BPM, key ("prod. Thraxx · 140 BPM · F#m") — set once, saved with the project.

---

## 7. Cortex Link (per-track visuals)

Goal: put Cortex on Kick, 808, Hats, Vox — each drives its own widget — and see/record them composited in one window.

- Each instance is a **LinkClient** with a user-named channel (defaults to the host track name if the host provides it via `updateTrackProperties`).
- One instance is the **Hub** (toggle "Main Display" in the UI; first instance opened becomes Hub by default).
- Transport: **shared memory ring buffer** per channel (`juce::MemoryMappedFile` or platform shared memory) with a small registry, so it works whether FL Studio runs plugins in-process or bridged. Clients only send `AnalysisFrame`s (tiny), never video.
- Hub UI lists live channels; user assigns each widget's source.
- **Auto Layout** button: tiles all channels' widgets in one click (grid / column / stacked full-screen with blend).
- Sync all frames on PPQ so everything lines up to the bar.

---

## 8. Outputs

### 8.1 Live
- **Fullscreen** to any display (second monitor).
- **Spout** (Win) and **Syphon** (Mac): send the final canvas, or a single widget, as a named source (`Cortex – Main`, `Cortex – Kick`…).
- **NDI** (Phase 6, optional; verify the NDI SDK license terms before shipping).

### 8.2 Video export (MP4)
Two paths:
1. **Standalone offline render (primary)**: load audio file → set resolution (1080p / 4K / 1080×1920 vertical), fps (30/60), range → render frames **faster or slower than real time, deterministically** (analysis run offline on the file, so output is identical every time) → pipe to FFmpeg with the audio muxed in. Progress bar + cancel.
2. **Plugin capture**: "Record" arms the instance; during DAW playback or offline bounce it logs `AnalysisFrame`s to a `.cxa` sidecar keyed to PPQ. In the standalone, open the `.cxa` + the bounced WAV → render the video perfectly synced. (Avoids heavy GPU encoding inside the DAW's render.) For Link sessions: one video per channel *or* one composited video.

After every export, open the **Publish Kit** (§17) so the video ships with its title, description, and tags.

Encoder: hardware first (NVENC / AMF / QSV / VideoToolbox), fallback libx264. H.264 and HEVC. Presets: "Instagram Reel", "TikTok", "YouTube 1080p", "YouTube 4K", "Loop (no audio)".

---

## 9. Safety: Flash Limiter

On by default. Measures frame-to-frame luminance change of the final output and limits large flashes to **≤3 per second** (WCAG 2.3.1-style), and caps saturated-red flashes. Modes: Off / Standard / Strict. Show the photosensitivity warning on first launch and in the export dialog. Never disable silently.

---

## 10. UI / UX

Match Interlock: **super clean, dark, minimal**, LANDR-level polish.

- Layout: big **canvas** center; left rail = **Modules** (thumbnails) and **Looks**; right rail = **Inspector** (selected widget params, mod routes, layers); bottom strip = **Analysis meters** (kick/snare/hat lights + band bars + BPM) and **transport/record/export**.
- Colors: near-black bg `#0B0B0F`, panels `#14141A`, text `#E8E8EE`, accent one electric color (default `#7C5CFF`, user-switchable). 1 px hairlines, 8 px radius, no gradients on chrome.
- Font: Inter (UI) + JetBrains Mono (numbers/HUD). Bundle as BinaryData.
- Resizable editor (min 900×560), remembers size. HiDPI correct.
- Canvas: drag to move widgets, corner handles to resize, double-click = fullscreen widget, `F` = fullscreen canvas, `Tab` = hide UI (clean view).
- Preset browser: tags (Module, Mood, Genre), favorites, search, A/B compare.
- Empty state: a demo scene runs immediately on load even before audio plays (idle animation), so it never looks broken.

---

## 11. Presets & files

- Scene preset = JSON (`.cortex`): widgets, modules + params, routes, layers, palette, canvas. Versioned schema (`"schema": 1`) with migration functions. Document in `docs/PRESET_FORMAT.md`.
- Plugin state (`getStateInformation`) = current scene JSON + APVTS XML.
- User folder: `Documents/Cortex/{Presets,Looks,Palettes,Exports,Captures}`.
- ~30 factory scenes at v1, built from the shipped modules. Include a few Thraxx/RD branded showcase scenes (e.g., Swarm forming "THRAXX").

---

## 12. Build phases

### Phase 0 — Web lab (can run in parallel with Phase 1)
- [ ] `lab/` Vite + Three.js page: audio file drop, JS analysis matching §4, uniform contract from §1.5
- [ ] Module picker + live param sliders, uses the shared `shaders/` folder directly
- [ ] Prototype ★1–★6 here first using the visual skills
- **Done when**: all six MVP modules look right on a Thraxx track in the browser.

### Phase 1 — Skeleton & analysis
- [ ] JUCE CMake project: VST3 + AU + Standalone targets, builds on Win + Mac
- [ ] Audio passthrough, 0 latency, pluginval passes at strictness 10
- [ ] `AnalysisEngine` + `AnalysisFrame` + lock-free FIFO
- [ ] Kick/snare/hat detectors with sensitivity/release; Catch2 tests on rendered test WAVs (four-on-floor, breakbeat, trap hats)
- [ ] Debug editor: meters + hit lights + spectrum (plain JUCE drawing)
- **Done when**: loads in FL Studio, hit lights match what you hear on a real Thraxx track.

### Phase 2 — Render engine + first modules
- [ ] `gfx::Device` over JUCE OpenGLContext, shader hot-reload in Debug builds
- [ ] Scene / Widget / Palette / ModuleRegistry
- [ ] Modules ★1–★6
- [ ] FrameInterpolator (smooth at 60/120/144 Hz regardless of buffer size)
- [ ] FlashLimiter
- **Done when**: 60 fps at 1080p with 3 widgets on a mid-range GPU, CPU of audio thread < 1%.

### Phase 3 — Modulation, layers, UI
- [ ] ModRouter + 8 macros + MIDI learn
- [ ] 8 core layers (Bloom, Grain, Vignette, Chromatic, Scanlines, Feedback, Glitch, Gradient Map) + Looks system
- [ ] Final UI per §10, preset browser, cover-art palette
- **Done when**: a non-technical user can pick a scene, drop cover art, and get a good-looking result in < 60 s.

### Phase 4 — Standalone + live output
- [ ] Standalone inputs: file player (drag-drop beat), interface, mic
- [ ] Tempo detect + manual BPM/tap
- [ ] Fullscreen, Spout, Syphon (for streaming beat-making sessions in OBS)
- **Done when**: drop a beat in, pick a scene, it looks right and locks to the BPM; OBS picks up the Spout feed with no dropped frames.

### Phase 5 — Export
- [ ] Offline deterministic render from audio file → FFmpeg → MP4 (H.264/HEVC, HW encoders)
- [ ] `.cxa` capture from plugin + re-render in standalone
- [ ] Export presets (Reel/TikTok/YouTube/4K)
- [ ] Publish Kit (§17): beat info form, AI titles/descriptions/tags, YouTube + SoundCloud text, thumbnail, sidecar files
- **Done when**: a 30 s 1080×1920 clip with audio exports, plays on a phone, and is frame-accurate to the beat — and the export folder has ready-to-paste YouTube + SoundCloud text.

### Phase 6 — Link + remaining modules + polish
- [ ] Cortex Link (shared memory hub), Auto Layout, per-channel export
- [ ] Modules 7–24, remaining layers (Track, Glass, shape Dither), NDI (if license OK)
- [ ] Grid mode (36 tiles, linking)
- [ ] Factory presets, onboarding, license/activation (simple offline key check, 2 machines)

### Phase 7 — Installer (see §16)
- [ ] Cortex Windows installer per §16
- [ ] Mac: signed + notarized .pkg (plain is fine for now)

---

## 13. Performance budgets

| Item | Budget |
|---|---|
| Audio thread added CPU | < 1% per instance at 48 kHz / 256 buffer |
| Analysis latency (sound → pixel) | < 30 ms |
| Render | ≤ 8 ms GPU @1080p60, 3 widgets + 4 layers, GTX 1660 / RX 6600 / Apple M1; adaptive quality keeps editor ≥ 60 fps |
| Module switch | no visible hitch (shaders pre-warmed) |
| Export | deterministic, never limited by real-time speed |
| Memory | < 300 MB per Hub, < 30 MB per Link client |
| Plugin load time | < 500 ms |

Editor closed = rendering stops (analysis keeps running only if Link/Record needs it).

---

## 14. Testing checklist (every phase)
- pluginval strictness 10 (Win + Mac)
- FL Studio: load/unload 20× fast, change buffer size live, save/reload project, multiple instances, bridged mode
- Ableton / Reaper smoke test
- Sample rates 44.1 / 48 / 96 / 192 kHz
- Close editor during playback, reopen; resize editor during playback
- GPU lost / minimized window / sleep-wake
- Flash Limiter verified with a strobe-heavy test track

---

## 15. Business notes (for Rhett, not for code)
- Comparable products sell as one-time purchases in the ~$29 (simple), ~$39 (80+ modes, grid, performance mode, free Lite tier) to ~$75–99 (pro, per-track, multi-output) range. Quality target is the **$100 tier**. Options: price at **$79–99** as a premium product, or launch at $59–69 with intro pricing. Only price premium if the North Star gates are actually met.
- Free tier idea: standalone with 3 modules, watermark on export.
- Market it with your own content: every Thraxx beat video exported through Cortex is an ad.
- Keep all names, shaders, and presets original; competing on the *concept* is fine, copying assets isn't.

---

## 16. Windows Installer — "Cortex Setup"

Goal: an installer that feels like part of the product, not a 2005 wizard. Same design language as Interlock and the Cortex UI (§10): **dark, minimal, clean, premium, one accent color, motion with purpose**. Think LANDR / Apple-level polish, not gamer RGB.

### 16.1 Approach
- **Custom installer app**, not a stock Inno/NSIS wizard. Build it as its own JUCE GUI target (`CortexSetup.exe`) that **reuses the Cortex OpenGL engine and shaders**, so the installer literally runs Cortex visuals in the background. That's the flex: the installer *is* a demo.
- Payload: the built files (VST3 bundle, standalone exe, factory presets, fonts, FFmpeg) zipped and embedded as a resource in the exe (or appended to it). One single `.exe` download.
- Manifest: `requireAdministrator` (writes to `Program Files` / `Common Files`).
- **Fallback**: if OpenGL 3.3 isn't available (old GPU, RDP, VM), drop to a static gradient + CSS-style 2D animations drawn with JUCE `Graphics`. The installer must never fail because of graphics.
- Prototype the installer screens' animations in the web lab first (§1.5) using the visual skills, then port the shaders.

### 16.2 Screens & animation (≈ 860×540, borderless, rounded 12 px, drop shadow, draggable)
Custom title bar: just a small Cortex mark left, minimize/close right (thin line icons).

1. **Intro (boot)**
   - Black screen → a single thin accent line draws across center (400 ms, ease-out) → line "breathes" into a waveform → waveform collapses into the **Cortex logo mark** (neural/brain-wave glyph) with a soft bloom.
   - Wordmark "CORTEX" fades in letter-by-letter with tracking tightening from wide to normal.
   - Subtle generative background starts: dark **Flowfield** particles at ~15% opacity, slowly drifting.
   - Built-in demo loop (short original beat snippet, muted by default, speaker toggle bottom-right). When unmuted, the background reacts to it with the real analysis engine.
   - Button: **Install** (primary, accent fill) · *Custom install* (text link).
2. **License (EULA)** — scrollable card with fade masks at top/bottom, checkbox animates a tick stroke. Keep it to one screen.
3. **Custom install** (only if chosen) — toggle cards with icon + label + path, toggles animate with a spring:
   - VST3 plugin → `C:\Program Files\Common Files\VST3\Cortex.vst3` (default on)
   - Standalone app → `C:\Program Files\RD Audio\Cortex\` (default on)
   - Factory presets & Looks → `%USERPROFILE%\Documents\Cortex\` (default on)
   - FFmpeg (needed for video export) (default on)
   - Desktop shortcut (default on)
   - Each path has a "Change" link (native folder picker). Shows required disk space, updates live.
4. **Installing** — the hero screen:
   - Center: a ring/circular **spectrum** that fills as progress goes 0→100%; each bar lights up as a file is written (driven by real progress, not fake).
   - Kick-style pulse on every completed component (VST3 done → pulse, standalone done → pulse…).
   - Under it: current step text in mono font ("Writing Cortex.vst3…"), percentage counting up, smooth (interpolated, never jumps backwards).
   - Background flowfield speeds up slightly with progress.
5. **Done**
   - Ring completes → bursts into particles that reform into the Cortex mark (Swarm module), single gentle bloom flash (respects Flash Limiter).
   - Text: "Cortex is installed." + short line "Rescan plugins in FL Studio: Options → Manage plugins → Find installed plugins."
   - Buttons: **Launch Cortex** (primary) · *Open FL Studio plugin guide* · *Close*.
6. **Error state** — no scary dialogs. Card slides up with a calm message, the reason in plain words, **Retry** and **Copy log** buttons. Log written to `%TEMP%\CortexSetup.log`.

Motion rules: all transitions 250–450 ms, ease-out-cubic/spring, screens slide horizontally with a slight parallax on background vs. card. 60 fps target. Nothing blinks or strobes. Respect Windows "reduce motion" (`SPI_GETCLIENTAREAANIMATION`) → swap to fades only.

### 16.3 Install logic (must be rock solid)
- Detect existing install/version → show "Update" or "Reinstall/Repair" instead of Install.
- Close-check: if FL Studio / Ableton / Cortex is running, ask user to close it (list the process names), with a Retry button.
- Copy files atomically (write to temp then move); on failure, roll back everything installed so far.
- Register uninstaller: `HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\Cortex` (DisplayName, DisplayVersion, Publisher "RD Audio", DisplayIcon, UninstallString, EstimatedSize).
- Start menu + optional desktop shortcut (`IShellLink`).
- **Uninstaller**: same visual style (short reverse animation: logo dissolves into particles). Option to keep or delete user presets/exports (keep by default).
- Silent mode for testing: `CortexSetup.exe /S` (no UI, exit codes).
- Never touch files outside the chosen paths; never delete user content without asking.

### 16.4 Trust & polish
- **Code signing** (Authenticode, EV or OV cert, or Azure Trusted Signing) on the installer, uninstaller, standalone, and VST3 DLL. Without it, Windows SmartScreen shows a scary "Windows protected your PC" warning — budget for this before public release.
- Embedded version info + icon (multi-size `.ico`: 16–256 px) matching the Cortex mark.
- Installer size target: < 120 MB (FFmpeg is most of it; use a trimmed LGPL build).
- Fonts: Inter + JetBrains Mono, same as the plugin.

### 16.5 Done when
- Fresh Windows 10 and Windows 11 VMs: install → FL Studio finds Cortex after rescan → standalone launches → uninstall leaves nothing behind (except kept presets).
- Runs smooth (60 fps) on a mid-range GPU and falls back cleanly on a VM with no GPU.
- Someone seeing it for the first time says "yo that's clean."

---

## 17. Publish Kit — YouTube / SoundCloud text + AI titles & descriptions

Goal: export a beat video and walk away with everything needed to post it — no typing the same description 50 times.

### 17.1 Beat Info (saved per project, filled once)
A clean form in the Export panel (and auto-filled where possible):
- **Beat name** (optional — AI can suggest one)
- **BPM** (auto from host/analysis), **Key** (auto-detect via chroma analysis, editable)
- **Genre / vibe**: trap, drill, R&B, rage, jersey, ambient… (chips + custom)
- **Mood words**: dark, bouncy, melodic, hard, emotional… (chips + custom)
- **"Type beat" artists** (e.g. up to 3 names the user types — Cortex never invents real artist names on its own)
- **Producer name / tag**: default "Thraxx" (global setting)
- **Links**: purchase/lease link, free download link, Instagram, email for business, other socials (global defaults, overridable per beat)
- **Licensing line**: e.g. "Free for non-profit use only. Credit (prod. Thraxx)." (global default template)

### 17.2 What gets generated
For **YouTube**:
- **5 title options** in different styles, user picks one or edits:
  - Type-beat SEO style: `[FREE] Artist x Artist Type Beat – "Beat Name" | Dark Trap Beat 2026`
  - Clean/artsy: `Beat Name (prod. Thraxx)`
  - Hype/creative: a punchy original name + vibe
- **Description**: hook line → beat info block (BPM, key, genre) → purchase/download links → licensing terms → socials → hashtags at the end. Timestamps optional.
- **Tags** (comma list) and **3 hashtags** for the top of the post.
- Validate limits live with counters: title ≤ 100 chars, description ≤ 5,000 chars, tags ≤ 500 chars total. Turn red when over.

For **SoundCloud**:
- Title (shorter, no "[FREE]" spam unless user wants), genre field, tags, description (shorter version with links + licensing).
- Check against SoundCloud's current field limits at build time and enforce them the same way.

Also generate:
- **Beat name ideas**: 10 original names from the vibe/mood (single words, two-word phrases, stylized).
- **On-video text**: the chosen title/tag can be pushed straight into the Wordmark/HUD layer for the video itself.
- **Thumbnail**: grab the best frame (highest visual energy near the drop) or a user-picked frame, overlay the title in the Cortex look, export 1280×720 PNG (and 1080×1080 for SoundCloud artwork).

### 17.3 AI engine
- Uses the **Claude API** (Anthropic Messages API). Model is a setting; default `claude-sonnet-5-5`, cheaper option `claude-haiku-5-5`.
- **User's own API key** entered in Settings, stored with Windows Credential Manager (DPAPI) / macOS Keychain — never in plain text, never in presets or logs.
- One request returns **structured JSON** (`titles[]`, `youtube.description`, `youtube.tags[]`, `hashtags[]`, `soundcloud.{title,description,tags[],genre}`, `beatNames[]`). Validate it with a schema; if invalid, retry once, then fall back.
- Prompt stored in `resources/prompts/publish_kit.md` (editable, versioned). It must tell the model to: use only artist names the user typed, keep text original, follow the char limits, match the user's chosen tone (Clean / Hype / SEO-heavy), and never make claims the user didn't give (no fake sales numbers, no fake placements).
- **Tone presets**: Clean, Hype, SEO-heavy, Minimal. Plus a "Make it different" re-roll button per field.
- **Offline fallback**: if no key or no internet, generate from fill-in templates (`{beatName}`, `{bpm}`, `{key}`, `{artists}`, `{links}`…) so the feature always works. Templates are user-editable.
- Network calls run on a background thread only — never audio or render thread. Timeout 30 s, cancel button, friendly error text.
- Privacy: only the Beat Info text is sent. Never send audio.

### 17.4 Output
- Panel with each field + **Copy** button + char counter. "Copy all YouTube" / "Copy all SoundCloud" buttons.
- Saved next to the video: `BeatName_youtube.txt`, `BeatName_soundcloud.txt`, `BeatName_publish.json`, `BeatName_thumb.png`.
- **History**: last generated kits saved per project; reuse your link block and licensing text across beats.
- Later (optional phase): direct upload via YouTube Data API (OAuth) and SoundCloud API, scheduled as drafts. Not in v1.

### 17.5 UI
Same design language as §10: a right-side drawer that slides in after export ("Your video's ready → Publish Kit"). Generated text types in with a quick streaming effect; the selected title glows with the accent color. Fields are clean cards, mono font for counters.
