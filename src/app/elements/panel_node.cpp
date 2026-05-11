// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
//
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b21tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

#include "panel_node.hpp"

#include <utility>

namespace scratcher::elements {

namespace {
el::color divider_color = el::rgba(80, 80, 90, 255);
} // anonymous namespace

// --- PanelNode ---

PanelNode::PanelNode(std::shared_ptr<el::view> root_view)
    : mRootView(std::move(root_view)) , mDeck(std::make_shared<el::deck_composite>())
{
    // deck_composite with 0 children crashes on render — seed a placeholder.
    mDeck->push_back(el::share(el::element{}));
    mDeck->select(0);
}

el::element_ptr PanelNode::GetElement()
{
    return el::share(el::hold(mDeck));
}

void PanelNode::RefreshDeck(el::element_ptr content)
{
    mDeck->clear();
    mDeck->push_back(std::move(content));
    mDeck->select(0);

    mRootView->layout();
    mRootView->refresh();
}

// --- LeafPanelNode ---

void LeafPanelNode::Initialize(el::element_ptr content, panel_id pid)
{
    mPanelId = pid;
    RefreshDeck(std::move(content));
}

// --- InstrumentPanelNode ---

void InstrumentPanelNode::SetInstruments(const std::vector<std::string>& symbols, std::function<void(std::string)> onSelect)
{
    if (!mInstrumentButton) return;

    el::vtile_composite items;
    for (const auto& sym : symbols) {
        auto item = el::menu_item(sym);
        std::string captured = sym;
        item.on_click = [onSelect, captured]() {
            if (onSelect) onSelect(captured);
        };
        items.push_back(el::share(std::move(item)));
    }

    constexpr float instrument_menu_max_height = 400.0f;
    mInstrumentButton->menu(
        el::layer(
            el::vsize(instrument_menu_max_height, el::vscroller(el::vtile(std::move(items)))),
            el::panel{}
        )
    );
}

void InstrumentPanelNode::SetTitle(const std::string& text)
{
    if (mTitleLabel) mTitleLabel->set_text(text);
}

void InstrumentPanelNode::InstallChart(el::element_ptr chart_element, cockpit::panel_id pid)
{
    if (mWorkArea) {
        // Hold the old chart through the deck mutation; release it only after
        // layout/refresh have settled so the chart's destruction (which can recurse
        // into ThorVG's cascade-free) happens outside any deck/cycfi-internal walk.
        el::element_ptr old_panel = nullptr;
        if (mWorkArea->size() < 2) {
            mWorkArea->push_back(chart_element);
        } else {
            old_panel = *(mWorkArea->begin() + 1);
            mWorkArea->select(0);
            mWorkArea->erase(mWorkArea->begin() + 1);
            mWorkArea->push_back(chart_element);
        }

        mWorkArea->select(1);
        mPanelId = pid;
        mHasChart = true;

        mRootView->layout();
        mRootView->refresh();
        old_panel.reset();
    }
}

void InstrumentPanelNode::UninstallChart()
{
    if (mWorkArea) {
        if (mWorkArea->size() >= 2) {
            mWorkArea->pop_back();/*erase(mWorkArea->begin() + 1);*/
        }
        mWorkArea->select(0);
    }

    mPanelId = 0;
    mHasChart = false;

    mRootView->layout();
    mRootView->refresh();
}

// --- SplitPanelNode ---


void SplitPanelNode::ReplaceChild(std::shared_ptr<PanelNode> oldChild, std::shared_ptr<PanelNode> newChild)
{
    if (mFirst == oldChild)
        mFirst = std::move(newChild);
    else if (mSecond == oldChild)
        mSecond = std::move(newChild);

    BuildLayout();
}

void SplitPanelNode::BuildLayout()
{
    el::element_ptr layout;

    if (mDirection == SplitDirection::Vertical) {
        layout = el::share(
            el::htile(
                el::hstretch(1.0, el::hold(mFirst->GetElement())),
                el::vmin_size(1, el::hsize(4, el::box(divider_color))),
                el::hstretch(1.0, el::hold(mSecond->GetElement()))
            )
        );
    } else {
        layout = el::share(
            el::vtile(
                el::vstretch(1.0, el::hold(mFirst->GetElement())),
                el::hmin_size(1, el::vsize(4, el::box(divider_color))),
                el::vstretch(1.0, el::hold(mSecond->GetElement()))
            )
        );
    }

    mDeck->clear();
    mDeck->push_back(std::move(layout));
    mDeck->select(0);
}

} // namespace scratcher::elements
