# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

# AI-Scratcher integration: sources fetched via CPM, provisioned into a build-local Python venv
# with pip. AI-Scratcher ships no CMakeLists.txt of its own; this module mirrors the
# ThorvgBuild.cmake venv pattern.
#
# This is the single place AI-Scratcher is installed from. Building `trader` (the project's
# documented build command) provisions this venv as a dependency (see the trader target below), so
# ci/venv.sh's "reuse an existing build tree's venv" path just works for any contributor who has
# built the project -- which every contributor is expected to do; there is no separate install path
# for someone who has not.
#
# Tracks GIT_TAG main, like the Elements dependency above: FetchContent re-fetches a branch tag on
# every configure, so a reconfigure moves _deps/ai_scratcher-src to the current tip of main. The
# venv is keyed to the fetched revision below: a configure that lands a different revision removes
# the venv outright, so nothing can run a stale toolkit between that configure and the build that
# re-provisions it. Override with a local checkout via CPM's own mechanism:
# -DCPM_ai_scratcher_SOURCE=<path>.

option(OPENTRADER_AI_SCRATCHER "Fetch and provision the AI-Scratcher toolkit (Syngate + AI agent)" ON)

if(NOT OPENTRADER_AI_SCRATCHER)
    set(AI_SCRATCHER_AVAILABLE OFF)
    return()
endif()

CPMAddPackage(
    NAME ai_scratcher
    GITHUB_REPOSITORY l2xl/ai-scratcher
    GIT_TAG main
    DOWNLOAD_ONLY YES
)

if(NOT ai_scratcher_SOURCE_DIR)
    message(WARNING "AI-Scratcher could not be downloaded — AI_SCRATCHER_AVAILABLE will be OFF")
    set(AI_SCRATCHER_AVAILABLE OFF)
    return()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(AI_SCRATCHER_VENV_DIR   "${CMAKE_BINARY_DIR}/ai-scratcher-venv")
set(AI_SCRATCHER_VENV_STAMP "${AI_SCRATCHER_VENV_DIR}/.ai-scratcher-installed.stamp")

execute_process(
    COMMAND git -C "${ai_scratcher_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE AI_SCRATCHER_FETCHED_REV
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT AI_SCRATCHER_FETCHED_REV STREQUAL "${AI_SCRATCHER_VENV_REV}")
    file(REMOVE_RECURSE "${AI_SCRATCHER_VENV_DIR}")
    set(AI_SCRATCHER_VENV_REV "${AI_SCRATCHER_FETCHED_REV}" CACHE INTERNAL "ai-scratcher revision the build-local venv is provisioned from")
endif()

# The stamp is the custom command's OUTPUT: it exists only to give ninja a file whose absence or
# age (against pyproject.toml, for a local-checkout override edited in place) re-runs the install.
# The install always starts from an empty venv so a rebuild never keeps packages a newer revision
# dropped.
add_custom_command(
    OUTPUT  "${AI_SCRATCHER_VENV_STAMP}"
    DEPENDS "${ai_scratcher_SOURCE_DIR}/pyproject.toml"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${AI_SCRATCHER_VENV_DIR}"
    COMMAND ${Python3_EXECUTABLE} -m venv "${AI_SCRATCHER_VENV_DIR}"
    COMMAND "${AI_SCRATCHER_VENV_DIR}/bin/pip" install --quiet --disable-pip-version-check "${ai_scratcher_SOURCE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${AI_SCRATCHER_VENV_STAMP}"
    COMMENT "Provisioning AI-Scratcher (syngate + agent) into a build-local venv..."
    VERBATIM
)

add_custom_target(ai_scratcher_venv ALL DEPENDS "${AI_SCRATCHER_VENV_STAMP}")

set(AI_SCRATCHER_PYTHON "${AI_SCRATCHER_VENV_DIR}/bin/python" CACHE FILEPATH "Python interpreter with ai_scratcher installed" FORCE)
set(AI_SCRATCHER_AVAILABLE ON)
