// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <elements.hpp>

#include "content_panel.hpp"
#include "split_direction.hpp"

namespace scratcher::elements {

namespace el = cycfi::elements;

// Common base for the panel-tree composite. While a node is in the tree it must
// hold a strong reference to the root cycfi view: the view drives layout/refresh
// invoked by the node, so it cannot legitimately outlive any of its nodes.
class PanelNode
{
public:
    virtual ~PanelNode() = default;

    virtual bool IsLeaf() const = 0;

    const std::shared_ptr<el::view>& RootView() const
    { return mRootView; }

    el::element_ptr GetElement();

protected:
    explicit PanelNode(std::shared_ptr<el::view> root_view);

    void RefreshDeck(el::element_ptr content);

    std::shared_ptr<el::view> mRootView;
    std::shared_ptr<el::deck_composite> mDeck;
};

class LeafPanelNode : public PanelNode
{
public:
    using PanelType = cockpit::PanelType;
    using panel_id = cockpit::panel_id;

    LeafPanelNode(std::shared_ptr<el::view> root_view, PanelType type)
    : PanelNode(std::move(root_view)) , mType(type)
    {}
    ~LeafPanelNode() override = default;

    void Initialize(el::element_ptr content, panel_id pid);

    bool IsLeaf() const override { return true; }

    PanelType Type() const { return mType; }
    panel_id PanelId() const { return mPanelId; }

protected:
    PanelType mType;
    panel_id mPanelId = 0;
};

// Leaf for instrument-bearing panels. The chrome (header with title + dropdown,
// work area, footer) is created once; the work-area is a 2-child deck whose
// child 0 is a waiting indicator and child 1 is the chart pixel buffer.
// Switching `select(0|1)` flips what's shown without mutating the cycfi tree
// shape — that avoids the layout glitch we hit when the leaf was wholesale-
// replaced on data arrival (a stripe of the previous bounds remained on the
// next paint).
//
// Symbol re-selection is implemented as InstallChart(new_chart) on the SAME
// leaf — the chrome stays mounted, only the chart child swaps in-place.
//
// The node is deliberately ignorant of bybit::InstrumentInfo: it only knows
// about symbol strings. Symbol→InstrumentInfo resolution and the cockpit
// registration that consumes the InstrumentInfo are MainWindow's job.
class InstrumentPanelNode : public LeafPanelNode
{
public:
    InstrumentPanelNode(std::shared_ptr<el::view> root_view, PanelType type, std::shared_ptr<el::text_writer> title_label, std::shared_ptr<el::basic_button_menu> instrument_button, std::shared_ptr<el::deck_composite> work_area)
    : LeafPanelNode(std::move(root_view), type) , mTitleLabel(std::move(title_label)) , mInstrumentButton(std::move(instrument_button)) , mWorkArea(std::move(work_area))
    {}
    ~InstrumentPanelNode() override = default;

    bool HasChart() const { return mHasChart; }

    // Push the latest instrument symbols to the chrome dropdown. `onSelect`
    // fires when the user picks a symbol; MainWindow turns that into an
    // InstallChart on this leaf.
    void SetInstruments(const std::vector<std::string>& symbols, std::function<void(std::string)> onSelect);

    // Update the title text shown next to the dropdown.
    void SetTitle(const std::string& text);

    // Mount `chart_element` as the work-area's chart child and switch the deck to it.
    void InstallChart(el::element_ptr chart_element, cockpit::panel_id pid);

    // Drop the current chart binding. Restores the waiting indicator and runs
    // the previously-stored cleanup. After this, HasChart() == false.
    void UninstallChart();

private:
    std::shared_ptr<el::text_writer> mTitleLabel;
    std::shared_ptr<el::basic_button_menu> mInstrumentButton;
    std::shared_ptr<el::deck_composite> mWorkArea;
    bool mHasChart = false;
};

class SplitPanelNode : public PanelNode
{
public:
    SplitPanelNode(std::shared_ptr<el::view> root_view, SplitDirection direction, std::shared_ptr<PanelNode> first, std::shared_ptr<PanelNode> second)
        : PanelNode(std::move(root_view)) , mDirection(direction) , mFirst(std::move(first)) , mSecond(std::move(second))
    {
        BuildLayout();
    }
    ~SplitPanelNode() override = default;

    bool IsLeaf() const override { return false; }

    SplitDirection Direction() const { return mDirection; }
    std::shared_ptr<PanelNode> First() const { return mFirst; }
    std::shared_ptr<PanelNode> Second() const { return mSecond; }

    void ReplaceChild(std::shared_ptr<PanelNode> oldChild, std::shared_ptr<PanelNode> newChild);

private:
    void BuildLayout();

    SplitDirection mDirection;
    std::shared_ptr<PanelNode> mFirst;
    std::shared_ptr<PanelNode> mSecond;
};

} // namespace scratcher::elements
