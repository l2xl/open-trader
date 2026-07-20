# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""User review freezes a leaf's bound routine span; re-approving a frozen item
is a separate commit from the edit it approves -- [INFRA-049]."""

import hashlib
import json
import subprocess
from pathlib import Path

import pytest

import reqlib
from check_self_approval import run as approval_run

SPAN = "@pytest.mark.req('INFRA-049')\ndef test_probe():\n    assert True\n"
DESC = "The freeze gate shall reject a stale approval.\n"


def _loc(span, path="scripts/tests/test_probe.py"):
    return reqlib.Location(path=path, line=1, name=f"{path}::test_probe", span=span)


def test_routine_sha_is_the_sha256_of_the_discovered_span():
    """routine_sha freezes exactly the raw span discovery captured for the tag."""
    loc = _loc(SPAN)
    assert reqlib.routine_sha(loc) == hashlib.sha256(SPAN.encode("utf-8")).hexdigest()


def test_compute_stamp_is_a_transparent_sha_over_canonical_json():
    """The reviewed stamp is a recomputable sha256 over the item's canonical payload."""
    item = reqlib.Item(
        uid="INFRA-049", path=Path("INFRA-049.yml"), header="Freeze",
        description=DESC, parents=["INFRA", "CORE"], tests={None: "a" * 64},
    )
    payload = {"description": DESC, "header": "Freeze", "parents": ["INFRA", "CORE"], "tests": {"": "a" * 64}}
    canonical = json.dumps(payload, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    assert reqlib.compute_stamp(item) == hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _reviewed_leaf(sha):
    item = reqlib.Item(
        uid="INFRA-049", path=Path("INFRA-049.yml"), header="Freeze",
        description=DESC, parents=["INFRA"], tests={None: sha},
    )
    item.reviewed = reqlib.compute_stamp(item)
    return {"INFRA-049": item}


def test_check_frozen_passes_while_the_stamped_span_is_intact():
    """A reviewed leaf whose bound routine still hashes to its sha stays frozen-clean."""
    loc = _loc(SPAN)
    items = _reviewed_leaf(reqlib.routine_sha(loc))
    assert reqlib.check_frozen(items, {("INFRA-049", None): [loc]}) == []


def test_check_frozen_fails_when_the_reviewed_span_changes():
    """Editing a frozen routine after review is a violation until the user re-reviews."""
    items = _reviewed_leaf(reqlib.routine_sha(_loc(SPAN)))
    edited = _loc(SPAN + "    # silently edited\n")
    errors = reqlib.check_frozen(items, {("INFRA-049", None): [edited]})
    assert len(errors) == 1
    assert "modified after review" in errors[0]


def test_check_frozen_flags_a_missing_or_ambiguous_binding():
    """A frozen tag that resolves to zero or many routines cannot be verified."""
    items = _reviewed_leaf(reqlib.routine_sha(_loc(SPAN)))
    assert any("no test routine tagged" in e for e in reqlib.check_frozen(items, {}))
    twin = {("INFRA-049", None): [_loc(SPAN), _loc(SPAN, path="test/probe.cpp")]}
    assert any("ambiguous" in e for e in reqlib.check_frozen(items, twin))


def _git(cwd, *args):
    subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, check=True)


def _commit(cwd, message):
    _git(cwd, "add", "-A")
    _git(cwd, "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-qm", message, "--no-verify")


def _put_item(req_dir, make_item, sha, description=DESC):
    probe = reqlib.Item(
        uid="INFRA-049", path=req_dir / "INFRA-049.yml", header="Freeze",
        description=description, parents=["INFRA"], tests={None: sha},
    )
    make_item(req_dir, "INFRA-049", description, parents=["INFRA"], header="Freeze", tests=sha, reviewed=reqlib.compute_stamp(probe))


def _frozen_repo(req_tree, span=SPAN):
    req_dir, make_item = req_tree
    repo = req_dir.parent
    test_dir = repo / "scripts" / "tests"
    test_dir.mkdir(parents=True, exist_ok=True)
    (test_dir / "test_probe.py").write_text(span)
    _put_item(req_dir, make_item, reqlib.routine_sha(_loc(span)))
    _git(repo, "init", "-q")
    _commit(repo, "freeze INFRA-049")
    return repo, req_dir, make_item


@pytest.mark.req("INFRA-049")
def test_edit_and_restamp_in_one_commit_touching_the_test_tree_is_rejected(req_tree):
    """Re-stamping a routine edited in the very same commit is self-approval."""
    repo, req_dir, make_item = _frozen_repo(req_tree)
    edited = SPAN + "    # edited\n"
    (repo / "scripts" / "tests" / "test_probe.py").write_text(edited)
    _put_item(req_dir, make_item, reqlib.routine_sha(_loc(edited)))
    _commit(repo, "self-approving edit")
    assert approval_run("HEAD^..HEAD", cwd=repo) == 1


def test_reapproval_with_a_substance_change_in_one_commit_is_rejected(req_tree):
    """Re-stamping while the item's own text changes in the same commit is self-approval."""
    repo, req_dir, make_item = _frozen_repo(req_tree)
    _put_item(req_dir, make_item, reqlib.routine_sha(_loc(SPAN)), description="The freeze gate shall reject a reworded approval.\n")
    _commit(repo, "reword and restamp together")
    assert approval_run("HEAD^..HEAD", cwd=repo) == 1


def test_edit_then_restamp_split_into_two_commits_passes(req_tree):
    """The disciplined flow: edit the routine, then a separate user-approved re-stamp."""
    repo, req_dir, make_item = _frozen_repo(req_tree)
    edited = SPAN + "    # edited\n"
    (repo / "scripts" / "tests" / "test_probe.py").write_text(edited)
    _commit(repo, "edit routine under review")
    _put_item(req_dir, make_item, reqlib.routine_sha(_loc(edited)))
    _commit(repo, "user-approved re-stamp")
    assert approval_run("HEAD~2..HEAD", cwd=repo) == 0
