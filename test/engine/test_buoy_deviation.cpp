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

TEST_CASE("Trades classify against the final mean whatever their ingestion-time side", "[BUOY-009]")
{
    BuoyCandleQuotes quotes(1000);

    // Running means: 100.00, 150.00, 145.00, 118.00. The trade (140.00, 2) arrives BELOW the
    // running mean 150.00 but lies above the final mean 118.00 — the retained volume-by-price
    // profile re-splits it upper when the deviations are derived, so the expectations pin the
    // final-mean rule of BUOY_CANDLE.md section 3: upper {200.00 x1, 140.00 x2}, lower
    // {100.00 x7}.
    std::vector<MockTrade> trades{ mk(0, 10000, 1), mk(100, 20000, 1), mk(200, 14000, 2), mk(300, 10000, 6) };
    const auto last_price = quotes.AppendTrades(trades, price_t(0, 2));
    quotes.AdvanceTo(1000, last_price);

    REQUIRE(quotes.quotes().size() == 1);
    const auto candle = quotes.quotes().front();
    CHECK(candle.mean == price_t(11800, 2));
    CHECK(candle.sigma_plus == price_t(5063, 2));   // upper {200.00 x1, 140.00 x2} around 118.00: isqrt(25640000 scaled) = 50.63
    CHECK(candle.sigma_minus == price_t(1800, 2));  // lower {100.00 x7} around 118.00: 18.00 exact
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

    SECTION("lone-trade period floors both sides") {
        BuoyCandleQuotes quotes(1000);
        // One trade: the upper side holds it at zero deviation (p == mu by construction) and
        // the lower side is empty — the two floor clauses in one period. Under the final-mean
        // split these are the only floored shapes a traded period can take: the VWAP identity
        // puts spread on both sides of the mean whenever prices differ at all.
        std::vector<MockTrade> trades{ mk(0, 10500, 3) };
        const auto last_price = quotes.AppendTrades(trades, price_t(0, 2));
        quotes.AdvanceTo(1000, last_price);

        REQUIRE(quotes.quotes().size() == 1);
        CHECK(quotes.quotes().front().sigma_plus == price_t(1, 2));
        CHECK(quotes.quotes().front().sigma_minus == price_t(1, 2));
    }
}

TEST_CASE("Period close and Reset clear the retained trade profile", "[BUOY-011]")
{
    BuoyCandleQuotes quotes(1000);

    // Period [0,1000) has spread (both sigmas 50.00); period [1000,2000) is single-price and
    // must not inherit any of it.
    std::vector<MockTrade> trades{ mk(0, 10000, 1), mk(100, 20000, 1), mk(1000, 15000, 4) };
    const auto last_price = quotes.AppendTrades(trades, price_t(0, 2));
    quotes.AdvanceTo(2000, last_price);

    REQUIRE(quotes.quotes().size() == 2);
    CHECK(quotes.quotes()[0].sigma_plus == price_t(5000, 2));   // upper {200.00} around 150.00
    CHECK(quotes.quotes()[0].sigma_minus == price_t(5000, 2));  // lower {100.00} around 150.00
    CHECK(quotes.quotes()[1].sigma_plus == price_t(1, 2));
    CHECK(quotes.quotes()[1].sigma_minus == price_t(1, 2));

    // Reset with a DIRTY profile: no period close in between, the active candle still
    // carries the spread, so only the Reset path can clear the profile before the rebuild.
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
    // sizes 100000.000 (raw 1e8 at 3 decimals). The per-side second moment around the mean is
    // v*d^2 = 1e8 * (5e5)^2 = 2.5e19 — beyond uint64 (~1.8e19), so only extended-integer
    // accumulation yields the exact 5.00000 deviation on each side of the mean 100005.00000.
    std::vector<MockTrade> trades{
        MockTrade{ time_point{} + milliseconds(0),   price_t(10000000000ull, 5), price_t(100000000ull, 3) },
        MockTrade{ time_point{} + milliseconds(100), price_t(10001000000ull, 5), price_t(100000000ull, 3) },
    };
    const auto last_price = quotes.AppendTrades(trades, price_t(0, 5));
    quotes.AdvanceTo(1000, last_price);

    REQUIRE(quotes.quotes().size() == 1);
    const auto candle = quotes.quotes().front();
    CHECK(candle.mean.raw() == 10000500000ull);
    CHECK(candle.sigma_plus == price_t(500000, 5));   // 5.00000 exact
    CHECK(candle.sigma_minus == price_t(500000, 5));  // the symmetric two-trade split puts one trade each side
}

// Eleven equal-size trades stepping 1.00 per tick across one period: 100.00 -> 110.00
// rising, or 110.00 -> 100.00 falling. Every running mean is exact (no truncation), the
// final mean is 105.00 either way, and by BUOY_CANDLE.md section 3 the two sides around
// that waist hold real spread: sigma_minus = RMS{100..104} = 3.31, sigma_plus =
// RMS{105..110} = 3.02 for both directions. The regime is NORMAL (waist mid-range), so
// the notation demands a two-sided bell.
std::vector<MockTrade> monotonic_period(bool rising)
{
    std::vector<MockTrade> trades;
    for (uint64_t k = 0; k <= 10; ++k)
        trades.push_back(mk(static_cast<int64_t>(k * 80), rising ? 10000 + k * 100 : 11000 - k * 100, 1));
    return trades;
}

BuoyCandleQuotes::candle_t close_single_period(const std::vector<MockTrade>& trades)
{
    BuoyCandleQuotes quotes(1000);
    const auto last_price = quotes.AppendTrades(trades, price_t(0, 2));
    quotes.AdvanceTo(1000, last_price);
    REQUIRE(quotes.quotes().size() == 1);
    return quotes.quotes().front();
}

TEST_CASE("Monotonic rising period keeps its lower half-bell weighted", "[buoy][monotonic]")
{
    // Reproduces the live-chart artefact: during a one-directional (rising) period the
    // ingestion-time running mean lags below every incoming trade, so ALL volume
    // classifies upper. The lower side stays empty, sigma_minus collapses to the
    // one-raw-unit floor and its wall renders at zero width everywhere — the buoy's
    // bottom half degrades to a bare spine while the upper sigma absorbs the ENTIRE
    // two-sided spread and keeps full weight.
    const auto candle = close_single_period(monotonic_period(true));

    REQUIRE(candle.mean == price_t(10500, 2));
    REQUIRE(candle.min == price_t(10000, 2));
    REQUIRE(candle.max == price_t(11000, 2));
    REQUIRE(candle.Classify() == BuoyRegime::NORMAL);  // mid-range waist: a two-sided bell is demanded

    INFO("sigma_minus raw = " << candle.sigma_minus.raw_at(2) << ", sigma_plus raw = " << candle.sigma_plus.raw_at(2));

    // Section 3 contract: the five trades below the 105.00 waist spread 1.00..5.00 away,
    // so the lower deviation is a full price unit at the very least (exact value 3.31).
    CHECK(candle.sigma_minus > price_t(100, 2));

    // The ramp is symmetric around its waist, so the halves must carry comparable
    // weight — never a full bell over a floored side.
    CHECK(2 * candle.sigma_minus.raw_at(2) > candle.sigma_plus.raw_at(2));

    // The render-facing symptom, via the same FitRange + WallWidth path the scratcher
    // emits through: one price unit below the waist the wall must hold comparable width
    // to one price unit above, not collapse to zero.
    const auto fitted = candle.FitRange();
    INFO("wall below = " << fitted.WallWidth(price_t(10400, 2)) << ", wall above = " << fitted.WallWidth(price_t(10600, 2)));
    CHECK(fitted.WallWidth(price_t(10400, 2)) > 0.5f * fitted.WallWidth(price_t(10600, 2)));
}

TEST_CASE("Monotonic falling period bounds the upper deviation inside its tip", "[buoy][monotonic]")
{
    // Falling direction of the same root cause: only the FIRST trade (the period high,
    // upper by the p >= mu convention) lands upper, so sigma_plus recentres a lone point
    // and comes out as the full tip distance max - mean — as if the side's whole mass sat
    // at the extreme. Section 3 puts six of the eleven trades at or above the final
    // waist, spreading 0.00..5.00, so the upper deviation is strictly inside the tip
    // (exact value 3.02). On screen the FitRange clamp masks this overshoot, which is
    // why the live chart shows degraded bottoms but not degraded tops.
    const auto candle = close_single_period(monotonic_period(false));

    REQUIRE(candle.mean == price_t(10500, 2));
    REQUIRE(candle.Classify() == BuoyRegime::NORMAL);

    INFO("sigma_minus raw = " << candle.sigma_minus.raw_at(2) << ", sigma_plus raw = " << candle.sigma_plus.raw_at(2));

    CHECK(candle.sigma_plus < candle.max - candle.mean);

    // The lower side receives the other ten trades (including four above the final
    // waist), so it keeps weight — the falling direction never degrades a half, which
    // pins the artefact's direction asymmetry.
    CHECK(candle.sigma_minus > price_t(100, 2));
}
