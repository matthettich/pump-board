# The VST3 SDK ships the validator a host's implementer would use: it loads the bundle and runs the SDK's own test
# suite over it (factory, busses, parameters, automation, processing, state, threading). Nothing on this machine or in
# CI could run it before, so a bundle could be missing a manifest, mismatched or silently unloadable and the build
# still went green.
#
# Two things this file deliberately does NOT do. It does not use the SDK's own cmake framework: that one wants to be
# the top-level project, pulls in its GUI library and pins CMake 3.25, and none of it is needed to compile one
# executable. And it does not fetch anything: the SDK is a submodule pinned at a release tag, so the version that
# validates the bundle is the version in the repository's history, not whatever is newest on the day CI runs.
#
# Built on its own, not as part of the app's CMakeLists:
#
#     git submodule update --init external/vst3-sdk
#     git -C external/vst3-sdk submodule update --init base pluginterfaces public.sdk
#     cmake -S tools/vst3-validator -B build-validator -DCMAKE_BUILD_TYPE=Release
#     cmake --build build-validator
#     ./build-validator/PumpBoardVst3Validator -selftest
#     ./build-validator/PumpBoardVst3Validator "/path/to/pump BOARD.vst3"
#
# The app and the plugin do not link any of this: it is a build time and CI tool. See docs/NOTES.md, card 8.9.

cmake_minimum_required(VERSION 3.24)
project(PumpBoardVst3Validator LANGUAGES C CXX)

set(PB_VST3_SDK "${CMAKE_CURRENT_LIST_DIR}/../../external/vst3-sdk")

# The failure this catches is the ordinary one: a fresh clone, or a CI checkout that did not initialise the submodule.
# Say what to type rather than dying inside a missing include twenty minutes later.
if(NOT EXISTS "${PB_VST3_SDK}/public.sdk/source/vst/testsuite/vsttestsuite.cpp")
    message(FATAL_ERROR
        "The VST3 SDK submodule is not checked out at ${PB_VST3_SDK}.\n"
        "Run: git submodule update --init external/vst3-sdk\n"
        "then: git -C external/vst3-sdk submodule update --init base pluginterfaces public.sdk")
endif()

# The SDK is split into the same layers its own cmake builds separately (base, the interfaces, the shared code, the
# plug-in side and the host side). One executable rather than five static libraries: the validator is the only thing
# linked from them, and a single target has no link order to get wrong.
set(PB_VST3_SDK_SOURCES
    # base: strings, streams, the class id tables, the threading primitives
    ${PB_VST3_SDK}/base/source/baseiids.cpp
    ${PB_VST3_SDK}/base/source/fbuffer.cpp
    ${PB_VST3_SDK}/base/source/fdebug.cpp
    ${PB_VST3_SDK}/base/source/fdynlib.cpp
    ${PB_VST3_SDK}/base/source/fobject.cpp
    ${PB_VST3_SDK}/base/source/fstreamer.cpp
    ${PB_VST3_SDK}/base/source/fstring.cpp
    ${PB_VST3_SDK}/base/source/timer.cpp
    ${PB_VST3_SDK}/base/source/updatehandler.cpp
    ${PB_VST3_SDK}/base/thread/source/fcondition.cpp
    ${PB_VST3_SDK}/base/thread/source/flock.cpp

    # pluginterfaces: the interfaces themselves, plus their ids
    ${PB_VST3_SDK}/pluginterfaces/base/conststringtable.cpp
    ${PB_VST3_SDK}/pluginterfaces/base/coreiids.cpp
    ${PB_VST3_SDK}/pluginterfaces/base/funknown.cpp
    ${PB_VST3_SDK}/pluginterfaces/base/ustring.cpp

    # the SDK's shared helpers
    ${PB_VST3_SDK}/public.sdk/source/common/commoniids.cpp
    ${PB_VST3_SDK}/public.sdk/source/common/commonstringconvert.cpp
    ${PB_VST3_SDK}/public.sdk/source/common/memorystream.cpp
    ${PB_VST3_SDK}/public.sdk/source/common/openurl.cpp
    ${PB_VST3_SDK}/public.sdk/source/common/readfile.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/vstpresetfile.cpp

    # the plug-in side of the SDK, which the tests instantiate the host side of
    ${PB_VST3_SDK}/public.sdk/source/common/pluginview.cpp
    ${PB_VST3_SDK}/public.sdk/source/main/pluginfactory.cpp
    ${PB_VST3_SDK}/public.sdk/source/main/moduleinit.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/utility/dataexchange.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/utility/stringconvert.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/utility/systemtime.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/utility/testing.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/utility/vst2persistence.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/vstaudioeffect.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/vstbus.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/vstcomponent.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/vstcomponentbase.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/vsteditcontroller.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/vstinitiids.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/vstnoteexpressiontypes.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/vstparameters.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/vstrepresentation.cpp

    # the host side: loading the bundle, hosting it, driving it
    ${PB_VST3_SDK}/public.sdk/source/vst/hosting/connectionproxy.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/hosting/eventlist.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/hosting/module.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/hosting/parameterchanges.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/hosting/processdata.cpp
    ${PB_VST3_SDK}/public.sdk/source/vst/moduleinfo/moduleinfoparser.cpp

    # the validator itself: the command line, and the tests it runs
    ${PB_VST3_SDK}/public.sdk/samples/vst-hosting/validator/source/main.cpp
    ${PB_VST3_SDK}/public.sdk/samples/vst-hosting/validator/source/usediids.cpp
    ${PB_VST3_SDK}/public.sdk/samples/vst-hosting/validator/source/validator.cpp
)

# The test suite is a directory per area (bus, processing, state, unit...) and the SDK adds files to it between
# releases. Globbed, so a version bump does not silently drop the new tests from the build.
file(GLOB_RECURSE PB_VST3_TESTSUITE_SOURCES CONFIGURE_DEPENDS
    ${PB_VST3_SDK}/public.sdk/source/vst/testsuite/*.cpp)
file(GLOB PB_VST3_HOSTING_TEST_SOURCES CONFIGURE_DEPENDS
    ${PB_VST3_SDK}/public.sdk/source/vst/hosting/test/*.cpp)
list(APPEND PB_VST3_SDK_SOURCES ${PB_VST3_TESTSUITE_SOURCES} ${PB_VST3_HOSTING_TEST_SOURCES})

# The module loader is the one genuinely platform specific piece: it opens the bundle and reaches its factory entry
# point. Each platform gets its own file, and macOS gets two more for thread checking and the clipboard.
if(APPLE)
    enable_language(OBJCXX)
    list(APPEND PB_VST3_SDK_SOURCES
        ${PB_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm
        ${PB_VST3_SDK}/public.sdk/source/common/threadchecker_mac.mm
        ${PB_VST3_SDK}/public.sdk/source/common/systemclipboard_mac.mm)
    # The SDK marks its own Objective-C++ as ARC, and so must we: mixed sources in one target get one mode.
    set_source_files_properties(
        ${PB_VST3_SDK}/public.sdk/source/vst/hosting/module_mac.mm
        ${PB_VST3_SDK}/public.sdk/source/common/threadchecker_mac.mm
        ${PB_VST3_SDK}/public.sdk/source/common/systemclipboard_mac.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    find_library(PB_COCOA_FRAMEWORK Cocoa REQUIRED)
elseif(WIN32)
    list(APPEND PB_VST3_SDK_SOURCES
        ${PB_VST3_SDK}/public.sdk/source/vst/hosting/module_win32.cpp
        ${PB_VST3_SDK}/public.sdk/source/common/threadchecker_win32.cpp
        ${PB_VST3_SDK}/public.sdk/source/common/systemclipboard_win32.cpp)
else()
    list(APPEND PB_VST3_SDK_SOURCES
        ${PB_VST3_SDK}/public.sdk/source/vst/hosting/module_linux.cpp
        ${PB_VST3_SDK}/public.sdk/source/common/threadchecker_linux.cpp
        ${PB_VST3_SDK}/public.sdk/source/common/systemclipboard_linux.cpp)
endif()

add_executable(PumpBoardVst3Validator ${PB_VST3_SDK_SOURCES})

# The SDK includes its own headers as "pluginterfaces/vst/..." and "public.sdk/source/...", so the root is the include
# path, not a subdirectory of it.
target_include_directories(PumpBoardVst3Validator PRIVATE ${PB_VST3_SDK})
set_target_properties(PumpBoardVst3Validator PROPERTIES
    CXX_STANDARD 17                      # what the SDK is written against; it is not ours to make C++20
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# Its own assertions are compiled in or out by these, the same two the SDK's cmake passes.
target_compile_definitions(PumpBoardVst3Validator PRIVATE
    $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)
if(APPLE)
    target_link_libraries(PumpBoardVst3Validator PRIVATE ${PB_COCOA_FRAMEWORK})
elseif(NOT WIN32)
    target_link_libraries(PumpBoardVst3Validator PRIVATE ${CMAKE_DL_LIBS})
endif()

# Third party code, not this repository's: the house rule about zero warnings covers Source/ and Tests/, and a wall of
# warnings from someone else's headers would bury the one line that matters. Errors still fail the build.
if(MSVC)
    target_compile_options(PumpBoardVst3Validator PRIVATE /W0)
else()
    target_compile_options(PumpBoardVst3Validator PRIVATE -w)
endif()
