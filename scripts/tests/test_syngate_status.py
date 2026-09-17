# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Status rollup over the syngate DAG -- [INFRA-043] [INFRA-044]."""

import pytest

import syngatelib


def _rec(passed, name="case", log=""):
    return {"passed": passed, "name": name, "log": log, "tags": []}


def _build(syngate_tree, monkeypatch):
    syngate_dir, make = syngate_tree
    return syngate_dir, make


@pytest.mark.syngate("INFRA-043")
def test_leaf_status_reflects_coverage_records(syngate_tree, monkeypatch):
    """A leaf's status derives from its bindings' coverage records."""
    syngate_dir, make = _build(syngate_tree, monkeypatch)
    make(syngate_dir, "ROOT-1", "root branch", tests="absent")
    make(syngate_dir, "NONE-1", "shall run without records", parents=["ROOT-1"], tests=None)
    make(syngate_dir, "PASS-1", "shall pass", parents=["ROOT-1"], tests=None)
    make(syngate_dir, "FAIL-1", "shall fail", parents=["ROOT-1"], tests=None)
    make(syngate_dir, "PART-1", "shall partly bind", parents=["ROOT-1"], tests={"a": None, "b": None})
    make(syngate_dir, "STUB-1", "not planned yet", parents=["ROOT-1"])
    items, errs = syngatelib.load_tree(syngate_dir)
    assert errs == []

    records = {
        ("PASS-1", None): [_rec(True)],
        ("FAIL-1", None): [_rec(False)],
        ("PART-1", "a"): [_rec(True)],
    }

    assert syngatelib.leaf_status(items["NONE-1"], records) == syngatelib.NOT_IMPLEMENTED
    assert syngatelib.leaf_status(items["PASS-1"], records) == syngatelib.TEST_PASSED
    assert syngatelib.leaf_status(items["FAIL-1"], records) == syngatelib.TEST_FAILED
    assert syngatelib.leaf_status(items["PART-1"], records) == syngatelib.PARTIALLY_IMPLEMENTED

    report = syngatelib.compute_status(items, records)
    assert report["NONE-1"]["status"] == syngatelib.NOT_IMPLEMENTED
    assert report["PASS-1"]["status"] == syngatelib.TEST_PASSED
    assert report["FAIL-1"]["status"] == syngatelib.TEST_FAILED
    assert report["PART-1"]["status"] == syngatelib.PARTIALLY_IMPLEMENTED
    assert report["STUB-1"]["status"] == syngatelib.NOT_IMPLEMENTED


def test_leaf_status_all_bindings_must_be_covered_to_pass(syngate_tree, monkeypatch):
    syngate_dir, make = _build(syngate_tree, monkeypatch)
    make(syngate_dir, "ROOT-1", "root branch", tests="absent")
    make(syngate_dir, "MULTI-1", "shall bind twice", parents=["ROOT-1"], tests={"a": None, "b": None})
    items, errs = syngatelib.load_tree(syngate_dir)
    assert errs == []

    both = {("MULTI-1", "a"): [_rec(True)], ("MULTI-1", "b"): [_rec(True)]}
    assert syngatelib.leaf_status(items["MULTI-1"], both) == syngatelib.TEST_PASSED

    one_fails = {("MULTI-1", "a"): [_rec(True)], ("MULTI-1", "b"): [_rec(False)]}
    assert syngatelib.leaf_status(items["MULTI-1"], one_fails) == syngatelib.TEST_FAILED


@pytest.mark.syngate("INFRA-044")
def test_status_rolls_up_through_parents(syngate_tree, monkeypatch):
    """A branch aggregates its descendants' statuses recursively through the DAG."""
    syngate_dir, make = _build(syngate_tree, monkeypatch)
    make(syngate_dir, "ROOT-1", "root branch", tests="absent")

    make(syngate_dir, "BFAIL-1", "failed branch", parents=["ROOT-1"], tests="absent")
    make(syngate_dir, "BFAIL_A-1", "shall a", parents=["BFAIL-1"], tests=None)
    make(syngate_dir, "BFAIL_B-1", "shall b", parents=["BFAIL-1"], tests=None)

    make(syngate_dir, "BNI-1", "untested branch", parents=["ROOT-1"], tests="absent")
    make(syngate_dir, "BNI_A-1", "shall na", parents=["BNI-1"], tests=None)
    make(syngate_dir, "BNI_B-1", "shall nb", parents=["BNI-1"], tests=None)

    make(syngate_dir, "BPASS-1", "passing branch", parents=["ROOT-1"], tests="absent")
    make(syngate_dir, "BPASS_A-1", "shall pa", parents=["BPASS-1"], tests=None)
    make(syngate_dir, "BPASS_B-1", "shall pb", parents=["BPASS-1"], tests=None)

    make(syngate_dir, "BMIX-1", "mixed branch", parents=["ROOT-1"], tests="absent")
    make(syngate_dir, "BMIX_A-1", "shall ma", parents=["BMIX-1"], tests=None)
    make(syngate_dir, "BMIX_B-1", "shall mb", parents=["BMIX-1"], tests=None)

    items, errs = syngatelib.load_tree(syngate_dir)
    assert errs == []

    records = {
        ("BFAIL_A-1", None): [_rec(False)],
        ("BFAIL_B-1", None): [_rec(True)],
        ("BPASS_A-1", None): [_rec(True)],
        ("BPASS_B-1", None): [_rec(True)],
        ("BMIX_A-1", None): [_rec(True)],
    }
    report = syngatelib.compute_status(items, records)

    assert report["BFAIL-1"]["status"] == syngatelib.TEST_FAILED
    assert report["BNI-1"]["status"] == syngatelib.NOT_IMPLEMENTED
    assert report["BPASS-1"]["status"] == syngatelib.TEST_PASSED
    assert report["BMIX-1"]["status"] == syngatelib.PARTIALLY_IMPLEMENTED
    assert report["ROOT-1"]["status"] == syngatelib.TEST_FAILED


def test_aggregate_precedence():
    assert syngatelib.aggregate([]) == syngatelib.NOT_IMPLEMENTED
    assert syngatelib.aggregate([syngatelib.TEST_FAILED, syngatelib.TEST_PASSED, syngatelib.NOT_IMPLEMENTED]) == syngatelib.TEST_FAILED
    assert syngatelib.aggregate([syngatelib.NOT_IMPLEMENTED, syngatelib.NOT_IMPLEMENTED]) == syngatelib.NOT_IMPLEMENTED
    assert syngatelib.aggregate([syngatelib.TEST_PASSED, syngatelib.TEST_PASSED]) == syngatelib.TEST_PASSED
    assert syngatelib.aggregate([syngatelib.TEST_PASSED, syngatelib.NOT_IMPLEMENTED]) == syngatelib.PARTIALLY_IMPLEMENTED


def test_multi_parent_child_counted_under_both_parents(syngate_tree, monkeypatch):
    syngate_dir, make = _build(syngate_tree, monkeypatch)
    make(syngate_dir, "ROOT-1", "root branch", tests="absent")
    make(syngate_dir, "P1-1", "parent one", parents=["ROOT-1"], tests="absent")
    make(syngate_dir, "P2-1", "parent two", parents=["ROOT-1"], tests="absent")
    make(syngate_dir, "SHARED-1", "shall be shared", parents=["P1-1", "P2-1"], tests=None)
    items, errs = syngatelib.load_tree(syngate_dir)
    assert errs == []

    report = syngatelib.compute_status(items, {("SHARED-1", None): [_rec(True)]})

    assert report["P1-1"]["children"] == ["SHARED-1"]
    assert report["P2-1"]["children"] == ["SHARED-1"]
    assert report["P1-1"]["status"] == syngatelib.TEST_PASSED
    assert report["P2-1"]["status"] == syngatelib.TEST_PASSED
    assert sorted(report["SHARED-1"]["parents"]) == ["P1-1", "P2-1"]


def test_report_entry_exposes_presentation_fields(syngate_tree, monkeypatch):
    syngate_dir, make = _build(syngate_tree, monkeypatch)
    stamp = "a" * 64
    make(syngate_dir, "ROOT-1", "root branch", tests="absent")
    make(syngate_dir, "PARENT-1", "parent branch", parents=["ROOT-1"], header="Parent", tests="absent")
    make(syngate_dir, "CA-1", "shall a", parents=["PARENT-1"], order=20, tests=None)
    make(syngate_dir, "CB-1", "shall b", parents=["PARENT-1"], order=10, tests=None)
    make(syngate_dir, "CC-1", "shall c", parents=["PARENT-1"], order=10, tests=None)
    make(syngate_dir, "LEAF-1", "shall report cleanly  ", parents=["ROOT-1"], header="Leaf", tests=None, reviewed=stamp)
    make(syngate_dir, "STUB-1", "not planned yet", parents=["ROOT-1"])
    items, errs = syngatelib.load_tree(syngate_dir)
    assert errs == []

    report = syngatelib.compute_status(items, {("LEAF-1", None): [_rec(True, name="mytest", log="ok")]})

    parent = report["PARENT-1"]
    assert parent["header"] == "Parent"
    assert parent["parents"] == ["ROOT-1"]
    assert parent["children"] == ["CB-1", "CC-1", "CA-1"]
    assert parent["folder"] == "syngate"
    assert parent["reviewed"] is False
    assert parent["tests"] == []

    leaf = report["LEAF-1"]
    assert leaf["header"] == "Leaf"
    assert leaf["reviewed"] is True
    assert leaf["description"] == "shall report cleanly"
    assert leaf["tests"] == [{"binding": "", "name": "mytest", "passed": True, "log": "ok"}]

    assert report["STUB-1"]["status"] == syngatelib.NOT_IMPLEMENTED


@pytest.mark.syngate("INFRA-070")
def test_an_items_own_problem_reddens_that_item_and_only_its_parents(syngate_tree, monkeypatch):
    """A validation problem is a defect of the item it names: it reddens that
    item and rolls up through its own parents, leaving unrelated branches alone
    -- notably the branch whose tooling detected the problem."""
    syngate_dir, make = _build(syngate_tree, monkeypatch)
    make(syngate_dir, "ROOT-1", "root branch", tests="absent")
    make(syngate_dir, "DATA-1", "data branch", parents=["ROOT-1"], tests="absent")
    make(syngate_dir, "BAD-1", "shall carry a stale stamp", parents=["DATA-1"], tests=None)
    make(syngate_dir, "TOOL-1", "tooling branch", parents=["ROOT-1"], tests="absent")
    make(syngate_dir, "TOOL_A-1", "shall detect stale stamps", parents=["TOOL-1"], tests=None)
    items, errs = syngatelib.load_tree(syngate_dir)
    assert errs == []

    records = {("TOOL_A-1", None): [_rec(True)], ("BAD-1", None): [_rec(True)]}
    problems = {"BAD-1": ["BAD-1: reviewed stamp does not match item content"]}
    report = syngatelib.compute_status(items, records, problems)

    assert report["BAD-1"]["status"] == syngatelib.TEST_FAILED
    assert report["BAD-1"]["problems"] == problems["BAD-1"]
    assert report["DATA-1"]["status"] == syngatelib.TEST_FAILED  # rolls up its own branch
    assert report["TOOL_A-1"]["status"] == syngatelib.TEST_PASSED  # detecting it is success
    assert report["TOOL-1"]["status"] == syngatelib.TEST_PASSED
    assert report["TOOL-1"]["problems"] == []
    assert report["ROOT-1"]["status"] == syngatelib.TEST_FAILED  # the root still shows the tree is red


def test_coverage_gaps_are_not_item_problems(syngate_tree):
    """A missing coverage file must not redden every reviewed leaf: an unrun
    binding already rolls up as not_implemented."""
    syngate_dir, make_item = syngate_tree
    make_item(syngate_dir, "ROOT-1", "root shall exist", header="root")
    make_item(syngate_dir, "LEAF-1", "the leaf shall pass", parents=["ROOT-1"], tests=None)
    items, errs = syngatelib.load_tree(syngate_dir)
    assert errs == []
    assert syngatelib.item_problems(items) == {}
