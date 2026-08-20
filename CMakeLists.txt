cmake_minimum_required(VERSION 3.22)

project(s3xtaOTT VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    juce
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.6.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(juce)

juce_add_plugin(s3xtaOTT
    COMPANY_NAME "1113 Audio"
    BUNDLE_ID "com.1113audio.sqtt"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST FALSE
    PLUGIN_MANUFACTURER_CODE Ju13
    PLUGIN_CODE Sq13
    FORMATS VST3 Standalone
    PRODUCT_NAME "1113 SQ-TT"
)

juce_generate_juce_header(s3xtaOTT)

juce_add_binary_data(s3xtaOTTAssets SOURCES Assets/logo_1113.png)

target_sources(s3xtaOTT
    PRIVATE
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp
)

target_compile_definitions(s3xtaOTT
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(s3xtaOTT
    PRIVATE
        s3xtaOTTAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Local black-box diagnostics for the legitimately installed VO-TT binary.
juce_add_console_app(vott_probe PRODUCT_NAME "VOTT Probe")
juce_generate_juce_header(vott_probe)
target_sources(vott_probe PRIVATE Tools/VottProbe.cpp)
target_compile_definitions(vott_probe PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(vott_probe PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
)
