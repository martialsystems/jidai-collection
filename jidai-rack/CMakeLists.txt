cmake_minimum_required(VERSION 3.22)
project(JidaiRack VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# One binary. BUSHIDO, RONIN and ORIGAMI are compiled from their own repositories at pinned commits;
# no plugin is hosted inside another (SHOGUN likewise, from its own repository). To build against local checkouts instead, pass
# -DFETCHCONTENT_SOURCE_DIR_BUSHIDO=/path/to/bushido -DFETCHCONTENT_SOURCE_DIR_RONIN=/path/to/ronin
# and -DFETCHCONTENT_SOURCE_DIR_ORIGAMI=/path/to/origami
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
  # 46a705a: factory bank (22 patterns), README as a product page, rule citations dropped from tab text; on top of b9e33b5.
  # e8b473b (frozen): jidai-common 1.1.2 vendored, pattern notes, MIDI tab wording, explicit float-to-double casts
  # (output bit-identical).
  # Pushed to origin on that branch. If it lands by squash or rebase, re-pin to the merged commit.
  GIT_TAG e8b473bc7ad5806c843b5bae8e82e8b156348059
  SOURCE_SUBDIR jidai-rack-sources-only)
FetchContent_Declare(ronin
  GIT_REPOSITORY https://github.com/martialsystems/Ronin.git
  # Ronin branch redesign/ronin (draft PR #6): JCS v1.1 engine, EG real-time law, TRIANGLE VCO, state format 2, HQ 2x,
  # jidai-common 1.1.1 (24ee621) vendored, VCO 8' = the exact jcs::pitch::kC3Hz. 38bfc11: factory bank (22 programs),
  # strict-warnings build, docs; on top of ecde6dd. 6226b90: PRESET list in balanced columns (plugin UI only; the rack
  # does not compile that file). 1a53da6 (frozen): jidai-common 1.1.2 vendored, EXT IN GATE declared S-TRIG (so the
  # rack's roles are RONIN's declared roles with no exception), PatchGraph::cableBadge (the rack's badge rule); tab text
  # in plain words; engine unchanged.
  # RONIN vendors jidai-common (third_party/jidai-common); the rack compiles RONIN's sources against the rack's own,
  # newer jidai-common (one copy of each header in the binary), which keeps the API RONIN uses.
  # Pushed to origin on that branch. If it lands by squash or rebase, re-pin to the merged commit.
  GIT_TAG 1a53da63d80e3c03d1c52c44c412c24ca289ad41
  SOURCE_SUBDIR jidai-rack-sources-only)
FetchContent_Declare(shogun
  GIT_REPOSITORY https://github.com/martialsystems/shogun.git
  # Shogun branch redesign/shogun: spec v2.2 engine (engine/shogun.h, per-sample processSample for the rack, 151 jacks
  # with JCS roles, 0/23/26 latency at 1x/2x/4x), 0 warnings under `make strict`.
  # SHAs below are after the Martial Systems LLC author rewrite of that branch.
  # 591ecac: factory bank (INIT + 21 kits with patterns, engine/factory.h) and the shared patch format engine/patch.h,
  # which the rack uses to read and write SHOGUN device state (the same JSON as the SHOGUN plugin) and to load kits.
  # fbc2563: no dead panel keys (KIT/PATTERN arrows and search, A/B, undo/redo, SRC; FILL, scenes, mute groups, lanes
  # removed). 5c999a5: jidai-common 1.1.2 vendored, manual; CLOCK:FILL IN and MOD:LANE A removed from engine/ports.h
  # (151 ports = its ROUTE bay = the rack's jacks); saved cables on them are dropped and reported (shogun::kRemovedPorts).
  # 5d80135 (frozen head): LEAD/BASS CV AMT scales the pitch CV into the voice (1.0 = exact 1 V/oct), MOD matrix scroll.
  # The rack compiles only engine/shogun.cpp, against
  # its own jidai-common (single include path; SHOGUN's vendored copy is not used) and reads plugin/Source/PanelLayout.inc
  # for the MAIN face. SHOGUN's alias shim (shogun::resolvePort) stays SHOGUN-local.
  # Pushed to origin on that branch. If it lands by squash or rebase, re-pin to the merged commit.
  GIT_TAG 5d801359e2baf01c8c50e0c42077ec4eb171b5f0
  SOURCE_SUBDIR jidai-rack-sources-only)
FetchContent_MakeAvailable(JUCE bushido ronin shogun)
set(SHOGUN_DIR ${shogun_SOURCE_DIR})

set(BUSHIDO_DIR ${bushido_SOURCE_DIR})

# The shared Jidai Cable Standard headers (and their tests) live next to the rack in this repo.
enable_testing()
set(JIDAI_COMMON_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../jidai-common ${CMAKE_CURRENT_BINARY_DIR}/jidai-common)
# ORIGAMI: its own repository (https://github.com/martialsystems/origami), pinned by commit. Its CMakeLists is added
# (no SOURCE_SUBDIR): it provides origami::dsp (the DSP core the rack device runs, include "origami/dsp/..."),
# ORIGAMI_UI_SOURCES (the panel the rack compiles into its JUCE targets, include "origami/plugin/..."), the
# standalone ORIGAMI plugin, and its tests (ctest origami, origami_plugin).
# Single jidai-common include path: ORIGAMI vendors jidai-common 1.1.2 (third_party/jidai-common, 8a4b5ae), but it
# is declared after the add_subdirectory above, so it finds the rack's jidai::common and does not add its own copy
# (one copy of each header in the binary, as for RONIN).
FetchContent_Declare(origami
  GIT_REPOSITORY https://github.com/martialsystems/origami.git
  # origami main 943bba3: factory presets in banks (INIT, RONIN, SHOGUN, BUSHIDO, GENERIC), FMA-safe bit-exact INIT
  # (-ffp-contract=off on ORIGAMI's targets), LEVEL COMP reference fix; f41c9c4 user manual; 16d3029 isnan in clampParam
  # (clean under the rack's gcc -Wfloat-equal); 8beeeb0 factory preset box on the rack face (the rack compiles
  # ORIGAMI_PRESET_SOURCES and links origami::factory_data); bbde745 jidai-common 1.1.2 (WAVE 0 is a true bypass at
  # any SYM). If it lands elsewhere by squash or rebase, re-pin.
  GIT_TAG bbde7457b5eb59ba72caf44bee67bb653d86ab49)
FetchContent_MakeAvailable(origami)
if(NOT TARGET origami::dsp OR NOT ORIGAMI_UI_SOURCES OR NOT ORIGAMI_PRESET_SOURCES OR NOT TARGET origami::factory_data)
  message(FATAL_ERROR "ORIGAMI did not provide origami::dsp, ORIGAMI_UI_SOURCES, ORIGAMI_PRESET_SOURCES and origami::factory_data")
endif()
set(RONIN_DIR ${ronin_SOURCE_DIR})

# ---- rack core: no JUCE. The rack graph, the devices, and both engines' DSP. ----
add_library(jidai_core STATIC
  core/RackGraph.cpp
  core/Rack.cpp
  core/RoninDevice.cpp
  core/BushidoDevice.cpp
  core/OrigamiDevice.cpp
  core/RackIODevice.cpp
  core/ShogunDevice.cpp
  ${SHOGUN_DIR}/engine/shogun.cpp
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
target_include_directories(jidai_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${BUSHIDO_DIR} ${RONIN_DIR}/Source ${RONIN_DIR}/Source/Modular ${SHOGUN_DIR}/engine)
target_link_libraries(jidai_core PUBLIC jidai::common origami::dsp)
set_target_properties(jidai_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

enable_testing()
add_executable(JidaiRackTests tests/RackTests.cpp tests/JcsRackTests.cpp tests/ShogunRackTests.cpp)
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
    ${RONIN_DIR}/panel/assets/panel_bg.svg
    racks/starter_racks.xml)

set(JIDAI_UI_SOURCES
  plugin/JidaiProcessor.cpp
  plugin/StarterRacks.cpp
  plugin/JidaiEditor.cpp
  plugin/DeviceBrowser.cpp
  plugin/RackView.cpp
  plugin/RoninPanel.cpp
  plugin/RackCableLayer.cpp
  plugin/RearPanel.cpp
  plugin/CompactFace.cpp
  plugin/ShogunState.cpp
  plugin/ShogunFace.cpp
  ${ORIGAMI_UI_SOURCES}
  ${ORIGAMI_PRESET_SOURCES}
  ${BUSHIDO_DIR}/ui/BushidoTabs.cpp
  ${BUSHIDO_DIR}/rack/CableModel.cpp
  ${BUSHIDO_DIR}/ui/Layout.cpp
  ${BUSHIDO_DIR}/ui/RackPanel.cpp
  ${BUSHIDO_DIR}/ui/CableLayer.cpp
  ${BUSHIDO_DIR}/ui/PatternScreen.cpp)

# SHOGUN's generated panel table (PanelLayout.inc), for the rack's SHOGUN face only.
set_source_files_properties(plugin/ShogunFace.cpp PROPERTIES INCLUDE_DIRECTORIES ${SHOGUN_DIR}/plugin/Source)

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
  origami::factory_data
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
  origami::factory_data
  juce::juce_audio_utils
  juce::juce_recommended_config_flags)
add_test(NAME rack_ui COMMAND JidaiRackProbe ${CMAKE_BINARY_DIR}/rack-shots)

# Preset banks: factory lists, the compiled rack patches, and a temp user-file directory.
juce_add_console_app(JidaiPresetTests PRODUCT_NAME "JidaiPresetTests")
target_sources(JidaiPresetTests PRIVATE tests/PresetBankTests.cpp tests/RackStateTests.cpp tests/StarterRackTests.cpp tests/ShogunStateTests.cpp ${JIDAI_UI_SOURCES})
target_compile_definitions(JidaiPresetTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JIDAI_NO_PLUGIN_ENTRY=1 JIDAI_PRESET_TEST=1)
target_link_libraries(JidaiPresetTests PRIVATE
  jidai_core
  JidaiPanels
  origami::factory_data
  juce::juce_audio_utils
  juce::juce_recommended_config_flags)
add_test(NAME presets COMMAND JidaiPresetTests)
