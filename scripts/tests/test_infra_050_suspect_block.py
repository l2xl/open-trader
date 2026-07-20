# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Unapproved changes block the gate -- [INFRA-050]."""

import argparse

import pytest

import reqlib


def _stamp_for(header, description, parents, tests):
    item = reqlib.Item(uid="X", path=reqlib.ROOT, header=header, description=description, parents=list(parents), tests=tests)
    return reqlib.compute_stamp(item)


@pytest.mark.req("INFRA-050")
def test_editing_a_reviewed_item_without_re_review_fails_validate_structure(req_tree):
    req_dir, make_item = req_tree
    sha = "a" * 64
    original = "The leaf shall do the thing.\n"
    stamp = _stamp_for("leaf", original, [], {None: sha})
    make_item(req_dir, "INFRA-050", "The leaf shall do a different thing entirely.\n", header="leaf", tests=sha, reviewed=stamp)

    items, errors = reqlib.load_tree(req_dir)
    assert not errors
    problems = reqlib.validate_structure(items)
    assert any("INFRA-050" in e and "reviewed stamp does not match item content" in e for e in problems)


def test_reviewed_item_left_untouched_passes_validate_structure(req_tree):
    req_dir, make_item = req_tree
    sha = "a" * 64
    original = "The leaf shall do the thing.\n"
    stamp = _stamp_for("leaf", original, [], {None: sha})
    make_item(req_dir, "INFRA-050", original, header="leaf", tests=sha, reviewed=stamp)

    items, errors = reqlib.load_tree(req_dir)
    assert not errors
    assert reqlib.validate_structure(items) == []


def test_strict_validate_reports_every_unreviewed_item(monkeypatch, capsys):
    import req as req_cli

    root = reqlib.Item(uid="ROOT-1", path=reqlib.ROOT, header="root", description="Root branch.\n", parents=[])
    child = reqlib.Item(
        uid="CHILD-1", path=reqlib.ROOT, header="leaf", description="The child shall do work.\n",
        parents=["ROOT-1"], tests={None: "b" * 64},
    )
    items = {"ROOT-1": root, "CHILD-1": child}

    monkeypatch.setattr(reqlib, "load_tree", lambda *a, **kw: (items, []))
    monkeypatch.setattr(reqlib, "discover_bindings", lambda *a, **kw: {})

    rc = req_cli.cmd_validate(argparse.Namespace(coverage=[], strict=True))
    captured = capsys.readouterr()

    assert rc == 1
    assert "ROOT-1: not reviewed (strict mode)" in captured.err
    assert "CHILD-1: not reviewed (strict mode)" in captured.err


def test_non_strict_validate_does_not_report_unreviewed_items(monkeypatch, capsys):
    import req as req_cli

    root = reqlib.Item(
        uid="ROOT-1", path=reqlib.ROOT, header="root", description="The root shall stand alone.\n",
        parents=[], tests={None: "c" * 64},
    )
    items = {"ROOT-1": root}

    monkeypatch.setattr(reqlib, "load_tree", lambda *a, **kw: (items, []))
    monkeypatch.setattr(reqlib, "discover_bindings", lambda *a, **kw: {})

    rc = req_cli.cmd_validate(argparse.Namespace(coverage=[], strict=False))
    captured = capsys.readouterr()

    assert rc == 0
    assert "not reviewed" not in captured.err
