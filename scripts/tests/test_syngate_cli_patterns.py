# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""`syngate review` / `syngate clear` accept several UIDs and glob patterns
(`BUOY-00?`, `BUOY-*`), expanding patterns against the loaded tree."""

import argparse
import sys
from pathlib import Path

import pytest

import syngate
import syngatelib


def _leaf(uid, reviewed=None):
    return syngatelib.Item(uid=uid, path=Path(f"{uid}.yml"), header=uid, description="x shall y\n", parents=["PAT"], tests={None: None}, reviewed=reviewed)


def _tree():
    branch = syngatelib.Item(uid="PAT", path=Path("PAT.yml"), header="Pattern", description="branch\n", parents=[])
    items = {"PAT": branch}
    for n in range(1, 4):
        uid = f"PAT-00{n}"
        items[uid] = _leaf(uid)
    return items


@pytest.mark.syngate("INFRA-051", "leaves_only")
def test_pattern_expands_to_matching_leaves_only():
    items = _tree()
    uids, errors = syngate._expand_uids(items, ["PAT-00?"], lambda item: item.is_leaf, "leaf")
    assert uids == ["PAT-001", "PAT-002", "PAT-003"]
    assert errors == []


@pytest.mark.syngate("INFRA-051", "branch_skip")
def test_star_pattern_skips_non_selectable_branch():
    items = _tree()
    uids, errors = syngate._expand_uids(items, ["PAT*"], lambda item: item.is_leaf, "leaf")
    assert uids == ["PAT-001", "PAT-002", "PAT-003"]
    assert errors == []


@pytest.mark.syngate("INFRA-051", "literal_passthrough")
def test_literals_pass_through_for_downstream_diagnostics():
    items = _tree()
    uids, errors = syngate._expand_uids(items, ["PAT", "NOPE-001"], lambda item: item.is_leaf, "leaf")
    assert uids == ["PAT", "NOPE-001"]
    assert errors == []


@pytest.mark.syngate("INFRA-051", "no_match_error")
def test_unmatched_pattern_is_an_error():
    items = _tree()
    uids, errors = syngate._expand_uids(items, ["ZZZ-*"], lambda item: item.is_leaf, "leaf")
    assert uids == []
    assert errors == ["pattern 'ZZZ-*' matches no leaf item"]


@pytest.mark.syngate("INFRA-051", "dedup")
def test_mixed_literals_and_patterns_deduplicate_preserving_order():
    items = _tree()
    uids, errors = syngate._expand_uids(items, ["PAT-002", "PAT-00?"], lambda item: item.is_leaf, "leaf")
    assert uids == ["PAT-002", "PAT-001", "PAT-003"]
    assert errors == []


@pytest.mark.syngate("INFRA-051", "clear_batch")
def test_clear_with_pattern_touches_only_reviewed_items(monkeypatch):
    items = _tree()
    items["PAT-001"] = _leaf("PAT-001", reviewed="a" * 64)
    items["PAT-003"] = _leaf("PAT-003", reviewed="b" * 64)
    written = []
    monkeypatch.setattr(syngatelib, "load_tree", lambda: (items, []))
    monkeypatch.setattr(syngatelib, "write_item", written.append)
    assert syngate.cmd_clear(argparse.Namespace(uid=["PAT-*"])) == 0
    assert [item.uid for item in written] == ["PAT-001", "PAT-003"]
    assert all(item.reviewed is None for item in written)


@pytest.mark.syngate("INFRA-051", "clear_unknown_literal")
def test_clear_with_unknown_literal_fails_after_processing_the_rest(monkeypatch):
    items = _tree()
    items["PAT-002"] = _leaf("PAT-002", reviewed="c" * 64)
    written = []
    monkeypatch.setattr(syngatelib, "load_tree", lambda: (items, []))
    monkeypatch.setattr(syngatelib, "write_item", written.append)
    assert syngate.cmd_clear(argparse.Namespace(uid=["NOPE-001", "PAT-002"])) == 1
    assert [item.uid for item in written] == ["PAT-002"]


@pytest.mark.syngate("INFRA-052", "selection")
def test_bound_routines_are_selected_by_node_id_and_by_tag_pairs(monkeypatch, tmp_path):
    items = _tree()
    items["PAT-003"].tests = {"a": None, "b": None}
    located = {
        ("PAT-001", None): [syngatelib.Location("scripts/tests/test_pat.py", 1, "scripts/tests/test_pat.py::test_one", "")],
        ("PAT-002", None): [syngatelib.Location("test/pat/test_pat.cpp", 1, "test/pat/test_pat.cpp:1", "")],
        ("PAT-003", "a"): [syngatelib.Location("test/pat/test_pat.cpp", 9, "test/pat/test_pat.cpp:9", "")],
        ("PAT-003", "b"): [syngatelib.Location("test/pat/test_pat.cpp", 17, "test/pat/test_pat.cpp:17", "")],
    }
    (tmp_path / "test_pat").touch()
    runs, written = [], []
    monkeypatch.setattr(syngatelib, "load_tree", lambda: (items, []))
    monkeypatch.setattr(syngatelib, "discover_bindings", lambda: located)
    monkeypatch.setattr(syngatelib, "write_item", written.append)
    monkeypatch.setattr(syngate.subprocess, "run", lambda argv, **kwargs: runs.append(argv) or argparse.Namespace(returncode=0))
    assert syngate.cmd_test(argparse.Namespace(uid=["PAT-00?"], build_dir=str(tmp_path), coverage_out=None)) == 0
    assert runs == [[sys.executable, "-m", "pytest", "scripts/tests/test_pat.py::test_one"], [str(tmp_path / "test_pat"), "[PAT-002],[PAT-003][a],[PAT-003][b]"]]
    assert written == []


@pytest.mark.syngate("INFRA-052", "unrun_record")
def test_a_binding_without_a_routine_is_recorded_as_failed(monkeypatch, tmp_path):
    items = _tree()
    coverage = tmp_path / "coverage.jsonl"
    monkeypatch.setattr(syngatelib, "load_tree", lambda: (items, []))
    monkeypatch.setattr(syngatelib, "discover_bindings", lambda: {})
    assert syngate.cmd_test(argparse.Namespace(uid=["PAT-001"], build_dir=str(tmp_path), coverage_out=str(coverage))) == 1
    records, errors = syngatelib.load_coverage([coverage])
    assert errors == [] and list(records) == [("PAT-001", None)]
    assert records[("PAT-001", None)][0]["passed"] is False
    assert "must match exactly one routine" in records[("PAT-001", None)][0]["log"]
