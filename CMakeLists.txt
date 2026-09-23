cmake_minimum_required(VERSION 3.22)
project(AutoShapeStereoVST3 VERSION 0.1.0 LANGUAGES C CXX)

set(default_juce_dir "")
foreach(candidate "$ENV{JUCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE")
    if(EXISTS "${candidate}/CMakeLists.txt")
        set(default_juce_dir "${candidate}")
        break()
    endif()
endforeach()
set(JUCE_DIR "${default_juce_dir}" CACHE PATH "Local JUCE checkout")
if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Supply a JUCE checkout with -DJUCE_DIR=C:/path/to/JUCE")
endif()
add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")

add_library(AutoShapeEngine STATIC Source/StereoEngine.cpp)
target_include_directories(AutoShapeEngine PUBLIC Source)
target_compile_features(AutoShapeEngine PUBLIC cxx_std_17)
target_link_libraries(AutoShapeEngine PUBLIC juce::juce_recommended_config_flags)

juce_add_plugin(AutoShapeStereoVST3
    COMPANY_NAME "Honey Badger Audio"
    BUNDLE_ID "com.honeybadgeraudio.autoshapestereo"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE HBad
    PLUGIN_CODE AsSt
    FORMATS VST3 Standalone
    PRODUCT_NAME "Auto Shape Stereo")
juce_generate_juce_header(AutoShapeStereoVST3)
target_sources(AutoShapeStereoVST3 PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_features(AutoShapeStereoVST3 PUBLIC cxx_std_17)
target_compile_definitions(AutoShapeStereoVST3 PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(AutoShapeStereoVST3 PRIVATE AutoShapeEngine juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(MSVC)
    target_compile_options(AutoShapeStereoVST3 PRIVATE /FS)
endif()

enable_testing()
add_executable(AutoShapeEngineTests Tests/EngineTests.cpp)
target_link_libraries(AutoShapeEngineTests PRIVATE AutoShapeEngine)
add_test(NAME EngineContracts COMMAND AutoShapeEngineTests)

juce_add_console_app(AutoShapePluginTests PRODUCT_NAME "Auto Shape Plugin Tests")
target_sources(AutoShapePluginTests PRIVATE Tests/PluginTests.cpp)
target_include_directories(AutoShapePluginTests PRIVATE Source
    "${JUCE_DIR}/modules"
    "${CMAKE_BINARY_DIR}/AutoShapeStereoVST3_artefacts/JuceLibraryCode")
target_link_libraries(AutoShapePluginTests PRIVATE AutoShapeStereoVST3 AutoShapeEngine)
add_test(NAME PluginContracts COMMAND AutoShapePluginTests)

juce_add_console_app(AutoShapeHostTests PRODUCT_NAME "Auto Shape VST3 Host Tests")
target_sources(AutoShapeHostTests PRIVATE Tests/HostTests.cpp)
target_compile_features(AutoShapeHostTests PRIVATE cxx_std_17)
target_compile_definitions(AutoShapeHostTests PRIVATE
    JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(AutoShapeHostTests PRIVATE juce::juce_audio_utils
    juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_dependencies(AutoShapeHostTests AutoShapeStereoVST3_VST3)
add_test(NAME VST3HostContracts COMMAND AutoShapeHostTests
    "${CMAKE_BINARY_DIR}/AutoShapeStereoVST3_artefacts/$<CONFIG>/VST3/Auto Shape Stereo.vst3")
