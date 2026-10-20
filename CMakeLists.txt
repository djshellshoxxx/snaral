cmake_minimum_required(VERSION 3.22)
project(Snaral VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
add_executable(snaral_engine_tests tests/SineVoiceTests.cpp)
target_include_directories(snaral_engine_tests PRIVATE Source)
add_test(NAME snaral_engine_tests COMMAND snaral_engine_tests)

option(SNARAL_BUILD_PLUGIN "Build VST3 and standalone targets" ON)
if(SNARAL_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(Snaral
        COMPANY_NAME "Circuit Drift Labs"
        BUNDLE_ID "com.circuitdriftlabs.snaral"
        PLUGIN_MANUFACTURER_CODE Cdlb
        PLUGIN_CODE Snrl
        FORMATS VST3 Standalone
        PRODUCT_NAME "Snaral"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE)

    target_sources(Snaral PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/Engine/SineVoice.h)
    target_compile_definitions(Snaral PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(Snaral PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()
