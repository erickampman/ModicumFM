CONFIGURE

cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

BUILD (also symlinks build/bin/FeM.clap into ~/.clap via a post-build step)

cmake --build build -j

CLEAN 

cmake --build build -j --clean-first


Config Dump build:

    cd ~/dev/dev-clap/FeM
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DFEM_PRESET_DUMP=ON
cmake --build build -j"$(nproc)"
