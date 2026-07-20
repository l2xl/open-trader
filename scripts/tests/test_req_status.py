# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Status rollup over the requirements DAG -- [INFRA-043] [INFRA-044]."""

import pytest

import reqlib


def _rec(passed, name="case", log=""):
    return {"passed": passed, "name": name, "log": log, "tags": []}


def _build(req_tree, monkeypatch):
    req_dir, make = req_tree
    return req_dir, make


@pytest.mark.req("INFRA-043")
def test_leaf_status_reflects_coverage_records(req_tree, monkeypatch):
    """A leaf's status derives from its bindings' coverage records."""
    req_dir, make = _build(req_tree, monkeypatch)
    make(req_dir, "ROOT-1", "root branch", tests="absent")
    make(req_dir, "NONE-1", "shall run without records", parents=["ROOT-1"], tests=None)
    make(req_dir, "PASS-1", "shall pass", parents=["ROOT-1"], tests=None)
    make(req_dir, "FAIL-1", "shall fail", parents=["ROOT-1"], tests=None)
    make(req_dir, "PART-1", "shall partly bind", parents=["ROOT-1"], tests={"a": None, "b": None})
    make(req_dir, "DEFER-1", "shall arrive later (defer)", parents=["ROOT-1"], tests="absent")
    items, errs = reqlib.load_tree(req_dir)
    assert errs == []

    records = {
        ("PASS-1", None): [_rec(True)],
        ("FAIL-1", None): [_rec(False)],
        ("PART-1", "a"): [_rec(True)],
    }

    assert reqlib.leaf_status(items["NONE-1"], records) == reqlib.NOT_IMPLEMENTED
    assert reqlib.leaf_status(items["PASS-1"], records) == reqlib.TEST_PASSED
    assert reqlib.leaf_status(items["FAIL-1"], records) == reqlib.TEST_FAILED
    assert reqlib.leaf_status(items["PART-1"], records) == reqlib.PARTIALLY_IMPLEMENTED
    assert reqlib.leaf_status(items["DEFER-1"], records) == reqlib.NOT_IMPLEMENTED

    report = reqlib.compute_status(items, records)
    assert report["NONE-1"]["status"] == reqlib.NOT_IMPLEMENTED
    assert report["PASS-1"]["status"] == reqlib.TEST_PASSED
    assert report["FAIL-1"]["status"] == reqlib.TEST_FAILED
    assert report["PART-1"]["status"] == reqlib.PARTIALLY_IMPLEMENTED
    assert report["DEFER-1"]["status"] == reqlib.NOT_IMPLEMENTED
    assert report["DEFER-1"]["deferred"] is True


def test_leaf_status_all_bindings_must_be_covered_to_pass(req_tree, monkeypatch):
    req_dir, make = _build(req_tree, monkeypatch)
    make(req_dir, "ROOT-1", "root branch", tests="absent")
    make(req_dir, "MULTI-1", "shall bind twice", parents=["ROOT-1"], tests={"a": None, "b": None})
    items, errs = reqlib.load_tree(req_dir)
    assert errs == []

    both = {("MULTI-1", "a"): [_rec(True)], ("MULTI-1", "b"): [_rec(True)]}
    assert reqlib.leaf_status(items["MULTI-1"], both) == reqlib.TEST_PASSED

    one_fails = {("MULTI-1", "a"): [_rec(True)], ("MULTI-1", "b"): [_rec(False)]}
    assert reqlib.leaf_status(items["MULTI-1"], one_fails) == reqlib.TEST_FAILED


@pytest.mark.req("INFRA-044")
def test_status_rolls_up_through_parents(req_tree, monkeypatch):
    """A branch aggregates its descendants' statuses recursively through the DAG."""
    req_dir, make = _build(req_tree, monkeypatch)
    make(req_dir, "ROOT-1", "root branch", tests="absent")

    make(req_dir, "BFAIL-1", "failed branch", parents=["ROOT-1"], tests="absent")
    make(req_dir, "BFAIL_A-1", "shall a", parents=["BFAIL-1"], tests=None)
    make(req_dir, "BFAIL_B-1", "shall b", parents=["BFAIL-1"], tests=None)

    make(req_dir, "BNI-1", "untested branch", parents=["ROOT-1"], tests="absent")
    make(req_dir, "BNI_A-1", "shall na", parents=["BNI-1"], tests=None)
    make(req_dir, "BNI_B-1", "shall nb", parents=["BNI-1"], tests=None)

    make(req_dir, "BPASS-1", "passing branch", parents=["ROOT-1"], tests="absent")
    make(req_dir, "BPASS_A-1", "shall pa", parents=["BPASS-1"], tests=None)
    make(req_dir, "BPASS_B-1", "shall pb", parents=["BPASS-1"], tests=None)

    make(req_dir, "BMIX-1", "mixed branch", parents=["ROOT-1"], tests="absent")
    make(req_dir, "BMIX_A-1", "shall ma", parents=["BMIX-1"], tests=None)
    make(req_dir, "BMIX_B-1", "shall mb", parents=["BMIX-1"], tests=None)

    items, errs = reqlib.load_tree(req_dir)
    assert errs == []

    records = {
        ("BFAIL_A-1", None): [_rec(False)],
        ("BFAIL_B-1", None): [_rec(True)],
        ("BPASS_A-1", None): [_rec(True)],
        ("BPASS_B-1", None): [_rec(True)],
        ("BMIX_A-1", None): [_rec(True)],
    }
    report = reqlib.compute_status(items, records)

    assert report["BFAIL-1"]["status"] == reqlib.TEST_FAILED
    assert report["BNI-1"]["status"] == reqlib.NOT_IMPLEMENTED
    assert report["BPASS-1"]["status"] == reqlib.TEST_PASSED
    assert report["BMIX-1"]["status"] == reqlib.PARTIALLY_IMPLEMENTED
    assert report["ROOT-1"]["status"] == reqlib.TEST_FAILED


def test_aggregate_precedence():
    assert reqlib.aggregate([]) == reqlib.NOT_IMPLEMENTED
    assert reqlib.aggregate([reqlib.TEST_FAILED, reqlib.TEST_PASSED, reqlib.NOT_IMPLEMENTED]) == reqlib.TEST_FAILED
    assert reqlib.aggregate([reqlib.NOT_IMPLEMENTED, reqlib.NOT_IMPLEMENTED]) == reqlib.NOT_IMPLEMENTED
    assert reqlib.aggregate([reqlib.TEST_PASSED, reqlib.TEST_PASSED]) == reqlib.TEST_PASSED
    assert reqlib.aggregate([reqlib.TEST_PASSED, reqlib.NOT_IMPLEMENTED]) == reqlib.PARTIALLY_IMPLEMENTED


def test_multi_parent_child_counted_under_both_parents(req_tree, monkeypatch):
    req_dir, make = _build(req_tree, monkeypatch)
    make(req_dir, "ROOT-1", "root branch", tests="absent")
    make(req_dir, "P1-1", "parent one", parents=["ROOT-1"], tests="absent")
    make(req_dir, "P2-1", "parent two", parents=["ROOT-1"], tests="absent")
    make(req_dir, "SHARED-1", "shall be shared", parents=["P1-1", "P2-1"], tests=None)
    items, errs = reqlib.load_tree(req_dir)
    assert errs == []

    report = reqlib.compute_status(items, {("SHARED-1", None): [_rec(True)]})

    assert report["P1-1"]["children"] == ["SHARED-1"]
    assert report["P2-1"]["children"] == ["SHARED-1"]
    assert report["P1-1"]["status"] == reqlib.TEST_PASSED
    assert report["P2-1"]["status"] == reqlib.TEST_PASSED
    assert sorted(report["SHARED-1"]["parents"]) == ["P1-1", "P2-1"]


def test_report_entry_exposes_presentation_fields(req_tree, monkeypatch):
    req_dir, make = _build(req_tree, monkeypatch)
    stamp = "a" * 64
    make(req_dir, "ROOT-1", "root branch", tests="absent")
    make(req_dir, "PARENT-1", "parent branch", parents=["ROOT-1"], header="Parent", tests="absent")
    make(req_dir, "CA-1", "shall a", parents=["PARENT-1"], order=20, tests=None)
    make(req_dir, "CB-1", "shall b", parents=["PARENT-1"], order=10, tests=None)
    make(req_dir, "CC-1", "shall c", parents=["PARENT-1"], order=10, tests=None)
    make(req_dir, "LEAF-1", "shall report cleanly  ", parents=["ROOT-1"], header="Leaf", tests=None, reviewed=stamp)
    make(req_dir, "DEF-1", "shall wait (defer)", parents=["ROOT-1"], tests="absent")
    items, errs = reqlib.load_tree(req_dir)
    assert errs == []

    report = reqlib.compute_status(items, {("LEAF-1", None): [_rec(True, name="mytest", log="ok")]})

    parent = report["PARENT-1"]
    assert parent["header"] == "Parent"
    assert parent["parents"] == ["ROOT-1"]
    assert parent["children"] == ["CB-1", "CC-1", "CA-1"]
    assert parent["folder"] == "req"
    assert parent["reviewed"] is False
    assert parent["tests"] == []

    leaf = report["LEAF-1"]
    assert leaf["header"] == "Leaf"
    assert leaf["reviewed"] is True
    assert leaf["deferred"] is False
    assert leaf["description"] == "shall report cleanly"
    assert leaf["tests"] == [{"binding": "", "name": "mytest", "passed": True, "log": "ok"}]

    assert report["DEF-1"]["deferred"] is True
