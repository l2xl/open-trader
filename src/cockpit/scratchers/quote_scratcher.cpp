// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#include "quote_scratcher.hpp"

#include <cstdint>

#include "instrument_panel.hpp"

namespace scratcher::cockpit {

namespace {

struct Color { uint8_t r, g, b, a; };

constexpr Color kGreen{40, 200, 80, 255};
constexpr Color kRed{220, 50, 50, 255};

constexpr Color BuoyColor(uint64_t prev, uint64_t curr)
{
    return curr >= prev ? kGreen : kRed;
}

inline float SubToFloat(uint64_t value, uint64_t floor)
{
    return static_cast<float>(static_cast<int64_t>(value) - static_cast<int64_t>(floor));
}

void EmitLine(tvg::Scene& scene, float x1, float y1, float x2, float y2, Color c)
{
    tvg_ptr<tvg::Shape> line{tvg::Shape::gen()};
    line->moveTo(x1, y1);
    line->lineTo(x2, y2);
    line->strokeFill(c.r, c.g, c.b, c.a);
    line->strokeWidth(1.0f);
    scene.add(line.get());
}

void EmitBuoy(tvg::Scene& scene, uint64_t buoy_ts, uint64_t duration,
              const BuoyCandleQuotes::candle_t& curr,
              const BuoyCandleQuotes::candle_t& prev,
              const SceneFloor& floor)
{
    const float mid_x = SubToFloat(buoy_ts + duration / 2, floor.time_ms);
    const float left_x = SubToFloat(buoy_ts, floor.time_ms);
    const float right_x = SubToFloat(buoy_ts + duration, floor.time_ms);

    const float min_y = SubToFloat(curr.min, floor.price_points);
    const float max_y = SubToFloat(curr.max, floor.price_points);
    const float mean_y = SubToFloat(curr.mean, floor.price_points);

    EmitLine(scene, mid_x, min_y, mid_x, mean_y, BuoyColor(prev.min, curr.min));
    EmitLine(scene, mid_x, mean_y, mid_x, max_y, BuoyColor(prev.max, curr.max));
    EmitLine(scene, left_x, mean_y, right_x, mean_y, BuoyColor(prev.mean, curr.mean));
}

}

void QuoteScratcher::OnAttach(InstrumentContentPanel& panel)
{
    mScene.reset(tvg::Scene::gen());
    panel.LogicalScene().add(mScene.get());
}

void QuoteScratcher::OnDetach(InstrumentContentPanel& /*panel*/)
{
    mScene.reset();
}

void QuoteScratcher::OnLayout(InstrumentContentPanel& panel)
{
    if (!mScene) return;
    mScene->remove();

    if (!mQuotes.first_buoy_timestamp()) return;

    // QuoteScratcher emits in (timestamp_ms - floor.t, price_pts - floor.p) coordinates.
    // High-price buoys land at higher logical Y, and the HUD's outer Y-flip carries that
    // upward on canvas — no explicit flip in this scratcher.
    auto& scene = *mScene;

    const SceneFloor& floor = panel.GetSceneFloor();
    const uint64_t duration = mQuotes.buoy_duration();
    uint64_t buoy_ts = *mQuotes.first_buoy_timestamp();

    BuoyCandleQuotes::candle_t prev{};
    bool has_prev = false;

    for (const auto& buoy : mQuotes.quotes()) {
        EmitBuoy(scene, buoy_ts, duration, buoy, has_prev ? prev : buoy, floor);
        prev = buoy;
        has_prev = true;
        buoy_ts += duration;
    }

    const auto active = mQuotes.active_candle();
    if (active.volume > 0) {
        EmitBuoy(scene, buoy_ts, duration, active, has_prev ? prev : active, floor);
    }
}

}
