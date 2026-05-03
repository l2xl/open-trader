// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <ranges>

#include "buoy_candle.hpp"
#include "scratcher.hpp"
#include "timedef.hpp"
#include "tvg_ptr.hpp"

namespace scratcher::cockpit {

class QuoteScratcher : public Scratcher
{
protected:
    BuoyCandleQuotes mQuotes;
    uint64_t mLastPrice = 0;
    tvg_ptr<tvg::Scene> mScene;  // self-owned subtree; attached to panel.LogicalScene() on first emit

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
        trade.trade_time;
        trade.price_points;
        trade.volume_points;
    }
    void IngestTrades(const Range& trades);

    void OnAttach(InstrumentContentPanel& panel) override;
    void OnLayout(InstrumentContentPanel& panel) override;
    void OnDetach(InstrumentContentPanel& panel) override;
};

template <std::ranges::forward_range Range>
requires requires(std::ranges::range_value_t<Range> trade) {
    trade.trade_time;
    trade.price_points;
    trade.volume_points;
}
void QuoteScratcher::IngestTrades(const Range& trades)
{
    auto begin = std::ranges::begin(trades);
    const auto end = std::ranges::end(trades);

    if (mQuotes.last_trade_timestamp()) {
        const uint64_t last_seen = *mQuotes.last_trade_timestamp();
        begin = std::upper_bound(begin, end, last_seen,
            [](uint64_t v, const auto& t) { return v < get_timestamp(t.trade_time); });
    }
    if (begin == end) return;

    const uint64_t last_price = mQuotes.last_trade_timestamp() ? mLastPrice : begin->price_points;
    const uint64_t now_ts = get_timestamp(std::chrono::utc_clock::now());
    mLastPrice = mQuotes.AppendTrades(std::ranges::subrange(begin, end), now_ts, last_price);
}

}
