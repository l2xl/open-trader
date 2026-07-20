#!/usr/bin/env python3
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Publish req_status.json as a native GitHub Check Run via the Checks API directly.

The requirement tree's genuinely nested, DAG-capable shape (branch -> feature
-> case, with multi-parent duplication) does not match the flat 2-level
structure most JUnit-consuming actions expect, and that mismatch is opaque
to debug without action-internal logs. This sidesteps the whole JUnit bridge:
render the same tree as GitHub-Flavored Markdown (collapsible <details> per
branch, a status ball emoji per node, and per bound leaf a collapsible test
log per test case) and POST it straight to the Checks API, which is documented
to render arbitrary Markdown in a Check Run's output.summary regardless of
what produced it.
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

BALL = {
    "test_passed": "\U0001F7E2",
    "test_failed": "\U0001F534",
    "partially_implemented": "\U0001F7E1",
    "not_implemented": "⚪",
}

MAX_SUMMARY_BYTES = 60000
MAX_LOG_LINES = 60


def _title(entry):
    text = entry.get("description", entry.get("text", ""))
    return entry["header"] or (text[:70] + ("..." if len(text) > 70 else ""))


def _roots(report):
    all_children = set()
    for entry in report.values():
        all_children.update(entry["children"])
    return sorted(uid for uid in report if uid not in all_children)


# Nested <details> blocks get no default left padding in rendered Markdown, so
# depth is made visible with literal non-breaking spaces (plain ASCII spaces
# collapse outside <pre>) prefixed onto each node's own label.
INDENT_UNIT = " " * 4


def _indent(depth):
    return INDENT_UNIT * depth


def _bullet(status):
    return f"<small>{BALL[status]}</small>"


def _render_test(test, depth):
    ball = _bullet("test_passed" if test["passed"] else "test_failed")
    label = f"{_indent(depth)}{ball} <code>{test['name']}</code>"
    lines = test["log"].strip().splitlines()
    if len(lines) > MAX_LOG_LINES:
        lines = ["...(truncated)"] + lines[-MAX_LOG_LINES:]
    log = "\n".join(lines).replace("```", "` ` `")
    if not log:
        return f"- {label}\n"
    return f"<details><summary>{label}</summary>\n\n```\n{log}\n```\n\n</details>\n"


def _render_node(uid, report, path, depth=0):
    entry = report[uid]
    ball = _bullet(entry["status"])
    label = f"{_indent(depth)}{ball} **{uid}** {_title(entry)}"
    kids = entry["children"]
    tests = entry.get("tests") or []
    if not kids:
        if not tests:
            return f"- {label}\n"
        body = "".join(_render_test(t, depth + 1) for t in tests)
        return f"<details><summary>{label}</summary>\n\n{body}\n</details>\n"
    if uid in path:
        return f"- {label} _(cycle)_\n"
    body = "".join(_render_node(child, report, path | {uid}, depth + 1) for child in kids)
    return f"<details><summary>{label}</summary>\n\n{body}\n</details>\n"


def render_summary(report):
    counts = {}
    for entry in report.values():
        counts[entry["status"]] = counts.get(entry["status"], 0) + 1

    lines = ["| Status | Count |", "|---|---|"]
    for status in ("test_passed", "test_failed", "partially_implemented", "not_implemented"):
        if status in counts:
            lines.append(f"| {_bullet(status)} {status} | {counts[status]} |")
    lines.append("")
    for uid in _roots(report):
        lines.append(_render_node(uid, report, frozenset()))

    summary = "\n".join(lines)
    if len(summary.encode("utf-8")) > MAX_SUMMARY_BYTES:
        summary = summary.encode("utf-8")[:MAX_SUMMARY_BYTES].decode("utf-8", "ignore") + "\n\n...(truncated)"
    return summary


def build_check_run_body(report):
    counts = {}
    for entry in report.values():
        counts[entry["status"]] = counts.get(entry["status"], 0) + 1
    conclusion = "failure" if counts.get("test_failed") else "success"
    title = " / ".join(f"{counts[s]} {s}" for s in ("test_passed", "test_failed", "partially_implemented", "not_implemented") if s in counts)
    return {
        "status": "completed",
        "conclusion": conclusion,
        "output": {"title": title, "summary": render_summary(report)},
    }


def publish(repo, sha, token, body, name="Requirements Status"):
    payload = dict(body, name=name, head_sha=sha)
    req = urllib.request.Request(
        f"https://api.github.com/repos/{repo}/check-runs",
        data=json.dumps(payload).encode("utf-8"),
        method="POST",
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "Content-Type": "application/json",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    try:
        with urllib.request.urlopen(req) as resp:
            return json.loads(resp.read())
    except urllib.error.HTTPError as exc:
        raise RuntimeError(f"{exc.code} {exc.reason}: {exc.read().decode('utf-8', 'replace')}") from exc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--status", type=Path, default=ROOT / "req_status.json")
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY"))
    parser.add_argument("--sha", default=os.environ.get("GITHUB_SHA"))
    args = parser.parse_args()

    token = os.environ.get("GITHUB_TOKEN")
    if not (args.repo and args.sha and token):
        print("missing --repo/--sha/GITHUB_TOKEN", file=sys.stderr)
        return 1

    report = json.loads(args.status.read_text())
    body = build_check_run_body(report)
    try:
        result = publish(args.repo, args.sha, token, body)
    except RuntimeError as exc:
        print(f"FAILED to create check run: {exc}", file=sys.stderr)
        return 1
    print(f"created check run: {result['html_url']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
