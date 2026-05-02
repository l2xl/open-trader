// Scratcher project
// Copyright (c) 2025 l2xl (l2xl/at/proton.me)
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
#include <cstdint>
#include <memory>
#include <functional>
#include <unordered_map>

#include <elements.hpp>
#include "content_panel.hpp"
#include "split_direction.hpp"
#include "tab_bar.hpp"
#include "panel_node.hpp"
#include "ui_builder.hpp"

namespace scratcher::elements {

struct InstrumentPanelDefaults
{
    std::chrono::seconds candle_period{60};
    uint32_t candle_width_pixels = 8;
};

class MainWindow
{
public:
    using on_panel_created_t = std::function<cockpit::panel_id(std::shared_ptr<cockpit::ContentPanel>)>;
    using on_panel_closed_t = std::function<void(cockpit::panel_id)>;
    using default_panel_type_accessor_t = std::function<cockpit::PanelType()>;
    using instrument_panel_defaults_accessor_t = std::function<InstrumentPanelDefaults()>;

    explicit MainWindow(UiBuilder& builder);
    ~MainWindow();

    int Run();

    void SetOnPanelCreated(on_panel_created_t handler);
    void SetOnPanelClosed(on_panel_closed_t handler);
    void SetDefaultPanelTypeAccessor(default_panel_type_accessor_t accessor);
    void SetInstrumentPanelDefaultsAccessor(instrument_panel_defaults_accessor_t accessor);

private:
    void SetupContent();
    std::shared_ptr<LeafPanelNode> OnNewTab(cockpit::PanelType type);

    std::shared_ptr<LeafPanelNode> MakeLeaf(cockpit::PanelType type);

    void HandleChangeType(std::shared_ptr<LeafPanelNode> node, cockpit::PanelType newType);
    void HandleSplit(std::shared_ptr<LeafPanelNode> node, cockpit::PanelType newType, SplitDirection dir);
    void HandleClose(std::shared_ptr<LeafPanelNode> node);

    void ReplaceNode(std::shared_ptr<PanelNode> oldNode, std::shared_ptr<PanelNode> newNode);

    cycfi::elements::app mApp;
    cycfi::elements::window mWindow;
    std::shared_ptr<cycfi::elements::view> mView;
    UiBuilder& mBuilder;

    on_panel_created_t mOnPanelCreated;
    on_panel_closed_t mOnPanelClosed;
    default_panel_type_accessor_t mDefaultPanelTypeAccessor;
    instrument_panel_defaults_accessor_t mInstrumentPanelDefaultsAccessor;

    std::unique_ptr<TabBar> mTabBar;

    struct TabRoot {
        std::shared_ptr<PanelNode> node;
        std::shared_ptr<cycfi::elements::deck_composite> slot;
    };
    std::unordered_map<tab_id, TabRoot> mTabRoots;

    bool mMenuVisible = false;
};

} // namespace scratcher::elements
