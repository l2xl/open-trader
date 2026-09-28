# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Structural lint for the Validate GitHub Actions workflow."""

import json
import shutil
import subprocess
from pathlib import Path

import pytest

from workflow_doc import WORKFLOW, load, steps

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
RULESET = REPO_ROOT / ".github" / "rulesets" / "main.json"


def test_license_build_test_syngate_is_the_whole_pipeline():
    doc = load()
    assert list(doc["jobs"]) == ["license", "build", "test", "syngate"]
    assert "needs" not in doc["jobs"]["license"]
    assert "needs" not in doc["jobs"]["build"]
    assert "if" not in doc["jobs"]["build"]
    assert doc["jobs"]["test"]["needs"] == "build"
    assert doc["jobs"]["syngate"]["needs"] == ["build", "test"]
    # Downstream jobs condition on their needs' actual results: the implicit
    # success() also demands every transitive ancestor succeeded, so a skipped
    # ancestor would silently skip the job (and syngate then fails
    # downloading the never-uploaded ctest-results artifact).
    assert "needs.build.result == 'success'" in doc["jobs"]["test"]["if"]
    assert "needs.build.result == 'success'" in doc["jobs"]["syngate"]["if"]


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


@pytest.mark.syngate("INFRA-071")
def test_license_and_reapproval_are_required_status_checks_on_main():
    doc = load()
    assert "check_license.py --check" in " | ".join(steps("license"))
    assert "check_self_approval.py" in " | ".join(steps("syngate"))

    ruleset = json.loads(RULESET.read_text())
    assert ruleset["enforcement"] == "active"
    assert ruleset["bypass_actors"] == []
    assert ruleset["conditions"]["ref_name"]["include"] == ["~DEFAULT_BRANCH"]
    checks = next(r for r in ruleset["rules"] if r["type"] == "required_status_checks")
    required = {c["context"] for c in checks["parameters"]["required_status_checks"]}
    assert required == set(doc["jobs"])
