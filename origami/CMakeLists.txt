# ORIGAMI: the stereo triple wave shaper effect. One DSP core (dsp/, framework-free) for the standalone plugin
# and the Jidai Rack device. Built from jidai-rack/CMakeLists.txt (add_subdirectory) or on its own.
cmake_minimum_required(VERSION 3.22)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(Origami VERSION 0.1.0 LANGUAGES C CXX)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  include(FetchContent)
  FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)
  enable_testing()
  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../jidai-common ${CMAKE_CURRENT_BINARY_DIR}/jidai-common)
endif()

# ---- DSP core: header-only, C++17, no JUCE. Include as "origami/dsp/OrigamiCore.h". ----
add_library(origami_dsp INTERFACE)
add_library(origami::dsp ALIAS origami_dsp)
target_include_directories(origami_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(origami_dsp INTERFACE jidai::common)
target_compile_features(origami_dsp INTERFACE cxx_std_17)

add_executable(OrigamiTests tests/OrigamiTests.cpp)
target_link_libraries(OrigamiTests PRIVATE origami_dsp)
set_target_properties(OrigamiTests PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(OrigamiTests PRIVATE -O2 -Wall -Wextra -Wpedantic -Wshadow -Werror)
endif()
add_test(NAME origami COMMAND OrigamiTests)

# ---- the panel (JUCE): shared by the plugin editor and the rack's ORIGAMI device ----
set(ORIGAMI_UI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/plugin/OrigamiPanel.cpp)
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(ORIGAMI_UI_SOURCES ${ORIGAMI_UI_SOURCES} PARENT_SCOPE)
endif()

# ---- the standalone effect: VST3 + Standalone, stereo in/out plus an optional sidechain ----
juce_add_plugin(Origami
  COMPANY_NAME "Martial Systems"
  BUNDLE_ID com.martialsystems.origami
  PLUGIN_MANUFACTURER_CODE Mrsy
  PLUGIN_CODE Orgm
  FORMATS VST3 Standalone
  PRODUCT_NAME "ORIGAMI"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  VST3_CATEGORIES Fx Distortion
  COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(Origami PRIVATE plugin/OrigamiProcessor.cpp plugin/OrigamiEditor.cpp ${ORIGAMI_UI_SOURCES})
target_compile_definitions(Origami PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Origami PRIVATE origami_dsp juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ---- plugin tests + headless screenshots (processor and editor without a host) ----
juce_add_console_app(OrigamiPluginTests PRODUCT_NAME "OrigamiPluginTests")
target_sources(OrigamiPluginTests PRIVATE tests/OrigamiPluginTests.cpp plugin/OrigamiProcessor.cpp plugin/OrigamiEditor.cpp ${ORIGAMI_UI_SOURCES})
target_compile_definitions(OrigamiPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 ORIGAMI_NO_PLUGIN_ENTRY=1)
target_link_libraries(OrigamiPluginTests PRIVATE origami_dsp juce::juce_audio_utils juce::juce_recommended_config_flags)
add_test(NAME origami_plugin COMMAND OrigamiPluginTests)
