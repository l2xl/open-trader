# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Tests for the Doorstop tree shape and references convention -- [INFRA-030] [INFRA-031]."""

from pathlib import Path

import pytest
import doorstop
from doorstop import settings
from doorstop.common import DoorstopError

REPO_ROOT = Path(__file__).resolve().parent.parent.parent


def test_real_tree_has_a_single_root_document_and_validates():
    """Mirrors ci/gate.sh's `doorstop -W -S --error-all` exactly."""
    tree = doorstop.build(root=str(REPO_ROOT))
    roots = [d for d in tree.documents if not d.parent]
    assert len(roots) == 1

    saved = (settings.CHECK_REVIEW_STATUS, settings.CHECK_SUSPECT_LINKS, settings.ERROR_ALL)
    settings.CHECK_REVIEW_STATUS, settings.CHECK_SUSPECT_LINKS, settings.ERROR_ALL = False, False, True
    try:
        assert tree.validate() is True
    finally:
        settings.CHECK_REVIEW_STATUS, settings.CHECK_SUSPECT_LINKS, settings.ERROR_ALL = saved


def test_seeded_missing_keyword_fails_reference_check(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "infra", "INFRA")
    test_file = base / "test_thing.py"
    test_file.write_text("def test_x(): pass\n")  # no [INFRA-001] tag
    make_item(
        base / "infra", "INFRA-001", "1.0", "leaf", normative=True, verify="test",
        references=[{"path": "test_thing.py", "type": "file", "keyword": "INFRA-001"}],
    )
    tree = doorstop.build(root=str(base))
    item = tree.find_item("INFRA-001")
    with pytest.raises(DoorstopError):
        item.find_references()


def test_present_keyword_passes_reference_check(tree_builder):
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
    item.find_references()  # must not raise
