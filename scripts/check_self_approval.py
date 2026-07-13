#!/usr/bin/env python3
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Requirements gate: re-approval of a frozen item must be a separate commit.

A commit is self-approving when it changes an item's approval (the reviewed
stamp or a stamped reference sha) and in the same commit changes the item's
substance (text or reference paths) or the content of a referenced test file.
The correct flow is two commits: the edit (item goes suspect), then the
user-approved 'doorstop clear' + 'doorstop review'.
"""

import subprocess
import sys

import yaml

REQ_GLOB = "req/"


def git(args, cwd):
    return subprocess.run(["git", *args], cwd=cwd, capture_output=True, text=True, check=False)


def show_yaml(commit, path, cwd):
    result = git(["show", f"{commit}:{path}"], cwd)
    if result.returncode != 0:
        return None
    return yaml.safe_load(result.stdout)


def approval_of(item):
    references = item.get("references") or []
    return item.get("reviewed") or None, [ref.get("sha") for ref in references if ref.get("sha")]


def substance_of(item):
    references = item.get("references") or []
    return item.get("text"), [ref.get("path") for ref in references]


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
        touched_tests = [ref.get("path") for ref in new.get("references") or [] if ref.get("path") in changed]
        for test_path in touched_tests:
            problems.append(f"{commit[:12]}: {path}: re-approval and change of referenced {test_path} in one commit")
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
