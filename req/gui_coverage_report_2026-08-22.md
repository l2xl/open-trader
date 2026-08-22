# GUI Requirements Coverage — Decision Report (Pros / Cons per Item)

Companion to `gui_coverage_analysis_2026-08-22.md` (which holds the background research and
sources). This report restates every suggested item in plain language, with explicit pros, cons,
effort, and a verdict, so each can be accepted or rejected independently.

**The one rule shared by all items:** verification evidence enters the gate only through an
executing Catch2/pytest routine that emits a coverage JSONL record — the same mechanism the req
system uses today. No item below adds a manual or declarative coverage channel.

---

## Item 1 — Scene-graph geometry assertions

**What it is.** Run a scratcher through its real lifecycle (`OnAttach` → `CalculateSize` →
`OnLayout`) against a synthetic panel with fixture data, then assert directly on the ThorVG scene
tree it produced: how many shapes, in what z-order, with what bounding boxes, colors, and
transform matrices. No rasterization at all. Example: "the order-mark shape's Y bound equals the
panel's price→pixel projection of the order price."

**Pros**
- Strongest available oracle: it tests code we own (scene construction), not ThorVG's rasterizer.
- Breakage is localized and self-explanatory: a color change fails only color assertions, a
  geometry bug fails only that shape's test — the failure message names the exact wrong value.
- Zero new dependencies; it is a direct scale-up of the pattern already used in
  `test/render/test_thorvg.cpp` (`check_matrix_equals`).
- Fully deterministic, fully wasm-portable (plain C++, compiles under Emscripten unchanged).
- The fixture/synthetic-panel harness it requires is the substrate every other item reuses.

**Cons**
- Blind to rasterization bugs: a perfect scene that ThorVG paints wrongly passes.
- Assertions are hand-written per component and per state — more authoring work than a snapshot.
- Tests can over-specify: asserting exact float coordinates where a tolerance was meant causes
  false failures; the tolerance policy has to be stated per assertion.
- Needs a test seam to construct `InstrumentContentPanel` without a real window/host — some
  refactoring of the panel may be required.

**Effort.** Medium: the panel test harness is the main cost; each scratcher test afterwards is
routine.

**Verdict: adopt first.** Highest value per unit of risk; everything else builds on its harness.

---

## Item 2 — Property/invariant tests for the coordinate pipeline

**What it is.** Generate many random inputs (timestamps, prices, viewport windows, pan/zoom
sequences) and assert that documented mathematical invariants always hold: price→pixel mapping is
monotonic; `TimeOfHudX(HudXOfTime(t))` round-trips within the documented error; the scene floor
is re-derived whenever `t_max − floor > 2^24 ms`. These invariants already exist as prose in
`src/cockpit/scratchers/README.md` §§2–3 — this item makes CI able to falsify them.

**Pros**
- The most "formal" item on the list: it verifies universally-quantified claims ("for all inputs
  in range..."), not hand-picked examples — closest in spirit to formal verification the project
  can get without a model checker.
- Finds edge cases humans don't write fixtures for (float-precision cliffs, degenerate windows,
  re-floor boundaries) — exactly the bug class of the layer-1→2 float cast.
- Cheap to run, no rendering involved, wasm-portable.
- Catch2 `GENERATE` plus a small seeded PRNG is enough — no new library, no license review.

**Cons**
- Only covers what has a stated invariant; it verifies the math contract, not visual appearance.
- Randomized tests must be seeded and reproducible or CI failures become unrepeatable; the seed
  policy must be part of the harness from day one.
- A failing case can be far from minimal — without shrinking (which real property-testing
  libraries provide and a home-grown generator won't), diagnosing may take longer.
- The documented tolerances must first be made precise; where the README is qualitative
  ("exact-ish"), someone has to decide the actual bound before it can be asserted.

**Effort.** Low–medium: the invariants are already specified; the work is the generator utility
and pinning down tolerances.

**Verdict: adopt, in parallel with Item 1.** Independent of it, and directly answers the
"preserve strict formal quality" requirement.

---

## Item 3 — Scene-dump text snapshots

**What it is.** A small serializer walks the ThorVG scene tree and writes a canonical text form
(node type, z-order, transform, bounds rounded to a stated precision, colors, clip) as YAML/JSON.
Tests render a fixture and diff the dump against a committed baseline file; a change shows up as
an ordinary git text diff that a human accepts or rejects at re-review.

**Pros**
- Broad regression net with almost no per-test authoring: one snapshot covers everything Item 1's
  hand-written assertions didn't think to assert.
- Diffs are readable and proportional to the change: a palette tweak diffs only color fields of
  affected baselines; the reviewer sees exactly what changed (unlike any pixel/digest scheme,
  where the answer is "something").
- Text baselines fit the repo's ethos: version-controlled, reviewable, no binary blobs, no
  image-diff dependency.
- The serializer is a triple investment: snapshot baselines now, ground truth for the AI judge
  (Item 7), and the future accessibility/semantic layer — same data, three consumers.
- Rasterizer-independent: valid under native SwCanvas, wasm, or a future GPU backend.

**Cons**
- Classic snapshot-test hazard: baselines get rubber-stamped. When 20 baselines change, the
  reviewer's diligence — not the tooling — is what catches the one unintended diff. (Item 9
  mitigates this.)
- A snapshot asserts "same as before," not "correct": the first baseline is only as right as the
  human who approved it.
- Precision policy is load-bearing: round floats too little and ULP noise diffs constantly; too
  much and real regressions hide inside the rounding. Needs an explicit documented choice.
- Baselines are extra files to maintain per fixture per scratcher; intentional refactors of scene
  structure (e.g. re-grouping shapes) churn all of them at once.

**Effort.** Low for the serializer; ongoing low-grade maintenance of baselines.

**Verdict: adopt as third step.** After Item 1 exists (so snapshots supplement targeted
assertions rather than replace them).

---

## Item 4 — Semantic raster probes

**What it is.** For a handful of small fixture scenes, actually rasterize on SwCanvas and assert
*semantic pixel facts* at computed locations: "the marker's interior contains painted pixels,"
"the area left of the axis is untouched," "two adjacent candles don't bleed into each other's
columns." Probe coordinates are derived from the same projection math the scene asserts — not
hardcoded. This is exactly the style `test/render/test_thorvg.cpp` already uses (alpha checks on
a 5×5 canvas).

**Pros**
- Covers the one gap Items 1–3 leave: a correct scene that rasterizes wrongly (clip applied in
  the wrong space, transform composed in wrong order, ThorVG regression on version bump).
- Robust to insignificant changes by construction: probes test paintedness/containment, not exact
  RGBA, so a color tweak doesn't break them.
- Deterministic on the pinned software rasterizer (ThorVG `v1.0.4` via CPM); a version bump
  visibly reddens these tests — which is the desired behavior, reviewed like any frozen change.
- Pattern already proven in-repo.

**Cons**
- Expensive to author relative to coverage gained: each probe is a hand-reasoned statement about
  stroke spreading and pixel geometry (see the ±0.5px stroke commentary in the existing test).
- Fragile at boundaries: antialiasing at non-integer coordinates makes "painted" threshold-y;
  probes must stick to structurally safe locations (interiors, clear gaps).
- Text is off-limits unless a fixed font ships with fixtures — font fallback differs per OS/CI
  image, so label pixels are nondeterministic across environments.
- Not valid on GPU backends; in a wasm future these tests only cover the SwCanvas path.

**Effort.** Low per probe, but keep the count deliberately small.

**Verdict: adopt sparingly.** A rasterization-integration safety net (a few probes per
component), never the primary oracle.

---

## Item 5 — Whole-image comparison (pixel hash / golden images) — REJECTED

**What it is.** The conventional approach: store a golden image (or a hash of the pixel buffer)
and compare renders against it. Listed for completeness because it was in revision 1 of the
analysis and was rejected on maintainer feedback.

**Pros**
- Total coverage of the rendered output — any visible change whatsoever is caught.
- Near-zero authoring cost per test.
- Hash variant stores no binaries and pairs naturally with the repo's freeze discipline.

**Cons (decisive)**
- Zero tolerance + global blast radius: one insignificant color or stroke change breaks *every*
  golden test simultaneously.
- The failure is information-free: a mismatched digest says nothing about what changed or whether
  it matters; a human must eyeball images anyway.
- Golden-image variants add binary blobs and an image-diff dependency; tolerance-threshold
  variants trade brittleness for the opposite failure (real 1-pixel regressions passing under the
  threshold).
- Dead on arrival for GPU/wasm-WebGL backends (non-deterministic rasterization).

**Verdict: rejected.** Items 3 + 4 together provide the same regression net with localized,
readable failures. At most, a tolerance-metric image report could exist as CI-informational
output — never as a per-requirement gate.

---

## Item 6 — State-space coverage via named bindings

**What it is.** For components with multiple distinct visual states (an order mark: in-view,
clipped at price edge, clipped at time edge, overlapping another mark, degenerate viewport),
enumerate the states and bind each to its own named test — `[UID][state_name]` — using the
multi-binding mechanism `req/README.md` already supports. Borrowed from academic GUI "event
coverage" criteria.

**Pros**
- Turns "does it draw" into a measurable checklist; `partially_implemented` rollup status becomes
  genuinely meaningful for GUI leaves (3 of 5 states covered = visible in the report).
- Zero tooling changes — the binding mechanism exists today.
- The enumeration itself is a requirements-quality improvement: writing the state list forces
  clipping/overlap behavior to be *specified*, which for order marks it currently is not.

**Cons**
- State explosion risk: combinatorial states (clipped AND overlapping AND ...) can balloon; the
  list needs judgment about which combinations are contractually meaningful.
- More granular bindings mean more review ceremony: each named binding is separately frozen and
  re-reviewed.
- Premature today: no real order-marker scratcher exists yet to apply it to.

**Effort.** Low mechanically; the intellectual work is the state enumeration.

**Verdict: adopt when the first order-marker scratcher lands.** Define the convention then, not
speculatively now.

---

## Item 7 — AI judge, Stage 1: verification prompt inside the test routine

**What it is.** For requirements that resist geometric formalization ("visually distinguishable,"
"not occluded," "legible"), a pytest test tagged `@pytest.mark.req("UID", "visual")` renders a
fixture to PNG, sends it with a written verification prompt to a pinned vision-capable model
(single structured API call, not an agent loop), validates the structured verdict
(pass/fail + per-check evidence), and passes or fails accordingly. The prompt text lives inside
the test routine, so the existing routine-hash freeze covers prompt edits automatically.

**Pros**
- Closes the only gap Items 1–4 cannot: executable verification of gestalt/semantic properties
  that previously existed solely as an implicit human glance at review time.
- Requires **zero req-system changes**: from the gate's perspective it is an ordinary pytest
  binding — discovered by tag, frozen by hash, reported via JSONL, joined by `req validate`.
- Tolerant of insignificant change by nature: a 2% color shift doesn't move a judgment of
  "distinct" or "legible" — this is precisely the flexibility whose absence killed Item 5.
- Graceful degradation is already built into the gate: no API key → test skips → binding rolls up
  `not_implemented` (the README explicitly makes coverage gaps non-failures).
- Cost is negligible (fractions of a cent per judged fixture; Batch API halves nightly runs).

**Cons**
- Stochastic oracle: published measurements show mainstream models answering the same screenshot
  question inconsistently across runs. Raw, unmitigated LLM judgment is **not** gate-grade —
  Items 8 and 9-style mitigations (grounding, calibration, majority voting) are mandatory
  companions, not options.
- A verdict is an attestation, not a reproducible proof: re-running later (new model version,
  provider change) may not reproduce it. Archived transcripts mitigate auditability, not
  reproducibility.
- New external dependencies in CI: network egress, an API-key secret, a paid third-party service,
  and a judge model that will eventually be deprecated (forcing recalibration + re-review).
- Latency: seconds per fixture vs. microseconds for native assertions — fine nightly, noticeable
  if put on every push.
- Cultural risk: "the AI approved it" can quietly erode the discipline if checks are admitted
  without calibration evidence. Requires an explicit rule: a check that can't name defect
  fixtures it catches is not a check.

**Effort.** Medium: the render-to-PNG fixture path, the structured-verdict harness, and the first
calibration set (Item 8) — most of which is one-time infrastructure.

**Verdict: adopt as a pilot** on 2–3 genuinely gestalt leaves, only together with Items 8 and the
grounding cross-check — never as free-form "does this look right?"

---

## Item 8 — Judge calibration via defect injection

**What it is.** Each AI-judged binding maintains a calibration fixture set: renders with
deliberately injected violations (marker shifted off its price, wrong side color, missing candle,
clipped label) plus known-good renders. `req review` of the binding runs the judge over the whole
set and permits the freeze only if it clears a stated bar (e.g. detects every defect class in
N-of-M runs, zero false alarms on clean renders). The calibration manifest is hashed, so changing
it — like changing the prompt or the pinned model — is a review-visible event.

**Pros**
- Converts "trust the model" into "trust the measurement": the oracle's error rate is empirically
  established before it may gate anything — the same way a physical measuring instrument is
  admitted.
- It is mutation testing applied to the oracle itself — a stronger reliability statement than most
  deterministic suites make about their own assertions.
- Defines the objective re-acceptance procedure for judge-model upgrades (re-run calibration),
  handled exactly like a ThorVG version bump.
- The defect fixtures double as documentation of what the requirement forbids.

**Cons**
- The largest cost inside the AI tier: authoring realistic defect renders per binding is real
  work, and a lazy defect set gives false confidence (the judge aces easy mutations while missing
  subtle real bugs).
- Calibration proves discrimination on *injected* defect classes only — silent about failure
  modes nobody thought to inject.
- Makes `req review` of agent bindings slower and API-dependent (a calibration run per freeze).
- Threshold governance (what bar, how many repetitions) is a policy decision that must be written
  down somewhere authoritative.

**Effort.** Medium–high per binding; this is where most of the AI-tier work lives.

**Verdict: mandatory companion to Item 7.** Without it, the AI tier should not exist.

---

## Item 9 — Grounded verdicts: cross-check the judge against the scene dump

**What it is.** The judge must report measurable observations with each check — the region it
found the marker in, how many candles it counted, the approximate color it read. The harness
mechanically compares those against the Item-3 scene dump (projected bounds, actual counts,
actual fills). A verdict whose stated evidence contradicts the scene data is rejected regardless
of its pass/fail claim (one retry, then failed as judge-inconsistent). The model is trusted only
for the residual gestalt judgment; every checkable fact is checked.

**Pros**
- The strongest known reliability lever for VLM oracles — mirrors the "symbolization" approach in
  current research (WebTestPilot, VISOR): anchor the model to structured data, don't trust raw
  image interpretation.
- Catches both hallucinated passes ("marker looks correct" when there is no marker) and
  hallucinated failures — precisely the errors that make naïve LLM judges untrustworthy.
- Nearly free once Item 3 exists: the ground-truth data is already generated per fixture.
- Also powers the human-side triage of Item 10.

**Cons**
- Couples the AI tier to the scene-dump serializer (Item 3 becomes a hard prerequisite).
- The region-matching logic (judge's rough pixel regions vs. projected exact bounds) needs its
  own tolerance policy, and overly strict matching would reject honest verdicts.
- Increases prompt/schema complexity: the judge must be instructed to produce evidence in a
  parseable form, which makes prompts longer and calibration (Item 8) more involved.

**Effort.** Low–medium once Item 3 exists.

**Verdict: mandatory companion to Item 7**, same status as Item 8.

---

## Item 10 — AI-assisted re-review triage (advisory, outside the gate)

**What it is.** When an intentional change touches many Item-3 snapshot baselines, an agent
summarizes the diffs for the human at `req review` time: "13 of 14 diffs are the expected
stroke-width change; baseline 9 also moved a label 3 px, which the change does not explain."
Purely advice to the reviewer — it produces no coverage records and gates nothing; the human
stamp remains the only thing that freezes state.

**Pros**
- Directly attacks the worst practical failure mode of snapshot testing (rubber-stamping bulk
  diffs) — the operational pain that motivated rejecting Item 5 in the first place.
- Zero risk to the formal model: it sits entirely outside the gate, so none of Item 7's
  stochasticity concerns apply.
- Cheap, and adoptable immediately once snapshots exist — no calibration machinery needed.

**Cons**
- Can create false comfort: a reviewer who habitually trusts the triage summary has outsourced
  diligence informally — without even Item 8's measured error rate.
- Another integration to maintain (API access at review time, a triage prompt to keep current).
- Value is proportional to baseline churn; if snapshot diffs turn out to be rare, it's machinery
  with little use.

**Effort.** Low.

**Verdict: adopt opportunistically** after Item 3 — useful, safe, and cancelable at any time.

---

## Item 11 — AI judge, Stage 2: `agent:` key in the requirement item schema

**What it is.** Promote the verification prompt out of the test routine (Item 7) into the
requirement item file itself, as a first-class `agent:` binding alongside `tests:` — carrying the
prompt, the pinned judge model, and the calibration-manifest hash. Requires extending the
canonical-JSON review stamp (`reqlib.compute_stamp`) to cover the new key, and a generic pytest
harness that discovers and executes `agent:` bindings from items. The only req-system change
proposed anywhere in this report.

**Pros**
- Puts the acceptance criterion where it conceptually belongs: next to the "shall" sentence, as
  part of the requirement's reviewed identity — the prompt *is* requirement content, and editing
  it invalidates `reviewed:` exactly like editing the description.
- One generic harness replaces N copy-pasted judge tests; adding a judged requirement becomes a
  YAML edit, not new test code.
- Makes AI-verified coverage visible as its own binding type in reports and the traceability
  matrix.

**Cons**
- The only item that touches `reqlib.py`/`req.py` and the stamp format — schema migration,
  tooling-test updates (the req system's own INFRA leaves), and a migration for existing stamps.
- Premature before Stage 1 proves the concept: if the pilot shows prompts need heavy per-fixture
  logic around them, a declarative YAML prompt slot is the wrong abstraction and would be undone.
- Splits verification logic across two places (item YAML + generic harness), slightly weakening
  the current "everything about a binding is in the tagged routine" simplicity.

**Effort.** Medium, concentrated in the requirements tooling.

**Verdict: defer.** Decide only after the Item-7 pilot demonstrates stable calibration on real
leaves.

---

## Item 12 — WASM-forward oracle placement (a constraint, not a work item)

**What it is.** The graphics stack is designed with a WebAssembly deployment in mind, and ThorVG
upstream officially supports wasm (Emscripten target; thorvg.web with WebGL/WebGPU). The
constraint adopted throughout this report: anchor every gate-grade oracle at the scene layer
(Layer 2), which compiles and behaves identically under native and wasm, and never at exact
raster output, which bifurcates (SwCanvas stays deterministic under wasm; WebGL/WebGPU backends
are GPU-dependent and not pixel-deterministic).

**Pros**
- Items 1, 2, 3 survive the native→wasm transition with zero rework; the coverage JSONL channel
  is transport-agnostic (a wasm/browser test lane can append the same records).
- Avoids ever building CI assets (golden images, pixel thresholds) that a backend change would
  invalidate wholesale.
- When a browser HUD exists, Playwright-class tooling slots in for *driving interaction* (pan,
  zoom, mouse-pick) — complementing, not replacing, the scene-layer oracles.

**Cons**
- Item 4's raster probes are knowingly SwCanvas-only; a future GPU backend gets no probe coverage
  by this strategy and would need its own (likely Item-7-style, tolerance-based) approach.
- Until a wasm lane actually exists, the constraint is speculative: it costs design freedom now
  for a target that may shift.

**Effort.** None now — it is a placement rule for the other items.

**Verdict: adopt as a standing constraint.**

---

## Summary matrix

| # | Item | Oracle strength | Change tolerance | Determinism | Req-system changes | Effort | Verdict |
|---|------|-----------------|------------------|-------------|--------------------|--------|---------|
| 1 | Scene-graph assertions | Strong, semantic | Localized breakage | Full | None | Medium | **Adopt first** |
| 2 | Property/invariant tests | Strong, universal | N/A (math) | Full (seeded) | None | Low–med | **Adopt (parallel)** |
| 3 | Scene-dump snapshots | Broad, "same as before" | Proportional, readable diffs | Full | None | Low | **Adopt third** |
| 4 | Semantic raster probes | Narrow, structural | Robust (paintedness only) | Full (SwCanvas) | None | Low (keep few) | **Adopt sparingly** |
| 5 | Whole-image compare / hash | Broad but blind | **None — global breakage** | Full but fragile | None | Low | **Rejected** |
| 6 | State-space named bindings | Coverage structure | N/A | N/A | None | Low | **Adopt when marker exists** |
| 7 | AI judge (prompt in test) | Semantic/gestalt | High by nature | **Stochastic** | None | Medium | **Pilot, with 8+9** |
| 8 | Defect-injection calibration | Oracle QA | N/A | Measured | None | Med–high | **Mandatory with 7** |
| 9 | Scene-grounded verdicts | Reliability lever | N/A | Partial | None | Low–med | **Mandatory with 7** |
| 10 | AI re-review triage | Advisory only | N/A | N/A | None | Low | **Adopt opportunistically** |
| 11 | `agent:` item schema | Same as 7, first-class | Same as 7 | Same as 7 | **Yes — stamp ext.** | Medium | **Defer until 7 proves out** |
| 12 | WASM-forward placement | Constraint | N/A | N/A | None | None | **Standing constraint** |

## Recommended sequence

1. Item 1 (harness + scene assertions for `QuoteScratcher` + one ruler) — with Item 12 as the
   standing placement rule.
2. Item 2 in parallel.
3. Item 3 (serializer + snapshots), then a few Item-4 probes.
4. Item 10 as soon as snapshot churn justifies it.
5. Item 7 pilot on 2–3 gestalt leaves, built with Items 8 + 9 from day one.
6. Item 6 when the first order-marker scratcher lands.
7. Item 11 decision after the pilot; only then any `reqlib.py` change.
