// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <cairo/cairo.h>
#include <elements.hpp>

namespace scratcher::elements {

// A cycfi/elements widget backed by a Cairo image surface and a raw ARGB32 pixel buffer.
// External renderers (e.g. ThorVG SwCanvas) write directly into the buffer; on each draw pass
// the surface is blit'ted into the parent Cairo context.
//
// Two callbacks plumb the lifecycle:
//  - on_resize: invoked whenever the widget's bounds change. Hand the fresh buffer pointer +
//    dimensions over to whatever owns the renderer so it can retarget.
//  - on_render: invoked on every draw pass, just before the blit. The owner should drive its
//    renderer to write a fresh frame into the buffer it received from on_resize.
class PixelBufferElement : public cycfi::elements::element
{
public:
    using on_resize_t = std::function<void(uint32_t* buffer, int stride_pixels, int width, int height)>;
    using on_render_t = std::function<void()>;

    PixelBufferElement() = default;
    ~PixelBufferElement() override;

    void SetOnResize(on_resize_t handler) { mOnResize = std::move(handler); }
    void SetOnRender(on_render_t handler) { mOnRender = std::move(handler); }

    cycfi::elements::view_limits limits(cycfi::elements::basic_context const&) const override;
    void layout(cycfi::elements::context const& ctx) override;
    void draw(cycfi::elements::context const& ctx) override;

private:
    int mWidth = 0;
    int mHeight = 0;
    int mStride = 0;
    std::vector<uint8_t> mPixels;
    cairo_surface_t* mSurface = nullptr;

    on_resize_t mOnResize;
    on_render_t mOnRender;
};

} // namespace scratcher::elements
