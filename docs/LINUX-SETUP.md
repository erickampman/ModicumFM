# Bringing an existing Linux clone up to date

Written 2026-10-14 for moving work from the macOS machine to a Linux PC that already has an older
clone of ModicumFM. For a new clone, `git clone --recurse-submodules
https://github.com/erickampman/ModicumFM.git` and skip to step 3.

## 1. Check for local changes

```sh
git status
```

If it shows edits made on this machine, commit or stash them before pulling.

## 2. Pull, then update the submodules

```sh
git pull
git submodule update --init --recursive
```

`git pull` does not move submodules, and the `dpf` and `external/clap` pointers may have changed since
this clone was last updated.

## 3. Build in a fresh directory

```sh
rm -rf build
cmake -G Ninja -B build -S . -DCMAKE_BUILD_TYPE=Release -DMODICUMFM_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

An old `build/` may have been configured for the earlier DPF-generated plugin, or with Make instead
of Ninja. CMake refuses to switch generators in an existing build directory, so start clean.

If the build is missing headers, install the UI dependencies:

```sh
sudo apt install pkg-config libgl1-mesa-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev ninja-build cmake
```

Then validate the plugin with [clap-validator](https://github.com/free-audio/clap-validator):

```sh
clap-validator validate build/ModicumFM.clap
```

## 4. The installed plugin

The build symlinks `~/.clap/ModicumFM.clap` to the new build output. If an older ModicumFM is
installed in another CLAP folder, remove it, or the host may load that one instead. Then have the host
rescan its plugins, or restart it. A running host keeps the binary it first loaded, so restart it
after every rebuild too.

## 5. Continue the work

Start Claude Code in the repo. It reads [CLAUDE.md](../CLAUDE.md), which points to the handoff in
[MIDI2-PORT.md](MIDI2-PORT.md); the next step there is the hands-on Linux check for step L. A prompt
such as "continue the ModicumFM port" is enough.
