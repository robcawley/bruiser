cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(BRUISER VERSION 1.4.0)
set(CMAKE_CXX_STANDARD 20)

# Reuse the JUCE checkout that NEON RIOT already has.
set(JUCE_DIR "${CMAKE_SOURCE_DIR}/../NEON RIOT/JUCE" CACHE PATH "Path to a JUCE 8 checkout")
add_subdirectory("${JUCE_DIR}" JUCE)

juce_add_binary_data(BruiserData SOURCES
    Resources/Fonts/Rajdhani-SemiBold.ttf
    Resources/Fonts/Rajdhani-Bold.ttf
    Resources/Fonts/Rajdhani-Medium.ttf
    Resources/Fonts/ShareTechMono-Regular.ttf)

set(BRUISER_SOURCES
    Source/PluginProcessor.cpp Source/PluginEditor.cpp
    Source/Engine/Groove.cpp Source/Engine/Player.cpp Source/Engine/Export.cpp)

# A Logic MIDI FX: sits above ROB KIT in the same channel strip and plays it.
juce_add_plugin(Bruiser
    COMPANY_NAME "Rob Cawley"
    BUNDLE_ID "com.robcawley.bruiser"
    PLUGIN_MANUFACTURER_CODE Rcwl
    PLUGIN_CODE Brsr
    PRODUCT_NAME "BRUISER"
    DESCRIPTION "Drummer for ROB KIT"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
    FORMATS AU
    COPY_PLUGIN_AFTER_BUILD FALSE
    VERSION 1.4.0)

target_sources(Bruiser PRIVATE ${BRUISER_SOURCES})
target_include_directories(Bruiser PRIVATE Source)
target_compile_definitions(Bruiser PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Bruiser PRIVATE BruiserData juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

set(BRUISER_BUNDLE "${CMAKE_BINARY_DIR}/Bruiser_artefacts/$<CONFIG>/AU/BRUISER.component")
add_custom_command(TARGET Bruiser_AU POST_BUILD
    COMMAND codesign --force --deep --sign - "${BRUISER_BUNDLE}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$ENV{HOME}/Library/Audio/Plug-Ins/Components/BRUISER.component"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${BRUISER_BUNDLE}" "$ENV{HOME}/Library/Audio/Plug-Ins/Components/BRUISER.component"
    VERBATIM)

# Tests: live player vs export, loops, fills, sections, kick follow, real-time safety, state, UI,
# and audio renders through the ROB KIT engine.
set(ROBKIT "${CMAKE_SOURCE_DIR}/../ROB KIT" CACHE PATH "Path to the ROB KIT source (only needed for the test tools)")
if (EXISTS "${ROBKIT}/Source/DSP/Kit.cpp")
juce_add_console_app(BruiserTest PRODUCT_NAME "BruiserTest")
target_sources(BruiserTest PRIVATE Tools/Test.cpp ${BRUISER_SOURCES}
    "${ROBKIT}/Source/DSP/Kit.cpp" "${ROBKIT}/Source/Parameters.cpp")
target_include_directories(BruiserTest PRIVATE Source "${ROBKIT}/Source")
target_compile_definitions(BruiserTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="BRUISER"
    JucePlugin_IsSynth=0 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=1 JucePlugin_IsMidiEffect=1)
target_link_libraries(BruiserTest PRIVATE BruiserData juce::juce_audio_utils juce::juce_audio_formats juce::juce_recommended_config_flags)

# Loop pack renderer: BRUISER drum loops through the ROB KIT engine.
juce_add_console_app(BruiserLoops PRODUCT_NAME "BruiserLoops")
target_sources(BruiserLoops PRIVATE Tools/Loops.cpp Source/Engine/Groove.cpp Source/Engine/Export.cpp
    "${ROBKIT}/Source/DSP/Kit.cpp" "${ROBKIT}/Source/Parameters.cpp")
target_include_directories(BruiserLoops PRIVATE Source "${ROBKIT}/Source")
target_compile_definitions(BruiserLoops PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(BruiserLoops PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_recommended_config_flags)
endif()
