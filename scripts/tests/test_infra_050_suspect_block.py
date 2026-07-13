# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Unreviewed items and suspect links block the strict gate until user-approved re-approval -- [INFRA-050]."""

import re

import doorstop

from workflow_doc import REPO_ROOT


def _issues(base):
    tree = doorstop.build(root=str(base))
    return [str(issue) for document in tree.documents for issue in document.get_issues()]


def _linked_fixture(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "prd", "PRD")
    make_document(base, "infra", "INFRA", parent="PRD")
    make_item(base / "prd", "PRD-001", "1.0", "parent requirement", normative=True)
    make_item(base / "infra", "INFRA-050", "1.5.0", "leaf", normative=True, verify="test")
    doorstop.build(root=str(base)).find_item("INFRA-050").link("PRD-001")
    tree = doorstop.build(root=str(base))
    tree.find_item("PRD-001").review()
    tree.find_item("INFRA-050").review()
    assert _issues(base) == []  # also stamps the new link (doorstop stamps links on validation, not review)
    return base


def test_editing_a_reviewed_item_raises_unreviewed_changes_until_re_approval(tree_builder):
    base = _linked_fixture(tree_builder)
    doorstop.build(root=str(base)).find_item("INFRA-050").set("text", "changed after approval")
    assert "INFRA-050: unreviewed changes" in _issues(base)

    item = doorstop.build(root=str(base)).find_item("INFRA-050")
    item.clear()
    item.review()
    assert _issues(base) == []


def test_editing_a_linked_parent_marks_the_child_link_suspect(tree_builder):
    base = _linked_fixture(tree_builder)
    doorstop.build(root=str(base)).find_item("PRD-001").set("text", "parent changed")
    assert "INFRA-050: suspect link: PRD-001" in _issues(base)


def test_gate_strict_mode_enables_review_and_suspect_checks():
    gate = (REPO_ROOT / "ci" / "gate.sh").read_text()
    branch = re.search(r"GATE_STRICT.*?then\s*(.*?)\s*else\s*(.*?)\s*fi", gate, re.DOTALL)
    assert branch, "gate.sh lost its GATE_STRICT branch"
    strict_cmd, relaxed_cmd = branch.group(1), branch.group(2)
    assert "--error-all" in strict_cmd and "-W" not in strict_cmd and "-S" not in strict_cmd
    assert "-W" in relaxed_cmd and "-S" in relaxed_cmd
