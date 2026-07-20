// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <iostream>
#include <vector>
#include <chrono>

#include <catch2/catch_test_macros.hpp>

#include "buoy_candle.hpp"
#include "data_rectangle.hpp"

using namespace scratcher;

using price_t = BuoyCandleQuotes::price_t;  // currency<uint64_t>

// Minimal trade fixture satisfying the candle ingestion concept (time/price/size), mirroring the
// fields the engine reads from a wire bybit::PublicTrade. Prices/sizes are integer-valued currency
// (decimals 0), so the candle's currency arithmetic reproduces plain integer expectations.
struct MockTrade {
    time_point time;
    price_t    price;
    price_t    size;
};

MockTrade mk(int64_t ts_ms, uint64_t price, uint64_t size)
{ return MockTrade{ time_point{} + milliseconds(ts_ms), price_t(price, 0), price_t(size, 0) }; }

TEST_CASE("SingleBuoyAppend")
{
    BuoyCandleQuotes quotes(1000); // 1 second candle duration

    REQUIRE(quotes.buoy_duration() == 1000);

    std::vector<MockTrade> trades;
    trades.push_back(mk(0,    100, 10));
    trades.push_back(mk(1000, 100, 10));
    trades.push_back(mk(2000, 1000, 100));

    const auto last_price = quotes.AppendTrades(trades, price_t(50, 0));
    quotes.AdvanceTo(2500, last_price);

    // Check that data was processed
    CHECK(quotes.quotes().size() == 2);

    CHECK(quotes.quotes().front().volume.raw() == 10);
    CHECK(quotes.quotes().front().min.raw() == 100);  // opening trade sets min=max=mean=price; the seeded prev close (50) is not memorised
    CHECK(quotes.quotes().front().max.raw() == 100);
    CHECK(quotes.quotes().front().mean.raw() == 100);
    CHECK(quotes.quotes().front().close.raw() == 100);

    CHECK(quotes.quotes().back().volume.raw() == 10);
    CHECK(quotes.quotes().back().min.raw() == 100);
    CHECK(quotes.quotes().back().max.raw() == 100);
    CHECK(quotes.quotes().back().mean.raw() == 100);
    CHECK(quotes.quotes().back().close.raw() == 100);

    CHECK(quotes.active_candle().volume.raw() == 100);
    CHECK(quotes.active_candle().min.raw() == 1000);  // lone trade in this period: zero-extent diamond at its own price
    CHECK(quotes.active_candle().max.raw() == 1000);
    CHECK(quotes.active_candle().mean.raw() == 1000);
    CHECK(quotes.active_candle().close.raw() == 1000);
}

TEST_CASE("TwoTradesAppend")
{
    BuoyCandleQuotes quotes(1000); // 1 second candle duration

    std::vector<MockTrade> trades;
    trades.push_back(mk(1000, 200, 10));
    trades.push_back(mk(1500, 300, 100));

    const auto last_price = quotes.AppendTrades(trades, price_t(100, 0));
    quotes.AdvanceTo(2000, last_price);

    // Check that data was processed
    CHECK(quotes.quotes().size() == 1);

    CHECK(quotes.quotes().back().volume.raw() == 110);
    CHECK(quotes.quotes().back().min.raw() == 200);  // opens at the first trade (200); the carried prev close (100) no longer pulls min down
    CHECK(quotes.quotes().back().max.raw() == 300);
    CHECK(quotes.quotes().back().mean.raw() == 290);
    CHECK(quotes.quotes().back().close.raw() == 300);

    CHECK(quotes.active_candle().volume.raw() == 0);
    CHECK(quotes.active_candle().min.raw() == 300);
    CHECK(quotes.active_candle().max.raw() == 300);
    CHECK(quotes.active_candle().mean.raw() == 300);
    CHECK(quotes.active_candle().close.raw() == 300);  // empty buoy carries the previous close
}

TEST_CASE("SimpleActiveCandle")
{
    BuoyCandleQuotes quotes(1000); // 1 second candle duration

    std::vector<MockTrade> trades;
    trades.push_back(mk(1000, 100, 10));
    trades.push_back(mk(2000, 200, 20));
    trades.push_back(mk(3000, 300, 30));

    const auto last_price = quotes.AppendTrades(trades, price_t(100, 0));
    quotes.AdvanceTo(2000, last_price);

    CHECK(quotes.first_buoy_timestamp() == 1000);
    CHECK(quotes.first_trade_timestamp() == 1000);
    CHECK(quotes.last_trade_timestamp() == 3000);

    CHECK(quotes.quotes().size() == 2);

    CHECK(quotes.quotes().front().volume.raw() == 10);
    CHECK(quotes.quotes().front().min.raw() == 100);
    CHECK(quotes.quotes().front().max.raw() == 100);
    CHECK(quotes.quotes().front().mean.raw() == 100);
    CHECK(quotes.quotes().front().close.raw() == 100);

    CHECK(quotes.quotes().back().volume.raw() == 20);
    CHECK(quotes.quotes().back().min.raw() == 200);  // opening trade (200) sets the floor; carried prev close (100) is not memorised
    CHECK(quotes.quotes().back().max.raw() == 200);
    CHECK(quotes.quotes().back().mean.raw() == 200);
    CHECK(quotes.quotes().back().close.raw() == 200);

    CHECK(quotes.active_candle().volume.raw() == 30);
    CHECK(quotes.active_candle().min.raw() == 300);  // lone trade in this period: zero-extent diamond at its own price
    CHECK(quotes.active_candle().max.raw() == 300);
    CHECK(quotes.active_candle().mean.raw() == 300);
    CHECK(quotes.active_candle().close.raw() == 300);
}

// AdvanceTo is the time-driven half of the split: with no new trades it fill-forwards empty
// buoys and rolls the active candle up to now_ts, carrying the last price. It must also leave
// the active candle as a clean zero-extent reset (in-place atomic reset, not reconstruction).
TEST_CASE("AdvanceToFillForwardAndActiveReset")
{
    BuoyCandleQuotes quotes(1000);  // 1s candles

    // One trade in slot [1000,2000): it is the open active candle, no closed buoys yet.
    std::vector<MockTrade> trades{ mk(1000, 500, 7) };
    const auto last_price = quotes.AppendTrades(trades, price_t(0, 0));
    REQUIRE(last_price.raw() == 500);
    REQUIRE(quotes.quotes().empty());
    CHECK(quotes.active_candle().volume.raw() == 7);
    CHECK(quotes.active_candle().close.raw() == 500);

    // Advance the wall clock into slot [5000,6000): slots starting at 1000,2000,3000,4000 have
    // elapsed, so the traded buoy plus three carried-forward empties close out.
    quotes.AdvanceTo(5000, last_price);

    REQUIRE(quotes.quotes().size() == 4);
    CHECK(quotes.quotes()[0].volume.raw() == 7);     // the traded buoy
    CHECK(quotes.quotes()[0].close.raw() == 500);
    for (std::size_t i = 1; i < 4; ++i) {            // carried-forward empties: zero extent at last price
        INFO("empty buoy " << i);
        CHECK(quotes.quotes()[i].volume.raw() == 0);
        CHECK(quotes.quotes()[i].min.raw() == 500);
        CHECK(quotes.quotes()[i].max.raw() == 500);
        CHECK(quotes.quotes()[i].mean.raw() == 500);
        CHECK(quotes.quotes()[i].close.raw() == 500);
    }

    // Active candle reset in place: zero-extent at the carried last price.
    CHECK(quotes.active_candle().volume.raw() == 0);
    CHECK(quotes.active_candle().min.raw() == 500);
    CHECK(quotes.active_candle().max.raw() == 500);
    CHECK(quotes.active_candle().mean.raw() == 500);
    CHECK(quotes.active_candle().close.raw() == 500);

    // A second advance with no further elapsed period is the cheap no-op path.
    quotes.AdvanceTo(5000, last_price);
    CHECK(quotes.quotes().size() == 4);
}
