# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

# ThorVG integration: sources downloaded via CPM, built with ThorVG's native Meson+Ninja toolchain.
#
# ThorVG ships only a Meson build (no CMakeLists.txt), so this module mirrors the SkiaBuild pattern:
# CPM downloads sources only, then custom commands drive the foreign build system in-tree.
#
# Build sequence on first run:
#   1. python3 -m venv      — create build-local Python venv (no global pip install)
#   2. pip install meson    — install meson into that venv (build-tree artifact only)
#   3. meson setup          — configure ThorVG with sw engine, static lib, no tools/tests
#   4. ninja -C <builddir>  — compile libthorvg.a
#
# The venv + meson install run only when the venv stamp is missing.
# The meson setup runs only when the build.ninja is missing.

CPMAddPackage(
    NAME thorvg
    GITHUB_REPOSITORY thorvg/thorvg
    GIT_TAG v1.0.4
    GIT_SHALLOW TRUE
    DOWNLOAD_ONLY YES
)

if(NOT thorvg_SOURCE_DIR)
    message(WARNING "ThorVG could not be downloaded — THORVG_AVAILABLE will be OFF")
    set(THORVG_AVAILABLE OFF)
    return()
endif()

find_program(NINJA_EXECUTABLE ninja REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(THORVG_BUILD_DIR    "${CMAKE_BINARY_DIR}/thorvg-build")
set(THORVG_VENV_DIR     "${CMAKE_BINARY_DIR}/thorvg-venv")
set(THORVG_VENV_STAMP   "${THORVG_VENV_DIR}/.meson-installed.stamp")
set(THORVG_MESON        "${THORVG_VENV_DIR}/bin/meson")
set(THORVG_NINJA_FILE   "${THORVG_BUILD_DIR}/build.ninja")
set(THORVG_LIB          "${THORVG_BUILD_DIR}/src/libthorvg-1.a")
set(THORVG_INCLUDE_DIR  "${thorvg_SOURCE_DIR}/inc")

# Step 1+2: build-local venv with meson — keeps the foreign build tool out of the system Python
add_custom_command(
    OUTPUT  "${THORVG_VENV_STAMP}"
    COMMAND ${Python3_EXECUTABLE} -m venv "${THORVG_VENV_DIR}"
    COMMAND "${THORVG_VENV_DIR}/bin/pip" install --quiet --disable-pip-version-check meson
    COMMAND ${CMAKE_COMMAND} -E touch "${THORVG_VENV_STAMP}"
    COMMENT "Bootstrapping meson into build-local venv for ThorVG..."
    VERBATIM
)

# Step 3: meson setup (re-runs when build.ninja is absent — handles `ninja -t clean` which wipes
# build.ninja but leaves meson's coredata behind, in which case we must `--reconfigure` instead of
# a fresh setup that would otherwise no-op on "Directory already configured")
add_custom_command(
    OUTPUT  "${THORVG_NINJA_FILE}"
    DEPENDS "${THORVG_VENV_STAMP}"
    COMMAND ${CMAKE_COMMAND}
            -DMESON=${THORVG_MESON}
            -DBUILD_DIR=${THORVG_BUILD_DIR}
            -DSRC_DIR=${thorvg_SOURCE_DIR}
            -P "${CMAKE_CURRENT_LIST_DIR}/ThorvgConfigure.cmake"
    COMMENT "Configuring ThorVG (meson setup)..."
    VERBATIM
)

# Step 4: ninja compile (re-runs only when libthorvg.a is absent)
add_custom_command(
    OUTPUT  "${THORVG_LIB}"
    DEPENDS "${THORVG_NINJA_FILE}"
    COMMAND ${NINJA_EXECUTABLE} -C "${THORVG_BUILD_DIR}"
    COMMENT "Building ThorVG (ninja)..."
    VERBATIM
)

add_custom_target(thorvg_build ALL DEPENDS "${THORVG_LIB}")

file(MAKE_DIRECTORY "${THORVG_INCLUDE_DIR}")

add_library(Thorvg::thorvg STATIC IMPORTED GLOBAL)
set_target_properties(Thorvg::thorvg PROPERTIES
    IMPORTED_LOCATION "${THORVG_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${THORVG_INCLUDE_DIR}"
)
add_dependencies(Thorvg::thorvg thorvg_build)

set(THORVG_AVAILABLE ON)
message(STATUS "ThorVG: ${THORVG_LIB}")
