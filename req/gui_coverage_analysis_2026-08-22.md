# Extending Requirements Coverage to Graphical UI — Analysis & Recommendations

Status: **analysis only, no code or `req/` tree changes included**. This document surveys how the
current requirements system works, what "GUI coverage" would have to mean for it to stay true to
its own discipline, what the wider industry does for analogous problems, and a concrete, phased
proposal sized to this codebase. It is written to be read once, argued over, and then either
promoted into `req/` items or discarded.

Revision 2 (same date). Changes after maintainer feedback: (a) pixel-buffer hash comparison is
**dropped** as a verification mechanism — its zero tolerance and global blast radius were judged
unacceptable; Tier 3 is rebuilt around scene-dump snapshots and semantic raster probes instead;
(b) the analysis now accounts for the planned WebAssembly path of the graphics stack rather than
treating the pipeline as native-only; (c) a new Tier 5 analyzes prompt-driven AI-agent
verification — requirement items carrying review/verification prompts, executed by vision-capable
model judges — as a way to close the remaining semantic-verification gap.

## 1. What we already have, precisely

The requirements tree (`req/`, see `req/README.md`) is a self-built, Doorstop-descended,
git-native system: every `*.yml` is one item, UID = filename, DAG shape lives in `parents:`, and a
leaf is "done" only when a `tests:` binding names a routine that (a) exists, (b) is discovered by
tag, and (c) has an execution record. Two mechanisms make this "strict":

- **Hash-freezing.** Once a user runs `req review <UID>`, the bound routine's source span is
  hashed into `tests:` and the item's whole canonical JSON is hashed into `reviewed:`. Any edit to
  the frozen routine reddens the gate until a human explicitly `req clear`s and re-reviews.
- **Coverage is *execution-derived*, not declared.** A Catch2 `TEST_CASE(..., "[UID]")` or a
  pytest `@pytest.mark.req("UID")` is discovered by scanning source, and only counts once it has
  actually *run* and self-reported to a JSONL file (`test/req_coverage_listener.cpp` for Catch2,
  `scripts/tests/conftest.py` for pytest). `req validate --coverage ...` joins declared bindings
  against executed records in both directions.

This is the mechanism the user's framing ("completely bound to auto tests") describes exactly:
**a requirement is only ever "verified" by something that runs as a Catch2 or pytest process and
emits a JSONL line.** There is currently no other channel — no manual-review evidence, no
screenshot artifact, no exploratory-test record feeds the gate. That is a *design choice*, not an
oversight (`req/README.md`'s whole point is "no settings files, no external state, everything
inside the item + the tagged test"), and it is the property most worth preserving rather than
routing around. Every tier proposed below — including the AI-agent tier — keeps that invariant:
verification evidence enters the gate only through an executing test routine's JSONL record.

The gap the user is pointing at is not hypothetical to the project — it is **already named**:
`req/infra/INFRA-030.yml`'s branch explicitly calls out "render snapshot regression" as
unimplemented verification infrastructure. This analysis is about how to build that infrastructure
without breaking the "verified only by something that runs and self-reports" invariant.

## 2. What's actually being asked: the shape of the target surface

`README.md` + `src/cockpit/scratchers/README.md` describe a 4-layer rendering pipeline:

```
Layer 1  business domain    currency<uint64_t> price, uint64_t ms time      (already covered)
Layer 2  ThorVG vector scene   float, retained-mode Scene/Shape tree, tvg::Matrix
Layer 3  pixel raster canvas   SwCanvas (software, deterministic, CPU-only)
Layer 4  host widget pixels    Cairo blit via Elements' PixelBufferElement
```

Everything currently tagged `[UID]` and reviewed lives at Layer 1: `QuoteScratcher`'s candle
bucketing (`test/cockpit/scratchers/test_quote_scratcher.cpp`) is exercised through a
clock-injection seam (`IngestTradesAt`) that never touches ThorVG. `test/render/test_thorvg.cpp`
exercises ThorVG's Layers 2–3 directly, but only as *library-level* smoke tests of ThorVG/Cairo
primitives, not of the project's own scratchers.

So "quote graph drawing" and "custom order marks on the graph" both live in the untested seam:
**Layer 1 output → the scratcher's `OnLayout`/`EmitChanges` → Layer 2 scene mutation → Layer 3
raster → (optionally) Layer 4 blit.** Coverage needs to reach that seam without becoming a
pixel-matching exercise that fights antialiasing, font hinting, or float rounding noise — which is
exactly the trap the wider industry has fallen into and mostly climbed back out of.

### 2.1 The WebAssembly consideration

The stack is native today, but is designed with a WebAssembly deployment path in mind. That is
realistic — ThorVG upstream officially supports it: an Emscripten build target producing
`thorvg-wasm.js`, plus the maintained [thorvg.web](https://github.com/thorvg/thorvg.web) package
targeting WebCanvas via WebGL/WebGPU. Three consequences for verification strategy:

- **Anchor oracles at Layer 2, not Layers 3–4.** The scene-graph and coordinate-pipeline code is
  plain C++ that compiles under Emscripten unchanged; assertions against the `tvg::Scene` tree and
  transform matrices are valid in a native binary, a wasm module under Node, or a browser. Layer 3
  bifurcates in a wasm future: the software rasterizer (`SwCanvas`) stays deterministic under wasm
  (the WASM spec fixes IEEE-754 float semantics), but thorvg.web's WebGL/WebGPU backends are
  GPU-dependent and **not** pixel-deterministic across machines. Any oracle tied to exact raster
  output would die at that transition; Layer-2 oracles survive it untouched. This independently
  reinforces the decision (Tier 3 below) not to build verification on raster comparison.
- **The coverage channel is transport-agnostic.** The JSONL contract is "append one JSON line per
  executed binding." A wasm test harness (Catch2 under Emscripten/Node, or a browser-driven run)
  can emit the same records through a trivial relay — the req system needs no changes to accept
  coverage from a future wasm CI lane running the *same tagged routines*.
- **Browser E2E tooling becomes applicable then — for interaction, not for pixels.** Once a
  browser-hosted HUD exists, Playwright-class tooling is the natural driver for input/interaction
  flows (pan, zoom, mouse-pick) against the wasm build. Its screenshot-diffing facilities remain
  the wrong oracle for the reasons above and in §3; its event-driving and instrumentation
  facilities are the useful part.

## 3. What the field actually does, and why most of it doesn't transfer directly

**Web-world visual regression (Percy, Applitools, Chromatic, Playwright `toHaveScreenshot`)** —
screenshot a browser DOM, diff pixels or use AI-assisted diffing, gate CI on a similarity
threshold. This is the dominant 2026 practice for *web* UI, but its own practitioners report it as
a poor fit for canvas/WebGL content ("the visual output is not reconstructable from the DOM" —
Percy), and today's HUD is a native Elements/Cairo/ThorVG app with no DOM, so the tooling category
doesn't apply now; under the future wasm/canvas target (§2.1) the *drivers* become usable but the
pixel-diff oracle stays weak. What is worth keeping from this world is the *operational* best
practice: fixed device-pixel ratio, no hardware-accelerated rendering in CI, mocked dynamic values
(prices/timestamps) before capture — all portable to a software-rasterized canvas. ([Percy:
canvas/WebGL limitation](https://percy.io/blog/visual-gui-testing), [visual regression best
practices](https://medium.com/@ss-tech/the-ui-visual-regression-testing-best-practices-playbook-dc27db61ebe0))

**The GUI test-oracle problem.** Academic GUI-testing literature (Xie & Memon's oracle-comparison
work, and the general "test oracle problem" survey) converges on the same point industry rediscovered
with canvas: pixel/state comparison is a *weak* oracle because "a visually correct result might
hide a semantically wrong one, or the reverse." The stronger oracle asserts *invariants* and
*widget-level properties* — the equivalent, here, of asserting scene-graph shape/geometry rather
than rasterized pixels. ([Designing and Comparing Automated Test Oracles for GUI-based
Applications](http://www.cs.umd.edu/~atif/pubs/XieMemonTOSEM2006.pdf))

**Declarative/retained-mode scene graphs as a testing seam.** Because a scene graph is a
first-class data structure synthesized *before* rasterization (Qt Quick's scene graph docs put it
plainly: the complete set of primitives is known before rendering starts), it is directly
inspectable and assertable without ever rasterizing — this is precisely the seam ThorVG's
`Scene`/`Shape` tree offers at Layer 2. Open Trader's own `test/render/test_thorvg.cpp` already
does this in miniature: `check_matrix_equals()` asserts a `tvg::Matrix` transform directly, with no
pixel involved. That is not a new idea to introduce — it is an existing, working pattern the
project invented independently and simply hasn't scaled up to the scratchers themselves yet.

**Requirements-as-code / traceability tooling (Doorstop, `jamb` for IEC 62304, MATLAB
Requirements Toolbox for ISO 26262/DO-178C).** Every one of these tools, at the mechanism level, is
doing what `req/` already does: version-controlled requirement items, typed links, and a coverage
join against test execution. `jamb` in particular pytest-markers tests with requirement IDs almost
identically to `req/`'s own `@pytest.mark.req(...)`. The takeaway is *validating*, not
prescriptive: Open Trader's home-grown system is already at (or ahead of) the state of the art for
this class of tool, including the DO-178C-grade discipline of bidirectional traceability. What
these standards add on top, though, is instructive — DO-178C's model-based-development supplement
notes that *model coverage does not eliminate the need for traceability to the model itself*,
i.e. exercising the scene graph is necessary but not sufficient; you still need explicit
requirement-to-scene-assertion traceability, not just "the code path executed." ([Doorstop
docs](https://doorstop.readthedocs.io/), [jamb — IEC 62304 traceability for
pytest](https://github.com/vanandrew/jamb), [DO-178C requirements
traceability](https://www.jamasoftware.com/requirements-management-guide/aerospace-and-defense/do-178c/))

**Property-based / invariant testing.** Widely used for numeric and financial-adjacent invariants
(Foundry's `invariant testing` for smart contracts is the sharpest modern example: generate random
call sequences, assert properties hold after every one, rather than hand-writing fixed cases).
Applied to *chart rendering math* rather than trading logic, the same idea generates random
(price, time) domains and viewport states and asserts geometric properties: monotonicity of the
price→pixel mapping, round-trip accuracy of `HudXOfTime`/`TimeOfHudX` within the documented
error bound, and correct re-flooring once `t_max - floor > 2^24 ms`. This is a close match for
work *already specified in prose* in `src/cockpit/scratchers/README.md` §3 (the float-precision
worked examples) — that section is, in effect, an un-executed property specification waiting for a
generator. ([Foundry invariant
testing](https://www.getfoundry.sh/guides/invariant-testing), [property-based testing vs. snapshot
testing](https://teachmeidea.com/snapshot-testing-benefits-pitfalls-when-to-use/))

**GUI coverage criteria research (Memon et al.).** Formal coverage criteria for GUIs are event- and
interaction-based (event coverage, event-interaction coverage, event-flow graphs), not pixel-based.
For Open Trader this reframes "order mark drawing coverage" away from "does it look right" and
toward "every distinct marker *state* (new order, filled, cancelled, at/near price-scale edge,
overlapping another marker, off-screen-clipped) has an asserted geometric outcome" — a coverage
notion the existing `tests:`/binding-name mechanism (`[UID][binding_name]`) already expresses
structurally, it just needs the state space enumerated. ([Coverage Criteria for GUI
Testing](http://www.cs.umd.edu/~atif/pubs/MemonFSE2001.pdf))

**Vision-language models as test oracles.** A fast-moving 2025–2026 research thread applies
vision-capable LLMs to exactly the gap this document targets: verifying GUI state against
natural-language requirements. The honest summary of that literature is "promising, but naïve use
is unreliable": an empirical study behind
[WebTestPilot](https://arxiv.org/html/2602.11724v2) found five mainstream LLMs asked the same
screenshot question ten times each produced seven distinct answers — stochasticity is the core
obstacle. The approaches that work pair the model with *symbolic grounding*: WebTestPilot
symbolizes GUI elements so oracles are inferred against structured data rather than raw pixels;
[VISOR](https://arxiv.org/html/2605.10408v2) builds a VLM-based oracle for robot GUIs with explicit
reliability evaluation; [VisionDroid](https://arxiv.org/html/2407.03037v1) uses multimodal LLMs to
detect non-crash functional GUI bugs. The design lesson for Tier 5 below: **an AI judge is usable
as a verification oracle only when its verdicts are (a) structurally constrained, (b) grounded
against machine-checkable scene data, and (c) calibrated against known-defect fixtures** — not as a
free-form "does this look right?" question.

**Accessibility as a second consumer of the same seam.** Canvas has no accessibility tree by
default; the standard remedy is a parallel semantic description kept in sync with the drawing. For
Open Trader this is not a near-term ask, but *the same data a screen-reader semantic layer would
need* (marker price/time/side, in HUD coordinates, before rasterization) is exactly the Layer-2
scene-graph data the testing seam above already wants to assert on — and, notably, the same data
the Tier-5 AI judge needs for grounding. Building the scene-serialization layer once serves all
three consumers. ([Chart.js accessibility](https://www.chartjs.org/docs/latest/general/accessibility.html),
[accessible charts checklist](https://www.a11y-collective.com/blog/accessible-charts/))

## 4. Recommendation: five coverage tiers, all still "bound to auto tests"

None of the below requires changing `req/README.md`'s core mechanism. Every new test is still a
Catch2 `TEST_CASE` or pytest function tagged `[UID]`/`@pytest.mark.req`, still self-reports to the
same JSONL coverage stream via the existing listener/hook, still gets hash-frozen on `req review`.
What changes is *what kind of assertion* a GUI-scoped leaf's bound routine is allowed to make.
Proposed tiers, in the order I'd land them:

### Tier 1 — Scene-graph / geometry assertions (highest value, lowest cost, do this first)

Drive a scratcher through its real lifecycle (`OnAttach` → `CalculateSize` → `OnLayout` /
`EmitChanges`) against a synthetic `InstrumentContentPanel`/fixture, then assert directly on the
resulting `tvg::Scene` tree — child count and order (z-order), each `tvg::Shape`'s path bounding
box, fill/stroke color, and `transform()` matrix (reusing `check_matrix_equals`-style helpers) —
with **no rasterization at all**. This is a straight scale-up of the existing
`test/render/test_thorvg.cpp` pattern from "ThorVG primitives" to "our scratchers' actual output,"
and it is the strongest oracle in the set because it tests the thing the project controls (its own
scene construction) rather than a third-party rasterizer's antialiasing. It is also fully
wasm-portable (§2.1). Natural home: a new `test/cockpit/scratchers/render/` directory mirroring
the existing `src/` mirror convention, one file per scratcher.

Order-mark example: assert that for an order at price P within the current view, the emitted
marker `Shape`'s bounding-box Y matches `panel`'s price→HUD-Y projection (an analogue of the
already-shipped `HudXOfTime`, symmetrical on price) exactly, or within a stated ULP/rounding
tolerance if the mapping goes through float; assert markers for out-of-view prices are absent or
clipped per the documented contract, not merely "don't crash."

Crucially for maintainability: **fine-grained assertions localize breakage.** A deliberate color
change breaks only the tests that assert that color; a marker-geometry change breaks only that
marker's tests. This is the property whole-image comparison can never have, and it is the direct
answer to the brittleness concern that removed pixel hashing from this proposal.

### Tier 2 — Property/invariant tests for the coordinate pipeline (the "formal" upgrade)

`scratchers/README.md` §§2–3 already read like an un-executed spec: forward-only computation,
reverse-projection confined to Layer 2's matrix inverse, a scene floor re-derived whenever
`t_max − floor > 2^24 ms`, documented precision-loss bounds per magnitude regime. Turn each
documented invariant into a generated-input Catch2 test (Catch2 already has `GENERATE`; a small
seeded-PRNG generator utility, consistent with the repo's "no `double`, no ad-hoc scaling" numeric
rules, is enough — no need to pull in a third-party property-testing library and its own license
review). Concretely: generate random `(time_ms, price_points)` pairs and viewport windows, assert
`TimeOfHudX(HudXOfTime(t)) == t` within the documented tolerance, assert the floor invariant holds
after any window pan/zoom sequence, assert price/time-to-pixel is monotonic across the visible
range. This is the piece that most directly answers "preserve strict formal verification quality":
it moves float-precision claims that currently live only in a comment table into something CI
actually falsifies.

### Tier 3 — Scene-dump snapshots + semantic raster probes (revised: no image hashing)

*The previous revision proposed hash-frozen golden pixel buffers here. Rejected — a sha256 digest
has zero tolerance and global blast radius: one benign palette or stroke tweak breaks every golden
test at once, and the failing digest carries no information about what changed. What replaces it
keeps the regression-net value without those failure modes.*

**3a. Canonical scene serialization ("scene dump") snapshots.** Add a small serializer that walks
a `tvg::Scene` tree and emits a canonical, stable text form — node type, z-order, transform,
path-bounds (rounded to a stated precision), fill/stroke color, clip — as YAML/JSON. Snapshot
tests render a scratcher against fixture data and diff the dump against a committed baseline
file. Properties that pixel hashing lacked:

- **Diffs are readable and reviewable.** A color change shows up as a one-line field diff in git,
  not an opaque digest mismatch; the human reviewing a `req review` re-stamp sees *what* changed.
- **Blast radius is proportional to the change.** A palette tweak diffs the color fields of the
  affected baselines and nothing else; geometry baselines are untouched. Bulk-accepting an
  intentional sweep is a normal, inspectable git operation.
- **It fits the repo's ethos** — everything stays a version-controlled text artifact under the
  same review discipline; no binary blobs, no external image-diff dependency.
- **It is rasterizer-independent** — valid against native SwCanvas, wasm SwCanvas, or a future
  GPU backend alike, because it captures the scene *before* rasterization.

Precision policy matters: serialize floats rounded to a documented tolerance (e.g. 2 decimal
places of scene units) so ULP-level noise never diffs, and route anything that must be exact
(matrix scale factors derived from integer math) through Tier-1 assertions instead.

**3b. Semantic raster probes, not image comparison.** The residual risk — a correct scene that
rasterizes wrongly — is covered by extending the *existing* `test_thorvg.cpp` probe pattern:
render a small fixture scene on `SwCanvas` and assert *semantic pixel facts* ("the marker's
interior region contains painted pixels", "the region left of the axis is untouched", "the two
candles do not bleed into each other's columns") via targeted coverage/alpha checks at computed
locations — locations *derived from the same projection math the scene asserts*, not hardcoded.
These probes are robust to insignificant color changes by construction (they test paintedness and
containment, not exact RGBA), only break when the probed property actually breaks, and stay
deterministic on software rasterizers (ThorVG is pinned at `GIT_TAG v1.0.4` in
`cmake/ThorvgBuild.cmake`; a version bump is expected to be a reviewed event, not silent). Keep
these few and structural — they are a rasterization-integration safety net, not the primary
oracle. If a tolerance-based whole-image metric (SSIM/perceptual diff) is ever wanted, treat it as
an optional CI-informational report, never a per-requirement gate.

Font/text rendering remains the one determinism caveat: label output goes through
`tvg::Text`/`panel.DefaultFontName()` (`time_ruler.cpp`), so raster probes should avoid text
regions unless a fixed font ships with the test fixtures; label *geometry* (position, reserved
box) is asserted at Tier 1/3a where fonts don't matter.

### Tier 4 — Marker/order state-space coverage as a named leaf convention

Borrow the "event coverage" framing directly: rather than one leaf per scratcher, enumerate the
*distinct visual states* a component like an order marker can be in (in-view, price-scale-clipped,
time-scale-clipped, coincident with another order, at a zero-width/degenerate viewport) and bind
each to its own `[UID][state_name]` binding — the multi-binding mechanism `req/README.md` already
supports natively. This gives the status rollup (`partially_implemented`) real meaning for GUI
leaves instead of one boolean "does it draw."

### Tier 5 — Prompt-driven AI-agent verification (new)

The tiers above formalize everything *formalizable*. What remains is the class of requirement that
resists reduction to geometry — "candles shall be visually distinguishable at a glance," "order
marks shall not be occluded by adjacent chart elements," "the ruler labels shall be legible" —
plus the gestalt question every human reviewer implicitly answers when approving a rendering leaf:
*does the drawn result actually look like what the requirement means?* Today that judgment exists
only inside `req review` and leaves no recurring, executable trace. The proposal: make it
executable by attaching a **verification prompt** to the requirement and running it through a
vision-capable model judge on every CI cycle, engineered so its verdict is trustworthy enough to
sit in the gate.

**5a. Where the prompt lives — two stages.**

*Stage 1 (zero req-system changes):* the prompt lives inside the bound test routine. A pytest test
tagged `@pytest.mark.req("UID", "visual")` contains the verification prompt as a literal, renders
the fixture, calls the model, validates the verdict, and passes/fails. Because the prompt text sits
inside the routine's hashed source span, **the existing freeze mechanism already covers it**: any
prompt edit reddens the gate exactly like any test edit, and `req review` freezes prompt and
harness together. From the req system's viewpoint this is just another auto test — the coverage
listener, JSONL join, and status rollup need no changes at all.

*Stage 2 (schema evolution, once Stage 1 proves out):* promote the prompt into the item file
itself, where it conceptually belongs — it is the requirement's *acceptance criterion for a visual
judge*, a sibling of the `description`'s "shall" sentence. Sketch:

```yaml
header: Order mark rendering
description: |
  The market data scene shall draw an order mark at the order's price level ...
parents: [MARKET_DATA_SCENE]
tests:
  geometry: <sha>              # Tier-1 deterministic binding, as today
agent:
  visual:
    prompt: |
      The image shows a rendered quote chart. Verify: an order mark appears at the
      price level stated in the fixture manifest; it is visually distinct from the
      candles; it is not clipped or occluded. Report each check with the pixel
      region you based it on.
    model: claude-opus-5       # pinned judge model, part of the frozen identity
    calibration: <sha>         # hash of the defect-fixture manifest this prompt was calibrated on
```

This requires extending the canonical-JSON stamp (`reqlib.compute_stamp`) to cover the `agent:`
key, so prompt edits invalidate `reviewed:` exactly like description edits do — a small, contained
tooling change, and the only req-system change in this entire document. The harness side stays a
generic pytest runner that discovers `agent:` bindings from items, renders the named fixture,
submits, and emits a coverage record per binding — so agent verification remains
execution-derived coverage like everything else.

**5b. The harness — a single structured vision call, not an agentic loop.** The judge is one
request per fixture: verification prompt + the rendered PNG (vision input) + a standing rubric,
with the response **structurally constrained** to a verdict schema (the Claude API's structured
outputs — `client.messages.parse()` against a schema — guarantee a parseable
`{verdict, per_check: [{check, pass, evidence_region, observed}], defects: []}` rather than prose).
No tool loop, no browsing, no state: the simplest tier of LLM integration, deliberately. Render
determinism is already solved by the same fixture discipline Tiers 1–3 need (synthetic data, fixed
viewport, SwCanvas offscreen).

**5c. Grounded verdicts — the "verify the verifier" cross-check.** This is the piece that lifts
the design above LLM-judge folklore, and it reuses Tier 3a's scene dump as ground truth. The judge
is required to report, for each check, *measurable observations* — the region it found the marker
in, the count of candles it saw, the approximate color it read. The harness then mechanically
cross-checks those observations against the scene dump (marker bounds projected to pixels, actual
candle count, actual fill color): **a verdict whose stated evidence contradicts the scene data is
rejected regardless of its pass/fail claim** (and retried once, then failed as
`judge-inconsistent`). The model is never trusted on assertions the machine can check; it is
trusted only on the residual gestalt judgment ("distinct," "legible," "not occluded") — with its
factual grounding verified. This mirrors the symbolization insight from WebTestPilot and VISOR:
VLM oracles become reliable when anchored to structured GUI data rather than free-form image
interpretation.

**5d. Calibration — measured reliability instead of pretended determinism.** An LLM judge is
stochastic; the empirical literature is blunt about it (§3). The honest way to admit one into a
strict gate is the way any measuring instrument is admitted: calibrate it, and re-calibrate when
anything in it changes. Maintain a **defect-injection fixture set** per agent binding: renders
with deliberately introduced violations (marker shifted off its price, wrong side color, missing
candle, label clipped, marks swapped) plus known-good renders. `req review` of an agent binding
runs the judge over the full calibration set and only permits the freeze if discrimination clears
a stated bar (e.g. ≥ N-of-M detection of every injected defect class, zero false alarms on the
clean set across K repetitions). The calibration manifest is hashed into the item
(`calibration:` above), so changing the defect set — like changing the prompt or the pinned model
— is a review-visible event. In production runs, use self-consistency (majority of 3 samples) for
verdicts near the boundary. This is also, incidentally, mutation testing applied to the oracle
itself — a stronger reliability statement than most deterministic test suites ever make about
*their* assertions.

**5e. Gate integration and failure semantics.** Agent-bound tests run in the existing
`requirements`/pytest CI lane, needing only an API key secret and network access. When the key is
absent (forks, offline dev), the tests *skip* — and the existing rollup semantics already do the
right thing: an unexecuted binding rolls up as `not_implemented`, never as a failure
(`req/README.md`: coverage gaps are deliberately not item problems). Nightly or label-gated
scheduling keeps cost negligible; the Batch API (50% price) fits the nightly shape. Every verdict
record should carry the model ID and the full response as an archived CI artifact — the
audit-trail equivalent of a test log, and the DO-178C-style "traceable proof artifact" that makes
an attestation reviewable after the fact.

**5f. Cost and model choice, roughly.** A judged fixture is one image (a few hundred KB PNG of a
small canvas ≈ ~1–1.5K image tokens) plus ~1K tokens of prompt/rubric and a few hundred tokens of
structured verdict. At Claude Opus 5 rates ($5/$25 per MTok), a 3-vote judgment of one fixture
costs well under a cent; a nightly run over even a hundred agent bindings is pocket change
relative to CI compute. Pin the judge model ID per binding (it is part of the oracle's identity);
treat a model upgrade like a ThorVG bump — re-run calibration, re-review, re-freeze. A cheaper
model (Claude Haiku 4.5) can serve as a pre-screen where volume ever matters, escalating
non-clean verdicts to the pinned judge.

**5g. Risks, stated plainly.**

- *Non-reproducibility.* A verdict is an attestation-at-a-time (like a CI log), not a rerunnable
  proof. Mitigated by calibration, grounding, archived transcripts, and majority voting — and by
  never letting the agent tier be the *only* oracle on a leaf that has any formalizable content
  (Tier 1/2/3 bindings carry that part; see the two-binding pattern in the schema sketch).
- *Model retirement.* A pinned model eventually EOLs. That is a scheduled re-calibration + re-review,
  structurally identical to the dependency-pin bumps the project already handles.
- *Judge gaming / prompt injection.* Fixture data is synthetic and repo-controlled, and the
  judge's output is a schema-validated verdict consumed by code, never executed as instructions —
  the surface is small; keep it so by never feeding live exchange data into judged renders.
- *Drift toward vibes.* The discipline that prevents "the AI said it looks fine" from eroding
  rigor is exactly the grounding + calibration machinery of 5c/5d. If a proposed agent check
  cannot name defect fixtures it would catch, it is not a check — reject it at review.

**5h. A second, softer role: AI-assisted re-review.** Independent of gate verdicts, an agent is
immediately useful at `req review` time as a *triage assistant* for Tier-3a snapshot diffs: when
an intentional styling change touches many scene-dump baselines, an agent summarizing "all 14
diffs are the expected stroke-width change; baseline 9 also moved a label 3px, which is not
explained by the change" turns a bulk re-review from rubber-stamping into informed acceptance.
This costs nothing to adopt (it is advice to the human, not evidence in the gate) and directly
softens the operational pain that motivated dropping pixel hashing. The human stamp remains the
only thing that freezes state — unchanged.

## 5. The human role, restated

The `req review` user-only rule stays untouched through all five tiers. What Tier 5 changes is
*what the human reviews*: not every rendered frame on every CI run, but the prompt, the fixture
set, the calibration evidence, and the judge's measured discrimination — once per freeze. The
recurring per-commit judgment is delegated to a calibrated, grounded, pinned oracle whose verdicts
flow through the same executable-coverage channel as every other test. Do **not** add a
"manually verified" status to the coverage model: with Tier 5 available, even gestalt requirements
have an executable verification path, so the temptation to punch a manual hole in the gate can be
resisted on principle.

Pure-aesthetic concerns that even a calibrated judge shouldn't gate ("is this palette pleasant")
still belong outside `req/` — in component READMEs as design notes — or reformulated as measurable
proxies (contrast ratios, occlusion rules) at Tier 1.

## 6. Suggested rollout order

1. **Tier 1** (scene-graph assertions) for `QuoteScratcher` and one ruler — closes the largest
   part of the gap `INFRA-030` names, at the lowest risk, and its fixture/panel harness is the
   substrate every later tier reuses.
2. **Tier 2** (coordinate-pipeline invariants) — independent of Tier 1, can proceed in parallel;
   directly strengthens the "formal" claim.
3. **Tier 3a** (scene serializer + dump snapshots) — the serializer is small, and it is a triple
   investment: snapshot baselines now, Tier-5 grounding data later, accessibility layer eventually.
   Add **3b** raster probes for the same two components.
4. **Tier 4** (state-space multi-bindings) once a real order-marker scratcher exists.
5. **Tier 5 Stage 1** (prompt-in-test agent judge) as a pilot on 2–3 genuinely gestalt leaves —
   including building the first defect-injection calibration set, which is where most of the
   learning is. Only after the pilot demonstrates stable calibration, decide on **Stage 2** (the
   `agent:` item-schema extension and the `compute_stamp` change).
6. Revisit wasm implications (§2.1) when the wasm lane becomes concrete — the above requires no
   rework for it by design.

## Sources consulted

- [Percy — What is Visual GUI Testing](https://percy.io/blog/visual-gui-testing) (canvas/WebGL limitation)
- [UI Visual Regression Testing Best Practices Playbook](https://medium.com/@ss-tech/the-ui-visual-regression-testing-best-practices-playbook-dc27db61ebe0)
- [Canvas Visual Testing with Retries](https://glebbahmutov.com/blog/canvas-testing/)
- [Xie & Memon — Designing and Comparing Automated Test Oracles for GUI-based Software Applications](http://www.cs.umd.edu/~atif/pubs/XieMemonTOSEM2006.pdf)
- [Memon — Coverage Criteria for GUI Testing](http://www.cs.umd.edu/~atif/pubs/MemonFSE2001.pdf)
- [WebTestPilot — Agentic End-to-End Web Testing against Natural Language Specification by Inferring Oracles with Symbolized GUI Elements](https://arxiv.org/html/2602.11724v2)
- [VISOR — A Vision-Language Model-based Test Oracle for Testing Robots](https://arxiv.org/html/2605.10408v2)
- [VisionDroid — Vision-driven Automated Mobile GUI Testing via Multimodal Large Language Model](https://arxiv.org/html/2407.03037v1)
- [Smartesting — The Future of Software Testing: Vision-Language Models](https://www.smartesting.com/en/the-future-of-software-testing-harnessing-vision-language-models/)
- [Doorstop — Text-Based Requirements Management Using Version Control](https://doorstop.readthedocs.io/)
- [jamb — IEC 62304 requirements traceability for pytest](https://github.com/vanandrew/jamb)
- [DO-178C requirements traceability guide](https://www.jamasoftware.com/requirements-management-guide/aerospace-and-defense/do-178c/)
- [Parasoft — Requirements Traceability for ISO 26262](https://www.parasoft.com/learning-center/iso-26262/requirements-traceability/)
- [Foundry — Invariant Testing](https://www.getfoundry.sh/guides/invariant-testing)
- [Property-Based Testing vs. Snapshot Testing](https://teachmeidea.com/snapshot-testing-benefits-pitfalls-when-to-use/)
- [Qt Quick Scene Graph](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html)
- [thorvg.web — ThorVG web integration (WebGL/WebGPU, WASM)](https://github.com/thorvg/thorvg.web)
- [ThorVG wiki — Web Development (Emscripten build)](https://github.com/thorvg/thorvg/wiki/Web-Development)
- [Chart.js — Accessibility](https://www.chartjs.org/docs/latest/general/accessibility.html)
- [The A11Y Collective — Accessible Charts Checklist](https://www.a11y-collective.com/blog/accessible-charts/)

## In-repo evidence this analysis is grounded on

- `req/README.md` — tree model, test-binding mechanics, coverage JSONL, CLI, status rollup,
  "coverage gaps are not item problems" (the semantics Tier 5's key-absent skip relies on)
- `req/infra/INFRA-030.yml` and its parent branch — names "render snapshot regression" as a known gap
- `CONTRIBUTING.md` §"Requirements & the TDD gate"
- `src/cockpit/scratchers/README.md` — 4-layer coordinate/transform pipeline, precision worked examples, prototype-vs-target gaps
- `test/render/test_thorvg.cpp` — existing scene-graph matrix assertions and semantic pixel-probe pattern (the precedent Tiers 1 and 3b scale up)
- `test/cockpit/scratchers/test_quote_scratcher.cpp` — current Layer-1-only coverage of `QuoteScratcher`
- `test/req_coverage_listener.cpp`, `scripts/tests/conftest.py` — coverage self-reporting, unaffected by any of the above
- `scripts/reqlib.py` (`compute_stamp`) — the one function Tier 5 Stage 2 would extend
- `cmake/ThorvgBuild.cmake` (`GIT_TAG v1.0.4`) — confirms the rasterizer version is pinned
- `src/cockpit/scratchers/time_ruler.cpp` — label text goes through `tvg::Text`/`panel.DefaultFontName()`, the font-determinism caveat for Tier 3b raster probes
