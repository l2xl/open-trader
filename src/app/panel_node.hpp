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

// Leaf whose chrome (header, work area, footer) is created once; the work area is a
// 2-child deck whose child 0 is a waiting indicator and child 1 the panel content.
// Switching `select(0|1)` flips what's shown without mutating the cycfi tree shape —
// that avoids the layout glitch we hit when the leaf was wholesale-replaced on data
// arrival (a stripe of the previous bounds remained on the next paint). Content
// re-installation swaps only the content child in-place, the chrome stays mounted.
class ScenePanelNode : public LeafPanelNode
{
public:
    ScenePanelNode(std::shared_ptr<el::view> root_view, PanelType type, std::shared_ptr<el::deck_composite> work_area)
    : LeafPanelNode(std::move(root_view), type) , mWorkArea(std::move(work_area))
    {}
    ~ScenePanelNode() override = default;

    bool HasContent() const { return mHasContent; }

    // Mount `content` as the work-area's content child and switch the deck to it.
    void InstallContent(el::element_ptr content, cockpit::panel_id pid);

    // Drop the current content. Restores the waiting indicator; HasContent() == false afterwards.
    void UninstallContent();

private:
    std::shared_ptr<el::deck_composite> mWorkArea;
    bool mHasContent = false;
};

// Scene leaf for instrument-bearing panels: adds the symbol dropdown and title to
// the chrome. Symbol re-selection is InstallContent(new_chart) on the SAME leaf.
//
// The node is deliberately ignorant of bybit::InstrumentInfo: it only knows
// about symbol strings. Symbol→InstrumentInfo resolution and the cockpit
// registration that consumes the InstrumentInfo are MainWindow's job.
class InstrumentPanelNode : public ScenePanelNode
{
public:
    InstrumentPanelNode(std::shared_ptr<el::view> root_view, PanelType type, std::shared_ptr<el::text_writer> title_label, std::shared_ptr<el::basic_button_menu> instrument_button, std::shared_ptr<el::deck_composite> work_area)
    : ScenePanelNode(std::move(root_view), type, std::move(work_area)) , mTitleLabel(std::move(title_label)) , mInstrumentButton(std::move(instrument_button))
    {}
    ~InstrumentPanelNode() override = default;

    // Push the latest instrument symbols to the chrome dropdown. `onSelect`
    // fires when the user picks a symbol; MainWindow turns that into an
    // InstallContent on this leaf.
    void SetInstruments(const std::vector<std::string>& symbols, std::function<void(std::string)> onSelect);

    // Update the title text shown next to the dropdown.
    void SetTitle(const std::string& text);

private:
    std::shared_ptr<el::text_writer> mTitleLabel;
    std::shared_ptr<el::basic_button_menu> mInstrumentButton;
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
