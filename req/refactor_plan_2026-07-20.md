# Requirements Tooling Refactor — Design & Plan (pending user review)

Status: **plan for review, nothing implemented**. The live system is still Doorstop-based; its
former documentation is in git history of this file (commit 986d18c and earlier).

# Description

Replace Doorstop with a self-owned requirements toolkit. Two drivers:

1. **Blocking weakness**: test binding is file-granular in three independent places — the review
   freeze hashes the whole test file (`check_frozen_tests.py`), coverage only greps the `[UID]`
   literal anywhere in the file (`check_req_coverage.py`), and status maps JUnit cases by file
   path, ignoring tags (`req_status.py`). Editing one routine in a shared file cascades re-reviews
   onto unrelated requirements.
2. **Dependency cost**: Doorstop's only load-bearing feature is its stamping, which is opaque
   (unverifiable base64, no extension point for routine-level shas) and imposes unwanted
   restrictions (forced `normative` on linked branches, filename=UID+prefix machinery, level
   reordering, per-dir `.doorstop.yml`).

# Decided Invariants (user, 2026-07-20)

- **No settings file at all.** No `.doorstop.yml`, no global `req.yaml`. UID = file stem, stamp
  policy hardcoded in reqlib, tree root is `req/` by convention.
- **Any `*.yml` under any `req/` subfolder is a requirement.** Folders are external sorting only,
  no semantic meaning, no prefix rules. Other extensions (`.md`, …) ignored by tooling.
  Global stem uniqueness enforced by validation. Moving a file between folders changes nothing.
- **Tree defined entirely inside item files** via `parents` and `tests`. Multi-parent = DAG,
  first-class. `level` dies.
- **Plain parent list** (user decision): `parents: [UID, …]`, no per-link stamps. Doorstop's
  suspect-link semantics are dropped; INFRA-050 must be reworded or removed.
- **Fields dropped**: `accept`, `test` (prose), `verify`, `derived`, `ref`, `active`,
  `normative`, `level`. Valuable prose folds into `description` at migration.
- **`text` renamed to `description`** (user, 2nd round).
- **`order` field added** (user, 2nd round): number, presentation-only sibling sort key for
  reports; siblings sort by `(order, UID)`; optional, default 0. **Excluded from the reviewed
  stamp** — reordering a report must never trigger re-review.
- **Single root enforced**: exactly one item with empty `parents` (PRODUCT-169).
- **Tag-only test binding, no paths or test names in items** (user, 2nd round — replaces the
  earlier `path::name` scheme): a binding is the structural tag pair `[UID][binding_name]`;
  a requirement's single default test is `[UID]` alone. Test source location is *discovered*
  from the tags at check time, never declared in the requirement file.
- **Coverage is report-driven, unified JSONL**: test executions report the tag sets they ran
  with pass/fail; the gate joins records against `tests:` — both directions must agree.

# Target Item Schema

```yaml
header: Build on push
description: |
  The CI pipeline shall build the project on every push and pull request.
  (exactly one "shall"; `(defer)` token marks deferral)
parents: [INFRA-065]
order: 10
tests:
  build_project_on_push: <routine-sha256, stamped at review; ~ before review>
  build_warnings_clean: ~
reviewed: <sha256-hex over header+description+parents+tests(incl. shas), or absent>
```

- `tests` forms: absent → branch (has children) or deferred leaf; `tests: ~` → single default
  test bound by `[UID]` tag alone (becomes `tests: <sha>` scalar once reviewed);
  mapping `name: sha` → each bound by `[UID][name]`. Names: `[a-z0-9_]+`, unique per item.
- Leaf = has `tests` key; branch = has children. Mutually exclusive, validated.
- `reviewed` is a transparent sha256 hex — verifiable with sha256sum. Stamping stays
  **user-only** (review = record of user approval). `order` and folder location excluded.

# Test Binding: Discovery, Hashing, Verification

Binding identity is the tag pair; location is derived, so tests can move freely between files
without touching requirement files or stamps.

- **Declaration in tests**: Catch2 `TEST_CASE("…", "[INFRA-066][build_project_on_push]")`;
  pytest `@pytest.mark.req("INFRA-066", "build_project_on_push")` (second arg optional for the
  default binding).
- **Discovery** (per binding, must match exactly one routine; 0 or >1 = gate error):
  - C++: authoritative = compiled binary: `unit_tests --list-tests --verbosity high "[UID][name]"`
    → test name + source file:line. Fallback without binary: regex over `test/**` for a TEST_CASE
    tag literal containing both tags (canonical order UID-first enforced), labeled weaker.
  - Python: `pytest --collect-only -m` marker filter → node id → file + function.
- **Routine hash**: sha256 of the routine's raw source span. C++ span = from the located
  TEST_CASE line to the next test macro in that file or EOF; Python span via
  `ast.get_source_segment`. Stamped into the item's `tests` map by the review tool.
- **Verification at gate** (reviewed leaves): re-discover each binding by tags, re-extract span,
  re-hash, compare. Changed → error requiring user re-review; missing/ambiguous → error.
  Consciously dropped: whole-file drift warning (no file is recorded anymore) — shared-fixture
  changes outside the routine span go undetected; accepted trade-off.

# Coverage JSONL Mechanism

- Record per executed routine: `{"tags": ["INFRA-066", "build_project_on_push"], "passed": true}`
  (C++: Catch2 event listener linked into `unit_tests` emits all UID-shaped tags + trailing
  binding names at testCaseEnded; Python: conftest hook from `req` markers; same file format,
  `req_coverage.jsonl`).
- Gate joins by tags: every reviewed leaf binding needs ≥1 record (missing → distinct
  "bound but not run" error); records with UID tags matching no known requirement → error
  (typo catcher). Failed record → `test_failed`; all pass → `test_passed`.
- Extension (not v1): flavor sub-tokens beyond the binding name.

# Command-Line API (user, 2nd round)

Single entry point `scripts/req.py` (thin subcommands over reqlib):

- `req new <UID> --parent <UID> [--dir req/<folder>] [--order N]` — scaffold an item file.
- `req review <UID>` — **user-only**: validates the item, discovers its bindings, *runs* the
  bound tests (unit_tests tag filter + pytest -m), and only on success computes routine shas +
  the reviewed stamp and writes them. `req clear <UID>` removes the stamp.
- `req report [--html <dir>]` — recursive status (JSON) + static HTML rendering of the tree
  (absorbs `req_status.py`/`render_req_report.py` as subcommands; CI keeps calling them
  through this entry point).
- `req validate` — structural validation + frozen + coverage checks (what `ci/gate.sh` calls).

# Relevant Files

- `scripts/reqlib.py` — **new**: load_tree, validate, canonical stamping, tag discovery,
  span hashing. pyyaml only.
- `scripts/req.py` — **new**: CLI entry point (new/review/clear/report/validate).
- `scripts/migrate_doorstop.py` — **new**, one-shot converter (user runs + commits).
- Catch2 listener TU linked into `unit_tests` + `scripts/tests/conftest.py` — coverage emitters.
- `scripts/req_status.py`, `check_frozen_tests.py`, `check_req_coverage.py`,
  `check_self_approval.py`, `render_req_report.py` — logic ports into reqlib/req.py.
- `ci/gate.sh`, `.github/workflows/validate.yml` — call `req validate` / `req report`; drop
  doorstop from venv.
- `scripts/tests/*` — 6 files rebuild fixtures as plain YAML dirs.
- `scripts/import_requirements.py` — retarget to new schema (needed for the full
  requirements_plan.md re-import later).

# Refactoring Plan (phases, each user-reviewed per TDD freeze workflow)

1. **Requirements first**: redraft the INFRA 1.0 subtree to describe the new tooling (schema,
   tag binding, JSONL coverage, routine freeze, CLI); remove/reword INFRA-050 (suspect links)
   and any item whose text names Doorstop. User reviews the redraft before code.
2. **reqlib + validation + its pytest suite** (green in parallel with live doorstop gate).
3. **Migration**: converter transforms all 22 items (16 infra + 6 product) to the new schema
   (text→description, references→tests bindings with canonical tags added to the bound tests);
   the 16 reviewed items re-stamped by the user in one commit.
4. **Coverage emitters + `req review` with tag discovery and routine-sha stamping.**
5. **Port gate + CI to `req validate`/`req report`**; delete doorstop from venv; delete
   `.doorstop.yml`.
6. **Rewrite this README** as the final documentation of the new system.

# Current State Facts (verified 2026-07-20, branch req-bootstrap)

- Tree: 22 items — 16 in `req/infra/` (all reviewed/frozen) + 6 in `req/product/` (PRODUCT-169
  root, PRODUCT-168 Infrastructure branch, rest are `(defer)` placeholders; PRODUCT-001 is
  `normative: false` heading → becomes a plain branch item in the new model).
- **PRODUCT hierarchy is level-only**: every product item has `links: []`; INFRA items carry real
  `links` (INFRA-041→PRODUCT-168 crosses documents). Migration must synthesize `parents` from
  level containment for PRODUCT and from `links` for INFRA; after migration `level` is deleted
  (its sibling ordering moves into `order`).
- **All 16 current bindings target pytest files** under `scripts/tests/` (no C++ test is
  requirement-bound yet; C++ tests carry only domain tags like `[datahub][feed]`). So phase 3
  needs only pytest `req` markers added to the specific bound test functions — INFRA-043/044
  currently share `test_req_status.py` whole-file and finally become per-routine. The Catch2
  listener is still built in phase 4: it's required before the full product tree re-import.
- UID-shaped tag regex (listener + validation): `[A-Z][A-Z_]*-[0-9]+`.
- CI flow today: build job → ctest `--output-junit` → artifact `ctest-results` → requirements
  job runs gate.sh + pytest `--junitxml` → `req_status.py --junit ×2` → Check Run
  (`publish_check_run.py`) + job summary (`ci/write_status_summary.py`). New flow: both suites
  emit/append `req_coverage.jsonl` (C++ one uploaded as artifact), `req report` joins JSONL only;
  junitparser dependency drops, venv keeps pyyaml + pytest + jinja2.
- `ci/gate.sh` bootstraps `.venv-req` itself; relaxed mode is default, `GATE_STRICT=1` = full
  doorstop `--error-all` (concept survives as: strict = every item reviewed).
- `check_self_approval.py` (CI job guarding that stamps only change in user-authored commits)
  reads raw YAML already — port is renaming fields only.
- Convention retained: exactly one "shall" per description; `(defer)` per-leaf token authoritative.

# Key Findings (analysis backing the plan)

- Doorstop surface is shallow: 4 scripts use only `doorstop.build()` + ~12 read-only item attrs,
  all directly derivable from raw YAML; `check_self_approval.py` already parses raw YAML.
- `doorstop -W -S --error-all` in relaxed mode contributes only structural validation, half of it
  duplicated by `test_doorstop_tree_shape.py`.
- File-shas identical across INFRA-043/044 (shared `test_req_status.py`) demonstrate the cascade.
- Catch2 `--list-tests --verbosity high` reports source file:line per test — the compiled binary
  is the authoritative tag→location resolver, immune to source-parsing drift.
- Tag-only binding makes requirement files independent of test-tree layout: renames/moves of
  test files no longer touch `req/` at all.

## Blockers

- Phase 1 INFRA redraft needs user review before any implementation (freeze workflow).
