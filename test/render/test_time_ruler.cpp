// Renders InstrumentContentPanel into a Cairo-backed RGBA buffer and writes PNGs to disk.
// The PNGs are intended to be inspected visually (e.g. by Claude Code) to verify the
// TimeRuler tick algorithm at a wide range of zoom levels and calendar anchors.
//
// Output directory is set at build time via TIME_RULER_OUTPUT_DIR (an absolute path
// pointing inside the build tree). Fonts are loaded from `<cwd>/resources/`, so the
// fixture chdir()s into ELSCRATCHER_RUNTIME_DIR (where the elscratcher exe + fonts live).

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <cairo/cairo.h>
#include <unistd.h>

#include "instrument_panel.hpp"

namespace {

namespace fs = std::filesystem;

struct CairoSurfaceDeleter
{
    void operator()(cairo_surface_t* s) const noexcept { if (s) cairo_surface_destroy(s); }
};
using cairo_surface_ptr = std::unique_ptr<cairo_surface_t, CairoSurfaceDeleter>;

class TestPanel final : public scratcher::cockpit::InstrumentPanel
{
public:
    TestPanel(std::chrono::seconds candle_period, uint32_t candle_width)
        : InstrumentPanel(scratcher::cockpit::PanelType::MarketGraph, candle_period, candle_width)
    {}

    //void PostToUi(std::function<void()> fn) override { fn(); }
    void Refresh() override {}
};

// Compose ThorVG's straight-alpha output onto a dark gray background so labels are
// visible in the saved PNG. ThorVG writes (0,0,0,0) where it drew nothing; we replace
// those pixels with the chart background colour.
//
// Cairo's CAIRO_FORMAT_ARGB32 is premultiplied; ThorVG ARGB8888 is straight. Before we
// can write a meaningful PNG we have to (a) composite over the bg, and (b) leave the
// result in premultiplied form so Cairo's PNG writer encodes it correctly.
void compose_over_bg(std::span<uint32_t> px, uint8_t bg_r, uint8_t bg_g, uint8_t bg_b)
{
    for (uint32_t& p : px) {
        const uint8_t a = (p >> 24) & 0xFF;
        const uint8_t r = (p >> 16) & 0xFF;
        const uint8_t g = (p >>  8) & 0xFF;
        const uint8_t b = (p      ) & 0xFF;

        // straight-alpha-over-opaque bg → opaque RGB
        const uint8_t out_r = static_cast<uint8_t>((r * a + bg_r * (255 - a)) / 255);
        const uint8_t out_g = static_cast<uint8_t>((g * a + bg_g * (255 - a)) / 255);
        const uint8_t out_b = static_cast<uint8_t>((b * a + bg_b * (255 - a)) / 255);

        // CAIRO_FORMAT_ARGB32 expects premultiplied; with alpha=255, premul == straight.
        p = (static_cast<uint32_t>(0xFF) << 24)
          | (static_cast<uint32_t>(out_r) << 16)
          | (static_cast<uint32_t>(out_g) <<  8)
          |  static_cast<uint32_t>(out_b);
    }
}

void render_panel_to_png(const fs::path& out_path,
                         std::chrono::seconds candle_period,
                         uint32_t  candle_width_px,
                         int       canvas_w,
                         int       canvas_h,
                         uint64_t  scene_floor_ms)
{
    TestPanel panel(candle_period, candle_width_px);
    // Pin the precision floor and the view's left edge to the same calendar
    // anchor: the floor's identity is now decoupled from the view, so the
    // test must explicitly say "draw this calendar time at the left edge".
    panel.SetSceneFloor(scratcher::cockpit::SceneFloor{scene_floor_ms, 0});
    panel.SetViewLeftTimeMs(static_cast<int64_t>(scene_floor_ms));
    panel.AllocatePixelBuffer(canvas_w, canvas_h);
    panel.Render();

    // Wrap the panel-owned render buffer in a cairo surface for PNG output.
    // Tight ARGB32 stride: width pixels per row, 4 bytes per pixel — matches
    // the layout the panel binds to the ThorVG canvas. The surface is declared
    // AFTER panel so it tears down first; panel keeps the storage alive across
    // the write_to_png call.
    const std::span<uint32_t> data{
        panel.PixelBufferData(),
        static_cast<size_t>(canvas_w) * static_cast<size_t>(canvas_h)
    };
    compose_over_bg(data, 25, 25, 30);

    cairo_surface_ptr surface{
        cairo_image_surface_create_for_data(
            reinterpret_cast<unsigned char*>(panel.PixelBufferData()),
            CAIRO_FORMAT_ARGB32, canvas_w, canvas_h, canvas_w * 4)};
    REQUIRE(cairo_surface_status(surface.get()) == CAIRO_STATUS_SUCCESS);
    cairo_surface_mark_dirty(surface.get());

    fs::create_directories(out_path.parent_path());
    REQUIRE(cairo_surface_write_to_png(surface.get(), out_path.c_str()) == CAIRO_STATUS_SUCCESS);
    // surface destroyed first (declared later), then panel — no dangling refs.
}

struct CwdFixture
{
    CwdFixture()
    {
        // Fonts are next to the elscratcher exe (resources/OpenSans-Regular.ttf).
        // LoadDefaultFont() in InstrumentContentPanel uses CWD-relative lookup.
        if (chdir(ELSCRATCHER_RUNTIME_DIR) != 0)
            FAIL("Failed to chdir to ELSCRATCHER_RUNTIME_DIR");
    }
};

// 2026-05-03 00:00:00 UTC
constexpr uint64_t kFloor_2026_05_03_00_00 = 1777766400000ULL;
// 2026-05-03 12:00:00 UTC
constexpr uint64_t kFloor_2026_05_03_12_00 = 1777809600000ULL;
// 2026-01-01 00:00:00 UTC
constexpr uint64_t kFloor_2026_01_01_00_00 = 1767225600000ULL;
// 2020-01-01 00:00:00 UTC
constexpr uint64_t kFloor_2020_01_01_00_00 = 1577836800000ULL;
// 2026-05-03 14:32:15 UTC — deliberately misaligned with any whole minute/hour
constexpr uint64_t kFloor_2026_05_03_14_32_15 = 1777818735000ULL;

constexpr int kCanvasW = 800;
constexpr int kCanvasH = 60;

fs::path out_dir() { return fs::path{TIME_RULER_OUTPUT_DIR}; }

} // namespace

TEST_CASE_METHOD(CwdFixture, "TimeRuler image series — zoomed across seconds-to-years", "[time_ruler][render][series]")
{
    using namespace std::chrono_literals;

    SECTION("01 — sub-minute (1s candles, 10px each → ~80s span)") {
        render_panel_to_png(out_dir() / "01_sub_minute_1s_10px.png",
                            1s, 10, kCanvasW, kCanvasH, kFloor_2026_05_03_12_00);
    }
    SECTION("02 — sub-hour (60s candles, 20px each → ~40min, default zoom)") {
        render_panel_to_png(out_dir() / "02_sub_hour_60s_20px.png",
                            60s, 20, kCanvasW, kCanvasH, kFloor_2026_05_03_12_00);
    }
    SECTION("03 — sub-hour misaligned floor (60s, 20px, floor at 14:32:15)") {
        render_panel_to_png(out_dir() / "03_sub_hour_misaligned.png",
                            60s, 20, kCanvasW, kCanvasH, kFloor_2026_05_03_14_32_15);
    }
    SECTION("04 — hours-spanning (60s candles, 4px each → ~3.3h)") {
        render_panel_to_png(out_dir() / "04_hours_60s_4px.png",
                            60s, 4, kCanvasW, kCanvasH, kFloor_2026_05_03_00_00);
    }
    SECTION("05 — day-spanning hourly (1h candles, 20px each → ~40h)") {
        render_panel_to_png(out_dir() / "05_days_1h_20px.png",
                            3600s, 20, kCanvasW, kCanvasH, kFloor_2026_05_03_00_00);
    }
    SECTION("06 — month-spanning daily (1d candles, 20px each → ~40d, May→Jun)") {
        render_panel_to_png(out_dir() / "06_month_1d_20px.png",
                            86400s, 20, kCanvasW, kCanvasH, kFloor_2026_05_03_00_00);
    }
    SECTION("07 — year-spanning daily (1d candles, 2px each → ~400d crosses a year)") {
        render_panel_to_png(out_dir() / "07_year_1d_2px.png",
                            86400s, 2, kCanvasW, kCanvasH, kFloor_2026_01_01_00_00);
    }
    SECTION("08 — multi-year daily zoomed-out (1d candles, 1px each → ~2.2y)") {
        render_panel_to_png(out_dir() / "08_multiyear_1d_1px.png",
                            86400s, 1, kCanvasW, kCanvasH, kFloor_2020_01_01_00_00);
    }
    SECTION("09 — decade weekly (7d candles, 1px each → ~15y)") {
        render_panel_to_png(out_dir() / "09_decade_7d_1px.png",
                            std::chrono::seconds{7 * 86400}, 1, kCanvasW, kCanvasH, kFloor_2020_01_01_00_00);
    }
}
