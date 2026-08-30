// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "quote_scratcher.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "instrument_panel.hpp"
#include "timedef.hpp"

namespace scratcher::cockpit {

namespace {

struct Color { uint8_t r, g, b, a; };

// Green/red fill the bell body; the waist marker uses the brighter same-hue
// derivative so it pops as a crisp anchor on top of the contour.
//
// These four are the MIDPOINT stop of the volume ramp specified in BUOY_CANDLE.md
// section 10.3 — the flat stand-in until BUOY_RENDER-001 lands, at which point the
// body takes the stop its own volume rank selects and the marker follows +0.13 L
// above it. Built in OKLCH at 80 % of the sRGB gamut edge, bullish hue 145 deg on a
// lightness lane 0.08 above bearish hue 30 deg (that lane offset is the only cue a
// red-green dichromat keeps; see section 10.4). The retired pair sat at L 0.32/0.23,
// so dark on a black ground that a bearish body cleared it by a contrast ratio of
// only 1.2 and its waist marker by 1.23; these clear the background by 3.8 and 2.6
// and the markers clear their bodies by ~1.7 on both sides.
constexpr Color kBodyGreen{44, 120, 51, 255};   // #2c7833  OKLCH(0.51, 0.128, 145)
constexpr Color kBodyRed{140, 39, 28, 255};     // #8c271c  OKLCH(0.43, 0.138, 30)
constexpr Color kWaistGreen{63, 165, 72, 255};  // #3fa548  OKLCH(0.64, 0.162, 145)
constexpr Color kWaistRed{200, 60, 44, 255};    // #c83c2c  OKLCH(0.56, 0.179, 30)
constexpr Color kGray{110, 110, 110, 255};

// Half-alpha derivative of a line's colour, filling its prefilter flanks. Same hue, so a
// flank landing on same-coloured ink is invisible and one landing on the background or on
// the darker body reads as the line's soft edge.
constexpr Color Flank(Color c) { return {c.r, c.g, c.b, static_cast<uint8_t>(c.a / 2)}; }

// Fill per pool, indexed by BuoyPool — the same order the shapes are added to the scene in.
constexpr std::array<Color, kBuoyPoolCount> kPoolFill{
    kGray,
    Flank(kBodyGreen),  kBodyGreen,
    Flank(kBodyRed),    kBodyRed,
    Flank(kWaistGreen), kWaistGreen,
    Flank(kWaistRed),   kWaistRed,
};

// PREFILTERED LINES. The buoy's two purely linear elements — the VWAP waist marker and
// the per-half range spine — sit on geometry that sweeps continuously across the pixel
// grid as the chart scrolls and rescales, which is the case a hard-edged hairline is
// worst at. ThorVG rasterises with analytic (box-filtered) coverage, so a rect exactly
// one pixel wide alternates between ONE fully covered column and TWO half covered ones
// as its centre crosses a pixel boundary: the ink is conserved but its distribution is
// not, and the eye reads the line pulsing between crisp-and-dark and soft-and-pale —
// exactly the "jaggies are especially noticeable when the lines are animated" case
// Chan & Durand open GPU Gems 2 ch. 22 ("Fast Prefiltered Lines") with.
//
// The two escapes from it are mutually exclusive. Snapping the line to the pixel grid is
// crisp but quantises its motion, which is why animated primitives are anti-aliased and
// NOT pixel-snapped (SVG's shape-rendering=crispEdges is the static-only counterpart).
// The other is to PREFILTER: widen the line to at least the filter footprint and let a
// soft skirt absorb the sub-pixel remainder, the same reasoning behind the "hairline"
// rule Skia/Direct2D apply below one pixel (clamp the width to one pixel and modulate
// alpha by the true width rather than shrink the geometry).
//
// So each line is emitted as a solid CORE plus a half-alpha FLANK on each side:
//   * core >= 2 px — at two pixels at least one column is fully covered at EVERY
//     sub-pixel offset ([1,1] centred, [.5,1,.5] at a half-pixel offset), so peak
//     intensity and total ink are both invariant under motion; at one pixel the peak
//     halves twice per pixel of travel;
//   * a 1 px flank at half alpha is the two-level quantisation of that filter skirt a
//     flat fill can express, turning the leftover edge redistribution into a steady soft
//     edge instead of a change in apparent width.
// Flanks are their line's own hue at half alpha and are painted directly under the core
// (see the pool Z-order), so they never wash out the line they belong to.
constexpr float kLineCorePx = 2.f;
constexpr float kLineFlankPx = 1.f;

// BUOY_CANDLE.md section 8 render-side tunables: the median-volume buoy's full waist
// width, the horizontal clearance between neighbouring slots, the contour sample count
// (N = 12 wall levels), and the aspect ratio below which a period becomes special.
constexpr float kTargetWaistWidthPx = 14.f;
constexpr std::size_t kContourLevels = 13;
constexpr float kAspect = 2.f;

// The slot gap is the SAME anti-flicker width as the line core above, and for the same
// reason read inside out: a gap is a strip of background between two neighbours, and a
// strip narrower than two device pixels is not guaranteed one fully UNCOVERED column at
// every sub-pixel offset — it would thin and blink shut as the chart scrolls, exactly
// the pulsing a one-pixel line does. Every slot-spanning element is inset by half of it
// per side: the bell through the A_slot clamp, the waist marker and the empty-buoy dash
// through the slot inset in AppendBuoy.
constexpr float kSlotGapPx = kLineCorePx;

inline float SubToFloat(uint64_t value, uint64_t floor)
{
    return static_cast<float>(static_cast<int64_t>(value) - static_cast<int64_t>(floor));
}

void ApplyFill(tvg::Shape& shape, Color c)
{
    shape.fill(c.r, c.g, c.b, c.a);
}

// Reset every pooled shape of one emission target and re-apply its fill (reset() drops
// the paint along with the path).
void ResetPool(BuoyShapePool& pool)
{
    for (std::size_t i = 0; i < kBuoyPoolCount; ++i) {
        pool[i]->reset();
        ApplyFill(*pool[i], kPoolFill[i]);
    }
}

// Axis-aligned filled quad, traversed (xa,ya) -> (xa,yb) -> (xb,yb) -> (xb,ya). Corners
// are NOT normalised: the caller's order is kept so a quad that shares a pool with a
// contour it must union with under the nonzero fill rule can match that contour's winding.
void AppendQuad(tvg::Shape& shape, float xa, float ya, float xb, float yb)
{
    shape.moveTo(xa, ya);
    shape.lineTo(xa, yb);
    shape.lineTo(xb, yb);
    shape.lineTo(xb, ya);
    shape.close();
}

// Prefiltered vertical line centred on `cx`, spanning `y_from`..`y_to`: the solid core
// into `core`, the two flanks into `flank`. `half_core` and `flank_w` are scene-x units.
// The core starts at the RIGHT of the centre and returns on the left — the traversal the
// bell wall contours use — so a spine sharing the body pool with its wall always winds
// with it and the nonzero rule unions them instead of carving a slit.
void AppendVLine(tvg::Shape& core, tvg::Shape& flank,
                 float cx, float y_from, float y_to, float half_core, float flank_w)
{
    AppendQuad(core,  cx + half_core,            y_from, cx - half_core,            y_to);
    AppendQuad(flank, cx + half_core + flank_w,  y_from, cx + half_core,            y_to);
    AppendQuad(flank, cx - half_core,            y_from, cx - half_core - flank_w,  y_to);
}

// Prefiltered horizontal line centred on `cy`, spanning `x_from`..`x_to`: solid core into
// `core`, the two flanks into `flank`. `half_core` and `flank_h` are scene-y units.
void AppendHLine(tvg::Shape& core, tvg::Shape& flank,
                 float cy, float x_from, float x_to, float half_core, float flank_h)
{
    AppendQuad(core,  x_from, cy - half_core,            x_to, cy + half_core);
    AppendQuad(flank, x_from, cy + half_core,            x_to, cy + half_core + flank_h);
    AppendQuad(flank, x_from, cy - half_core - flank_h,  x_to, cy - half_core);
}

// Coloring baseline: the most recent FILLED buoy strictly before `idx` in `closed`.
// Empty buoys carry the previous close forward as min == max == mean == close, so an
// empty predecessor's extents collapse onto that flat carried level. Comparing against
// it would measure growth against the carried close rather than the prior real candle —
// e.g. the top wick paints green whenever curr.max merely exceeds the carried close,
// even when the last traded high was higher. Walking back to the last traded buoy
// restores the intended "did the high/low/mean grow vs the previous real candle"
// semantics. Returns nullptr when no filled predecessor exists (series anchor — the
// caller then paints neutral by comparing the buoy against itself). prev.close is
// unaffected by this choice: empty buoys carry close forward unchanged, so the last
// filled buoy's close equals any intervening empty buoy's close.
const BuoyCandleQuotes::candle_t* PrevFilledBuoy(const BuoyCandleQuotes::quotes_t& closed,
                                                 std::size_t idx)
{
    for (std::size_t j = idx; j-- > 0; ) {
        if (closed[j].volume.raw() > 0) return &closed[j];
    }
    return nullptr;
}

// Emit one buoy into its Shape pools per BUOY_CANDLE.md. Filled geometry only:
//   * the per-half RANGE SPINE is a prefiltered vertical line from the waist to that
//     half's extreme, drawn for EVERY period — every period has tips, and where the
//     3σ taper thins below a pixel (or no bell is drawn at all) the spine is what
//     keeps the range visible;
//   * the BODY is two independently coloured half-bells, each ONE closed contour
//     (flat waist edge at ±A + that side's Gaussian wall, Catmull-Rom smoothed);
//     waist half-width A = (Wt/2)·WidthRatio px clamped to the slot; the upper half
//     colours by curr.max vs prev.max, the lower half by curr.min vs prev.min — the
//     spine of a half shares its half's colour channel;
//   * the WAIST MARKER is a prefiltered horizontal line at the VWAP level spanning
//     the slot less the gap inset, coloured by curr.mean vs prev.mean;
//   * SPECIAL (degraded) periods — single-price, or failing the screen-space ASPECT
//     test — draw the spines and that same waist marker, in the same colour channels,
//     and no bell;
//   * the empty-buoy gray dash is (candle_width - gap) px wide × 0.5 px tall.
// Every slot-spanning element is inset kSlotGapPx/2 per side so neighbouring buoys
// keep a gap of background between them — see kSlotGapPx for why that gap is the
// same width as a prefiltered line's core.
// X dimensions are pixel-stable through the LogicalScene matrix because period_ms
// maps to candle_width px via e11; dimensions that need a fixed pixel count are
// supplied in scene units as (pixels * px.x) or (pixels * px.y) per axis.
void AppendBuoy(BuoyShapePool& pool,
                uint64_t buoy_ts, uint64_t duration,
                const BuoyCandleQuotes::candle_t& curr,
                const BuoyCandleQuotes::candle_t& prev,
                const BuoyCandleQuotes::Calibration& calibration,
                const SceneFloor& floor,
                const ScenePixelSize& px,
                float candle_width_px,
                std::size_t price_decimals)
{
    // Candle prices are currency carried verbatim from the wire; the scene works in integer
    // "points" on the instrument's price grid, so project each currency to that grid here via
    // raw_at(price_decimals) — the one place wire scale becomes scene coordinates.
    const float slot_left  = SubToFloat(buoy_ts, floor.time_ms);
    const float slot_right = SubToFloat(buoy_ts + duration, floor.time_ms);
    const float mid_x      = 0.5f * (slot_left + slot_right);
    const float mean_y     = SubToFloat(curr.mean.raw_at(price_decimals), floor.price_points);

    // Slot inset: every element that spans the period horizontally stops half a gap
    // short of the slot edge, so neighbouring buoys are separated by a full kSlotGapPx
    // of background rather than butting into one continuous band. Clamped so a slot
    // narrower than the gap still leaves a 1 px mark instead of inverting.
    const float inset  = std::min(0.5f * kSlotGapPx * px.x,
                                  std::max(0.f, 0.5f * (slot_right - slot_left) - 0.5f * px.x));
    const float left_x  = slot_left + inset;
    const float right_x = slot_right - inset;

    const auto shape = [&pool](BuoyPool which) -> tvg::Shape& {
        return *pool[static_cast<std::size_t>(which)];
    };

    if (curr.volume.raw() == 0) {
        // Empty buoy — no trades arrived during the period. Carry the previous last
        // price forward as a 0.5 px-tall gray rect; in this state the model has
        // min == max == mean == last_price, so no body would have any visible extent.
        // Inset like every other slot-spanning element, which is what makes a run of
        // empty periods read as the dashes it is named for rather than one flat rule.
        const float half_h = 0.25f * px.y;
        AppendQuad(shape(BuoyPool::Gray), left_x, mean_y - half_h, right_x, mean_y + half_h);
        return;
    }

    // Gray "move" connector: a thin vertical line bridging the previous close to this
    // buoy's mean level, drawn only when the previous close sits outside [min, max] — i.e.
    // the price jumped into a new band between periods. The candle does not encode that
    // move (min/max reflect only this period's own trades), so the connector is what makes
    // a gap between consecutive buoys visible. The rect spans down to mean so it meets the
    // candle with no gap, but the gray pool is drawn UNDER the body (see OnAttach Z-order),
    // so the inner segment is hidden and only the part outside [min, max] (previous close →
    // nearest tip) shows — a stem from the prior close to the candle edge.
    if (prev.close > curr.max || prev.close < curr.min) {
        const float prev_close_y = SubToFloat(prev.close.raw_at(price_decimals), floor.price_points);
        // 1 px wide (not 0.5 px). A sub-pixel-width filled rect distributes its
        // anti-aliased coverage differently as its screen-x sweeps sub-pixel positions
        // under scroll — one ~50% column vs two ~25% columns — which the eye reads as a
        // flickering change in thickness/brightness. At a full pixel the covered ink is
        // constant across sub-pixel offsets, so the connector stays visually steady.
        const float half_w  = 0.5f * px.x;
        AppendQuad(shape(BuoyPool::Gray), mid_x - half_w, prev_close_y, mid_x + half_w, mean_y);
    }

    // Pixel-fixed line geometry. The waist marker is thin across Y and the range spine
    // across X, so each takes its core/flank extents from that axis' scene-units-per-pixel.
    const float waist_half_core = 0.5f * kLineCorePx * px.y;
    const float waist_flank     = kLineFlankPx * px.y;
    const float spine_half_core = 0.5f * kLineCorePx * px.x;
    const float spine_flank     = kLineFlankPx * px.x;

    // The waist channel: mean growth against the previous FILLED buoy, bullish by default
    // (no filled predecessor makes prev == curr, and `>=` paints that green). A degraded
    // period's marker and a readable buoy's waist share this one channel — degradation is
    // about the distribution being unreadable, not about the direction being unknown.
    const bool waist_grown = curr.mean >= prev.mean;
    tvg::Shape& waist_core  = shape(waist_grown ? BuoyPool::WaistGreen : BuoyPool::WaistRed);
    tvg::Shape& waist_flank_shape = shape(waist_grown ? BuoyPool::WaistGreenFlank : BuoyPool::WaistRedFlank);

    // Peak half-width from the area constraint: the target waist scaled by the
    // dimensionless window ratio, capped by the slot so neighbours never touch.
    const bool single_price = !(curr.min < curr.max);
    const float a_slot_px = std::max(0.5f, 0.5f * (candle_width_px - kSlotGapPx));
    float a_px = 0.f;
    BuoyCandleQuotes::candle_t fitted = curr;
    if (!single_price) {
        fitted = curr.FitRange();
        a_px = std::min(0.5f * kTargetWaistWidthPx * BuoyCandleQuotes::WidthRatio(fitted, calibration), a_slot_px);
    }

    // Per-half colour channel, shared by that half's range spine and wall contour: the
    // upper half tracks curr.max vs prev.max, the lower curr.min vs prev.min.
    const auto half_pools = [&](bool upper) {
        const bool grown = upper ? !(curr.max < prev.max) : !(curr.min < prev.min);
        return grown ? std::pair{BuoyPool::BodyGreen, BuoyPool::BodyGreenFlank}
                     : std::pair{BuoyPool::BodyRed,   BuoyPool::BodyRedFlank};
    };

    // Range spines, both halves, BEFORE the regime test and unconditionally: a tip is a
    // property of the period, not of whether the period's distribution happens to be
    // drawable, so a degraded buoy shows its range exactly like a readable one. A tip
    // that coincides with the waist still emits its spine — it is nominally there, it
    // simply spans zero price and so covers no pixel. Emitted in the SAME traversal
    // order as the wall contour that may follow it into the same pool (start right of
    // the waist, out to the extreme, back on the left), so the two always share one
    // winding and the nonzero fill rule unions them; composing a buoy from independent
    // halves is what makes cross-winding cancellation (the carved centre slit of the
    // retired whole-body path) structurally impossible.
    const auto append_spine = [&](bool upper) {
        const auto& extreme = upper ? curr.max : curr.min;
        const auto [core_pool, flank_pool] = half_pools(upper);
        AppendVLine(shape(core_pool), shape(flank_pool), mid_x, mean_y,
                    SubToFloat(extreme.raw_at(price_decimals), floor.price_points),
                    spine_half_core, spine_flank);
    };
    append_spine(true);
    append_spine(false);

    // Screen-space regime test (BUOY_CANDLE.md section 5): a single-price period is
    // special by construction; otherwise the buoy is special when even its longer half
    // is shorter than ASPECT body widths on screen — the bell cannot be read at that
    // aspect, so the fixed-size marker is stamped instead and nothing height-scaled is
    // drawn. The marker IS the waist line: everything the degraded period still says is
    // its VWAP level and whether that level grew.
    const float top_px = static_cast<float>((curr.max - curr.mean).raw_at(price_decimals)) / px.y;
    const float bot_px = static_cast<float>((curr.mean - curr.min).raw_at(price_decimals)) / px.y;
    if (single_price || std::max(top_px, bot_px) < kAspect * 2.f * a_px) {
        AppendHLine(waist_core, waist_flank_shape, mean_y, left_x, right_x,
                    waist_half_core, waist_flank);
        return;
    }

    // Body: each half is ONE closed filled sub-path — the straight waist edge at ±A plus
    // the Gaussian wall of that side, smoothed as two per-wall Catmull-Rom chains meeting
    // at the tip so the tip stays a sharp corner, wound with (and unioned against) the
    // spine already in the pool. A zero-height half (waist ON the extreme) grows no wall
    // and is skipped — the other half then reads as the flat-waisted half-bell of
    // BUOY_CANDLE.md section 5, and the skipped half keeps its zero-length spine.
    const int64_t mean_raw = static_cast<int64_t>(curr.mean.raw_at(price_decimals));
    const auto append_half = [&](bool upper) {
        const auto& extreme = upper ? curr.max : curr.min;
        const int64_t extreme_raw = static_cast<int64_t>(extreme.raw_at(price_decimals));
        if (extreme_raw == mean_raw) return;
        tvg::Shape& body = shape(half_pools(upper).first);

        std::vector<BuoySplinePoint> outline;
        outline.reserve(2 * kContourLevels - 1);
        for (std::size_t k = 0; k < kContourLevels; ++k) {
            const int64_t level_raw = mean_raw + (extreme_raw - mean_raw) * static_cast<int64_t>(k) / static_cast<int64_t>(kContourLevels - 1);
            const float width = (k + 1 == kContourLevels) ? 0.f
                : fitted.WallWidth(BuoyCandleQuotes::price_t(static_cast<uint64_t>(level_raw), price_decimals));
            outline.push_back({mid_x + width * a_px * px.x, SubToFloat(static_cast<uint64_t>(level_raw), floor.price_points)});
        }
        for (std::size_t k = kContourLevels - 1; k-- > 0;)
            outline.push_back({2.f * mid_x - outline[k].x, outline[k].y});

        const std::span<const BuoySplinePoint> right_wall(outline.data(), kContourLevels);
        const std::span<const BuoySplinePoint> left_wall(outline.data() + kContourLevels - 1, kContourLevels);
        body.moveTo(outline.front().x, outline.front().y);
        for (const auto& segment : CatmullRomSpline(right_wall, false))
            body.cubicTo(segment.control1.x, segment.control1.y, segment.control2.x, segment.control2.y, segment.end.x, segment.end.y);
        for (const auto& segment : CatmullRomSpline(left_wall, false))
            body.cubicTo(segment.control1.x, segment.control1.y, segment.control2.x, segment.control2.y, segment.end.x, segment.end.y);
        body.close();
    };
    append_half(true);
    append_half(false);

    // Waist marker on top of the body: a prefiltered horizontal line spanning the slot
    // (less the gap inset) at the VWAP level, its core kLineCorePx tall regardless of
    // the price-axis scale. Axis-aligned on purpose — the retired diamond met the scrolling axis with
    // four slanted edges, the worst case for horizontal sub-pixel motion, while a
    // horizontal line presents none at all and only its two short ends ever move across
    // the grid.
    AppendHLine(waist_core, waist_flank_shape, mean_y, left_x, right_x,
                waist_half_core, waist_flank);
}

// Visible-window calibration (BUOY_CANDLE.md section 4): medians over the buoys inside
// the current view — closed periods plus the active candle when its slot is visible.
// Ineligible periods (empty, single-price) are excluded by Calibrate itself.
BuoyCandleQuotes::Calibration VisibleCalibration(const InstrumentPanel& panel, const BuoyCandleQuotes& quotes)
{
    const auto first_ts_opt = quotes.first_buoy_timestamp();
    if (!first_ts_opt) return {};

    const int64_t view_left  = panel.ViewLeftTimeMs();
    const int64_t period_ms  = static_cast<int64_t>(panel.CandlePeriod().count()) * 1000;
    const int64_t cwidth     = std::max<int64_t>(1, panel.CandleWidth());
    const int64_t inner_w    = std::max<int64_t>(1, panel.InnerDataRect().w);
    const int64_t view_right = view_left + (inner_w * period_ms) / cwidth;

    const auto& closed = quotes.quotes();
    const int64_t first_ts = static_cast<int64_t>(*first_ts_opt);
    const int64_t duration = static_cast<int64_t>(quotes.buoy_duration());
    if (duration <= 0) return {};

    std::vector<BuoyCandleQuotes::candle_t> window;
    const int64_t closed_n  = static_cast<int64_t>(closed.size());
    const int64_t v_from    = std::max<int64_t>(0, (view_left - first_ts) / duration);
    const int64_t v_to_excl = std::min<int64_t>(closed_n, std::max<int64_t>(0, (view_right - first_ts) / duration + 1));
    window.reserve(static_cast<std::size_t>(std::max<int64_t>(0, v_to_excl - v_from)) + 1);
    for (int64_t i = v_from; i < v_to_excl; ++i)
        window.push_back(closed[static_cast<std::size_t>(i)]);

    const int64_t active_ts = first_ts + closed_n * duration;
    if (active_ts < view_right && active_ts + duration > view_left)
        window.push_back(quotes.active_candle());

    return BuoyCandleQuotes::Calibrate(window);
}

// ~10 % drift hysteresis on a calibration median: exact integer cross-products, so no
// currency value ever leaves fixed point for the comparison.
bool DriftedBeyondTenth(const BuoyCandleQuotes::price_t& emitted, const BuoyCandleQuotes::price_t& fresh)
{
    const std::size_t d = std::max(emitted.decimals(), fresh.decimals());
    const big_int emitted_raw = big_int(emitted.raw_at(d));
    const big_int fresh_raw = big_int(fresh.raw_at(d));
    return fresh_raw * 10 > emitted_raw * 11 || fresh_raw * 10 < emitted_raw * 9;
}

}

void QuoteScratcher::OnAttach(InstrumentPanel& panel)
{
    mScene.reset(tvg::Scene::gen());

    // Z-order is add() order under mScene, and BuoyPool is declared in exactly that
    // order: the gray pool first so the "move" connector renders UNDER the buoy — it
    // runs all the way to mean but the body covers its inner segment, leaving only the
    // part outside [min, max] (previous close → nearest tip) visible as a stem with no
    // gap. Then, per colour family, the half-alpha prefilter flanks immediately below
    // their solid core so a flank never washes out the line it belongs to: bell bodies
    // and their range spines first, the waist markers (degraded periods' markers
    // included) on top. Empty-buoy dashes also live in the gray pool but sit where no
    // buoy is drawn, so their layer placement is immaterial. Closed-pool shapes are
    // grouped contiguously ahead of all active-pool shapes.
    for (BuoyShapePool* pool : {&mClosedShapes, &mActiveShapes}) {
        for (std::size_t i = 0; i < kBuoyPoolCount; ++i) {
            (*pool)[i].reset(tvg::Shape::gen());
            ApplyFill(*(*pool)[i], kPoolFill[i]);
            mScene->add((*pool)[i].get());
        }
    }

    panel.LogicalScene().add(mScene.get());
}

void QuoteScratcher::OnDetach(InstrumentPanel& /*panel*/)
{
    mScene.reset();

    for (BuoyShapePool* pool : {&mClosedShapes, &mActiveShapes})
        for (auto& shape : *pool) shape.reset();

    mEmittedClosedCount = 0;
    mEmittedFirstBuoyTs.reset();
    mEmittedFloorTimeMs = 0;
    mEmittedFloorPricePts = 0;
    mEmittedPxSizeX = 0.0f;
    mEmittedPxSizeY = 0.0f;
    mEmittedCalibration.reset();
}

uint64_t QuoteScratcher::WallNowMs()
{
    return std::chrono::duration_cast<milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void QuoteScratcher::PriceAutoscale(InstrumentPanel& panel)
{
    // Visible-window geometry for the autoscale span. Recomputed only on data arrival — as the
    // view scrolls between trades the window is left as-is (the scale follows on the next
    // trade, sub-second on a live feed), which keeps zoom/scroll free of price rescans.
    const int64_t view_left  = panel.ViewLeftTimeMs();
    const int64_t period_ms  = static_cast<int64_t>(panel.CandlePeriod().count()) * 1000;
    const int64_t cwidth     = std::max<int64_t>(1, panel.CandleWidth());
    const int64_t inner_w    = std::max<int64_t>(1, panel.InnerDataRect().w);
    const int64_t span_ms    = (inner_w * period_ms) / cwidth;
    const int64_t view_right = view_left + span_ms;

    // Compute the price extent across VISIBLE buoys only. Closed buoys are
    //    append-only and never evicted, so using all-historical extents would let
    //    e22 shrink monotonically with session length and squash recent candles
    //    into a thin band. Restricting to [view_left, view_right] gives a tight
    //    autoscale that follows the current viewport. Empty buoys (volume == 0)
    //    contribute their carried-forward last_price level via min == max, which
    //    keeps the level visible without expanding the range.
    // Extents are accumulated in scene points (the instrument's price grid), projecting each
    // candle's currency via raw_at — the same grid SceneFloor.price_points lives on.
    const std::size_t pd = panel.PriceDecimals();
    uint64_t p_min = std::numeric_limits<uint64_t>::max();
    uint64_t p_max = 0;
    const auto first_ts_opt = mQuotes.first_buoy_timestamp();
    if (first_ts_opt) {
        const auto& closed = mQuotes.quotes();
        const int64_t first_ts = static_cast<int64_t>(*first_ts_opt);
        const int64_t duration = static_cast<int64_t>(mQuotes.buoy_duration());
        if (duration > 0 && !closed.empty()) {
            const int64_t closed_n   = static_cast<int64_t>(closed.size());
            const int64_t v_from     = std::max<int64_t>(0, (view_left  - first_ts) / duration);
            const int64_t v_to_excl  = std::min<int64_t>(closed_n,
                std::max<int64_t>(0, (view_right - first_ts) / duration + 1));
            for (int64_t i = v_from; i < v_to_excl; ++i) {
                const auto& buoy = closed[static_cast<std::size_t>(i)];
                p_min = std::min(p_min, buoy.min.raw_at(pd));
                p_max = std::max(p_max, buoy.max.raw_at(pd));
            }
        }
        const auto active = mQuotes.active_candle();
        if (active.volume.raw() > 0) {
            p_min = std::min(p_min, active.min.raw_at(pd));
            p_max = std::max(p_max, active.max.raw_at(pd));
        } else if (active.mean.raw() > 0) {
            // Empty active candle still represents a real price level (= last trade
            // price carried forward by AppendTrades fill-forward) — include it so a
            // long no-trade gap keeps the carried-forward dash on screen.
            p_min = std::min(p_min, active.mean.raw_at(pd));
            p_max = std::max(p_max, active.mean.raw_at(pd));
        }
    }
    if (p_min == std::numeric_limits<uint64_t>::max()) return;  // no data yet — leave matrix as-is

    // Refloor when the live data either escapes the current window (expansion)
    //    or sits inside less than half of it (contraction — happens after a price
    //    spike scrolls off-screen). The contraction guard requires data_range > 0
    //    so a flat zero-range visible window does not retrigger on every frame.
    //    After a refloor the data fills ~71 % of the new window (range + 2·margin
    //    where margin = range/5), well above the 50 % contraction threshold, so
    //    the hysteresis is self-stable.
    const bool window_valid = mScaleTopPrice > mScaleFloorPrice;
    const bool data_inside  = window_valid && p_min >= mScaleFloorPrice && p_max <= mScaleTopPrice;
    const uint64_t window_range = window_valid ? (mScaleTopPrice - mScaleFloorPrice) : 0;
    const uint64_t data_range   = (p_max >= p_min) ? (p_max - p_min) : 0;
    const bool data_too_tight   = window_valid && data_range > 0 && data_range * 2 < window_range;
    if (!data_inside || data_too_tight) {
        if (p_max == p_min) {
            const uint64_t pad = std::max<uint64_t>(1, p_max / 200);
            p_min -= std::min(pad, p_min);
            p_max += pad;
        }
        const uint64_t range  = p_max - p_min;
        const uint64_t margin = std::max<uint64_t>(1, range / 5);  // 20 % visual padding
        mScaleFloorPrice = p_min > margin ? p_min - margin : 0;
        mScaleTopPrice   = p_max + margin;

        SceneFloor floor = panel.GetSceneFloor();
        if (floor.price_points != mScaleFloorPrice) {
            floor.price_points = mScaleFloorPrice;
            panel.SetSceneFloor(floor);  // QuoteScratcher::OnLayout detects this and re-emits closed shapes
        }
    }
}

void QuoteScratcher::TimeFloorRefloor(InstrumentPanel& panel)
{
    // The LogicalScene X transform renders geometry as x = e11·(t−floor) + e13 in float32, and
    // the large (·−floor) term cancels — so the worst on-screen rounding is
    // ~ e11 · (view_right − floor) · 2⁻²⁴ px. Refloor only when that projected error would
    // exceed a sub-pixel tolerance: for normal periods this is days/years of continuous scroll
    // apart, otherwise triggering on zoom (an e11 change), so steady scrolling never rebuilds
    // geometry. The new floor sits one span behind view_left so a little left-pan stays precise.
    const int64_t view_left  = panel.ViewLeftTimeMs();
    const int64_t period_ms  = static_cast<int64_t>(panel.CandlePeriod().count()) * 1000;
    const int64_t cwidth     = std::max<int64_t>(1, panel.CandleWidth());
    const int64_t inner_w    = std::max<int64_t>(1, panel.InnerDataRect().w);
    const int64_t span_ms    = (inner_w * period_ms) / cwidth;
    const int64_t view_right = view_left + span_ms;

    const tvg::Matrix m      = panel.LogicalScene().transform();
    const double  e11        = std::abs(static_cast<double>(m.e11));
    const int64_t floor_ms   = static_cast<int64_t>(panel.GetSceneFloor().time_ms);

    constexpr double kFloat32Ulp   = 1.0 / 16777216.0;   // 2⁻²⁴
    constexpr double kRefloorTolPx = 0.5;
    const double err_px = e11 * static_cast<double>(std::max<int64_t>(0, view_right - floor_ms)) * kFloat32Ulp;

    const bool precision_breach = err_px > kRefloorTolPx;
    const bool floor_ahead      = floor_ms > view_left;   // floor must stay at/behind the left edge
    if (precision_breach || floor_ahead) {
        SceneFloor sf = panel.GetSceneFloor();
        sf.time_ms = static_cast<uint64_t>(std::max<int64_t>(0, view_left - span_ms));
        panel.SetSceneFloor(sf);
    }
}

void QuoteScratcher::CalculateSize(InstrumentPanel& panel)
{
    // Time/scroll path: advance the live edge and keep the transform consistent with the
    // current layout. NO trade ingestion or price-window rescan — those are data-driven
    // (IngestAndScale). Everything here is gated so a steady tick mutates nothing expensive.

    // Advance the candle clock: fill-forward empty buoys + roll the active candle to now.
    // Cheap unless a buoy boundary was crossed.
    mQuotes.AdvanceTo(NowMs(), mLastPrice);

    // Defensive precision refloor of the time axis (replaces the old per-2-span hysteresis).
    TimeFloorRefloor(panel);

    // Re-derive the price-axis scale for the current inner height — this is what makes a resize
    // (height change without new data) take effect. The price window itself is owned by the
    // data path; skip until it exists. ApplyLogicalSceneTransform (run right after) preserves
    // whatever e22 we leave in the matrix.
    if (mScaleTopPrice > mScaleFloorPrice) {
        const float H = static_cast<float>(std::max<int64_t>(1, panel.InnerDataRect().h));
        const float e22 = H / static_cast<float>(mScaleTopPrice - mScaleFloorPrice);
        const tvg::Matrix cur = panel.LogicalScene().transform();
        if (cur.e22 != e22) {
            panel.LogicalScene().transform(tvg::Matrix{cur.e11, 0.0f, cur.e13,
                                                        0.0f,    e22,  cur.e23,
                                                        0.0f,    0.0f, 1.0f});
        }
    }
}

void QuoteScratcher::OnLayout(InstrumentPanel& panel)
{
    if (!mScene) return;

    const auto first_ts = mQuotes.first_buoy_timestamp();
    const SceneFloor& floor = panel.GetSceneFloor();
    const ScenePixelSize px = panel.PixelSizeOf(panel.LogicalScene());
    const float candle_w_px = static_cast<float>(panel.CandleWidth());
    const std::size_t pd = panel.PriceDecimals();

    // Refresh the emitted calibration only when a visible-window median drifts beyond
    // the ~10 % hysteresis band, so widths stay steady while the window's contents creep.
    // An empty fresh calibration (no eligible visible buoy) never replaces an adopted
    // one — special/degenerate periods draw fixed-size markers that need no calibration.
    const auto fresh = VisibleCalibration(panel, mQuotes);
    bool calibration_changed = false;
    if (fresh.median_volume.raw() != 0 &&
        (!mEmittedCalibration ||
         DriftedBeyondTenth(mEmittedCalibration->median_volume, fresh.median_volume) ||
         DriftedBeyondTenth(mEmittedCalibration->median_sigma_sum, fresh.median_sigma_sum))) {
        mEmittedCalibration = fresh;
        calibration_changed = true;
    }
    const BuoyCandleQuotes::Calibration calibration = mEmittedCalibration ? *mEmittedCalibration : BuoyCandleQuotes::Calibration{};

    // Closed-pool invalidation: any of (a) series anchor moved, (b) scene floor
    // repositioned, (c) scene pixel size changed on EITHER axis — the prefiltered line
    // thicknesses, the gray dash half-height and the ASPECT regime are all (k * px)-
    // derived, and px.x is the inverse of e11, which the panel derives from the candle
    // width, so tracking it also catches the candle-width-driven slot clamp — (d)
    // calibration adopted, every closed width consumes it. Pan and vertical resize are
    // absorbed by the LogicalScene matrix and do NOT invalidate.
    const bool series_changed = !first_ts || mEmittedFirstBuoyTs != first_ts;
    const bool floor_changed  = floor.time_ms != mEmittedFloorTimeMs ||
                                floor.price_points != mEmittedFloorPricePts;
    const bool px_changed     = px.x != mEmittedPxSizeX || px.y != mEmittedPxSizeY;
    if (series_changed || floor_changed || px_changed || calibration_changed) {
        ResetPool(mClosedShapes);
        mEmittedClosedCount = 0;
        mEmittedFirstBuoyTs = first_ts;
        mEmittedFloorTimeMs = floor.time_ms;
        mEmittedFloorPricePts = floor.price_points;
        mEmittedPxSizeX = px.x;
        mEmittedPxSizeY = px.y;
    }

    // Active pool is always replaced — at most one buoy across its pooled shapes.
    ResetPool(mActiveShapes);

    if (!first_ts) return;

    const uint64_t duration = mQuotes.buoy_duration();
    const auto& closed = mQuotes.quotes();

    const std::size_t n = closed.size();
    for (std::size_t i = mEmittedClosedCount; i < n; ++i) {
        const uint64_t ts = *first_ts + i * duration;
        const auto& curr = closed[i];
        // Color against the last FILLED buoy, skipping carried-forward empty buoys
        // whose extents collapse onto the previous close. With no filled predecessor
        // (series anchor) compare against itself — paints neutral (green via `>=`).
        const auto* prev_filled = PrevFilledBuoy(closed, i);
        const auto& prev = prev_filled ? *prev_filled : curr;
        AppendBuoy(mClosedShapes, ts, duration, curr, prev, calibration, floor, px, candle_w_px, pd);
    }
    mEmittedClosedCount = n;

    const auto active = mQuotes.active_candle();
    const uint64_t active_ts = *first_ts + n * duration;
    const auto* prev_filled = PrevFilledBuoy(closed, n);
    BuoyCandleQuotes::candle_t prev = prev_filled ? *prev_filled : active;
    AppendBuoy(mActiveShapes, active_ts, duration, active, prev, calibration, floor, px, candle_w_px, pd);
}

}
