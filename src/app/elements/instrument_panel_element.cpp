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

#include "instrument_panel_element.hpp"

#include <utility>

#include <elements.hpp>

#include "pixel_buffer_element.hpp"

namespace scratcher::elements {

namespace el = cycfi::elements;

InstrumentPanelElement::InstrumentPanelElement(cockpit::PanelType type, std::chrono::seconds candle_period, uint32_t candle_width_pixels,
                                                  std::weak_ptr<el::view> view, InstrumentPanelWidgets widgets, EnsurePrivate)
    : InstrumentContentPanel(type, candle_period, candle_width_pixels)
    , mView(std::move(view))
    , mWidgets(std::move(widgets))
{}

InstrumentPanelElement::~InstrumentPanelElement() = default;

std::shared_ptr<InstrumentPanelElement> InstrumentPanelElement::Create(cockpit::PanelType type, std::chrono::seconds candle_period, uint32_t candle_width_pixels,
                                                                          std::weak_ptr<el::view> view, InstrumentPanelWidgets widgets)
{
    auto self = std::make_shared<InstrumentPanelElement>(type, candle_period, candle_width_pixels, std::move(view), std::move(widgets), EnsurePrivate{});

    self->mPixelBuffer = std::make_shared<PixelBufferElement>();

    std::weak_ptr<cockpit::InstrumentContentPanel> panel_ref = self;
    self->mPixelBuffer->SetOnResize([panel_ref](uint32_t* buffer, uint32_t stride, uint32_t w, uint32_t h) {
        if (auto p = panel_ref.lock()) {
            p->SetTarget(buffer, stride, w, h);
            p->OnSize(static_cast<int>(w), static_cast<int>(h));
        }
    });
    self->mPixelBuffer->SetOnRender([panel_ref] {
        if (auto p = panel_ref.lock()) p->Render();
    });

    if (self->mWidgets.workArea) {
        self->mWidgets.workArea->push_back(el::share(el::hold(self->mPixelBuffer)));
        self->mWidgets.workArea->select(0);
    }

    return self;
}

void InstrumentPanelElement::SetDataReady(bool /*ready*/)
{
    auto weak = weak_from_this();
    PostToUi([weak] {
        auto base = weak.lock();
        if (!base) return;
        auto self = std::static_pointer_cast<InstrumentPanelElement>(base);
        if (auto v = self->mView.lock()) {
            v->refresh();
        }
    });
}

void InstrumentPanelElement::PostToUi(std::function<void()> fn)
{
    if (auto v = mView.lock())
        v->post(std::move(fn));
}

void InstrumentPanelElement::OnSymbolChanged(const std::string& symbol)
{
    auto weak = weak_from_this();
    PostToUi([weak, symbol] {
        auto base = weak.lock();
        if (!base) return;
        auto self = std::static_pointer_cast<InstrumentPanelElement>(base);
        if (self->mWidgets.SetTitle)
            self->mWidgets.SetTitle(symbol);
        if (auto v = self->mView.lock()) {
            v->layout();
            v->refresh();
        }
    });
}

void InstrumentPanelElement::OnInstrumentListChanged(const std::vector<std::string>& symbols)
{
    auto weak = weak_from_this();
    PostToUi([weak, symbols] {
        auto base = weak.lock();
        if (!base) return;
        auto self = std::static_pointer_cast<InstrumentPanelElement>(base);
        if (!self->mWidgets.SetInstruments) return;

        std::weak_ptr<InstrumentPanelElement> ref = self;
        self->mWidgets.SetInstruments(symbols, [ref](std::string sym) {
            if (auto s = ref.lock())
                s->EmitUserSymbolSelection(std::move(sym));
        });

        if (auto v = self->mView.lock()) {
            v->layout();
            v->refresh();
        }
    });
}

void InstrumentPanelElement::OnInstrumentInfoChanged(const std::optional<bybit::InstrumentInfo>& /*info*/)
{
    auto weak = weak_from_this();
    PostToUi([weak] {
        auto base = weak.lock();
        if (!base) return;
        auto self = std::static_pointer_cast<InstrumentPanelElement>(base);
        if (auto v = self->mView.lock()) {
            v->layout();
            v->refresh();
        }
    });
}

} // namespace scratcher::elements
