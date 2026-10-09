cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# These MUST stay before project(): that is where CMake configures the Apple
# toolchain and reads them. Set after project(), the deployment target is
# ignored outright and the architectures are unreliable, which on GitHub's
# Apple-Silicon runners yields a silently arm64-only "universal" build.
# 10.13 (not 11.0) because 11.0 excludes Catalina even with a good x86_64
# slice; the arm64 slice is clamped up to 11.0 by the toolchain anyway.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
endif()

project(Camshaft VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is a sibling of this repository: ../JUCE
add_subdirectory(../JUCE JUCE)

# FxmeTools (the module and its JUCE-free core) ships inside the FxmeFX
# submodule, as in MechanOdd. The helper registers the module, builds FxmeCore
# and provides fxmetools_attach().
include(${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeFX/lib/FxmeTools/cmake/FxmeTools.cmake)

juce_add_plugin(Camshaft
    COMPANY_NAME                "FX-Mechanics"
    COMPANY_WEBSITE             "www.fx-mechanics.com"
    PLUGIN_MANUFACTURER_CODE    FXME
    PLUGIN_CODE                 CMSH
    FORMATS                     AU VST3 Standalone
    COPY_PLUGIN_AFTER_BUILD     FALSE
    PRODUCT_NAME                "Camshaft"
    DESCRIPTION                 "FX-Mechanics range wavetable synth"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    BUNDLE_ID                   "com.fx-mechanics.Camshaft"
)

# Sources are globbed recursively; re-run the configure step after adding one.
file(GLOB_RECURSE CAMSHAFT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/*.h)
target_sources(Camshaft PRIVATE ${CAMSHAFT_SOURCES})
target_include_directories(Camshaft PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)

# The module, FxmeCore and the WDL convolution engine (Cab, ConvolReverb).
fxmetools_attach(Camshaft)

# ── Reused FxmeFX effects ───────────────────────────────────────────────────
# Each effect's DSP class and component, not its plugin wrapper. The
# components include Common/TopBar.h, which needs the FxmeCommonBinaryData
# target (the FX-Mechanics logo) on the include path.
include(${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeFX/Source/Common/CommonAssets.cmake)
target_link_libraries(Camshaft PRIVATE FxmeCommonBinaryData)

set(CAMSHAFT_FXMEFX_EFFECTS
    StereoDelay Tube Equalizer Oct Compressor Limiter Transient Cab ConvolReverb
    Chorus Flanger Phaser Freeze)
set(CAMSHAFT_ASSETS "")
foreach(_fx IN LISTS CAMSHAFT_FXMEFX_EFFECTS ITEMS VuMeter)
    set(_fxdir ${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeFX/Source/${_fx})
    file(GLOB _fxsrcs ${_fxdir}/*.cpp)
    list(FILTER _fxsrcs EXCLUDE REGEX "(PluginProcessor|PluginEditor|PdMain)\\.cpp$")
    target_sources(Camshaft PRIVATE ${_fxsrcs})
    target_include_directories(Camshaft PRIVATE ${_fxdir})

    # Images and IR files (Cab uses IR/, ConvolReverb ir/).
    file(GLOB _fxassets
        ${_fxdir}/img/*.png ${_fxdir}/img/*.jpg ${_fxdir}/Assets/*.png
        ${_fxdir}/IR/*.wav  ${_fxdir}/ir/*.wav)
    list(APPEND CAMSHAFT_ASSETS ${_fxassets})
endforeach()

# The effects' factory module presets (FxmeFX Source/<Effect>/Presets), in
# their own binary-data namespace; defines FXMEFX_HAS_MODULE_PRESETS. Their
# user presets live in FX-Mechanics/Modules/<Effect>/Presets, shared with
# FxmeFX's own plugins and with MechanOdd.
include(${CMAKE_CURRENT_SOURCE_DIR}/lib/FxmeFX/cmake/FxmeModulePresets.cmake)
fxmefx_add_module_presets(Camshaft ${CAMSHAFT_FXMEFX_EFFECTS})

# Camshaft's own images and factory presets.
file(GLOB CAMSHAFT_OWN_ASSETS
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.png
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.jpg
    ${CMAKE_CURRENT_SOURCE_DIR}/Source/Assets/*.xml)
list(APPEND CAMSHAFT_ASSETS ${CAMSHAFT_OWN_ASSETS})

juce_add_binary_data(CamshaftBinaryData SOURCES ${CAMSHAFT_ASSETS})
# Embedded WAV byte arrays gain nothing from LTO and can exhaust RAM while
# compiling: off for the binary-data target only.
set_target_properties(CamshaftBinaryData PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION OFF
    POSITION_INDEPENDENT_CODE TRUE)
target_compile_options(CamshaftBinaryData PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O0 -fno-lto>
    $<$<CXX_COMPILER_ID:MSVC>:/Od>)
target_link_libraries(Camshaft PRIVATE CamshaftBinaryData)

juce_generate_juce_header(Camshaft)

target_compile_definitions(Camshaft
    PUBLIC
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Camshaft
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_dsp
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ── Engine test (JUCE-free) ─────────────────────────────────────────────────
# The engine under Source/Engine builds against FxmeCore alone; this renders
# a chord from the built-in source and checks the output. Not built by
# default: cmake --build build --target CamshaftEngineTest
add_executable(CamshaftEngineTest EXCLUDE_FROM_ALL
    tests/EngineTest.cpp
    Source/Engine/CamVoice.cpp
    Source/Engine/CamEngine.cpp)
target_include_directories(CamshaftEngineTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source/Engine)
target_link_libraries(CamshaftEngineTest PRIVATE FxmeCore)
