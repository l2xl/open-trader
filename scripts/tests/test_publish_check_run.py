# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Tests for scripts/publish_check_run.py -- [INFRA-045]."""

from unittest.mock import patch, MagicMock

from publish_check_run import build_check_run_body, publish, render_summary


def _entry(status, header="Node", description="Node text", children=None, tests=None):
    return {
        "status": status, "header": header, "description": description,
        "children": children or [], "tests": tests or [],
    }


def test_render_summary_includes_counts_table_and_ball_per_status():
    report = {
        "ROOT": _entry("partially_implemented", children=["A", "B"]),
        "A": _entry("test_passed", header="passes"),
        "B": _entry("test_failed", header="fails"),
    }
    summary = render_summary(report)
    assert "test_passed | 1 |" in summary
    assert "test_failed | 1 |" in summary
    assert "\U0001F534" in summary  # red ball for the failed leaf
    assert "\U0001F7E2" in summary  # green ball for the passed leaf


def test_leaf_test_logs_render_as_collapsible_details():
    report = {
        "A": _entry("test_failed", header="leaf", tests=[
            {"binding": "", "name": "test/datahub/test_dao.cpp", "passed": False, "log": "FAILED: REQUIRE( rows == 1 )"},
            {"binding": "alt", "name": "test/data/test_currency.cpp", "passed": True, "log": "All tests passed"},
        ]),
    }
    summary = render_summary(report)
    assert "<details><summary><small>\U0001F534</small> **A** leaf</summary>" in summary
    assert "<code>test/datahub/test_dao.cpp</code>" in summary
    assert "FAILED: REQUIRE( rows == 1 )" in summary
    assert "All tests passed" in summary
    assert "```" in summary  # logs are fenced


def test_leaf_without_logs_stays_a_plain_list_item():
    report = {"A": _entry("test_passed", header="leaf")}
    summary = render_summary(report)
    assert "- <small>\U0001F7E2</small> **A** leaf" in summary
    assert "<details>" not in summary


def test_build_check_run_body_conclusion_reflects_any_failure():
    passing = {"A": _entry("test_passed")}
    failing = {"A": _entry("test_failed")}
    assert build_check_run_body(passing)["conclusion"] == "success"
    assert build_check_run_body(failing)["conclusion"] == "failure"


def test_summary_is_truncated_under_the_checks_api_byte_limit():
    huge_children = [f"LEAF-{i:04d}" for i in range(2000)]
    report = {"ROOT": _entry("test_passed", children=huge_children)}
    for uid in huge_children:
        report[uid] = _entry("test_passed", header=f"requirement number {uid} with some extra padding text")
    summary = render_summary(report)
    assert len(summary.encode("utf-8")) <= 60050


def test_publish_posts_expected_payload_and_returns_parsed_response():
    fake_response = MagicMock()
    fake_response.read.return_value = b'{"html_url": "https://github.com/x/y/runs/1"}'
    fake_response.__enter__.return_value = fake_response
    with patch("publish_check_run.urllib.request.urlopen", return_value=fake_response) as mock_open:
        result = publish("owner/repo", "deadbeef", "tok", {"status": "completed", "conclusion": "success", "output": {}})
    assert result["html_url"] == "https://github.com/x/y/runs/1"
    request = mock_open.call_args[0][0]
    assert request.full_url == "https://api.github.com/repos/owner/repo/check-runs"
    assert request.get_header("Authorization") == "Bearer tok"
