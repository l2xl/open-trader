// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "instrument_panel_element.hpp"

#include <utility>

namespace scratcher::elements {

namespace el = cycfi::elements;

InstrumentPanelElement::InstrumentPanelElement(cockpit::PanelType type, seconds candle_period, uint32_t candle_width_pixels, std::weak_ptr<el::view> view, EnsurePrivate)
    : InstrumentPanel(type, candle_period, candle_width_pixels) , mView(std::move(view))
{}

InstrumentPanelElement::~InstrumentPanelElement()
{
    if (mSurface) cairo_surface_destroy(mSurface);
}

std::shared_ptr<InstrumentPanelElement> InstrumentPanelElement::Create(cockpit::PanelType type, seconds candle_period, uint32_t candle_width_pixels, std::weak_ptr<el::view> view)
{
    return std::make_shared<InstrumentPanelElement>(type, candle_period, candle_width_pixels, std::move(view), EnsurePrivate{});
}

void InstrumentPanelElement::Update()
{
    InstrumentPanel::Update();
    Refresh();
}

void InstrumentPanelElement::Refresh()
{
    if (auto view = mView.lock()) {
        view->post([view_ref = mView] {
            if (auto view = view_ref.lock())
                view->base_view::refresh();
        });
    }
}

el::view_limits InstrumentPanelElement::limits(el::basic_context const&) const
{
    return {{380.0f, 240.0f}, {el::full_extent, el::full_extent}};
}

void InstrumentPanelElement::layout(el::context const& ctx)
{
    const float fw = ctx.bounds.width();
    const float fh = ctx.bounds.height();
    if (fw <= 0.0f || fh <= 0.0f)
        return;

    const int w = static_cast<int>(fw);
    const int h = static_cast<int>(fh);
    const auto canvas_rect = OuterCanvasRect();
    if (w == canvas_rect.width() && h == canvas_rect.height())
        return;

    // Panel allocates the render buffer and binds the ThorVG canvas target in one
    // action. We only view the buffer via a cairo surface for elements painting.
    // Tight ARGB32 stride (w * 4 bytes) matches both cairo's required alignment for
    // ARGB32 and the tight ARGB8888 stride the panel binds to the canvas.
    AllocatePixelBuffer(w, h);

    if (mSurface) cairo_surface_destroy(mSurface);
    mSurface = cairo_image_surface_create_for_data( reinterpret_cast<unsigned char*>(PixelBufferData()), CAIRO_FORMAT_ARGB32, w, h, w * 4);
}

void InstrumentPanelElement::draw(el::context const& ctx)
{
    if (!mSurface) return;

    // Circuit A: try-lock + frame-throttle gate. If a worker is mid-Update or the
    // throttle window is still open, OnUpdate() returns immediately and Render()
    // draws the previously-published scene.
    OnUpdate();
    const auto dmg = Render();
    if (!dmg.empty()) {
        cairo_surface_mark_dirty_rectangle(mSurface, dmg.left, dmg.top, dmg.width(), dmg.height());
    }

    cairo_t* cr = &ctx.canvas.cairo_context();
    cairo_save(cr);

    cairo_rectangle(cr, ctx.bounds.left, ctx.bounds.top, ctx.bounds.width(), ctx.bounds.height());
    cairo_clip(cr);

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    cairo_set_source_surface(cr, mSurface, ctx.bounds.left, ctx.bounds.top);
    cairo_paint(cr);

    cairo_restore(cr);
}

} // namespace scratcher::elements
