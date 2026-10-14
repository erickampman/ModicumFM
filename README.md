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
- **Global**: Poly / Mono, MPE (off / on), pitch-bend range (off … ±4 semitones), Quality, master level.
- **MIDI 1.0 values** are upscaled with the spec's Min-Center-Max rule (M2-104 D.1.3), so 127 /
  16383 are full scale and 64 / 8192 the exact centre; MIDI 1.0 poly pressure decodes. A MIDI 2.0
  note-on with velocity 0 is a note-on (M2-104 §4.2.8), not a note-off; at the 0% Vel Floor it
  plays silently.
- **MIDI 2.0 pitch** (from PlainOldSynth, via IFSynth): notes match on note + channel + group, so the
  same key on two channels is two notes, and per-note messages reach only the note they name.
  - **Channel bend** is tracked per channel; each note follows its own channel's.
  - **Per-note pitch bend** (UMP 0x6) uses the channel's range (the Bend Range param) unless
    **registered controller 7** (per-note bend sensitivity) sets one for that channel. M2-104 v1.0
    §4.2.12: per-note bend "acts like Pitch Bend in every way". RC 7 is from a later revision.
  - **Pitch 7.25** (registered per-note controller #3) sets the sounding note's absolute pitch, and
    both bends offset from it. It lasts for that note only (no tuning table).
  - Bend changes ramp over 2 ms; a new note starts at its bent pitch, and a mono legato note ramps.
    RC 7 survives `reset()` (configuration a controller sends once; some hosts reset on stop).
- **All Notes Off (CC 123)** releases the notes of the channel it arrives on, pedal-held ones
  included; they go into their release as after a note-off. **All Sound Off (CC 120)** silences
  that channel's voices at once, release and chorus tail included, without forgetting other
  channels' bend and pressure.
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

**CLAP.** Note ids are kept: when a host supplies them (Bitwig does), they decide which note is
which, so a re-struck key is a new note and an expression can name only the id. Note expressions:
**tuning** arrives as Pitch 7.25 at key + tuning, so it is exact at any size, not clamped to the
bend range; **pressure** becomes polyphonic pressure, which moves that note's filter cutoff like
aftertouch; **brightness** becomes per-note controller #74 (nothing in the voice responds to it yet). A CLAP note gets `CLAP_EVENT_NOTE_END` when its voice
falls silent at the end of the carriers' release (or is stolen, or leaves the mono stack), not at
note-off, so Bitwig keeps its per-voice modulation running through the release. The voice-info
extension reports 10 voices (1 in Mono) and supports overlapping notes on one key.

**MPE** (lower zone), switched on in the Global box; off by default, and presets leave it alone
since it describes the controller, not the sound. Member channels 2–16 bend ±48, and the master
channel's bend (the Bend Range param) adds to every member note. **RPN 0** (MIDI 1.0 CC
101/100/6/38, or the MIDI 2.0 registered controller) sets a channel's bend range; under MPE, any
member sets all members. Ranges reset on the MPE Configuration Message and when the switch flips,
and like RC 7 they survive `reset()`. Pressure is tracked per channel (and per note, for
polyphonic pressure), so under MPE each note's own pressure moves its own filter; a member note
takes the higher of its own and the master channel's. **Released notes** (MPE §3.3.3: control of a
note ceases once Note Off has occurred): a ringing MPE note keeps the member-channel bend and
pressure it had at note-off, and follows only the master channel; key-addressed MIDI 2.0 per-note
messages stop reaching it; a CLAP note addressed by its id keeps taking expression until
NOTE_END. With MPE off a pitch wheel and aftertouch still move releasing notes. Timbre (CC 74) is
not routed yet.

Next: pressure and timbre as modulation sources; see
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
