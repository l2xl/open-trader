// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#include "price_ruler.hpp"

#include <algorithm>
#include <climits>
#include <cmath>
#include <string>

#include "currency.hpp"
#include "instrument_panel.hpp"

namespace scratcher::cockpit {

namespace {

constexpr float kLabelFontScale       = 0.85f;  // labels slightly smaller than the body font
constexpr int   kLabelPadding         = 4;      // px between tick stub and label, and strip margin
constexpr int   kTickLength           = 4;
constexpr int   kMinLabelGapPx        = 6;      // minimum vertical gap between adjacent labels
constexpr int   kTargetTickSpacingPx  = 44;     // desired px between gridlines
constexpr int   kAvgCharWidthN        = 7;      // char_width ≈ font_size * 7/10 (digit-wide, with
constexpr int   kAvgCharWidthD        = 10;     // margin so the widest price label clears the strip)
constexpr int   kMinLabelChars        = 6;      // floor on the reserved strip's label width

int approx_char_px(float font_size)
{
    return std::max(1, static_cast<int>(font_size) * kAvgCharWidthN / kAvgCharWidthD);
}

int approx_label_px(const std::string& s, float font_size)
{
    return static_cast<int>(s.size()) * approx_char_px(font_size);
}

std::string format_price(uint64_t points, std::size_t decimals)
{
    return currency<uint64_t>(points, decimals).to_string();
}

// Smallest "nice" step (1, 2 or 5 × 10^k, in price points) that is at least `target`. Mirrors
// the time ruler's nice-step philosophy so gridlines land on round, human-readable prices.
uint64_t nice_step(double target)
{
    if (!(target > 1.0)) return 1;
    const double exp  = std::floor(std::log10(target));
    const double base = std::pow(10.0, exp);
    const double frac = target / base;
    const double mult = frac <= 1.0 ? 1.0 : frac <= 2.0 ? 2.0 : frac <= 5.0 ? 5.0 : 10.0;
    return std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(mult * base)));
}

// Emit a left-anchored Text into a scene at HUD-pixel coordinates. Counter-flips Y so glyphs
// render upright under the HUD scene's Y-flip-about-canvas_h transform.
void emit_label(tvg::Scene& scene, const std::string& font, float font_size, const std::string& text,
                float x, float y_hud, uint8_t r, uint8_t g, uint8_t b)
{
    tvg_ptr<tvg::Text> lbl{tvg::Text::gen()};
    lbl->font(font.c_str());
    lbl->size(font_size);
    lbl->text(text.c_str());
    lbl->fill(r, g, b);
    lbl->align(0.0f, 0.0f);
    lbl->transform(tvg::Matrix{1.0f, 0.0f, x,
                               0.0f, -1.0f, y_hud,
                               0.0f, 0.0f, 1.0f});
    scene.add(lbl.get());
}

} // namespace

void PriceRuler::OnAttach(InstrumentPanel& panel)
{
    mScene.reset(tvg::Scene::gen());
    mAxisShape.reset(tvg::Shape::gen());
    mTickLinesShape.reset(tvg::Shape::gen());
    mLabelScene.reset(tvg::Scene::gen());

    mScene->add(mAxisShape.get());
    mScene->add(mTickLinesShape.get());
    mScene->add(mLabelScene.get());

    panel.HudScene().add(mScene.get());
}

void PriceRuler::OnDetach(InstrumentPanel& /*panel*/)
{
    mLabelScene.reset();
    mTickLinesShape.reset();
    mAxisShape.reset();
    mScene.reset();
}

void PriceRuler::CalculateSize(InstrumentPanel& panel)
{
    const float font_size = panel.DefaultFontSize() * kLabelFontScale;

    // Reserve a strip wide enough for the largest visible price label. The inner-rect top maps to
    // the highest visible price (most digits); read it from the live transform via PriceOfHudY.
    // CalculateSize runs before this frame's ApplyLogicalSceneTransform, so the matrix here is the
    // previous layout's — stable frame-to-frame, and floored at a minimum char count before any
    // price scale exists (first frames, e22 == 0).
    const float canvas_h = static_cast<float>(panel.OuterCanvasRect().height());
    const uint64_t top_pts = panel.PriceOfHudY(canvas_h);
    const std::string sample = format_price(top_pts, panel.PriceDecimals());
    const int label_w = std::max(approx_label_px(sample, font_size), kMinLabelChars * approx_char_px(font_size));
    mReservedWidth = kTickLength + kLabelPadding + label_w + kLabelPadding;

    PixelRect& rect = panel.MutableInnerDataRect();
    rect.right = std::max(rect.left, rect.right - mReservedWidth);
}

void PriceRuler::OnLayout(InstrumentPanel& panel)
{
    RebuildAll(panel);
}

void PriceRuler::RebuildAll(InstrumentPanel& panel)
{
    if (!mScene) return;

    const PixelRect& rect = panel.InnerDataRect();
    if (rect.width() <= 0 || rect.height() <= 0) return;

    const float canvas_h = static_cast<float>(panel.OuterCanvasRect().height());
    const float font_size = panel.DefaultFontSize() * kLabelFontScale;
    const ScenePixelSize hud_px = panel.PixelSizeOf(panel.HudScene());

    // Axis at the strip's left edge (= rect.right in canvas-Y-down). +0.5 px centres the
    // half-pixel-wide stroke on a pixel boundary, mirroring the time ruler.
    const float axis_x       = static_cast<float>(rect.right) + 0.5f * hud_px.x;
    const float y_top_hud    = canvas_h - static_cast<float>(rect.top);
    const float y_bottom_hud = canvas_h - static_cast<float>(rect.bottom);

    mAxisShape->reset();
    mAxisShape->moveTo(axis_x, y_top_hud);
    mAxisShape->lineTo(axis_x, y_bottom_hud);
    mAxisShape->strokeFill(180, 180, 180, 255);
    mAxisShape->strokeWidth(0.5f * hud_px.x);

    mTickLinesShape->reset();
    mLabelScene->remove();

    // Visible price band, recovered by inverting the inner-rect top/bottom through the live
    // transform. Before any quote data the price scale is degenerate (e22 == 0 → top == bottom),
    // so only the axis is drawn.
    const uint64_t top_pts    = panel.PriceOfHudY(y_top_hud);
    const uint64_t bottom_pts = panel.PriceOfHudY(y_bottom_hud);
    if (top_pts <= bottom_pts) {
        mTickLinesShape->strokeFill(180, 180, 180, 255);
        mTickLinesShape->strokeWidth(0.5f * hud_px.y);
        return;
    }

    const std::size_t pd = panel.PriceDecimals();
    const double pts_per_px = static_cast<double>(top_pts - bottom_pts) / static_cast<double>(rect.height());
    const uint64_t step = nice_step(pts_per_px * kTargetTickSpacingPx);

    const float label_x = axis_x + static_cast<float>(kTickLength + kLabelPadding);
    // Labels run bottom→top (price ascending → canvas-Y descending). Track the last emitted
    // label's top edge in canvas-Y-down so the next, higher label can be suppressed if it would
    // collide.
    int last_label_top_px = INT_MAX;

    for (uint64_t tick = (bottom_pts / step + 1) * step; tick <= top_pts; tick += step) {
        const float hud_y = panel.HudYOfPrice(tick);

        // Tick stub: short horizontal mark extending right from the axis into the strip.
        mTickLinesShape->moveTo(axis_x, hud_y);
        mTickLinesShape->lineTo(axis_x + static_cast<float>(kTickLength), hud_y);

        const int label_center_px = static_cast<int>(canvas_h - hud_y);
        const int half_h          = static_cast<int>(font_size * 0.5f);
        const int label_top_px    = label_center_px - half_h;
        const int label_bottom_px = label_center_px + half_h;
        if (label_bottom_px + kMinLabelGapPx <= last_label_top_px) {
            emit_label(*mLabelScene, panel.DefaultFontName(), font_size, format_price(tick, pd),
                       label_x, hud_y + font_size * 0.5f, 210, 210, 210);
            last_label_top_px = label_top_px;
        }
    }

    mTickLinesShape->strokeFill(180, 180, 180, 255);
    mTickLinesShape->strokeWidth(0.5f * hud_px.y);
}

}
