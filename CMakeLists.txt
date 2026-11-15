cmake_minimum_required(VERSION 3.22)

project(AuroraEngine
    VERSION 0.1.0
    DESCRIPTION "Aurora Engine - modular mixing and mastering processor"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(AURORA_BUILD_TESTS "Build the Catch2 unit tests" OFF)
option(AURORA_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in Aurora sources" OFF)

set(AURORA_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE")
if(NOT EXISTS "${AURORA_JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "JUCE 8.0.17-security is missing from external/JUCE.\n"
        "Run: git clone --depth 1 --branch 8.0.17-security https://github.com/juce-framework/JUCE.git external/JUCE")
endif()

add_subdirectory("${AURORA_JUCE_DIR}" JUCE)

# ---------------------------------------------------------------------------
# Embedded assets: themes and fonts. The generated header is BinaryData.h in
# the AuroraAssets namespace so ThemeManager can call
# AuroraAssets::getNamedResource("dark-matter.json", ...).
# ---------------------------------------------------------------------------
juce_add_binary_data(AuroraAssets
    NAMESPACE AuroraAssets
    HEADER_NAME BinaryData.h
    SOURCES
        assets/themes/dark-matter.json
        assets/themes/industrial-steel.json
        assets/fonts/InterVariable.ttf
        assets/fonts/JetBrainsMono-Variable.ttf)

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
juce_add_plugin(AuroraEngine
    COMPANY_NAME "Aurora Audio"
    BUNDLE_ID "com.auroraaudio.auroraengine"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Aurn
    PLUGIN_CODE Aurn
    PRODUCT_NAME "Aurora Engine"
    FORMATS VST3 Standalone)

target_sources(AuroraEngine PRIVATE
    Source/Core/ModuleCatalog.cpp
    Source/Core/RackEngine.cpp
    Source/Core/ThemeManager.cpp
    Source/Core/PluginProcessor.cpp

    Source/DSP/Utility.cpp
    Source/DSP/ParametricEQ.cpp
    Source/DSP/Compressor.cpp
    Source/DSP/MultibandComp.cpp
    Source/DSP/DynamicsCore.cpp
    Source/DSP/Saturator.cpp
    Source/DSP/SpatialFX.cpp
    Source/DSP/MasteringStage.cpp

    Source/Metering/LoudnessMeter.cpp
    Source/Metering/SpectrumAnalyzer.cpp
    Source/Metering/PhaseScope.cpp

    Source/AI/ARIAAnalyzer.cpp

    Source/UI/AuroraLookAndFeel.cpp
    Source/UI/Components/ParameterControl.cpp
    Source/UI/Components/EQCurveDisplay.cpp
    Source/UI/Components/SpectrumDisplay.cpp
    Source/UI/Components/PhaseScopeDisplay.cpp
    Source/UI/Components/LevelMeter.cpp
    Source/UI/RackView.cpp
    Source/UI/DetailEditorView.cpp
    Source/UI/MeteringPanel.cpp
    Source/UI/ARIAPanelView.cpp
    Source/UI/PluginEditor.cpp)

target_include_directories(AuroraEngine PRIVATE Source)

target_compile_definitions(AuroraEngine PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_USE_MP3AUDIOFORMAT=0)

target_link_libraries(AuroraEngine PRIVATE
    AuroraAssets
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_basics
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(AuroraEngine PRIVATE /W4 /permissive- /Zc:__cplusplus /utf-8)
    if(AURORA_WARNINGS_AS_ERRORS)
        target_compile_options(AuroraEngine PRIVATE /WX)
    endif()
else()
    target_compile_options(AuroraEngine PRIVATE -Wall -Wextra -Wpedantic)
    if(AURORA_WARNINGS_AS_ERRORS)
        target_compile_options(AuroraEngine PRIVATE -Werror)
    endif()
endif()

# Keep the artefacts together for packaging.
set_target_properties(AuroraEngine PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/artefacts/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/artefacts/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/artefacts/bin")

if(AURORA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
