cmake_minimum_required(VERSION 3.22)
project(Snaral VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# ---------------------------------------------------------------------------
# Unit tests: every tests/*Tests.cpp is its own executable linking Source/ headers only
# (JUCE-independent). Tests needing JUCE live in tests/juce/*Tests.cpp (see below).
# ---------------------------------------------------------------------------
include(CTest)
file(GLOB SNARAL_CORE_SOURCES CONFIGURE_DEPENDS Source/Core/*.cpp Source/Engine/*.cpp Source/Dsp/*.cpp)
file(GLOB SNARAL_TEST_SOURCES CONFIGURE_DEPENDS tests/*Tests.cpp)
foreach(test_src ${SNARAL_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    string(TOLOWER ${test_name} test_target)
    add_executable(snaral_${test_target} ${test_src} ${SNARAL_CORE_SOURCES})
    target_include_directories(snaral_${test_target} PRIVATE Source)
    add_test(NAME snaral_${test_target} COMMAND snaral_${test_target})
endforeach()

option(SNARAL_BUILD_PLUGIN "Build plugin and standalone targets" ON)
option(SNARAL_BUILD_CLAP "Also build a CLAP plugin" ON)
set(SNARAL_JUCE_DIR "" CACHE PATH "Use a local JUCE checkout instead of fetching")
set(SNARAL_CLAP_EXT_DIR "" CACHE PATH "Use a local clap-juce-extensions checkout instead of fetching")

if(SNARAL_BUILD_PLUGIN)
    include(FetchContent)
    if(SNARAL_JUCE_DIR)
        add_subdirectory(${SNARAL_JUCE_DIR} juce EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 9.0.3
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()

    if(SNARAL_BUILD_CLAP)
        if(SNARAL_CLAP_EXT_DIR)
            add_subdirectory(${SNARAL_CLAP_EXT_DIR} clap-juce-extensions EXCLUDE_FROM_ALL)
        else()
            FetchContent_Declare(clap-juce-extensions
                GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
                GIT_TAG main
                GIT_SHALLOW TRUE)
            FetchContent_MakeAvailable(clap-juce-extensions)
        endif()
    endif()

    if(APPLE)
        set(SNARAL_FORMATS VST3 AU Standalone)
    else()
        set(SNARAL_FORMATS VST3 Standalone)
    endif()
    if(UNIX AND NOT APPLE)
        list(APPEND SNARAL_FORMATS LV2)
    endif()

    juce_add_plugin(Snaral
        COMPANY_NAME "Circuit Drift Labs"
        BUNDLE_ID "com.circuitdriftlabs.snaral"
        PLUGIN_MANUFACTURER_CODE Cdlb
        PLUGIN_CODE Snrl
        FORMATS ${SNARAL_FORMATS}
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES Instrument Synth
        COPY_PLUGIN_AFTER_BUILD FALSE
        PRODUCT_NAME "Snaral")

    # Application sources: every .cpp under Source/ (including Source/Core and Source/Engine).
    file(GLOB_RECURSE SNARAL_PLUGIN_SOURCES CONFIGURE_DEPENDS Source/*.cpp)
    target_sources(Snaral PRIVATE ${SNARAL_PLUGIN_SOURCES})
    target_include_directories(Snaral PRIVATE Source)
    target_compile_definitions(Snaral PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1)
    target_link_libraries(Snaral PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        PUBLIC juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
    juce_generate_juce_header(Snaral)

    if(SNARAL_BUILD_CLAP)
        clap_juce_extensions_plugin(TARGET Snaral
            CLAP_ID "com.circuitdriftlabs.snaral"
            CLAP_FEATURES instrument synthesizer bass stereo
            CLAP_MISC_FLAGS "")
    endif()

    # JUCE-dependent tests: tests/juce/*Tests.cpp link juce_core/juce_data_structures + Source/ (non-GUI).
    file(GLOB SNARAL_JUCE_TEST_SOURCES CONFIGURE_DEPENDS tests/juce/*Tests.cpp)
    file(GLOB_RECURSE SNARAL_JUCE_TEST_LIB_SOURCES CONFIGURE_DEPENDS Source/Core/*.cpp Source/State/*.cpp Source/Parameters/*.cpp)
    foreach(test_src ${SNARAL_JUCE_TEST_SOURCES})
        get_filename_component(test_name ${test_src} NAME_WE)
        string(TOLOWER ${test_name} test_target)
        add_executable(snaral_juce_${test_target} ${test_src} ${SNARAL_JUCE_TEST_LIB_SOURCES})
        target_include_directories(snaral_juce_${test_target} PRIVATE Source)
        target_compile_definitions(snaral_juce_${test_target} PRIVATE JUCE_STANDALONE_APPLICATION=1
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
        target_link_libraries(snaral_juce_${test_target} PRIVATE
            juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
        add_test(NAME snaral_juce_${test_target} COMMAND snaral_juce_${test_target})
    endforeach()
endif()
