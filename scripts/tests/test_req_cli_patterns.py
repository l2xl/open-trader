# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""`req review` / `req clear` accept several UIDs and glob patterns
(`BUOY-00?`, `BUOY-*`), expanding patterns against the loaded tree."""

import argparse
from pathlib import Path

import pytest

import req
import reqlib


def _leaf(uid, reviewed=None):
    return reqlib.Item(uid=uid, path=Path(f"{uid}.yml"), header=uid, description="x shall y\n", parents=["PAT"], tests={None: None}, reviewed=reviewed)


def _tree():
    branch = reqlib.Item(uid="PAT", path=Path("PAT.yml"), header="Pattern", description="branch\n", parents=[])
    items = {"PAT": branch}
    for n in range(1, 4):
        uid = f"PAT-00{n}"
        items[uid] = _leaf(uid)
    return items


@pytest.mark.req("INFRA-051", "leaves_only")
def test_pattern_expands_to_matching_leaves_only():
    items = _tree()
    uids, errors = req._expand_uids(items, ["PAT-00?"], lambda item: item.is_leaf, "leaf")
    assert uids == ["PAT-001", "PAT-002", "PAT-003"]
    assert errors == []


@pytest.mark.req("INFRA-051", "branch_skip")
def test_star_pattern_skips_non_selectable_branch():
    items = _tree()
    uids, errors = req._expand_uids(items, ["PAT*"], lambda item: item.is_leaf, "leaf")
    assert uids == ["PAT-001", "PAT-002", "PAT-003"]
    assert errors == []


@pytest.mark.req("INFRA-051", "literal_passthrough")
def test_literals_pass_through_for_downstream_diagnostics():
    items = _tree()
    uids, errors = req._expand_uids(items, ["PAT", "NOPE-001"], lambda item: item.is_leaf, "leaf")
    assert uids == ["PAT", "NOPE-001"]
    assert errors == []


@pytest.mark.req("INFRA-051", "no_match_error")
def test_unmatched_pattern_is_an_error():
    items = _tree()
    uids, errors = req._expand_uids(items, ["ZZZ-*"], lambda item: item.is_leaf, "leaf")
    assert uids == []
    assert errors == ["pattern 'ZZZ-*' matches no leaf item"]


@pytest.mark.req("INFRA-051", "dedup")
def test_mixed_literals_and_patterns_deduplicate_preserving_order():
    items = _tree()
    uids, errors = req._expand_uids(items, ["PAT-002", "PAT-00?"], lambda item: item.is_leaf, "leaf")
    assert uids == ["PAT-002", "PAT-001", "PAT-003"]
    assert errors == []


@pytest.mark.req("INFRA-051", "clear_batch")
def test_clear_with_pattern_touches_only_reviewed_items(monkeypatch):
    items = _tree()
    items["PAT-001"] = _leaf("PAT-001", reviewed="a" * 64)
    items["PAT-003"] = _leaf("PAT-003", reviewed="b" * 64)
    written = []
    monkeypatch.setattr(reqlib, "load_tree", lambda: (items, []))
    monkeypatch.setattr(reqlib, "write_item", written.append)
    assert req.cmd_clear(argparse.Namespace(uid=["PAT-*"])) == 0
    assert [item.uid for item in written] == ["PAT-001", "PAT-003"]
    assert all(item.reviewed is None for item in written)


@pytest.mark.req("INFRA-051", "clear_unknown_literal")
def test_clear_with_unknown_literal_fails_after_processing_the_rest(monkeypatch):
    items = _tree()
    items["PAT-002"] = _leaf("PAT-002", reviewed="c" * 64)
    written = []
    monkeypatch.setattr(reqlib, "load_tree", lambda: (items, []))
    monkeypatch.setattr(reqlib, "write_item", written.append)
    assert req.cmd_clear(argparse.Namespace(uid=["NOPE-001", "PAT-002"])) == 1
    assert [item.uid for item in written] == ["PAT-002"]
