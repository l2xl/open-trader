// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <limits>

#include "timedef.hpp"
#include "buoy_candle.hpp"


namespace scratcher {

void BuoyCandleQuotes::AdvanceTo(uint64_t now_ts, price_t last_price)
{
    if (!m_first_buoy_timestamp)
        return;

    uint64_t active_buoy_ts = now_ts - now_ts % buoy_duration();
    uint64_t next_buoy_ts = *m_first_buoy_timestamp + m_buoy_data.size() * buoy_duration();
    while (active_buoy_ts > next_buoy_ts) {
        candle_t buoy = mCurCandle;
        reset_active(last_price);
        m_buoy_data.push_back(buoy);
        next_buoy_ts += buoy_duration();
    }
}

}
