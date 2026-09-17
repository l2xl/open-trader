# Synergy Context Gate (syngate)

Self-owned product architecture analysis, synthesis support and tracking toolkit. The tree of item
files under `syngate/` is the single source of truth — the product's features, architecture and
contracts decomposed into items that serve both as the context fed to AI-driven work and as the
TDD gate every bound test is frozen against. `scripts/syngatelib.py` is the library,
`scripts/syngate.py` the CLI.

# Tree Model

- **Any `*.yml` file under any `syngate/` subfolder is one item.** The UID is the file stem
  (`INFRA-066.yml` → `INFRA-066`), globally unique. Folders are external sorting only — moving a
  file between folders changes nothing. Other extensions (`.md`, …) are ignored by tooling.
- **The tree shape lives inside the items** via `parents`. Multi-parent items form a DAG; exactly
  one item has empty `parents` (the root, `OPEN-TRADER`). No settings files anywhere.
  Use consideration to name items, which represents a whole feature, without a number (like top OPEN_TRADER),
  then its subbranches may be groupped by subfolder and have same name and numbered suffix (unless it again represent large feature)
- **Leaf vs branch is structural**: a leaf carries a `tests` key; a branch has children. Mutually
  exclusive. A childless item without `tests` is simply not yet implemented — it rolls up as
  `not_implemented` like any leaf with no coverage.

# Item Schema

```yaml
header: CI build on push
description: |
  The CI pipeline shall build the project on every push to the repository.
parents: [INFRA-065]
order: 10
tests: ~
reviewed: <sha256 hex, present only after user review>
```

- `header` — a minimal noun phrase naming the behaviour (`Per-subscription condition`), no mechanism and no
  restated assertion; the description carries the contract.
- `description` — exactly one "shall" on test-bearing leaves.
- `order` — optional, presentation-only sibling sort key; siblings sort by `(order, UID)`.
  Excluded from the reviewed stamp: reordering a report never triggers re-review.
- `tests` forms: `~` → single default test bound by the `[UID]` tag alone (becomes
  `tests: <routine-sha256>` once reviewed); mapping `name: sha|~` → each binding bound by the
  `[UID][name]` tag pair. Binding names: `[a-z0-9_]+`, unique per item.
- `reviewed` — transparent stamp: sha256 hex over the canonical JSON
  `{"description":…,"header":…,"parents":[…],"tests":{name:sha}|null}` (`sort_keys`, compact
  separators, UTF-8; default binding name `""`, unstamped shas `""`). Recompute with
  `syngatelib.compute_stamp` or plain `hashlib`+`json`. Stamping is **user-only** — the stamp is the
  record of user approval.

# Test Binding

Binding identity is the tag pair; the routine's location is *discovered* from tags at check time,
never declared in items — tests move freely between files without touching `syngate/`.

- **Pytest** (`scripts/tests/`): `@pytest.mark.syngate("INFRA-066")` for the default binding,
  `@pytest.mark.syngate("INFRA-066", "binding_name")` for a named one. Discovered by an `ast` scan.
- **Catch2** (`test/`): `TEST_CASE("…", "[INFRA-066]")` or `…[INFRA-066][binding_name]`.
  Discovered by a source scan for test macros; the binding name is the tag immediately following
  the UID tag, so place item tags last.
- Each binding must resolve to **exactly one** routine; 0 or >1 is a gate error on reviewed items.
- **Routine hash**: sha256 of the routine's raw source span (pytest: decorators through the end of
  the function; Catch2: the TEST_CASE line to the next test macro or EOF). Shared-fixture changes
  outside the span are consciously not tracked.

# Coverage JSONL

Test executions self-report which bindings ran: one JSON line
`{"tags": ["INFRA-066", "binding_name"], "passed": true, "name": …, "log": …}` appended to the
file named by `SYNGATE_COVERAGE_FILE` (no emission when unset).

- Emitters: the pytest hook in `scripts/tests/conftest.py` and the Catch2 listener
  `test/syngate_coverage_listener.cpp` (linked into every test executable by `add_unit_test`).
- The gate joins records against `tests:` in both directions: every reviewed leaf binding needs ≥1
  executed record, and every record's UID must match a known item.

# CLI (`scripts/syngate.py`)

- `syngate new <UID> --parent <UID> [--dir syngate/<folder>] [--order N]` — scaffold an item.
- `syngate review <UID>` — **user-only**: validates the item, discovers its bindings, runs the bound
  tests, and stamps routine shas + `reviewed` once every binding resolves to exactly one runnable
  routine — whether that routine currently passes or fails. A failing routine still freezes and the
  leaf simply rolls up as `test_failed` (TDD red state) until the implementation lands; only a
  binding that can't be run at all (ambiguous, unresolved, or not built) blocks stamping. `syngate clear
  <UID>` removes the stamp (and reverts shas to `~`).
- `syngate validate [--coverage FILE …] [--strict]` — structural validation + frozen-routine checks
  (+ coverage join when given files; `--strict` requires every item reviewed). CI entry:
  `ci/gate.sh` (bootstraps `.venv-syngate`: pyyaml, pytest, jinja2; `GATE_STRICT=1` adds `--strict`).
- `syngate report [--coverage FILE …] [--out syngate_status.json] [--html <dir>]` — recursive status
  rollup + static HTML site.
- `syngate ui [--port N] [--coverage FILE …] [--build-dir DIR] [--no-browser]` — serve the local tree
  editor (`scripts/syngate_ui.py` + `syngate_ui.html`, stdlib-only) at `http://127.0.0.1:8712`.
  - **The outline tree carries every structured field of an item**: rollup status, UID, problem /
    review / leaf badges, the header (edited in place) and — by the row's position — `parents` and
    `order`. The text panel keeps only the raw description and the tests section (bindings, review /
    clear with output streamed into the page).
  - **Row tools**: `+` scaffolds a child inline (UID suggested from the siblings), `✕` deletes a
    childless item, `⊖` drops one parent link of a multi-parent item.
  - **Drag a row by its UID** (keyboard twin: Alt+Shift+arrows): drop on a row's edge to reorder, on
    its middle to re-parent, Ctrl+drop to add one more parent. `order` is set automatically — a free
    integer between the neighbours' keys (one file written), else the family is renumbered in steps
    of 10. `order` is one key per item, so a multi-parent item carries the same key under each parent.
  - **Autosave**: typing is stored per field after ~1 s idle and on leaving the field or the tab.
    Every write is a compare-and-swap against the value the page loaded, so an edit made on disk
    meanwhile (agent, git, IDE) is never silently overwritten — the page offers keep mine / take
    theirs. Reviewed items stay read-only until unlocked through their `✓` badge, since any substance
    edit makes the stamp stale (typing the text back restores it).
  - Review / clear shell out to the `syngate.py` code path, so stamping semantics (user-only,
    test-gated) are identical to the terminal. Loopback-bound; every request needs the per-session
    token from the printed URL (Jupyter-style defense for localhost tools that execute commands).

# Status Rollup

Leaf: no executed records → `not_implemented`; any failed record → `test_failed`; all bindings
covered and passing → `test_passed`; some covered → `partially_implemented`. A childless item
without a `tests` key has no bindings to cover, so it rolls up as `not_implemented` the same way.
Branch: aggregate of children (any failed → failed; all not_implemented → not_implemented; all
passed → passed; else partial).

**Validation problems redden the item they name, never the item that found them.** A stale review
stamp, a drifted frozen routine or a malformed item is a defect of *that* item: `item_problems`
attributes it by UID, `compute_status` marks the item `test_failed`, and it rolls up through that
item's own parents only. An item whose bound test detects such a violation is working, so it
stays green — the tooling's own tests assert tooling behaviour against fixtures, and where they read
the live tree (`INFRA-030`) they assert its *layout* (`validate_layout`), not its review state.
Coverage gaps are deliberately not item problems: an unrun binding already rolls up as
`not_implemented`, and one missing coverage file would otherwise redden every reviewed leaf at once.

# Process Rules (TDD gate)

- **Test-first, then freeze.** The covering routine is tagged with the leaf's UID before
  implementation; the user approves via `syngate review`, which freezes the routine by hash while it is
  still red — approval fixes *which* routine and *what it says*, not whether it already passes.
- **Frozen routines are immutable.** Editing a reviewed leaf's bound routine reddens the gate until
  a user-approved `syngate clear` + re-review.
- **Two-commit re-approval** (`scripts/check_self_approval.py`, CI `approval` job): a commit that
  changes an item's approval may neither change the item's substance nor touch any file under the
  test trees.
- **CI flow** (`.github/workflows/validate.yml`): build → ctest (emits
  `build-ci/syngate_coverage.jsonl`, uploaded as `syngate-coverage-cpp`) → syngate job: `gate.sh`,
  pytest (emits `pytest-coverage.jsonl`), coverage join via `gate.sh --coverage …`, `syngate report`,
  job summary + `Syngate Status` check run.
- **A red gate never costs the report.** The gate's verdict decides the job's colour, not whether
  the run is reported: every report step runs on `!cancelled()`, and `gate.sh`'s output (`gate: OK`
  / `gate: FAILED`) is captured into `syngate_validate.log` and folded into the top of both the job
  summary and the check run, so the reason for the red is read off the report itself.

# History

- **2026-07-20** — converted from Doorstop by `scripts/migrate_doorstop.py` (one-shot; kept for
  reference) into the self-owned `reqlib` / `req.py` requirements toolkit. All review stamps were
  dropped at conversion and re-stamped by the user as bindings landed. `scripts/import_requirements.py`
  still targets the old schema and must be retargeted before the full `requirements_plan.md`
  re-import. The pre-refactor Doorstop documentation lives in this file's git history; the refactor
  design plan is [refactor_plan_2026-07-20.md](refactor_plan_2026-07-20.md).
- **2026-09-18** — refocused from requirements management to product architecture analysis and
  synthesis support, and renamed to Synergy Context Gate: `req/` → `syngate/`, `reqlib.py` →
  `syngatelib.py`, `req.py` → `syngate.py`, `req_ui.*` → `syngate_ui.*`, `@pytest.mark.req` →
  `@pytest.mark.syngate`, `REQ_COVERAGE_FILE` → `SYNGATE_COVERAGE_FILE`, `.venv-req` → `.venv-syngate`.
