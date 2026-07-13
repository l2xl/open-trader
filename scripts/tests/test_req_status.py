# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Tests for scripts/req_status.py -- [INFRA-043] [INFRA-044]."""

from pathlib import Path

import doorstop

from req_status import compute_status, load_test_results, run


JUNIT_ALL_PASS = """<?xml version="1.0"?>
<testsuites>
<testsuite name="s" tests="1">
<testcase classname="pkg.test_thing" name="test_ok"/>
</testsuite>
</testsuites>
"""

JUNIT_ONE_FAILS = """<?xml version="1.0"?>
<testsuites>
<testsuite name="s" tests="2">
<testcase classname="pkg.test_thing" name="test_ok"/>
<testcase classname="pkg.test_thing" name="test_bad"><failure message="boom">assert 1 == 2</failure></testcase>
</testsuite>
</testsuites>
"""

JUNIT_CTEST = """<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="proj" tests="2" failures="1">
<testcase name="test/data/test_currency.cpp" classname="proj" status="run" time="0.1">
<system-out>All tests passed (12 assertions in 3 test cases)</system-out>
</testcase>
<testcase name="test/datahub/test_dao.cpp" classname="proj" status="fail" time="0.2">
<failure message="Failed"/>
<system-out>FAILED: REQUIRE( rows == 1 )</system-out>
</testcase>
</testsuite>
"""


def _load(tmp_path, *xml_texts):
    paths = []
    for i, text in enumerate(xml_texts):
        p = tmp_path / f"junit{i}.xml"
        p.write_text(text)
        paths.append(p)
    return load_test_results(paths)


def _passing(key, name="test_ok"):
    return {key: [{"name": name, "passed": True, "log": ""}]}


def _failing(key, name="test_bad"):
    return {key: [{"name": name, "passed": False, "log": "boom"}]}


def test_load_test_results_all_pass(tmp_path):
    results = _load(tmp_path, JUNIT_ALL_PASS)
    assert [r["passed"] for r in results["pkg.test_thing"]] == [True]


def test_load_test_results_keeps_per_case_outcome_and_log(tmp_path):
    results = _load(tmp_path, JUNIT_ONE_FAILS)
    by_name = {r["name"]: r for r in results["pkg.test_thing"]}
    assert by_name["test_ok"]["passed"] is True
    assert by_name["test_bad"]["passed"] is False
    assert "boom" in by_name["test_bad"]["log"]
    assert "assert 1 == 2" in by_name["test_bad"]["log"]


def test_load_test_results_ctest_junit_binds_by_source_path_and_captures_output(tmp_path):
    results = _load(tmp_path, JUNIT_CTEST)
    passed = results["test/data/test_currency.cpp"]
    assert passed[0]["passed"] is True
    assert "All tests passed" in passed[0]["log"]
    failed = results["test/datahub/test_dao.cpp"]
    assert failed[0]["passed"] is False
    assert "REQUIRE( rows == 1 )" in failed[0]["log"]


def test_load_test_results_merges_reports_and_tolerates_missing_file(tmp_path, capsys):
    p = tmp_path / "junit0.xml"
    p.write_text(JUNIT_ALL_PASS)
    results = load_test_results([p, tmp_path / "absent.xml"])
    assert "pkg.test_thing" in results
    assert "absent.xml" in capsys.readouterr().err


def test_cross_document_link_rollup(tree_builder):
    """A leaf in a child document rolls its status up to the parent-document item it links to."""
    base, make_document, make_item = tree_builder
    make_document(base, "root", "ROOT")
    make_item(base / "root", "ROOT-001", "1.0", "root", normative=True)
    make_document(base, "infra", "INFRA", parent="ROOT")
    make_item(
        base / "infra", "INFRA-001", "1.0", "leaf",
        normative=True, links=["ROOT-001"], verify="test",
        references=[{"path": "pkg/test_thing.py", "type": "file", "keyword": "INFRA-001"}],
    )
    tree = doorstop.build(root=str(base))

    report = compute_status(tree, _passing("pkg.test_thing"))
    assert report["INFRA-001"]["status"] == "test_passed"
    assert report["ROOT-001"]["status"] == "test_passed"

    report_failed = compute_status(tree, _failing("pkg.test_thing"))
    assert report_failed["INFRA-001"]["status"] == "test_failed"
    assert report_failed["ROOT-001"]["status"] == "test_failed"


def test_leaf_entry_carries_bound_test_results(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "infra", "INFRA")
    make_item(
        base / "infra", "INFRA-001", "1.0", "leaf",
        normative=True, verify="test",
        references=[{"path": "test/data/test_currency.cpp", "type": "file", "keyword": "INFRA-001"}],
    )
    tree = doorstop.build(root=str(base))

    results = {"test/data/test_currency.cpp": [{"name": "test/data/test_currency.cpp", "passed": True, "log": "All tests passed"}]}
    report = compute_status(tree, results)
    assert report["INFRA-001"]["status"] == "test_passed"
    assert report["INFRA-001"]["tests"] == results["test/data/test_currency.cpp"]


def test_same_document_level_containment_rollup(tree_builder):
    """A branch heading's status aggregates its feature/case descendants by level containment alone."""
    base, make_document, make_item = tree_builder
    make_document(base, "infra", "INFRA")
    make_item(base / "infra", "INFRA-041", "1.0", "branch", normative=False)
    make_item(base / "infra", "INFRA-042", "1.1.0", "feature", normative=False)
    make_item(
        base / "infra", "INFRA-043", "1.1.1", "case",
        normative=True, verify="test",
        references=[{"path": "pkg/test_thing.py", "type": "file", "keyword": "INFRA-043"}],
    )
    tree = doorstop.build(root=str(base))

    report = compute_status(tree, _passing("pkg.test_thing"))
    assert report["INFRA-043"]["status"] == "test_passed"
    assert report["INFRA-042"]["status"] == "test_passed"
    assert report["INFRA-041"]["status"] == "test_passed"

    report_untested = compute_status(tree, {})
    assert report_untested["INFRA-043"]["status"] == "not_implemented"
    assert report_untested["INFRA-041"]["status"] == "not_implemented"


def test_partial_when_some_children_pass_and_some_not_implemented(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "infra", "INFRA")
    make_item(base / "infra", "INFRA-041", "1.0", "branch", normative=False)
    make_item(
        base / "infra", "INFRA-001", "1.1", "done",
        normative=True, verify="test",
        references=[{"path": "pkg/test_thing.py", "type": "file", "keyword": "INFRA-001"}],
    )
    make_item(base / "infra", "INFRA-002", "1.2", "not started (defer)", normative=True, verify="test")
    tree = doorstop.build(root=str(base))

    report = compute_status(tree, _passing("pkg.test_thing"))
    assert report["INFRA-001"]["status"] == "test_passed"
    assert report["INFRA-002"]["status"] == "not_implemented"
    assert report["INFRA-041"]["status"] == "partially_implemented"


def test_any_failed_child_fails_the_parent_even_with_deferred_siblings(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "infra", "INFRA")
    make_item(base / "infra", "INFRA-041", "1.0", "branch", normative=False)
    make_item(
        base / "infra", "INFRA-001", "1.1", "broken",
        normative=True, verify="test",
        references=[{"path": "pkg/test_thing.py", "type": "file", "keyword": "INFRA-001"}],
    )
    make_item(base / "infra", "INFRA-002", "1.2", "not started (defer)", normative=True, verify="test")
    tree = doorstop.build(root=str(base))

    report = compute_status(tree, _failing("pkg.test_thing"))
    assert report["INFRA-041"]["status"] == "test_failed"


def test_inspection_leaf_with_no_test_binding_is_not_implemented(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "infra", "INFRA")
    make_item(base / "infra", "INFRA-037", "1.5.2", "manual discipline", normative=True, verify="inspection")
    tree = doorstop.build(root=str(base))

    report = compute_status(tree, {})
    assert report["INFRA-037"]["status"] == "not_implemented"


def test_run_writes_status_json(tree_builder):
    base, make_document, make_item = tree_builder
    make_document(base, "infra", "INFRA")
    make_item(base / "infra", "INFRA-041", "1.0", "branch", normative=False)
    out = base / "req_status.json"
    report = run(base, [], out)
    assert out.exists()
    assert report["INFRA-041"]["status"] == "not_implemented"
