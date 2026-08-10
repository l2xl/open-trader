# Scratchers Concept

The Trade Cockpit UI is built combining a dynamical set of content panels.
The content panels may use UI library components layout or a custom drawing canvas.

Scratcher is a class responsible for drawing on the content panel canvas. Multiple scratchers collaboratively draw on the canvas creating complex UI visualizations like a quote graph with visual indicators.
Everything on the canvas is controlled collaboratively by a number of scratchers. Examples are the quote graph candles, rulers, or even graph margins.

The scratcher interface follows an observer-driven lifecycle (no `EmitChanges` mass-rebuild):
- `OnAttach(InstrumentPanel&)` — called once when added to a panel. The scratcher creates its own sub-scene(s), attaches them under `panel.HudScene()` or `panel.LogicalScene()`, and (typically) subscribes to view-change notifications via `panel.SubscribeView(...)`.
- `CalculateSize(InstrumentPanel&)` — pre-layout phase, called when the panel's canvas size changes. Rulers shrink `panel.MutableInnerDataRect()` here to reserve their axis strips.
- `OnLayout(InstrumentPanel&)` — post-layout phase. Called after the inner-rect is finalised and the logical-scene transform has been applied; scratchers update geometry to match the new layout.
- `OnDetach(InstrumentPanel&)` — unsubscribe and release sub-scenes; runs from the panel's destructor in reverse-attach order.

Mid-frame mutations (e.g. TimeRuler pan-extend) announce themselves to the panel via `panel.MarkDirty(paint)`. The damage-tracked render loop captures pre-mutation `bounds()` for each dirty paint and invokes `Canvas::viewport(damage_union)` for the next `Canvas::draw(false)` so the redraw stays incremental. Resize forces a full redraw via `mLayoutDirty`.

# Coordinate & Transform Pipeline

This section pins down the data types and transforms across every layer of the rendering stack so scratchers, the panel, and the host widget agree on units and precision. The current code is a restricted prototype: not every layer is fully wired yet, but every new piece must respect the contract defined here. Sections 6–7 list the prototype-vs-target gaps that are intentionally accepted.

## 1. Layer strata (forward path 1 → 4)

- **Layer 1a** — logical / business prices, orders, levels, indicator values.
  Backing type: `currency<uint64_t>` (fixed-point, `src/engine/currency.hpp`).
  Owner: upstream data + cockpit business logic.
- **Layer 1b** — logical / business time.
  Backing type: `uint64_t` ms-since-epoch obtained from `std::chrono::system_clock::time_point` via `get_timestamp()` (`common/timedef.hpp`).
  Owner: upstream data + cockpit business logic.
- **Layer 2** — ThorVG vector scene.
  Backing type: `float`, with a per-axis scene floor subtracted before the cast (see section 3); pan/zoom carried in `tvg::Matrix` attached to scenes.
  Owner: `InstrumentPanel` scene tree + scratcher `OnLayout` emissions.
- **Layer 3** — pixel canvas (ThorVG SwCanvas raster target).
  Backing type: `uint32_t` width/height/stride.
  Owner: `InstrumentPanel::SetTarget` / `OnSize`.
- **Layer 4** — host graphics library (Elements / Cairo blit, screen pixels).
  Backing type: `float` elements widget coordinates; Cairo blit with `int` dirty rects.
  Owner: `InstrumentPanelElement`.

Notes on layer-1 typing:
- Prices/orders/indicators are *only* `currency<uint64_t>`. No `double` math, no `currency<float>`, no ad-hoc `int64_t` price scaling at this layer.
- Time is *only* `uint64_t` ms-since-epoch (UTC) at the boundary; in-memory the upstream model may keep `std::chrono::utc_clock::time_point` and convert via `get_timestamp()` exactly at the layer-1/2 cast.
- Wire-format JSON sits at layer 0. The data pipeline now parses each fractional field into `currency<uint64_t>` once at deserialization (`PublicTrade::price`/`size`, instrument tick/precision, …), so scratchers receive fixed-point values and rescale to a fixed point count via `currency::raw_at()` — they never parse strings. `PublicTrade::time` stays a wire string (a millisecond timestamp, not a fractional value).

Notes on layer-2/3 typing (pixel & HUD domains):
- Pixel-lattice integers — `Rectangle` fields, ruler strip reservations, damage extents — are `int64_t` (`scratcher::Rectangle`, `src/common/data_rectangle.hpp`). `mCanvasWidth/Height` stay `uint32_t` as the raster-target size ThorVG binds. Raw `int`/`unsigned` pixel variables are not used.
- Continuous HUD coordinates and typography metrics (font sizes, estimated label widths, collision edges) are `float` end-to-end. No float→int→float round trips: a value crosses between the two domains at most once per path — `int64_t → float` where a lattice value enters HUD math, `float → int64_t` via `std::ceil`/`std::llround` where a continuous metric becomes a reservation or damage extent.
- The narrowing to `int32_t`/`int` at the ThorVG (`Canvas::viewport`) and Cairo (`cairo_surface_mark_dirty_rectangle`) boundaries is explicit at the call site and happens nowhere else.

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

Floor selection (owned by `InstrumentPanel`, configurable via `SetSceneFloor`):
- **`time_ms`** — defaulted in `Initialize()` to `(now_ms / candle_period_ms) * candle_period_ms`, i.e. the candle boundary at or before the panel's session start. The cockpit / data layer overrides via `SetSceneFloor` once the visible data window's left edge is known. The default keeps `t_max - floor` below `candle_period_ms + drift`, which for typical chart spans (hours to days) stays well within `2^24 ms` — exact ms precision.
- **`price_points`** — defaults to `0`, acceptable while charted instruments stay below `~10^7` price points (cents-precision majors). For higher-decimal instruments the cockpit calls `SetSceneFloor` with the data-window minimum rounded down to the instrument's tick base.

Reverse mouse-pick → model time/price uses `(scene_x_inv + floor.time_ms, scene_y_inv + floor.price_points)`, with the inverse computed via `tvg::Matrix` operations only.

## 4. Scene tree & transforms (layer 2 internals)

```
SwCanvas (layer 3, pixels — uint32_t target, int64_t Rectangle)
└─ mHudScene              T = Y-flip-about(canvas_h),  clip = rect(0, 0, canvas_w, canvas_h)
   │                      # Y-flip-about-canvas_h turns the entire scene's contents into
   │                      # HUD-Y-up: y=0 at canvas bottom, y=canvas_h at canvas top.
   │                      # Rulers, ticks, labels, hover overlays live here directly.
   ├─ <ruler subtrees>     # TimeRuler, PriceRuler each own a sub-scene attached here
   └─ mLogicalScene        T = M_view · scale(px_per_ms, px_per_pt) · translate(-floor)
                           clip = inner data rect (HUD-Y-up corners)
                           # scratchers emit (timestamp_ms, price_points) in layer-1
                           # numeric domain; the matrix maps them onto the inner data rect.
                           # Pan/zoom UI composes into M_view (Phase 4 work).
```

- `mLogicalScene` is the only scene that carries layer-1 business values. Scratchers drawing market data (e.g. `QuoteScratcher`) emit there; the panel composes the floor + scale via the matrix.
- `mHudScene` carries HUD-Y-up pixel coordinates. Rulers and labels live here. Their X coordinates are absolute HUD pixels; their Y coordinates are HUD-Y-up (so a tick line below the axis has a SMALLER y_hud than the axis itself).
- Text inside `mHudScene` requires a per-paint counter-flip (matrix `{1, 0, x; 0, -1, y_hud; 0, 0, 1}` instead of `translate(x, y_hud)`) to keep glyphs upright under HUD's outer Y-flip. Lines and shapes are unaffected.
- `mHudScene`'s clip is in canvas-pixel space (HUD's parent is the canvas with identity transform). `mLogicalScene`'s clip is in HUD space (parent of mLogicalScene), since Paint::clip is applied with the parent-matrix per ThorVG's renderer; the inner data rect therefore maps to HUD coords as `(rect.x, canvas_h - rect.y_end(), rect.w, rect.h)`.

## 5. Panel-side state

`InstrumentPanel` carries the cross-layer state required to keep transforms consistent:

- `mCanvasWidth`, `mCanvasHeight` (`uint32_t`) — layer-3 outer canvas size set by host on resize. `OuterCanvasRect()` exposes them as a `Rectangle`.
- `mInnerDataRect` (`scratcher::Rectangle`, `int64_t` origin+size) — layer-3 canvas area left after rulers reserve their strips during `CalculateSize`. Accessed via `MutableInnerDataRect()` (mutating, by rulers) and `InnerDataRect()` (read-only, by everyone else).
- `mSceneFloor` (`SceneFloor { uint64_t time_ms; uint64_t price_points; }`) — layer-1→2 boundary floor subtracted before the float cast. Configurable via `SetSceneFloor`.
- `mLogicalScene->transform()` (`tvg::Matrix`) — single source of truth for the layer-1→2 scale and the layer-2 inner-rect translation. `e11 = px_per_ms`, `e22 = px_per_point`, `e13 = inner.x - e11 * (view_left - floor.t)`, `e23 = canvas_h - inner.y_end()`. There is no parallel `LogicalScale` field — readers that need a scale (rulers computing tick spacing, mouse-pick reverse projection, tests) take it directly from this matrix. The panel exposes `HudXOfTime(int64_t)` and `TimeOfHudX(float)` that read this matrix as the live source. `M_view` (pan/zoom) will eventually compose into the same matrix; reverse projections invert it there.
- `mHudScene->transform()` — Y-flip about outer canvas height. Children of mHudScene (rulers and the logical scene) inherit Y-up coords; text paints inside HUD apply a per-paint counter-flip to keep glyphs upright.

## 6. Lifecycle pipeline (current contract)

`InstrumentPanel::OnSize(width, height)`:
1. `mCanvasWidth/Height` ← input; `mInnerDataRect` ← `(0, 0, w, h)` (full canvas; rulers will shrink it).
2. `ApplyOuterSceneTransforms()` — sets `mHudScene` Y-flip-about-canvas_h transform and canvas-rect clip.
3. For each scratcher: `CalculateSize(panel)` — may shrink `mInnerDataRect` (rulers reserve outer strips for axis space). Order matters; the standard chain is `TimeRuler → PriceRuler → QuoteScratcher`.
4. `ApplyLogicalSceneTransform()` — composes `M_logical` from `mLogicalScene`'s current scale, the view-left anchor, the new `mInnerDataRect`, and the canvas height; also assigns the inner-rect clip in HUD space.
5. For each scratcher: `OnLayout(panel)` — scratcher updates its sub-scene contents (axis lines, tick labels, etc.) to match the new inner rect.
6. Fire SubscribeSize subscribers, set `mLayoutDirty = true` so the next `Render()` does a full clear-and-redraw.

`InstrumentPanel::Render()` is invoked from `InstrumentPanelElement::draw()`. It returns a `Rectangle` describing the canvas-pixel region that was repainted (empty rect = nothing drawn this frame, host skips `cairo_surface_mark_dirty_rectangle`):
- **Full-redraw path** (taken when `mLayoutDirty`): `viewport(0,0,w,h)` → `update()` → `draw(true)` → `sync()`. Returns the full canvas rect.
- **Incremental path** (taken when scratchers populated `mDirtyPaints` via `MarkDirty`): compute damage union from captured pre-bounds, `viewport(damage)` → `update()` → `draw(false)` → `sync()`. Returns the damage rect.
- **Early-out**: no dirty paints and no layout dirty → return empty rect.

Float→`int` rounding at the layer-3/4 boundary is owned exclusively by ThorVG / Cairo; scratchers never round to pixels by themselves.

## 7. Prototype-vs-target gap (intentional)

Wired now:
- `M_logical` on `mLogicalScene` is the single source of truth for layer-1→2 scale and layer-2 translation. `Initialize()` calls `SeedDefaultLogicalSceneScale()` to write `e11 = CandleWidth / candle_period_ms` and `e22 = 0` (price scale must be configured by the data layer once instrument extents are known); `OnSize` calls `ApplyLogicalSceneTransform` which preserves `e11`/`e22` and rewrites `e13`/`e23` from the inner data rect.
- `mSceneFloor` exposed via `GetSceneFloor` / `SetSceneFloor`. Default time floor anchors at the candle boundary at or before panel-session start; default price floor is `0`.
- `QuoteScratcher` subtracts the floor in `int64_t` before the float cast when emitting buoy geometry.
- Outer canvas rect (`OuterCanvasRect()`, computed from `mCanvasWidth/Height`) and inner data rect (`MutableInnerDataRect`/`InnerDataRect`) are split.

Still gapped (intentional, prototype):
- `M_view` (pan/zoom) on `mLogicalScene` is not wired — currently the matrix only carries scale + view-left offset. Reverse mouse-pick → model also still unimplemented (`HudXOfTime` / `TimeOfHudX` are wired but no UI consumer yet); both belong to the same future iteration.
- Price scale (`mLogicalScene->transform().e22`) has no automatic data-extent derivation; the cockpit / data layer must rewrite the matrix once the visible price window is known.
- TimeRuler emits persistent paints with full-rebuild on view changes; the in-place pan-translate optimisation (within ± 0.5 viewport buffer) is supported architecturally but the current `RebuildAll` path always re-emits.

Gaps above are accepted while the data and persistence layers below are not yet finalised. New work in this directory must:
- not introduce reverse-path logic outside layer 2,
- not bypass the scene floor when emitting into `mLogicalScene`,
- not duplicate the layer-2 scale into a parallel field — read it from `mLogicalScene->transform()`,
- keep outer-canvas vs inner-data-rect roles distinct,
- not use `double` or `currency<float>` for business-domain math.

# Layered Class Hierarchy

```
Scratcher (interface: OnAttach / CalculateSize / OnLayout / OnDetach)
├── PriceRuler         — vertical price-axis: axis line, nice-step (1-2-5) tick stubs and per-tick
│                        price labels recovered from the live LogicalScene transform; sub-scene
│                        under HudScene
├── PriceIndicator     — last-price overlay: dotted line across the inner rect + coloured price tag
│                        in the ruler strip; sub-scene under HudScene
├── QuoteScratcher     — ingests PublicTrade stream, builds BuoyCandleQuotes, emits buoy shapes; sub-scene under LogicalScene
└── TimeRuler          — horizontal time-axis with persistent paints (axis shape, tick lines scene,
                         label scene, leftmost-timestamp text), boundary labels above + regular ticks
                         below the line; sub-scene under HudScene
```

`InstrumentPanel` (`cockpit`) owns the ordered list of scratchers, drives the lifecycle pipeline (see §6), and exposes:
- `HudScene()` / `LogicalScene()` — parent scenes for scratcher-owned sub-trees.
- `SubscribeSize(cb)` / `SubscribeView(cb)` / `Unsubscribe(id)` — observer registration for size/view notifications.
- `MarkDirty(paint)` — announce a paint that's about to mutate so the next `Render()` damages its pre-mutation bounds.
- `HudXOfTime(time_ms)` / `TimeOfHudX(hud_x)` — forward and reverse projection through `mLogicalScene`'s X-axis transform.

The standard scratcher registration order for a quote chart is: `TimeRuler` → `PriceRuler` → `QuoteScratcher`.
