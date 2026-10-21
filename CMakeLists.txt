cmake_minimum_required(VERSION 3.28)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(comp1 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# Sources and their string literals are UTF-8 on every compiler.
if(MSVC)
    add_compile_options(/utf-8)
endif()
# MSVC refuses an initializer list that mixes designated and positional members, where clang only warns: clang
# makes it an error too, so a Mac build can't pass what Windows fails. comp1's own directories add these options
# after fetching their dependencies, whose flags stay as they are.
set(comp1_compile_options "$<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang>:-Werror=c99-designator>")

# The options plugkit's check harness sets (plugkit's README.md, "What the plugin stages expect").
option(COMP1_BUILD_PLUGIN "Build the Plugin Shell (needs JUCE)" ON)
option(COMP1_BUILD_TESTS "Build the tests" ON)
# Link-time optimisation is for what ships; the check harness turns it off, as it makes MSVC's links slow.
option(COMP1_LTO "Build the Plugin Shell with link-time optimisation" ON)
# Left empty, every configuration but Debug strips (keeping a .dSYM or PDB beside each bundle); ON or OFF applies
# to every configuration. The check harness sets OFF, so its Release test builds keep their symbols.
set(COMP1_STRIP "" CACHE STRING
    "Strip the Plugin Shell's binaries, keeping a .dSYM or PDB beside each bundle: ON, OFF, or empty for every configuration but Debug")

include(FetchContent)

# A .deps folder is shared by universal builds, dependencies' build folders included; a build for other
# architectures would rebuild those libraries for its own alone and break the others' links.
if(APPLE AND DEFINED FETCHCONTENT_BASE_DIR)
    get_filename_component(comp1_deps_name "${FETCHCONTENT_BASE_DIR}" NAME)
    if(comp1_deps_name STREQUAL ".deps" AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64;x86_64")
        message(FATAL_ERROR "FETCHCONTENT_BASE_DIR ${FETCHCONTENT_BASE_DIR} is shared by universal builds, "
            "and this one builds for '${CMAKE_OSX_ARCHITECTURES}'. Configure with "
            "-DCMAKE_OSX_ARCHITECTURES=\"arm64;x86_64\" (the check harness does), or with a folder of its own: "
            "-DFETCHCONTENT_BASE_DIR=${FETCHCONTENT_BASE_DIR}/<name> -DFETCHCONTENT_SOURCE_DIR_<DEP>=<its -src folder>.")
    endif()
endif()

# plugkit, at the one tag plugkit.conf pins (ADR 0007): CI clones the same tag for the check harness, so the two
# can't drift. A changed pin reconfigures.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/plugkit.conf")
file(STRINGS "${PROJECT_SOURCE_DIR}/plugkit.conf" comp1_plugkit_tag REGEX "^PLUGKIT_TAG=")
string(REPLACE "PLUGKIT_TAG=" "" comp1_plugkit_tag "${comp1_plugkit_tag}")
if(NOT comp1_plugkit_tag MATCHES "^(v[0-9]+\\.[0-9]+\\.[0-9]+|[0-9a-f]{40})$")
    message(FATAL_ERROR "plugkit.conf pins plugkit to '${comp1_plugkit_tag}': PLUGKIT_TAG must be a release tag (vX.Y.Z) "
        "or a full commit hash, never a branch (ADR 0007)")
endif()
FetchContent_Declare(plugkit
    GIT_REPOSITORY https://github.com/Patrick-M-Bourget/plugkit.git
    GIT_TAG "${comp1_plugkit_tag}"
    GIT_SHALLOW TRUE
    SYSTEM
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(plugkit)

add_subdirectory(engine)

if(COMP1_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()

if(COMP1_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
