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

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <elements.hpp>

#include "instrument_panel.hpp"
#include "ui_builder.hpp"

namespace scratcher::elements {

class PixelBufferElement;

class InstrumentPanelElement : public cockpit::InstrumentContentPanel, public std::enable_shared_from_this<InstrumentPanelElement>
{
    struct EnsurePrivate {};

public:
    InstrumentPanelElement(cockpit::PanelType type, std::chrono::seconds candle_period, uint32_t candle_width_pixels,
                            std::weak_ptr<cycfi::elements::view> view, InstrumentPanelWidgets widgets, EnsurePrivate);
    ~InstrumentPanelElement() override;

    static std::shared_ptr<InstrumentPanelElement> Create(cockpit::PanelType type, std::chrono::seconds candle_period, uint32_t candle_width_pixels,
                                                            std::weak_ptr<cycfi::elements::view> view, InstrumentPanelWidgets widgets);

    void SetDataReady(bool ready) override;

    void PostToUi(std::function<void()> fn) override;

protected:
    void OnSymbolChanged(const std::string& symbol) override;
    void OnInstrumentListChanged(const std::vector<std::string>& symbols) override;
    void OnInstrumentInfoChanged(const std::optional<bybit::InstrumentInfo>& info) override;

private:
    std::weak_ptr<cycfi::elements::view> mView;
    InstrumentPanelWidgets mWidgets;
    std::shared_ptr<PixelBufferElement> mPixelBuffer;
};

} // namespace scratcher::elements
