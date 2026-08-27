// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <vector>

#include "buoy_candle.hpp"
#include "data_update.hpp"
#include "scratcher.hpp"
#include "timedef.hpp"
#include "tvg_ptr.hpp"

namespace scratcher::cockpit {

class InstrumentPanel;

// Scene-projected 2D point/segment for the buoy contour smoothing. The spline runs on the
// renderer's float coordinates (ThorVG native), after the scratcher has projected the
// fixed-point price levels; everything upstream of it stays currency-exact.
struct BuoySplinePoint { float x = 0.f; float y = 0.f; };
struct BuoyBezierSegment { BuoySplinePoint control1; BuoySplinePoint control2; BuoySplinePoint end; };

// Catmull-Rom smoothing expressed as cubic Beziers (BUOY_CANDLE.md section 6). Closed rings
// wrap their neighbours; open paths clamp them, leaving the endpoints (the half-normal waist
// corners) in place.
inline std::vector<BuoyBezierSegment> CatmullRomSpline(std::span<const BuoySplinePoint> points, bool closed)
{
    std::vector<BuoyBezierSegment> segments;
    const std::ptrdiff_t n = static_cast<std::ptrdiff_t>(points.size());
    if (n < 2) return segments;
    const auto at = [&](std::ptrdiff_t i) -> const BuoySplinePoint& {
        return closed ? points[static_cast<size_t>((i % n + n) % n)] : points[static_cast<size_t>(std::clamp<std::ptrdiff_t>(i, 0, n - 1))];
    };
    const std::ptrdiff_t count = closed ? n : n - 1;
    segments.reserve(static_cast<size_t>(count));
    for (std::ptrdiff_t i = 0; i < count; ++i) {
        const auto& p0 = at(i - 1); const auto& p1 = at(i); const auto& p2 = at(i + 1); const auto& p3 = at(i + 2);
        segments.push_back({{p1.x + (p2.x - p0.x) / 6.f, p1.y + (p2.y - p0.y) / 6.f},
                            {p2.x - (p3.x - p1.x) / 6.f, p2.y - (p3.y - p1.y) / 6.f},
                            p2});
    }
    return segments;
}

// Fill pools in Z-order (== the add() order under mScene). Every buoy element is
// emitted into the pool carrying its colour; a *Flank pool holds the half-alpha
// prefilter skirt of the line elements whose solid core pool follows it, so a flank
// is always painted under the core it belongs to.
enum class BuoyPool : std::size_t
{
    Gray = 0,           // empty-buoy dashes + the move connector, under everything
    BodyGreenFlank, BodyGreen,
    BodyRedFlank,   BodyRed,
    WaistGreenFlank, WaistGreen,
    WaistRedFlank,   WaistRed,
    Count
};

inline constexpr std::size_t kBuoyPoolCount = static_cast<std::size_t>(BuoyPool::Count);

// One emission target's worth of pooled shapes, indexed by BuoyPool.
using BuoyShapePool = std::array<tvg_ptr<tvg::Shape>, kBuoyPoolCount>;

// QuoteScratcher maintains a persistent ThorVG sub-scene under panel.LogicalScene().
// Each buoy renders per BUOY_CANDLE.md as the volume-weighted bell notation:
//
//  * The per-half RANGE SPINE is a PREFILTERED vertical line from the waist out to
//    that half's extreme, emitted for EVERY period — it keeps the range visible
//    where the 3σ taper thins below a pixel, and it is the only range cue a special
//    period has. A tip sitting ON the waist still gets its spine; it spans zero
//    price and so covers no pixel.
//  * The BODY is two half-bells, each emitted as ONE closed filled contour — the
//    straight waist edge at ±A plus that side's Gaussian wall (BuoyCandleData::
//    FitRange + WallWidth samples, Catmull-Rom smoothed). Waist half-width A is the
//    target waist width scaled by the dimensionless BuoyCandleQuotes::WidthRatio
//    against the visible-window calibration, clamped to the slot so neighbours never
//    overlap. Colors follow the notation's per-half channels against the previous
//    FILLED buoy — upper half by curr.max vs prev.max, lower half by curr.min vs
//    prev.min — and each half's spine shares its half's channel. A zero-height half
//    grows no wall and is skipped, leaving the flat-waisted half-bell of the skewed
//    regime.
//  * The WAIST MARKER on top — a PREFILTERED horizontal line spanning the slot at
//    the VWAP level, pixel-fixed via InstrumentPanel::PixelSizeOf, colored by
//    curr.mean vs prev.mean.
//  * SPECIAL (degraded) periods — single-price (H == L) or failing the screen-space
//    ASPECT test (even the longer half shorter than two body widths) — draw the
//    waist marker and nothing else, in the very same pool and colour channel a
//    readable buoy's waist uses; the bell cannot be read at that aspect, so all
//    that survives is the VWAP level and its direction.
//
// Both purely linear elements — the waist marker and the per-half range spine —
// are PREFILTERED lines rather than hard-edged hairlines (rationale and sources in
// quote_scratcher.cpp): a solid core at least two device pixels across plus a one
// pixel half-alpha flank on each side. A hairline exactly one pixel wide alternates
// between one fully covered column and two half covered ones as the chart scrolls
// sub-pixel, which the eye reads as the line pulsing between crisp and pale; a two
// pixel core always keeps one column fully covered and the flanks turn the leftover
// redistribution into a steady soft edge.
//
//  Empty buoys (volume == 0) render as a single gray 0.5 px-tall filled rect at the
//  carried-forward last price level; the gray "move" connector bridges the previous
//  close into a buoy whose range it falls outside of.
//
// Shape pool layout (Z-order is add() order under mScene):
//   BuoyPool::Gray                — gray dashes for empty buoys + move connectors
//   BuoyPool::Body{Green,Red}     — range spine cores + filled bell contours
//   BuoyPool::Waist{Green,Red}    — fixed-pixel waist line cores on top, degraded
//                                   periods' markers included
//   BuoyPool::*Flank              — the half-alpha prefilter skirt of the line
//                                   elements, each directly UNDER its core pool
//   mActiveShapes                 — same pool layout, reset every frame
//
// Closed-pool invalidation triggers — any of:
//   (a) BuoyCandleQuotes::Reset() (data series rewound) — first-buoy-ts changed;
//   (b) panel.SetSceneFloor() — floor coords shifted;
//   (c) Logical-scene pixel size changed on either axis — the waist line and range
//       spine thicknesses, the dash half-height and the ASPECT regime are all
//       (k * px)-derived. px.x moves only when the candle width does, which is also
//       what sets the non-overlap slot clamp;
//   (d) visible-window calibration medians drifted beyond the ~10 % hysteresis —
//       every closed body width consumes the emitted calibration. A vertical zoom
//       alone never moves the bell widths (s_y cancels out of the width ratio), so
//       px.y gates only the pixel-fixed heights and the ASPECT test.
//
// The candle data model itself (BuoyCandleQuotes) is single-writer (IngestTrades, called
// from CalculateSize, which runs under panel.mDataMutex) and many-reader; concurrent
// vector + atomic candle fields keep that safe modulo torn-snapshot reads of the active
// candle, which are visually inconsequential.
class QuoteScratcher : public Scratcher
{
protected:
    BuoyCandleQuotes mQuotes;
    BuoyCandleQuotes::price_t mLastPrice;

    tvg_ptr<tvg::Scene> mScene;

    BuoyShapePool mClosedShapes;
    BuoyShapePool mActiveShapes;

    static tvg::Shape& PoolShape(const BuoyShapePool& pool, BuoyPool which)
    { return *pool[static_cast<std::size_t>(which)]; }

    std::size_t mEmittedClosedCount = 0;
    std::optional<uint64_t> mEmittedFirstBuoyTs;
    uint64_t mEmittedFloorTimeMs = 0;
    uint64_t mEmittedFloorPricePts = 0;
    // Scene pixel size captured at the last closed-pool emission. The waist line and
    // range spine thicknesses, the gray-dash half-height and the ASPECT regime are all
    // (k * px)-derived, so any change on either axis forces a full re-emit of every
    // closed shape. px.x is the inverse of e11, which the panel derives from the candle
    // width, so tracking it also covers the candle-width-driven slot clamp.
    float mEmittedPxSizeX = 0.0f;
    float mEmittedPxSizeY = 0.0f;
    // Visible-window calibration the emitted widths consume. Refreshed from the
    // window medians only when either median drifts beyond the ~10 % hysteresis
    // band — adoption is a closed-pool re-emit trigger, so all buoys always share
    // one calibration.
    std::optional<BuoyCandleQuotes::Calibration> mEmittedCalibration;

    // Auto-scale memory: the current visible price window. We re-floor only when
    // live data drifts outside this window so closed-buoy geometry (anchored at
    // floor.price_points) does not get invalidated every frame.
    uint64_t mScaleFloorPrice = 0;
    uint64_t mScaleTopPrice = 0;

public:
    explicit QuoteScratcher(milliseconds buoy_duration)
        : mQuotes(static_cast<uint64_t>(buoy_duration.count()))
    {}

    BuoyCandleQuotes::candle_t GetActiveCandle() const { return mQuotes.active_candle(); }
    const BuoyCandleQuotes::quotes_t& GetQuotes() const { return mQuotes.quotes(); }
    uint64_t BuoyDuration() const { return mQuotes.buoy_duration(); }
    std::optional<uint64_t> FirstBuoyTimestamp() const { return mQuotes.first_buoy_timestamp(); }

    template <std::ranges::forward_range Range>
    requires requires(std::ranges::range_value_t<Range> trade) {
        trade.time;
        trade.price;
        trade.size;
    }
    void IngestTrades(const Range& trades);

    void OnAttach(InstrumentPanel& panel) override;
    void CalculateSize(InstrumentPanel& panel) override;
    void OnLayout(InstrumentPanel& panel) override;
    void OnDetach(InstrumentPanel& panel) override;

    // Data path: ingest a snapshot|increment of trades and rescale the price axis. Called by
    // the panel under its data mutex — the only entry point that mutates the series. `trades`
    // is the feed's native bybit::PublicTrade cache subrange (any forward range whose value
    // exposes time/price/size), passed straight through with no copy. A snapshot rebuilds the
    // append-only series from scratch; an increment appends the new tail. Trade dedup makes a
    // re-sent overlap harmless.
    template <std::ranges::forward_range Range>
    requires requires(std::ranges::range_value_t<Range> trade) {
        trade.time;
        trade.price;
        trade.size;
    }
    void IngestAndScale(InstrumentPanel& panel, datahub::update_kind kind, const Range& trades);

protected:
    // Data-path price-window autoscale (visible-buoy extent + price refloor). Sets the scene
    // price floor; the e22 derivation stays on the time path so resize is handled without data.
    void PriceAutoscale(InstrumentPanel& panel);

    // Time-path defensive refloor of the scene TIME floor on a float32 precision budget,
    // replacing the per-2-span hysteresis so steady scrolling does not periodically rebuild
    // the closed-buoy geometry.
    void TimeFloorRefloor(InstrumentPanel& panel);

    // Wall clock in Unix ms (sys_clock, leap-second-free) driving the candle fill-forward.
    static uint64_t WallNowMs();

    // Clock seam for the time path (CalculateSize's AdvanceTo): tests override to pin a
    // deterministic now against toy timestamps, mirroring the IngestTradesAt data-path seam.
    virtual uint64_t NowMs() const { return WallNowMs(); }

    // Clock-injected core of IngestTrades: the public overload reads the wall clock and
    // forwards here; tests inject a deterministic now_ts. now_ts drives BuoyCandleQuotes'
    // fill-forward of empty buoys up to the present moment.
    template <std::ranges::forward_range Range>
    requires requires(std::ranges::range_value_t<Range> trade) {
        trade.time;
        trade.price;
        trade.size;
    }
    void IngestTradesAt(const Range& trades, uint64_t now_ts);
};

template <std::ranges::forward_range Range>
requires requires(std::ranges::range_value_t<Range> trade) {
    trade.time;
    trade.price;
    trade.size;
}
void QuoteScratcher::IngestTrades(const Range& trades)
{
    // sys_clock (not utc_clock) so now_ts uses the same Unix-ms convention as
    // wire trade timestamps. get_timestamp(utc_clock::now()) would carry leap
    // seconds (~27 s offset in 2026) and create phantom empty buoys ahead of
    // the real wire stream — every subsequent trade would then be rejected as
    // "earlier than last processed".
    const uint64_t now_ts = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    IngestTradesAt(trades, now_ts);
}

template <std::ranges::forward_range Range>
requires requires(std::ranges::range_value_t<Range> trade) {
    trade.time;
    trade.price;
    trade.size;
}
void QuoteScratcher::IngestTradesAt(const Range& trades, uint64_t now_ts)
{
    auto begin = std::ranges::begin(trades);
    const auto end = std::ranges::end(trades);

    if (mQuotes.last_trade_timestamp()) {
        const uint64_t last_seen = *mQuotes.last_trade_timestamp();
        // ranges::upper_bound (not std::upper_bound): a transform view's iterator yields a
        // prvalue, so it is not a LegacyForwardIterator and the classic algorithm is ill-formed.
        begin = std::ranges::upper_bound(begin, end, last_seen, std::less<>{},
            [](const auto& t) { return get_timestamp(t.time); });
    }

    // Seed last_price for the AppendTrades fill-forward path. Three cases:
    //   * Prior series exists → carry mLastPrice forward (used as the price of every
    //     empty buoy the fill-forward loop pushes).
    //   * No prior series AND new trades present → seed from the first new trade.
    //   * No prior series AND no new trades → nothing to anchor; skip the call.
    // Crucially we do NOT early-return when only the second clause fails: the
    // fill-forward loop in AppendTrades runs UNCONDITIONALLY whenever a series
    // exists, so calling it with an empty subrange is what keeps empty-buoy gray
    // dashes materialising in real time between actual trade arrivals.
    BuoyCandleQuotes::price_t last_price;
    if (mQuotes.last_trade_timestamp()) {
        last_price = mLastPrice;
    } else if (begin != end) {
        last_price = (*begin).price;
    } else {
        return;
    }

    // AppendTrades ingests the (deduped) trade slice; AdvanceTo then fill-forwards empty
    // buoys and rolls the active candle up to now_ts. Splitting them lets the time/scroll
    // path call AdvanceTo alone — see QuoteScratcher::CalculateSize — while trade arrival
    // drives both. Calling AppendTrades with an empty slice is a no-op that still returns
    // last_price, so AdvanceTo's fill-forward keeps running during no-trade gaps.
    mLastPrice = mQuotes.AppendTrades(std::ranges::subrange(begin, end), last_price);
    mQuotes.AdvanceTo(now_ts, mLastPrice);
}

template <std::ranges::forward_range Range>
requires requires(std::ranges::range_value_t<Range> trade) {
    trade.time;
    trade.price;
    trade.size;
}
void QuoteScratcher::IngestAndScale(InstrumentPanel& panel, datahub::update_kind kind, const Range& trades)
{
    // A snapshot rebuilds the append-only series from scratch (Reset clears the trade
    // bookmarks so nothing is deduped away); an increment appends the new tail.
    if (kind == datahub::update_kind::snapshot)
        mQuotes.Reset();

    try {
        IngestTrades(trades);
    }
    catch (const std::exception&)
    { /* one malformed batch must not stall the series */ }

    PriceAutoscale(panel);
}

}
