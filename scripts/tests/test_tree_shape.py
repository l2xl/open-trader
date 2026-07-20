# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Tests for reqlib.load_tree + validate_structure tree-shape semantics -- [INFRA-030]."""

import pytest
import yaml

import reqlib


def _errors(req_dir):
    items, load_errors = reqlib.load_tree(req_dir)
    return items, load_errors, reqlib.validate_structure(items)


def _matching(errors, needle):
    return [e for e in errors if needle in e]


def _seed_minimal(req_dir, make_item):
    """One root branch with a single valid test-bearing leaf child."""
    make_item(req_dir, "ROOT-1", "the product shall exist", header="root")
    make_item(req_dir, "LEAF-1", "the leaf shall pass", parents=["ROOT-1"], tests=None)


def test_any_yaml_is_an_item_uid_is_stem_folders_meaningless(req_tree):
    req_dir, make_item = req_tree
    make_item(req_dir, "ROOT-1", "root shall be here", header="root")
    make_item(req_dir / "deep" / "nested", "LEAF-1", "the leaf shall pass", parents=["ROOT-1"], tests=None)
    items, load_errors, errors = _errors(req_dir)
    assert load_errors == []
    assert set(items) == {"ROOT-1", "LEAF-1"}
    assert items["LEAF-1"].uid == "LEAF-1"
    assert errors == []


def test_duplicate_stem_across_folders_is_rejected(req_tree):
    req_dir, make_item = req_tree
    make_item(req_dir / "a", "ROOT-1", "root shall be one", header="root")
    make_item(req_dir / "b", "ROOT-1", "root shall be two", header="dup")
    _items, load_errors, _errs = _errors(req_dir)
    assert _matching(load_errors, "duplicate UID")


def test_unknown_field_is_rejected(req_tree):
    req_dir, make_item = req_tree
    _seed_minimal(req_dir, make_item)
    (req_dir / "LEAF-1.yml").write_text(
        yaml.safe_dump({"header": "x", "description": "the leaf shall pass", "parents": ["ROOT-1"], "tests": None, "bogus": 1}),
        encoding="utf-8",
    )
    _items, load_errors, _errs = _errors(req_dir)
    assert _matching(load_errors, "unknown field 'bogus'")


def test_unknown_parent_is_rejected(req_tree):
    req_dir, make_item = req_tree
    make_item(req_dir, "ROOT-1", "root shall exist", header="root")
    make_item(req_dir, "LEAF-1", "the leaf shall pass", parents=["MISSING-9"], tests=None)
    _items, _load_errors, errors = _errors(req_dir)
    assert _matching(errors, "unknown parent 'MISSING-9'")


def test_exactly_one_root_required(req_tree):
    req_dir, make_item = req_tree
    make_item(req_dir, "ROOT-1", "root a shall exist", header="a")
    make_item(req_dir, "ROOT-2", "root b shall exist", header="b")
    _items, _load_errors, errors = _errors(req_dir)
    assert _matching(errors, "expected exactly one root")


def test_multi_parent_dag_is_allowed(req_tree):
    req_dir, make_item = req_tree
    make_item(req_dir, "ROOT-1", "root shall exist", header="root")
    make_item(req_dir, "A-1", "branch a shall exist", parents=["ROOT-1"])
    make_item(req_dir, "B-1", "branch b shall exist", parents=["ROOT-1"])
    make_item(req_dir, "C-1", "the leaf shall pass", parents=["A-1", "B-1"], tests=None)
    _items, load_errors, errors = _errors(req_dir)
    assert load_errors == []
    assert errors == []


def test_cycle_is_rejected(req_tree):
    req_dir, make_item = req_tree
    make_item(req_dir, "ROOT-1", "root shall pass", header="root", tests=None)
    make_item(req_dir, "X-1", "branch x shall exist", parents=["Y-1"])
    make_item(req_dir, "Y-1", "branch y shall exist", parents=["X-1"])
    _items, _load_errors, errors = _errors(req_dir)
    assert _matching(errors, "cycle")


def test_leaf_and_branch_are_mutually_exclusive(req_tree):
    req_dir, make_item = req_tree
    make_item(req_dir, "ROOT-1", "root shall pass", header="root", tests=None)
    make_item(req_dir, "LEAF-1", "the leaf shall pass", parents=["ROOT-1"], tests=None)
    _items, _load_errors, errors = _errors(req_dir)
    assert _matching(errors, "mutually exclusive")


def test_childless_item_without_tests_needs_defer_token(req_tree):
    req_dir, make_item = req_tree
    make_item(req_dir, "ROOT-1", "root shall exist", header="root")
    make_item(req_dir, "STUB-1", "not planned yet", parents=["ROOT-1"])
    _items, _load_errors, errors = _errors(req_dir)
    assert _matching(errors, "(defer)")

    make_item(req_dir, "STUB-1", "not planned yet (defer)", parents=["ROOT-1"])
    _items, _load_errors, errors = _errors(req_dir)
    assert errors == []


def test_test_bearing_leaf_needs_exactly_one_shall(req_tree):
    req_dir, make_item = req_tree
    make_item(req_dir, "ROOT-1", "root shall exist", header="root")

    make_item(req_dir, "LEAF-1", "the leaf just passes", parents=["ROOT-1"], tests=None)
    _items, _load_errors, errors = _errors(req_dir)
    assert _matching(errors, "exactly one 'shall'")

    make_item(req_dir, "LEAF-1", "the leaf shall pass and it shall also flush", parents=["ROOT-1"], tests=None)
    _items, _load_errors, errors = _errors(req_dir)
    assert _matching(errors, "exactly one 'shall'")

    make_item(req_dir, "LEAF-1", "the leaf shall pass", parents=["ROOT-1"], tests=None)
    _items, _load_errors, errors = _errors(req_dir)
    assert errors == []


def test_reviewed_must_be_sha256_hex(req_tree):
    req_dir, make_item = req_tree
    _seed_minimal(req_dir, make_item)
    make_item(req_dir, "ROOT-1", "the product shall exist", header="root", reviewed="not-a-sha")
    _items, load_errors, _errs = _errors(req_dir)
    assert _matching(load_errors, "must be a sha256 hex stamp")


def test_reviewed_stamp_must_match_computed_content(req_tree):
    req_dir, make_item = req_tree
    _seed_minimal(req_dir, make_item)
    items, _load_errors = reqlib.load_tree(req_dir)
    good = reqlib.compute_stamp(items["ROOT-1"])

    make_item(req_dir, "ROOT-1", "the product shall exist", header="root", reviewed=good)
    _items, load_errors, errors = _errors(req_dir)
    assert load_errors == []
    assert _matching(errors, "reviewed") == []

    make_item(req_dir, "ROOT-1", "the product shall exist", header="root", reviewed="0" * 64)
    _items, _load_errors, errors = _errors(req_dir)
    assert _matching(errors, "reviewed stamp does not match")


@pytest.mark.req("INFRA-030")
def test_live_requirements_tree_validates():
    """The real req/ tree loads and satisfies validate_structure with no errors."""
    items, load_errors = reqlib.load_tree()
    assert load_errors == []
    assert reqlib.validate_structure(items) == []
