// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#include "price_ruler.hpp"

#include <algorithm>

#include <thorvg.h>

#include "instrument_panel.hpp"

namespace scratcher::cockpit {

namespace {

constexpr int kLabelChars = 10;
constexpr int kLabelPadding = 4;

}

void PriceRuler::CalculateSize(InstrumentContentPanel& panel)
{
    const float font_size = panel.DefaultFontSize();
    const int char_width = static_cast<int>(font_size * 0.55f);
    mReservedWidth = char_width * kLabelChars + kLabelPadding * 2;

    PixelRect& rect = panel.MutableClientRect();
    rect.right = std::max(rect.left, rect.right - mReservedWidth);
}

void PriceRuler::EmitChanges(InstrumentContentPanel& panel)
{
    auto* scene = panel.UiScene();
    if (!scene) return;

    const PixelRect& rect = panel.GetClientRect();
    const float axis_x = static_cast<float>(rect.right) + 0.5f;

    auto* axis = tvg::Shape::gen();
    axis->moveTo(axis_x, static_cast<float>(rect.top));
    axis->lineTo(axis_x, static_cast<float>(rect.bottom));
    axis->strokeFill(180, 180, 180, 255);
    axis->strokeWidth(1.0f);
    scene->add(axis);

    auto* label = tvg::Text::gen();
    label->font(panel.DefaultFontName());
    label->size(panel.DefaultFontSize());
    label->text("Price");
    label->fill(200, 200, 200);
    const float label_x = axis_x + static_cast<float>(kLabelPadding);
    const float label_y = static_cast<float>(rect.top) + panel.DefaultFontSize() + static_cast<float>(kLabelPadding);
    label->translate(label_x, label_y);
    scene->add(label);
}

}
