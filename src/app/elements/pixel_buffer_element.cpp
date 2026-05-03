// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#include "pixel_buffer_element.hpp"

namespace scratcher::elements {

namespace el = cycfi::elements;

PixelBufferElement::~PixelBufferElement()
{
    if (mSurface) cairo_surface_destroy(mSurface);
}

el::view_limits PixelBufferElement::limits(el::basic_context const&) const
{
    return {{120.0f, 80.0f}, {el::full_extent, el::full_extent}};
}

void PixelBufferElement::layout(el::context const& ctx)
{
    const float fw = ctx.bounds.width();
    const float fh = ctx.bounds.height();
    if (fw <= 0.0f || fh <= 0.0f) return;

    const int w = static_cast<int>(fw);
    const int h = static_cast<int>(fh);
    if (w == mWidth && h == mHeight) return;

    mWidth = w;
    mHeight = h;
    mStride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
    mPixels.assign(static_cast<size_t>(mStride) * h, 0);

    if (mSurface) cairo_surface_destroy(mSurface);
    mSurface = cairo_image_surface_create_for_data(mPixels.data(), CAIRO_FORMAT_ARGB32, w, h, mStride);

    if (mOnResize) {
        mOnResize(reinterpret_cast<uint32_t*>(mPixels.data()), mStride / 4, w, h);
    }
}

void PixelBufferElement::draw(el::context const& ctx)
{
    if (!mSurface) return;

    if (mOnRender) {
        const auto dmg = mOnRender();
        if (!dmg.empty()) {
            cairo_surface_mark_dirty_rectangle(mSurface, dmg.left, dmg.top, dmg.width(), dmg.height());
        }
    }

    cairo_t* cr = &ctx.canvas.cairo_context();
    cairo_save(cr);

    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_rectangle(cr, ctx.bounds.left, ctx.bounds.top, ctx.bounds.width(), ctx.bounds.height());
    cairo_fill(cr);

    cairo_set_source_surface(cr, mSurface, ctx.bounds.left, ctx.bounds.top);
    cairo_rectangle(cr, ctx.bounds.left, ctx.bounds.top, ctx.bounds.width(), ctx.bounds.height());
    cairo_fill(cr);

    cairo_restore(cr);
}

} // namespace scratcher::elements
