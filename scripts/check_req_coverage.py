#!/usr/bin/env python3
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Requirements gate: every reviewed normative leaf carries a valid test binding.

A component-document leaf (CORE/DATA_MODEL/TRADER_HUD/APP/INFRA) passes when either:
  - its 'verify' attribute is 'inspection', or
  - it has >=1 'references' entry {type: file, path, keyword == its own UID}
    whose file exists and contains the literal Catch2 tag '[<UID>]'.

Violations are fatal only for reviewed (frozen) items: the TDD workflow binds
tests at review time, so unreviewed items are reported as pending counts only.
"""

import sys
from pathlib import Path

import doorstop

ROOT = Path(__file__).resolve().parent.parent


def check_item(item, root=ROOT):
    problems = []
    verify = item.attribute("verify")
    if verify == "inspection":
        return problems
    if verify not in (None, "", "test"):
        problems.append(f"unknown verify attribute '{verify}' (expected test|inspection)")
    refs = item.references or []
    if not refs:
        problems.append("no test reference and verify is not inspection")
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
            problems.append(f"referenced file {path} lacks Catch2 tag {tag}")
    return problems


def run(root):
    tree = doorstop.build(root=str(root))
    errors = []
    pending = 0
    for document in tree.documents:
        if str(document.prefix) == "PRODUCT":
            continue
        for item in document.items:
            if not item.active or not item.normative:
                continue
            problems = check_item(item, root)
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
