# XCockpit
# Copyright (c) 2026 l2xl (l2xl/at/proton.me)
# Distributed under the Intellectual Property Reserve License, v2 (IPRL)

"""Self-owned requirements toolkit core (no doorstop).

Tree layout: every `*.yml` under `req/` (any depth) is a requirement item; the
UID is the file stem; folders carry no semantics. Item schema:

    header: one-line title
    description: |
      prose with exactly one "shall" on test-bearing leaves; `(defer)` marks deferral
    parents: [UID, ...]        # DAG edges; exactly one item in the tree has none
    order: 10                  # optional presentation-only sibling sort key
    tests: ~ | <sha> | {name: sha|~, ...}   # leaf test bindings; absent on branches
    reviewed: <sha256 hex>     # user-only approval stamp, or absent

A binding is identified purely by tags: the default binding is the `[UID]` tag
alone, a named binding is the `[UID][name]` tag pair (binding name immediately
after the UID tag; requirement tags go last in the tag list). Test locations are
discovered from tags at check time, never declared in items.

The reviewed stamp is transparent: sha256 hex over the canonical JSON
    {"description": ..., "header": ..., "parents": [...], "tests": {name: sha} | null}
serialized with sort_keys=True, separators=(",", ":"), ensure_ascii=False,
UTF-8 encoded (default binding name is ""; unstamped shas are ""). Verify with:
    python3 -c 'import reqlib,sys; print(reqlib.compute_stamp(reqlib.load_tree()[0][sys.argv[1]]))' <UID>

Coverage joins `req_coverage.jsonl` records `{"tags": [...], "passed": bool}`
(optional "name"/"log") emitted by the pytest conftest hook and the Catch2
listener against the items' `tests` bindings.
"""

import ast
import hashlib
import json
import re
from dataclasses import dataclass, field
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
REQ_DIR = ROOT / "req"

UID_RE = re.compile(r"^[A-Z][A-Z_]*-[0-9]+$")
BINDING_NAME_RE = re.compile(r"^[a-z0-9_]+$")
SHA_RE = re.compile(r"^[0-9a-f]{64}$")
KNOWN_FIELDS = {"header", "description", "parents", "order", "tests", "reviewed"}

PY_TEST_DIRS = ("scripts/tests",)
CPP_TEST_DIRS = ("test",)

NOT_IMPLEMENTED = "not_implemented"
PARTIALLY_IMPLEMENTED = "partially_implemented"
TEST_PASSED = "test_passed"
TEST_FAILED = "test_failed"


@dataclass
class Item:
    uid: str
    path: Path
    header: str = ""
    description: str = ""
    parents: list = field(default_factory=list)
    order: float = 0
    tests: dict = None  # None = branch/deferred; {name|None: sha|None} otherwise
    reviewed: str = None
    folder: str = ""

    @property
    def is_leaf(self):
        return self.tests is not None

    @property
    def deferred(self):
        return "(defer)" in self.description


@dataclass
class Location:
    path: str  # repo-relative
    line: int  # 1-based first line of the routine
    name: str  # test routine / case identifier
    span: str  # raw source span hashed for the freeze


def _parse_tests(value, errors, uid):
    if isinstance(value, dict):
        tests = {}
        for name, sha in value.items():
            if not isinstance(name, str) or not BINDING_NAME_RE.match(name):
                errors.append(f"{uid}: invalid binding name {name!r}")
                continue
            tests[name] = _parse_sha(sha, errors, uid)
        return tests
    return {None: _parse_sha(value, errors, uid)}


def _parse_sha(sha, errors, uid):
    if sha is None:
        return None
    if not isinstance(sha, str) or not SHA_RE.match(sha):
        errors.append(f"{uid}: invalid routine sha {sha!r}")
        return None
    return sha


def load_tree(req_dir=REQ_DIR):
    """Return ({uid: Item}, [errors]); items with unusable YAML are skipped."""
    items, errors = {}, []
    for path in sorted(req_dir.rglob("*.yml")):
        uid = path.stem
        if uid in items:
            errors.append(f"{uid}: duplicate UID ({path.relative_to(req_dir)} and {items[uid].path.relative_to(req_dir)})")
            continue
        try:
            data = yaml.safe_load(path.read_text(encoding="utf-8"))
        except yaml.YAMLError as exc:
            errors.append(f"{uid}: unparsable YAML: {exc}")
            continue
        if not isinstance(data, dict):
            errors.append(f"{uid}: item file is not a mapping")
            continue
        item = Item(uid=uid, path=path, folder=str(path.parent.relative_to(req_dir.parent)))
        for key in sorted(set(data) - KNOWN_FIELDS):
            errors.append(f"{uid}: unknown field '{key}'")
        item.header = str(data.get("header") or "").strip()
        item.description = str(data.get("description") or "")
        parents = data.get("parents")
        if parents is None:
            parents = []
        if not isinstance(parents, list) or not all(isinstance(p, str) for p in parents):
            errors.append(f"{uid}: 'parents' must be a list of UIDs")
            parents = [p for p in parents if isinstance(p, str)] if isinstance(parents, list) else []
        item.parents = parents
        order = data.get("order", 0)
        if not isinstance(order, (int, float)) or isinstance(order, bool):
            errors.append(f"{uid}: 'order' must be a number")
            order = 0
        item.order = order
        if "tests" in data:
            item.tests = _parse_tests(data["tests"], errors, uid)
        reviewed = data.get("reviewed")
        if reviewed is not None and (not isinstance(reviewed, str) or not SHA_RE.match(reviewed)):
            errors.append(f"{uid}: 'reviewed' must be a sha256 hex stamp")
            reviewed = None
        item.reviewed = reviewed
        items[uid] = item
    return items, errors


def children_map(items):
    children = {uid: set() for uid in items}
    for item in items.values():
        for parent in item.parents:
            if parent in children:
                children[parent].add(item.uid)
    return children


def sorted_children(items, children, uid):
    return sorted(children.get(uid, ()), key=lambda c: (items[c].order, c))


def stamp_payload(item):
    tests = None
    if item.tests is not None:
        tests = {name or "": sha or "" for name, sha in item.tests.items()}
    return {"description": item.description, "header": item.header, "parents": list(item.parents), "tests": tests}


def compute_stamp(item):
    canonical = json.dumps(stamp_payload(item), sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def validate_structure(items):
    errors = []
    children = children_map(items)
    roots = []
    for uid, item in sorted(items.items()):
        if not UID_RE.match(uid):
            errors.append(f"{uid}: file stem is not a valid UID")
        for parent in item.parents:
            if parent == uid:
                errors.append(f"{uid}: links to itself")
            elif parent not in items:
                errors.append(f"{uid}: unknown parent '{parent}'")
        if len(set(item.parents)) != len(item.parents):
            errors.append(f"{uid}: duplicate parents")
        if not item.parents:
            roots.append(uid)
        kids = children[uid]
        if item.is_leaf and kids:
            errors.append(f"{uid}: has both 'tests' and children {sorted(kids)}; leaf and branch are mutually exclusive")
        if not item.is_leaf and not kids and not item.deferred:
            errors.append(f"{uid}: childless item without 'tests' must carry the (defer) token")
        if not item.description.strip():
            errors.append(f"{uid}: empty description")
        if item.is_leaf and not item.deferred and item.description.count("shall") != 1:
            errors.append(f"{uid}: test-bearing leaf description must contain exactly one 'shall'")
        if item.reviewed:
            if item.is_leaf and any(sha is None for sha in item.tests.values()):
                errors.append(f"{uid}: reviewed but a binding has no stamped routine sha")
            elif compute_stamp(item) != item.reviewed:
                errors.append(f"{uid}: reviewed stamp does not match item content (edit without user re-review)")
    if len(roots) != 1:
        errors.append(f"tree: expected exactly one root item with empty parents, found {len(roots)}: {sorted(roots)}")
    errors.extend(_cycle_errors(items, children))
    return errors


def _cycle_errors(items, children):
    state = {}  # uid -> 1 visiting, 2 done
    errors = []

    def visit(uid, trail):
        if state.get(uid) == 2:
            return
        if state.get(uid) == 1:
            errors.append(f"tree: cycle through {' -> '.join(trail + [uid])}")
            return
        state[uid] = 1
        for child in sorted(children.get(uid, ())):
            visit(child, trail + [uid])
        state[uid] = 2

    for uid in sorted(items):
        visit(uid, [])
    return errors


def bindings_from_tags(tags):
    """Ordered tag list -> [(uid, binding_name|None)]; the binding name is the
    tag immediately following its UID tag."""
    out = []
    for i, tag in enumerate(tags):
        if not UID_RE.match(tag):
            continue
        nxt = tags[i + 1] if i + 1 < len(tags) else None
        if nxt and BINDING_NAME_RE.match(nxt) and not UID_RE.match(nxt):
            out.append((tag, nxt))
        else:
            out.append((tag, None))
    return out


def _python_locations(root):
    found = {}
    for base in PY_TEST_DIRS:
        for path in sorted((root / base).rglob("*.py")):
            source = path.read_text(encoding="utf-8")
            try:
                tree = ast.parse(source)
            except SyntaxError:
                continue
            lines = source.splitlines(keepends=True)
            for node in ast.walk(tree):
                if not isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                    continue
                for binding in _req_marks(node):
                    first = min([node.lineno] + [d.lineno for d in node.decorator_list])
                    span = "".join(lines[first - 1:node.end_lineno])
                    rel = str(path.relative_to(root))
                    found.setdefault(binding, []).append(Location(rel, first, f"{rel}::{node.name}", span))
    return found


def _req_marks(node):
    for decorator in node.decorator_list:
        if not (isinstance(decorator, ast.Call) and isinstance(decorator.func, ast.Attribute) and decorator.func.attr == "req"):
            continue
        args = [a.value for a in decorator.args if isinstance(a, ast.Constant) and isinstance(a.value, str)]
        if args:
            yield (args[0], args[1] if len(args) > 1 else None)


CPP_TEST_MACRO_RE = re.compile(r"^\s*(TEST_CASE|SCENARIO|TEMPLATE_TEST_CASE|TEST_CASE_METHOD)\s*\(", re.MULTILINE)
CPP_TAGS_RE = re.compile(r'"((?:\[[^\]"]+\])+)"')


def _cpp_locations(root):
    found = {}
    for base in CPP_TEST_DIRS:
        for path in sorted((root / base).rglob("*.cpp")):
            source = path.read_text(encoding="utf-8", errors="replace")
            starts = [m.start() for m in CPP_TEST_MACRO_RE.finditer(source)]
            for i, start in enumerate(starts):
                end = starts[i + 1] if i + 1 < len(starts) else len(source)
                span = source[start:end]
                tags_match = CPP_TAGS_RE.search(span)
                if not tags_match:
                    continue
                tags = re.findall(r"\[([^\]]+)\]", tags_match.group(1))
                line = source[:start].count("\n") + 1
                rel = str(path.relative_to(root))
                for binding in bindings_from_tags(tags):
                    found.setdefault(binding, []).append(Location(rel, line, f"{rel}:{line}", span))
    return found


def discover_bindings(root=ROOT):
    """Map (uid, binding_name|None) -> [Location] over all known test trees."""
    found = _python_locations(root)
    for binding, locations in _cpp_locations(root).items():
        found.setdefault(binding, []).extend(locations)
    return found


def routine_sha(location):
    return hashlib.sha256(location.span.encode("utf-8")).hexdigest()


def binding_tag(uid, name):
    return f"[{uid}][{name}]" if name else f"[{uid}]"


def check_frozen(items, discovered):
    """Reviewed leaves: every binding resolves to exactly one routine whose
    span hash still matches the stamped sha."""
    errors = []
    for uid, item in sorted(items.items()):
        if not (item.reviewed and item.is_leaf):
            continue
        for name, sha in item.tests.items():
            tag = binding_tag(uid, name)
            locations = discovered.get((uid, name), [])
            if not locations:
                errors.append(f"{uid}: no test routine tagged {tag} found")
            elif len(locations) > 1:
                errors.append(f"{uid}: tag {tag} is ambiguous: {', '.join(l.name for l in locations)}")
            elif sha and routine_sha(locations[0]) != sha:
                errors.append(f"{uid}: frozen test routine {locations[0].name} modified after review (requires user re-review)")
    return errors


def check_bindings_exist(items, discovered):
    """Unreviewed leaves: report (non-fatal) bindings with no tagged routine yet."""
    pending = []
    for uid, item in sorted(items.items()):
        if item.reviewed or not item.is_leaf or item.deferred:
            continue
        for name in item.tests:
            if not discovered.get((uid, name)):
                pending.append(f"{uid}: no test routine tagged {binding_tag(uid, name)} yet")
    return pending


def load_coverage(paths):
    """[(binding -> [records])], [errors]; records keep optional name/log."""
    records, errors = {}, []
    for path in paths:
        path = Path(path)
        if not path.is_file():
            errors.append(f"coverage report missing: {path}")
            continue
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip():
                continue
            try:
                record = json.loads(line)
                tags = record["tags"]
                passed = bool(record["passed"])
            except (json.JSONDecodeError, KeyError, TypeError):
                errors.append(f"{path}:{n}: malformed coverage record")
                continue
            record = {"passed": passed, "name": record.get("name", ""), "log": record.get("log", ""), "tags": tags}
            for binding in bindings_from_tags([t for t in tags if isinstance(t, str)]):
                records.setdefault(binding, []).append(record)
    return records, errors


def check_coverage(items, records):
    """Reviewed leaf bindings need >=1 executed record; unknown UIDs in records
    are typos."""
    errors = []
    for uid, item in sorted(items.items()):
        if not (item.reviewed and item.is_leaf):
            continue
        for name in item.tests:
            if (uid, name) not in records:
                errors.append(f"{uid}: binding {binding_tag(uid, name)} bound but not run in any coverage report")
    known_bindings = {(uid, name) for uid, item in items.items() if item.is_leaf for name in item.tests}
    for (uid, name) in sorted(records, key=lambda b: (b[0], b[1] or "")):
        if uid not in items:
            errors.append(f"coverage: records tagged {binding_tag(uid, name)} match no known requirement")
        elif items[uid].is_leaf and (uid, name) not in known_bindings:
            errors.append(f"{uid}: coverage records for undeclared binding {binding_tag(uid, name)}")
    return errors


def leaf_status(item, records):
    if not item.is_leaf:
        return NOT_IMPLEMENTED
    per_binding = [records.get((item.uid, name), []) for name in item.tests]
    if all(not recs for recs in per_binding):
        return NOT_IMPLEMENTED
    if any(not r["passed"] for recs in per_binding for r in recs):
        return TEST_FAILED
    if all(recs for recs in per_binding):
        return TEST_PASSED
    return PARTIALLY_IMPLEMENTED


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


def compute_status(items, records):
    children = children_map(items)
    memo = {}

    def status_of(uid):
        if uid in memo:
            return memo[uid]
        memo[uid] = NOT_IMPLEMENTED  # cycle guard
        kids = children[uid]
        memo[uid] = aggregate([status_of(k) for k in kids]) if kids else leaf_status(items[uid], records)
        return memo[uid]

    report = {}
    for uid, item in items.items():
        tests = []
        if item.is_leaf:
            for name in item.tests:
                for record in records.get((uid, name), []):
                    tests.append({"binding": name or "", "name": record["name"] or binding_tag(uid, name), "passed": record["passed"], "log": record["log"]})
        report[uid] = {
            "status": status_of(uid),
            "header": item.header,
            "description": item.description.strip(),
            "parents": list(item.parents),
            "children": sorted_children(items, children, uid),
            "order": item.order,
            "folder": item.folder,
            "reviewed": bool(item.reviewed),
            "deferred": item.deferred,
            "tests": tests,
        }
    return report


class _BlockStr(str):
    pass


class _ItemDumper(yaml.SafeDumper):
    pass


_ItemDumper.add_representer(_BlockStr, lambda dumper, value: dumper.represent_scalar("tag:yaml.org,2002:str", value, style="|"))
_ItemDumper.add_representer(type(None), lambda dumper, value: dumper.represent_scalar("tag:yaml.org,2002:null", "~"))


def dump_item(item):
    data = {"header": item.header, "description": _BlockStr(item.description if item.description.endswith("\n") else item.description + "\n"), "parents": list(item.parents)}
    if item.order:
        data["order"] = item.order
    if item.tests is not None:
        if set(item.tests) == {None}:
            data["tests"] = item.tests[None]
        else:
            data["tests"] = dict(item.tests)
    if item.reviewed:
        data["reviewed"] = item.reviewed
    return yaml.dump(data, Dumper=_ItemDumper, sort_keys=False, default_flow_style=None, allow_unicode=True, width=120)


def write_item(item):
    item.path.write_text(dump_item(item), encoding="utf-8")
