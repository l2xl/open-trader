# Requirements Tree

Requirements are managed with [Doorstop](https://github.com/doorstop-dev/doorstop) (v3.1, installed into `.venv-req/`).

## Bootstrap tree

This tree is currently **minimal and self-referential**: the requirements needed to build the requirements
system itself, not yet the product's full requirements plan. `PRODUCT` is rooted at `PRODUCT-169` ("XCockpit",
the product itself), with 5 top-level branch children, 4 of them `(defer)` placeholders with no sub-items;
`INFRA` carries only its `1.0` (requirements tracking tooling) and `3.0` (CI/CD at GitHub) branches in full,
with `2.0` (auto tests) reduced to a bare `(defer)` placeholder. `CORE`, `DATA_MODEL`, `TRADER_HUD`, and `APP`
do not exist yet — they, and the rest of `PRODUCT`, come back once this bootstrap tree is itself reviewable
and working end to end.

## Document tree

```
PRODUCT  req/product      PRODUCT-169 (root) -> 5 branches (4 deferred, 1 = Infrastructure)
 └─ INFRA       req/infra       requirements-tooling itself: 1.0 tracking, 2.0 auto tests (defer), 3.0 CI/CD
```

## Anatomy of a requirement

Every item records **mandated functionality** — a property of the product or its infrastructure that must hold and stay verifiable — never a ticket, a task, or a one-off "remove/avoid X" work item. A requirement states the **goal, not the way to reach it**:

- `header` — the goal in a few words; the item's primary human-readable label, used as its name in status reports. Every item carries one.
- `text` — a concise normative statement using exactly one **"shall"**; it may add a preferred delivery detail only where that choice is itself deliberate. One consistent "shall" per leaf is what keeps import parsing and human scanning both reliable.
- `accept` / `test` — carry the "how": the acceptance condition and the shape of the bound test.

Every requirement must be verifiable in terms observable by its bound test; wording that cannot be checked (or holds only outside the verification environment) disqualifies the item as a requirement. Test binding is ideally **one-to-one** — each leaf references its own dedicated test. A single test covering several leaves is tolerable only in rare cases, and a many-to-many requirement↔test relation always signals wrong decomposition: binding is file-granular, so a shared test file is exactly that smell.

A requirement is satisfied in exactly two ways, with no exemptions — a **leaf** (no children) when an automated test bound to its UID passes, a **branch** (has children) when all of its children are (see the two sections below). There is no manual-inspection escape.

## One explicit parent chain, no mesh

Every item on the path from a leaf up to `PRODUCT-169` carries a real `links:` entry to its immediate parent
— not just level nesting, and never a prose mention like "see [INFRA-040]" in a `text:` field (that's a
readable cross-reference, not a structural link; see "Cross-cutting requirements" below). Concretely:
`INFRA-043` links to `INFRA-042`, `INFRA-042` links to `INFRA-041`, `INFRA-041` links to `PRODUCT-168`.
This makes every branch/section heading a real (if non-testable) node in the traceability graph, which
Doorstop requires to be `normative: true` (a non-normative item cannot appear in
`links:` at all, in either direction — a hard validator error, not a convention). A branch carries no
`verify` attribute and no test binding of its own: it is satisfied precisely when all of its children are. `PRODUCT-168`'s own relation
to the `PRODUCT-169` root stays level-only, matching how the rest of `PRODUCT`'s hierarchy has always worked.

The result is a single tree with one root and no multi-parent nodes in this reduced tree — verified by
walking the same `links:` + level-containment graph `scripts/req_status.py` computes. Doorstop's `links` field
can technically point at more than one parent (a leaf shared by two independent features, say), and nothing
stops that when it's genuinely needed — it just isn't exercised here, on purpose, to keep this bootstrap tree
easy to reason about end to end.

## Requirement → test binding

Every normative leaf with `verify: test` carries a `references` entry:

```yaml
references:
  - path: test/<component>/test_<name>.cpp
    type: file
    keyword: DATA_MODEL-042
```

and the covering Catch2 `TEST_CASE` carries the tag `[DATA_MODEL-042]`. The tag doubles as the per-requirement test filter: `<test-exe> "[DATA_MODEL-042]"`. There is no manual-inspection alternative: a leaf that cannot be covered by an automated test is not a valid requirement and must be reformulated or dropped — the coverage gate rejects `verify: inspection` outright.

## Deferred requirements

`(defer)` is the single marker for "not scheduled yet, consciously postponed" — never silently dropped. The **per-leaf token is authoritative**; every deferred leaf carries its own `(defer)`, even when its case header also carries one as a human-readable hint for a wholly-deferred case (e.g. `#### DATA_MODEL: Case 7.4 — Batch operations (defer)`). A case header alone is never sufficient — a case can be *mixed* (some leaves active, some deferred), and the importer only reads the per-leaf marker.

An advanced/complex feature that hasn't been scheduled into a phase gets **exactly one `(defer)` marker** at the case or leaf it naturally attaches to — never a full per-component decomposition into rendering/data-binding/column-level sub-leaves. Decomposition happens when the feature is actually scheduled, as part of that phase's user-reviewed iteration planning. This keeps the tree sized for what's being built, not for every feature anyone might eventually want.

## Cross-cutting requirements

A requirement lives under exactly one document for decomposition purposes — its natural owner. When it's also consumed by, or interacts with, a requirement in another branch, that relationship is a **trace reference, not a second copy**: name the other item inline with `(consumes [ID])` / `(rendered by [ID])`, e.g. `TRADER_HUD-079` "(rendered by [APP-064])", `DATA_MODEL-035` "(the kill-switch path ([DATA_MODEL-066]) is exempt)". Doorstop's `links` field can point at any UID, not just the document's structural parent, so a leaf may formally link to more than one parent when the relationship is that direct. Don't restructure the tree (collapse branches, flatten hierarchy) to avoid a cross-cutting collision — pick the one clear owner and reference it from everywhere else.

## TDD freeze workflow

1. Requirement drafted (item exists, unreviewed). **The whole tree is unreviewed today** — drafted and awaiting review; nothing is frozen.
2. Tests written first and bound via `references` + tag.
3. User reviews requirement + tests; approval is stamped with `doorstop review <UID>` — this fingerprints the item **including the referenced test file SHA-256** (`item_sha_required`).
4. Implementation follows. **Editing a frozen (reviewed) test is prohibited**: the gate fails until the user approves a re-review (`doorstop clear <UID>` + `doorstop review <UID>`).

`doorstop review` and `doorstop clear` are **user-only actions** — review state *is* the record of the user's approval, so no agent ever stamps or clears one. The importer likewise always lands items unreviewed.

## Gate

`ci/gate.sh` (locally) and `.github/workflows/validate.yml` (CI) run:

1. `doorstop -W -S --error-all` — tree validation, all warnings as errors; review/suspect checks stay off while the tree is adopted item by item (freeze semantics live in the scripts below). `GATE_STRICT=1 ci/gate.sh` runs the full `--error-all` once the whole tree is reviewed.
2. `scripts/check_frozen_tests.py` — re-hashes reviewed references; Doorstop stamps SHAs at review but never re-verifies them itself.
3. `scripts/check_req_coverage.py` — every reviewed normative leaf has a valid test binding; unreviewed leaves are pending, non-fatal.

CI additionally builds the C++ unit tests (`cmake --build <build-dir> --target unit_tests`) and runs the offline profile (`ctest -LE live --output-junit ...`); `scripts/req_status.py` folds the pytest and ctest JUnit reports into a recursive per-requirement status (with per-test logs), published as a native Check Run on the commit by `scripts/publish_check_run.py`. `scripts/render_req_report.py` renders the same status JSON as a local static HTML site for offline browsing.

## Cheat sheet

```
.venv-req/bin/doorstop                            # validate tree
.venv-req/bin/doorstop publish all                 # render documents
.venv-req/bin/doorstop add DATA_MODEL              # new leaf in a document
.venv-req/bin/doorstop link DATA_MODEL-042 PRODUCT-007
.venv-req/bin/doorstop review DATA_MODEL-042       # freeze after user approval
.venv-req/bin/doorstop clear DATA_MODEL-042        # user-approved unfreeze/re-review
```
