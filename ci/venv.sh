#!/usr/bin/env bash
# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

# Resolves the AI-Scratcher Python interpreter and prints its path on stdout. Never installs the
# package by any means other than cmake/AiScratcher.cmake's fetch logic:
#   1. an already-built application build tree's venv (default cmake-build-debug-clang, override
#      with OPENTRADER_BUILD_DIR) -- building `trader` provisions it, so any contributor who has
#      built the project already has this;
#   2. else, if AI_SCRATCHER_SOURCE_DIR points at an already-fetched source tree (the build job's
#      CPM fetch, passed to CI's syngate job as a build artifact instead of fetching it again), just
#      venv + pip install from it -- no CMake, no network;
#   3. else, fail with instructions. A checkout that has never been configured and built is not a
#      supported way to run `syngate` or the pre-push hook -- configure and build the project first.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

APP_BUILD_DIR="${OPENTRADER_BUILD_DIR:-$ROOT/cmake-build-debug-clang}"
if [[ -x "$APP_BUILD_DIR/ai-scratcher-venv/bin/python" ]]; then
    echo "$APP_BUILD_DIR/ai-scratcher-venv/bin/python"
    exit 0
fi

if [[ -n "${AI_SCRATCHER_SOURCE_DIR:-}" ]]; then
    PROVISION_DIR="$ROOT/.ai-scratcher-provision"
    VENV_DIR="$PROVISION_DIR/ai-scratcher-venv"
    if [[ ! -x "$VENV_DIR/bin/python" ]]; then
        python3 -m venv "$VENV_DIR" >&2
        "$VENV_DIR/bin/pip" install --quiet --disable-pip-version-check "$AI_SCRATCHER_SOURCE_DIR" >&2
    fi
    echo "$VENV_DIR/bin/python"
    exit 0
fi

echo "ci/venv.sh: no AI-Scratcher installation found under $APP_BUILD_DIR." >&2
echo "Configure and build the project first, e.g.:" >&2
echo "  cmake -B $APP_BUILD_DIR -G Ninja -DCMAKE_BUILD_TYPE=Debug" >&2
echo "  cmake --build $APP_BUILD_DIR --target ai_scratcher_venv   # or --target trader, which depends on it" >&2
exit 1
