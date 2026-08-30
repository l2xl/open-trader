// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "buoy_candle.hpp"

using namespace scratcher;
using Catch::Matchers::WithinRel;

using price_t = BuoyCandleQuotes::price_t;
using candle_t = BuoyCandleQuotes::candle_t;

namespace {

candle_t mk_candle(uint64_t min, uint64_t max, uint64_t mean, uint64_t volume, uint64_t sigma_minus, uint64_t sigma_plus, size_t decimals = 0)
{
    candle_t candle(price_t(min, decimals), price_t(max, decimals), price_t(mean, decimals), price_t(mean, decimals), price_t(volume, 0));
    candle.sigma_minus = price_t(sigma_minus, decimals);
    candle.sigma_plus = price_t(sigma_plus, decimals);
    return candle;
}

candle_t asymmetric_candle()
{ return mk_candle(90, 110, 100, 200, 2, 3); }

// Fitted calibration window: sigma sums {4, 5, 8}, volumes {100, 200, 400}; every sigma sits
// inside its tip/TAIL bound, so FitRange leaves the window unchanged.
std::vector<candle_t> calibration_window()
{
    return {
        mk_candle(90, 110, 100, 100, 2, 2),
        asymmetric_candle(),
        mk_candle(80, 120, 100, 400, 3, 5),
    };
}

double shoelace_area(const std::vector<candle_t::ContourPoint>& points)
{
    double acc = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const auto& a = points[i];
        const auto& b = points[(i + 1) % points.size()];
        const double ay = static_cast<double>(a.price.raw_at(0)), by = static_cast<double>(b.price.raw_at(0));
        acc += static_cast<double>(a.width) * by - static_cast<double>(b.width) * ay;
    }
    return std::abs(acc) / 2.0;
}

}

TEST_CASE("Range fit clamps each deviation one-sidedly to its tip over TAIL", "[BUOY_GEOMETRY-011]")
{
    const candle_t raw = mk_candle(90, 110, 100, 500, 2, 10);  // raw sigma_plus at the full tip distance
    const candle_t fit = raw.FitRange();

    CHECK(fit.sigma_plus == price_t(3, 0));   // (110 - 100) / TAIL, fixed-point truncation
    CHECK(fit.sigma_minus == price_t(2, 0));  // already inside its tip: untouched
    CHECK(fit.min == raw.min);
    CHECK(fit.max == raw.max);
    CHECK(fit.mean == raw.mean);
    CHECK(fit.volume == raw.volume);

    CHECK(fit.WallWidth(fit.max) <= std::exp(-4.5f));  // the declared zero never lands beyond the tip

    // Two-tick quiet period: the truncating tip/TAIL division must not clamp below the
    // one-raw-unit deviation floor, and the wall stays finite everywhere
    const candle_t quiet = mk_candle(10000, 10002, 10001, 50, 1, 1, 2).FitRange();
    CHECK(quiet.sigma_plus == price_t(1, 2));
    CHECK(quiet.sigma_minus == price_t(1, 2));
    CHECK(quiet.WallWidth(quiet.mean) == 1.f);
    CHECK(std::isfinite(quiet.WallWidth(quiet.max)));
}

TEST_CASE("Calibration medians run over non-empty, non-degenerate periods", "[BUOY_GEOMETRY-001]")
{
    // The excluded periods carry extreme volumes/spreads so that any wrong inclusion —
    // degenerate, empties, or both — shifts at least one median off its expectation.
    // The single-price candle is assembled field-wise: the validating constructor rejects
    // it, but the engine still emits such candles for lone-trade periods.
    auto window = calibration_window();
    candle_t flat;
    flat.min = flat.max = flat.mean = flat.close = price_t(100, 0);
    flat.volume = price_t(5000, 0);
    flat.sigma_plus = flat.sigma_minus = price_t(1, 0);
    window.push_back(flat);                                   // single-price degenerate: excluded
    window.push_back(mk_candle(60, 160, 100, 0, 50, 50, 0));  // empty carry-forward: excluded
    window.push_back(mk_candle(60, 160, 100, 0, 50, 50, 0));  // second empty

    const auto calibration = BuoyCandleQuotes::Calibrate(window);
    CHECK(calibration.median_volume == price_t(200, 0));
    CHECK(calibration.median_sigma_sum == price_t(5, 0));
}

TEST_CASE("Width ratio is dimensionless and one for the median period", "[BUOY_GEOMETRY-002]")
{
    const auto window = calibration_window();
    const auto calibration = BuoyCandleQuotes::Calibrate(window);

    CHECK(BuoyCandleQuotes::WidthRatio(window[1], calibration) == 1.0f);   // V == median V, sigma sum == median sigma sum
    CHECK(BuoyCandleQuotes::WidthRatio(window[2], calibration) == 1.25f);  // (400/200) * (5/8)

    candle_t doubled = window[2];
    doubled.volume = price_t(800, 0);
    CHECK(BuoyCandleQuotes::WidthRatio(doubled, calibration) == 2.5f);     // linear in volume at fixed spread
}

TEST_CASE("Wall decays as a per-side Gaussian around the waist", "[BUOY_GEOMETRY-003]")
{
    const candle_t candle = asymmetric_candle();

    CHECK_THAT(candle.WallWidth(price_t(103, 0)), WithinRel(std::exp(-0.5), 1e-6));  // one upper deviation
    CHECK_THAT(candle.WallWidth(price_t(98, 0)), WithinRel(std::exp(-0.5), 1e-6));   // one lower deviation

    // At equal price distance the halves differ whenever the deviations differ
    CHECK_THAT(candle.WallWidth(price_t(102, 0)), WithinRel(std::exp(-2.0 / 9.0), 1e-6));
    CHECK_THAT(candle.WallWidth(price_t(98, 0)), WithinRel(std::exp(-0.5), 1e-6));
    CHECK(candle.WallWidth(price_t(102, 0)) != candle.WallWidth(price_t(98, 0)));

    candle_t symmetric = candle;
    symmetric.sigma_plus = symmetric.sigma_minus;
    CHECK(symmetric.WallWidth(price_t(102, 0)) == symmetric.WallWidth(price_t(98, 0)));

    // Field-assembled candle with unset sigmas: the wall must collapse to the sigma->0 limit
    // (unit width at the waist, zero elsewhere), never to NaN/inf
    candle_t zero_sigma = asymmetric_candle();
    zero_sigma.sigma_plus = price_t{};
    zero_sigma.sigma_minus = price_t{};
    CHECK(zero_sigma.WallWidth(zero_sigma.mean) == 1.f);
    CHECK(zero_sigma.WallWidth(price_t(101, 0)) == 0.f);
    CHECK(zero_sigma.WallWidth(price_t(98, 0)) == 0.f);
}

TEST_CASE("Contour pins the tips to zero and meets the waist at unit width with zero slope", "[BUOY_GEOMETRY-004]")
{
    const candle_t candle = asymmetric_candle();
    const auto contour = candle.SampleContour(13);
    REQUIRE(!contour.points.empty());

    std::size_t high_tips = 0, low_tips = 0;
    for (const auto& point : contour.points) {
        if (point.price == candle.max) { ++high_tips; CHECK(point.width == 0.f); }
        if (point.price == candle.min) { ++low_tips;  CHECK(point.width == 0.f); }
        CHECK(std::abs(point.width) <= 1.f);
    }
    CHECK(high_tips == 1);
    CHECK(low_tips == 1);

    CHECK(candle.WallWidth(candle.mean) == 1.f);

    // Flat peak: one raw price tick off a fine-scaled waist keeps the wall at unit width
    const candle_t fine = mk_candle(90000, 110000, 100000, 100, 2000, 2000, 3);
    CHECK(fine.WallWidth(price_t(100001, 3)) > 1.f - 1e-6f);
    CHECK(fine.WallWidth(price_t(99999, 3)) > 1.f - 1e-6f);
}

TEST_CASE("Wall width decays monotonically from the waist to each tip", "[BUOY_GEOMETRY-005]")
{
    const candle_t candle = asymmetric_candle();

    float prev_up = candle.WallWidth(candle.mean);
    float prev_down = prev_up;
    REQUIRE(prev_up == 1.f);
    for (uint64_t step = 1; step <= 10; ++step) {
        const float up = candle.WallWidth(price_t(100 + step, 0));
        const float down = candle.WallWidth(price_t(100 - step, 0));
        CHECK(up < prev_up);
        CHECK(down < prev_down);
        prev_up = up;
        prev_down = down;
    }
}

TEST_CASE("Sigma tick levels sit one deviation off the waist at equal width", "[BUOY_GEOMETRY-006]")
{
    const candle_t candle = asymmetric_candle();
    const auto ticks = candle.TickLevels();

    CHECK(ticks.upper == price_t(103, 0));
    CHECK(ticks.lower == price_t(98, 0));
    CHECK_THAT(ticks.width, WithinRel(std::exp(-0.5), 1e-6));
    CHECK_THAT(candle.WallWidth(ticks.upper), WithinRel(static_cast<double>(ticks.width), 1e-6));
    CHECK_THAT(candle.WallWidth(ticks.lower), WithinRel(static_cast<double>(ticks.width), 1e-6));
}

TEST_CASE("Contour samples fixed price levels and mirrors without duplicating tips", "[BUOY_GEOMETRY-007]")
{
    const candle_t candle = asymmetric_candle();
    constexpr std::size_t levels = 13;
    const auto contour = candle.SampleContour(levels);

    REQUIRE(contour.points.size() == 2 * levels - 2);  // levels on the right wall, mirrored interior on the left
    CHECK(contour.closed);

    CHECK(contour.points.front().price == candle.max);
    CHECK(contour.points[levels - 1].price == candle.min);
    for (std::size_t k = 1; k < levels; ++k)  // right wall descends max -> min
        CHECK(contour.points[k].price < contour.points[k - 1].price);
    for (std::size_t k = 1; k + 1 < levels; ++k) {
        CHECK(contour.points[k].width > 0.f);
        const auto& left = contour.points[contour.points.size() - k];
        CHECK(left.price == contour.points[k].price);
        CHECK(left.width == -contour.points[k].width);
    }
}

TEST_CASE("Waist off the uniform level grid is inserted as an explicit unit-width sample", "[BUOY_GEOMETRY-007]")
{
    // 13 uniform levels over [90, 110] are {110,109,107,105,104,102,100,99,97,95,94,92,90}:
    // a mean of 101 falls between grid levels. Without the inserted waist sample the
    // strongly asymmetric sigmas would let the spline bulge the peak off the mean.
    const candle_t candle = mk_candle(90, 110, 101, 200, 2, 6);
    constexpr std::size_t levels = 13;
    const auto contour = candle.SampleContour(levels);

    REQUIRE(contour.points.size() == 2 * (levels + 1) - 2);  // one inserted level, mirrored
    std::size_t waist_samples = 0;
    float widest = 0.f;
    for (const auto& point : contour.points) {
        if (point.price == candle.mean) { ++waist_samples; CHECK(std::abs(point.width) == 1.f); }
        widest = std::max(widest, std::abs(point.width));
    }
    CHECK(waist_samples == 2);  // right wall + mirrored left wall
    CHECK(widest == 1.f);       // nothing wider than the waist anywhere

    for (std::size_t k = 1; k < levels + 1; ++k)  // right wall still strictly descends
        CHECK(contour.points[k].price < contour.points[k - 1].price);

    // A mean already on the grid inserts nothing — the asymmetric fixture's mean of 100
    // is a uniform level, so the contour keeps its 2*levels-2 size.
    CHECK(asymmetric_candle().SampleContour(levels).points.size() == 2 * levels - 2);
}

TEST_CASE("Half-normal regime keeps a flat waist and decays with the far side's deviation", "[BUOY_GEOMETRY-009]")
{
    // Waist at 107.1 of [90.0, 110.0]: (high - mean)/(high - low) = 0.145 < 0.15
    const candle_t candle = mk_candle(900, 1100, 1071, 100, 40, 3, 1);
    CHECK(candle.Classify() == BuoyRegime::HALF_NORMAL);

    // Boundary: exactly the skew fraction stays normal, strictly inside becomes half-normal
    CHECK(mk_candle(900, 1100, 1070, 100, 40, 3, 1).Classify() == BuoyRegime::NORMAL);   // 3/20 exactly
    CHECK(mk_candle(900, 1100, 930, 100, 3, 40, 1).Classify() == BuoyRegime::NORMAL);    // low side, 3/20 exactly
    CHECK(mk_candle(900, 1100, 929, 100, 3, 40, 1).Classify() == BuoyRegime::HALF_NORMAL);

    const auto contour = candle.SampleContour(13);
    REQUIRE(contour.points.size() == 2 * 13 - 1);
    CHECK(!contour.closed);  // open path: the straight waist edge closes it in the renderer, corners stay sharp

    CHECK(contour.points.front().price == candle.mean);
    CHECK(contour.points.front().width == 1.f);
    CHECK(contour.points.back().price == candle.mean);
    CHECK(contour.points.back().width == -1.f);

    std::size_t far_tips = 0;
    for (const auto& point : contour.points) {
        CHECK(point.price <= candle.mean);  // the body spans waist to the far tip only
        if (point.price == candle.min) { ++far_tips; CHECK(point.width == 0.f); }
        else {
            const float distance = static_cast<float>(price_t(candle.mean - point.price).raw_at(1));
            const float x = distance / static_cast<float>(candle.sigma_minus.raw_at(1));
            CHECK_THAT(std::abs(point.width), WithinRel(std::exp(-0.5 * static_cast<double>(x) * static_cast<double>(x)), 1e-5));  // far side's sigma
        }
    }
    CHECK(far_tips == 1);
}

TEST_CASE("Single-price period is rejected at construction whatever its volume", "[BUOY_GEOMETRY-010]")
{
    CHECK_THROWS_AS(mk_candle(100, 100, 100, 5000, 1, 1), std::invalid_argument);
    CHECK_THROWS_AS(mk_candle(110, 90, 100, 100, 2, 3), std::invalid_argument);  // inverted range is equally degenerate

    CHECK(asymmetric_candle().Classify() == BuoyRegime::NORMAL);
}

TEST_CASE("Contour area scaled by the width ratio is proportional to traded volume", "[BUOY_GEOMETRY-012]")
{
    const auto window = calibration_window();
    const auto calibration = BuoyCandleQuotes::Calibrate(window);

    // A clamped period sharing the window's calibration: raw sigma_plus equals its tip distance,
    // FitRange pulls it to (110-104)/TAIL = 2; the joint waist re-inflation through the width
    // ratio must keep its area on the volume line.
    const candle_t clamped = mk_candle(90, 110, 104, 300, 3, 6).FitRange();
    REQUIRE(clamped.sigma_plus == price_t(2, 0));

    constexpr std::size_t levels = 129;
    const auto area = [&](const candle_t& candle) {
        const auto contour = candle.SampleContour(levels);
        REQUIRE(contour.points.size() >= 3);
        return shoelace_area(contour.points) * static_cast<double>(BuoyCandleQuotes::WidthRatio(candle, calibration));
    };

    const double area_a = area(window[0]);
    const double area_b = area(window[1]);
    const double area_clamped = area(clamped);

    CHECK_THAT(area_b / area_a, WithinRel(2.0, 0.02));        // volumes 200 vs 100
    CHECK_THAT(area_clamped / area_a, WithinRel(3.0, 0.02));  // volumes 300 vs 100
}
