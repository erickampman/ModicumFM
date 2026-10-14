# Testing ModicumFM

The project ships a suite of in-process self-tests (via CTest) plus the CLAP conformance
validator. Everything runs headless on Linux/macOS — no DAW or audio device required.

## Prerequisites

- CMake ≥ 3.17 and a C++20 compiler (gcc or clang).
- Submodules (CLAP headers for the tests; DPF + pugl for the plugin build):

  ```sh
  git submodule update --init --recursive
  ```

- To build the plugin itself (DPF's OpenGL/NanoVG UI, the GUI layer), the X11 + GL dev packages:

  ```sh
  sudo apt install pkg-config libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev
  ```

## Build and run all tests

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Add `-DMODICUMFM_WERROR=ON` at configure time to treat warnings as errors for the framework
and the `pm-selftest` glue (this is what CI keys on). The tests that pull in vendored
PolygraphModular headers stay on that code's own warning style.

## What each test covers

Eight CTest cases exercise the `pm_framework` engine end-to-end, and `mfm-clap` drives the built
plugin through the CLAP C API (`clap-validator`, below, covers CLAP conformance):

| CTest name       | Executable               | Verifies |
|------------------|--------------------------|----------|
| `pm`             | `pm-selftest`            | UMP MIDI layer: predicates (a MIDI 2.0 velocity-0 note-on is a note-on), Min-Center-Max scaling of MIDI 1.0 values, raw-word decode, microtonal pitch attribute, CLAP→UMP decoders |
| `pm-module`      | `pm-module-test`         | `Module` + `ModuleMap` core: param read/write, process proc, map add/get |
| `pm-adsr`        | `pm-adsr-test`           | ADSR envelope: note-on drives attack, note-off drives release to ~0 |
| `pm-modules`     | `pm-modules-test`        | Combine / Final compile+run; PitchConverter maps note→pitch; ChannelPressure keeps a released MPE note's member pressure, still follows the master, and is unchanged with MPE off; the Timbre source (CCConverter, CC 74 Per Note) reads its channel, keeps a released MPE note's value and follows the master |
| `pm-factory`     | `pm-factory-test`        | ModuleFactory + the init-specs build all 10 voice modules; an FM6Op sounds on a note |
| `pm-modicumfm`         | `pm-modicumfm-test`            | The ModicumFM voice: note plays at pitch, full pitch bend = +2 semitones, channel pressure opens the filter, voice frees on release; CC 123 releases only its channel's notes (pedal-held too), CC 120 cuts its channel at once while another plays on, and keeps other channels' bends; Expression: pressure and CC 74 change nothing at amount 0, Bright ± brightens / darkens, Level +1 swells from silence and −1 ducks to silence, Tmb>Cutoff opens the filter |
| `pm-fm6op`       | `pm-fm6op-test`          | FM6Op: algorithm-table invariants, operator-role analysis, all 32 algorithms sound and stay within ±1, algorithm 4's cross-feedback reaches the sound, velocity floor + curves, Character macros (neutral identical; Brightness / Bite / Grit / Body direction), Touch (exact modulator scaling by velocity) and Key Track (darker + faster decay up the keyboard; middle C unchanged), shared envelopes (neutral identical; Loudness attack, Brightness decay/sustain, Loudness sustain on a pluck) |
| `pm-voicemanager`| `pm-voicemanager-test`   | Polyphony: a 3-note chord allocates 3 voices and frees them on release |
| `mfm-clap`       | `mfm-clap-host-test`     | The built `.clap`: ports, note dialects (MIDI 2.0 preferred), gui extension, params (ids = indexes, enums), state (incl. the legacy DPF blob and loading while active), 7.9-pitch / CLAP / MIDI 1.0 notes measured end to end; pitch bend (per-note and MIDI 1.0 channel, the Bend Range param as the per-note range, RC 7, other key / other channel left alone, mono legato) Pitch 7.25 (retune, bend on top, not persistent), and CLAP tuning note expressions (by key + note id, by note id alone, another note id left alone); NOTE_END after the release (CLAP notes only, echoing id/key/channel/port), output events in time order, and voice-info; the MPE switch (an Off/On enum; member bend ±48 when on, ±2 when off; master bend added to members) and RPN 0; released notes in the release tail (MPE member bend frozen, master bend still applies, MPE off unchanged, key-addressed per-note bend ignored, CLAP tuning by id still applies); a MIDI 2.0 velocity-0 note-on playing |

## Running a single test

```sh
ctest --test-dir build -R pm-modicumfm --output-on-failure  # by name (regex)
./build/pm-modicumfm-test                                    # or run the binary directly
```

Each test binary prints diagnostic values (e.g. `pitch: 440.8 Hz, bent 494.7 Hz`) and ends with
`ALL PASS`; CTest keys success on that string.

## CLAP conformance

Validate the built plugin against the CLAP spec with
[`clap-validator`](https://github.com/free-audio/clap-validator):

```sh
clap-validator validate build/ModicumFM.clap
```

## Continuous integration

`.github/workflows/ci.yml` runs the configure + build + `ctest` on every push across
Linux (gcc/clang) and macOS (clang) with `-DMODICUMFM_WERROR=ON`.
