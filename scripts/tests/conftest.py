# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Shared fixtures for the requirements-tooling suite + coverage JSONL emitter.

Tests bind to requirements with @pytest.mark.req("UID"[, "binding_name"]).
When REQ_COVERAGE_FILE is set, each executed req-marked test appends a
{"tags": [...], "passed": bool, "name": nodeid} record for `req report`.
"""

import json
import os
import sys
from pathlib import Path

import pytest
import yaml

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))


def pytest_configure(config):
    config.addinivalue_line("markers", "req(uid, name=None): bind this test to a requirement leaf binding")


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    report = outcome.get_result()
    coverage_file = os.environ.get("REQ_COVERAGE_FILE")
    if not coverage_file or report.when != "call":
        return
    for marker in item.iter_markers("req"):
        tags = [str(arg) for arg in marker.args]
        if not tags:
            continue
        log = "" if report.passed else report.longreprtext[-4000:]
        record = {"tags": tags, "passed": report.passed, "name": item.nodeid, "log": log}
        with open(coverage_file, "a", encoding="utf-8") as f:
            f.write(json.dumps(record) + "\n")


def make_item(req_dir, uid, description, parents=(), header="", order=0, tests="absent", reviewed=None):
    """Write a new-schema requirement item file; tests: 'absent' | None | sha | {name: sha}."""
    req_dir.mkdir(parents=True, exist_ok=True)
    data = {"header": header, "description": description, "parents": list(parents)}
    if order:
        data["order"] = order
    if tests != "absent":
        data["tests"] = tests
    if reviewed is not None:
        data["reviewed"] = reviewed
    (req_dir / f"{uid}.yml").write_text(yaml.safe_dump(data, sort_keys=False, allow_unicode=True))


@pytest.fixture
def req_tree(tmp_path):
    """Yields (req_dir, make_item); load with reqlib.load_tree(req_dir) once built."""
    req_dir = tmp_path / "req"
    req_dir.mkdir()
    return req_dir, make_item
