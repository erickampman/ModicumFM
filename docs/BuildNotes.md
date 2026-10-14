CONFIGURE

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

BUILD (also symlinks build/ModicumFM.clap into ~/.clap via a post-build step)

cmake --build build -j

CLEAN 

cmake --build build -j --clean-first


Config Dump build:

    cd ~/dev/dev-clap/ModicumFM
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DMODICUMFM_PRESET_DUMP=ON
cmake --build build -j"$(nproc)"
