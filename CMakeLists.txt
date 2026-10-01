cmake_minimum_required(VERSION 3.17)

# C is needed for DPF's pugl / dgl sources.
project(ModicumFM VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)          # PM framework uses std::numbers / std::span
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MODICUMFM_WERROR "Treat warnings as errors (framework + tests)" OFF)
option(MODICUMFM_PRESET_DUMP "Dev-only: press 'P' in the UI to dump the patch as JSON to \$HOME" OFF)

# --- PM framework port -----------------------------------------------------
# Vendored PolygraphModular framework + our shim/MIDI layer, as a static lib. Compiled with
# relaxed warnings (vendored code, its own style); our glue and tests stay strict. PIC so it
# can be linked into the shared .clap module.
add_library(pm_framework STATIC
    src/Common/pm_midi.cpp
    src/Common/pm_param_data.cpp
    src/Module/LPFilterModule.cpp
    src/Module/ADSRHelper.cpp
    src/Module/ADSRModule.cpp
    src/Module/CombineModule.cpp
    src/Module/FinalModule.cpp
    src/Module/PitchConverterModule.cpp
    src/Common/sine.cpp
    src/Module/FM6OpModule.cpp
    src/Module/ChorusModule.cpp
    src/Module/SharedLFOModule.cpp
    src/Module/PitchBendConverterModule.cpp
    src/Module/ChannelPressureModule.cpp
    src/Module/ModuleFactory.cpp
    src/Voice/ModuleMap.cpp
    src/Voice/pm_module_specs.cpp
    src/Voice/VoiceStateManager.cpp
    src/Voice/Voice.cpp
    src/Voice/VoiceManager.cpp)
set_target_properties(pm_framework PROPERTIES POSITION_INDEPENDENT_CODE ON)
# All framework folders on the include path so the vendored `#include "X.hpp"` resolve
# across Module/ Voice/ Common/ (mirrors PolygraphModular's layout).
target_include_directories(pm_framework PUBLIC src/Common src/Module src/Voice)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(pm_framework PRIVATE -Wall -Wno-unknown-pragmas -Wno-unused-parameter)
endif()

# --- presets: Presets/*.json compiled in as a C++ array, not loaded at runtime -------------
# Avoids any runtime JSON parser in the plugin and doesn't depend on CLAP's own preset-discovery
# extension — the UI presents its own dropdown. Adding/removing a preset file requires
# reconfiguring (CONFIGURE_DEPENDS re-globs on build, but a brand new file still needs `cmake -B
# build -S .` rerun once to pick up the new match).
find_package(Python3 COMPONENTS Interpreter REQUIRED)
file(GLOB MODICUMFM_PRESET_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Presets/*.json")
set(MODICUMFM_GENERATED_PRESETS_H "${CMAKE_BINARY_DIR}/generated/GeneratedPresets.h")
add_custom_command(
    OUTPUT "${MODICUMFM_GENERATED_PRESETS_H}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/gen_presets.py"
            ${MODICUMFM_PRESET_FILES} -o "${MODICUMFM_GENERATED_PRESETS_H}"
    DEPENDS ${MODICUMFM_PRESET_FILES} "${CMAKE_SOURCE_DIR}/tools/gen_presets.py"
    COMMENT "Generating embedded presets from Presets/*.json")
add_custom_target(generate_presets DEPENDS "${MODICUMFM_GENERATED_PRESETS_H}")

# --- embedded resources: Resources/* compiled in as raw bytes ----------------------------
# Uses DPF's own res2c.py (ships in the dpf/ submodule) to embed the logo PNG's raw file bytes;
# NanoVG's createImageFromMemory() decodes it at UI-construction time. No filesystem dependency
# on the resource file once built — same reasoning as the preset codegen above.
set(MODICUMFM_RESOURCES_HPP "${CMAKE_BINARY_DIR}/generated/EmbeddedResources.hpp")
set(MODICUMFM_RESOURCES_CPP "${CMAKE_BINARY_DIR}/generated/EmbeddedResources.cpp")
file(GLOB MODICUMFM_RESOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/Resources/*")
add_custom_command(
    OUTPUT "${MODICUMFM_RESOURCES_HPP}" "${MODICUMFM_RESOURCES_CPP}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/dpf/utils/res2c.py"
            EmbeddedResources "${CMAKE_SOURCE_DIR}/Resources" "${CMAKE_BINARY_DIR}/generated"
    DEPENDS ${MODICUMFM_RESOURCE_FILES} "${CMAKE_SOURCE_DIR}/dpf/utils/res2c.py"
    COMMENT "Embedding Resources/* as compiled-in data")
add_custom_target(generate_resources DEPENDS "${MODICUMFM_RESOURCES_HPP}" "${MODICUMFM_RESOURCES_CPP}")

# --- the plugin: CLAP + OpenGL UI, built with DPF --------------------------
# DPF (vendored submodule) owns the plugin wrapper and generates the .clap from our DSP + UI
# subclasses; the DSP side drives pm_framework's VoiceManager.
add_subdirectory(dpf)
dpf_add_plugin(ModicumFM
    TARGETS clap
    UI_TYPE opengl
    USE_WEB_VIEW
    FILES_DSP
        plugin/ModicumFMPlugin.cpp
    FILES_UI
        src/UI/ModicumFMUI.cpp)
# So both DSP and UI sources find DistrhoPluginInfo.h + ModicumFMParams.h.
target_include_directories(ModicumFM PUBLIC plugin)
# The DSP side pulls in the vendored engine.
target_link_libraries(ModicumFM-dsp PUBLIC pm_framework)
# The UI shares framework headers (FM6OpAlgorithms.hpp for the algorithm names and operator roles),
# so it links pm_framework too. Static-lib symbol extraction dedups it in the final .clap.
target_link_libraries(ModicumFM-ui PUBLIC pm_framework)
# Generated preset array (see the codegen step above) — must exist before ModicumFMUI.cpp compiles.
add_dependencies(ModicumFM-ui generate_presets)
target_include_directories(ModicumFM-ui PRIVATE "${CMAKE_BINARY_DIR}/generated")
# Embedded logo bytes — the generated .cpp defines the data the .hpp declares.
add_dependencies(ModicumFM-ui generate_resources)
target_sources(ModicumFM-ui PRIVATE "${MODICUMFM_RESOURCES_CPP}")
# DPF may default to an older standard; the DSP glue includes C++20 framework headers.
set_target_properties(ModicumFM-dsp ModicumFM-ui PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
# Dev-only JSON patch dumper (keystroke → file); compiled out of release builds.
if(MODICUMFM_PRESET_DUMP)
    target_compile_definitions(ModicumFM-ui PRIVATE MODICUMFM_PRESET_DUMP=1)
endif()

# --- install: symlink the built .clap into the user CLAP dir ---------------
# Runs automatically after ModicumFM-clap is (re)built, so the plugin in the
# user CLAP dir is always a symlink pointing at the freshest build.
# create_symlink replaces any existing file or link, so this is idempotent.
if(APPLE)
    set(MODICUMFM_CLAP_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
else()
    set(MODICUMFM_CLAP_DIR "$ENV{HOME}/.clap")
endif()
add_custom_command(TARGET ModicumFM-clap POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MODICUMFM_CLAP_DIR}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${CMAKE_BINARY_DIR}/bin/ModicumFM.clap"
        "${MODICUMFM_CLAP_DIR}/ModicumFM.clap"
    COMMENT "Symlinking ModicumFM.clap into ${MODICUMFM_CLAP_DIR}"
    VERBATIM)

# --- self-tests (the framework; headless, no DAW) --------------------------
add_executable(pm-selftest test/pm_selftest.cpp)
target_link_libraries(pm-selftest PRIVATE pm_framework)
target_include_directories(pm-selftest PRIVATE external/clap/include) # clap_ump.h decoders

add_executable(pm-module-test test/pm_module_test.cpp)
target_link_libraries(pm-module-test PRIVATE pm_framework)

add_executable(pm-adsr-test test/pm_adsr_test.cpp)
target_link_libraries(pm-adsr-test PRIVATE pm_framework)

add_executable(pm-modules-test test/pm_modules_test.cpp)
target_link_libraries(pm-modules-test PRIVATE pm_framework)

add_executable(pm-factory-test test/pm_factory_test.cpp)
target_link_libraries(pm-factory-test PRIVATE pm_framework)

add_executable(pm-modicumfm-test test/pm_modicumfm_test.cpp)
target_link_libraries(pm-modicumfm-test PRIVATE pm_framework)

add_executable(pm-fm6op-test test/pm_fm6op_test.cpp)
target_link_libraries(pm-fm6op-test PRIVATE pm_framework)

add_executable(pm-voicemanager-test test/pm_voicemanager_test.cpp)
target_link_libraries(pm-voicemanager-test PRIVATE pm_framework)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    # pm-selftest only pulls in light headers → fully strict (optionally -Werror in CI).
    target_compile_options(pm-selftest PRIVATE -Wall -Wextra)
    if(MODICUMFM_WERROR)
        target_compile_options(pm-selftest PRIVATE -Werror)
    endif()
    # tests that pull in vendored headers → relaxed (unused params in inline methods).
    foreach(t pm-module-test pm-adsr-test pm-modules-test pm-factory-test
              pm-modicumfm-test pm-fm6op-test pm-voicemanager-test)
        target_compile_options(${t} PRIVATE -Wall -Wno-unknown-pragmas -Wno-unused-parameter)
    endforeach()
endif()

enable_testing()
add_test(NAME pm COMMAND pm-selftest)
set_tests_properties(pm PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-module COMMAND pm-module-test)
set_tests_properties(pm-module PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-adsr COMMAND pm-adsr-test)
set_tests_properties(pm-adsr PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-modules COMMAND pm-modules-test)
set_tests_properties(pm-modules PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-factory COMMAND pm-factory-test)
set_tests_properties(pm-factory PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-modicumfm COMMAND pm-modicumfm-test)
set_tests_properties(pm-modicumfm PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-fm6op COMMAND pm-fm6op-test)
set_tests_properties(pm-fm6op PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")

add_test(NAME pm-voicemanager COMMAND pm-voicemanager-test)
set_tests_properties(pm-voicemanager PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")
