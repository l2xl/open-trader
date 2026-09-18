#!/usr/bin/env python3
# Open Trader
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Synergy Context Gate (syngate) CLI: new / test / review / clear / validate / report.

`review` and `clear` are user-only: the reviewed stamp is the record of the
user's approval. `test` runs the routines bound to items without stamping.
`validate` is the CI gate entry point; `report` computes the recursive status
rollup from coverage JSONL and renders the static HTML site.
"""

import argparse
import fnmatch
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import syngatelib
from syngatelib import ROOT, SYNGATE_DIR, Item


def _load_or_die():
    items, errors = syngatelib.load_tree()
    if errors:
        for line in errors:
            print(f"  {line}", file=sys.stderr)
        sys.exit(1)
    return items


def cmd_new(args):
    items, _ = syngatelib.load_tree()
    if args.uid in items:
        print(f"{args.uid}: already exists at {items[args.uid].path}", file=sys.stderr)
        return 1
    if not syngatelib.UID_RE.match(args.uid):
        print(f"{args.uid}: not a valid UID", file=sys.stderr)
        return 1
    for parent in args.parent:
        if parent not in items:
            print(f"unknown parent '{parent}'", file=sys.stderr)
            return 1
    directory = ROOT / args.dir if args.dir else SYNGATE_DIR
    directory.mkdir(parents=True, exist_ok=True)
    item = Item(uid=args.uid, path=directory / f"{args.uid}.yml", header="TODO", description="TODO: The component shall ...\n", parents=list(args.parent), order=args.order, tests={None: None})
    syngatelib.write_item(item)
    print(f"created {item.path.relative_to(ROOT)}")
    return 0


def _resolve(item, discovered):
    """({binding: Location}, {binding: why it cannot run}) -- a binding runs only when exactly one routine carries its tag pair."""
    resolved, unrun = {}, {}
    for name in item.tests:
        locations = discovered.get((item.uid, name), [])
        if len(locations) == 1:
            resolved[name] = locations[0]
        else:
            found = ", ".join(l.name for l in locations) or "none"
            unrun[name] = f"binding {syngatelib.binding_tag(item.uid, name)} must match exactly one routine, found: {found}"
            print(f"{item.uid}: {unrun[name]}", file=sys.stderr)
    return resolved, unrun


def _run_bound(resolved, build_dir):
    """Run the routines of {uid: {binding: Location}}: pytest routines are
    selected by node id in one process, Catch2 cases by an OR of their tag pairs
    in one process per binary. -> (all passed, {(uid, binding): why it did not run})."""
    node_ids, tags_by_binary, unrun = [], {}, {}
    for uid, bindings in resolved.items():
        for name, loc in bindings.items():
            if loc.path.endswith(".py"):
                node_ids.append(loc.name)
                continue
            binary = ROOT / build_dir / Path(loc.path).stem
            if binary.is_file():
                tags_by_binary.setdefault(binary, []).append(syngatelib.binding_tag(uid, name))
            else:
                unrun[(uid, name)] = f"test binary not built: {binary} (build target {binary.name} first)"
                print(f"{uid}: {unrun[(uid, name)]}", file=sys.stderr)
    passed = True
    if node_ids:
        print(f"running pytest: {' '.join(node_ids)}", flush=True)
        passed &= subprocess.run([sys.executable, "-m", "pytest", *node_ids], cwd=ROOT).returncode == 0
    for binary, tags in tags_by_binary.items():
        print(f"running {binary.name} \"{','.join(tags)}\"", flush=True)
        passed &= subprocess.run([str(binary), ",".join(tags)], cwd=ROOT).returncode == 0
    return passed, unrun


class _Recording:
    """The coverage emitters of the tests run inside write to a scratch file
    that is folded into `coverage_out` on exit. A binding the run could not
    execute is recorded as failed: a test that cannot be found is a red test."""

    def __init__(self, coverage_out):
        self.coverage_out = coverage_out

    def __enter__(self):
        if self.coverage_out:
            self.scratch = tempfile.TemporaryDirectory()
            self.fresh = Path(self.scratch.name) / "coverage.jsonl"
            os.environ["SYNGATE_COVERAGE_FILE"] = str(self.fresh)
        return self

    def unrun(self, uid, name, why):
        if self.coverage_out:
            with open(self.fresh, "a", encoding="utf-8") as f:
                f.write(json.dumps({"tags": [uid] + ([name] if name else []), "passed": False, "name": "", "log": why}) + "\n")

    def __exit__(self, *exc):
        if self.coverage_out:
            del os.environ["SYNGATE_COVERAGE_FILE"]
            syngatelib.merge_coverage(self.coverage_out, self.fresh)
            self.scratch.cleanup()


def _expand_uids(items, uid_args, selectable, kind):
    """Expand literal UIDs and glob patterns (fnmatch: * ? [seq]) against the tree.

    Literals pass through untouched so per-command diagnostics stay precise;
    patterns select only items satisfying `selectable`, and matching nothing is
    an error. Order follows the sorted tree; duplicates collapse.
    """
    selected, errors = [], []
    for arg in uid_args:
        if any(ch in arg for ch in "*?["):
            matched = [uid for uid in sorted(items) if fnmatch.fnmatchcase(uid, arg) and selectable(items[uid])]
            if not matched:
                errors.append(f"pattern '{arg}' matches no {kind} item")
            selected.extend(matched)
        else:
            selected.append(arg)
    seen = set()
    return [uid for uid in selected if not (uid in seen or seen.add(uid))], errors


def _review_one(items, structural, discovered, uid, build_dir, recording):
    own = [e for e in structural if e.startswith(f"{uid}:") and "reviewed stamp" not in e and "no stamped routine sha" not in e]
    if own:
        for line in own:
            print(f"  {line}", file=sys.stderr)
        return False
    item = items.get(uid)
    if item is None:
        print(f"unknown UID '{uid}'", file=sys.stderr)
        return False
    if not item.is_leaf:
        print(f"{uid}: branch items are reviewed through their children; nothing to stamp", file=sys.stderr)
        return False
    resolved, unresolved = _resolve(item, discovered)
    passed, unrun = _run_bound({uid: resolved}, build_dir) if not unresolved else (False, {})
    for name, why in [*unresolved.items(), *((name, why) for (_, name), why in unrun.items())]:
        recording.unrun(uid, name, why)
    if unresolved or unrun:
        print(f"{uid}: bound test could not be run; not stamping", file=sys.stderr)
        return False
    # Test-first TDD: the routine is frozen by hash as soon as it runs and resolves
    # unambiguously, whether it currently passes or fails. A stamped-but-failing leaf
    # rolls up as test_failed until the covering implementation lands and turns it green.
    item.tests = {name: syngatelib.routine_sha(loc) for name, loc in resolved.items()}
    item.reviewed = syngatelib.compute_stamp(item)
    syngatelib.write_item(item)
    state = "passing" if passed else "FAILING -- red, pending implementation"
    print(f"{uid}: reviewed ({item.reviewed}) -- bound test currently {state}")
    return True


def cmd_review(args):
    items = _load_or_die()
    uids, errors = _expand_uids(items, args.uid, lambda item: item.is_leaf, "leaf")
    if errors:
        for line in errors:
            print(line, file=sys.stderr)
        return 1
    structural = syngatelib.validate_structure(items)
    discovered = syngatelib.discover_bindings()
    with _Recording(args.coverage_out) as recording:
        failed = [uid for uid in uids if not _review_one(items, structural, discovered, uid, args.build_dir, recording)]
    if failed:
        print(f"review: {len(failed)}/{len(uids)} item(s) not stamped: {' '.join(failed)}", file=sys.stderr)
        return 1
    if len(uids) > 1:
        print(f"review: stamped {len(uids)} item(s)")
    return 0


def cmd_test(args):
    items = _load_or_die()
    uids, errors = _expand_uids(items, args.uid, lambda item: item.is_leaf, "leaf")
    discovered = syngatelib.discover_bindings()
    resolved, unresolved = {}, {}
    for uid in uids:
        item = items.get(uid)
        if item is None or not item.is_leaf:
            errors.append(f"unknown UID '{uid}'" if item is None else f"{uid}: a branch binds no tests of its own")
            continue
        resolved[uid], missing = _resolve(item, discovered)
        unresolved.update({(uid, name): why for name, why in missing.items()})
    with _Recording(args.coverage_out) as recording:
        passed, unrun = _run_bound(resolved, args.build_dir)
        unrun.update(unresolved)
        for (uid, name), why in unrun.items():
            recording.unrun(uid, name, why)
    for line in errors:
        print(line, file=sys.stderr)
    broken = {uid for uid, _ in unrun}
    print(f"test: {len(resolved) - len(broken)}/{len(uids)} item(s) run -- {'passed' if passed and not unrun else 'FAILED'}")
    return 0 if passed and not errors and not unrun else 1


def cmd_clear(args):
    items = _load_or_die()
    uids, errors = _expand_uids(items, args.uid, lambda item: bool(item.reviewed), "reviewed")
    if errors:
        for line in errors:
            print(line, file=sys.stderr)
        return 1
    ret = 0
    for uid in uids:
        item = items.get(uid)
        if item is None:
            print(f"unknown UID '{uid}'", file=sys.stderr)
            ret = 1
            continue
        if not item.reviewed:
            print(f"{uid}: not reviewed")
            continue
        item.reviewed = None
        if item.tests is not None:
            item.tests = {name: None for name in item.tests}
        syngatelib.write_item(item)
        print(f"{uid}: review stamp cleared")
    return ret


def cmd_validate(args):
    items, errors = syngatelib.load_tree()
    errors.extend(syngatelib.validate_structure(items))
    discovered = syngatelib.discover_bindings()
    errors.extend(syngatelib.check_frozen(items, discovered))
    if args.coverage:
        records, coverage_errors = syngatelib.load_coverage(args.coverage)
        errors.extend(coverage_errors)
        errors.extend(syngatelib.check_coverage(items, records))
    if args.strict:
        errors.extend(f"{uid}: not reviewed (strict mode)" for uid, item in sorted(items.items()) if not item.reviewed)
    pending = syngatelib.check_bindings_exist(items, discovered)
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
    records, coverage_errors = syngatelib.load_coverage(args.coverage)
    for line in coverage_errors:
        print(f"warning: {line}", file=sys.stderr)
    # Validation problems land on the items that own them, not on whichever
    # item's tooling found them.
    problems = syngatelib.item_problems(items, syngatelib.discover_bindings())
    report = syngatelib.compute_status(items, records, problems)
    out = Path(args.out)
    import json
    out.write_text(json.dumps(report, indent=2, sort_keys=True))
    counts = {}
    for entry in report.values():
        counts[entry["status"]] = counts.get(entry["status"], 0) + 1
    print(f"wrote {out} -- {counts}")
    if args.html:
        import render_syngate_report
        render_syngate_report.run(out, Path(args.html))
        print(f"rendered site to {args.html}")
    return 0


def cmd_ui(args):
    import syngate_ui
    return syngate_ui.serve(port=args.port, coverage=args.coverage, build_dir=args.build_dir, open_browser=not args.no_browser)


def main():
    parser = argparse.ArgumentParser(prog="syngate", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("new", help="scaffold a syngate item")
    p.add_argument("uid")
    p.add_argument("--parent", action="append", required=True)
    p.add_argument("--dir", help="folder under the repo root, e.g. syngate/infra")
    p.add_argument("--order", type=int, default=0)
    p.set_defaults(func=cmd_new)

    p = sub.add_parser("review", help="user-only: run bound tests, stamp routine shas + reviewed (stamps even on a failing test -- TDD red state)")
    p.add_argument("uid", nargs="+", help="UID(s) or glob pattern(s) like 'BUOY-00?' / 'BUOY-*' (quote patterns for the shell); patterns select leaves only")
    p.add_argument("--build-dir", default="cmake-build-debug-clang")
    p.add_argument("--coverage-out", help="coverage JSONL to fold the run's records into; a re-run binding supersedes its previous records")
    p.set_defaults(func=cmd_review)

    p = sub.add_parser("test", help="run the routines bound to leaf items, without stamping")
    p.add_argument("uid", nargs="+", help="UID(s) or glob pattern(s); patterns select leaves only")
    p.add_argument("--build-dir", default="cmake-build-debug-clang")
    p.add_argument("--coverage-out", help="coverage JSONL to fold the run's records into; a re-run binding supersedes its previous records")
    p.set_defaults(func=cmd_test)

    p = sub.add_parser("clear", help="user-only: drop the reviewed stamp")
    p.add_argument("uid", nargs="+", help="UID(s) or glob pattern(s); patterns select reviewed items only")
    p.set_defaults(func=cmd_clear)

    p = sub.add_parser("validate", help="structural + frozen + coverage checks (CI gate)")
    p.add_argument("--coverage", action="append", default=[], help="syngate_coverage.jsonl file(s); repeatable")
    p.add_argument("--strict", action="store_true", help="require every item reviewed")
    p.set_defaults(func=cmd_validate)

    p = sub.add_parser("report", help="recursive status rollup + optional HTML site")
    p.add_argument("--coverage", action="append", default=[], help="syngate_coverage.jsonl file(s); repeatable")
    p.add_argument("--out", default=str(ROOT / "syngate_status.json"))
    p.add_argument("--html", help="output directory for the static site")
    p.set_defaults(func=cmd_report)

    p = sub.add_parser("ui", help="serve the local tree editor in the browser (loopback + session token)")
    p.add_argument("--port", type=int, default=8712, help="listen port on 127.0.0.1 (default 8712, 0 = ephemeral)")
    p.add_argument("--coverage", action="append", default=[], help="syngate_coverage.jsonl file(s) to color statuses; repeatable (default: well-known local files)")
    p.add_argument("--build-dir", default="cmake-build-debug-clang", help="build tree with the Catch2 test binaries for review runs")
    p.add_argument("--no-browser", action="store_true", help="do not open the browser automatically")
    p.set_defaults(func=cmd_ui)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
