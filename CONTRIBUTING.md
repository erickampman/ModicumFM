# Contributing to ModicumFM

Bug reports and pull requests are welcome. Build steps are in the README; this page is the rules the
code depends on.

## Before each commit

```sh
ctest --test-dir build --output-on-failure
clap-validator validate build/ModicumFM.clap
```

The tests are the `pm-*` engine tests and `mfm-clap`. `mfm-clap` loads the built `.clap` and plays it headless, sending MIDI 2.0, MPE and CLAP events itself, so it needs no MIDI hardware. They are silent. On macOS, if you touched the AUv2 build, also run
`ninja -C build install-auv2` and `auval -v aumu MdFM nLKL`.

After rebuilding, quit and restart the host before listening: a running host keeps the plugin binary
it loaded first.

## Parameters: append, never insert

The CLAP parameter id is the parameter's index, and hosts save sessions and automation by id. So a
new parameter goes at the **end** of the list, and existing ones are never reordered or removed: that
would silently rewire everyone's saved projects. For each new parameter:
- add it to the `symbolIs` order guard in `plugin/ModicumFMParams.h`;
- if it shapes the sound, give it a preset key (`src/UI/PresetKeys.hpp`) and add a line for it to every `Presets/*.json`, edited as text so the files aren't reformatted.

MIDI controller setup (MPE on/off and the like) stays out of presets.

## MIDI 2.0 and MPE

MIDI 2.0 per-note messages and CLAP note expressions are the main path. MPE is accepted as a
compatibility input (an Off/On switch, lower zone); nothing in this family of projects sends MPE.

## Threads

`state_load` runs on the main thread while `process()` may be running. It stores the loaded values
only; the audio thread applies them at the start of the next block (or `activate()` does). Never
rebuild the voice graph from the main thread.

## Dependencies

The submodules are pinned (`tools/fetch_deps.sh`). Don't commit changes inside them. A fix needed in
clap-wrapper goes in `patches/` as a patch file, which `fetch_deps.sh` applies, so `external/clap-wrapper`
showing a local change is expected. DPF and pugl are Unlikelyware's forks; changes go to those repos
first and come here as a new pin.

## Third-party code and licenses

ModicumFM is MIT licensed, and contributions are taken under the same license: a pull request is
offered as MIT.

- **What ships.** [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists every third-party
  component in the builds, with its version, license and full license text, and every binary
  package includes it. A change that adds a component, or moves one to a new version, updates its
  entry in the same commit.
- **Upstream code stays as it came.** Submodules aren't edited in place: a fix goes upstream, to
  Unlikelyware's DPF or pugl fork, or into a patch file that `tools/fetch_deps.sh` applies.
  Copied-in files keep their copyright and license headers. The PolygraphModular engine in `src/` is Unlikelyware's own, MIT like the rest.
- **A new dependency** must allow MIT-licensed source and binary distribution with nothing more
  than keeping its notice: MIT, BSD, ISC, zlib, Apache-2.0 (with its NOTICE file, if it has one) or
  public domain, and the SIL Open Font License for fonts. No GPL or LGPL code.
- **Trademarks.** VST® is a registered trademark of Steinberg: the first "VST" in any document
  carries the ®, and a document that names VST ends with Steinberg's attribution line. Showing the
  VST Compatible Logo brings in the rest of [Steinberg's VST Usage Guidelines](https://github.com/steinbergmedia/vst3sdk/blob/master/VST3_Usage_Guidelines.pdf). Name
  hosts and other products plainly, to say what works with what, never so it reads as an
  endorsement.

## Drawing (NanoVG)

NanoVG fills non-convex paths through the stencil buffer and assumes it starts at zero, but DPF clears
only colour and depth. On Linux a stale stencil shows as speckles. Any widget that fills a non-convex
shape (a filled waveform, a polygon) must set `glStencilMask(0xff)` and clear the stencil before
drawing. The editor's top-level `onNanoDisplay` (`src/UI/ModicumFMUI.cpp`) does this. Convex fills (rectangles, rounded rectangles, circles) and strokes are
unaffected.

## Rules learned the hard way

- **Panels before their controls.** Declare each `GroupBox` member before the controls it parents.
  Members are destroyed in reverse order, so a panel freed first crashes its children's destructors
  when the editor closes. The host test only calls the GUI's `create()`, so only a real host catches
  this.
- **Every sound parameter in every preset.** The preset loader leaves keys a preset doesn't mention
  alone, so a missing key keeps the previous preset's value.
- **Output events stay in time order** within a block.
- **Controller setup survives `reset()`.** Configuration a controller sends once (RPN 0, registered
  controller 7) must outlive a reset: some hosts reset on transport stop.
- **Test helpers:** a full-scale bend computes 2³², which wraps to 0 in a `uint32_t`; clamp it.
- **Pitch tests:** measure on the simplest waveform you can. Zero crossings with interpolation are
  exact; autocorrelation needs peak interpolation.
- **A new module in the voice** needs `pm_module_specs.cpp` (with its Reset proc),
  `pm_addresses.h`, `CMakeLists.txt` and `VoiceManager::buildDefaultPatch`. Engine items and modules
  are appended too, and `pm-factory` counts the voice's modules.

## Platform-specific code

- `src/UI/BackingScale_mac.mm` (Retina scale) and the AUv2 target are macOS-only.

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
                        PitchConverter, Slew (glide), PitchBendConverter, ChannelPressure,
                        CCConverter (timbre, mod wheel), Chorus
                        (+ ModDelayLine, CascadeLPF), SharedLFO (+ LFOClockSync), Final;
                        ModuleSharedState.hpp (cross-voice state)
src/Voice/            voice engine
                        Voice.*, VoiceStateManager.*, VoiceManager.* (buildDefaultPatch wires the
                        voice), ModuleMap.*, ModuleInitSpecs.hpp, pm_module_specs.cpp, …
Presets/              factory presets (JSON), compiled into GeneratedPresets.h by
                        tools/gen_presets.py
Resources/            font, logo and help page, embedded into the binary at build time
test/                 in-process self-tests (see TESTING.md)
docs/                 ROADMAP.md (the macro / template plan)
dpf/                  DISTRHO Plugin Framework (submodule, + nested pugl) — the GUI layer only
external/clap         CLAP headers (submodule, pinned 1.2.9) — the plugin and the tests
```

VST is a registered trademark of Steinberg Media Technologies GmbH.
