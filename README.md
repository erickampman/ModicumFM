# ModicumFM

A cross-platform **CLAP 6-operator FM synth** from Unlikelyware. One DX7-style FM engine per
voice, with stereo L/R detune, into a resonant filter and a chorus. Built on the vendored
PolygraphModular voice engine and a DPF/NanoVG UI kit, forked from TwinFM (itself scaffolded from
[IFSynth](https://github.com/erickampman/IFSynth)).

The longer-term goal is to hide FM's complexity behind role-based macros, a shared brightness
envelope, responsiveness controls and instrument templates. The full operator panel stays as the
expert view. See [docs/ROADMAP.md](docs/ROADMAP.md).

## Status

**Polyphonic 6-op FM synth with a full custom GUI.** The PM framework drives a `VoiceManager`
(10-voice poly with note stealing, or mono legato) rendering, per voice:

```
note pitch + pitch bend ──> FM6Op engine
  → Low-Pass Filter  ← Filter Envelope (cutoff mod 1) + Channel Pressure (cutoff mod 2)
  → Chorus ← shared LFO → Master
```

- **FM engine** (`FM6Op`):
  - The 32 DX7 algorithms, each with its own feedback path (a cross-operator loop in 4 and 6).
    Output is normalised by carrier count.
  - Coarse/fine tune, stereo **Detune** (L/R up to ±25 cents), optional 2×/4× oversampling
    (shown as Global → Quality).
  - Six operators, each with a wave (sine or a small harmonic stack), ratio, level and a
    delay-ADSR envelope, edited as a draggable graph (as is the filter envelope). The Ratio knob snaps to harmonic ratios; Shift-drag is continuous
    (the per-operator Fine param remains for presets/automation but has no knob). Decay and release are times to fall 60 dB.
  - No VCA: the carriers' envelopes are the amp envelope, and a voice frees once it falls
    silent.
  - The algorithm is shown as a diagram (click for a picker of all 32), and the panel labels
    each operator's role in the current algorithm ("out" or "mod N").
- **LPF**: 24 dB/oct Butterworth with resonance compensation. The filter envelope and channel
  pressure each move the cutoff by an adjustable number of octaves. By default the filter is
  fully open and unmodulated.
- **Chorus**: mix, rate, depth, base delay, feedback. One LFO phase is shared by every voice, so
  all notes' choruses sweep together; L and R run 90° apart.
- **Voice tab / Character**: role-based macros — Brightness (modulator levels + high carriers), Bite (a harder,
  shorter modulator burst), Grit (feedback + top modulator), Body (carrier tilt + width) — applied in the
  engine on top of the operator settings, neutral by default; hovering one rings its target
  operators in the FM Engine diagram. Shared **Loudness / Brightness envelopes** scale every
  carrier's / modulator's envelope (times ×0.01–×1000, capped at the operator limits; sustain offset) and draw a representative
  one; **Reset** returns all of it to neutral. The Operators tab (default) is the full engine.
- **Response**: velocity → level with a floor (0 / 25 / 50 / 75 % / fixed) and a curve
  (concave / linear / convex) — PolygraphModular's velocity-curve family, as fixed choices; Touch
  (velocity → modulator levels, i.e. brightness) and Key Track (note → modulator levels and
  decay/release times), both neutral at 0.
- **Global**: Poly / Mono, pitch-bend range (off … ±4 semitones), Quality, master level.
- **Presets**: factory presets in `Presets/*.json`, baked into the plugin at build time —
  classic DX-style starting points: E.Piano, Tubular Bells, Organ, Bass, Brass, Pluck, Glass Pad,
  Drowning With Chowning, plus Init. (Organ: four drawbars on algorithm 31 plus a dedicated chiff carrier.) In-UI
  help page (`Resources/help.html`).

MIDI notes are sample-accurate: `process()` splits rendering at each event's frame offset, then
renders in 64-sample control chunks, so filter-envelope and pressure modulation update about
every 1.3 ms. Parameter changes from the host are applied at their event; edits from the editor
are applied at the top of the next block.

The plugin implements the CLAP entry point itself (`src/plugin.cpp`) and keeps the
[DISTRHO Plugin Framework (DPF)](https://github.com/DISTRHO/DPF) purely as the GUI layer, so it can
receive MIDI 2.0 notes (including 7.9 microtonal pitch) that DPF's generated CLAP wrapper drops.

Next: MIDI 2.0 per-note pitch bend and expression, MPE input, and CLAP note expressions; see
[docs/MIDI2-PORT.md](docs/MIDI2-PORT.md).

## Layout

The engine (`src/`) mirrors PolygraphModular's structure. DPF is used **only as the GUI layer**
(UIExporter + NanoVG + pugl); the CLAP entry point is hand-written in `src/plugin.cpp`:

```
plugin/               the shared plugin definition
                        DistrhoPluginInfo.h (UI flags; the plugin identity macros are inert),
                        ModicumFMParams.h (shared param table, append-only, + dropdown labels)
src/plugin.cpp        the raw CLAP plugin: entry point, note/audio/params/state/gui extensions,
                        and the DSP glue (MIDI + params → VoiceManager)
src/plugin_gui.h      the C seam between plugin.cpp and the GUI shell
src/UI/               the plugin GUI (reusable NanoVG widgets + the panel)
                        Knob, RatioKnob, EnvelopeGraph, AlgorithmDiagram (view + picker), Dropdown, PopupMenu, TabBar, GroupBox,
                        Label, TitleText, Logo, HelpButton; ParamEdit.hpp (multi-param edits); PresetKeys / PresetDump (preset menu + dev-only JSON dumper);
                        ModicumFMUI.cpp — the panel, and the CLAP gui extension (UIExporter)
src/Common/           shim + MIDI + params + utilities
                        pm_compat.h (Apple→portable), pm_midi.* (spec UMP + MIDI-1 decode, incl.
                        pitch bend / channel pressure), clap_ump.h, pm_params.h / pm_param_data.* /
                        pm_addresses.h, sine.* (FM6Op harmonic tables), ResonanceComp.hpp, …
src/Module/           Module core + modules
                        FM6Op (+ FM6OpAlgorithms.hpp: the 32 algorithms + operator-role
                        analysis), LPFilter (+ ButterLow4), ADSR, Combine (pitch sum),
                        PitchConverter, PitchBendConverter, ChannelPressure, Chorus
                        (+ ModDelayLine, CascadeLPF), SharedLFO (+ LFOClockSync), Final;
                        ModuleSharedState.hpp (cross-voice state)
src/Voice/            voice engine
                        Voice.*, VoiceStateManager.*, VoiceManager.* (buildDefaultPatch wires the
                        voice), ModuleMap.*, ModuleInitSpecs.hpp, pm_module_specs.cpp, …
Presets/              factory presets (JSON), compiled into GeneratedPresets.h by
                        tools/gen_presets.py
Resources/            font, logo and help page, embedded into the binary at build time
test/                 in-process self-tests (see TESTING.md)
docs/                 ROADMAP.md (the macro / template plan), BuildNotes.md
dpf/                  DISTRHO Plugin Framework (submodule, + nested pugl) — the GUI layer only
external/clap         CLAP headers (submodule, pinned 1.2.9) — the plugin and the tests
```

## Build

DPF's OpenGL/NanoVG UI needs the usual X11 + GL dev packages (Linux):

```sh
sudo apt install pkg-config libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev
```

Then:

```sh
git clone --recurse-submodules <this-repo>
cmake -G Ninja -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure     # see TESTING.md for the full suite
```

Ninja is recommended: on macOS, Make fails when Xcode's path has a space (e.g. `Xcode 26.3.app`).

Produces `build/ModicumFM.clap` (a bundle on macOS). A post-build step symlinks it into `~/.clap` (Linux) or
`~/Library/Audio/Plug-Ins/CLAP/` (macOS), so any CLAP host picks up the freshest build after a
rescan. Validate with [`clap-validator`](https://github.com/free-audio/clap-validator). Test
details in [TESTING.md](TESTING.md).

Configure options: `-DMODICUMFM_WERROR=ON` (warnings as errors), `-DMODICUMFM_PRESET_DUMP=ON`
(dev-only DUMP button that writes the current patch as JSON to `$HOME`).

## License

MIT.
