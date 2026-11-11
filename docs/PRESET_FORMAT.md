# The `.cortex` preset format

A Cortex preset is one UTF-8 JSON file with the `.cortex` extension. User presets live in
`Documents/Cortex/Presets`; favourites are listed in `Documents/Cortex/favourites.json`. The same JSON
is stored in the plugin's saved state (both A/B slots), so a project reopens exactly as it was left.

Code: `src/presets/PresetFormat.cpp` (reading and writing), `src/presets/PresetManager.cpp` (the library).
Tests: `tests/PresetTests.cpp`.

## Rules

- `schema` is required. This document describes **schema 1**.
- A file with a higher schema than this build knows is refused with a message, never half-loaded.
  When the schema changes, `presetFromVar` gets a migration step from each older schema.
- Every number is clamped to its range on load, so a hand-edited file cannot break the renderer.
- Unknown keys are ignored. A widget whose `module` this build does not have is skipped; the rest of
  the preset still loads. A file with no drawable widget is refused.
- Names written to files (modules, blend modes, layer types, sources, targets) are permanent.

## Example

```json
{
    "schema": 1,
    "name": "Undertow",
    "tags": ["Flowfield", "Bloom Orb", "Dark", "Trap"],
    "look": "Club Neon",
    "background": 6,
    "tonemap": "film",
    "exposure": 1.0,
    "bloom": 0.12,
    "crossfadeBeats": 1.0,
    "coverPalette": ["#043741", "#773C3D", "#B2613A", "#3DB0B0", "#FEDB77", "#080008"],
    "widgets": [
        {
            "module": "flowfield",
            "rect": [0.0, 0.0, 1.0, 1.0],
            "blend": "normal",
            "opacity": 1.0,
            "intensity": 1.0,
            "speed": 1.0,
            "scale": 1.0,
            "colorShift": 0.0,
            "palette": 6,
            "params": [0.35, 0.4, 0.7, 0.35, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0],
            "layers": [
                { "type": "vignette", "amount": 0.4, "params": [0.5, 0.5, 0.5, 0.5] },
                { "type": "gradientMap", "amount": 0.6, "params": [0.5, 0.5, 0.5, 0.5], "palette": 9 }
            ]
        }
    ],
    "routes": [
        { "source": "kick", "target": "widgetScale", "widget": 0, "index": 0,
          "amount": 0.15, "curve": "linear", "smoothing": 0.03 }
    ]
}
```

## Fields

### Scene

| Key | Type | Range / values | Default |
|---|---|---|---|
| `schema` | int | 1 | required |
| `name` | string | | "Untitled" |
| `tags` | string[] | free text; the browser filters by them | [] |
| `look` | string | a factory Look name, or "None" | "None" |
| `background` | int | factory palette index, 0..11 | 0 |
| `tonemap` | string | `film` (AgX), `punchy` (ACES fitted), `neutral` (Khronos PBR Neutral) | `film` |
| `exposure` | number | 0.25..4 | 1 |
| `bloom` | number | 0..0.5 | 0.08 |
| `crossfadeBeats` | number | 0..16; 0 cuts straight to the scene | 1 |
| `coverPalette` | string[6] | `#RRGGBB` x 6: five colours dark to bright, then the background | absent |
| `widgets` | object[] | drawn in order, later on top | required, at least one |
| `routes` | object[] | | [] |

### Widget

| Key | Type | Range / values | Default |
|---|---|---|---|
| `module` | string | `bloom_orb`, `spectrum_bars`, `scope`, `waveform_stack`, `flowfield`, `pulse_grid` | required |
| `rect` | number[4] | x, y, width, height as fractions of the canvas, origin top-left | [0, 0, 1, 1] |
| `blend` | string | `normal`, `add`, `screen`, `multiply` | `normal` |
| `opacity` | number | 0..1 | 1 |
| `intensity` | number | 0..2 | 1 |
| `speed` | number | 0..4 | 1 |
| `scale` | number | 0.25..3 | 1 |
| `colorShift` | number | 0..1 (one full turn of the palette) | 0 |
| `palette` | int | factory palette index | 0 |
| `params` | number[16] | module parameters; meanings in `src/engine/ModuleInfo.cpp` | the module's defaults |
| `layers` | object[] | this widget's own effects, run in order | [] |

### Layer

| Key | Type | Range / values |
|---|---|---|
| `type` | string | `grain`, `vignette`, `chromatic`, `scanlines`, `feedback`, `glitch`, `gradientMap` (`bloom` only in Looks) |
| `amount` | number | 0..1; 0 switches it off |
| `params` | number[4] | 0..1; names in `layerParamNames()` (`src/engine/Layers.cpp`) |
| `palette` | int | optional: a fixed palette for `gradientMap` that ignores cover art |

### Route

| Key | Type | Range / values |
|---|---|---|
| `source` | string | `kick`, `snare`, `hat`, `band1`..`band8`, `rms`, `flux`, `centroid`, `pitch`, `beatPhase`, `barPhase`, `lfo1`, `lfo2`, `random`, `macro1`..`macro8` |
| `target` | string | `widgetIntensity`, `widgetSpeed`, `widgetScale`, `widgetColorShift`, `widgetParam`, `sceneBloom`, `sceneExposure`, `fxFeedback`, `fxGlitch`, `fxChromatic` |
| `widget` | int | -1 for every widget, else a widget index |
| `index` | int | `widgetParam` only: which of the 16 params |
| `amount` | number | -1..1, a fraction of the target's range |
| `curve` | string | `linear`, `exponential`, `logarithmic` |
| `smoothing` | number | seconds, 0..2 |

The 8 macros' own routes are fixed (docs/DECISIONS.md D-024) and are not stored in presets.
