# Skia integration: sources downloaded via CPM, built with Skia's native GN+Ninja toolchain.
# Skia is always built in release mode regardless of the project build type —
# Skia's software rasterizer is dramatically slower without full optimization.
#
# Build sequence on first run:
#   1. git-sync-deps   — clones ~45 third-party repos into third_party/externals/
#                        and downloads the pre-built bin/gn binary from CIPD
#   2. gn gen          — configures the GN build
#   3. ninja           — compiles libskia.a
#   4. gn cmake-ide    — GN→CMake translator for IDE source navigation
#
# git-sync-deps runs only when DEPS changes (stamp file).
# A git wrapper adds --force to every checkout so re-runs never fail on
# untracked files left behind by a previous partial sync or a DEPS update.

CPMAddPackage(
    NAME skia
    GIT_REPOSITORY https://skia.googlesource.com/skia.git
    GIT_TAG main
    GIT_SHALLOW TRUE
    DOWNLOAD_ONLY YES
)

if(NOT skia_SOURCE_DIR)
    message(WARNING "Skia could not be downloaded — SKIA_AVAILABLE will be OFF")
    return()
endif()

find_program(NINJA_EXECUTABLE ninja REQUIRED)

# GN build args — release build is mandatory; clang is required for optimised Skia backends
set(SKIA_GN_ARGS_LIST
    "cc=\"clang-23\""
    "cxx=\"clang++-23\""
    "is_debug=false"
    "skia_use_system_freetype2=false"
    "skia_use_system_harfbuzz=false"
)
string(JOIN " " SKIA_GN_ARGS ${SKIA_GN_ARGS_LIST})

set(SKIA_OUT_SUBDIR  "out/release")
set(SKIA_IDE_SUBDIR  "out/cmake-ide")
set(SKIA_LIB         "${skia_SOURCE_DIR}/${SKIA_OUT_SUBDIR}/libskia.a")
set(SKIA_IDE_CMAKE   "${skia_SOURCE_DIR}/${SKIA_IDE_SUBDIR}/CMakeLists.txt")
set(SKIA_DEPS_STAMP  "${CMAKE_BINARY_DIR}/skia-deps.stamp")

# git wrapper: adds --force to every checkout so untracked files never block a dep update
set(_skia_git_wrapper "${CMAKE_BINARY_DIR}/skia_git_force.sh")
file(WRITE "${_skia_git_wrapper}" [=[#!/bin/bash
# Wraps git to inject --force into checkout commands for Skia dep syncing.
# Required because git-sync-deps runs plain 'git checkout <hash>' which refuses
# to overwrite untracked files left by a previous partial sync or DEPS update.
args=()
for arg in "$@"; do
    args+=("$arg")
    [ "$arg" = "checkout" ] && args+=("--force")
done
exec git "${args[@]}"
]=])
file(CHMOD "${_skia_git_wrapper}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# Step 1: sync deps (re-runs only when DEPS changes; also downloads bin/gn via fetch-gn)
add_custom_command(
    OUTPUT  "${SKIA_DEPS_STAMP}"
    DEPENDS "${skia_SOURCE_DIR}/DEPS"
    COMMAND ${CMAKE_COMMAND} -E env "GIT_EXECUTABLE=${_skia_git_wrapper}" python3 tools/git-sync-deps
    COMMAND ${CMAKE_COMMAND} -E touch "${SKIA_DEPS_STAMP}"
    WORKING_DIRECTORY "${skia_SOURCE_DIR}"
    COMMENT "Syncing Skia third-party dependencies..."
    VERBATIM
)

# Steps 2+3: gn gen → ninja (re-runs only when libskia.a is absent)
add_custom_command(
    OUTPUT  "${SKIA_LIB}"
    DEPENDS "${SKIA_DEPS_STAMP}"
    COMMAND "${skia_SOURCE_DIR}/bin/gn" gen "${SKIA_OUT_SUBDIR}" "--args=${SKIA_GN_ARGS}"
    COMMAND ${NINJA_EXECUTABLE} -C "${SKIA_OUT_SUBDIR}" skia
    WORKING_DIRECTORY "${skia_SOURCE_DIR}"
    COMMENT "Building Skia release (gn gen → ninja)..."
    VERBATIM
)

# Step 4: GN→CMake translator for IDE source navigation (re-runs only when libskia.a changes)
add_custom_command(
    OUTPUT  "${SKIA_IDE_CMAKE}"
    DEPENDS "${SKIA_LIB}"
    COMMAND "${skia_SOURCE_DIR}/bin/gn" gen "${SKIA_IDE_SUBDIR}"
            --ide=json
            "--json-ide-script=${skia_SOURCE_DIR}/gn/gn_to_cmake.py"
    WORKING_DIRECTORY "${skia_SOURCE_DIR}"
    COMMENT "Running GN→CMake translator for Skia IDE support..."
    VERBATIM
)

add_custom_target(skia_build ALL DEPENDS "${SKIA_LIB}" "${SKIA_IDE_CMAKE}")

add_library(Skia::skia STATIC IMPORTED GLOBAL)
set_target_properties(Skia::skia PROPERTIES
    IMPORTED_LOCATION "${SKIA_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${skia_SOURCE_DIR}"
)
add_dependencies(Skia::skia skia_build)

set(SKIA_AVAILABLE ON)
message(STATUS "Skia: ${SKIA_LIB}")