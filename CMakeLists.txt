cmake_minimum_required(VERSION 3.24)

project(PabloSampler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PABLO_ENABLE_STEMS "Enable AI stem separation (ONNX Runtime)" ON)
option(PABLO_BUILD_TESTS "Build unit tests" ON)

include(cmake/FetchJUCE.cmake)
if(PABLO_ENABLE_STEMS)
    include(cmake/FetchOrtHeaders.cmake)
endif()

# The model downloader uses juce::URL over HTTPS. On Linux/macOS that needs
# libcurl (JUCE_USE_CURL=1); on Windows JUCE uses WinInet, so curl is off there.
if(NOT WIN32)
    find_package(CURL REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Shared engine/model/dsp/stem sources. These are compiled directly into each
# final target (plugin + tests) so JUCE module unity files are only compiled
# once per target. (JUCE modules add their sources as INTERFACE, so a static
# library linking them would double-compile and collide at final link.)
# ---------------------------------------------------------------------------
set(PABLO_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/model/SessionState.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/SamplerEngine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/Voice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/engine/Recorder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/TransientDetector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Resampler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/stems/OrtDynamicApi.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/stems/ModelManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/stems/StemSeparator.cpp
)

set(PABLO_CORE_JUCE_LIBS
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_dsp
)

function(pablo_configure_common target)
    # PROJECT_SOURCE_DIR (not CMAKE_CURRENT_SOURCE_DIR) so this is correct when
    # called from the tests/ subdirectory too.
    target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        $<$<BOOL:${PABLO_ENABLE_STEMS}>:PABLO_ENABLE_STEMS=1>
    )
    if(WIN32)
        target_compile_definitions(${target} PUBLIC JUCE_USE_CURL=0)
    else()
        target_compile_definitions(${target} PUBLIC JUCE_USE_CURL=1)
        target_link_libraries(${target} PRIVATE CURL::libcurl)
    endif()
    if(PABLO_ENABLE_STEMS)
        target_include_directories(${target} PRIVATE ${PABLO_ORT_INCLUDE_DIR})
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W3 /bigobj)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(PabloSampler
    COMPANY_NAME "PabloAudio"
    PLUGIN_MANUFACTURER_CODE Pblo
    PLUGIN_CODE Pbls
    FORMATS VST3 Standalone
    PRODUCT_NAME "PABLO Sampler"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler
)

juce_add_binary_data(pablo_assets SOURCES
    assets/fonts/PermanentMarker-Regular.ttf
)

target_sources(PabloSampler PRIVATE
    ${PABLO_CORE_SOURCES}
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/PabloLookAndFeel.cpp
    src/ui/WaveformView.cpp
    src/ui/ChopInspectorPanel.cpp
    src/ui/PadGridComponent.cpp
    src/ui/TrackTabsComponent.cpp
    src/ui/TransportBar.cpp
)

pablo_configure_common(PabloSampler)
target_link_libraries(PabloSampler
    PRIVATE
        pablo_assets
        ${PABLO_CORE_JUCE_LIBS}
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(PABLO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
