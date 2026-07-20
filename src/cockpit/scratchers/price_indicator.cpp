// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "price_indicator.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

#include "currency.hpp"
#include "instrument_panel.hpp"

namespace scratcher::cockpit {

namespace {

constexpr float kLabelFontScale  = 0.85f;
constexpr int   kBoxPaddingX     = 5;
constexpr int   kBoxPaddingY     = 2;
constexpr float kBoxCornerRadius = 3.0f;   // rounded-corner radius shared by the tag fill + border
constexpr float kBoxBorderWidth  = 1.0f;
constexpr int   kAvgCharWidthN   = 7;   // digit-wide estimate, matching the price ruler's strip
constexpr int   kAvgCharWidthD   = 10;

struct Color { uint8_t r, g, b; };
constexpr Color kUpFill{0, 140, 0};
constexpr Color kUpBorder{70, 205, 70};
constexpr Color kDownFill{170, 0, 0};
constexpr Color kDownBorder{235, 85, 85};
constexpr Color kLineColor{150, 150, 150};

int approx_label_px(const std::string& s, float font_size)
{
    const int char_w = std::max(1, static_cast<int>(font_size) * kAvgCharWidthN / kAvgCharWidthD);
    return static_cast<int>(s.size()) * char_w;
}

} // namespace

void PriceIndicator::OnAttach(InstrumentPanel& panel)
{
    mScene.reset(tvg::Scene::gen());
    mLine.reset(tvg::Shape::gen());
    mBox.reset(tvg::Shape::gen());
    mLabel.reset(tvg::Text::gen());

    mScene->add(mLine.get());
    mScene->add(mBox.get());
    mScene->add(mLabel.get());

    panel.HudScene().add(mScene.get());
}

void PriceIndicator::OnDetach(InstrumentPanel& /*panel*/)
{
    mLabel.reset();
    mBox.reset();
    mLine.reset();
    mScene.reset();
}

void PriceIndicator::OnLayout(InstrumentPanel& panel)
{
    if (!mScene) return;

    // Clear last frame's geometry up front so every early-out leaves the overlay empty.
    mLine->reset();
    mBox->reset();
    mLabel->text("");

    const auto quote = panel.QuoteScratcherInstance();
    if (!quote || !quote->FirstBuoyTimestamp()) return;

    const PixelRect& rect = panel.InnerDataRect();
    if (rect.width() <= 0 || rect.height() <= 0) return;

    // close carries the most recent trade price (held forward through empty periods); mean is the
    // active period's VWAP. PriceAutoscale always includes the active candle, so the last price
    // sits inside the visible band — no clamping needed.
    const auto candle = quote->GetActiveCandle();
    const std::size_t pd = panel.PriceDecimals();
    const uint64_t last_pts = candle.close.raw_at(pd);
    if (last_pts == 0) return;

    const float font_size = panel.DefaultFontSize() * kLabelFontScale;
    const ScenePixelSize hud_px = panel.PixelSizeOf(panel.HudScene());
    const float hud_y = panel.HudYOfPrice(last_pts);

    // Dotted horizontal line across the inner rect at the last price.
    const float dash[] = {1.5f * hud_px.x, 2.5f * hud_px.x};
    mLine->moveTo(static_cast<float>(rect.left), hud_y);
    mLine->lineTo(static_cast<float>(rect.right), hud_y);
    mLine->strokeFill(kLineColor.r, kLineColor.g, kLineColor.b, 255);
    mLine->strokeWidth(1.0f * hud_px.y);
    mLine->strokeDash(dash, 2);

    // Rounded price tag in the strip, vertically centred on the line; fill + border coloured by
    // last-vs-mean bias. The single rounded-rect path carries both the fill and the stroke, so the
    // corners round for the box and its border together.
    const bool up = candle.close >= candle.mean;
    const Color fill_c   = up ? kUpFill : kDownFill;
    const Color border_c = up ? kUpBorder : kDownBorder;

    const std::string text = currency<uint64_t>(last_pts, pd).to_string();
    const float box_w    = static_cast<float>(approx_label_px(text, font_size) + kBoxPaddingX * 2);
    const float box_h    = font_size + static_cast<float>(kBoxPaddingY) * 2.0f;
    const float box_left = static_cast<float>(rect.right) + 1.0f;
    const float radius   = kBoxCornerRadius * hud_px.x;

    mBox->appendRect(box_left, hud_y - 0.5f * box_h, box_w, box_h, radius, radius);
    mBox->fill(fill_c.r, fill_c.g, fill_c.b, 255);
    mBox->strokeFill(border_c.r, border_c.g, border_c.b, 255);
    mBox->strokeWidth(kBoxBorderWidth * hud_px.x);

    // White price text, centred in the box: align(0.5, 0.5) anchors the text's own bbox centre so
    // the glyphs — not the top-left corner — land on the box centre, eliminating the bbox-padding
    // offset. The counter-flip keeps the glyphs upright under HUD's Y-flip.
    mLabel->font(panel.DefaultFontName().c_str());
    mLabel->size(font_size);
    mLabel->text(text.c_str());
    mLabel->fill(255, 255, 255);
    mLabel->align(0.5f, 0.5f);
    mLabel->transform(tvg::Matrix{1.0f, 0.0f, box_left + 0.5f * box_w,
                                  0.0f, -1.0f, hud_y,
                                  0.0f, 0.0f, 1.0f});
}

}
