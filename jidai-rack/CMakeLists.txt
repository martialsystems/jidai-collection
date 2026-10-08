cmake_minimum_required(VERSION 3.22)
project(JidaiRack VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# One binary, two engines. BUSHIDO and RONIN are compiled from their own repositories at pinned commits;
# neither plugin is hosted inside the other. To build against local checkouts instead, pass
# -DFETCHCONTENT_SOURCE_DIR_BUSHIDO=/path/to/bushido -DFETCHCONTENT_SOURCE_DIR_RONIN=/path/to/ronin
# (and -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE for an existing JUCE 8.0.4 checkout).
include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.4
  GIT_SHALLOW TRUE)
# SOURCE_SUBDIR points at a folder with no CMakeLists.txt: fetch the sources, do not add their own projects.
FetchContent_Declare(bushido
  GIT_REPOSITORY https://github.com/martialsystems/bushido.git
  # Bushido branch redesign/bushido (PR #4): JCS v1.1 engine, rack/PitchLaw.h over the vendored jidai-common, no retired-prefix aliases.
  # This commit resolves only once that branch is pushed. If it lands by squash or rebase, re-pin to the merged commit.
  GIT_TAG 5ce7e2f7635e351cb7b3b5f888ab52ce773d9507
  SOURCE_SUBDIR jidai-rack-sources-only)
FetchContent_Declare(ronin
  GIT_REPOSITORY https://github.com/martialsystems/Ronin.git
  # Ronin branch redesign/ronin (draft PR #6): JCS v1.1 engine, EG real-time law, TRIANGLE VCO, state format 2, HQ 2x.
  # RONIN vendors jidai-common (third_party/jidai-common); the rack compiles RONIN's sources against the rack's own,
  # newer jidai-common (one copy of each header in the binary), which keeps the API RONIN uses.
  # This commit resolves only once that branch is pushed. If it lands by squash or rebase, re-pin to the merged commit.
  GIT_TAG d3b7a6cf6e15ea032526ec8040ff352e0e7bc444
  SOURCE_SUBDIR jidai-rack-sources-only)
FetchContent_MakeAvailable(JUCE bushido ronin)

set(BUSHIDO_DIR ${bushido_SOURCE_DIR})

# The shared Jidai Cable Standard headers (and their tests) live next to the rack in this repo.
enable_testing()
set(JIDAI_COMMON_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../jidai-common ${CMAKE_CURRENT_BINARY_DIR}/jidai-common)
# ORIGAMI: its DSP core (shared by the standalone effect and the rack device), the plugin, and its tests.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../origami ${CMAKE_CURRENT_BINARY_DIR}/origami)
set(RONIN_DIR ${ronin_SOURCE_DIR})

# ---- rack core: no JUCE. The rack graph, the devices, and both engines' DSP. ----
add_library(jidai_core STATIC
  core/RackGraph.cpp
  core/Rack.cpp
  core/RoninDevice.cpp
  core/BushidoDevice.cpp
  core/OrigamiDevice.cpp
  core/RackIODevice.cpp
  ${BUSHIDO_DIR}/engine/BushidoModule.cpp
  ${RONIN_DIR}/Source/Modular/Module.cpp
  ${RONIN_DIR}/Source/Modular/PatchGraph.cpp
  ${RONIN_DIR}/Source/Modular/PatchState.cpp
  ${RONIN_DIR}/Source/Modular/ExtIn.cpp
  ${RONIN_DIR}/Source/Modular/OutputModule.cpp
  ${RONIN_DIR}/Source/Modular/Noise.cpp
  ${RONIN_DIR}/Source/Modular/Vcf.cpp
  ${RONIN_DIR}/Source/Modular/Vca1.cpp
  ${RONIN_DIR}/Source/Modular/Vca2.cpp
  ${RONIN_DIR}/Source/Modular/Eg1.cpp
  ${RONIN_DIR}/Source/Modular/Mg.cpp
  ${RONIN_DIR}/Source/Modular/Vco.cpp
  ${RONIN_DIR}/Source/Modular/Eg2.cpp
  ${RONIN_DIR}/Source/Modular/Ring.cpp
  ${RONIN_DIR}/Source/Modular/Divider.cpp
  ${RONIN_DIR}/Source/Modular/Inverter.cpp
  ${RONIN_DIR}/Source/Modular/Integrator.cpp
  ${RONIN_DIR}/Source/Modular/Mixer.cpp
  ${RONIN_DIR}/Source/Modular/SampleHold.cpp
  ${RONIN_DIR}/Source/UI/PatchBayLogic.cpp)
target_include_directories(jidai_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${BUSHIDO_DIR} ${RONIN_DIR}/Source)
target_link_libraries(jidai_core PUBLIC jidai::common origami::dsp)
set_target_properties(jidai_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

enable_testing()
add_executable(JidaiRackTests tests/RackTests.cpp tests/JcsRackTests.cpp)
target_link_libraries(JidaiRackTests PRIVATE jidai_core)
target_compile_options(JidaiRackTests PRIVATE -Wall -Wextra)
add_test(NAME rack COMMAND JidaiRackTests)

# ---- panel art: each instrument's own vector panel and layout ----
juce_add_binary_data(JidaiPanels
  SOURCES
    ${BUSHIDO_DIR}/assets/bushido_panel_bg.svg
    ${BUSHIDO_DIR}/assets/bushido_layout.json
    ${BUSHIDO_DIR}/assets/bushido_patterns.json
    ${BUSHIDO_DIR}/assets/rack_patches.json
    ${RONIN_DIR}/panel/assets/panel_bg.svg)

set(JIDAI_UI_SOURCES
  plugin/JidaiProcessor.cpp
  plugin/JidaiEditor.cpp
  plugin/DeviceBrowser.cpp
  plugin/RackView.cpp
  plugin/RoninPanel.cpp
  plugin/RackCableLayer.cpp
  plugin/RearPanel.cpp
  plugin/CompactFace.cpp
  ${ORIGAMI_UI_SOURCES}
  ${BUSHIDO_DIR}/ui/BushidoTabs.cpp
  ${BUSHIDO_DIR}/rack/CableModel.cpp
  ${BUSHIDO_DIR}/ui/Layout.cpp
  ${BUSHIDO_DIR}/ui/RackPanel.cpp
  ${BUSHIDO_DIR}/ui/CableLayer.cpp
  ${BUSHIDO_DIR}/ui/PatternScreen.cpp)

# ---- the plugin: VST3 effect and a Standalone that opens the default audio devices ----
juce_add_plugin(JidaiRack
  COMPANY_NAME "Martial Systems"
  BUNDLE_ID com.martialsystems.jidairack
  PLUGIN_MANUFACTURER_CODE Mrsy
  PLUGIN_CODE Jdrk
  FORMATS VST3 Standalone
  PRODUCT_NAME "JIDAI RACK"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  VST3_CATEGORIES Fx
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(JidaiRack PRIVATE ${JIDAI_UI_SOURCES} plugin/StandaloneApp.cpp)
target_compile_definitions(JidaiRack PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1)
target_link_libraries(JidaiRack PRIVATE
  jidai_core
  JidaiPanels
  juce::juce_audio_utils
  PUBLIC
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# ---- rack UI probe: builds the editor offscreen, drives it with mouse events and keys, writes PNGs ----
juce_add_console_app(JidaiRackProbe PRODUCT_NAME "JidaiRackProbe")
target_sources(JidaiRackProbe PRIVATE tools/RackProbe.cpp ${JIDAI_UI_SOURCES})
target_compile_definitions(JidaiRackProbe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JIDAI_NO_PLUGIN_ENTRY=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(JidaiRackProbe PRIVATE
  jidai_core
  JidaiPanels
  juce::juce_audio_utils
  juce::juce_recommended_config_flags)
add_test(NAME rack_ui COMMAND JidaiRackProbe ${CMAKE_BINARY_DIR}/rack-shots)

# Preset banks: factory lists, the compiled rack patches, and a temp user-file directory.
juce_add_console_app(JidaiPresetTests PRODUCT_NAME "JidaiPresetTests")
target_sources(JidaiPresetTests PRIVATE tests/PresetBankTests.cpp tests/RackStateTests.cpp ${JIDAI_UI_SOURCES})
target_compile_definitions(JidaiPresetTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JIDAI_NO_PLUGIN_ENTRY=1 JIDAI_PRESET_TEST=1)
target_link_libraries(JidaiPresetTests PRIVATE
  jidai_core
  JidaiPanels
  juce::juce_audio_utils
  juce::juce_recommended_config_flags)
add_test(NAME presets COMMAND JidaiPresetTests)
