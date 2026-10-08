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
  # Bushido branch cleanup/neutral-names: neutral names (engine/BushidoModule, assets/bushido_*) and INIT-only patterns.
  # This commit resolves only once that branch is pushed. If it lands by squash or rebase, re-pin to the merged commit.
  GIT_TAG 1c8f835003f10d33abf0033c60ab11b14bfb1b2c
  SOURCE_SUBDIR jidai-rack-sources-only)
FetchContent_Declare(ronin
  GIT_REPOSITORY https://github.com/martialsystems/Ronin.git
  # Ronin branch cleanup/neutral-names: neutral names and the INIT-only factory bank.
  # This commit resolves only once that branch is pushed. If it lands by squash or rebase, re-pin to the merged commit.
  GIT_TAG 5aebe460e4759796154ba33ac6ff94cfce2db822
  SOURCE_SUBDIR jidai-rack-sources-only)
FetchContent_MakeAvailable(JUCE bushido ronin)

set(BUSHIDO_DIR ${bushido_SOURCE_DIR})
set(RONIN_DIR ${ronin_SOURCE_DIR})

# ---- rack core: no JUCE. The rack graph, the devices, and both engines' DSP. ----
add_library(jidai_core STATIC
  core/RackGraph.cpp
  core/Rack.cpp
  core/RoninDevice.cpp
  core/BushidoDevice.cpp
  ${BUSHIDO_DIR}/engine/BushidoModule.cpp
  ${RONIN_DIR}/Source/Modular/Module.cpp
  ${RONIN_DIR}/Source/Modular/PatchGraph.cpp
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
set_target_properties(jidai_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

enable_testing()
add_executable(JidaiRackTests tests/RackTests.cpp)
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
  NEEDS_MIDI_INPUT FALSE
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

# ---- window probe: builds the editor offscreen, drags devices in and out, writes a PNG ----
juce_add_console_app(JidaiRackProbe PRODUCT_NAME "JidaiRackProbe")
target_sources(JidaiRackProbe PRIVATE tools/RackProbe.cpp ${JIDAI_UI_SOURCES})
target_compile_definitions(JidaiRackProbe PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JIDAI_NO_PLUGIN_ENTRY=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(JidaiRackProbe PRIVATE
  jidai_core
  JidaiPanels
  juce::juce_audio_utils
  juce::juce_recommended_config_flags)

# Preset banks: factory lists, the compiled rack patches, and a temp user-file directory.
juce_add_console_app(JidaiPresetTests PRODUCT_NAME "JidaiPresetTests")
target_sources(JidaiPresetTests PRIVATE tests/PresetBankTests.cpp ${JIDAI_UI_SOURCES})
target_compile_definitions(JidaiPresetTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JIDAI_NO_PLUGIN_ENTRY=1 JIDAI_PRESET_TEST=1)
target_link_libraries(JidaiPresetTests PRIVATE
  jidai_core
  JidaiPanels
  juce::juce_audio_utils
  juce::juce_recommended_config_flags)
add_test(NAME presets COMMAND JidaiPresetTests)
