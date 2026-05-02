// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#include "time_ruler.hpp"

#include <algorithm>

#include <thorvg.h>

#include "instrument_panel.hpp"

namespace scratcher::cockpit {

namespace {

constexpr int kLabelPadding = 4;

}

void TimeRuler::CalculateSize(InstrumentContentPanel& panel)
{
    const float font_size = panel.DefaultFontSize();
    mReservedHeight = static_cast<int>(font_size * 1.2f) + kLabelPadding * 2;

    PixelRect& rect = panel.MutableInnerDataRect();
    rect.bottom = std::max(rect.top, rect.bottom - mReservedHeight);
}

void TimeRuler::EmitChanges(InstrumentContentPanel& panel)
{
    auto* scene = panel.UiScene();
    if (!scene) return;

    const PixelRect& rect = panel.InnerDataRect();
    const float axis_y = static_cast<float>(rect.bottom) + 0.5f;

    auto* axis = tvg::Shape::gen();
    axis->moveTo(static_cast<float>(rect.left), axis_y);
    axis->lineTo(static_cast<float>(rect.right), axis_y);
    axis->strokeFill(180, 180, 180, 255);
    axis->strokeWidth(0.5f);
    scene->add(axis);

    auto* label = tvg::Text::gen();
    label->font(panel.DefaultFontName());
    label->size(panel.DefaultFontSize());
    label->text("Time");
    label->fill(200, 200, 200);
    const float label_x = static_cast<float>(rect.left) + static_cast<float>(kLabelPadding);
    const float label_y = axis_y + panel.DefaultFontSize() + static_cast<float>(kLabelPadding);
    label->translate(label_x, label_y);
    scene->add(label);
}

}
