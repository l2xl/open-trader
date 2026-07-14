# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Tests for scripts/check_req_coverage.py -- [INFRA-032]."""

import doorstop

from check_req_coverage import check_item, run


def test_covered_leaf_has_no_problems(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "infra", "INFRA")
    test_file = base / "test_thing.py"
    test_file.write_text("# [INFRA-001]\ndef test_x(): pass\n")
    make_item(
        base / "infra", "INFRA-001", "1.0", "leaf", normative=True, verify="test",
        references=[{"path": "test_thing.py", "type": "file", "keyword": "INFRA-001"}],
    )
    tree = doorstop.build(root=str(base))
    item = tree.find_item("INFRA-001")
    assert check_item(item, is_branch=False, root=base) == []


def test_uncovered_leaf_reports_pending_not_error(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "infra", "INFRA")
    make_item(base / "infra", "INFRA-001", "1.0", "leaf", normative=True)
    exit_code = run(base)
    assert exit_code == 0  # unreviewed -> pending, non-fatal


def test_reviewed_leaf_missing_reference_is_fatal(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "infra", "INFRA")
    make_item(base / "infra", "INFRA-001", "1.0", "leaf", normative=True)
    tree = doorstop.build(root=str(base))
    tree.find_item("INFRA-001").review()
    assert run(base) == 1


def test_branch_is_covered_by_children_without_a_test(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "infra", "INFRA")
    make_item(base / "infra", "INFRA-001", "1.0", "heading", normative=True)
    make_item(base / "infra", "INFRA-002", "1.1", "child", normative=True, links=["INFRA-001"])
    tree = doorstop.build(root=str(base))
    assert check_item(tree.find_item("INFRA-001"), is_branch=True, root=base) == []


def test_inspection_attribute_is_rejected(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "infra", "INFRA")
    make_item(base / "infra", "INFRA-001", "1.0", "waived", normative=True, verify="inspection")
    tree = doorstop.build(root=str(base))
    problems = check_item(tree.find_item("INFRA-001"), is_branch=False, root=base)
    assert any("inspection is not allowed" in p for p in problems)
