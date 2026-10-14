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
| `pm`             | `pm-selftest`            | UMP MIDI layer: predicates, raw-word decode, microtonal pitch attribute, CLAP→UMP decoders |
| `pm-module`      | `pm-module-test`         | `Module` + `ModuleMap` core: param read/write, process proc, map add/get |
| `pm-adsr`        | `pm-adsr-test`           | ADSR envelope: note-on drives attack, note-off drives release to ~0 |
| `pm-modules`     | `pm-modules-test`        | Combine / Final compile+run; PitchConverter maps note→pitch |
| `pm-factory`     | `pm-factory-test`        | ModuleFactory + the init-specs build all 10 voice modules; an FM6Op sounds on a note |
| `pm-modicumfm`         | `pm-modicumfm-test`            | The ModicumFM voice: note plays at pitch, full pitch bend = +2 semitones, channel pressure opens the filter, voice frees on release |
| `pm-fm6op`       | `pm-fm6op-test`          | FM6Op: algorithm-table invariants, operator-role analysis, all 32 algorithms sound and stay within ±1, algorithm 4's cross-feedback reaches the sound, velocity floor + curves, Character macros (neutral identical; Brightness / Bite / Grit / Body direction), Touch (exact modulator scaling by velocity) and Key Track (darker + faster decay up the keyboard; middle C unchanged), shared envelopes (neutral identical; Loudness attack, Brightness decay/sustain, Loudness sustain on a pluck) |
| `pm-voicemanager`| `pm-voicemanager-test`   | Polyphony: a 3-note chord allocates 3 voices and frees them on release |
| `mfm-clap`       | `mfm-clap-host-test`     | The built `.clap`: ports, note dialects (MIDI 2.0 preferred), gui extension, params (ids = indexes, enums), state (incl. the legacy DPF blob and loading while active), and 7.9-pitch / CLAP / MIDI 1.0 notes measured end to end |

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
