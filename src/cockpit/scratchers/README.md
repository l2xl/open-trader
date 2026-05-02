# Scratchers Concept

The Trade Cockpit UI is built combining a dynamical set of content panels.
The content panels may use UI library components layout or a custom drawing canvas.

Scratcher is a class responsible for drawing on the content panel canvas. Multiple scratchers collaboratively draw on the canvas creating complex UI visualizations like a quote graph with visual indicators.
Everything on the canvas is controlled collaboratively by a number of scratchers. Examples are the quote graph candles, rulers, or even graph margins.

The scratcher interface contains two methods:
- `CalculateSize(InstrumentContentPanel&)` — called every time the size of the content panel changes; scratchers adjust pixel geometry (e.g. reserve space for axis labels).
- `EmitChanges(InstrumentContentPanel&)` — performs the actual emission of vector graphics objects into the panel's ThorVG scenes.

# Coordinate & Transform Pipeline

This section pins down the data types and transforms across every layer of the rendering stack so scratchers, the panel, and the host widget agree on units and precision. The current code is a restricted prototype: not every layer is fully wired yet, but every new piece must respect the contract defined here. Sections 6–7 list the prototype-vs-target gaps that are intentionally accepted.

## 1. Layer strata (forward path 1 → 4)

- **Layer 1a** — logical / business prices, orders, levels, indicator values.
  Backing type: `currency<uint64_t>` (fixed-point, `common/currency.hpp`).
  Owner: upstream data + cockpit business logic.
- **Layer 1b** — logical / business time.
  Backing type: `uint64_t` ms-since-epoch obtained from `std::chrono::utc_clock::time_point` via `get_timestamp()` (`common/timedef.hpp`).
  Owner: upstream data + cockpit business logic.
- **Layer 2** — ThorVG vector scene.
  Backing type: `float`, with a per-axis scene floor subtracted before the cast (see section 3); pan/zoom carried in `tvg::Matrix` attached to scenes.
  Owner: `InstrumentContentPanel` scene tree + `Scratcher::EmitChanges`.
- **Layer 3** — pixel canvas (ThorVG SwCanvas raster target).
  Backing type: `uint32_t` width/height/stride, `int` rectangles.
  Owner: `InstrumentContentPanel::SetTarget` / `OnSize`.
- **Layer 4** — host graphics library (Elements / Cairo blit, screen pixels).
  Backing type: `int` widget pixels.
  Owner: `PixelBufferElement`.

Notes on layer-1 typing:
- Prices/orders/indicators are *only* `currency<uint64_t>`. No `double` math, no `currency<float>`, no ad-hoc `int64_t` price scaling at this layer.
- Time is *only* `uint64_t` ms-since-epoch (UTC) at the boundary; in-memory the upstream model may keep `std::chrono::utc_clock::time_point` and convert via `get_timestamp()` exactly at the layer-1/2 cast.
- Wire-format JSON strings (`PublicTrade::price`, `::time`, …) sit at layer 0. They are the data pipeline's concern; scratchers never see strings, and the eventual replacement of those `std::string` fields with `currency<uint64_t>` / `uint64_t` is out of the current scope.

## 2. Direction rule

- **Forward computations always run 1 → 4.** Each scratcher takes business-domain values, projects them into layer-2 floats with the panel-supplied floor, and lets the panel's scene tree compose them down to layers 3 and 4. Layer 4 receives integer pixels only.
- **Reverse projections (4 → 1) are consolidated at layer 2 only**, via the inverse of the `tvg::Matrix` attached to the relevant scene. Pan/zoom feedback, mouse-pick → timestamp/price, keyboard scrolling, etc. all go through that single matrix inverse. Precision loss on the reverse path is acceptable — any input arriving from human pointer/keyboard is rough by definition.
- **Pan/zoom is delegated to the ThorVG layer.** The panel mutates the scene matrix; no scratcher reproduces the pan/zoom state in its own coordinates.

## 3. Float precision strategy (layer 1 → layer 2)

`float` has a 24-bit mantissa, so consecutive integers are representable exactly only up to `2^24 ≈ 1.68 × 10^7`. Beyond that the representable gap is `2^(k − 23)` for any value in `[2^k, 2^(k+1))`. Both ms-timestamps and integer price points routinely exceed that threshold:

Worked examples (magnitude, bit width, resulting float gap, verdict):

- ms-since-epoch (UTC, 2026): magnitude ~1.78e12, ~41 bits, gap ~131 s. Unusable.
- ms-since 2000-01-01: magnitude ~8.3e11, ~40 bits, gap ~65 s. Unusable for ms data.
- ms over a 1-week window: magnitude ~6e8, ~30 bits, gap ~128 ms. OK for second-grain candles.
- ms over a 1-day window: magnitude ~9e7, ~27 bits, gap ~16 ms. Fine for charts.
- ms over a 6-hour window: magnitude ~2e7, ~25 bits, gap ~4 ms. Fine.
- ms over a 1-hour window: magnitude up to ~4e6, ~22 bits, exact. Exact ms.
- BTC price x100 (cents) over a 95k..105k range: magnitude ~1e6, ~20 bits, exact. Exact cent.
- BTC price x1e6 (raw points), absolute: magnitude ~1e11, ~37 bits, gap ~16 384 ticks. Unusable absolute.
- Same, range-only over a 10 000 spread: magnitude ~1e10, ~34 bits, gap ~2 048 ticks. Unusable absolute.
- Same, range with floor at window-min: magnitude `~1e10 - p0`, kept under 24 bits, exact-ish. OK.

Therefore every cast `uint64_t → float` at the 1 → 2 boundary subtracts a **scene floor** first. The subtraction stays in integer math (`int64_t` to absorb either-direction differences) before the float cast:

```
scene_x = static_cast<float>(static_cast<int64_t>(t_model)    - static_cast<int64_t>(floor.time_ms));
scene_y = static_cast<float>(static_cast<int64_t>(price_pts)  - static_cast<int64_t>(floor.price_points));
```

A *static* floor that holds ms precision until 2050 or 2100 is not achievable in float32 — `t_max - floor` would have to fit in `2^24 ms ≈ 4 h 39 m`, which cannot span decades. The floor is therefore **dynamic** and refloored whenever `(t_max - floor) > 2^24 ms`.

Floor selection (owned by `InstrumentContentPanel`, configurable via `SetSceneFloor`):
- **`time_ms`** — defaulted in `Initialize()` to `(now_ms / candle_period_ms) * candle_period_ms`, i.e. the candle boundary at or before the panel's session start. The cockpit / data layer overrides via `SetSceneFloor` once the visible data window's left edge is known. The default keeps `t_max - floor` below `candle_period_ms + drift`, which for typical chart spans (hours to days) stays well within `2^24 ms` — exact ms precision.
- **`price_points`** — defaults to `0`, acceptable while charted instruments stay below `~10^7` price points (cents-precision majors). For higher-decimal instruments the cockpit calls `SetSceneFloor` with the data-window minimum rounded down to the instrument's tick base.

Reverse mouse-pick → model time/price uses `(scene_x_inv + floor.time_ms, scene_y_inv + floor.price_points)`, with the inverse computed via `tvg::Matrix` operations only.

## 4. Scene tree & transforms (layer 2 internals)

```
SwCanvas (layer 3, pixels — uint32_t target, int rect)
└─ mRootScene             T = I,           clip = rect(0, 0, canvas_w, canvas_h)
   ├─ mLogicalCanvasScene T = M_view · Y-flip-about(innerDataRect.height)
   │                      # M_view holds pan/zoom; Y-flip turns canvas pixels Y-up
   │   └─ mLogicalScene   T = M_logical = scale(px_per_unit_t, px_per_unit_p) ·
   │                                       translate(-sceneFloor.t0, -sceneFloor.p0)
   │                      # scratchers emit (timestamp_ms, price_points) in layer-1
   │                      # numeric domain; M_logical maps them onto the inner data rect
   └─ mUiScene            T = I    (Y-down pixel space, full canvas rect)
                          # rulers, ticks, labels, hover overlays
```

- `mLogicalScene` is the only scene that carries layer-1 business values. Scratchers drawing market data emit there; the panel composes the floor + scale + Y-flip via the matrix chain.
- `mLogicalCanvasScene` carries Y-up canvas pixels. `M_view` (pan/zoom) is a property of this scene — the single point at which reverse projections are inverted.
- `mUiScene` is plain Y-down canvas pixels. Rulers, axis lines, tick labels, mouse overlays, and static legends live here. Coordinates are integers (`PixelRect`) cast to floats at emission. This scene does **not** participate in pan/zoom and never sees layer-1 model values directly — rulers consume `t0..t1` / `p0..p1` window extents from the panel and emit pre-computed pixel positions.

## 5. Panel-side state

`InstrumentContentPanel` carries the cross-layer state required to keep transforms consistent:

- `mCanvasWidth`, `mCanvasHeight` (`int`) — layer-3 outer canvas size set by host on resize. `OuterCanvasRect()` exposes them as a `PixelRect`.
- `mInnerDataRect` (`PixelRect`, `int`) — layer-3 canvas area left after rulers reserve their strips during `CalculateSize`. Accessed via `MutableInnerDataRect()` (mutating, by rulers) and `InnerDataRect()` (read-only, by everyone else).
- `mSceneFloor` (`SceneFloor { uint64_t time_ms; uint64_t price_points; }`) — layer-1→2 boundary floor subtracted before the float cast. Configurable via `SetSceneFloor`.
- `mLogicalScene->transform()` (`tvg::Matrix`) — single source of truth for the layer-1→2 scale and the layer-2 inner-rect translation. `e11 = px_per_ms`, `e22 = px_per_point`, `e13 = inner.left`, `e23 = outer_h - inner.bottom`. There is no parallel `LogicalScale` field — readers that need a scale (rulers computing tick spacing, mouse-pick reverse projection, tests) take it directly from this matrix. The panel writes scale via `SeedDefaultLogicalSceneScale()` (during `Initialize`); external configuration goes through `mLogicalScene->transform(...)` directly.
- `mLogicalCanvasScene->transform()` — Y-flip about outer canvas height. `M_view` (pan/zoom) will compose into the same matrix once wired; reverse projections invert it there.

## 6. Two-phase pipeline (current contract)

`InstrumentContentPanel::OnSize(width, height)`:
1. `mCanvasWidth/Height` ← input; `mInnerDataRect` ← `(0, 0, w, h)` (full canvas; rulers will shrink it).
2. `ApplyOuterSceneTransforms()` — sets `mRootScene` clip and `mLogicalCanvasScene` Y-flip about the outer canvas height; `mUiScene` and `mRootScene` get identity. Pan/zoom would compose into `mLogicalCanvasScene` here once wired.
3. For each scratcher: `CalculateSize(panel)` — may shrink `mInnerDataRect` (rulers reserve outer strips for axis space). Order matters; the standard chain is `TimeRuler → PriceRuler → QuoteScratcher`.
4. `ApplyLogicalSceneTransform()` — composes `M_logical` from `mLogicalScale` + `mInnerDataRect` + outer canvas height and assigns it to `mLogicalScene`. Scratchers that have already produced floor-anchored model coordinates (via `panel.GetSceneFloor()`) end up positioned inside the inner data rect.
5. Clear children of `mUiScene` and `mLogicalScene`; call `EmitChanges` for each scratcher.

`InstrumentContentPanel::Render()` is invoked from `PixelBufferElement::draw()`; it issues `tvg::SwCanvas::update/draw/sync` and the resulting ARGB32 buffer is blitted into the host Cairo context. No data transformation happens during render — only rasterisation. Float→`int` rounding at the layer-3/4 boundary is owned exclusively by ThorVG / Cairo; scratchers never round to pixels by themselves.

## 7. Prototype-vs-target gap (intentional)

Wired now:
- `M_logical` on `mLogicalScene` is the single source of truth for layer-1→2 scale and layer-2 translation. `Initialize()` calls `SeedDefaultLogicalSceneScale()` to write `e11 = CandleWidth / candle_period_ms` and `e22 = 0` (price scale must be configured by the data layer once instrument extents are known); `OnSize` calls `ApplyLogicalSceneTransform` which preserves `e11`/`e22` and rewrites `e13`/`e23` from the inner data rect.
- `mSceneFloor` exposed via `GetSceneFloor` / `SetSceneFloor`. Default time floor anchors at the candle boundary at or before panel-session start; default price floor is `0`.
- `QuoteScratcher::EmitChanges` subtracts the floor in `int64_t` before the float cast.
- Outer canvas rect (`OuterCanvasRect()`, computed from `mCanvasWidth/Height`) and inner data rect (`MutableInnerDataRect`/`InnerDataRect`) are split.

Still gapped (intentional, prototype):
- `M_view` (pan/zoom) on `mLogicalCanvasScene` is not wired — currently identity. Reverse mouse-pick → model also still unimplemented; both belong to the same future iteration.
- Price scale (`mLogicalScene->transform().e22`) has no automatic data-extent derivation; the cockpit / data layer must rewrite the matrix once the visible price window is known.
- Ruler labels remain static "Price" / "Time" placeholders; tick generation driven by `(t0..t1, p0..p1)` is not yet implemented.
- `BuoyCandleData<uint64_t, uint64_t>` raw points have not been replaced by `currency<uint64_t>`; this waits on the upstream parser ditching `std::string`.
- `margin.{hpp,cpp}` and `price_indicator.hpp` still hold obsolete `IChartPanel` API and should be deleted in a follow-up.

Gaps above are accepted while the data and persistence layers below are not yet finalised. New work in this directory must:
- not introduce reverse-path logic outside layer 2,
- not bypass the scene floor when emitting into `mLogicalScene`,
- not duplicate the layer-2 scale into a parallel field — read it from `mLogicalScene->transform()`,
- keep outer-canvas vs inner-data-rect roles distinct,
- not use `double` or `currency<float>` for business-domain math.

# Layered Class Hierarchy

```
Scratcher (pure interface: CalculateSize / EmitChanges)
├── PriceRuler         — vertical price-axis geometry, axis line + label into UI scene
├── QuoteScratcher     — ingests PublicTrade stream, builds BuoyCandleQuotes, emits buoy shapes into the logical scene
└── TimeRuler          — horizontal time-axis geometry, axis line + label into UI scene
```

`InstrumentContentPanel` (`cockpit`) owns the ordered list of scratchers and drives the two-phase pipeline:
1. On resize → `CalculateSize` for every scratcher (geometry adjustments, pixel-space reservations)
2. On render → `EmitChanges` for every scratcher (re-emit vector objects into the appropriate scenes)

The standard scratcher registration order for a quote chart is: `TimeRuler` → `PriceRuler` → `QuoteScratcher`.
