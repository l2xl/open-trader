// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include <chrono>

#include "buoy_candle.hpp"
#include "scratcher.hpp"

namespace scratcher::cockpit {

class QuoteScratcher : public Scratcher
{
protected:
    BuoyCandleQuotes mQuotes;

public:
    explicit QuoteScratcher(milliseconds buoy_duration)
        : mQuotes(static_cast<uint64_t>(buoy_duration.count()))
    {}

    BuoyCandleQuotes::candle_t GetActiveCandle() const { return mQuotes.active_candle(); }
    const BuoyCandleQuotes::quotes_t& GetQuotes() const { return mQuotes.quotes(); }
    uint64_t BuoyDuration() const { return mQuotes.buoy_duration(); }
    std::optional<uint64_t> FirstBuoyTimestamp() const { return mQuotes.first_buoy_timestamp(); }

    void CalculateSize(IChartPanel&) override {}
    void CalculatePaint(IChartPanel&) override;

private:
    void IngestNewTrades(IChartPanel&);
};

}
