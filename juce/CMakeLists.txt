cmake_minimum_required(VERSION 3.22)
project(SwarmSynthJUCE VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "$ENV{HOME}/JUCE" CACHE PATH "Path to a JUCE checkout")
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(SwarmSynthJUCE
    PLUGIN_MANUFACTURER_CODE  Adkt
    PLUGIN_CODE               Swrm
    FORMATS                   VST3 Standalone
    IS_SYNTH                  TRUE
    NEEDS_MIDI_INPUT          TRUE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    PRODUCT_NAME              "SwarmSynth")

target_sources(SwarmSynthJUCE PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(SwarmSynthJUCE PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SwarmSynthJUCE PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

# Headless checks for the envelope model (no JUCE, no GUI):
#   cmake --build build --target test_env && ./build/test_env
add_executable(test_env tools/test_env.cpp)
target_include_directories(test_env PRIVATE Source)
