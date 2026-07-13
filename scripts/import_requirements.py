#!/usr/bin/env python3
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Decompose requirements_plan.md (sections 1-5) into the Doorstop tree under req/.

PRODUCT receives one heading item per branch and per feature plus one normative
item per case; component documents (CORE/DATA_MODEL/TRADER_HUD/APP/INFRA)
receive one leaf item per requirement, linked to its case's PRODUCT item. Leaf
attributes: accept, test, verify (test|inspection; the plan's 'demo' token is
imported as inspection), deferred (only when marked '(defer)').

Component documents use feature-local levels (M.K.i) so per-document level
validation stays contiguous; PRODUCT levels mirror the plan (N.M.K).

Refuses to touch non-empty documents unless --force, which removes previously
imported item files first. Run from anywhere; paths resolve from the repo root.
"""

import argparse
import re
import sys
from pathlib import Path

import doorstop
from doorstop.core import importer

ROOT = Path(__file__).resolve().parent.parent
PLAN = ROOT / "requirements_plan.md"
PREFIXES = ("CORE", "DATA_MODEL", "TRADER_HUD", "APP", "INFRA")

RE_BRANCH = re.compile(r"^## (\d)\. (.+?) \((CORE|DATA_MODEL|TRADER_HUD|APP|INFRA)\)\s*$")
RE_FEATURE = re.compile(r"^### (\d)\.(\d+)\s+(.+?)\s*$")
RE_CASE = re.compile(r"^#### (CORE|DATA_MODEL|TRADER_HUD|APP|INFRA): Case (\d+)\.(\d+)\s*[—–-]\s*(.+?)\s*$")
RE_LEAF = re.compile(r"^- \*\*\[([A-Z_]+)-(\d{3})\]\*\*\s*(.+)$")
RE_SECTION_END = re.compile(r"^## \d")


def split_leaf(body):
    def cut(marker, text):
        idx = text.find(marker)
        if idx < 0:
            return text, ""
        return text[:idx].rstrip(), text[idx + len(marker):].strip()

    text, rest = cut("*Accept:*", body)
    accept, rest = cut("*Test:*", rest)
    test, verify_part = cut("*Verify:*", rest)
    verify = "test"
    if verify_part:
        token = verify_part.split()[0].strip(".,;").lower()
        if token in ("inspection", "demo"):
            verify = "inspection"  # the plan's 'demo' collapses into inspection: both mean human-verified
    elif re.search(r"\bmanual\b|\bvisual\b", test, re.IGNORECASE) and not re.search(r"snapshot|snap-app", test, re.IGNORECASE):
        verify = "inspection"
    return text.rstrip(" ."), accept.rstrip(), test.rstrip(), verify


def parse_plan(lines):
    branches = []
    branch = feature = case = None
    current_leaf = None
    for line in lines:
        m = RE_BRANCH.match(line)
        if m:
            branch = {"section": int(m.group(1)), "name": m.group(2), "prefix": m.group(3), "intro": [], "features": []}
            branches.append(branch)
            feature = case = current_leaf = None
            continue
        if branch is None:
            continue
        if RE_SECTION_END.match(line) and not RE_BRANCH.match(line):
            branch = feature = case = current_leaf = None
            continue
        m = RE_FEATURE.match(line)
        if m:
            feature = {"num": int(m.group(2)), "name": m.group(3), "goal": [], "cases": []}
            branch["features"].append(feature)
            case = current_leaf = None
            continue
        m = RE_CASE.match(line)
        if m and feature is not None:
            case = {"m": int(m.group(2)), "k": int(m.group(3)), "name": m.group(4), "desc": [], "leaves": []}
            feature["cases"].append(case)
            current_leaf = None
            continue
        m = RE_LEAF.match(line)
        if m and case is not None:
            current_leaf = {"prefix": m.group(1), "num": int(m.group(2)), "body": m.group(3)}
            case["leaves"].append(current_leaf)
            continue
        stripped = line.strip()
        if current_leaf is not None and stripped and not stripped.startswith(("#", "- ")):
            current_leaf["body"] += " " + stripped
        elif case is not None and stripped and not stripped.startswith("#"):
            case["desc"].append(stripped)
        elif feature is not None and case is None and stripped and not stripped.startswith("#"):
            feature["goal"].append(stripped)
        elif feature is None and stripped and not stripped.startswith("#"):
            branch["intro"].append(stripped)
    return branches


def wipe(document):
    for item in list(document.items):
        Path(item.path).unlink()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--force", action="store_true", help="remove existing items before importing")
    parser.add_argument("--plan", default=str(PLAN))
    args = parser.parse_args()

    plan_path = Path(args.plan)
    if not plan_path.is_file():
        print(f"plan not found: {plan_path}", file=sys.stderr)
        return 1

    branches = parse_plan(plan_path.read_text(encoding="utf-8").splitlines())
    if len(branches) != 5:
        print(f"expected 5 branch sections, parsed {len(branches)}", file=sys.stderr)
        return 1

    tree = doorstop.build(root=str(ROOT))
    docs = {str(d.prefix): d for d in tree.documents}
    for prefix in ("PRODUCT", *PREFIXES):
        if prefix not in docs:
            print(f"missing Doorstop document: {prefix}", file=sys.stderr)
            return 1
        if next(iter(docs[prefix].items), None) is not None:
            if not args.force:
                print(f"document {prefix} is not empty; re-run with --force to reimport", file=sys.stderr)
                return 1
            wipe(docs[prefix])

    product = docs["PRODUCT"]
    product_counter = 0

    def product_add(level, text, normative):
        nonlocal product_counter
        product_counter += 1
        uid = f"PRODUCT-{product_counter:03d}"
        importer.add_item("PRODUCT", uid, attrs={"level": level, "text": text, "normative": normative}, document=product)
        return uid

    total_leaves = 0
    for branch in branches:
        n = branch["section"]
        intro = " ".join(branch["intro"])
        product_add(f"{n}.0", f"{branch['name']} ({branch['prefix']})\n\n{intro}".strip(), False)
        doc = docs[branch["prefix"]]
        for feature in branch["features"]:
            goal = " ".join(feature["goal"])
            product_add(f"{n}.{feature['num']}.0", f"{feature['name']}\n\n{goal}".strip(), False)
            for case in feature["cases"]:
                desc = " ".join(case["desc"])
                case_uid = product_add(f"{n}.{case['m']}.{case['k']}", f"{case['name']}\n\n{desc}".strip(), True)
                deferred_case = "(defer)" in case["name"]
                for i, leaf in enumerate(case["leaves"], start=1):
                    if leaf["prefix"] != branch["prefix"]:
                        print(f"leaf prefix mismatch in section {n}: {leaf['prefix']}-{leaf['num']:03d}", file=sys.stderr)
                        return 1
                    text, accept, test, verify = split_leaf(leaf["body"])
                    uid = f"{leaf['prefix']}-{leaf['num']:03d}"
                    attrs = {
                        "level": f"{case['m']}.{case['k']}.{i}",
                        "text": text,
                        "links": [case_uid],
                        "accept": accept,
                        "test": test,
                        "verify": verify,
                    }
                    if deferred_case or "(defer)" in text:
                        attrs["deferred"] = True
                    importer.add_item(branch["prefix"], uid, attrs=attrs, document=doc)
                    total_leaves += 1
        print(f"{branch['prefix']}: {sum(len(c['leaves']) for f in branch['features'] for c in f['cases'])} leaves, {sum(len(f['cases']) for f in branch['features'])} cases")

    print(f"imported: {product_counter} PRODUCT items, {total_leaves} component leaves")
    return 0


if __name__ == "__main__":
    sys.exit(main())
