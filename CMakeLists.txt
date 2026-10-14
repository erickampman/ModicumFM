cmake_minimum_required(VERSION 3.17)

# C is needed for DPF's pugl / dgl sources.
project(ModicumFM VERSION 0.1.0 LANGUAGES C CXX)

# Everything is linked into the .clap, a shared module, so every static library in it (the engine,
# the plugin core, DPF's DGL) must be position-independent. clang on Linux defaults to PIE and
# hides this; gcc does not ("relocation R_X86_64_PC32 ... recompile with -fPIC"). As IFSynth
# (e5a32e3) and PlainOldSynth.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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

# --- DPF is used ONLY for the GUI ------------------------------------------
# UIExporter + NanoVG + pugl (plus the native WebView the Help button needs). The CLAP entry point
# is ours (src/plugin.cpp), because DPF's generated CLAP wrapper cannot receive MIDI 2.0 note
# events: see that file's header.
add_subdirectory(dpf)
# dgl-opengl is normally created by dpf_add_plugin()'s UI_TYPE branch; call the same helper
# directly. SHARED_RESOURCES=TRUE embeds DGL's DejaVu font. USE_WEB_VIEW=TRUE mirrors
# dpf_add_plugin's USE_WEB_VIEW: it defines DGL_USE_WEB_VIEW PUBLIC (required by
# DistrhoPluginChecks.h when DISTRHO_UI_WEB_VIEW is 1) and builds DGL's own WebView impl.
# USE_FILE_BROWSER=TRUE brings in the Distrho String/ChildProcess helpers that impl uses.
dpf__add_dgl_opengl(TRUE TRUE TRUE)

# --- modicumfm_core: the whole plugin (DSP + host glue + GUI shell) as one static archive --------
# Hidden visibility for our own objects, so clap_entry (marked CLAP_EXPORT in plugin.cpp) is the
# only symbol this target exports (the vendored engine and DGL libraries keep their own default
# visibility).
add_library(modicumfm_core STATIC
    src/plugin.cpp
    src/UI/ModicumFMUI.cpp
    "${MODICUMFM_RESOURCES_CPP}"                # generated: the logo/help bytes (see above)
    "${DPF_ROOT_DIR}/distrho/src/DistrhoUI.cpp"
    "${DPF_ROOT_DIR}/distrho/src/DistrhoUtils.cpp")
set_target_properties(modicumfm_core PROPERTIES
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON)
target_include_directories(modicumfm_core PUBLIC
    src                               # plugin_gui.h (the DSP <-> GUI seam)
    plugin                            # DistrhoPluginInfo.h + ModicumFMParams.h
    external/clap/include             # <clap/clap.h> (the raw entry point)
    "${CMAKE_BINARY_DIR}/generated"   # GeneratedPresets.h + EmbeddedResources.hpp
    "${DPF_ROOT_DIR}/distrho")        # DistrhoUI.hpp / DistrhoUIInternal.hpp
target_compile_definitions(modicumfm_core PUBLIC
    DISTRHO_PLUGIN_TARGET_CLAP
    DISTRHO_IS_STANDALONE=0
    MODICUMFM_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(modicumfm_core PUBLIC pm_framework dgl-opengl)
add_dependencies(modicumfm_core generate_presets generate_resources)
# TRUE = the UI's Help button opens a native WebView. On macOS this adds DistrhoUI_macOS.mm and
# links WebKit; the WebView implementation itself comes from DPF's UI sources, not from dgl.
dpf__add_plugin_specific_ui_sources(modicumfm_core TRUE)
if(APPLE)
    # The live Retina scale of the host window (see the file's header comment).
    target_sources(modicumfm_core PRIVATE src/UI/BackingScale_mac.mm)
endif()
# Dev-only JSON patch dumper (keystroke → file); compiled out of release builds.
if(MODICUMFM_PRESET_DUMP)
    target_compile_definitions(modicumfm_core PRIVATE MODICUMFM_PRESET_DUMP=1)
endif()

# --- the .clap output ------------------------------------------------------
# A MODULE with no sources of its own would be rejected by CMake, and simply linking the archive is
# not enough: nothing references clap_entry, so the linker would drop the object that defines it.
# Hence the empty placeholder plus a whole-archive force-load.
function(modicumfm_force_load_whole_archive TARGET_NAME LIB_NAME)
    if(APPLE)
        target_link_options(${TARGET_NAME} PRIVATE "-Wl,-force_load,$<TARGET_FILE:${LIB_NAME}>")
    elseif(UNIX)
        target_link_options(${TARGET_NAME} PRIVATE
            "-Wl,--whole-archive" "$<TARGET_FILE:${LIB_NAME}>" "-Wl,--no-whole-archive")
    endif()
endfunction()

set(MODICUMFM_EMPTY_SOURCE "${CMAKE_BINARY_DIR}/generated/modicumfm_empty.c")
# A genuinely empty file trips -Wempty-translation-unit under -Wpedantic -Werror, and ISO C requires
# at least one declaration anyway.
set(_modicumfm_empty_content "typedef int modicumfm_empty_translation_unit_marker;\n")
set(_modicumfm_empty_existing "")
if(EXISTS "${MODICUMFM_EMPTY_SOURCE}")
    file(READ "${MODICUMFM_EMPTY_SOURCE}" _modicumfm_empty_existing)
endif()
if(NOT _modicumfm_empty_existing STREQUAL _modicumfm_empty_content)
    file(WRITE "${MODICUMFM_EMPTY_SOURCE}" "${_modicumfm_empty_content}")
endif()

add_library(ModicumFM MODULE "${MODICUMFM_EMPTY_SOURCE}")
target_link_libraries(ModicumFM PRIVATE modicumfm_core)
modicumfm_force_load_whole_archive(ModicumFM modicumfm_core)

if(APPLE)
    set_target_properties(ModicumFM PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.unlikelyware.modicumfm"
        MACOSX_BUNDLE_BUNDLE_NAME "ModicumFM"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
else()
    set_target_properties(ModicumFM PROPERTIES PREFIX "" SUFFIX ".clap")
endif()

# --- install: symlink the built .clap into the user CLAP dir ---------------
# Runs automatically after ModicumFM is (re)built, so the plugin in the user CLAP dir is always a
# symlink pointing at the freshest build. The rm first clears an old link or a copied bundle
# directory, which create_symlink would not replace.
if(APPLE)
    set(MODICUMFM_CLAP_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
    set(MODICUMFM_CLAP_ARTIFACT "$<TARGET_BUNDLE_DIR:ModicumFM>")
else()
    set(MODICUMFM_CLAP_DIR "$ENV{HOME}/.clap")
    set(MODICUMFM_CLAP_ARTIFACT "$<TARGET_FILE:ModicumFM>")
endif()
add_custom_command(TARGET ModicumFM POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MODICUMFM_CLAP_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${MODICUMFM_CLAP_DIR}/ModicumFM.clap"
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        "${MODICUMFM_CLAP_ARTIFACT}"
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

# --- end-to-end CLAP host test: note-port dialect negotiation + the MIDI 2.0 7.9-pitch path ------
# Drives the real built .clap over the CLAP C API (no DAW, no GUI): dlopen + clap_entry + factory,
# then a MIDI 2.0 channel-voice-2 NoteOn carrying a 7.9-pitch attribute. This is the only check that
# proves the raw entry point does what dpf_add_plugin could not.
add_executable(mfm-clap-host-test test/mfm_clap_host_test.cpp)
target_include_directories(mfm-clap-host-test PRIVATE
    external/clap/include             # <clap/clap.h>
    plugin)                           # ModicumFMParams.h (parameter ids/enums)
# dlopen/dlsym/dlclose: on glibc before 2.34 these live in libdl.
target_link_libraries(mfm-clap-host-test PRIVATE ${CMAKE_DL_LIBS})
add_dependencies(mfm-clap-host-test ModicumFM)

if(APPLE)
    set(MODICUMFM_CLAP_UNDER_TEST "$<TARGET_BUNDLE_DIR:ModicumFM>")
else()
    set(MODICUMFM_CLAP_UNDER_TEST "$<TARGET_FILE:ModicumFM>")
endif()
add_test(NAME mfm-clap COMMAND mfm-clap-host-test "${MODICUMFM_CLAP_UNDER_TEST}")
set_tests_properties(mfm-clap PROPERTIES PASS_REGULAR_EXPRESSION "ALL PASS")
