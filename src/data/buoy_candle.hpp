// Scratcher project
// Copyright (c) 2025 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
//
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b25tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

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
    typedef BuoyCandleData<uint64_t, uint64_t> candle_t;
    typedef tbb::concurrent_vector<candle_t> quotes_t;
private:
    const uint64_t m_buoy_duration;

    std::optional<std::atomic_uint64_t> m_first_buoy_timestamp;
    std::optional<std::atomic_uint64_t> m_first_trade_timestamp;
    std::optional<std::atomic_uint64_t> m_last_trade_timestamp;
    quotes_t m_buoy_data;
    BuoyCandleData<std::atomic_uint64_t, std::atomic_uint64_t> mCurCandle;;

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

    void Reset()
    { m_first_buoy_timestamp.reset(); }

    template <std::ranges::input_range Range>
    requires requires(std::ranges::range_value_t<Range> trade) {
        trade.trade_time;
        trade.price_points;
        trade.volume_points;
    }
    uint64_t AppendTrades(const Range& trades, uint64_t now_ts, uint64_t last_price)
    {
        if (!trades.empty()) {

            uint64_t first_ts = get_timestamp(std::ranges::begin(trades)->trade_time);

            if (!m_first_buoy_timestamp) { // indicates that Reset() was called
                mCurCandle = candle_t(last_price, last_price, last_price, last_price, 0);
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
                trade_ts = get_timestamp(it->trade_time);

                while (trade_ts >= next_buoy_ts) {
                    if (mCurCandle.volume > 0 || !m_buoy_data.empty()) {
                        candle_t buoy = mCurCandle;
                        mCurCandle = candle_t(last_price, last_price, last_price, last_price, 0);
                        m_buoy_data.emplace_back(buoy);
                    }
                    next_buoy_ts += buoy_duration();
                }

                uint64_t last_volume = mCurCandle.volume.load();
                uint64_t sum_volume = last_volume + it->volume_points;

                if (last_volume == 0) {
                    // First trade of the period: the buoy opens AT the trade price, not at
                    // the carried-forward previous close. A lone-trade buoy is therefore a
                    // zero-extent diamond (min == max == mean == price); the move from the
                    // previous close is indicated separately by the scratcher, not by
                    // widening this candle. The carried close still seeds empty buoys for
                    // the gray dash (see the reset above) but is overwritten the instant a
                    // trade lands.
                    mCurCandle.max = it->price_points;
                    mCurCandle.min = it->price_points;
                    mCurCandle.mean = it->price_points;
                } else {
                    mCurCandle.max = std::max(it->price_points, mCurCandle.max.load());
                    mCurCandle.min = std::min(it->price_points, mCurCandle.min.load());
                    mCurCandle.mean = (mCurCandle.mean.load() * last_volume + it->price_points * it->volume_points) / sum_volume;
                }
                mCurCandle.close = it->price_points;
                mCurCandle.volume = sum_volume;

                last_price = it->price_points;
            }
            m_last_trade_timestamp.emplace(trade_ts);
        }

        if (m_first_buoy_timestamp) {
            uint64_t active_buoy_ts = now_ts - now_ts % buoy_duration();
            uint64_t next_buoy_ts = *m_first_buoy_timestamp + m_buoy_data.size() * buoy_duration();
            while (active_buoy_ts > next_buoy_ts) {
                candle_t buoy = mCurCandle;
                mCurCandle = candle_t(last_price, last_price, last_price, last_price, 0);
                m_buoy_data.push_back(buoy);
                next_buoy_ts += buoy_duration();
            }
        }

        return last_price;
    }
};

}

#endif //BOUY_CANDLE_QUOTES_HPP
