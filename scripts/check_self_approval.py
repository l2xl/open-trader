#!/usr/bin/env python3
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Requirements gate: re-approval of a frozen item must be a separate commit.

A commit is self-approving when it changes an item's approval (the reviewed
stamp or a stamped routine sha in `tests`) and in the same commit changes the
item's substance (description, parents, or binding names). The correct flow is
two commits: the edit (stamp goes stale), then the user-approved
'req clear' + 'req review'.

Binding locations are discovered from tags, not recorded in items, so the
exact frozen routine's file is unknown per commit; instead, an approval-changing
commit may not touch any file under the test trees at all — re-stamping a
routine edited in the same commit would otherwise pass the frozen check.
"""

import subprocess
import sys

import yaml

REQ_GLOB = "req/"
TEST_TREES = ("scripts/tests/", "test/")


def git(args, cwd):
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, check=False)


def show_yaml(commit, path, cwd):
    result = git(["show", f"{commit}:{path}"], cwd)
    if result.returncode != 0:
        return None
    return yaml.safe_load(result.stdout)


def _test_shas(item):
    tests = item.get("tests")
    if isinstance(tests, dict):
        return sorted(sha for sha in tests.values() if isinstance(sha, str))
    return [tests] if isinstance(tests, str) else []


def _binding_names(item):
    tests = item.get("tests", None)
    return sorted(tests) if isinstance(tests, dict) else []


def approval_of(item):
    return item.get("reviewed") or None, _test_shas(item)


def substance_of(item):
    return item.get("description") or item.get("text"), item.get("parents"), _binding_names(item)


def check_commit(commit, cwd):
    changed = git(["diff-tree", "--no-commit-id", "--name-only", "-r", commit], cwd).stdout.split()
    problems = []
    for path in changed:
        if not (path.startswith(REQ_GLOB) and path.endswith(".yml")):
            continue
        old = show_yaml(f"{commit}^", path, cwd)
        new = show_yaml(commit, path, cwd)
        if old is None or new is None or not isinstance(old, dict) or not isinstance(new, dict):
            continue
        stamp, shas = approval_of(new)
        if approval_of(old) == (stamp, shas) or (not stamp and not shas):
            continue
        if substance_of(old) != substance_of(new):
            problems.append(f"{commit[:12]}: {path}: re-approval and item change in one commit")
        for touched in changed:
            if touched.startswith(TEST_TREES):
                problems.append(f"{commit[:12]}: {path}: re-approval and test-tree change ({touched}) in one commit")
    return problems


def run(rev_range, cwd="."):
    commits = git(["rev-list", "--no-merges", rev_range], cwd)
    if commits.returncode != 0:
        print(f"self-approval: bad revision range '{rev_range}': {commits.stderr.strip()}", file=sys.stderr)
        return 2
    problems = []
    for commit in commits.stdout.split():
        problems.extend(check_commit(commit, cwd))
    if problems:
        print(f"self-approval: {len(problems)} violation(s):", file=sys.stderr)
        for line in problems:
            print(f"  {line}", file=sys.stderr)
        return 1
    print(f"self-approval: OK ({len(commits.stdout.split())} commit(s) checked)")
    return 0


def main():
    if len(sys.argv) != 2:
        print("usage: check_self_approval.py <rev-range>", file=sys.stderr)
        return 2
    return run(sys.argv[1])


if __name__ == "__main__":
    sys.exit(main())
