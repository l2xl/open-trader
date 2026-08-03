# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Structural lint for the Validate GitHub Actions workflow."""

import shutil
import subprocess

import pytest

from workflow_doc import WORKFLOW, load


def test_workflow_entry_checks_gate_the_sequential_pipeline_jobs():
    doc = load()
    assert list(doc["jobs"]) == ["license", "approval", "build", "test", "requirements"]
    assert "needs" not in doc["jobs"]["license"]
    assert "needs" not in doc["jobs"]["approval"]
    assert doc["jobs"]["build"]["needs"] == ["license", "approval"]
    assert "needs.license.result == 'success'" in doc["jobs"]["build"]["if"]
    assert doc["jobs"]["test"]["needs"] == "build"
    assert doc["jobs"]["requirements"]["needs"] == ["build", "test"]
    # Downstream jobs condition on their needs' actual results: the implicit
    # success() also demands every transitive ancestor succeeded, so a skipped
    # ancestor would silently skip the job (and requirements then fails
    # downloading the never-uploaded ctest-results artifact).
    assert "needs.build.result == 'success'" in doc["jobs"]["test"]["if"]
    assert "needs.build.result == 'success'" in doc["jobs"]["requirements"]["if"]


def test_ctest_runs_in_the_same_pinned_toolchain_container_as_the_build():
    # The runtime libs the tests link against are no longer apt-installed per
    # job: they come from the build image. ctest also needs the workspace at the
    # same absolute path CMake baked in, which only holds inside that container.
    doc = load()
    build_image = doc["jobs"]["build"]["container"]["image"]
    test_image = doc["jobs"]["test"]["container"]["image"]
    assert build_image == test_image
    assert "@sha256:" in build_image  # pinned by digest, not a mutable tag
    assert any("ctest" in s.get("run", "") for s in doc["jobs"]["test"]["steps"])


@pytest.mark.skipif(shutil.which("actionlint") is None, reason="actionlint not installed")
def test_actionlint_passes():
    result = subprocess.run(["actionlint", str(WORKFLOW)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
