#!/usr/bin/env python3
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Recursive status rollup over the Doorstop tree.

Four states: not_implemented, partially_implemented, test_passed, test_failed.
A leaf is test_passed/test_failed only if bound to a test result from one of
the JUnit reports (pytest --junitxml for the tooling suite, ctest
--output-junit for the C++ suite, where the CTest test name is the test's
source path); every other leaf (unbound, deferred, inspection) is
not_implemented. Each bound leaf also carries the per-test logs so reports can
show the evidence behind the status. A parent's status aggregates every child
reachable through same-document level containment (heading -> feature -> case)
or cross-document `links:` (child-document item -> parent-document item):
any failed child -> test_failed; all children not_implemented ->
not_implemented; all children test_passed -> test_passed; anything else
(partial progress) -> partially_implemented.
"""

import argparse
import json
import sys
from pathlib import Path

import doorstop
from junitparser import Error, Failure, JUnitXml, Skipped, TestSuite

ROOT = Path(__file__).resolve().parent.parent

MAX_LOG_CHARS = 4000


def _sig_prefix(level):
    return level[:-1] if level and level[-1] == 0 else level


def _level_tuple(item):
    return tuple(int(part) for part in str(item.level).split("."))


def build_children(tree):
    """Map uid -> set(child uid) via level containment (per document) and cross-document links."""
    children = {}

    def add_edge(parent_uid, child_uid):
        children.setdefault(parent_uid, set()).add(child_uid)
        children.setdefault(child_uid, set())

    for document in tree.documents:
        items = sorted(document.items, key=_level_tuple)
        stack = []  # (sig_prefix, uid)
        for item in items:
            uid = str(item.uid)
            sp = _sig_prefix(_level_tuple(item))
            while stack and not (len(stack[-1][0]) < len(sp) and sp[: len(stack[-1][0])] == stack[-1][0]):
                stack.pop()
            if stack:
                add_edge(stack[-1][1], uid)
            else:
                children.setdefault(uid, set())
            stack.append((sp, uid))

        for item in document.items:
            for link in item.links:
                add_edge(str(link), str(item.uid))

    return children


def _case_log(case):
    parts = []
    for result in case.result or []:
        text = (result.message or "").strip()
        if result.text and result.text.strip():
            text = f"{text}\n{result.text.strip()}" if text else result.text.strip()
        if text:
            parts.append(text)
    system_out = getattr(case, "system_out", None)
    if system_out and str(system_out).strip():
        parts.append(str(system_out).strip())
    log = "\n\n".join(parts)
    if len(log) > MAX_LOG_CHARS:
        log = "...(truncated)\n" + log[-MAX_LOG_CHARS:]
    return log


def load_test_results(junit_paths):
    """Map binding keys -> list of {name, passed, log} per test case.

    Keys are the JUnit classname (pytest: dotted module path) and the case
    name (ctest: the test's source path). A requirement reference binds by
    dotted module for .py files and by the repo-relative path otherwise.
    """
    results = {}
    for junit_path in junit_paths:
        path = Path(junit_path)
        if not path.is_file():
            print(f"warning: junit report missing, skipped: {path}", file=sys.stderr)
            continue
        xml = JUnitXml.fromfile(str(path))
        suites = [xml] if isinstance(xml, TestSuite) else list(xml)
        for suite in suites:
            for case in suite:
                outcomes = case.result or []
                if any(isinstance(r, Skipped) for r in outcomes):
                    continue
                failed = any(isinstance(r, (Failure, Error)) for r in outcomes)
                record = {"name": case.name, "passed": not failed, "log": _case_log(case)}
                for key in {case.classname, case.name} - {None, ""}:
                    results.setdefault(key, []).append(record)
    return results


def _binding_key(path):
    return path[:-3].replace("/", ".") if path.endswith(".py") else path


NOT_IMPLEMENTED = "not_implemented"
PARTIALLY_IMPLEMENTED = "partially_implemented"
TEST_PASSED = "test_passed"
TEST_FAILED = "test_failed"


def leaf_tests(item, results):
    """Test results bound to a leaf; empty for unbound, deferred, and inspection
    leaves -- there is no automated evidence to derive from."""
    refs = item.references or [] if item.normative else []
    tests = []
    for ref in refs:
        tests.extend(results.get(_binding_key(ref.get("path", "")), []))
    return tests


def leaf_status(tests):
    if not tests:
        return NOT_IMPLEMENTED
    return TEST_FAILED if any(not t["passed"] for t in tests) else TEST_PASSED


def aggregate(child_statuses):
    if not child_statuses:
        return NOT_IMPLEMENTED
    if TEST_FAILED in child_statuses:
        return TEST_FAILED
    if all(s == NOT_IMPLEMENTED for s in child_statuses):
        return NOT_IMPLEMENTED
    if all(s == TEST_PASSED for s in child_statuses):
        return TEST_PASSED
    return PARTIALLY_IMPLEMENTED


def compute_status(tree, results):
    children = build_children(tree)
    items_by_uid = {str(item.uid): item for document in tree.documents for item in document.items}
    tests_by_uid = {uid: leaf_tests(item, results) for uid, item in items_by_uid.items()}
    memo = {}

    def status_of(uid):
        if uid in memo:
            return memo[uid]
        memo[uid] = NOT_IMPLEMENTED  # cycle guard
        kids = children.get(uid, set())
        if kids:
            result = aggregate([status_of(k) for k in kids])
        elif uid in items_by_uid:
            result = leaf_status(tests_by_uid[uid])
        else:
            result = "n/a"
        memo[uid] = result
        return result

    report = {}
    for uid, item in items_by_uid.items():
        report[uid] = {
            "status": status_of(uid),
            "document": str(item.document.prefix),
            "level": str(item.level),
            "header": (item.header or "").strip(),
            "text": (item.text or "").strip(),
            "normative": item.normative,
            "verify": item.attribute("verify"),
            "links": [str(u) for u in item.links],
            "children": sorted(children.get(uid, set())),
            "reviewed": bool(item.reviewed),
            "tests": tests_by_uid[uid] if not children.get(uid) else [],
        }
    return report


def run(root, junit_paths, out_path):
    tree = doorstop.build(root=str(root))
    results = load_test_results(junit_paths or [])
    report = compute_status(tree, results)
    out_path.write_text(json.dumps(report, indent=2, sort_keys=True))
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--junit", type=Path, action="append", default=[], help="JUnit report(s) binding leaf status; repeatable")
    parser.add_argument("--out", type=Path, default=ROOT / "req_status.json")
    args = parser.parse_args()
    report = run(ROOT, args.junit, args.out)
    counts = {}
    for entry in report.values():
        counts[entry["status"]] = counts.get(entry["status"], 0) + 1
    print(f"wrote {args.out} -- {counts}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
