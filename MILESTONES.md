# ModicumFM milestones

## Tests
- all: cmake --build build -j && ctest --test-dir build --output-on-failure

## M1: Core FM synth
- [x] 6-operator FM engine: algorithms, carrier/modulator roles, envelopes, VoiceManager
- [x] Chorus, LPF with key tracking, polyphony, mono/legato, glide
- [x] DPF/NanoVG custom UI: operator panels, knobs, dropdowns, tabs, presets

## M2: Hiding FM complexity (roadmap ideas 1, 2, 6)
- [x] Role-based macros: Brightness, Bite, Grit, Body (Character panel)
- [x] Shared relative envelopes: Loudness and Brightness × Attack/Decay/Release/Sustain
- [x] Responsiveness: velocity→level, Touch (velocity→modulator level), Key Track
- [x] Pitch panel: Glide (Time, Mode) and Vibrato (Rate, Depth, Wheel, Sync) — code done, awaiting host check
- [x] Realism: per-note random pitch and index offset (Character panel, 2026-11-01)

## M3: MIDI 2.0 and MPE port
- [x] Raw CLAP entry point (DPF as GUI only, MIDI 2.0 dialect available)
- [x] Per-note UMP decode; note matching on note + channel + group; RC 7 bend range; Pitch 7.25
- [x] CLAP note ids; note expressions (tuning as Pitch 7.25; pressure/brightness decoded)
- [x] `CLAP_EVENT_NOTE_END`; voice-info extension
- [x] MPE lower zone: RPN 0 bend range, released-note control freeze, MPE on/off dropdown
- [x] MIDI 1.0→2.0 Min-Center-Max upscale; MIDI 2.0 velocity-0 note-on treated as note-on
- [x] All Notes Off (CC 123) and All Sound Off (CC 120), per channel
- [x] Expression panel: Pressure and Timbre (CC 74) → Brightness / Cutoff / Level
- [x] Linux CI (Ubuntu gcc + clang with -Werror)

## Save and Load (2026-10-27)
- [x] Save / Load in the header write the patch as the factory presets' JSON and read it back (src/UI/PresetFile.hpp),
      through DPF's file dialog; MPE left out, loaded values clamped to each param's range.
      Ported from StruckGaussian. Fixed on the way:
      the dumper wrote no Vibrato section. Load also takes the factory files, which have no schema. The patch mirror is
      now always kept (Save writes it). Help page: a Presets section.
      mfm-presetfile checks the round trip, every factory preset, and non-presets
- [x] AUv2: clap-wrapper gives no timers and (unpatched) never calls gui show(), so the UI's idle never ran and DPF would never
      collect the dialog's result. The GUI now runs its own main-run-loop timer when the host has none, started at
      set_parent; the home folder comes from the account, not the sandboxed $HOME; failures show in the Preset menu
      (StruckGaussian 3f0c97c)
- [x] By hand on Linux (2026-11-01): Save and Load (DPF's own dialog)
- [x] By hand on the Mac (2026-11-01): Save and Load in Logic Pro (AUv2) and StageHand (CLAP)

## M4: Later
- [ ] Instrument templates: curated patches with 4–6 named macro knobs (roadmap idea 5)
- [ ] Pitch 7.25 as persistent per-key tuning table (CLAP tuning stays per-note)
- [x] Move onto the DPF fork (erickampman/DPF) with own DGL_NAMESPACE (2026-10-23)
- [x] VST3 / AUv2 via clap-wrapper (2026-10-21, macOS): VST3 offers Tuning, Brightness and Pressure
  note expressions via CLAP_PLUGIN_AS_VST3; AUv2 `aumu MdFM nLKL` passes auval; clap-wrapper's AUv2
  GUI-show patch carried in patches/. Checked by the author in REAPER; Linux build not yet checked
