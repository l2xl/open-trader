// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#include "quote_scratcher.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

#include "bybit/entities/public_trade.hpp"
#include "currency.hpp"
#include "instrument_panel.hpp"
#include "timedef.hpp"

namespace scratcher::cockpit {

namespace {

struct Color { uint8_t r, g, b, a; };

constexpr Color kGreen{40, 200, 80, 255};
constexpr Color kRed{220, 50, 50, 255};
constexpr Color kGray{140, 140, 140, 255};

inline float SubToFloat(uint64_t value, uint64_t floor)
{
    return static_cast<float>(static_cast<int64_t>(value) - static_cast<int64_t>(floor));
}

void ApplyFill(tvg::Shape& shape, Color c)
{
    shape.fill(c.r, c.g, c.b, c.a);
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
                float candle_width_px)
{
    const float left_x  = SubToFloat(buoy_ts, floor.time_ms);
    const float right_x = SubToFloat(buoy_ts + duration, floor.time_ms);
    const float mid_x   = 0.5f * (left_x + right_x);
    const float mean_y  = SubToFloat(curr.mean, floor.price_points);

    if (curr.volume == 0) {
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

    const float min_y = SubToFloat(curr.min, floor.price_points);
    const float max_y = SubToFloat(curr.max, floor.price_points);

    // Top wick: apex at (mid, max), base from (left, mean) → (right, mean). Color by
    // comparison of curr.max against prev.max.
    auto& top_shape = (curr.max >= prev.max) ? wicks_green : wicks_red;
    top_shape.moveTo(left_x, mean_y);
    top_shape.lineTo(mid_x,  max_y);
    top_shape.lineTo(right_x, mean_y);
    top_shape.close();

    // Bottom wick: apex at (mid, min), base from (left, mean) → (right, mean). Color
    // by comparison of curr.min against prev.min. Wound the opposite way so both
    // triangles use a consistent fill rule under any future winding-sensitive setting.
    auto& bot_shape = (curr.min >= prev.min) ? wicks_green : wicks_red;
    bot_shape.moveTo(left_x, mean_y);
    bot_shape.lineTo(right_x, mean_y);
    bot_shape.lineTo(mid_x,  min_y);
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
    ApplyFill(*mClosedWicksGreenShape,  kGreen);
    ApplyFill(*mClosedWicksRedShape,    kRed);
    ApplyFill(*mClosedBodyGreenShape,   kGreen);
    ApplyFill(*mClosedBodyRedShape,     kRed);
    ApplyFill(*mActiveGrayShape,        kGray);
    ApplyFill(*mActiveWicksGreenShape,  kGreen);
    ApplyFill(*mActiveWicksRedShape,    kRed);
    ApplyFill(*mActiveBodyGreenShape,   kGreen);
    ApplyFill(*mActiveBodyRedShape,     kRed);

    // Z-order (add() order): wicks first so the diamond bodies sit on top and cap the
    // triangle bases meeting at mean. Gray rects (only ever drawn for empty buoys, which
    // by definition have no wicks/body for the same buoy) are inserted between the wick
    // and body layers — placement doesn't affect any visible overlap, but it groups all
    // "closed-pool" shapes contiguously ahead of all "active-pool" shapes.
    mScene->add(mClosedWicksGreenShape.get());
    mScene->add(mClosedWicksRedShape.get());
    mScene->add(mClosedGrayShape.get());
    mScene->add(mClosedBodyGreenShape.get());
    mScene->add(mClosedBodyRedShape.get());

    mScene->add(mActiveWicksGreenShape.get());
    mScene->add(mActiveWicksRedShape.get());
    mScene->add(mActiveGrayShape.get());
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

namespace {

// Pull the wire-format trades strictly newer than `after_ts` from the feed snapshot
// and adapt them into the three-field record the IngestTrades concept consumes.
// Malformed strings are skipped; the wire feed is best-effort.
struct PublicTradeRecord
{
    time_point trade_time;
    uint64_t   price_points;
    uint64_t   volume_points;
};

std::vector<PublicTradeRecord> PullAdapted(
    const std::deque<bybit::PublicTrade>& snapshot,
    uint64_t after_ts,
    std::size_t price_decimals,
    std::size_t size_decimals)
{
    auto begin = snapshot.begin();
    const auto end = snapshot.end();
    if (after_ts > 0) {
        begin = std::upper_bound(begin, end, after_ts,
            [](uint64_t v, const bybit::PublicTrade& t) {
                try { return v < static_cast<uint64_t>(std::stoll(t.time)); }
                catch (...) { return false; }
            });
    }
    std::vector<PublicTradeRecord> out;
    if (begin == end) return out;
    out.reserve(static_cast<std::size_t>(std::distance(begin, end)));
    for (auto it = begin; it != end; ++it) {
        try {
            // Construct the time_point directly from the wire-ms count rather
            // than going through utc_clock::from_sys, which would add the
            // current leap-second offset (~27 s in 2026) and de-sync our
            // last_seen bookmark from the feed's wire timestamps.
            out.push_back(PublicTradeRecord{
                .trade_time = time_point{milliseconds{std::stoll(it->time)}},
                .price_points  = currency<uint64_t>(it->price, price_decimals).raw(),
                .volume_points = currency<uint64_t>(it->size,  size_decimals).raw(),
            });
        }
        catch (const std::exception&) {
            // Skip malformed wire record.
        }
    }
    return out;
}

}

void QuoteScratcher::CalculateSize(InstrumentPanel& panel)
{
    // 1) Drain new trades from the cockpit-provided datahub feed AND advance the
    //    candle series clock. Even when the slice past last_seen is empty,
    //    IngestTrades calls AppendTrades so its fill-forward loop pushes empty
    //    buoys for every elapsed period — without that, the active candle stays
    //    pinned in the past while live mode's view_left scrolls right, producing
    //    the "long gap, then sudden burst of empty buoys when the next trade
    //    arrives" pattern. The feed snapshot is the sorted authoritative deque
    //    maintained by the data manager; we slice the tail past last_seen.
    if (const auto& feed = panel.PublicTradesFeed(); feed) {
        const uint64_t last_seen = mQuotes.last_trade_timestamp().value_or(0);
        auto adapted = PullAdapted(feed->get_snapshot(), last_seen, panel.PriceDecimals(), panel.SizeDecimals());
        try {
            IngestTrades(adapted);
        }
        catch (const std::exception&)
        { /* one malformed batch must not stall */ }
    }

    // Visible-window geometry, used both for time-floor hysteresis and the
    // visible-only price autoscale below.
    const int64_t view_left = panel.ViewLeftTimeMs();
    const int64_t period_ms = static_cast<int64_t>(panel.CandlePeriod().count()) * 1000;
    const int64_t cwidth    = std::max<int64_t>(1, panel.CandleWidth());
    const int64_t inner_w   = std::max(1, panel.InnerDataRect().width());
    const int64_t span_ms   = (inner_w * period_ms) / cwidth;
    const int64_t view_right = view_left + span_ms;

    // 1b) Anchor the TIME floor one panel-width of scene-time BEFORE the current
    //     view-left edge. That keeps every visible (t − floor) inside roughly
    //     [span, 2·span] ms of float-exact range, defeating the catastrophic-
    //     cancellation pattern in the LogicalScene X composition
    //     (hud_x = e11·(t−floor) + e13 with e13 ≈ −e11·(view_left−floor)).
    //
    //     A hysteresis band (current must be > view_left − span/2 to be considered
    //     "too close", or < view_left − 3·span to be "too far back") lets the
    //     floor stay put as the view drifts naturally to the right; otherwise
    //     a live-mode frame would refloor every tick and invalidate the
    //     persistent closed-buoy shape pool on every animation cycle.
    {
        const int64_t desired   = view_left - span_ms;
        const int64_t current   = static_cast<int64_t>(panel.GetSceneFloor().time_ms);
        const bool too_close    = current > view_left - span_ms / 2;
        const bool too_far_back = current < view_left - 3 * span_ms;
        if (too_close || too_far_back) {
            SceneFloor sf = panel.GetSceneFloor();
            sf.time_ms = static_cast<uint64_t>(std::max<int64_t>(0, desired));
            panel.SetSceneFloor(sf);
        }
    }

    // 2) Compute the price extent across VISIBLE buoys only. Closed buoys are
    //    append-only and never evicted, so using all-historical extents would let
    //    e22 shrink monotonically with session length and squash recent candles
    //    into a thin band. Restricting to [view_left, view_right] gives a tight
    //    autoscale that follows the current viewport. Empty buoys (volume == 0)
    //    contribute their carried-forward last_price level via min == max, which
    //    keeps the level visible without expanding the range.
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
                p_min = std::min(p_min, buoy.min);
                p_max = std::max(p_max, buoy.max);
            }
        }
        const auto active = mQuotes.active_candle();
        if (active.volume > 0) {
            p_min = std::min(p_min, active.min);
            p_max = std::max(p_max, active.max);
        } else if (active.mean > 0) {
            // Empty active candle still represents a real price level (= last trade
            // price carried forward by AppendTrades fill-forward) — include it so a
            // long no-trade gap keeps the carried-forward dash on screen.
            p_min = std::min(p_min, active.mean);
            p_max = std::max(p_max, active.mean);
        }
    }
    if (p_min == std::numeric_limits<uint64_t>::max()) return;  // no data yet — leave matrix as-is

    // 3) Refloor when the live data either escapes the current window (expansion)
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

    // 4) Re-derive the price-axis scale every frame: inner-rect height can change
    //    on resize without invalidating the window, and ApplyLogicalSceneTransform
    //    (called after CalculateSize) preserves whatever e22 we leave in cur.
    const int H = std::max(1, panel.InnerDataRect().height());
    const float e22 = static_cast<float>(H) / static_cast<float>(mScaleTopPrice - mScaleFloorPrice);
    const tvg::Matrix cur = panel.LogicalScene().transform();
    if (cur.e22 != e22) {
        panel.LogicalScene().transform(tvg::Matrix{cur.e11, 0.0f, cur.e13,
                                                    0.0f,    e22,  cur.e23,
                                                    0.0f,    0.0f, 1.0f});
    }
}

void QuoteScratcher::OnLayout(InstrumentPanel& panel)
{
    if (!mScene) return;

    const auto first_ts = mQuotes.first_buoy_timestamp();
    const SceneFloor& floor = panel.GetSceneFloor();
    const ScenePixelSize px = panel.PixelSizeOf(panel.LogicalScene());
    const float candle_w_px = static_cast<float>(panel.CandleWidth());

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
        ApplyFill(*mClosedWicksGreenShape, kGreen);
        ApplyFill(*mClosedWicksRedShape,   kRed);
        ApplyFill(*mClosedBodyGreenShape,  kGreen);
        ApplyFill(*mClosedBodyRedShape,    kRed);
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
    ApplyFill(*mActiveWicksGreenShape, kGreen);
    ApplyFill(*mActiveWicksRedShape,   kRed);
    ApplyFill(*mActiveBodyGreenShape,  kGreen);
    ApplyFill(*mActiveBodyRedShape,    kRed);

    if (!first_ts) return;

    const uint64_t duration = mQuotes.buoy_duration();
    const auto& closed = mQuotes.quotes();

    const std::size_t n = closed.size();
    for (std::size_t i = mEmittedClosedCount; i < n; ++i) {
        const uint64_t ts = *first_ts + i * duration;
        const auto& curr = closed[i];
        // First buoy in the series has no predecessor — comparing against itself
        // paints it neutral (green via the `>=` tie-break). Intentional anchor.
        const auto& prev = (i == 0) ? curr : closed[i - 1];
        AppendBuoy(*mClosedWicksGreenShape, *mClosedWicksRedShape,
                   *mClosedBodyGreenShape,  *mClosedBodyRedShape,
                   *mClosedGrayShape,
                   ts, duration, curr, prev, floor, px, candle_w_px);
    }
    mEmittedClosedCount = n;

    const auto active = mQuotes.active_candle();
    const uint64_t active_ts = *first_ts + n * duration;
    BuoyCandleQuotes::candle_t prev = closed.empty() ? active : closed.back();
    AppendBuoy(*mActiveWicksGreenShape, *mActiveWicksRedShape,
               *mActiveBodyGreenShape,  *mActiveBodyRedShape,
               *mActiveGrayShape,
               active_ts, duration, active, prev, floor, px, candle_w_px);
}

}
