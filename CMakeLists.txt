cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime -> no Visual C++ redistributable needed on Windows
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(Nachhall VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(NACHHALL_BUILD_TESTS "Build the offline DSP tests" ON)

# ---------------------------------------------------------------------------
# JUCE (downloaded automatically; use -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE for a local copy)
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.9
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
set(NACHHALL_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND NACHHALL_FORMATS AU)
endif()

juce_add_plugin(Nachhall
    COMPANY_NAME "DusHfm Audio"
    COMPANY_WEBSITE "https://github.com/DusHfm"
    BUNDLE_ID com.dushfm.nachhall
    PLUGIN_MANUFACTURER_CODE Dhfm
    PLUGIN_CODE Nhll
    FORMATS ${NACHHALL_FORMATS}
    PRODUCT_NAME "Nachhall"
    DESCRIPTION "Algorithmic reverb"
    VERSION ${PROJECT_VERSION}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Reverb
    AU_MAIN_TYPE kAudioUnitType_Effect)

set(NACHHALL_DSP_SOURCES
    Source/dsp/DecayDesign.cpp
    Source/dsp/EarlyReflections.cpp
    Source/dsp/PostEq.cpp
    Source/dsp/ReverbEngine.cpp
    Source/dsp/Tanks.cpp)

target_sources(Nachhall PRIVATE
    ${NACHHALL_DSP_SOURCES}
    Source/IRAnalyzer.cpp
    Source/Params.cpp
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/Presets.cpp
    Source/gui/Controls.cpp
    Source/gui/EqDisplay.cpp
    Source/gui/MidiLearnOverlay.cpp
    Source/gui/PresetBrowser.cpp
    Source/gui/Theme.cpp)

target_compile_definitions(Nachhall PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(Nachhall
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
if(NACHHALL_BUILD_TESTS)
    add_executable(NachhallDspTests tests/DspTests.cpp ${NACHHALL_DSP_SOURCES})
    # every documented Pro-R 2 behaviour as a measurement
    add_executable(NachhallBehaviourTests tests/BehaviourTests.cpp ${NACHHALL_DSP_SOURCES})
    # reference measurements: test signals, measurement plan, comparison with recordings (reference/)
    add_executable(NachhallReference tests/ReferenceTool.cpp ${NACHHALL_DSP_SOURCES})
    foreach(t NachhallDspTests NachhallBehaviourTests NachhallReference)
        target_compile_features(${t} PRIVATE cxx_std_17)
        if(NOT MSVC)
            target_compile_options(${t} PRIVATE -O3)
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Developer tool: renders the editor with analyser data into a PNG (not needed for the plug-in)
option(NACHHALL_BUILD_UISHOT "Build the UI screenshot tool" OFF)
if(NACHHALL_BUILD_UISHOT)
    juce_add_gui_app(NachhallUiShot PRODUCT_NAME "NachhallUiShot")
    target_sources(NachhallUiShot PRIVATE
        tests/UiShot.cpp
        ${NACHHALL_DSP_SOURCES}
        Source/IRAnalyzer.cpp
        Source/Params.cpp
        Source/PluginEditor.cpp
        Source/PluginProcessor.cpp
        Source/Presets.cpp
        Source/gui/Controls.cpp
        Source/gui/EqDisplay.cpp
        Source/gui/MidiLearnOverlay.cpp
        Source/gui/PresetBrowser.cpp
        Source/gui/Theme.cpp)
    target_compile_definitions(NachhallUiShot PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        "JucePlugin_Name=\"Nachhall\""
        "JucePlugin_VersionString=\"${PROJECT_VERSION}\"")
    target_link_libraries(NachhallUiShot PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
# Developer tool: loads the built VST3 like a DAW does and runs plug-in checks
option(NACHHALL_BUILD_HOSTTEST "Build the VST3 host test" OFF)
if(NACHHALL_BUILD_HOSTTEST)
    juce_add_console_app(NachhallHostTest PRODUCT_NAME "NachhallHostTest")
    target_sources(NachhallHostTest PRIVATE tests/HostTest.cpp)
    target_compile_definitions(NachhallHostTest PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(NachhallHostTest PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_recommended_config_flags)
endif()
