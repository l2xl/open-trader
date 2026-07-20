#!/usr/bin/env python3
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""One-shot Doorstop -> reqlib schema converter.

Parents are synthesized from `links:` where present (INFRA) and from level
containment within a document otherwise (PRODUCT); `order` is assigned from the
old sibling level ordering (10, 20, ...). Items with `references:` become
default-binding leaves (`tests: ~`); reviewed stamps are dropped — stamping is
user-only and happens via `req review` after the bound tests carry req tags.
Removes the per-document `.doorstop.yml` files. Run once, review, commit.
"""

import sys
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))

import reqlib
from reqlib import ROOT, REQ_DIR, Item


def level_tuple(data):
    return tuple(int(part) for part in str(data["level"]).split("."))


def sig_prefix(level):
    return level[:-1] if level and level[-1] == 0 else level


def containment_parents(document_items):
    """{uid: parent_uid} via the old per-document level-prefix stack."""
    parents = {}
    stack = []
    for uid, data in sorted(document_items.items(), key=lambda kv: level_tuple(kv[1])):
        sp = sig_prefix(level_tuple(data))
        while stack and not (len(stack[-1][0]) < len(sp) and sp[: len(stack[-1][0])] == stack[-1][0]):
            stack.pop()
        if stack:
            parents[uid] = stack[-1][1]
        stack.append((sp, uid))
    return parents


def main():
    documents = {}  # doc_dir -> {uid: old data}
    for path in sorted(REQ_DIR.rglob("*.yml")):
        if path.name == ".doorstop.yml":
            continue
        documents.setdefault(path.parent, {})[path.stem] = {"path": path, **yaml.safe_load(path.read_text(encoding="utf-8"))}

    converted = []
    for doc_dir, doc_items in documents.items():
        contained = containment_parents(doc_items)
        for uid, data in doc_items.items():
            links = [next(iter(link)) if isinstance(link, dict) else str(link) for link in data.get("links") or []]
            parents = links or ([contained[uid]] if uid in contained else [])
            item = Item(
                uid=uid,
                path=data["path"],
                header=str(data.get("header") or "").strip(),
                description=str(data.get("text") or "").strip() + "\n",
                parents=parents,
                tests={None: None} if data.get("references") else None,
            )
            item.level = level_tuple(data)
            converted.append(item)

    by_parent = {}
    for item in converted:
        for parent in item.parents or [None]:
            by_parent.setdefault(parent, []).append(item)
    for siblings in by_parent.values():
        for position, item in enumerate(sorted(siblings, key=lambda it: it.level), 1):
            item.order = max(item.order, position * 10)

    for item in converted:
        reqlib.write_item(item)
    for doc_dir in documents:
        doorstop_yml = doc_dir / ".doorstop.yml"
        if doorstop_yml.exists():
            doorstop_yml.unlink()
    print(f"converted {len(converted)} items in {len(documents)} folder(s); stamps dropped (re-stamp via 'req review')")
    return 0


if __name__ == "__main__":
    sys.exit(main())
