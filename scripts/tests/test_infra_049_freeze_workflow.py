# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""User approval freezes a requirement with fingerprints; frozen changes are caught and re-approval is a separate commit -- [INFRA-049]."""

import hashlib
import subprocess

import doorstop
import yaml

from check_frozen_tests import run as frozen_run
from check_self_approval import run as approval_run
from workflow_doc import load, steps


def _make_reviewed_item(tree_builder, text="def test_x(): pass\n"):
    base, make_document, make_item = tree_builder
    make_document(base, "req/infra", "INFRA", extensions={"item_sha_required": True})
    (base / "test_thing.py").write_text(text)
    make_item(
        base / "req/infra", "INFRA-049", "1.4.0", "leaf", normative=True, verify="test",
        references=[{"path": "test_thing.py", "type": "file", "keyword": "INFRA-049"}],
    )
    doorstop.build(root=str(base)).find_item("INFRA-049").review()
    return base


def test_review_stamps_the_item_and_its_bound_test_file_fingerprints(tree_builder):
    base = _make_reviewed_item(tree_builder)
    data = yaml.safe_load((base / "req/infra" / "INFRA-049.yml").read_text())
    assert data["reviewed"]
    assert data["references"][0]["sha"] == hashlib.sha256((base / "test_thing.py").read_bytes()).hexdigest()
    assert frozen_run(base) == 0


def test_modified_frozen_reference_fails(tree_builder):
    base = _make_reviewed_item(tree_builder)
    (base / "test_thing.py").write_text("def test_x(): pass  # silently edited\n")
    assert frozen_run(base) == 1


def test_unreviewed_item_is_ignored_even_if_modified(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "req/infra", "INFRA", extensions={"item_sha_required": True})
    (base / "test_thing.py").write_text("def test_x(): pass\n")
    make_item(
        base / "req/infra", "INFRA-049", "1.4.0", "leaf", normative=True, verify="test",
        references=[{"path": "test_thing.py", "type": "file", "keyword": "INFRA-049"}],
    )
    (base / "test_thing.py").write_text("def test_x(): pass  # edited before any review\n")
    assert frozen_run(base) == 0


def _git(base, *args):
    subprocess.run(["git", *args], cwd=base, capture_output=True, text=True, check=True)


def _commit_all(base, message):
    _git(base, "add", "-A")
    _git(base, "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-qm", message, "--no-verify")


def _history_fixture(tree_builder):
    base = _make_reviewed_item(tree_builder)
    _git(base, "init", "-q")
    _commit_all(base, "freeze")
    return base


def _restamp(base):
    item = doorstop.build(root=str(base)).find_item("INFRA-049")
    item.clear()
    item.review()


def test_edit_and_restamp_in_one_commit_is_rejected(tree_builder):
    base = _history_fixture(tree_builder)
    (base / "test_thing.py").write_text("def test_x(): pass  # edited\n")
    _restamp(base)
    _commit_all(base, "self-approving edit")
    assert approval_run("HEAD^..HEAD", cwd=base) == 1


def test_unreviewed_item_edit_with_its_test_in_one_commit_passes(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "req/infra", "INFRA", extensions={"item_sha_required": True})
    (base / "test_thing.py").write_text("def test_x(): pass\n")
    make_item(base / "req/infra", "INFRA-049", "1.4.0", "leaf", normative=True, verify="test")
    _git(base, "init", "-q")
    _commit_all(base, "draft item")
    (base / "test_thing.py").write_text("def test_x(): pass  # evolved with the draft\n")
    make_item(
        base / "req/infra", "INFRA-049", "1.4.0", "leaf reworded", normative=True, verify="test",
        references=[{"path": "test_thing.py", "type": "file", "keyword": "INFRA-049"}],
    )
    _commit_all(base, "rework unreviewed item and its test together")
    assert approval_run("HEAD^..HEAD", cwd=base) == 0


def test_edit_and_restamp_split_into_two_commits_passes(tree_builder):
    base = _history_fixture(tree_builder)
    (base / "test_thing.py").write_text("def test_x(): pass  # edited\n")
    _commit_all(base, "edit under review")
    _restamp(base)
    _commit_all(base, "user-approved re-stamp")
    assert approval_run("HEAD~2..HEAD", cwd=base) == 0


def test_ci_runs_the_self_approval_check_on_pull_requests_and_non_main_pushes():
    job = load()["jobs"]["approval"]
    assert "pull_request" in job["if"] and "refs/heads/main" in job["if"]
    joined = " | ".join(steps("approval"))
    assert "fetch-depth: 0" in yaml.safe_dump(job)
    assert "check_self_approval.py" in joined
