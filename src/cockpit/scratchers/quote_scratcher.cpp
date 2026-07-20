// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "quote_scratcher.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include "instrument_panel.hpp"
#include "timedef.hpp"

namespace scratcher::cockpit {

namespace {

struct Color { uint8_t r, g, b, a; };

// Pure dark green/red sampled from the reference swatch, used for the wick triangles
// (the high/low reach). The mean-price diamond uses ~65 %-luminance derivatives so the
// body grounds as a darker anchor while staying the same hue — triangles read brighter
// than the body, keeping the high/low pop above the mean.
constexpr Color kWickGreen{0, 95, 0, 255};     // #005f00
constexpr Color kWickRed{95, 0, 0, 255};       // #5f0000
constexpr Color kBodyGreen{0, 62, 0, 255};     // #003e00
constexpr Color kBodyRed{62, 0, 0, 255};       // #3e0000
constexpr Color kGray{110, 110, 110, 255};

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

// Emit one buoy into its color-grouped Shape pools. Filled geometry only (no strokes),
// sized so that:
//   * the diamond body is candle_width px wide × (candle_width / 2) px tall,
//   * the wick triangles span the full buoy width at their base and reach the real
//     curr.max / curr.min price at their apex,
//   * the empty-buoy gray dash is candle_width px wide × 0.5 px tall.
// X dimensions are pixel-stable through the LogicalScene matrix because period_ms
// maps to candle_width px via e11; Y dimensions that need a fixed pixel count are
// supplied in scene units as (pixels * px.y).
void AppendBuoy(tvg::Shape& wicks_green, tvg::Shape& wicks_red,
                tvg::Shape& body_green,  tvg::Shape& body_red,
                tvg::Shape& gray,
                uint64_t buoy_ts, uint64_t duration,
                const BuoyCandleQuotes::candle_t& curr,
                const BuoyCandleQuotes::candle_t& prev,
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
        // min == max == mean == last_price, so neither wicks nor a body would have
        // any visible extent anyway.
        const float half_h = 0.25f * px.y;
        gray.moveTo(left_x,  mean_y - half_h);
        gray.lineTo(right_x, mean_y - half_h);
        gray.lineTo(right_x, mean_y + half_h);
        gray.lineTo(left_x,  mean_y + half_h);
        gray.close();
        return;
    }

    const float min_y = SubToFloat(curr.min.raw_at(price_decimals), floor.price_points);
    const float max_y = SubToFloat(curr.max.raw_at(price_decimals), floor.price_points);

    // Flatten each wick apex into a 0.5 px-wide horizontal edge so the tip reads as a
    // crisp short cap rather than a needle-thin point that the rasterizer thins to
    // near-invisibility. Pixel-stable through the scene matrix (period_ms → candle_width
    // px via e11), so the cap stays 0.5 px regardless of zoom.
    const float tip_half_w = 0.25f * px.x;

    // Top wick: trapezium with the long base from (left, mean) → (right, mean) and a
    // short top edge spanning (mid ± tip_half_w, max). Color by curr.max vs prev.max.
    auto& top_shape = (curr.max >= prev.max) ? wicks_green : wicks_red;
    top_shape.moveTo(left_x, mean_y);
    top_shape.lineTo(mid_x - tip_half_w, max_y);
    top_shape.lineTo(mid_x + tip_half_w, max_y);
    top_shape.lineTo(right_x, mean_y);
    top_shape.close();

    // Bottom wick: trapezium mirroring the top — short bottom edge spanning
    // (mid ± tip_half_w, min). Color by curr.min vs prev.min. Wound the opposite way so
    // both shapes use a consistent fill rule under any future winding-sensitive setting.
    auto& bot_shape = (curr.min >= prev.min) ? wicks_green : wicks_red;
    bot_shape.moveTo(left_x, mean_y);
    bot_shape.lineTo(right_x, mean_y);
    bot_shape.lineTo(mid_x + tip_half_w, min_y);
    bot_shape.lineTo(mid_x - tip_half_w, min_y);
    bot_shape.close();

    // Mean body diamond. Vertices: left tip at (left, mean), top at (mid, mean+halfH),
    // right tip at (right, mean), bottom at (mid, mean-halfH). Half-height is one
    // quarter of the candle width in canvas px so the full diamond height equals
    // candle_width / 2 px regardless of the price-axis scale.
    const float half_diamond_h = (candle_width_px * 0.25f) * px.y;
    auto& body_shape = (curr.mean >= prev.mean) ? body_green : body_red;
    body_shape.moveTo(left_x,  mean_y);
    body_shape.lineTo(mid_x,   mean_y + half_diamond_h);
    body_shape.lineTo(right_x, mean_y);
    body_shape.lineTo(mid_x,   mean_y - half_diamond_h);
    body_shape.close();

    // Gray "move" connector: a thin vertical line bridging the previous close to this
    // buoy's mean level, drawn only when the previous close sits outside [min, max] — i.e.
    // the price jumped into a new band between periods. The candle no longer encodes that
    // move (min/max reflect only this period's own trades), so the connector is what makes
    // a gap between consecutive buoys visible. The rect spans down to mean so it meets the
    // candle with no gap, but the gray pool is drawn UNDER the wicks and body (see OnAttach
    // Z-order), so the inner segment is hidden and only the part outside [min, max]
    // (previous close → nearest tip) shows — a stem from the prior close to the candle edge.
    if (prev.close > curr.max || prev.close < curr.min) {
        const float prev_close_y = SubToFloat(prev.close.raw_at(price_decimals), floor.price_points);
        // 1 px wide (not 0.5 px). A sub-pixel-width filled rect distributes its
        // anti-aliased coverage differently as its screen-x sweeps sub-pixel positions
        // under scroll — one ~50% column vs two ~25% columns — which the eye reads as a
        // flickering change in thickness/brightness. At a full pixel the covered ink is
        // constant across sub-pixel offsets, so the connector stays visually steady.
        const float half_w  = 0.5f * px.x;
        gray.moveTo(mid_x - half_w, prev_close_y);
        gray.lineTo(mid_x + half_w, prev_close_y);
        gray.lineTo(mid_x + half_w, mean_y);
        gray.lineTo(mid_x - half_w, mean_y);
        gray.close();
    }
}

}

void QuoteScratcher::OnAttach(InstrumentPanel& panel)
{
    mScene.reset(tvg::Scene::gen());

    mClosedGrayShape.reset(tvg::Shape::gen());
    mClosedWicksGreenShape.reset(tvg::Shape::gen());
    mClosedWicksRedShape.reset(tvg::Shape::gen());
    mClosedBodyGreenShape.reset(tvg::Shape::gen());
    mClosedBodyRedShape.reset(tvg::Shape::gen());

    mActiveGrayShape.reset(tvg::Shape::gen());
    mActiveWicksGreenShape.reset(tvg::Shape::gen());
    mActiveWicksRedShape.reset(tvg::Shape::gen());
    mActiveBodyGreenShape.reset(tvg::Shape::gen());
    mActiveBodyRedShape.reset(tvg::Shape::gen());

    ApplyFill(*mClosedGrayShape,        kGray);
    ApplyFill(*mClosedWicksGreenShape,  kWickGreen);
    ApplyFill(*mClosedWicksRedShape,    kWickRed);
    ApplyFill(*mClosedBodyGreenShape,   kBodyGreen);
    ApplyFill(*mClosedBodyRedShape,     kBodyRed);
    ApplyFill(*mActiveGrayShape,        kGray);
    ApplyFill(*mActiveWicksGreenShape,  kWickGreen);
    ApplyFill(*mActiveWicksRedShape,    kWickRed);
    ApplyFill(*mActiveBodyGreenShape,   kBodyGreen);
    ApplyFill(*mActiveBodyRedShape,     kBodyRed);

    // Z-order (add() order): gray first so the "move" connector renders UNDER the buoy —
    // it runs all the way to mean but the wicks and body cover its inner segment, leaving
    // only the part outside [min, max] (previous close → nearest tip) visible as a stem
    // with no gap. Wicks next, then the diamond bodies on top so they cap the triangle
    // bases meeting at mean. Empty-buoy dashes also live in the gray pool but sit where no
    // buoy is drawn, so their layer placement is immaterial. Closed-pool shapes are grouped
    // contiguously ahead of all active-pool shapes.
    mScene->add(mClosedGrayShape.get());
    mScene->add(mClosedWicksGreenShape.get());
    mScene->add(mClosedWicksRedShape.get());
    mScene->add(mClosedBodyGreenShape.get());
    mScene->add(mClosedBodyRedShape.get());

    mScene->add(mActiveGrayShape.get());
    mScene->add(mActiveWicksGreenShape.get());
    mScene->add(mActiveWicksRedShape.get());
    mScene->add(mActiveBodyGreenShape.get());
    mScene->add(mActiveBodyRedShape.get());

    panel.LogicalScene().add(mScene.get());
}

void QuoteScratcher::OnDetach(InstrumentPanel& /*panel*/)
{
    mScene.reset();

    mClosedGrayShape.reset();
    mClosedWicksGreenShape.reset();
    mClosedWicksRedShape.reset();
    mClosedBodyGreenShape.reset();
    mClosedBodyRedShape.reset();

    mActiveGrayShape.reset();
    mActiveWicksGreenShape.reset();
    mActiveWicksRedShape.reset();
    mActiveBodyGreenShape.reset();
    mActiveBodyRedShape.reset();

    mEmittedClosedCount = 0;
    mEmittedFirstBuoyTs.reset();
    mEmittedFloorTimeMs = 0;
    mEmittedFloorPricePts = 0;
    mEmittedPxSizeY = 0.0f;
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
    const int64_t inner_w    = std::max(1, panel.InnerDataRect().width());
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
    const int64_t inner_w    = std::max(1, panel.InnerDataRect().width());
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
    mQuotes.AdvanceTo(WallNowMs(), mLastPrice);

    // Defensive precision refloor of the time axis (replaces the old per-2-span hysteresis).
    TimeFloorRefloor(panel);

    // Re-derive the price-axis scale for the current inner height — this is what makes a resize
    // (height change without new data) take effect. The price window itself is owned by the
    // data path; skip until it exists. ApplyLogicalSceneTransform (run right after) preserves
    // whatever e22 we leave in the matrix.
    if (mScaleTopPrice > mScaleFloorPrice) {
        const int H = std::max(1, panel.InnerDataRect().height());
        const float e22 = static_cast<float>(H) / static_cast<float>(mScaleTopPrice - mScaleFloorPrice);
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

    // Closed-pool invalidation: any of (a) series anchor moved, (b) scene floor
    // repositioned, (c) Y pixel size changed (diamond half-height and gray dash
    // half-height are both derived from px.y and would otherwise drift out of
    // their nominal pixel dimensions). Pan/zoom/resize on X are absorbed by the
    // LogicalScene matrix and do NOT invalidate.
    const bool series_changed = !first_ts || mEmittedFirstBuoyTs != first_ts;
    const bool floor_changed  = floor.time_ms != mEmittedFloorTimeMs ||
                                floor.price_points != mEmittedFloorPricePts;
    const bool px_changed     = px.y != mEmittedPxSizeY;
    if (series_changed || floor_changed || px_changed) {
        mClosedGrayShape->reset();
        mClosedWicksGreenShape->reset();
        mClosedWicksRedShape->reset();
        mClosedBodyGreenShape->reset();
        mClosedBodyRedShape->reset();
        ApplyFill(*mClosedGrayShape,       kGray);
        ApplyFill(*mClosedWicksGreenShape, kWickGreen);
        ApplyFill(*mClosedWicksRedShape,   kWickRed);
        ApplyFill(*mClosedBodyGreenShape,  kBodyGreen);
        ApplyFill(*mClosedBodyRedShape,    kBodyRed);
        mEmittedClosedCount = 0;
        mEmittedFirstBuoyTs = first_ts;
        mEmittedFloorTimeMs = floor.time_ms;
        mEmittedFloorPricePts = floor.price_points;
        mEmittedPxSizeY = px.y;
    }

    // Active pool is always replaced — at most one buoy, three sub-paths across two
    // color shapes plus the gray-dash branch.
    mActiveGrayShape->reset();
    mActiveWicksGreenShape->reset();
    mActiveWicksRedShape->reset();
    mActiveBodyGreenShape->reset();
    mActiveBodyRedShape->reset();
    ApplyFill(*mActiveGrayShape,       kGray);
    ApplyFill(*mActiveWicksGreenShape, kWickGreen);
    ApplyFill(*mActiveWicksRedShape,   kWickRed);
    ApplyFill(*mActiveBodyGreenShape,  kBodyGreen);
    ApplyFill(*mActiveBodyRedShape,    kBodyRed);

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
        AppendBuoy(*mClosedWicksGreenShape, *mClosedWicksRedShape,
                   *mClosedBodyGreenShape,  *mClosedBodyRedShape,
                   *mClosedGrayShape,
                   ts, duration, curr, prev, floor, px, candle_w_px, pd);
    }
    mEmittedClosedCount = n;

    const auto active = mQuotes.active_candle();
    const uint64_t active_ts = *first_ts + n * duration;
    const auto* prev_filled = PrevFilledBuoy(closed, n);
    BuoyCandleQuotes::candle_t prev = prev_filled ? *prev_filled : active;
    AppendBuoy(*mActiveWicksGreenShape, *mActiveWicksRedShape,
               *mActiveBodyGreenShape,  *mActiveBodyRedShape,
               *mActiveGrayShape,
               active_ts, duration, active, prev, floor, px, candle_w_px, pd);
}

}
