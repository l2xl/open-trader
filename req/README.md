# Requirements System

Self-owned requirements toolkit — no external requirements manager. The tree of item files under
`req/` is the single source of truth; `scripts/reqlib.py` is the library, `scripts/req.py` the CLI.

# Tree Model

- **Any `*.yml` file under any `req/` subfolder is one requirement item.** The UID is the file stem
  (`INFRA-066.yml` → `INFRA-066`), globally unique. Folders are external sorting only — moving a
  file between folders changes nothing. Other extensions (`.md`, …) are ignored by tooling.
- **The tree shape lives inside the items** via `parents`. Multi-parent items form a DAG; exactly
  one item has empty `parents` (the root, `PRODUCT-169`). No settings files anywhere.
- **Leaf vs branch is structural**: a leaf carries a `tests` key; a branch has children. Mutually
  exclusive. A childless item without `tests` is a deferred leaf and must carry the `(defer)` token
  in its description.

# Item Schema

```yaml
header: CI build on push and pull request
description: |
  The CI pipeline shall build the project on every push and pull request.
parents: [INFRA-065]
order: 10
tests: ~
reviewed: <sha256 hex, present only after user review>
```

- `description` — exactly one "shall" on test-bearing leaves; `(defer)` marks deferral.
- `order` — optional, presentation-only sibling sort key; siblings sort by `(order, UID)`.
  Excluded from the reviewed stamp: reordering a report never triggers re-review.
- `tests` forms: `~` → single default test bound by the `[UID]` tag alone (becomes
  `tests: <routine-sha256>` once reviewed); mapping `name: sha|~` → each binding bound by the
  `[UID][name]` tag pair. Binding names: `[a-z0-9_]+`, unique per item.
- `reviewed` — transparent stamp: sha256 hex over the canonical JSON
  `{"description":…,"header":…,"parents":[…],"tests":{name:sha}|null}` (`sort_keys`, compact
  separators, UTF-8; default binding name `""`, unstamped shas `""`). Recompute with
  `reqlib.compute_stamp` or plain `hashlib`+`json`. Stamping is **user-only** — the stamp is the
  record of user approval.

# Test Binding

Binding identity is the tag pair; the routine's location is *discovered* from tags at check time,
never declared in items — tests move freely between files without touching `req/`.

- **Pytest** (`scripts/tests/`): `@pytest.mark.req("INFRA-066")` for the default binding,
  `@pytest.mark.req("INFRA-066", "binding_name")` for a named one. Discovered by an `ast` scan.
- **Catch2** (`test/`): `TEST_CASE("…", "[INFRA-066]")` or `…[INFRA-066][binding_name]`.
  Discovered by a source scan for test macros; the binding name is the tag immediately following
  the UID tag, so place requirement tags last.
- Each binding must resolve to **exactly one** routine; 0 or >1 is a gate error on reviewed items.
- **Routine hash**: sha256 of the routine's raw source span (pytest: decorators through the end of
  the function; Catch2: the TEST_CASE line to the next test macro or EOF). Shared-fixture changes
  outside the span are consciously not tracked.

# Coverage JSONL

Test executions self-report which bindings ran: one JSON line
`{"tags": ["INFRA-066", "binding_name"], "passed": true, "name": …, "log": …}` appended to the
file named by `REQ_COVERAGE_FILE` (no emission when unset).

- Emitters: the pytest hook in `scripts/tests/conftest.py` and the Catch2 listener
  `test/req_coverage_listener.cpp` (linked into every test executable by `add_unit_test`).
- The gate joins records against `tests:` in both directions: every reviewed leaf binding needs ≥1
  executed record, and every record's UID must match a known requirement.

# CLI (`scripts/req.py`)

- `req new <UID> --parent <UID> [--dir req/<folder>] [--order N]` — scaffold an item.
- `req review <UID>` — **user-only**: validates the item, discovers its bindings, runs the bound
  tests, and only on success stamps routine shas + `reviewed`. `req clear <UID>` removes the stamp
  (and reverts shas to `~`).
- `req validate [--coverage FILE …] [--strict]` — structural validation + frozen-routine checks
  (+ coverage join when given files; `--strict` requires every item reviewed). CI entry:
  `ci/gate.sh` (bootstraps `.venv-req`: pyyaml, pytest, jinja2; `GATE_STRICT=1` adds `--strict`).
- `req report [--coverage FILE …] [--out req_status.json] [--html <dir>]` — recursive status
  rollup + static HTML site.

# Status Rollup

Leaf: no executed records → `not_implemented` (deferred leaves always); any failed record →
`test_failed`; all bindings covered and passing → `test_passed`; some covered →
`partially_implemented`. Branch: aggregate of children (any failed → failed; all
not_implemented → not_implemented; all passed → passed; else partial).

# Process Rules (TDD gate)

- **Test-first, then freeze.** The covering routine is tagged with the leaf's UID before
  implementation; the user approves via `req review`, which freezes the routine by hash.
- **Frozen routines are immutable.** Editing a reviewed leaf's bound routine reddens the gate until
  a user-approved `req clear` + re-review.
- **Two-commit re-approval** (`scripts/check_self_approval.py`, CI `approval` job): a commit that
  changes an item's approval may neither change the item's substance nor touch any file under the
  test trees.
- **CI flow** (`.github/workflows/validate.yml`): build → ctest (emits
  `build-ci/req_coverage.jsonl`, uploaded as `req-coverage-cpp`) → requirements job: `gate.sh`,
  pytest (emits `pytest-coverage.jsonl`), coverage join via `gate.sh --coverage …`, `req report`,
  job summary + `Requirements Status` check run.

# Migration Note (2026-07-20)

Converted from Doorstop by `scripts/migrate_doorstop.py` (one-shot; kept for reference). All
review stamps were dropped at conversion — items are re-stamped by the user via `req review` as
their bindings land. `scripts/import_requirements.py` still targets the old schema and must be
retargeted before the full `requirements_plan.md` re-import. The pre-refactor Doorstop
documentation and the refactor design plan live in this file's git history.
