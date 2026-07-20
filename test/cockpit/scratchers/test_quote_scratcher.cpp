// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include "scratchers/quote_scratcher.hpp"
#include "bybit/entities/public_trade.hpp"
#include "currency.hpp"
#include "timedef.hpp"

using namespace scratcher;
using namespace scratcher::cockpit;

namespace {

// These vectors drive the real QuoteScratcher and read back the buoys through its public
// surface (GetQuotes / GetActiveCandle / FirstBuoyTimestamp / BuoyDuration), so they cover
// the whole class: the trade-concept ingestion template, fresh-series last_price seeding,
// the dedup of already-seen trades, and delegation to the BuoyCandleQuotes engine.
//
// QuoteScratcher::IngestTrades reads the wall clock to fill-forward empty buoys up to the
// present, which is hostile to toy timestamps — a now of ~1.7e12 ms against a first buoy at
// t~=0 would fill ~now/duration empty buoys. So we exercise the protected clock-injection
// seam IngestTradesAt(trades, now_ts) (exposed below), pinning now_ts inside the last
// trade's slot so the last period is always the open "active" candle and no trailing empty
// buoys are appended.
//
// Series layout = closed buoys (GetQuotes(), oldest first) followed by the single active
// candle.
//
// Buoy semantics: a buoy's min/max/mean reflect ONLY the trades that fell inside its
// period. A lone trade produces a zero-extent buoy (min == max == mean == price) — a small
// diamond, not a step up/down from the previous close. An empty period (no trades) is the
// one place a price is carried forward: it records min == max == mean == the previous close
// with volume 0, which the scratcher draws as the flat gray dash. The move from a previous
// close into a new buoy is shown by the scratcher as a separate gray connector, not by
// widening the candle, so it never appears in these values.

struct Trade { uint64_t ts; uint64_t price; uint64_t volume; };
struct Buoy  { uint64_t min; uint64_t max; uint64_t mean; uint64_t close; uint64_t volume; };

struct Scenario {
    std::string        name;
    uint64_t           duration;  // buoy period length (ms)
    uint64_t           now_ts;    // wall-clock anchor; kept inside the last trade's slot
    std::vector<Trade> trades;    // sorted ascending by ts
    std::vector<Buoy>  buoys;     // expected series: closed buoys then the active candle
};

// Exposes the protected clock-injection seam so the suite is deterministic.
struct TestQuoteScratcher : QuoteScratcher {
    using QuoteScratcher::QuoteScratcher;
    using QuoteScratcher::IngestTradesAt;
};

// QuoteScratcher's ingestion concept consumes the native wire trade: time as a time_point,
// price/size as currency. Integer test prices/volumes become decimals-0 currency, so the
// candle's currency arithmetic reproduces the human-reviewed integer expectations exactly.
std::vector<bybit::PublicTrade> ToTrades(const std::vector<Trade>& trades)
{
    std::vector<bybit::PublicTrade> out;
    out.reserve(trades.size());
    for (const Trade& t : trades) {
        bybit::PublicTrade pt;
        pt.time  = time_point{} + milliseconds(static_cast<int64_t>(t.ts));
        pt.price = currency<uint64_t>(t.price, 0);
        pt.size  = currency<uint64_t>(t.volume, 0);
        out.push_back(std::move(pt));
    }
    return out;
}

// Human-reviewable test vectors. All prices/volumes live in 1..10; the buoy duration is
// 10 ms so slot boundaries fall on multiples of 10. Each row lists the trades that arrive
// and the resulting buoy series. Expected buoys are {min, max, mean, close, volume};
// `close` is the period's last trade price (the carried previous close for an empty buoy).
std::vector<Scenario> Scenarios()
{
    return {
        // ---- single period --------------------------------------------------------
        { "1 trade -> flat buoy, every value == price",
          10, 3,
          { {3, 5, 4} },
          { {5, 5, 5, 5, 4} } },

        { "2 trades, equal price & volume -> one buoy, volume sums",
          10, 5,
          { {2, 6, 3}, {5, 6, 3} },
          { {6, 6, 6, 6, 6} } },

        { "2 trades, equal volume, prices differ -> mean = (p1+p2)/2, close = last price",
          10, 6,
          { {2, 4, 2}, {6, 8, 2} },
          { {4, 8, 6, 8, 4} } },

        // ---- two periods ----------------------------------------------------------
        { "2 periods, lone trades: period-1 buoy is a flat diamond at its own price (no step from prev close)",
          10, 13,
          { {3, 5, 4}, {13, 7, 2} },
          { {5, 5, 5, 5, 4}, {7, 7, 7, 7, 2} } },

        { "2 periods: [equal price & volume] then [equal volume, prices both above prev close]",
          10, 16,
          { {2, 6, 3}, {4, 6, 3}, {12, 7, 2}, {16, 9, 2} },
          { {6, 6, 6, 6, 6}, {7, 9, 8, 9, 4} } },

        // ---- three periods, middle empty ------------------------------------------
        { "3 periods, middle empty, lone trades at the prev-close price stay flat",
          10, 23,
          { {3, 5, 4}, {23, 5, 4} },
          { {5, 5, 5, 5, 4}, {5, 5, 5, 5, 0}, {5, 5, 5, 5, 4} } },

        { "3 periods, middle empty: [equal price & volume], empty, [equal volume, prices both below carried close]",
          10, 24,
          { {2, 5, 2}, {4, 5, 2}, {22, 2, 3}, {24, 4, 3} },
          { {5, 5, 5, 5, 4}, {5, 5, 5, 5, 0}, {2, 4, 3, 4, 6} } },
    };
}

std::vector<Buoy> CollectBuoys(const TestQuoteScratcher& scr)
{
    std::vector<Buoy> out;
    for (const auto& c : scr.GetQuotes())
        out.push_back({c.min.raw(), c.max.raw(), c.mean.raw(), c.close.raw(), c.volume.raw()});
    const auto active = scr.GetActiveCandle();
    out.push_back({active.min.raw(), active.max.raw(), active.mean.raw(), active.close.raw(), active.volume.raw()});
    return out;
}

} // namespace

TEST_CASE("QuoteScratcher buoy series", "[quote_scratcher][buoy]")
{
    const Scenario sc = GENERATE(from_range(Scenarios()));
    INFO("scenario: " << sc.name);

    TestQuoteScratcher scr{milliseconds(static_cast<int64_t>(sc.duration))};
    CHECK(scr.BuoyDuration() == sc.duration);

    scr.IngestTradesAt(ToTrades(sc.trades), sc.now_ts);

    // The first buoy timestamp is the first trade's slot floored to the buoy duration.
    REQUIRE(scr.FirstBuoyTimestamp().has_value());
    CHECK(*scr.FirstBuoyTimestamp() == sc.trades.front().ts - sc.trades.front().ts % sc.duration);

    const std::vector<Buoy> actual = CollectBuoys(scr);
    REQUIRE(actual.size() == sc.buoys.size());
    for (std::size_t i = 0; i < actual.size(); ++i) {
        INFO("buoy[" << i << "]");
        CHECK(actual[i].min    == sc.buoys[i].min);
        CHECK(actual[i].max    == sc.buoys[i].max);
        CHECK(actual[i].mean   == sc.buoys[i].mean);
        CHECK(actual[i].close  == sc.buoys[i].close);
        CHECK(actual[i].volume == sc.buoys[i].volume);
    }
}

// Re-ingesting already-seen trades must be a no-op: CalculateSize re-sends the feed tail
// every frame, so dedup against last_trade_timestamp is what keeps a buoy from being
// counted twice.
TEST_CASE("QuoteScratcher ignores already-seen trades", "[quote_scratcher][dedup]")
{
    const std::vector<Trade> batch{ {2, 5, 2}, {4, 5, 2} };  // one slot, flat at price 5

    TestQuoteScratcher scr{milliseconds(10)};
    scr.IngestTradesAt(ToTrades(batch), 4);
    const std::vector<Buoy> after_first = CollectBuoys(scr);

    REQUIRE(after_first.size() == 1);
    CHECK(after_first[0].min    == 5);
    CHECK(after_first[0].max    == 5);
    CHECK(after_first[0].mean   == 5);
    CHECK(after_first[0].close  == 5);
    CHECK(after_first[0].volume == 4);

    scr.IngestTradesAt(ToTrades(batch), 4);  // identical batch again
    const std::vector<Buoy> after_replay = CollectBuoys(scr);

    REQUIRE(after_replay.size() == after_first.size());
    CHECK(after_replay[0].min    == after_first[0].min);
    CHECK(after_replay[0].max    == after_first[0].max);
    CHECK(after_replay[0].mean   == after_first[0].mean);
    CHECK(after_replay[0].close  == after_first[0].close);
    CHECK(after_replay[0].volume == after_first[0].volume);
}
