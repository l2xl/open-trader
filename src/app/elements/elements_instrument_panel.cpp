// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
//
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b25tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

#include "elements_instrument_panel.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <cairo/cairo.h>
#include <elements.hpp>

namespace scratcher::elements {

namespace el = cycfi::elements;

class ChartElement : public el::element
{
    std::weak_ptr<cockpit::InstrumentContentPanel> mPanel;
    int mWidth = 0;
    int mHeight = 0;
    int mStride = 0;
    std::vector<uint8_t> mPixels;
    cairo_surface_t* mSurface = nullptr;

public:
    explicit ChartElement(std::weak_ptr<cockpit::InstrumentContentPanel> panel)
        : mPanel(std::move(panel))
    {}

    ~ChartElement() override
    {
        if (mSurface) cairo_surface_destroy(mSurface);
    }

    el::view_limits limits(el::basic_context const&) const override
    {
        return {{120.0f, 80.0f}, {el::full_extent, el::full_extent}};
    }

    void layout(el::context const& ctx) override
    {
        const int w = static_cast<int>(ctx.bounds.width());
        const int h = static_cast<int>(ctx.bounds.height());
        if (w <= 0 || h <= 0) return;
        if (w == mWidth && h == mHeight) return;

        mWidth = w;
        mHeight = h;
        mStride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, w);
        mPixels.assign(static_cast<size_t>(mStride) * h, 0);

        if (mSurface) cairo_surface_destroy(mSurface);
        mSurface = cairo_image_surface_create_for_data(mPixels.data(), CAIRO_FORMAT_ARGB32, w, h, mStride);

        if (auto panel = mPanel.lock()) {
            panel->SetTarget(reinterpret_cast<uint32_t*>(mPixels.data()),
                             static_cast<uint32_t>(mStride / 4),
                             static_cast<uint32_t>(w),
                             static_cast<uint32_t>(h));
            panel->OnSize(w, h);
        }
    }

    void draw(el::context const& ctx) override
    {
        auto panel = mPanel.lock();
        if (!panel || !mSurface) return;

        panel->Render();
        cairo_surface_mark_dirty(mSurface);

        cairo_t* cr = &ctx.canvas.cairo_context();
        cairo_save(cr);
        cairo_set_source_surface(cr, mSurface, ctx.bounds.left, ctx.bounds.top);
        cairo_rectangle(cr, ctx.bounds.left, ctx.bounds.top, ctx.bounds.width(), ctx.bounds.height());
        cairo_fill(cr);
        cairo_restore(cr);
    }
};

ElementsInstrumentPanel::ElementsInstrumentPanel(cockpit::PanelType type,
                                                 std::weak_ptr<el::view> view,
                                                 std::weak_ptr<IDataController> controller,
                                                 InstrumentPanelWidgets widgets,
                                                 EnsurePrivate)
    : cockpit::InstrumentContentPanel(type, std::move(controller))
    , mView(std::move(view))
    , mWidgets(std::move(widgets))
{}

ElementsInstrumentPanel::~ElementsInstrumentPanel() = default;

std::shared_ptr<ElementsInstrumentPanel> ElementsInstrumentPanel::Create(cockpit::PanelType type,
                                                                         std::weak_ptr<el::view> view,
                                                                         std::weak_ptr<IDataController> controller,
                                                                         InstrumentPanelWidgets widgets)
{
    auto self = std::make_shared<ElementsInstrumentPanel>(type, std::move(view), std::move(controller), std::move(widgets), EnsurePrivate{});
    self->InitInstrumentSubscription(self);

    self->mChartElement = std::make_shared<ChartElement>(std::weak_ptr<cockpit::InstrumentContentPanel>(self));

    if (self->mWidgets.workArea) {
        self->mWidgets.workArea->clear();
        self->mWidgets.workArea->push_back(el::share(el::hold(self->mChartElement)));
        self->mWidgets.workArea->select(0);
    }

    return self;
}

void ElementsInstrumentPanel::SetDataReady(bool ready)
{
    std::weak_ptr ref = weak_from_this();
    PostToUi([ref, ready] {
        auto self = ref.lock();
        if (!self) return;
        auto& deck = self->mWidgets.overlayDeck;
        if (!deck || deck->size() < 2) return;
        deck->select(ready ? 1 : 0);
        if (auto v = self->mView.lock()) {
            v->layout();
            v->refresh();
        }
    });
}

void ElementsInstrumentPanel::PostToUi(std::function<void()> fn)
{
    if (auto v = mView.lock())
        v->post(std::move(fn));
}

void ElementsInstrumentPanel::OnInstrumentsReady(std::vector<std::string> symbols)
{
    if (!mWidgets.SetInstruments) return;

    std::weak_ptr ref = weak_from_this();
    mWidgets.SetInstruments(symbols, [ref](std::string sym) {
        if (auto self = ref.lock())
            self->SelectSymbol(std::move(sym));
    });

    if (auto v = mView.lock()) {
        v->layout();
        v->refresh();
    }
}

void ElementsInstrumentPanel::OnSymbolSelected(const std::string& symbol)
{
    if (mWidgets.SetTitle)
        mWidgets.SetTitle(symbol);

    if (auto v = mView.lock()) {
        v->layout();
        v->refresh();
    }
}

} // namespace scratcher::elements
