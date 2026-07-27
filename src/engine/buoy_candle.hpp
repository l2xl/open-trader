// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef BOUY_CANDLE_QUOTES_HPP
#define BOUY_CANDLE_QUOTES_HPP

#include <iostream>
#include <ranges>
#include <limits>
#include <chrono>
#include <cassert>
#include <algorithm>
#include <optional>
#include <tbb/concurrent_vector.h>

#include "currency.hpp"
#include "data_rectangle.hpp"
#include "timedef.hpp"

using std::chrono::duration_cast;
using std::chrono::milliseconds;

namespace scratcher {

template <typename P, typename V>
struct BuoyCandleData
{
    P min;
    P max;
    P mean;
    P close;  // last trade price of the period; for an empty buoy the carried previous close
    V volume;

    BuoyCandleData() = default;

    BuoyCandleData(auto min, auto max, auto mean, auto close, auto vol)
    {
        this->min = min;
        this->max = max;
        this->mean = mean;
        this->close = close;
        this->volume = vol;
    }

    template <typename P1, typename V1>
    BuoyCandleData(const BuoyCandleData<P1, V1>& other)
    {
        min = other.min;
        max = other.max;
        mean = other.mean;
        close = other.close;
        volume = other.volume;
    }

    BuoyCandleData& operator=(const BuoyCandleData& other) = default;

    template <typename P1, typename V1>
    BuoyCandleData& operator=(const BuoyCandleData<P1, V1>& other)
    {
        min = other.min;
        max = other.max;
        mean = other.mean;
        close = other.close;
        volume = other.volume;
        return *this;
    }

};


class BuoyCandleQuotes {
public:
    // Prices/volumes are carried as currency<uint64_t> straight from the wire PublicTrade, so the
    // candle keeps the exchange's fixed-point values verbatim; currency reconciles per-trade scale
    // differences. No instrument decimals live here — conversion to scene "points" happens only at
    // the ThorVG boundary (QuoteScratcher, via Currency::raw_at + InstrumentInfo).
    typedef currency<uint64_t> price_t;
    typedef BuoyCandleData<price_t, price_t> candle_t;
    typedef tbb::concurrent_vector<candle_t> quotes_t;
private:
    // The active candle is mutated by the data thread and snapshot-read by the render thread, so
    // its fields use currency-over-atomic storage.
    typedef currency<std::atomic_uint64_t> atomic_price_t;

    const uint64_t m_buoy_duration;

    std::optional<std::atomic_uint64_t> m_first_buoy_timestamp;
    std::optional<std::atomic_uint64_t> m_first_trade_timestamp;
    std::optional<std::atomic_uint64_t> m_last_trade_timestamp;
    quotes_t m_buoy_data;
    BuoyCandleData<atomic_price_t, atomic_price_t> mCurCandle;

    // In-place reset of the persistent active candle. The object is never reconstructed — a
    // concurrent active_candle() reader sees field-wise atomic stores. volume is stored last so
    // any intermediate state a reader observes is a consistent zero-extent (lone-trade) candle
    // rather than a torn extent (a torn currency value/decimals pair is a tolerated one-frame
    // visual artefact, consistent with the pre-existing torn-snapshot allowance).
    void reset_active(const price_t& price)
    {
        mCurCandle.min = price;
        mCurCandle.max = price;
        mCurCandle.mean = price;
        mCurCandle.close = price;
        mCurCandle.volume = price_t{};
    }

public:
    explicit BuoyCandleQuotes(uint64_t candle_time)
        : m_buoy_duration(candle_time)
    {}

    uint64_t buoy_duration() const
    { return m_buoy_duration; }

    std::optional<uint64_t> first_trade_timestamp() const
    { return m_first_trade_timestamp; }
    
    std::optional<uint64_t> last_trade_timestamp() const
    { return m_last_trade_timestamp; }

    std::optional<uint64_t> first_buoy_timestamp() const
    { return m_first_buoy_timestamp; }

    const quotes_t& quotes() const
    { return m_buoy_data; }

    candle_t active_candle() const
    { return mCurCandle; }

    // Rewind the series so the next AppendTrades rebuilds from scratch. Clears the
    // trade bookmarks too (not just the first-buoy anchor) so a snapshot rebuild does
    // not dedup incoming trades against a stale last-seen timestamp.
    void Reset()
    {
        m_first_buoy_timestamp.reset();
        m_first_trade_timestamp.reset();
        m_last_trade_timestamp.reset();
    }

    template <std::ranges::input_range Range>
    requires requires(std::ranges::range_value_t<Range> trade) {
        trade.time;
        trade.price;
        trade.size;
    }
    price_t AppendTrades(const Range& trades, price_t last_price)
    {
        if (trades.empty())
            return last_price;

        uint64_t first_ts = get_timestamp((*std::ranges::begin(trades)).time);

        if (!m_first_buoy_timestamp) { // indicates that Reset() was called
            reset_active(last_price);
            m_buoy_data.clear();
            m_first_trade_timestamp.emplace(first_ts);
            m_first_buoy_timestamp.emplace(first_ts - first_ts % buoy_duration());
            m_last_trade_timestamp.reset();
        }

        if (m_buoy_data.size() > 0 && first_ts < *m_first_buoy_timestamp + (m_buoy_data.size() - 1) * m_buoy_duration)
            throw std::invalid_argument("Trade time earlier then first candle time");

        if (m_last_trade_timestamp && first_ts < *m_last_trade_timestamp)
            throw std::invalid_argument("Trade time earlier then last processed trade time");

        uint64_t next_buoy_ts = *m_first_buoy_timestamp + m_buoy_data.size() * buoy_duration();
        uint64_t trade_ts = 0; // Will not be empty since trades is not empty
        for(auto it = std::ranges::begin(trades); it != std::ranges::end(trades); ++it) {
            // Bind the element once: the range is the feed's native PublicTrade subrange (its
            // iterator may yield a prvalue and provide no operator->), so a single dereference
            // both satisfies the access pattern and reads each wire trade exactly once.
            const auto& trade = *it;
            trade_ts = get_timestamp(trade.time);

            while (trade_ts >= next_buoy_ts) {
                if (mCurCandle.volume.raw() > 0 || !m_buoy_data.empty()) {
                    candle_t buoy = mCurCandle;
                    reset_active(last_price);
                    m_buoy_data.emplace_back(buoy);
                }
                next_buoy_ts += buoy_duration();
            }

            price_t last_volume = mCurCandle.volume;
            price_t sum_volume = last_volume + trade.size;

            if (last_volume.raw() == 0) {
                // First trade of the period: the buoy opens AT the trade price, not at
                // the carried-forward previous close. A lone-trade buoy is therefore a
                // zero-extent diamond (min == max == mean == price); the move from the
                // previous close is indicated separately by the scratcher, not by
                // widening this candle. The carried close still seeds empty buoys for
                // the gray dash (see the reset above) but is overwritten the instant a
                // trade lands.
                mCurCandle.max = trade.price;
                mCurCandle.min = trade.price;
                mCurCandle.mean = trade.price;
            } else {
                if (mCurCandle.max < trade.price) mCurCandle.max = trade.price;
                if (trade.price < mCurCandle.min) mCurCandle.min = trade.price;
                mCurCandle.mean = (mCurCandle.mean * last_volume + trade.price * trade.size) / sum_volume;
            }
            mCurCandle.close = trade.price;
            mCurCandle.volume = sum_volume;

            last_price = trade.price;
        }
        m_last_trade_timestamp.emplace(trade_ts);

        return last_price;
    }

    // Time-driven advance: fill-forward empty buoys and roll the active candle up to
    // `now_ts`, carrying `last_price` into each empty period. Carries NO trade data —
    // this is the only series mutation the time/scroll path performs, decoupled from
    // AppendTrades so a wall-clock tick advances the live edge without re-ingesting.
    // Cheap when now_ts has not crossed the next buoy boundary (loop body skipped).
    void AdvanceTo(uint64_t now_ts, price_t last_price);
};

}

#endif //BOUY_CANDLE_QUOTES_HPP
