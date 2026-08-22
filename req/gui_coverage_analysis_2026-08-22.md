# Extending Requirements Coverage to Graphical UI — Analysis & Recommendations

Status: **analysis only, no code or `req/` tree changes included**. This document surveys how the
current requirements system works, what "GUI coverage" would have to mean for it to stay true to
its own discipline, what the wider industry does for analogous problems, and a concrete, phased
proposal sized to this codebase. It is written to be read once, argued over, and then either
promoted into `req/` items or discarded.

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
routing around.

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

## 3. What the field actually does, and why most of it doesn't transfer directly

**Web-world visual regression (Percy, Applitools, Chromatic, Playwright `toHaveScreenshot`)** —
screenshot a browser DOM, diff pixels or use AI-assisted diffing, gate CI on a similarity
threshold. This is the dominant 2026 practice for *web* UI, but two of its own practitioners'
caveats disqualify it as-is here: (1) it is explicitly reported as a poor fit for canvas/WebGL
content ("DOM snapshotting... the visual output is not reconstructable from the DOM" — Percy), and
(2) Open Trader's HUD is a native Elements/Cairo/ThorVG app with no browser or DOM at all, so the
entire tooling category (which drives a real or headless browser) is inapplicable by construction.
The one thing worth keeping from this world is the *operational* best practice: fixed device-pixel
ratio, disabled hardware acceleration, mocked dynamic values (prices/timestamps) before capture —
all directly portable to a native software-rasterized canvas. ([Percy: canvas/WebGL
limitation](https://percy.io/blog/visual-gui-testing), [visual regression best
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

**Accessibility as a second consumer of the same seam.** Canvas has no accessibility tree by
default; the standard remedy is a parallel semantic description (ARIA layer, or — per the
`html-in-canvas` "draw HTML labels while keeping the accessibility tree untouched" pattern — a
structured description kept in sync with the drawing). For Open Trader this is not a near-term ask,
but it is worth naming because *the same data a screen-reader semantic layer would need* (marker
price/time/side, in HUD coordinates, before rasterization) is *exactly* the Layer-2 scene-graph
data the testing seam above already wants to assert on. Building one well sets up the other for
free later. ([Chart.js accessibility](https://www.chartjs.org/docs/latest/general/accessibility.html),
[accessible charts checklist](https://www.a11y-collective.com/blog/accessible-charts/))

## 4. Recommendation: four coverage tiers, all still "bound to auto tests"

None of the below requires changing `req/README.md`'s mechanism. Every new test is still a Catch2
`TEST_CASE` or pytest function tagged `[UID]`/`@pytest.mark.req`, still self-reports to the same
JSONL coverage stream via the existing listener/hook, still gets hash-frozen on `req review`. What
changes is *what kind of assertion* a GUI-scoped leaf's bound routine is allowed to make. Proposed
tiers, in the order I'd land them:

### Tier 1 — Scene-graph / geometry assertions (highest value, lowest cost, do this first)

Drive a scratcher through its real lifecycle (`OnAttach` → `CalculateSize` → `OnLayout` /
`EmitChanges`) against a synthetic `InstrumentContentPanel`/fixture, then assert directly on the
resulting `tvg::Scene` tree — child count and order (z-order), each `tvg::Shape`'s path bounding
box, fill/stroke color, and `transform()` matrix (reusing `check_matrix_equals`-style helpers) —
with **no rasterization at all**. This is a straight scale-up of the existing
`test/render/test_thorvg.cpp` pattern from "ThorVG primitives" to "our scratchers' actual output,"
and it is the strongest oracle in the set because it tests the thing the project controls (its own
scene construction) rather than a third-party rasterizer's antialiasing. Natural home: a new
`test/cockpit/scratchers/render/` directory mirroring the existing `src/` mirror convention, one
file per scratcher.

Order-mark example: assert that for an order at price P within the current view, the emitted
marker `Shape`'s bounding-box Y matches `panel`'s price→HUD-Y projection (an analogue of the
already-shipped `HudXOfTime`, symmetrical on price) exactly, or within a stated ULP/rounding
tolerance if the mapping goes through float; assert markers for out-of-view prices are absent or
clipped per the documented contract, not merely "don't crash."

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

### Tier 3 — Deterministic golden-raster tests, hash-frozen like everything else

For the small remaining risk that Tier 1+2 miss (a real ThorVG/Cairo rasterization regression that
scene-graph assertions can't see), extend the *existing* tiny-canvas pattern
(`test_thorvg.cpp`'s 5×5/3×3 `SwCanvas` buffers with exact alpha checks) to small, synthetic,
representative scratcher outputs — a handful of candles, one or two order markers, not a
realistic full-size chart. Two things make this safe rather than the usual golden-image
minefield:

- **The rasterizer is already deterministic and pinned.** `SwCanvas` is software-only (no GPU
  driver variance), and ThorVG is CPM-fetched at a fixed tag (`GIT_TAG v1.0.4` in
  `cmake/ThorvgBuild.cmake`) — the two biggest sources of golden-image flakiness in the wider
  industry (GPU nondeterminism, unpinned rendering-library versions) are already absent here.
  Bumping the ThorVG tag should be treated the same way the project already treats a frozen-routine
  edit: expected to redden golden tests, requiring an explicit `req clear` + re-review, not a
  silent baseline overwrite.
- **Store a digest, not a PNG.** Rather than committing baseline image files (binary blobs are a
  poor fit for this repo's "everything is a reviewable text diff, hash-frozen" ethos), hash the raw
  pixel buffer (sha256) and assert against a literal digest in the test source — a direct extension
  of the existing `tests:`/`reviewed:` hash-freeze idea to *expected render output*, not just
  *test-routine source*. On mismatch, dump the actual buffer to a PNG under a build-output path (not
  committed) so a human can inspect the diff before deciding whether to accept a new digest via the
  same `req review` ritual that already governs routine changes. This reuses machinery the project
  already trusts instead of importing an image-diff dependency.

Font/text rendering is the one open determinism question worth resolving before Tier 3 touches
anything with a label (`TimeRuler`, `PriceRuler`): confirm `panel.DefaultFontName()` resolves to a
font file bundled with the build rather than a system font, or golden digests will differ by OS/CI
image. If bundling isn't already the case, either bundle a fixed font for golden tests specifically,
or keep label regions out of the hashed buffer and assert only their geometry (position/box) at
Tier 1 instead — cheaper and arguably a better oracle anyway per §3's "pixel comparison is a weak
oracle" point above.

### Tier 4 — Marker/order state-space coverage as a named leaf convention

Borrow the "event coverage" framing directly: rather than one leaf per scratcher, enumerate the
*distinct visual states* a component like an order marker can be in (in-view, price-scale-clipped,
time-scale-clipped, coincident with another order, at a zero-width/degenerate viewport) and bind
each to its own `[UID][state_name]` binding — the multi-binding mechanism `req/README.md` already
supports natively. This gives the status rollup (`partially_implemented`) real meaning for GUI
leaves instead of one boolean "does it draw."

## 5. The one thing that can't be automated, and how to say so honestly

Not every GUI concern reduces to an assertion: "does this palette read well," "is this chart
pleasant to look at" are human-judgment questions, and the current req system's hard rule — a
leaf is only ever verified by something that executes and self-reports — has no channel for that
kind of evidence by design. Two honest options, not a third that pretends otherwise:

1. **Don't put pure-aesthetic concerns in `req/` as leaves at all.** Keep them as design notes in
   the component READMEs (as `scratchers/README.md` already does for the coordinate contract), and
   let `req review`'s existing "user-only" gate double as the human sign-off it already
   functionally is — a reviewer approving a rendering leaf is *implicitly* also eyeballing the
   result, the same way they're implicitly reading the diff today.
2. **If a leaf must exist for a subjective property, bind it to a test that verifies the
   measurable proxy, not the subjective claim** — e.g. not "buoy candles look clear" but "candle
   fill/stroke maintain ≥3:1 luminance contrast against the panel background across the documented
   theme palette," which is a Tier-1-style assertion, not a screenshot a human has to eyeball.

Either way, resist adding a "manually verified" status to the coverage model — that would quietly
undo the property the user explicitly asked to preserve.

## 6. Suggested rollout order

1. Land Tier 1 (scene-graph assertions) for `QuoteScratcher` and one ruler, closing the largest
   part of the gap `INFRA-030` already names, at the lowest implementation risk.
2. Land Tier 2 (coordinate-pipeline invariants) — independent of Tier 1, can proceed in parallel,
   directly strengthens the "formal" claim.
3. Resolve the font-bundling question, then land Tier 3 for the same two components as a pilot,
   including the ThorVG-version-bump re-review convention.
4. Introduce Tier 4's multi-binding convention once a real order-marker scratcher exists to apply
   it to.
5. Only then decide whether any new `req/hud/*` leaves are needed, or whether the above simply
   attaches new bindings to leaves that already exist (`req/hud/market_data/*`,
   `req/engine/buoy/geometry/*`) — most of the coordinate/geometry work looks like it fills gaps in
   existing items rather than requiring new ones.

## Sources consulted

- [Percy — What is Visual GUI Testing](https://percy.io/blog/visual-gui-testing) (canvas/WebGL limitation)
- [UI Visual Regression Testing Best Practices Playbook](https://medium.com/@ss-tech/the-ui-visual-regression-testing-best-practices-playbook-dc27db61ebe0)
- [Canvas Visual Testing with Retries](https://glebbahmutov.com/blog/canvas-testing/)
- [Xie & Memon — Designing and Comparing Automated Test Oracles for GUI-based Software Applications](http://www.cs.umd.edu/~atif/pubs/XieMemonTOSEM2006.pdf)
- [Memon — Coverage Criteria for GUI Testing](http://www.cs.umd.edu/~atif/pubs/MemonFSE2001.pdf)
- [Doorstop — Text-Based Requirements Management Using Version Control](https://doorstop.readthedocs.io/)
- [jamb — IEC 62304 requirements traceability for pytest](https://github.com/vanandrew/jamb)
- [DO-178C requirements traceability guide](https://www.jamasoftware.com/requirements-management-guide/aerospace-and-defense/do-178c/)
- [Parasoft — Requirements Traceability for ISO 26262](https://www.parasoft.com/learning-center/iso-26262/requirements-traceability/)
- [Foundry — Invariant Testing](https://www.getfoundry.sh/guides/invariant-testing)
- [Property-Based Testing vs. Snapshot Testing](https://teachmeidea.com/snapshot-testing-benefits-pitfalls-when-to-use/)
- [Qt Quick Scene Graph](https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html)
- [Chart.js — Accessibility](https://www.chartjs.org/docs/latest/general/accessibility.html)
- [The A11Y Collective — Accessible Charts Checklist](https://www.a11y-collective.com/blog/accessible-charts/)
- [html-in-canvas — Accessible Charts demo](https://html-in-canvas.dev/demos/accessible-charts/)

## In-repo evidence this analysis is grounded on

- `req/README.md` — tree model, test-binding mechanics, coverage JSONL, CLI, status rollup
- `req/infra/INFRA-030.yml` and its parent branch — names "render snapshot regression" as a known gap
- `CONTRIBUTING.md` §"Requirements & the TDD gate"
- `src/cockpit/scratchers/README.md` — 4-layer coordinate/transform pipeline, precision worked examples, prototype-vs-target gaps
- `test/render/test_thorvg.cpp` — existing scene-graph matrix assertions and tiny-canvas exact-alpha pattern (the precedent Tiers 1 and 3 scale up)
- `test/cockpit/scratchers/test_quote_scratcher.cpp` — current Layer-1-only coverage of `QuoteScratcher`
- `test/req_coverage_listener.cpp`, `scripts/tests/conftest.py` — coverage self-reporting, unaffected by any of the above
- `cmake/ThorvgBuild.cmake` (`GIT_TAG v1.0.4`) — confirms the rasterizer version is already pinned
- `src/cockpit/scratchers/time_ruler.cpp` — confirms label text goes through `tvg::Text`/`panel.DefaultFontName()`, the open font-determinism question for Tier 3
