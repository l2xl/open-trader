// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "quote_scratcher.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include "instrument_panel.hpp"
#include "timedef.hpp"

namespace scratcher::cockpit {

namespace {

struct Color { uint8_t r, g, b, a; };

// Dark green/red fill the bell body; the waist diamond uses the brighter same-hue
// derivatives so it pops as a crisp anchor on top of the darker contour. The special
// lozenge is the deliberately foreign high-contrast amber of the case the bell cannot
// express.
constexpr Color kBodyGreen{0, 62, 0, 255};      // #003e00
constexpr Color kBodyRed{62, 0, 0, 255};        // #3e0000
constexpr Color kDiamondGreen{0, 95, 0, 255};   // #005f00
constexpr Color kDiamondRed{95, 0, 0, 255};     // #5f0000
constexpr Color kSpecial{255, 191, 0, 255};     // #ffbf00
constexpr Color kGray{110, 110, 110, 255};

// BUOY_CANDLE.md section 8 render-side tunables: the median-volume buoy's full waist
// width, the horizontal clearance between neighbouring slots, the contour sample count
// (N = 12 wall levels), and the aspect ratio below which a period becomes special.
constexpr float kTargetWaistWidthPx = 14.f;
constexpr float kSlotGapPx = 2.f;
constexpr std::size_t kContourLevels = 13;
constexpr float kAspect = 2.f;

inline float SubToFloat(uint64_t value, uint64_t floor)
{
    return static_cast<float>(static_cast<int64_t>(value) - static_cast<int64_t>(floor));
}

void ApplyFill(tvg::Shape& shape, Color c)
{
    shape.fill(c.r, c.g, c.b, c.a);
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

// Color-grouped Shape pools one buoy emits into; references borrow the scratcher's
// pooled shapes for the duration of a single AppendBuoy call.
struct BuoyShapes
{
    tvg::Shape& gray;
    tvg::Shape& body_green;
    tvg::Shape& body_red;
    tvg::Shape& diamond_green;
    tvg::Shape& diamond_red;
    tvg::Shape& special;
};

// Emit one buoy into its Shape pools per BUOY_CANDLE.md. Filled geometry only:
//   * the BODY is two independently coloured half-bells, each ONE closed contour
//     (flat waist edge at ±A + that side's Gaussian wall, Catmull-Rom smoothed) plus
//     a 1 px waist-to-extreme spine keeping the range visible where the 3σ taper
//     thins below a pixel; waist half-width A = (Wt/2)·WidthRatio px clamped to the
//     slot; the upper half colours by curr.max vs prev.max, the lower half by
//     curr.min vs prev.min;
//   * the waist diamond is candle_width px wide × (candle_width / 2) px tall,
//     coloured by curr.mean vs prev.mean;
//   * SPECIAL periods (single-price, or failing the screen-space ASPECT test) draw
//     only the fixed-size bright lozenge;
//   * the empty-buoy gray dash is candle_width px wide × 0.5 px tall.
// X dimensions are pixel-stable through the LogicalScene matrix because period_ms
// maps to candle_width px via e11; Y dimensions that need a fixed pixel count are
// supplied in scene units as (pixels * px.y).
void AppendBuoy(BuoyShapes shapes,
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
    const float left_x  = SubToFloat(buoy_ts, floor.time_ms);
    const float right_x = SubToFloat(buoy_ts + duration, floor.time_ms);
    const float mid_x   = 0.5f * (left_x + right_x);
    const float mean_y  = SubToFloat(curr.mean.raw_at(price_decimals), floor.price_points);

    if (curr.volume.raw() == 0) {
        // Empty buoy — no trades arrived during the period. Carry the previous last
        // price forward as a 0.5 px-tall gray rect; in this state the model has
        // min == max == mean == last_price, so no body would have any visible extent.
        const float half_h = 0.25f * px.y;
        shapes.gray.moveTo(left_x,  mean_y - half_h);
        shapes.gray.lineTo(right_x, mean_y - half_h);
        shapes.gray.lineTo(right_x, mean_y + half_h);
        shapes.gray.lineTo(left_x,  mean_y + half_h);
        shapes.gray.close();
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
        shapes.gray.moveTo(mid_x - half_w, prev_close_y);
        shapes.gray.lineTo(mid_x + half_w, prev_close_y);
        shapes.gray.lineTo(mid_x + half_w, mean_y);
        shapes.gray.lineTo(mid_x - half_w, mean_y);
        shapes.gray.close();
    }

    const float half_diamond_h = (candle_width_px * 0.25f) * px.y;

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

    // Screen-space regime test (BUOY_CANDLE.md section 5): a single-price period is
    // special by construction; otherwise the buoy is special when even its longer half
    // is shorter than ASPECT body widths on screen — the bell cannot be read at that
    // aspect, so a fixed-size high-contrast lozenge is stamped instead and nothing
    // height-scaled is drawn.
    const float top_px = static_cast<float>((curr.max - curr.mean).raw_at(price_decimals)) / px.y;
    const float bot_px = static_cast<float>((curr.mean - curr.min).raw_at(price_decimals)) / px.y;
    if (single_price || std::max(top_px, bot_px) < kAspect * 2.f * a_px) {
        shapes.special.moveTo(left_x,  mean_y);
        shapes.special.lineTo(mid_x,   mean_y + half_diamond_h);
        shapes.special.lineTo(right_x, mean_y);
        shapes.special.lineTo(mid_x,   mean_y - half_diamond_h);
        shapes.special.close();
        return;
    }

    // Body: each half is ONE closed filled sub-path — the straight waist edge at ±A plus
    // the Gaussian wall of that side, smoothed as two per-wall Catmull-Rom chains meeting
    // at the tip so the tip stays a sharp corner. A per-half range spine (1 px rect from
    // waist to that half's extreme) is emitted first in the SAME traversal order — start
    // at the right of the waist, out to the extreme, back on the left — so both sub-paths
    // always share one winding and the nonzero fill rule unions them; composing a buoy
    // from independent halves is what makes cross-winding cancellation (the carved centre
    // slit of the retired whole-body path) structurally impossible. Colors per the
    // notation's channels: the upper half by curr.max vs prev.max, the lower half by
    // curr.min vs prev.min. A zero-height half (waist ON the extreme) is skipped — the
    // other half then reads as the flat-waisted half-bell of BUOY_CANDLE.md section 5.
    const int64_t mean_raw = static_cast<int64_t>(curr.mean.raw_at(price_decimals));
    const float mean_level_y = SubToFloat(curr.mean.raw_at(price_decimals), floor.price_points);
    const float spine_half_w = 0.5f * px.x;
    const auto append_half = [&](bool upper) {
        const auto& extreme = upper ? curr.max : curr.min;
        const int64_t extreme_raw = static_cast<int64_t>(extreme.raw_at(price_decimals));
        if (extreme_raw == mean_raw) return;
        const bool grown = upper ? !(curr.max < prev.max) : !(curr.min < prev.min);
        tvg::Shape& shape = grown ? shapes.body_green : shapes.body_red;
        const float extreme_y = SubToFloat(extreme.raw_at(price_decimals), floor.price_points);

        shape.moveTo(mid_x + spine_half_w, mean_level_y);
        shape.lineTo(mid_x + spine_half_w, extreme_y);
        shape.lineTo(mid_x - spine_half_w, extreme_y);
        shape.lineTo(mid_x - spine_half_w, mean_level_y);
        shape.close();

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
        shape.moveTo(outline.front().x, outline.front().y);
        for (const auto& segment : CatmullRomSpline(right_wall, false))
            shape.cubicTo(segment.control1.x, segment.control1.y, segment.control2.x, segment.control2.y, segment.end.x, segment.end.y);
        for (const auto& segment : CatmullRomSpline(left_wall, false))
            shape.cubicTo(segment.control1.x, segment.control1.y, segment.control2.x, segment.control2.y, segment.end.x, segment.end.y);
        shape.close();
    };
    append_half(true);
    append_half(false);

    // Waist diamond on top of the body. Vertices: left tip at (left, mean), top at
    // (mid, mean+halfH), right tip at (right, mean), bottom at (mid, mean-halfH) —
    // full height = candle_width / 2 px regardless of the price-axis scale.
    auto& diamond_shape = (curr.mean >= prev.mean) ? shapes.diamond_green : shapes.diamond_red;
    diamond_shape.moveTo(left_x,  mean_y);
    diamond_shape.lineTo(mid_x,   mean_y + half_diamond_h);
    diamond_shape.lineTo(right_x, mean_y);
    diamond_shape.lineTo(mid_x,   mean_y - half_diamond_h);
    diamond_shape.close();
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

    mClosedGrayShape.reset(tvg::Shape::gen());
    mClosedBodyGreenShape.reset(tvg::Shape::gen());
    mClosedBodyRedShape.reset(tvg::Shape::gen());
    mClosedDiamondGreenShape.reset(tvg::Shape::gen());
    mClosedDiamondRedShape.reset(tvg::Shape::gen());
    mClosedSpecialShape.reset(tvg::Shape::gen());

    mActiveGrayShape.reset(tvg::Shape::gen());
    mActiveBodyGreenShape.reset(tvg::Shape::gen());
    mActiveBodyRedShape.reset(tvg::Shape::gen());
    mActiveDiamondGreenShape.reset(tvg::Shape::gen());
    mActiveDiamondRedShape.reset(tvg::Shape::gen());
    mActiveSpecialShape.reset(tvg::Shape::gen());

    ApplyFill(*mClosedGrayShape,         kGray);
    ApplyFill(*mClosedBodyGreenShape,    kBodyGreen);
    ApplyFill(*mClosedBodyRedShape,      kBodyRed);
    ApplyFill(*mClosedDiamondGreenShape, kDiamondGreen);
    ApplyFill(*mClosedDiamondRedShape,   kDiamondRed);
    ApplyFill(*mClosedSpecialShape,      kSpecial);
    ApplyFill(*mActiveGrayShape,         kGray);
    ApplyFill(*mActiveBodyGreenShape,    kBodyGreen);
    ApplyFill(*mActiveBodyRedShape,      kBodyRed);
    ApplyFill(*mActiveDiamondGreenShape, kDiamondGreen);
    ApplyFill(*mActiveDiamondRedShape,   kDiamondRed);
    ApplyFill(*mActiveSpecialShape,      kSpecial);

    // Z-order (add() order): gray first so the "move" connector renders UNDER the buoy —
    // it runs all the way to mean but the body covers its inner segment, leaving only the
    // part outside [min, max] (previous close → nearest tip) visible as a stem with no
    // gap. Bell bodies next, then the waist diamonds, and the special lozenges on
    // top. Empty-buoy dashes also live in the gray pool but sit where
    // no buoy is drawn, so their layer placement is immaterial. Closed-pool shapes are
    // grouped contiguously ahead of all active-pool shapes.
    mScene->add(mClosedGrayShape.get());
    mScene->add(mClosedBodyGreenShape.get());
    mScene->add(mClosedBodyRedShape.get());
    mScene->add(mClosedDiamondGreenShape.get());
    mScene->add(mClosedDiamondRedShape.get());
    mScene->add(mClosedSpecialShape.get());

    mScene->add(mActiveGrayShape.get());
    mScene->add(mActiveBodyGreenShape.get());
    mScene->add(mActiveBodyRedShape.get());
    mScene->add(mActiveDiamondGreenShape.get());
    mScene->add(mActiveDiamondRedShape.get());
    mScene->add(mActiveSpecialShape.get());

    panel.LogicalScene().add(mScene.get());
}

void QuoteScratcher::OnDetach(InstrumentPanel& /*panel*/)
{
    mScene.reset();

    mClosedGrayShape.reset();
    mClosedBodyGreenShape.reset();
    mClosedBodyRedShape.reset();
    mClosedDiamondGreenShape.reset();
    mClosedDiamondRedShape.reset();
    mClosedSpecialShape.reset();

    mActiveGrayShape.reset();
    mActiveBodyGreenShape.reset();
    mActiveBodyRedShape.reset();
    mActiveDiamondGreenShape.reset();
    mActiveDiamondRedShape.reset();
    mActiveSpecialShape.reset();

    mEmittedClosedCount = 0;
    mEmittedFirstBuoyTs.reset();
    mEmittedFloorTimeMs = 0;
    mEmittedFloorPricePts = 0;
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
    // repositioned, (c) Y pixel size changed (pixel-fixed heights and the ASPECT
    // regime are derived from px.y), (d) calibration adopted — every closed width
    // consumes it. Pan/zoom/resize on X are absorbed by the LogicalScene matrix and
    // do NOT invalidate.
    const bool series_changed = !first_ts || mEmittedFirstBuoyTs != first_ts;
    const bool floor_changed  = floor.time_ms != mEmittedFloorTimeMs ||
                                floor.price_points != mEmittedFloorPricePts;
    const bool px_changed     = px.y != mEmittedPxSizeY;
    if (series_changed || floor_changed || px_changed || calibration_changed) {
        mClosedGrayShape->reset();
        mClosedBodyGreenShape->reset();
        mClosedBodyRedShape->reset();
        mClosedDiamondGreenShape->reset();
        mClosedDiamondRedShape->reset();
        mClosedSpecialShape->reset();
        ApplyFill(*mClosedGrayShape,         kGray);
        ApplyFill(*mClosedBodyGreenShape,    kBodyGreen);
        ApplyFill(*mClosedBodyRedShape,      kBodyRed);
        ApplyFill(*mClosedDiamondGreenShape, kDiamondGreen);
        ApplyFill(*mClosedDiamondRedShape,   kDiamondRed);
        ApplyFill(*mClosedSpecialShape,      kSpecial);
        mEmittedClosedCount = 0;
        mEmittedFirstBuoyTs = first_ts;
        mEmittedFloorTimeMs = floor.time_ms;
        mEmittedFloorPricePts = floor.price_points;
        mEmittedPxSizeY = px.y;
    }

    // Active pool is always replaced — at most one buoy across the seven pooled shapes.
    mActiveGrayShape->reset();
    mActiveBodyGreenShape->reset();
    mActiveBodyRedShape->reset();
    mActiveDiamondGreenShape->reset();
    mActiveDiamondRedShape->reset();
    mActiveSpecialShape->reset();
    ApplyFill(*mActiveGrayShape,         kGray);
    ApplyFill(*mActiveBodyGreenShape,    kBodyGreen);
    ApplyFill(*mActiveBodyRedShape,      kBodyRed);
    ApplyFill(*mActiveDiamondGreenShape, kDiamondGreen);
    ApplyFill(*mActiveDiamondRedShape,   kDiamondRed);
    ApplyFill(*mActiveSpecialShape,      kSpecial);

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
        AppendBuoy({*mClosedGrayShape, *mClosedBodyGreenShape, *mClosedBodyRedShape,
                    *mClosedDiamondGreenShape, *mClosedDiamondRedShape, *mClosedSpecialShape},
                   ts, duration, curr, prev, calibration, floor, px, candle_w_px, pd);
    }
    mEmittedClosedCount = n;

    const auto active = mQuotes.active_candle();
    const uint64_t active_ts = *first_ts + n * duration;
    const auto* prev_filled = PrevFilledBuoy(closed, n);
    BuoyCandleQuotes::candle_t prev = prev_filled ? *prev_filled : active;
    AppendBuoy({*mActiveGrayShape, *mActiveBodyGreenShape, *mActiveBodyRedShape,
                *mActiveDiamondGreenShape, *mActiveDiamondRedShape, *mActiveSpecialShape},
               active_ts, duration, active, prev, calibration, floor, px, candle_w_px, pd);
}

}
