// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
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

    tvg::Shape& Closed(BuoyPool which) const { return PoolShape(mClosedShapes, which); }
    tvg::Shape& Active(BuoyPool which) const { return PoolShape(mActiveShapes, which); }
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
TEST_CASE("Emitted half-bells carry per-half spines, pin tips and span the calibrated waist", "[quote_scratcher][emission]")
{
    using Catch::Matchers::WithinAbs;
    EmissionHarness h{{ {2, 4, 2}, {6, 8, 2}, {12, 5, 3}, {16, 9, 1} }, 16};

    // Closed body (green: no filled predecessor, buoy compares to itself). Both halves'
    // prefiltered spine cores come first (MoveTo + 3 LineTo + Close each), then ONE
    // closed contour per half — the waist edge closing two 12-segment Catmull-Rom wall
    // chains (MoveTo + 24 CubicTo + Close). Two halves -> 62 commands, all in the green
    // pool; the spines' half-alpha flanks live in the flank pool below it.
    const PathData body = ReadPath(h.scratcher->Closed(BuoyPool::BodyGreen));
    REQUIRE(body.cmds.size() == 2 * (5 + 26));
    CHECK(body.cmds.front() == tvg::PathCommand::MoveTo);
    CHECK(std::count(body.cmds.begin(), body.cmds.end(), tvg::PathCommand::CubicTo) == 48);
    CHECK(std::count(body.cmds.begin(), body.cmds.end(), tvg::PathCommand::Close) == 4);
    REQUIRE(body.pts.size() == 2 * (4 + 1 + 24 * 3));
    CHECK(ReadPath(h.scratcher->Closed(BuoyPool::BodyRed)).cmds.empty());

    // Spine cores: kLineCorePx = 2 px wide (2 * 50 ms at px.x = 50, so +-50 ms of the
    // slot centre at 5 ms), the upper spanning exactly mean..max and the lower mean..min.
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK_THAT(std::abs(body.pts[i].x - 5.0f), WithinAbs(50.0, 0.01));
        CHECK_THAT(body.pts[i].y, WithinAbs(i == 0 || i == 3 ? 6.0 : 8.0, 1e-4));
        CHECK_THAT(std::abs(body.pts[4 + i].x - 5.0f), WithinAbs(50.0, 0.01));
        CHECK_THAT(body.pts[4 + i].y, WithinAbs(i == 0 || i == 3 ? 6.0 : 4.0, 1e-4));
    }

    // Each spine's prefilter flanks: two more quads per spine in the flank pool, each
    // one kLineFlankPx = 1 px (50 ms) wide and butted against a core edge, so the flank
    // pool's x extent runs to 2 px either side of the centre and never inside the core.
    const PathData flanks = ReadPath(h.scratcher->Closed(BuoyPool::BodyGreenFlank));
    REQUIRE(flanks.cmds.size() == 4 * 5);
    REQUIRE(flanks.pts.size() == 4 * 4);
    for (const auto& p : flanks.pts) {
        const float d = std::abs(p.x - 5.0f);
        CHECK((std::abs(d - 50.0f) < 0.01f || std::abs(d - 100.0f) < 0.01f));
    }

    // Upper contour starts at the right waist corner — the calibrated half-width
    // (7 px * 50 ms/px) right of the centre at the mean level — and its on-curve points
    // (MoveTo + CubicTo ends; control points legitimately overshoot) span exactly
    // mean..max vertically and the +-waist horizontally, with the tip pinned on-centre.
    const std::size_t contour = 2 * 4;  // both spine cores precede the first contour
    const float waist_ms = 350.f;
    CHECK_THAT(body.pts[contour].x, WithinAbs(5.0 + waist_ms, 0.1));
    CHECK_THAT(body.pts[contour].y, WithinAbs(6.0, 1e-4));
    std::vector<tvg::Point> on_curve{body.pts[contour]};
    for (std::size_t i = contour + 1; i + 2 < contour + 1 + 24 * 3; i += 3)
        on_curve.push_back(body.pts[i + 2]);
    REQUIRE(on_curve.size() == 25);
    float min_x = 1e9f, max_x = -1e9f, min_y = 1e9f, max_y = -1e9f;
    bool tip_on_curve = false;
    for (const auto& p : on_curve) {
        min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
        if (std::abs(p.y - 8.f) < 1e-4f && std::abs(p.x - 5.f) < 1e-4f) tip_on_curve = true;
    }
    CHECK(tip_on_curve);
    CHECK_THAT(min_y, WithinAbs(6.0, 1e-4));
    CHECK_THAT(max_y, WithinAbs(8.0, 1e-4));
    CHECK_THAT(max_x, WithinAbs(5.0 + waist_ms, 0.1));
    CHECK_THAT(min_x, WithinAbs(5.0 - waist_ms, 0.1));

    // Waist marker on top (green): one core quad spanning the whole slot (0..10 ms) at
    // the mean, kLineCorePx = 2 px tall — +-0.05 scene points at px.y = 0.05 — and two
    // 1 px flanks butted onto it. No gray ink (no empty buoy, and the previous close
    // sits inside every following range) and nothing in the bearish waist pool.
    const PathData waist = ReadPath(h.scratcher->Closed(BuoyPool::WaistGreen));
    REQUIRE(waist.pts.size() == 4);
    float waist_min_x = 1e9f, waist_max_x = -1e9f;
    for (const auto& p : waist.pts) {
        waist_min_x = std::min(waist_min_x, p.x);
        waist_max_x = std::max(waist_max_x, p.x);
        CHECK_THAT(std::abs(p.y - 6.0f), WithinAbs(0.05, 1e-4));
    }
    CHECK_THAT(waist_min_x, WithinAbs(0.0, 1e-4));
    CHECK_THAT(waist_max_x, WithinAbs(10.0, 1e-4));
    const PathData waist_flanks = ReadPath(h.scratcher->Closed(BuoyPool::WaistGreenFlank));
    REQUIRE(waist_flanks.pts.size() == 2 * 4);
    for (const auto& p : waist_flanks.pts) {
        const float d = std::abs(p.y - 6.0f);
        CHECK((std::abs(d - 0.05f) < 1e-4f || std::abs(d - 0.10f) < 1e-4f));
    }
    CHECK(ReadPath(h.scratcher->Closed(BuoyPool::WaistRed)).cmds.empty());
    CHECK(ReadPath(h.scratcher->Closed(BuoyPool::Gray)).cmds.empty());

    // The active buoy (same shape family) emits its own half-bells into the active pool.
    CHECK(ReadPath(h.scratcher->Active(BuoyPool::BodyGreen)).cmds.size() == 2 * (5 + 26));
}

TEST_CASE("Empty period renders as the gray dash at the carried close", "[quote_scratcher][emission]")
{
    using Catch::Matchers::WithinAbs;
    EmissionHarness h{{ {2, 4, 2}, {6, 8, 2}, {22, 5, 3}, {26, 9, 1} }, 26};

    // Closed series: filled slot 0, empty slot 1 (carried close 8), active slot 2.
    // The dash is the only closed gray ink: one rect spanning the empty slot.
    const PathData gray = ReadPath(h.scratcher->Closed(BuoyPool::Gray));
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

    CHECK(ReadPath(h.scratcher->Closed(BuoyPool::BodyGreen)).cmds.size() == 2 * (5 + 26));
}

TEST_CASE("Halves color independently by their own extreme's growth", "[quote_scratcher][emission]")
{
    // Slot 0: range [4, 8]. Slot 1: max 9 grows (upper half green) while min 3 falls
    // (lower half red) — the per-half channels must split one buoy across both pools.
    EmissionHarness h{{ {2, 4, 2}, {6, 8, 2}, {12, 9, 2}, {14, 3, 2}, {22, 6, 1} }, 22};

    const PathData green = ReadPath(h.scratcher->Closed(BuoyPool::BodyGreen));
    const PathData red = ReadPath(h.scratcher->Closed(BuoyPool::BodyRed));
    CHECK(green.cmds.size() == 3 * (5 + 26));  // both halves of slot 0 + slot 1's upper
    CHECK(red.cmds.size() == 1 * (5 + 26));    // slot 1's lower half only

    // The red pool's spine spans slot 1's mean (6) down to its min (3).
    using Catch::Matchers::WithinAbs;
    CHECK_THAT(red.pts[0].y, WithinAbs(6.0, 1e-4));
    CHECK_THAT(red.pts[1].y, WithinAbs(3.0, 1e-4));
}

TEST_CASE("Single-price periods stamp the waist marker instead of a bell", "[quote_scratcher][emission][BUOY_GEOMETRY-010]")
{
    using Catch::Matchers::WithinAbs;
    EmissionHarness h{{ {3, 5, 4}, {13, 7, 2} }, 13};

    // Lone-trade closed buoy: the degraded marker IS the waist line, emitted into the
    // ordinary bullish waist pool (no filled predecessor -> compares to itself -> the
    // `>=` default) rather than a pool of its own, and no bell contour is drawn.
    const PathData waist = ReadPath(h.scratcher->Closed(BuoyPool::WaistGreen));
    REQUIRE(waist.pts.size() == 4);
    for (const auto& p : waist.pts) CHECK_THAT(std::abs(p.y - 5.0f), WithinAbs(0.05, 1e-4));
    CHECK(ReadPath(h.scratcher->Closed(BuoyPool::WaistRed)).cmds.empty());

    // The range spines are still emitted — a period has tips whether or not its
    // distribution can be drawn — and with min == max == mean they span zero price, so
    // they are nominal: two spine cores, no contour, every point on the mean level.
    const PathData body = ReadPath(h.scratcher->Closed(BuoyPool::BodyGreen));
    CHECK(body.cmds.size() == 2 * 5);
    REQUIRE(body.pts.size() == 2 * 4);
    for (const auto& p : body.pts) CHECK_THAT(p.y, WithinAbs(5.0, 1e-4));
    CHECK(ReadPath(h.scratcher->Closed(BuoyPool::BodyRed)).cmds.empty());

    // The lone-trade active buoy is special too, and its price band (7) excludes the
    // previous close (5), so the gray move connector bridges them in the active pool.
    CHECK(ReadPath(h.scratcher->Active(BuoyPool::WaistGreen)).pts.size() == 4);
    CHECK(ReadPath(h.scratcher->Active(BuoyPool::Gray)).cmds.size() == 5);
}

// Raster regression: renders crafted asymmetric buoys through the real panel and checks
// actual pixel coverage — fill-rule artifacts (an opposite-winding spine carving a centre
// slit through the bell) are invisible to path read-back. Also dumps the canvas to
// /tmp/buoy_raster.ppm for visual inspection.
TEST_CASE("Buoy bodies rasterize solid across the range spine", "[quote_scratcher][raster]")
{
    // Render-scaled geometry: 1 s buoys matching the panel's 1 s candle period so a slot
    // is the full 60 px candle width; price floor 85 with 10 px/pt so 85..115 spans the
    // 300 px canvas. Slot 0: heavy volume at 100, lone spike to 110 — long thin upper
    // taper, sharp lower pinch. Slot 1: mirrored, spike down to 90. Slot 2 (active):
    // milder asymmetry.
    HeadlessPanel panel{PanelType::Empty, seconds(1), 60};
    panel.SetViewLeftTimeMs(0);
    panel.SetSceneFloor(SceneFloor{0, 85});
    const tvg::Matrix m = panel.LogicalScene().transform();
    panel.LogicalScene().transform(tvg::Matrix{m.e11, 0.f, m.e13, 0.f, 10.f, m.e23, 0.f, 0.f, 1.f});
    auto scratcher = std::make_shared<EmissionScratcher>(milliseconds(1000), 2500);
    panel.AddScratcher(scratcher);
    scratcher->IngestTradesAt(ToTrades({ {50, 100, 10}, {100, 95, 10}, {150, 105, 10}, {200, 90, 5}, {250, 110, 5}, {300, 100, 10},
                                         {1200, 100, 30}, {1250, 99, 30}, {1300, 101, 20}, {1400, 112, 2}, {1500, 100, 30},
                                         {2200, 100, 30}, {2300, 105, 2}, {2500, 100, 30} }), 2500);
    panel.AllocatePixelBuffer(400, 300);
    panel.Render();

    const uint32_t* pixels = panel.PixelBufferData();
    REQUIRE(pixels != nullptr);
    std::ofstream out("/tmp/buoy_raster.ppm", std::ios::binary);
    out << "P6\n400 300\n255\n";
    for (std::size_t i = 0; i < 400u * 300u; ++i) {
        const uint32_t p = pixels[i];
        out.put(static_cast<char>((p >> 16) & 0xff));
        out.put(static_cast<char>((p >> 8) & 0xff));
        out.put(static_cast<char>(p & 0xff));
    }

    // Slot 1 is a waist-low HALF-NORMAL candle (running-mean truncation lands the mean
    // on min), whose contour winds opposite to a normal bell's — the regression this
    // guards: an opposite-winding spine used to carve the interior to half coverage
    // (slit) and annihilate the thin taper entirely. The interior of every body row
    // must rasterize at the full body green (0, 62, 0).
    // The notation is fill-only: the body contour must carry no stroke — zero width,
    // no stroke paint — so the bounded area alone shows the red/green fill.
    CHECK(scratcher->Closed(BuoyPool::BodyGreen).strokeWidth() == 0.f);
    CHECK(scratcher->Closed(BuoyPool::BodyRed).strokeWidth() == 0.f);

    // kBodyGreen #2c7833 -> 0x78 on the green channel; anything less is a coverage hole.
    constexpr uint32_t kBodyG = 0x78;
    const auto green_at = [&](std::size_t x, std::size_t y) { return (pixels[y * 400 + x] >> 8) & 0xff; };
    for (std::size_t x = 86; x <= 93; ++x) CHECK(green_at(x, 130) == kBodyG);  // wide half-normal interior incl. spine columns
    for (std::size_t x : {89u, 90u}) CHECK(green_at(x, 110) == kBodyG);        // thin taper: spine + wall union, not cancellation
    for (std::size_t x : {29u, 30u}) CHECK(green_at(x, 140) == kBodyG);        // normal bell centre above the waist marker

    // Slot gap: every slot-spanning element stops half a kSlotGapPx short of the slot
    // edge, so neighbouring buoys never fuse into one continuous band. Slot 2 runs
    // x = 120..180; its waist marker's solid core (kWaistGreen #3fa548) covers 121..178
    // and leaves the boundary column at each end as background — which pairs with the
    // neighbour's own inset into the full 2 px gap.
    const auto rgb_at = [&](std::size_t x, std::size_t y) { return pixels[y * 400 + x] & 0x00ffffffu; };
    CHECK(rgb_at(119, 149) == 0u);
    CHECK(rgb_at(120, 149) == 0u);
    CHECK(rgb_at(121, 149) == 0x3fa548u);
    CHECK(rgb_at(178, 149) == 0x3fa548u);
    CHECK(rgb_at(179, 149) == 0u);
    CHECK(rgb_at(180, 149) == 0u);
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

// A period whose range is real but too short on screen for the bell to be read is
// *degraded* (BUOY_CANDLE.md §5) — yet it still has tips, so its range spines must be
// drawn exactly as a readable buoy's are. Only the bell contour is dropped.
TEST_CASE("Degraded periods keep their range spines", "[quote_scratcher][emission][BUOY_GEOMETRY-010]")
{
    using Catch::Matchers::WithinAbs;
    // Slot 0 spans 4..8 and is readable; slot 1 spans 9..11 around a mean of 10 — too
    // little height on screen against a waist the calibration makes far wider, so it fails
    // the ASPECT test and no bell is drawn for it.
    EmissionHarness h{{ {2, 4, 2}, {6, 8, 2}, {12, 9, 2}, {16, 11, 2}, {26, 9, 1} }, 26};

    // Slot 1's min, max and mean all grew from slot 0's, so both its spines and its waist
    // marker take the bullish channel — the same pools slot 0's readable bell uses.
    const PathData body = ReadPath(h.scratcher->Closed(BuoyPool::BodyGreen));
    // Slot 0: two spine cores + two contours. Slot 1: two spine cores and NO contour.
    REQUIRE(body.cmds.size() == 2 * (5 + 26) + 2 * 5);

    // Slot 1's spines sit at its slot centre (10..20 ms -> 15) and span mean..max and
    // mean..min with real extent — this is what a degraded period still shows.
    const std::size_t slot1 = 2 * (4 + 1 + 24 * 3);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK_THAT(std::abs(body.pts[slot1 + i].x - 15.0f), WithinAbs(50.0, 0.01));
        CHECK_THAT(std::abs(body.pts[slot1 + 4 + i].x - 15.0f), WithinAbs(50.0, 0.01));
    }
    CHECK_THAT(body.pts[slot1 + 0].y, WithinAbs(10.0, 1e-4));  // upper spine: mean
    CHECK_THAT(body.pts[slot1 + 1].y, WithinAbs(11.0, 1e-4));  //              .. max
    CHECK_THAT(body.pts[slot1 + 4].y, WithinAbs(10.0, 1e-4));  // lower spine: mean
    CHECK_THAT(body.pts[slot1 + 5].y, WithinAbs(9.0, 1e-4));   //              .. min

    // Its marker is an ordinary waist line: two of them in the bullish pool, one per slot.
    CHECK(ReadPath(h.scratcher->Closed(BuoyPool::WaistGreen)).pts.size() == 2 * 4);
    CHECK(ReadPath(h.scratcher->Closed(BuoyPool::WaistRed)).cmds.empty());
}
