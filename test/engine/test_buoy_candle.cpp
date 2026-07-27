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

TEST_CASE("Period bucketing splits trades into fixed-duration candles", "[BUOY-001]")
{
    BuoyCandleQuotes quotes(1000); // 1 second candle duration

    REQUIRE(quotes.buoy_duration() == 1000);

    std::vector<MockTrade> trades;
    trades.push_back(mk(0,    100, 10));
    trades.push_back(mk(1000, 100, 10));
    trades.push_back(mk(2000, 1000, 100));

    const auto last_price = quotes.AppendTrades(trades, price_t(50, 0));
    quotes.AdvanceTo(2500, last_price);

    CHECK(quotes.first_buoy_timestamp() == 0);
    CHECK(quotes.first_trade_timestamp() == 0);
    CHECK(quotes.last_trade_timestamp() == 2000);

    CHECK(quotes.quotes().size() == 2);

    CHECK(quotes.quotes().front().volume.raw() == 10);  // period [0,1000)
    CHECK(quotes.quotes().front().min.raw() == 100);
    CHECK(quotes.quotes().front().max.raw() == 100);
    CHECK(quotes.quotes().front().mean.raw() == 100);
    CHECK(quotes.quotes().front().close.raw() == 100);

    CHECK(quotes.quotes().back().volume.raw() == 10);  // period [1000,2000)
    CHECK(quotes.quotes().back().min.raw() == 100);
    CHECK(quotes.quotes().back().max.raw() == 100);
    CHECK(quotes.quotes().back().mean.raw() == 100);
    CHECK(quotes.quotes().back().close.raw() == 100);

    CHECK(quotes.active_candle().volume.raw() == 100);  // period [2000,3000), still open
    CHECK(quotes.active_candle().min.raw() == 1000);
    CHECK(quotes.active_candle().max.raw() == 1000);
    CHECK(quotes.active_candle().mean.raw() == 1000);
    CHECK(quotes.active_candle().close.raw() == 1000);
}

TEST_CASE("Volume-weighted aggregation across trades in a period", "[BUOY-002]")
{
    BuoyCandleQuotes quotes(1000); // 1 second candle duration

    std::vector<MockTrade> trades;
    trades.push_back(mk(1000, 200, 10));
    trades.push_back(mk(1500, 300, 100));

    const auto last_price = quotes.AppendTrades(trades, price_t(100, 0));
    quotes.AdvanceTo(2000, last_price);

    CHECK(quotes.quotes().size() == 1);

    CHECK(quotes.quotes().back().volume.raw() == 110);
    CHECK(quotes.quotes().back().min.raw() == 200);
    CHECK(quotes.quotes().back().max.raw() == 300);
    CHECK(quotes.quotes().back().mean.raw() == 290);  // (200*10 + 300*100) / 110, truncated
    CHECK(quotes.quotes().back().close.raw() == 300);

    CHECK(quotes.active_candle().volume.raw() == 0);
    CHECK(quotes.active_candle().min.raw() == 300);
    CHECK(quotes.active_candle().max.raw() == 300);
    CHECK(quotes.active_candle().mean.raw() == 300);
    CHECK(quotes.active_candle().close.raw() == 300);  // empty buoy carries the previous close
}

TEST_CASE("Candle opens at first trade price, not the carried close", "[BUOY-003]")
{
    BuoyCandleQuotes quotes(1000); // 1 second candle duration

    std::vector<MockTrade> trades{ mk(0, 200, 10) };

    const auto last_price = quotes.AppendTrades(trades, price_t(50, 0)); // seeded prev close (50) is below the trade price
    quotes.AdvanceTo(1000, last_price);

    REQUIRE(quotes.quotes().size() == 1);
    CHECK(quotes.quotes().back().volume.raw() == 10);
    CHECK(quotes.quotes().back().min.raw() == 200);  // opens at the trade, not pulled down to 50
    CHECK(quotes.quotes().back().max.raw() == 200);
    CHECK(quotes.quotes().back().mean.raw() == 200);
    CHECK(quotes.quotes().back().close.raw() == 200);
}

// AdvanceTo is the time-driven half of the split: with no new trades it fill-forwards empty
// buoys, carrying the last price. This case checks the fill-forward candle content; time-driven
// mechanics (elapsed-period detection, active-candle reset, no-op path) are covered by BUOY-005.
TEST_CASE("Empty periods carry the last price forward as zero-volume candles", "[BUOY-004]")
{
    BuoyCandleQuotes quotes(1000);  // 1s candles

    // One trade in slot [1000,2000): it is the open active candle, no closed buoys yet.
    std::vector<MockTrade> trades{ mk(1000, 500, 7) };
    const auto last_price = quotes.AppendTrades(trades, price_t(0, 0));
    REQUIRE(last_price.raw() == 500);
    REQUIRE(quotes.quotes().empty());

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
}

TEST_CASE("Time-driven advance closes elapsed periods and resets the active candle", "[BUOY-005]")
{
    BuoyCandleQuotes quotes(1000);  // 1s candles

    std::vector<MockTrade> trades{ mk(500, 100, 5) };  // trade in slot [0,1000)
    const auto last_price = quotes.AppendTrades(trades, price_t(0, 0));
    REQUIRE(quotes.quotes().empty());

    // Advance into slot [3000,4000): the traded buoy closes plus two empties elapse with no trades.
    quotes.AdvanceTo(3000, last_price);

    REQUIRE(quotes.quotes().size() == 3);
    CHECK(quotes.quotes()[0].volume.raw() == 5);      // the traded buoy
    CHECK(quotes.quotes()[0].close.raw() == 100);
    for (std::size_t i = 1; i < 3; ++i) {             // elapsed with no trades: zero extent at last price
        CHECK(quotes.quotes()[i].volume.raw() == 0);
        CHECK(quotes.quotes()[i].close.raw() == 100);
    }

    CHECK(quotes.active_candle().volume.raw() == 0);  // reset in place: zero-extent at the carried last price
    CHECK(quotes.active_candle().min.raw() == 100);
    CHECK(quotes.active_candle().max.raw() == 100);
    CHECK(quotes.active_candle().mean.raw() == 100);
    CHECK(quotes.active_candle().close.raw() == 100);

    quotes.AdvanceTo(3000, last_price);  // same ts: no boundary crossed, cheap no-op
    CHECK(quotes.quotes().size() == 3);
    CHECK(quotes.active_candle().volume.raw() == 0);
    CHECK(quotes.active_candle().close.raw() == 100);
}

TEST_CASE("Monotonic ingestion guard rejects out-of-order trades", "[BUOY-006]")
{
    BuoyCandleQuotes last_trade_guard(1000);
    std::vector<MockTrade> seed{ mk(1000, 100, 10), mk(1500, 110, 5) };
    last_trade_guard.AppendTrades(seed, price_t(0, 0));
    CHECK(last_trade_guard.last_trade_timestamp() == 1500);

    std::vector<MockTrade> stale{ mk(1200, 120, 1) };  // earlier than the last processed trade
    CHECK_THROWS_AS(last_trade_guard.AppendTrades(stale, price_t(0, 0)), std::invalid_argument);

    BuoyCandleQuotes closed_span_guard(1000);
    std::vector<MockTrade> opening{ mk(500, 100, 10) };
    const auto last_price = closed_span_guard.AppendTrades(opening, price_t(0, 0));
    closed_span_guard.AdvanceTo(3000, last_price);  // closes 3 buoys via AdvanceTo, which never touches the trade bookmark
    REQUIRE(closed_span_guard.quotes().size() == 3);
    CHECK(closed_span_guard.last_trade_timestamp() == 500);

    // 1500 is after the trade bookmark (500) but before the last closed buoy's period start (2000)
    std::vector<MockTrade> before_closed_span{ mk(1500, 200, 1) };
    CHECK_THROWS_AS(closed_span_guard.AppendTrades(before_closed_span, price_t(0, 0)), std::invalid_argument);
}

TEST_CASE("Reset rebuilds the series from scratch on next append", "[BUOY-007]")
{
    BuoyCandleQuotes quotes(1000);

    std::vector<MockTrade> trades{ mk(5000, 100, 10), mk(6000, 200, 20) };
    quotes.AppendTrades(trades, price_t(0, 0));

    REQUIRE(quotes.quotes().size() == 1);
    CHECK(quotes.first_buoy_timestamp() == 5000);
    CHECK(quotes.first_trade_timestamp() == 5000);

    quotes.Reset();

    std::vector<MockTrade> older_trades{ mk(2000, 300, 5), mk(3000, 400, 15) };  // earlier than the pre-reset trades
    CHECK_NOTHROW(quotes.AppendTrades(older_trades, price_t(999, 0)));

    CHECK(quotes.first_buoy_timestamp() == 2000);  // re-anchored to the new first trade's floored ts
    CHECK(quotes.first_trade_timestamp() == 2000);

    REQUIRE(quotes.quotes().size() == 1);           // old closed candle is gone, rebuilt from scratch
    CHECK(quotes.quotes().back().volume.raw() == 5);
    CHECK(quotes.quotes().back().min.raw() == 300);
    CHECK(quotes.quotes().back().max.raw() == 300);
    CHECK(quotes.quotes().back().mean.raw() == 300);
    CHECK(quotes.quotes().back().close.raw() == 300);
}
