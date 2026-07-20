// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <elements.hpp>

#include "content_panel.hpp"
#include "panel_node.hpp"
#include "split_direction.hpp"
#include "tab_bar.hpp"
#include "ui_builder.hpp"

namespace scratcher::cockpit { class TradeCockpit; }

namespace scratcher::elements {

class MainWindow
{
public:
    MainWindow(UiBuilder& builder, std::shared_ptr<cockpit::TradeCockpit> cockpit);
    ~MainWindow();

    int Run();

private:
    void SetupContent();
    std::shared_ptr<LeafPanelNode> OnNewTab(cockpit::PanelType type);

    // Creates a leaf for `type`. Instrument-bearing leaves are built with their full
    // chrome (header + dropdown + work-area + footer); the work-area shows a waiting
    // indicator until OnInstrumentsArrived installs a chart in-place.
    std::shared_ptr<LeafPanelNode> MakeLeaf(cockpit::PanelType type);

    // Build a non-instrument leaf (Empty/etc.) wrapping a UiBuilder-built chrome.
    std::shared_ptr<LeafPanelNode> MakeGenericLeaf(cockpit::PanelType type);

    // Build an instrument leaf chrome for `type` with the work-area parked on the
    // waiting indicator. The chart is installed later via InstallChart once the
    // instrument list arrives (or symbol selection changes).
    std::shared_ptr<InstrumentPanelNode> MakeInstrumentLeaf(cockpit::PanelType type);

    // Build a chart panel for `symbol` and install it inside the existing leaf's
    // work-area deck (deck-switch from waiting indicator to chart). The cockpit
    // owns the symbol→InstrumentInfo lookup performed at registration time.
    void InstallChart(std::shared_ptr<InstrumentPanelNode> leaf, std::string symbol);

    // Returns the configured default symbol if it is in the current symbol list,
    // otherwise the first known symbol, otherwise empty.
    std::string ResolveDefaultSymbol() const;

    void HandleChangeType(std::shared_ptr<LeafPanelNode> node, cockpit::PanelType newType);
    void HandleSplit(std::shared_ptr<LeafPanelNode> node, cockpit::PanelType newType, SplitDirection dir);
    void HandleClose(std::shared_ptr<LeafPanelNode> node);

    void ReplaceNode(std::shared_ptr<PanelNode> oldNode, std::shared_ptr<PanelNode> newNode);

    void OnSymbolsArrived(std::vector<std::string> symbols);
    void ForEachInstrumentLeaf(const std::function<void(std::shared_ptr<InstrumentPanelNode>)>& fn);
    void PushSymbolListTo(std::shared_ptr<InstrumentPanelNode> leaf);

    cycfi::elements::app mApp;
    cycfi::elements::window mWindow;
    std::shared_ptr<cycfi::elements::view> mView;
    UiBuilder& mBuilder;
    std::shared_ptr<cockpit::TradeCockpit> mCockpit;

    std::shared_ptr<TabBar> mTabBar;

    struct TabRoot {
        std::shared_ptr<PanelNode> node;
        std::shared_ptr<cycfi::elements::deck_composite> slot;
    };
    std::unordered_map<tab_id, TabRoot> mTabRoots;

    std::vector<std::string> mSymbols;
    bool mInstrumentsReady = false;
    uint64_t mInstrumentSubId = 0;

    bool mMenuVisible = false;
};

} // namespace scratcher::elements
