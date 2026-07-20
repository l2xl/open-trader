// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>

#include <elements.hpp>

namespace scratcher::elements {

using tab_id = size_t;

class TabBar : public std::enable_shared_from_this<TabBar>
{
    struct EnsurePrivate {};
public:
    TabBar(std::weak_ptr<cycfi::elements::view> view, EnsurePrivate);

    static std::shared_ptr<TabBar> Create(std::weak_ptr<cycfi::elements::view> view);

    cycfi::elements::element_ptr Build();

    void SetPlusButton(cycfi::elements::element_ptr btn);

    tab_id AddTab(const std::string& label, cycfi::elements::element_ptr page);
    void RemoveTab(tab_id id);
    void SelectTab(tab_id id);

    tab_id ActiveTabId() const;
    size_t TabCount() const;

    std::function<void(tab_id)> onTabSelected;
    std::function<void(tab_id)> onTabClosed;

private:
    struct TabInfo {
        tab_id id;
        std::string label;
    };

    std::shared_ptr<cycfi::elements::view> View() const;

    void RebuildTabButtons();
    void SwitchTab(size_t index);
    size_t IndexOfTab(tab_id id) const;

    std::weak_ptr<cycfi::elements::view> mView;
    tab_id mNextTabId = 0;
    std::vector<TabInfo> mTabs;
    size_t mActiveIndex = 0;

    cycfi::elements::element_ptr mPlusButton;

    // Single-child deck holding the current button row, mutated in place
    std::shared_ptr<cycfi::elements::deck_composite> mButtonDeck;
    // Page deck: index 0 = empty placeholder, tab pages at [1..N]
    std::shared_ptr<cycfi::elements::deck_composite> mPageDeck;
};

} // namespace scratcher::elements
