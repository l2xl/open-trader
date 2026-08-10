// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "instrument_panel.hpp"
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

namespace {

// Emission (Tier 2) seam: pins BOTH clocks (data-path IngestTradesAt and the time-path
// NowMs consumed by CalculateSize's AdvanceTo) so panel-driven layout is deterministic
// against toy timestamps, and exposes the pooled shapes for ThorVG path read-back.
struct EmissionScratcher : QuoteScratcher {
    uint64_t mPinnedNow;
    EmissionScratcher(milliseconds duration, uint64_t now) : QuoteScratcher(duration), mPinnedNow(now) {}
    using QuoteScratcher::IngestTradesAt;
    uint64_t NowMs() const override { return mPinnedNow; }

    tvg::Shape& ClosedGray() const    { return *mClosedGrayShape; }
    tvg::Shape& ClosedGreen() const   { return *mClosedBodyGreenShape; }
    tvg::Shape& ClosedRed() const     { return *mClosedBodyRedShape; }
    tvg::Shape& ClosedDiamondGreen() const { return *mClosedDiamondGreenShape; }
    tvg::Shape& ClosedSpecial() const { return *mClosedSpecialShape; }
    tvg::Shape& ActiveGray() const    { return *mActiveGrayShape; }
    tvg::Shape& ActiveGreen() const   { return *mActiveBodyGreenShape; }
    tvg::Shape& ActiveSpecial() const { return *mActiveSpecialShape; }
};

struct PathData { std::vector<tvg::PathCommand> cmds; std::vector<tvg::Point> pts; };

PathData ReadPath(const tvg::Shape& shape)
{
    const tvg::PathCommand* cmds = nullptr;
    const tvg::Point* pts = nullptr;
    uint32_t cmd_count = 0, pt_count = 0;
    REQUIRE(shape.path(&cmds, &cmd_count, &pts, &pt_count) == tvg::Result::Success);
    return {{cmds, cmds + cmd_count}, {pts, pts + pt_count}};
}

// Headless panel: no UI host, so the redraw request is a no-op.
struct HeadlessPanel : InstrumentPanel {
    using InstrumentPanel::InstrumentPanel;
    void Refresh() override {}
};

// Headless panel with a pinned view, zero scene floor, and a hand-set price scale of
// 20 px per point so the toy candles (a few points tall) pass the ASPECT regime test.
// Slot geometry: candle period 1 s mapped to 20 px (e11 = 0.02 px/ms, px.x = 50), buoy
// duration 10 ms, 400x300 canvas, no rulers (PanelType::Empty adds no scratchers).
struct EmissionHarness {
    HeadlessPanel panel{PanelType::Empty, seconds(1), 20};
    std::shared_ptr<EmissionScratcher> scratcher;

    EmissionHarness(const std::vector<Trade>& trades, uint64_t now)
    {
        panel.SetViewLeftTimeMs(0);
        panel.SetSceneFloor(SceneFloor{0, 0});
        const tvg::Matrix m = panel.LogicalScene().transform();
        panel.LogicalScene().transform(tvg::Matrix{m.e11, 0.f, m.e13, 0.f, 20.f, m.e23, 0.f, 0.f, 1.f});
        scratcher = std::make_shared<EmissionScratcher>(milliseconds(10), now);
        panel.AddScratcher(scratcher);
        scratcher->IngestTradesAt(ToTrades(trades), now);
        panel.AllocatePixelBuffer(400, 300);
    }
};

} // namespace

// Fixture shared by the emission cases: slot 0 holds a two-price buoy (min 4, max 8,
// mean 6, volume 4, fitted sigmas 1/1 after the range fit), the second filled slot
// (min 5, max 9, mean 6, volume 4, fitted sigmas 1/1). Calibration medians over the
// two: volume 4, sigma sum 2 — so each filled buoy's width ratio is exactly 1 and the
// waist half-width is Wt/2 = 7 px = 350 scene ms at px.x = 50.
TEST_CASE("Emitted bell body carries the range spine, pins tips and spans the calibrated waist", "[quote_scratcher][emission]")
{
    using Catch::Matchers::WithinAbs;
    EmissionHarness h{{ {2, 4, 2}, {6, 8, 2}, {12, 5, 3}, {16, 9, 1} }, 16};

    // Closed body (green: no filled predecessor, buoy compares to itself): the 1 px range
    // spine rect (MoveTo + 3 LineTo + Close) followed by one closed Catmull-Rom ring of
    // 2*13-2 = 24 samples (MoveTo + 24 CubicTo + Close).
    const PathData body = ReadPath(h.scratcher->ClosedGreen());
    REQUIRE(body.cmds.size() == 5 + 26);
    CHECK(body.cmds.front() == tvg::PathCommand::MoveTo);
    CHECK(body.cmds[5] == tvg::PathCommand::MoveTo);
    CHECK(body.cmds.back() == tvg::PathCommand::Close);
    CHECK(std::count(body.cmds.begin(), body.cmds.end(), tvg::PathCommand::CubicTo) == 24);
    REQUIRE(body.pts.size() == 4 + 1 + 24 * 3);
    CHECK(ReadPath(h.scratcher->ClosedRed()).cmds.empty());

    // Spine rect: 1 px wide (50 ms at px.x = 50) around the slot centre (slot 0..10 ms
    // -> mid 5), spanning exactly max..min.
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK_THAT(std::abs(body.pts[i].x - 5.0f), WithinAbs(25.0, 0.01));
        CHECK_THAT(body.pts[i].y, WithinAbs(i < 2 ? 8.0 : 4.0, 1e-4));
    }

    // First contour sample is the pinned high tip at the slot centre.
    CHECK_THAT(body.pts[4].x, WithinAbs(5.0, 1e-4));
    CHECK_THAT(body.pts[4].y, WithinAbs(8.0, 1e-4));

    // On-curve extents (the MoveTo point and each CubicTo end — Bezier control points
    // legitimately overshoot a rounded peak): the vertical span is exactly min..max, the
    // horizontal span is the calibrated waist half-width (7 px * 50 ms/px) around the centre.
    std::vector<tvg::Point> on_curve{body.pts[4]};
    for (std::size_t i = 5; i + 2 < body.pts.size(); i += 3)
        on_curve.push_back(body.pts[i + 2]);
    REQUIRE(on_curve.size() == 25);
    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    for (const auto& p : on_curve) {
        min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
    }
    CHECK_THAT(min_y, WithinAbs(4.0, 1e-4));
    CHECK_THAT(max_y, WithinAbs(8.0, 1e-4));
    CHECK_THAT(max_x, WithinAbs(5.0 + 350.0, 0.1));
    CHECK_THAT(min_x, WithinAbs(5.0 - 350.0, 0.1));

    // Waist diamond on top (green), lozenge pool empty, no gray ink (no empty buoy, and
    // the previous close sits inside every following range).
    CHECK(ReadPath(h.scratcher->ClosedDiamondGreen()).pts.size() == 4);
    CHECK(ReadPath(h.scratcher->ClosedSpecial()).cmds.empty());
    CHECK(ReadPath(h.scratcher->ClosedGray()).cmds.empty());

    // The active buoy (same shape family) emits its own spine + bell into the active pool.
    CHECK(ReadPath(h.scratcher->ActiveGreen()).cmds.size() == 5 + 26);
}

TEST_CASE("Empty period renders as the gray dash at the carried close", "[quote_scratcher][emission]")
{
    using Catch::Matchers::WithinAbs;
    EmissionHarness h{{ {2, 4, 2}, {6, 8, 2}, {22, 5, 3}, {26, 9, 1} }, 26};

    // Closed series: filled slot 0, empty slot 1 (carried close 8), active slot 2.
    // The dash is the only closed gray ink: one rect spanning the empty slot.
    const PathData gray = ReadPath(h.scratcher->ClosedGray());
    REQUIRE(gray.cmds.size() == 5);
    REQUIRE(gray.pts.size() == 4);
    float min_x = 1e9f, max_x = -1e9f;
    for (const auto& p : gray.pts) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        CHECK_THAT(p.y, WithinAbs(8.0, 0.02));  // 0.5 px tall at px.y = 0.05
    }
    CHECK_THAT(min_x, WithinAbs(10.0, 1e-4));
    CHECK_THAT(max_x, WithinAbs(20.0, 1e-4));

    CHECK(ReadPath(h.scratcher->ClosedGreen()).cmds.size() == 5 + 26);
}

TEST_CASE("Single-price periods stamp the bright lozenge instead of a bell", "[quote_scratcher][emission][BUOY_GEOMETRY-010]")
{
    EmissionHarness h{{ {3, 5, 4}, {13, 7, 2} }, 13};

    // Lone-trade closed buoy: special lozenge only — no body, no diamond.
    CHECK(ReadPath(h.scratcher->ClosedSpecial()).pts.size() == 4);
    CHECK(ReadPath(h.scratcher->ClosedGreen()).cmds.empty());
    CHECK(ReadPath(h.scratcher->ClosedRed()).cmds.empty());
    CHECK(ReadPath(h.scratcher->ClosedDiamondGreen()).cmds.empty());

    // The lone-trade active buoy is special too, and its price band (7) excludes the
    // previous close (5), so the gray move connector bridges them in the active pool.
    CHECK(ReadPath(h.scratcher->ActiveSpecial()).pts.size() == 4);
    CHECK(ReadPath(h.scratcher->ActiveGray()).cmds.size() == 5);
}

TEST_CASE("Catmull-Rom control points offset segment ends by a sixth of the neighbour vector", "[BUOY_GEOMETRY-008]")
{
    using Catch::Matchers::WithinAbs;
    const std::vector<BuoySplinePoint> square{ {0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f} };

    const auto closed = CatmullRomSpline(square, true);
    REQUIRE(closed.size() == 4);
    // Segment P1 -> P2: C1 = P1 + (P2 - P0)/6, C2 = P2 - (P3 - P1)/6
    CHECK_THAT(closed[1].control1.x, WithinAbs(1.0 + 1.0 / 6.0, 1e-6));
    CHECK_THAT(closed[1].control1.y, WithinAbs(1.0 / 6.0, 1e-6));
    CHECK_THAT(closed[1].control2.x, WithinAbs(1.0 + 1.0 / 6.0, 1e-6));
    CHECK_THAT(closed[1].control2.y, WithinAbs(1.0 - 1.0 / 6.0, 1e-6));
    CHECK(closed[1].end.x == 1.f);
    CHECK(closed[1].end.y == 1.f);

    const auto open = CatmullRomSpline(square, false);
    REQUIRE(open.size() == 3);
    CHECK_THAT(open[1].control1.x, WithinAbs(1.0 + 1.0 / 6.0, 1e-6));  // interior segment: same formula, neighbours exist
    CHECK_THAT(open[1].control1.y, WithinAbs(1.0 / 6.0, 1e-6));
    CHECK_THAT(open[1].control2.x, WithinAbs(1.0 + 1.0 / 6.0, 1e-6));
    CHECK_THAT(open[1].control2.y, WithinAbs(1.0 - 1.0 / 6.0, 1e-6));
}
