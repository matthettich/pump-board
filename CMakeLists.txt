cmake_minimum_required(VERSION 3.24)
project(PumpBoard VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "" FORCE)

add_subdirectory(external/JUCE)

# pump DSP sources used by the effect adapters (card 1.1).
# pump's engines are header-only and framework-free: each one needs its plugin folder plus pump's common/ on the
# include path (they include "dsp/Fx.hpp" and their own "XxxParams.hpp").
set(PUMP_DSP_SOURCES
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/spring/SpringEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/echo/EchoEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/tube/TubeEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/crush/CrushEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/swirl/SwirlEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/comp/CompEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/heat/HeatEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/tone/ToneEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/verb/VerbEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/verb/VerbDsp.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/limit/LimitEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/limit/LimitDsp.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/snap/SnapEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/strip/StripEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/calm/CalmEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/calm/CalmDsp.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/gate/GateEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/grain/GrainEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/stutter/StutterEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/chop/ChopEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/wide/WideEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/band/BandEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/band/BandDsp.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/eq/EQEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/eq/EQDsp.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/coil/CoilEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/coil/CoilDsp.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/coil/CoilParams.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/tune/TuneEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/vox/VoxEngine.hpp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/grumble/GrumbleEngine.hpp
    # HEAT's linear-phase splitter goes through pump's RealFFT, which is pffft; every plugin ships its own copy
    # of this one C file (card 5.7). One is enough for the whole app.
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/heat/pffft.c)
set(PUMP_DSP_INCLUDES
    ${CMAKE_SOURCE_DIR}/external/pump/common
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/spring
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/echo
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/tube
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/crush
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/swirl
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/comp
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/heat
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/tone
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/verb
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/limit
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/snap
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/strip
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/calm
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/gate
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/grain
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/stutter
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/chop
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/wide
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/band
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/eq
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/coil
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/tune
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/vox
    ${CMAKE_SOURCE_DIR}/external/pump/plugins/grumble)

file(GLOB_RECURSE PB_CORE_SOURCES CONFIGURE_DEPENDS
    Source/Common/*.cpp
    Source/Engine/*.cpp
    Source/Effects/*.cpp
    Source/Effects/adapters/*.cpp
    Source/Control/*.cpp
    Source/Modular/*.cpp
    Source/Presets/*.cpp)

# ---- Core library shared by the app and the tests ----
add_library(PumpBoardCore STATIC ${PB_CORE_SOURCES} ${PUMP_DSP_SOURCES})
# The plugin links this library into a bundle, so its objects must be position independent. Harmless for the app.
set_target_properties(PumpBoardCore PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
target_include_directories(PumpBoardCore PUBLIC Source ${PUMP_DSP_INCLUDES})
target_link_libraries(PumpBoardCore
    PUBLIC
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
target_compile_definitions(PumpBoardCore PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # JUCE_STANDALONE_APPLICATION deliberately does not live here: it is PUBLIC, so it would follow the core into
    # the plugin and make the shared code think it is an app. juce_add_gui_app sets it for the app target itself,
    # and juce_add_plugin sets it to JucePlugin_Build_Standalone for the plugin (card 8.1).
    $<$<CONFIG:Debug>:PB_ALLOCATION_GUARD=1>)

# ---- Fonts (card 5.27) ----
# JetBrains Mono (SIL OFL), the IDM skin's only face, compiled into the app so it no longer depends on the source tree at runtime and renders
# identically on a machine that has neither font installed. The licence texts ride along in the bundle's Resources.
juce_add_binary_data(PumpBoardFonts
    SOURCES
        external/pump/common/fonts/JetBrainsMono-Regular.ttf
        external/pump/common/fonts/JetBrainsMono-Bold.ttf
    HEADER_NAME FontData.h
    NAMESPACE pumpfonts)

# ---- App ----
juce_add_gui_app(PumpBoard
    PRODUCT_NAME "pump BOARD"
    COMPANY_NAME "wake"
    BUNDLE_ID "xyz.matthettich.pumpboard"
    ICON_BIG "${CMAKE_SOURCE_DIR}/Resources/icon-1024.png"   # one compact pedal in the IDM pink & green skin; source Resources/icon.svg
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "pump BOARD needs audio input to process your instrument.")

file(GLOB_RECURSE PB_APP_SOURCES CONFIGURE_DEPENDS Source/App/*.cpp Source/UI/*.cpp)
target_sources(PumpBoard PRIVATE ${PB_APP_SOURCES})
target_link_libraries(PumpBoard PRIVATE PumpBoardCore PumpBoardFonts juce::juce_gui_extra)

set(PB_FONT_LICENCES
    ${CMAKE_SOURCE_DIR}/external/pump/common/fonts/OFL-JetBrainsMono.txt)
set_source_files_properties(${PB_FONT_LICENCES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_sources(PumpBoard PRIVATE ${PB_FONT_LICENCES})

# ---- Plugin (card 8.1) ----
# A second set of artefacts from the same sources: VST3 and AU beside the app, never instead of it. Its own bundle
# id, its own codes, its own bundle. It links the core and the fonts, and nothing from Source/App — that is where
# START_JUCE_APPLICATION and the audio device live.
juce_add_plugin(PumpBoardPlugin
    PRODUCT_NAME "pump BOARD"
    PLUGIN_NAME "pump BOARD"
    COMPANY_NAME "wake"
    BUNDLE_ID "xyz.matthettich.pumpboard.plugin"
    PLUGIN_MANUFACTURER_CODE Wake
    PLUGIN_CODE Pb01
    DESCRIPTION "pump BOARD — sixteen pump pedals in series"
    FORMATS VST3 AU                        # JUCE only creates the formats this platform can build
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE                  # footswitch, CC and clock mappings arrive from the host
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE       # card 8.5: computer-key mappings only work while the editor has focus
    VST3_CATEGORIES Fx
    # A MIDI-input effect is what JUCE picks for NEEDS_MIDI_INPUT: an AU host files the AU under MIDI-controlled
    # effects. kAudioUnitType_Effect would put it in the plain effects list instead, at the cost of the MIDI the
    # board's mappings need.
    AU_MAIN_TYPE kAudioUnitType_MusicEffect
    COPY_PLUGIN_AFTER_BUILD FALSE)

# Card 8.2: the processor and its session live in a library the plugin and the tests both link. The entry point
# does not: createPluginFilter belongs to the bundle, and a test binary has no host to hand it to.
add_library(PumpBoardPluginSupport STATIC
    Source/Plugin/PluginSession.cpp
    Source/Plugin/PluginProcessor.cpp)
set_target_properties(PumpBoardPluginSupport PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
target_link_libraries(PumpBoardPluginSupport PUBLIC PumpBoardCore)

target_sources(PumpBoardPlugin PRIVATE Source/Plugin/PluginEntry.cpp)
target_link_libraries(PumpBoardPlugin PRIVATE PumpBoardPluginSupport PumpBoardFonts juce::juce_gui_extra)

# ---- Tests ----
juce_add_console_app(PumpBoardTests PRODUCT_NAME "PumpBoardTests")
file(GLOB_RECURSE PB_TEST_SOURCES CONFIGURE_DEPENDS Tests/*.cpp)
target_sources(PumpBoardTests PRIVATE ${PB_TEST_SOURCES})
target_link_libraries(PumpBoardTests PRIVATE PumpBoardCore PumpBoardPluginSupport)
