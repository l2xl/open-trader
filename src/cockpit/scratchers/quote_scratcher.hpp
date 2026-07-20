// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <optional>
#include <ranges>

#include "buoy_candle.hpp"
#include "data_update.hpp"
#include "scratcher.hpp"
#include "timedef.hpp"
#include "tvg_ptr.hpp"

namespace scratcher::cockpit {

class InstrumentPanel;

// QuoteScratcher maintains a persistent ThorVG sub-scene under panel.LogicalScene().
// Each buoy is rendered as up to three pieces:
//
//  * A wick triangle above mean — apex at curr.max, base from (left, mean) to
//    (right, mean). Color follows curr.max vs prev.max.
//  * A wick triangle below mean — apex at curr.min, base from (left, mean) to
//    (right, mean). Color follows curr.min vs prev.min.
//  * A horizontal diamond body centered on mean — width = candle_width, height =
//    candle_width / 2 (both in CANVAS PIXELS, sized via InstrumentPanel::PixelSizeOf
//    on LogicalScene). Color follows curr.mean vs prev.mean. Drawn on top of the
//    wicks so the triangle bases meeting at mean are visually capped.
//
//  Empty buoys (volume == 0) render as a single gray 0.5 px-tall filled rect at the
//  carried-forward last price level — no diamond, no wicks.
//
// Shape pool layout (Z-order is add() order under mScene):
//   mClosedGrayShape         — gray rects for past empty buoys
//   mClosedWicks{Green,Red}  — filled triangles, drawn below the body
//   mClosedBody {Green,Red}  — filled diamonds, drawn on top
//   mActive…                 — same five-shape layout, reset every frame
//
// Closed-pool invalidation triggers — any of:
//   (a) BuoyCandleQuotes::Reset() (data series rewound) — first-buoy-ts changed;
//   (b) panel.SetSceneFloor() — floor coords shifted;
//   (c) Logical-scene Y pixel size changed — diamond half-height and gray dash
//       half-height are derived from it, so every closed body must be re-emitted.
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

    tvg_ptr<tvg::Shape> mClosedGrayShape;
    tvg_ptr<tvg::Shape> mClosedWicksGreenShape;
    tvg_ptr<tvg::Shape> mClosedWicksRedShape;
    tvg_ptr<tvg::Shape> mClosedBodyGreenShape;
    tvg_ptr<tvg::Shape> mClosedBodyRedShape;

    tvg_ptr<tvg::Shape> mActiveGrayShape;
    tvg_ptr<tvg::Shape> mActiveWicksGreenShape;
    tvg_ptr<tvg::Shape> mActiveWicksRedShape;
    tvg_ptr<tvg::Shape> mActiveBodyGreenShape;
    tvg_ptr<tvg::Shape> mActiveBodyRedShape;

    std::size_t mEmittedClosedCount = 0;
    std::optional<uint64_t> mEmittedFirstBuoyTs;
    uint64_t mEmittedFloorTimeMs = 0;
    uint64_t mEmittedFloorPricePts = 0;
    // Pixel-size-Y captured at the last closed-pool emission. The diamond body
    // half-height and gray-dash half-height are (k * px.y), so any change here
    // forces a full re-emit of every closed shape.
    float mEmittedPxSizeY = 0.0f;

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
