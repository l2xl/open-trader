# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Tag-only binding discovery and coverage join -- [INFRA-031] [INFRA-032]."""

import json

import pytest

import reqlib


SHA = "a" * 64


PY_SOURCE = '''\
import pytest


@pytest.mark.req("INFRA-100")
def test_default_binding():
    pass


@pytest.mark.req("INFRA-100", "smoke")
def test_named_binding():
    pass
'''

CPP_SOURCE = '''\
TEST_CASE("cpp default", "[INFRA-101]") { CHECK(1); }

TEST_CASE("cpp named", "[INFRA-101][edge]") { CHECK(1); }
'''


def _loc(name="t", path="test/x.cpp", line=1, span="body"):
    return reqlib.Location(path, line, name, span)


def test_bindings_from_tags_default_and_named():
    assert reqlib.bindings_from_tags(["INFRA-100"]) == [("INFRA-100", None)]
    assert reqlib.bindings_from_tags(["INFRA-100", "smoke"]) == [("INFRA-100", "smoke")]
    assert reqlib.bindings_from_tags(["smoke", "INFRA-100", "edge"]) == [("INFRA-100", "edge")]


def test_bindings_from_tags_name_must_immediately_follow_its_uid():
    """A UID followed by another UID takes the default binding; a name only
    binds when it sits directly after its UID."""
    assert reqlib.bindings_from_tags(["INFRA-100", "INFRA-101"]) == [("INFRA-100", None), ("INFRA-101", None)]
    assert reqlib.bindings_from_tags(["smoke", "INFRA-100"]) == [("INFRA-100", None)]
    assert reqlib.bindings_from_tags(["INFRA-100", "smoke", "extra"]) == [("INFRA-100", "smoke")]


@pytest.mark.req("INFRA-031")
def test_discover_bindings_finds_pytest_markers_and_cpp_test_cases(tmp_path, monkeypatch):
    """Routine locations are discovered purely from tags: pytest @req markers
    for the default and named binding, and Catch2 TEST_CASE tag pairs."""
    (tmp_path / "py").mkdir()
    (tmp_path / "cpp").mkdir()
    (tmp_path / "py" / "test_x.py").write_text(PY_SOURCE, encoding="utf-8")
    (tmp_path / "cpp" / "test_y.cpp").write_text(CPP_SOURCE, encoding="utf-8")
    monkeypatch.setattr(reqlib, "PY_TEST_DIRS", ("py",))
    monkeypatch.setattr(reqlib, "CPP_TEST_DIRS", ("cpp",))

    found = reqlib.discover_bindings(root=tmp_path)

    assert [l.name for l in found[("INFRA-100", None)]] == ["py/test_x.py::test_default_binding"]
    assert [l.name for l in found[("INFRA-100", "smoke")]] == ["py/test_x.py::test_named_binding"]
    assert len(found[("INFRA-101", None)]) == 1
    assert found[("INFRA-101", None)][0].path == "cpp/test_y.cpp"
    assert len(found[("INFRA-101", "edge")]) == 1


def test_reviewed_leaf_binding_missing_or_ambiguous_routine_is_a_gate_error(req_tree):
    """check_frozen fails a reviewed leaf whose binding resolves to zero or to
    more than one tagged routine."""
    req_dir, make_item = req_tree
    make_item(req_dir, "INFRA-100", "The system shall bind by tag.", tests=None, reviewed=SHA)
    items, load_errors = reqlib.load_tree(req_dir)
    assert not load_errors

    missing = reqlib.check_frozen(items, {})
    assert any("no test routine tagged [INFRA-100]" in e for e in missing)

    ambiguous = reqlib.check_frozen(items, {("INFRA-100", None): [_loc("a"), _loc("b")]})
    assert any("[INFRA-100] is ambiguous" in e for e in ambiguous)

    resolved = reqlib.check_frozen(items, {("INFRA-100", None): [_loc("a")]})
    assert resolved == []


def test_load_coverage_parses_jsonl_and_flags_malformed(tmp_path):
    report = tmp_path / "req_coverage.jsonl"
    report.write_text(
        json.dumps({"tags": ["INFRA-100"], "passed": True}) + "\n"
        + "\n"
        + json.dumps({"tags": ["INFRA-100", "smoke"], "passed": False}) + "\n"
        + "{ not json\n"
        + json.dumps({"passed": True}) + "\n",
        encoding="utf-8",
    )

    records, errors = reqlib.load_coverage([report])

    assert records[("INFRA-100", None)][0]["passed"] is True
    assert records[("INFRA-100", "smoke")][0]["passed"] is False
    assert len(errors) == 2
    assert all("malformed coverage record" in e for e in errors)

    _, missing = reqlib.load_coverage([tmp_path / "absent.jsonl"])
    assert any("coverage report missing" in e for e in missing)


@pytest.mark.req("INFRA-032")
def test_check_coverage_joins_records_to_reviewed_leaves_both_directions(req_tree):
    """A reviewed leaf binding with no executed record, a record whose UID tag
    is unknown, and a record naming an undeclared binding on a known leaf are
    all coverage-gate errors."""
    req_dir, make_item = req_tree
    make_item(req_dir, "INFRA-100", "The system shall be covered.", tests=None, reviewed=SHA)
    items, load_errors = reqlib.load_tree(req_dir)
    assert not load_errors

    unrun = reqlib.check_coverage(items, {})
    assert any("bound but not run" in e for e in unrun)

    record = [{"passed": True, "name": "t", "log": "", "tags": []}]
    unknown_uid = reqlib.check_coverage(items, {("INFRA-999", None): record})
    assert any("[INFRA-999]" in e and "no known requirement" in e for e in unknown_uid)

    undeclared_name = reqlib.check_coverage(items, {("INFRA-100", None): record, ("INFRA-100", "ghost"): record})
    assert any("undeclared binding [INFRA-100][ghost]" in e for e in undeclared_name)

    clean = reqlib.check_coverage(items, {("INFRA-100", None): record})
    assert clean == []
