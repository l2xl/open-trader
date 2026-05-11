// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#include "price_ruler.hpp"

#include <algorithm>

#include "instrument_panel.hpp"

namespace scratcher::cockpit {

namespace {

constexpr int kLabelChars = 10;
constexpr int kLabelPadding = 4;

}

void PriceRuler::CalculateSize(InstrumentPanel& panel)
{
    const float font_size = panel.DefaultFontSize();
    const int char_width = static_cast<int>(font_size * 0.55f);
    mReservedWidth = char_width * kLabelChars + kLabelPadding * 2;

    PixelRect& rect = panel.MutableInnerDataRect();
    rect.right = std::max(rect.left, rect.right - mReservedWidth);
}

void PriceRuler::OnAttach(InstrumentPanel& panel)
{
    mScene.reset(tvg::Scene::gen());
    panel.HudScene().add(mScene.get());
}

void PriceRuler::OnDetach(InstrumentPanel& /*panel*/)
{
    mScene.reset();
}

void PriceRuler::OnLayout(InstrumentPanel& panel)
{
    if (!mScene) return;
    mScene->remove();
    auto& scene = *mScene;

    const PixelRect& rect = panel.InnerDataRect();
    const float canvas_h = static_cast<float>(panel.OuterCanvasRect().height());

    // HUD-Y-up conversion: canvas Y → HUD-y is canvas_h - canvas_y.
    // The +0.5f half-pixel offset for the vertical axis is on X (rect.right + 0.5),
    // so it carries through unchanged.
    const float axis_x = static_cast<float>(rect.right) + 0.5f;
    const float y_top_hud    = canvas_h - static_cast<float>(rect.top);
    const float y_bottom_hud = canvas_h - static_cast<float>(rect.bottom);

    tvg_ptr<tvg::Shape> axis{tvg::Shape::gen()};
    axis->moveTo(axis_x, y_top_hud);
    axis->lineTo(axis_x, y_bottom_hud);
    axis->strokeFill(180, 180, 180, 255);
    axis->strokeWidth(0.5f);
    scene.add(axis.get());

    // Counter-flip per Text paint: under HUD's Y-flip, glyph local +Y (downward in
    // text-internal coords) would render as upward on canvas. {1, 0, x; 0, -1, y_hud; 0, 0, 1}
    // inverts the text's local Y so the resultant canvas orientation is upright.
    tvg_ptr<tvg::Text> label{tvg::Text::gen()};
    label->font(panel.DefaultFontName().c_str());
    label->size(panel.DefaultFontSize());
    label->text("Price");
    label->fill(200, 200, 200);
    const float label_x = axis_x + static_cast<float>(kLabelPadding);
    const float label_canvas_y = static_cast<float>(rect.top) + panel.DefaultFontSize() + static_cast<float>(kLabelPadding);
    const float label_y_hud = canvas_h - label_canvas_y;
    label->transform(tvg::Matrix{1.0f, 0.0f, label_x,
                                 0.0f, -1.0f, label_y_hud,
                                 0.0f, 0.0f, 1.0f});
    scene.add(label.get());
}

}
