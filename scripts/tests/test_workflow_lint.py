# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Tests for the requirements-gate GitHub Actions workflow -- [INFRA-066] [INFRA-067] [INFRA-068] [INFRA-069]."""

import shutil
import subprocess
from pathlib import Path

import pytest
import yaml

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
WORKFLOW = REPO_ROOT / ".github" / "workflows" / "requirements-gate.yml"


def _doc():
    return yaml.safe_load(WORKFLOW.read_text())


def _steps(job):
    return [f"{s.get('name', '')} {s.get('uses', '')} {s.get('run', '')}" for s in _doc()["jobs"][job]["steps"]]


def test_workflow_has_three_sequential_jobs():
    doc = _doc()
    assert list(doc["jobs"]) == ["build", "test", "requirements"]
    assert doc["jobs"]["test"]["needs"] == "build"
    assert doc["jobs"]["requirements"]["needs"] == ["build", "test"]


def test_workflow_builds_on_push_and_pull_request():
    doc = _doc()
    assert "push" in doc[True] and "pull_request" in doc[True]
    build_steps = " | ".join(_steps("build"))
    assert "cmake --build" in build_steps
    assert "unit_tests" in build_steps


def test_workflow_runs_offline_tests_against_built_tree():
    build_steps = " | ".join(_steps("build"))
    test_steps = " | ".join(_steps("test"))
    assert "upload-artifact" in build_steps and "build-tree" in build_steps
    assert "download-artifact" in test_steps and "build-tree" in test_steps
    assert "ctest" in test_steps
    assert "-LE live" in test_steps


def test_workflow_runs_status_rollup_over_both_junit_reports():
    steps = _steps("requirements")
    assert any("pytest" in s.lower() for s in steps)
    status_step = next(s for s in steps if "req_status.py" in s)
    assert "--junit pytest-results.xml" in status_step
    assert "--junit build-ci/ctest-results.xml" in status_step


def test_workflow_publishes_status_as_run_artifact():
    uploads = [s for s in _doc()["jobs"]["requirements"]["steps"] if "upload-artifact" in s.get("uses", "")]
    assert any("req_status.json" in s.get("with", {}).get("path", "") for s in uploads)


def test_workflow_renders_status_report_on_the_job_report_page():
    steps = " | ".join(_steps("requirements"))
    assert "write_status_summary.py" in steps


def test_workflow_publishes_requirements_tree_on_the_commit():
    steps = " | ".join(_steps("requirements"))
    assert "publish_check_run.py --status req_status.json" in steps


def test_workflow_has_no_github_pages_publication():
    doc = _doc()
    steps = " | ".join(s for job in doc["jobs"] for s in _steps(job))
    assert "pages" not in steps.lower()


@pytest.mark.skipif(shutil.which("actionlint") is None, reason="actionlint not installed")
def test_actionlint_passes():
    result = subprocess.run(["actionlint", str(WORKFLOW)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
