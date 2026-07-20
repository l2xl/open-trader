# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Tests for scripts/render_req_report.py -- [INFRA-045]."""

import json

import pytest

from render_req_report import run


def _write_status(path, entries):
    path.write_text(json.dumps(entries))


def _status_entries():
    return {
        "ROOT-001": {
            "status": "test_passed", "header": "Root", "description": "Root",
            "parents": [], "children": ["INFRA-001"], "order": 0, "folder": "req",
            "reviewed": False, "deferred": False, "tests": [],
        },
        "INFRA-001": {
            "status": "test_passed", "header": "", "description": "Leaf",
            "parents": ["ROOT-001"], "children": [], "order": 0, "folder": "req/infra",
            "reviewed": False, "deferred": False, "tests": [],
        },
    }


@pytest.mark.req("INFRA-045")
def test_render_produces_index_and_navigable_cards(tmp_path):
    status_path = tmp_path / "req_status.json"
    _write_status(status_path, _status_entries())
    out_dir = tmp_path / "site"
    run(status_path, out_dir)

    index_html = (out_dir / "index.html").read_text()
    assert "ROOT-001" in index_html and "INFRA-001" in index_html
    assert 'href="items/INFRA-001.html"' in index_html

    root_card = (out_dir / "items" / "ROOT-001.html").read_text()
    assert 'href="INFRA-001.html"' in root_card  # child navigable from the parent card

    leaf_card = (out_dir / "items" / "INFRA-001.html").read_text()
    assert 'href="ROOT-001.html"' in leaf_card  # parent link navigable from the leaf card
    assert "status-green" in leaf_card


def test_item_card_shows_collapsible_test_logs(tmp_path):
    entries = _status_entries()
    entries["INFRA-001"]["tests"] = [
        {"binding": "", "name": "test/data/test_currency.cpp", "passed": True, "log": "All tests passed"},
        {"binding": "dao", "name": "test/datahub/test_dao.cpp", "passed": False, "log": "FAILED: REQUIRE( rows == 1 )"},
    ]
    status_path = tmp_path / "req_status.json"
    _write_status(status_path, entries)
    out_dir = tmp_path / "site"
    run(status_path, out_dir)

    leaf_card = (out_dir / "items" / "INFRA-001.html").read_text()
    assert "test/data/test_currency.cpp" in leaf_card
    assert "All tests passed" in leaf_card
    assert "FAILED: REQUIRE( rows == 1 )" in leaf_card
    assert leaf_card.count("<details") == 2


def test_item_card_shows_folder_reviewed_and_deferred(tmp_path):
    entries = _status_entries()
    entries["INFRA-001"]["folder"] = "req/infra"
    entries["INFRA-001"]["reviewed"] = True
    entries["INFRA-001"]["deferred"] = True
    status_path = tmp_path / "req_status.json"
    _write_status(status_path, entries)
    out_dir = tmp_path / "site"
    run(status_path, out_dir)

    leaf_card = (out_dir / "items" / "INFRA-001.html").read_text()
    assert "req/infra" in leaf_card
    assert "True" in leaf_card


def test_tree_page_renders_root_and_marks_dag_merge_duplicates(tmp_path):
    entries = _status_entries()
    entries["INFRA-002"] = {
        "status": "test_passed", "header": "", "description": "Shared leaf",
        "parents": ["ROOT-001", "INFRA-001"], "children": [], "order": 0, "folder": "req/infra",
        "reviewed": False, "deferred": False, "tests": [],
    }
    entries["ROOT-001"]["children"] = ["INFRA-001", "INFRA-002"]
    entries["INFRA-001"]["children"] = ["INFRA-002"]
    status_path = tmp_path / "req_status.json"
    _write_status(status_path, entries)
    out_dir = tmp_path / "site"
    run(status_path, out_dir)

    tree_html = (out_dir / "tree.html").read_text()
    assert tree_html.count('href="items/INFRA-002.html"') == 2  # rendered under both ROOT-001 and INFRA-001
    assert "also under another parent" in tree_html
