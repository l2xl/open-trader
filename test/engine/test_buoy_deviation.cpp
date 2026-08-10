// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <vector>
#include <chrono>

#include <catch2/catch_test_macros.hpp>

#include "buoy_candle.hpp"
#include "data_rectangle.hpp"

using namespace scratcher;

using price_t = BuoyCandleQuotes::price_t;

// Mirrors the MockTrade fixture of test_buoy_candle.cpp. Prices carry two fixed-point decimals
// (raw 10000 = 100.00) so the integer sigma derivation lands on meaningful sub-unit values, and
// every fixture keeps the running currency mean exact — no truncation leaks into expectations.
// Sigmas are fixed-point on the price scale, floored at one raw unit per side.
struct MockTrade {
    time_point time;
    price_t    price;
    price_t    size;
};

MockTrade mk(int64_t ts_ms, uint64_t price_raw, uint64_t size)
{ return MockTrade{ time_point{} + milliseconds(ts_ms), price_t(price_raw, 2), price_t(size, 0) }; }

TEST_CASE("Per-side deviations are volume-weighted RMS around the waist", "[BUOY-008]")
{
    BuoyCandleQuotes quotes(1000);

    // Side-stable fixture: running means are 110.00, 100.00, 100.00, so ingestion-time
    // classification equals the split around the final mean 100.00 — upper {110.00 x2,
    // 100.00 x4}, lower {90.00 x2}.
    std::vector<MockTrade> trades{ mk(0, 11000, 2), mk(200, 9000, 2), mk(400, 10000, 4) };
    const auto last_price = quotes.AppendTrades(trades, price_t(0, 2));
    quotes.AdvanceTo(1000, last_price);

    REQUIRE(quotes.quotes().size() == 1);
    const auto candle = quotes.quotes().front();
    CHECK(candle.mean == price_t(10000, 2));
    CHECK(candle.sigma_plus == price_t(577, 2));    // isqrt((2*100^2 + 4*0)/6 scaled) = 5.77
    CHECK(candle.sigma_minus == price_t(1000, 2));  // sqrt((2*100^2)/2) = 10.00 exact
}

TEST_CASE("Trades classify against the running mean at ingestion time", "[BUOY-009]")
{
    BuoyCandleQuotes quotes(1000);

    // Running means: 100.00, 150.00, 145.00, 118.00. The trade (140.00, 2) is classified lower
    // against the running mean 150.00 although 140.00 lies above the final mean 118.00 — the
    // approximation sanctioned by BUOY_CANDLE.md section 3. Final-mean classification would put
    // it upper and yield different deviations, so the expectations pin the running rule.
    std::vector<MockTrade> trades{ mk(0, 10000, 1), mk(100, 20000, 1), mk(200, 14000, 2), mk(300, 10000, 6) };
    const auto last_price = quotes.AppendTrades(trades, price_t(0, 2));
    quotes.AdvanceTo(1000, last_price);

    REQUIRE(quotes.quotes().size() == 1);
    const auto candle = quotes.quotes().front();
    CHECK(candle.mean == price_t(11800, 2));
    CHECK(candle.sigma_plus == price_t(5936, 2));   // upper {100.00 x1, 200.00 x1} around 118.00: sqrt(3524) = 59.36
    CHECK(candle.sigma_minus == price_t(1907, 2));  // lower {140.00 x2, 100.00 x6} around 118.00: sqrt(364) = 19.07
}

TEST_CASE("Deviation floor guards empty and single-price sides", "[BUOY-010]")
{
    SECTION("single-price period floors both sides") {
        BuoyCandleQuotes quotes(1000);
        std::vector<MockTrade> trades{ mk(0, 10000, 5), mk(100, 10000, 5) };
        const auto last_price = quotes.AppendTrades(trades, price_t(0, 2));
        quotes.AdvanceTo(1000, last_price);

        REQUIRE(quotes.quotes().size() == 1);
        CHECK(quotes.quotes().front().sigma_plus == price_t(1, 2));
        CHECK(quotes.quotes().front().sigma_minus == price_t(1, 2));
    }

    SECTION("one-sided period floors the empty side only") {
        BuoyCandleQuotes quotes(1000);
        // Running means 100.00, 105.00, 105.00: every trade classifies upper, the lower side
        // stays empty.
        std::vector<MockTrade> trades{ mk(0, 10000, 1), mk(100, 11000, 1), mk(200, 10500, 2) };
        const auto last_price = quotes.AppendTrades(trades, price_t(0, 2));
        quotes.AdvanceTo(1000, last_price);

        REQUIRE(quotes.quotes().size() == 1);
        CHECK(quotes.quotes().front().sigma_plus == price_t(353, 2));  // sqrt((25 + 25 + 0)/4 scaled) = 3.53
        CHECK(quotes.quotes().front().sigma_minus == price_t(1, 2));
    }
}

TEST_CASE("Period close and Reset zero the per-side moment accumulators", "[BUOY-011]")
{
    BuoyCandleQuotes quotes(1000);

    // Period [0,1000) has spread (sigma_plus 50.00); period [1000,2000) is single-price and
    // must not inherit any of it.
    std::vector<MockTrade> trades{ mk(0, 10000, 1), mk(100, 20000, 1), mk(1000, 15000, 4) };
    const auto last_price = quotes.AppendTrades(trades, price_t(0, 2));
    quotes.AdvanceTo(2000, last_price);

    REQUIRE(quotes.quotes().size() == 2);
    CHECK(quotes.quotes()[0].sigma_plus == price_t(5000, 2));  // upper {100.00, 200.00} around 150.00
    CHECK(quotes.quotes()[0].sigma_minus == price_t(1, 2));
    CHECK(quotes.quotes()[1].sigma_plus == price_t(1, 2));
    CHECK(quotes.quotes()[1].sigma_minus == price_t(1, 2));

    // Reset with DIRTY accumulators: no period close in between, the active candle still
    // carries the spread, so only the Reset path can zero the moments before the rebuild.
    BuoyCandleQuotes rebuilt(1000);
    std::vector<MockTrade> spread{ mk(0, 10000, 1), mk(100, 20000, 1) };
    rebuilt.AppendTrades(spread, price_t(0, 2));
    CHECK(rebuilt.active_candle().sigma_plus == price_t(5000, 2));  // spread accumulated, not closed

    rebuilt.Reset();

    std::vector<MockTrade> fresh{ mk(0, 30000, 2) };
    rebuilt.AppendTrades(fresh, price_t(0, 2));

    const auto active = rebuilt.active_candle();
    CHECK(active.sigma_plus == price_t(1, 2));
    CHECK(active.sigma_minus == price_t(1, 2));
}

TEST_CASE("Empty carry-forward periods report floor deviations", "[BUOY-012]")
{
    BuoyCandleQuotes quotes(1000);

    std::vector<MockTrade> trades{ mk(0, 50000, 7) };
    const auto last_price = quotes.AppendTrades(trades, price_t(0, 2));
    quotes.AdvanceTo(3000, last_price);  // closes the traded period plus two empties

    REQUIRE(quotes.quotes().size() == 3);
    for (std::size_t i = 1; i < 3; ++i) {
        INFO("empty buoy " << i);
        CHECK(quotes.quotes()[i].volume.raw() == 0);
        CHECK(quotes.quotes()[i].sigma_plus == price_t(1, 2));
        CHECK(quotes.quotes()[i].sigma_minus == price_t(1, 2));
    }
}

TEST_CASE("Wire-scale moment accumulation keeps deviations precise", "[BUOY-013]")
{
    BuoyCandleQuotes quotes(1000);

    // BTC-scale wire values: prices 100000.00000 and 100010.00000 (raw 1e10 at 5 decimals),
    // sizes 100000.000 (raw 1e8 at 3 decimals). Even origin-shifted, the second moment of the
    // 10.0-wide spread is v*d^2 = 1e8 * (1e6)^2 = 1e20 and the recentred numerator 5e19 — both
    // beyond uint64 (~1.8e19), so only extended-integer accumulation yields the exact 5.00000
    // deviation around the mean 100005.00000 (the unshifted moment would need ~1e28).
    std::vector<MockTrade> trades{
        MockTrade{ time_point{} + milliseconds(0),   price_t(10000000000ull, 5), price_t(100000000ull, 3) },
        MockTrade{ time_point{} + milliseconds(100), price_t(10001000000ull, 5), price_t(100000000ull, 3) },
    };
    const auto last_price = quotes.AppendTrades(trades, price_t(0, 5));
    quotes.AdvanceTo(1000, last_price);

    REQUIRE(quotes.quotes().size() == 1);
    const auto candle = quotes.quotes().front();
    CHECK(candle.mean.raw() == 10000500000ull);
    CHECK(candle.sigma_plus == price_t(500000, 5));  // 5.00000 exact
    CHECK(candle.sigma_minus == price_t(1, 5));
}
