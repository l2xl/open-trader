# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""AI-Scratcher toolkit provisioning -- [AI-SCRATCHER-GATE-010] the gate resolves it, [AI-SCRATCHER-SERVER-010] cmake installs it."""

from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parent.parent.parent


@pytest.mark.syngate("AI-SCRATCHER-GATE-010")
def test_gate_resolves_the_cmake_provisioned_interpreter():
    venv_sh = (ROOT / "ci" / "venv.sh").read_text()
    assert "ai-scratcher-venv/bin/python" in venv_sh  # tier 1: reuse an already-built app tree
    assert "AI_SCRATCHER_SOURCE_DIR" in venv_sh  # tier 2: CI's build-artifact-supplied source
    assert "no AI-Scratcher installation found" in venv_sh  # else: fail with instructions, no fetch
    # The only two installs of the package: CMake's own custom command (AiScratcher.cmake) and
    # this script's tier-2 local install FROM a source that command already fetched -- never an
    # independent fetch of AI-Scratcher outside cmake/AiScratcher.cmake.
    assert 'pip" install --quiet --disable-pip-version-check "$AI_SCRATCHER_SOURCE_DIR"' in venv_sh
    gate_sh = (ROOT / "ci" / "gate.sh").read_text()
    assert 'PY="$(bash "$ROOT/ci/venv.sh")"' in gate_sh


@pytest.mark.syngate("AI-SCRATCHER-SERVER-010")
def test_build_fetches_ai_scratcher_via_cpm_into_a_venv():
    cmake_lists = (ROOT / "CMakeLists.txt").read_text()
    assert "cmake/AiScratcher.cmake" in cmake_lists
    # Building trader provisions the venv as a dependency, so any contributor who builds the
    # project (the one setup step expected of everyone) already has ci/venv.sh's tier 1 covered.
    assert "add_dependencies(trader ai_scratcher_venv)" in cmake_lists
    module = (ROOT / "cmake" / "AiScratcher.cmake").read_text()
    assert "CPMAddPackage(" in module
    assert "NAME ai_scratcher" in module
    assert "GITHUB_REPOSITORY l2xl/ai-scratcher" in module
    assert "GIT_TAG main" in module
    assert "-m venv" in module
    assert 'pip" install' in module
    assert "${ai_scratcher_SOURCE_DIR}" in module
