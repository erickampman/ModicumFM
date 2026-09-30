# TwinFM

A cross-platform **CLAP dual 4-operator FM synth** from Unlikelyware. Each voice runs two
independent FM engines, mixed in stereo and shaped by a resonant filter, an amp envelope and a
chorus. Built on the vendored PolygraphModular voice engine and a DPF/NanoVG UI kit, scaffolded
from [IFSynth](https://github.com/erickampman/IFSynth).

## Status

**Polyphonic dual-FM synth with a full custom GUI.** The PM framework drives a `VoiceManager`
(10-voice poly with note stealing, or mono legato) rendering, per voice:

```
note pitch + pitch bend ─┬─> FM Engine A ─┐
                         └─> FM Engine B ─┴─> Mixer (level + pan each)
  → Low-Pass Filter  ← Filter Envelope (cutoff mod 1) + Channel Pressure (cutoff mod 2)
  → VCA × Amp Envelope → Chorus ← shared LFO → Master
```

- **FM engines (×2)**: PMU's FM4Op — 8 algorithms, top-operator feedback, coarse/fine tune,
  stereo detune, optional 2×/4× oversampling. Four operators each with wave (sine or small
  harmonic stack), ratio, fine, level and a delay-ADSR envelope. The panel shows one engine at a
  time (A | B tabs); both always play.
- **Mixer**: level and pan per engine.
- **LPF**: 24 dB/oct Butterworth with resonance compensation; filter envelope and channel
  pressure each move the cutoff by an adjustable number of octaves.
- **Amp envelope** (VCA) and **chorus** (mix, rate, depth, base delay, feedback). One LFO phase
  is shared by every voice, so all notes' choruses sweep together; L/R run 90° apart.
- **Global**: Poly / Mono, pitch-bend range (off … ±4 semitones), master level.
- **Presets**: factory presets in `Presets/*.json`, baked into the plugin at build time; in-UI
  help page (`Resources/help.html`).

MIDI notes are sample-accurate: `run()` splits rendering at each event's frame offset, then
renders in 64-sample control chunks so envelope and pressure modulation of the filter update
about every 1.3 ms. Parameter changes are applied per host block (DPF applies them before
`run()`).

The wrapper is built with the [DISTRHO Plugin Framework (DPF)](https://github.com/DISTRHO/DPF),
which generates the CLAP around our DSP + UI subclasses.

## Layout

The engine (`src/`) mirrors PolygraphModular's structure; the DPF plugin assembly lives in
`plugin/` + `src/UI/`:

```
plugin/               the DPF plugin definition (the CLAP wrapper is generated from these)
                        DistrhoPluginInfo.h (plugin flags), TwinFMParams.h (shared param
                        table), TwinFMPlugin.cpp (DSP: MIDI + params → VoiceManager)
src/UI/               the plugin GUI (reusable NanoVG widgets + the panel)
                        Knob, Dropdown, PopupMenu, TabBar, GroupBox, Label, TitleText, Logo,
                        HelpButton; PresetKeys / PresetDump (preset menu + dev-only JSON dumper);
                        TwinFMUI.cpp — the panel
src/Common/           shim + MIDI + params + utilities
                        pm_compat.h (Apple→portable), pm_midi.* (spec UMP + MIDI-1 decode, incl.
                        pitch bend / channel pressure), clap_ump.h, pm_params.h / pm_param_data.* /
                        pm_addresses.h, sine.* (FM4Op harmonic tables), ResonanceComp.hpp, …
src/Module/           Module core + modules
                        FM4Op, QuadStereoMixer, LPFilter (+ ButterLow4), ADSR, Combine (VCA and
                        pitch sum), PitchConverter, PitchBendConverter, ChannelPressure, Chorus
                        (+ ModDelayLine, CascadeLPF), SharedLFO (+ LFOClockSync), Final;
                        ModuleSharedState.hpp (cross-voice state)
src/Voice/            voice engine
                        Voice.*, VoiceStateManager.*, VoiceManager.* (buildDefaultPatch wires the
                        voice), ModuleMap.*, ModuleInitSpecs.hpp, pm_module_specs.cpp, …
Presets/              factory presets (JSON), compiled into GeneratedPresets.h by
                        tools/gen_presets.py
Resources/            font, logo and help page, embedded into the binary at build time
test/                 in-process self-tests (see TESTING.md)
dpf/                  DISTRHO Plugin Framework (submodule, + nested pugl) — generates the .clap
external/clap         CLAP headers (submodule, pinned 1.2.9) — used by the framework self-tests
```

## Build

DPF's OpenGL/NanoVG UI needs the usual X11 + GL dev packages (Linux):

```sh
sudo apt install pkg-config libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev
```

Then:

```sh
git clone --recurse-submodules <this-repo>
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure     # see TESTING.md for the full suite
```

Produces `build/bin/TwinFM.clap`. A post-build step symlinks it into `~/.clap` (Linux) or
`~/Library/Audio/Plug-Ins/CLAP/` (macOS), so any CLAP host picks up the freshest build after a
rescan. Validate with [`clap-validator`](https://github.com/free-audio/clap-validator). Test
details in [TESTING.md](TESTING.md).

Configure options: `-DTWINFM_WERROR=ON` (warnings as errors), `-DTWINFM_PRESET_DUMP=ON`
(dev-only DUMP button that writes the current patch as JSON to `$HOME`).

## License

MIT.
