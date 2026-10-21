# ModicumFM

A 6-operator FM synth as a CLAP plugin: a hand-written CLAP entry point (`src/plugin.cpp`), the
vendored PolygraphModular engine (`src/Common`, `src/Module`, `src/Voice`), and DPF as the GUI layer
only (`src/UI`). README.md describes the synth; TESTING.md the test suites.

## Start here

The current work is porting PlainOldSynth's MIDI 2.0 / MPE / CLAP improvements, and its record is
**[docs/MIDI2-PORT.md](docs/MIDI2-PORT.md)**. Read its "Handoff" section first: it says what is done,
what is waiting on the author, and what to do next (as of 2026-10-14: every step is done, including the
Linux check, and the Pitch panel (glide + vibrato) is built and waiting on the author's host check; next is chosen with the
author from its "Open after the port" list). Keep that file current as you work: status line, the step's row, and the handoff section.

## Build and test

```sh
cmake -G Ninja -B build -S . -DCMAKE_BUILD_TYPE=Release -DMODICUMFM_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
clap-validator validate build/ModicumFM.clap
cmake --build build --target install-vst3 install-auv2   # VST3 symlink; AUv2 copy (macOS)
auval -v aumu MdFM nLKL                                  # macOS
```

Linux needs `pkg-config libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev` and
the submodules (`tools/fetch_deps.sh`, which also applies `patches/`). Updating an older Linux clone:
[docs/LINUX-SETUP.md](docs/LINUX-SETUP.md). The build symlinks the plugin into
`~/.clap` (Linux) or `~/Library/Audio/Plug-Ins/CLAP` (macOS).

## Working with the author

- Commit to `main` directly, no pull requests; **ask before pushing**.
- Ask before anything UI-visible: new parameters, panel controls, dropdown entries, layout.
- Before each commit: the ctest suite and `clap-validator` on the built `.clap`.
- After a rebuild, quit and restart the host: a running host keeps the binary it first loaded.
- Parameters are appended, never inserted or reordered (the CLAP param id is the index); see the
  port record's handoff for the full checklist.
- Do not edit the submodules (`dpf`, `external/*`); clap-wrapper fixes go in `patches/`.
