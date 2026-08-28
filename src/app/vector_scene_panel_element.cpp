// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "vector_scene_panel_element.hpp"

namespace scratcher::elements {

namespace el = cycfi::elements;

VectorSceneElement::VectorSceneElement(cockpit::VectorScenePanel& panel, std::weak_ptr<el::view> view)
    : mPanel(panel), mView(std::move(view))
{}

VectorSceneElement::~VectorSceneElement()
{
    if (mSurface) cairo_surface_destroy(mSurface);
}

void VectorSceneElement::PostRefresh()
{
    if (auto view = mView.lock()) {
        view->post([view_ref = mView] {
            if (auto view = view_ref.lock())
                view->base_view::refresh();
        });
    }
}

el::view_limits VectorSceneElement::limits(el::basic_context const&) const
{
    const auto min = mPanel.MinimalCanvasSize();
    return {{static_cast<float>(min.w), static_cast<float>(min.h)}, {el::full_extent, el::full_extent}};
}

void VectorSceneElement::layout(el::context const& ctx)
{
    const float fw = ctx.bounds.width();
    const float fh = ctx.bounds.height();
    if (fw <= 0.0f || fh <= 0.0f)
        return;

    const int w = static_cast<int>(fw);
    const int h = static_cast<int>(fh);
    const auto canvas_rect = mPanel.OuterCanvasRect();
    if (w == static_cast<int>(canvas_rect.w) && h == static_cast<int>(canvas_rect.h))
        return;

    // The panel allocates the render buffer and binds the ThorVG canvas target in one action; we
    // only view the buffer via a cairo surface. Tight ARGB32 stride (w * 4 bytes) matches both
    // cairo's required alignment for ARGB32 and the tight ARGB8888 stride bound to the canvas.
    mPanel.AllocatePixelBuffer(w, h);

    if (mSurface) cairo_surface_destroy(mSurface);
    mSurface = cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(mPanel.PixelBufferData()), CAIRO_FORMAT_ARGB32, w, h, w * 4);
}

void VectorSceneElement::draw(el::context const& ctx)
{
    if (!mSurface) return;

    // Circuit A: try-lock + frame-throttle gate. If a worker is mid-Update or the throttle window
    // is still open, OnUpdate() returns immediately and Render() draws the previously-published scene.
    mPanel.OnUpdate();
    const auto dmg = mPanel.Render();
    if (!dmg.empty()) {
        cairo_surface_mark_dirty_rectangle(mSurface, static_cast<int>(dmg.x), static_cast<int>(dmg.y), static_cast<int>(dmg.w), static_cast<int>(dmg.h));
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
