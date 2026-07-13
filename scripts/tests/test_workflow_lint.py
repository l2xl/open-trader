# XCockpit
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


def test_workflow_installs_runtime_libs_before_running_tests():
    test_job_steps = load()["jobs"]["test"]["steps"]
    test_index = next(i for i, s in enumerate(test_job_steps) if "ctest" in s.get("run", ""))
    deps_index = next(i for i, s in enumerate(test_job_steps) if "apt-get install" in s.get("run", ""))
    assert deps_index < test_index
    assert "libboost-context-dev" in test_job_steps[deps_index]["run"]


@pytest.mark.skipif(shutil.which("actionlint") is None, reason="actionlint not installed")
def test_actionlint_passes():
    result = subprocess.run(["actionlint", str(WORKFLOW)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
