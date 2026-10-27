# ZackAMP — top-level build.
#
# Layers:
#   ampforge_dsp   : framework-independent C++17 DSP + engine + parameter model (no JUCE).
#   core sources   : presets, IR library, MIDI mapping (use juce_core / juce_audio_formats);
#                    compiled into each JUCE target via ampforge_add_core().
#   ZackAMP        : the standalone application (JUCE GUI app).
#   tests / tools  : doctest suites, measurement, rendering, benchmarking, IR generation.
cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version (11.0 = first Apple Silicon release)")
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary by default")
endif()

project(ZackAMP VERSION 0.1.0 LANGUAGES C CXX)

set(AMPFORGE_PRODUCT_NAME "ZackAMP" CACHE STRING "Product name shown to users")
set(AMPFORGE_COMPANY_NAME "ZackAMP" CACHE STRING "Company / vendor name")
set(AMPFORGE_BUNDLE_ID "com.zedmtrappe.zackamp" CACHE STRING "macOS bundle identifier")
option(AMPFORGE_BUILD_APP "Build the standalone application" ON)
option(AMPFORGE_BUILD_TESTS "Build automated tests" ON)
option(AMPFORGE_BUILD_TOOLS "Build developer tools" ON)
set(AMPFORGE_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE" CACHE PATH "Path to JUCE (8.0.9)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    set(AF_WARNINGS /W4)
else()
    set(AF_WARNINGS -Wall -Wextra -Wshadow -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# Framework-independent DSP + engine
file(GLOB_RECURSE AF_DSP_SOURCES CONFIGURE_DEPENDS
     Source/DSP/*.cpp Source/Audio/*.cpp Source/Parameters/*.cpp)
add_library(ampforge_dsp STATIC ${AF_DSP_SOURCES})
target_include_directories(ampforge_dsp PUBLIC Source)
target_compile_options(ampforge_dsp PRIVATE ${AF_WARNINGS})
set_target_properties(ampforge_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

enable_testing()

if(AMPFORGE_BUILD_TESTS)
    file(GLOB AF_DSP_TEST_SOURCES CONFIGURE_DEPENDS Tests/DSP/*.cpp Tests/Audio/*.cpp)
    add_executable(ampforge_dsp_tests Tests/TestMain.cpp ${AF_DSP_TEST_SOURCES})
    target_include_directories(ampforge_dsp_tests PRIVATE third_party/doctest)
    target_link_libraries(ampforge_dsp_tests PRIVATE ampforge_dsp)
    add_test(NAME dsp_and_engine COMMAND ampforge_dsp_tests)
endif()

if(AMPFORGE_BUILD_TOOLS)
    add_executable(ampforge_measure Tools/AmpMeasure.cpp)
    target_link_libraries(ampforge_measure PRIVATE ampforge_dsp)
    add_executable(ampforge_irgen Tools/IRGenerator.cpp)
    target_link_libraries(ampforge_irgen PRIVATE ampforge_dsp)
    add_executable(ampforge_bench Tools/Benchmark.cpp)
    target_link_libraries(ampforge_bench PRIVATE ampforge_dsp)
endif()

# ---------------------------------------------------------------------------
# JUCE-based layers
if(NOT EXISTS "${AMPFORGE_JUCE_DIR}/CMakeLists.txt")
    message(STATUS "JUCE not found at ${AMPFORGE_JUCE_DIR} — fetching JUCE 8.0.9")
    include(FetchContent)
    FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                         GIT_TAG 8.0.9 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(juce)
else()
    add_subdirectory("${AMPFORGE_JUCE_DIR}" juce EXCLUDE_FROM_ALL)
endif()

file(GLOB AF_FACTORY_IRS CONFIGURE_DEPENDS Resources/FactoryIRs/*.wav Resources/FactoryIRs/manifest.json)
file(GLOB AF_FACTORY_PRESETS CONFIGURE_DEPENDS Resources/FactoryPresets/*.json)
juce_add_binary_data(ampforge_resources NAMESPACE AmpForgeData HEADER_NAME AmpForgeData.h
                     SOURCES ${AF_FACTORY_IRS} ${AF_FACTORY_PRESETS})
set_target_properties(ampforge_resources PROPERTIES POSITION_INDEPENDENT_CODE ON)

file(GLOB_RECURSE AF_CORE_SOURCES CONFIGURE_DEPENDS
     Source/Presets/*.cpp Source/IR/*.cpp Source/MIDI/*.cpp Source/Platform/*.cpp)

set(AF_JUCE_DEFS
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_FLAC=0 JUCE_USE_OGGVORBIS=0 JUCE_USE_MP3AUDIOFORMAT=0 JUCE_USE_LAME_AUDIO_FORMAT=0
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0
    AMPFORGE_PRODUCT_NAME="${AMPFORGE_PRODUCT_NAME}"
    AMPFORGE_VERSION_STRING="${PROJECT_VERSION}")

function(ampforge_add_core target)
    target_sources(${target} PRIVATE ${AF_CORE_SOURCES})
    target_include_directories(${target} PRIVATE Source)
    target_compile_definitions(${target} PRIVATE ${AF_JUCE_DEFS})
    target_link_libraries(${target} PRIVATE ampforge_dsp ampforge_resources
        juce::juce_core juce::juce_data_structures juce::juce_audio_formats juce::juce_events
        juce::juce_recommended_config_flags)
    target_compile_options(${target} PRIVATE ${AF_WARNINGS})
endfunction()

if(AMPFORGE_BUILD_TESTS)
    juce_add_console_app(ampforge_core_tests PRODUCT_NAME "ampforge_core_tests")
    file(GLOB AF_CORE_TEST_SOURCES CONFIGURE_DEPENDS Tests/Presets/*.cpp Tests/IR/*.cpp Tests/Integration/*.cpp)
    target_sources(ampforge_core_tests PRIVATE Tests/TestMain.cpp ${AF_CORE_TEST_SOURCES})
    target_include_directories(ampforge_core_tests PRIVATE third_party/doctest)
    ampforge_add_core(ampforge_core_tests)
    add_test(NAME presets_ir_midi COMMAND ampforge_core_tests)
endif()

if(AMPFORGE_BUILD_TOOLS)
    juce_add_console_app(ampforge_render PRODUCT_NAME "ampforge_render")
    target_sources(ampforge_render PRIVATE Tools/Render.cpp)
    ampforge_add_core(ampforge_render)
endif()

if(AMPFORGE_BUILD_APP)
    juce_add_gui_app(ZackAMP
        PRODUCT_NAME "${AMPFORGE_PRODUCT_NAME}"
        COMPANY_NAME "${AMPFORGE_COMPANY_NAME}"
        BUNDLE_ID "${AMPFORGE_BUNDLE_ID}"
        VERSION "${PROJECT_VERSION}"
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT "ZackAMP needs audio input access to process your guitar signal from your audio interface."
        HARDENED_RUNTIME_ENABLED TRUE
        HARDENED_RUNTIME_OPTIONS com.apple.security.device.audio-input
        ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/Images/AppIcon.png")
    file(GLOB_RECURSE AF_APP_SOURCES CONFIGURE_DEPENDS Source/App/*.cpp Source/UI/*.cpp)
    target_sources(ZackAMP PRIVATE ${AF_APP_SOURCES})
    ampforge_add_core(ZackAMP)
    target_link_libraries(ZackAMP PRIVATE
        juce::juce_gui_basics juce::juce_gui_extra juce::juce_audio_devices juce::juce_audio_utils)
endif()
