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
- [ ] Realism: per-note random pitch and index offset

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

## M4: Later
- [ ] Instrument templates: curated patches with 4–6 named macro knobs (roadmap idea 5)
- [ ] Pitch 7.25 as persistent per-key tuning table (CLAP tuning stays per-note)
- [ ] Move onto the DPF fork (erickampman/DPF) with own DGL_NAMESPACE
- [ ] VST3 / AUv2 via clap-wrapper
