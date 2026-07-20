#!/usr/bin/env python3
# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Requirements CLI: new / review / clear / validate / report.

`review` and `clear` are user-only: the reviewed stamp is the record of the
user's approval. `validate` is the CI gate entry point; `report` computes the
recursive status rollup from coverage JSONL and renders the static HTML site.
"""

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import reqlib
from reqlib import ROOT, REQ_DIR, Item


def _load_or_die():
    items, errors = reqlib.load_tree()
    if errors:
        for line in errors:
            print(f"  {line}", file=sys.stderr)
        sys.exit(1)
    return items


def cmd_new(args):
    items, _ = reqlib.load_tree()
    if args.uid in items:
        print(f"{args.uid}: already exists at {items[args.uid].path}", file=sys.stderr)
        return 1
    if not reqlib.UID_RE.match(args.uid):
        print(f"{args.uid}: not a valid UID", file=sys.stderr)
        return 1
    for parent in args.parent:
        if parent not in items:
            print(f"unknown parent '{parent}'", file=sys.stderr)
            return 1
    directory = ROOT / args.dir if args.dir else REQ_DIR
    directory.mkdir(parents=True, exist_ok=True)
    item = Item(uid=args.uid, path=directory / f"{args.uid}.yml", header="TODO", description="TODO: The component shall ...\n", parents=list(args.parent), order=args.order, tests={None: None})
    reqlib.write_item(item)
    print(f"created {item.path.relative_to(ROOT)}")
    return 0


def _run_python(locations):
    node_ids = [loc.name for loc in locations]
    print(f"running pytest: {' '.join(node_ids)}")
    return subprocess.run([sys.executable, "-m", "pytest", *node_ids], cwd=ROOT).returncode == 0


def _run_cpp(locations, uid, name, build_dir):
    ok = True
    for loc in locations:
        binary = ROOT / build_dir / Path(loc.path).stem
        if not binary.is_file():
            print(f"test binary not built: {binary} (build target {Path(loc.path).stem} first)", file=sys.stderr)
            return False
        tag = reqlib.binding_tag(uid, name)
        print(f"running {binary.name} \"{tag}\"")
        ok &= subprocess.run([str(binary), tag], cwd=ROOT).returncode == 0
    return ok


def cmd_review(args):
    items = _load_or_die()
    structural = reqlib.validate_structure(items)
    own = [e for e in structural if e.startswith(f"{args.uid}:") and "reviewed stamp" not in e and "no stamped routine sha" not in e]
    if own:
        for line in own:
            print(f"  {line}", file=sys.stderr)
        return 1
    item = items.get(args.uid)
    if item is None:
        print(f"unknown UID '{args.uid}'", file=sys.stderr)
        return 1
    if not item.is_leaf:
        print(f"{args.uid}: branch items are reviewed through their children; nothing to stamp", file=sys.stderr)
        return 1
    discovered = reqlib.discover_bindings()
    resolved = {}
    for name in item.tests:
        tag = reqlib.binding_tag(args.uid, name)
        locations = discovered.get((args.uid, name), [])
        if len(locations) != 1:
            found = ", ".join(l.name for l in locations) or "none"
            print(f"{args.uid}: binding {tag} must match exactly one routine, found: {found}", file=sys.stderr)
            return 1
        resolved[name] = locations[0]
    python_locations = [loc for loc in resolved.values() if loc.path.endswith(".py")]
    cpp_locations = {name: loc for name, loc in resolved.items() if not loc.path.endswith(".py")}
    ok = True
    if python_locations:
        ok &= _run_python(python_locations)
    for name, loc in cpp_locations.items():
        ok &= _run_cpp([loc], args.uid, name, args.build_dir)
    if not ok:
        print(f"{args.uid}: bound tests failed; not stamping", file=sys.stderr)
        return 1
    item.tests = {name: reqlib.routine_sha(loc) for name, loc in resolved.items()}
    item.reviewed = reqlib.compute_stamp(item)
    reqlib.write_item(item)
    print(f"{args.uid}: reviewed ({item.reviewed})")
    return 0


def cmd_clear(args):
    items = _load_or_die()
    item = items.get(args.uid)
    if item is None:
        print(f"unknown UID '{args.uid}'", file=sys.stderr)
        return 1
    if not item.reviewed:
        print(f"{args.uid}: not reviewed")
        return 0
    item.reviewed = None
    if item.tests is not None:
        item.tests = {name: None for name in item.tests}
    reqlib.write_item(item)
    print(f"{args.uid}: review stamp cleared")
    return 0


def cmd_validate(args):
    items, errors = reqlib.load_tree()
    errors.extend(reqlib.validate_structure(items))
    discovered = reqlib.discover_bindings()
    errors.extend(reqlib.check_frozen(items, discovered))
    if args.coverage:
        records, coverage_errors = reqlib.load_coverage(args.coverage)
        errors.extend(coverage_errors)
        errors.extend(reqlib.check_coverage(items, records))
    if args.strict:
        errors.extend(f"{uid}: not reviewed (strict mode)" for uid, item in sorted(items.items()) if not item.reviewed)
    pending = reqlib.check_bindings_exist(items, discovered)
    if pending:
        print(f"validate: {len(pending)} unreviewed binding(s) without a tagged routine yet (pending, non-fatal)")
    if errors:
        print(f"validate: {len(errors)} error(s):", file=sys.stderr)
        for line in errors:
            print(f"  {line}", file=sys.stderr)
        return 1
    print(f"validate: OK ({len(items)} items)")
    return 0


def cmd_report(args):
    items = _load_or_die()
    records, coverage_errors = reqlib.load_coverage(args.coverage)
    for line in coverage_errors:
        print(f"warning: {line}", file=sys.stderr)
    report = reqlib.compute_status(items, records)
    out = Path(args.out)
    import json
    out.write_text(json.dumps(report, indent=2, sort_keys=True))
    counts = {}
    for entry in report.values():
        counts[entry["status"]] = counts.get(entry["status"], 0) + 1
    print(f"wrote {out} -- {counts}")
    if args.html:
        import render_req_report
        render_req_report.run(out, Path(args.html))
        print(f"rendered site to {args.html}")
    return 0


def main():
    parser = argparse.ArgumentParser(prog="req", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("new", help="scaffold a requirement item")
    p.add_argument("uid")
    p.add_argument("--parent", action="append", required=True)
    p.add_argument("--dir", help="folder under the repo root, e.g. req/infra")
    p.add_argument("--order", type=int, default=0)
    p.set_defaults(func=cmd_new)

    p = sub.add_parser("review", help="user-only: run bound tests, stamp routine shas + reviewed")
    p.add_argument("uid")
    p.add_argument("--build-dir", default="cmake-build-debug-clang")
    p.set_defaults(func=cmd_review)

    p = sub.add_parser("clear", help="user-only: drop the reviewed stamp")
    p.add_argument("uid")
    p.set_defaults(func=cmd_clear)

    p = sub.add_parser("validate", help="structural + frozen + coverage checks (CI gate)")
    p.add_argument("--coverage", action="append", default=[], help="req_coverage.jsonl file(s); repeatable")
    p.add_argument("--strict", action="store_true", help="require every item reviewed")
    p.set_defaults(func=cmd_validate)

    p = sub.add_parser("report", help="recursive status rollup + optional HTML site")
    p.add_argument("--coverage", action="append", default=[], help="req_coverage.jsonl file(s); repeatable")
    p.add_argument("--out", default=str(ROOT / "req_status.json"))
    p.add_argument("--html", help="output directory for the static site")
    p.set_defaults(func=cmd_report)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
