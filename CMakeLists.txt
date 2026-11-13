cmake_minimum_required(VERSION 3.22)

project(WhyshoDE1 VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
#  Whysho DE1 — a modern, straightforward de-esser (VST3 / AU / SA)
#  Part of the Whysho plugin suite. Independent, self-contained build.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Universal binaries on macOS (x86_64 + arm64) unless overridden by CI.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

include(FetchContent)

# ---- JUCE -----------------------------------------------------------------
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(juce)

# ---- Monocypher (Ed25519 verify, SHA-512 variant = standard Ed25519) ------
FetchContent_Declare(
    monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        4.0.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(monocypher)

add_library(monocypher STATIC
    ${monocypher_SOURCE_DIR}/src/monocypher.c
    ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC
    ${monocypher_SOURCE_DIR}/src
    ${monocypher_SOURCE_DIR}/src/optional)

# ---------------------------------------------------------------------------
juce_add_plugin(WhyshoDE1
    COMPANY_NAME                "whysho"
    COMPANY_WEBSITE             "https://whysho.com"
    BUNDLE_ID                   "com.whysho.de1"
    PLUGIN_MANUFACTURER_CODE    Wsho
    PLUGIN_CODE                 Wde1
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "whysho DE1"
    DESCRIPTION                 "whysho DE1 — modern de-esser"
    VST3_CATEGORIES             Fx Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_AUTO_MANIFEST          FALSE)

juce_generate_juce_header(WhyshoDE1)

set(WHYSHO_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/Parameters.cpp
    src/PresetManager.cpp
    src/dsp/DeEsserEngine.cpp
    src/dsp/SpectrumAnalyzer.cpp
    src/licensing/License.cpp
    src/licensing/Activation.cpp
    src/licensing/MachineId.cpp
    src/licensing/AntiTamper.cpp
    src/gui/HistoryDisplay.cpp
    src/gui/SpectrumBand.cpp
    src/gui/LicenseDialog.cpp
    src/gui/WhyshoLookAndFeel.cpp)

target_sources(WhyshoDE1 PRIVATE ${WHYSHO_SOURCES})

target_compile_definitions(WhyshoDE1 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_include_directories(WhyshoDE1 PRIVATE src)

target_link_libraries(WhyshoDE1 PRIVATE
    monocypher
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_cryptography
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
#  Optional: offscreen GUI snapshot tool (local preview only, never shipped).
#  Build with -DWHYSHO_BUILD_SNAPSHOT=ON, run under xvfb-run.
# ---------------------------------------------------------------------------
option(WHYSHO_BUILD_SNAPSHOT "Build offscreen GUI snapshot tool" OFF)
if(WHYSHO_BUILD_SNAPSHOT)
    juce_add_console_app(WhyshoDE1_Snapshot PRODUCT_NAME "WhyshoDE1Snapshot")
    juce_generate_juce_header(WhyshoDE1_Snapshot)
    target_sources(WhyshoDE1_Snapshot PRIVATE tools/Snapshot.cpp ${WHYSHO_SOURCES})
    target_include_directories(WhyshoDE1_Snapshot PRIVATE src)
    target_compile_definitions(WhyshoDE1_Snapshot PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STANDALONE_APPLICATION=1 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_CHECK_MEMORY_LEAKS=0 NDEBUG=1)
    target_link_libraries(WhyshoDE1_Snapshot PRIVATE
        monocypher
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
        juce::juce_gui_basics juce::juce_cryptography
        PUBLIC juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
#  Optional: headless DSP tests (dev only).  -DWHYSHO_BUILD_TESTS=ON
# ---------------------------------------------------------------------------
option(WHYSHO_BUILD_TESTS "Build headless DSP tests" OFF)
if(WHYSHO_BUILD_TESTS)
    juce_add_console_app(WhyshoDE1_DspTest PRODUCT_NAME "WhyshoDE1DspTest")
    target_sources(WhyshoDE1_DspTest PRIVATE tools/DspTest.cpp src/dsp/DeEsserEngine.cpp)
    target_include_directories(WhyshoDE1_DspTest PRIVATE src)
    target_compile_definitions(WhyshoDE1_DspTest PUBLIC JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
    target_link_libraries(WhyshoDE1_DspTest PRIVATE juce::juce_audio_processors juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
#  Linux needs libcurl for JUCE's HTTPS (Windows/macOS use native networking).
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    find_package(CURL)
    foreach(tgt WhyshoDE1 WhyshoDE1_Snapshot)
        if(TARGET ${tgt})
            if(CURL_FOUND)
                target_link_libraries(${tgt} PRIVATE CURL::libcurl)
            else()
                # Dev boxes without libcurl: build works, online activation is off.
                target_compile_definitions(${tgt} PUBLIC JUCE_USE_CURL=0)
            endif()
        endif()
    endforeach()
endif()
