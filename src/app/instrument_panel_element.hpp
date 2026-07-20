// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <chrono>
#include <memory>

#include <cairo/cairo.h>
#include <elements.hpp>

#include "instrument_panel.hpp"

namespace scratcher::elements {

// Cycfi/Elements widget that IS the InstrumentPanel: it wraps the panel-owned render
// buffer in a cairo image surface for elements painting, and routes layout()/draw()
// directly to InstrumentPanel's AllocatePixelBuffer/OnUpdate/Render. Replaces the
// previous two-class arrangement (InstrumentPanelElement wrapping a PixelBufferElement)
// — one less indirection, no callback plumbing between size/render and the cockpit panel.
class InstrumentPanelElement : public cockpit::InstrumentPanel, public cycfi::elements::element
{
    struct EnsurePrivate {};

public:
    InstrumentPanelElement(cockpit::PanelType type, seconds candle_period, uint32_t candle_width_pixels, std::weak_ptr<cycfi::elements::view> view, EnsurePrivate);
    ~InstrumentPanelElement() override;

    static std::shared_ptr<InstrumentPanelElement> Create(cockpit::PanelType type, seconds candle_period, uint32_t candle_width_pixels, std::weak_ptr<cycfi::elements::view> view);

    // ContentPanel overrides
    void Update() override;
    void Refresh() override;

    // cycfi::elements::element overrides
    cycfi::elements::view_limits limits(cycfi::elements::basic_context const&) const override;
    void layout(cycfi::elements::context const& ctx) override;
    void draw(cycfi::elements::context const& ctx) override;

private:
    std::weak_ptr<cycfi::elements::view> mView;
    cairo_surface_t* mSurface = nullptr;
};

} // namespace scratcher::elements
