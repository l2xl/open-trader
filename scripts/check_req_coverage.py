#!/usr/bin/env python3
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Requirements gate: every reviewed normative leaf is bound to an automated test.

There are exactly two ways a requirement is satisfied, with no exemptions:
  - a leaf (an item with no children) is covered iff it carries >=1 'references'
    entry {type: file, path, keyword == its own UID} whose file exists and
    contains the literal tag '[<UID>]';
  - a branch (an item with children) is covered by its children and carries no
    test binding of its own.

There is deliberately no `verify: inspection` escape hatch: a leaf that cannot
be covered by an automated test is not a valid requirement. Encountering
`verify: inspection` anywhere is therefore a hard violation.

Violations are fatal only for reviewed (frozen) items: the TDD workflow binds
tests at review time, so unreviewed items are reported as pending counts only.
"""

import sys
from pathlib import Path

import doorstop

ROOT = Path(__file__).resolve().parent.parent


def branch_uids(tree):
    """UIDs that have at least one child linking up to them."""
    branches = set()
    for document in tree.documents:
        for item in document.items:
            for link in item.links:
                branches.add(str(link.value))
    return branches


def check_item(item, is_branch, root=ROOT):
    problems = []
    if item.attribute("verify") == "inspection":
        problems.append("verify: inspection is not allowed; leaves are covered by tests, branches by their children")
    if is_branch:
        return problems
    refs = item.references or []
    if not refs:
        problems.append("leaf has no test reference")
        return problems
    tag = f"[{item.uid}]"
    for ref in refs:
        path = ref.get("path", "")
        keyword = ref.get("keyword", "")
        full = root / path
        if keyword != str(item.uid):
            problems.append(f"reference keyword '{keyword}' does not match UID")
        if not full.is_file():
            problems.append(f"referenced file missing: {path}")
        elif tag not in full.read_text(encoding="utf-8", errors="replace"):
            problems.append(f"referenced file {path} lacks tag {tag}")
    return problems


def run(root):
    tree = doorstop.build(root=str(root))
    branches = branch_uids(tree)
    errors = []
    pending = 0
    for document in tree.documents:
        if str(document.prefix) == "PRODUCT":
            continue
        for item in document.items:
            if not item.active or not item.normative:
                continue
            problems = check_item(item, str(item.uid) in branches, root)
            if not problems:
                continue
            if item.reviewed:
                errors.extend(f"{item.uid}: {p}" for p in problems)
            else:
                pending += 1
    if pending:
        print(f"coverage: {pending} unreviewed leaf(s) without test binding (pending, non-fatal)")
    if errors:
        print(f"coverage: {len(errors)} error(s) on reviewed items:", file=sys.stderr)
        for line in errors:
            print(f"  {line}", file=sys.stderr)
        return 1
    print("coverage: OK")
    return 0


def main():
    return run(ROOT)


if __name__ == "__main__":
    sys.exit(main())
