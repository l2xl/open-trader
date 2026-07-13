#!/usr/bin/env python3
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Requirements gate: frozen (reviewed) requirements' referenced test files are unchanged.

Doorstop stamps a SHA-256 per referenced file at 'doorstop review' (with the
item_sha_required extension) but never re-verifies stamped hashes afterwards.
This script closes that gap: it re-hashes every reviewed item's references and
fails when a frozen test file was modified without a user-approved re-review
('doorstop clear' + 'doorstop review').
"""

import hashlib
import sys
from pathlib import Path

import doorstop

ROOT = Path(__file__).resolve().parent.parent


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            digest.update(chunk)
    return digest.hexdigest()


def check_item(item, root=ROOT):
    problems = []
    checked = 0
    for ref in item.references or []:
        path = ref.get("path", "")
        stored = ref.get("sha")
        full = root / path
        checked += 1
        if stored is None:
            problems.append(f"reviewed but reference {path} has no stamped sha (re-run doorstop review)")
        elif not full.is_file():
            problems.append(f"frozen reference missing: {path}")
        elif sha256_of(full) != stored:
            problems.append(f"frozen test modified after review: {path} (requires user-approved 'doorstop clear' + re-review)")
    return problems, checked


def run(root):
    tree = doorstop.build(root=str(root))
    errors = []
    checked = 0
    for document in tree.documents:
        for item in document.items:
            if not item.active or not item.reviewed:
                continue
            problems, n = check_item(item, root)
            checked += n
            errors.extend(f"{item.uid}: {p}" for p in problems)
    if errors:
        print(f"frozen: {len(errors)} violation(s):", file=sys.stderr)
        for line in errors:
            print(f"  {line}", file=sys.stderr)
        return 1
    print(f"frozen: OK ({checked} stamped reference(s) verified)")
    return 0


def main():
    return run(ROOT)


if __name__ == "__main__":
    sys.exit(main())
