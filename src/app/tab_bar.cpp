// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "tab_bar.hpp"

#include <stdexcept>

namespace scratcher::elements {

namespace el = cycfi::elements;

namespace {

el::color tab_normal_color = el::rgba(45, 45, 48, 255);
el::color tab_active_color = el::rgba(60, 60, 65, 255);
el::color text_color = el::rgba(200, 200, 200, 255);

} // anonymous namespace

TabBar::TabBar(std::weak_ptr<el::view> view, EnsurePrivate)
    : mView(std::move(view))
    , mButtonDeck(std::make_shared<el::deck_composite>())
    , mPageDeck(std::make_shared<el::deck_composite>())
{
    // Both decks need at least one child to avoid crash on render
    mButtonDeck->push_back(el::share(el::element{}));
    mButtonDeck->select(0);
    mPageDeck->push_back(el::share(el::element{}));
    mPageDeck->select(0);
}

std::shared_ptr<TabBar> TabBar::Create(std::weak_ptr<el::view> view)
{
    return std::make_shared<TabBar>(std::move(view), EnsurePrivate{});
}

std::shared_ptr<el::view> TabBar::View() const
{
    auto v = mView.lock();
    if (!v) throw std::logic_error("TabBar: view expired");
    return v;
}

el::element_ptr TabBar::Build()
{
    RebuildTabButtons();
    return el::share(
        el::vtile(
            el::hold(mButtonDeck),
            el::vstretch(1.0, el::hold(mPageDeck))
        )
    );
}

void TabBar::SetPlusButton(el::element_ptr btn)
{
    mPlusButton = std::move(btn);
}

tab_id TabBar::AddTab(const std::string& label, el::element_ptr page)
{
    tab_id id = mNextTabId++;
    mTabs.push_back({id, label});
    mPageDeck->push_back(page);

    mActiveIndex = mTabs.size() - 1;
    mPageDeck->select(mActiveIndex + 1); // +1: index 0 is placeholder
    RebuildTabButtons();

    auto v = View();
    v->layout();
    v->refresh();
    return id;
}

void TabBar::RemoveTab(tab_id id)
{
    size_t idx = IndexOfTab(id);
    if (idx >= mTabs.size()) return;

    mTabs.erase(mTabs.begin() + idx);
    mPageDeck->erase(mPageDeck->begin() + idx + 1); // +1: index 0 is placeholder

    if (mActiveIndex >= mTabs.size() && mActiveIndex > 0)
        mActiveIndex = mTabs.size() - 1;

    if (!mTabs.empty())
        mPageDeck->select(mActiveIndex + 1);
    else
        mPageDeck->select(0); // show placeholder

    RebuildTabButtons();
    auto v = View();
    v->layout();
    v->refresh();
}

void TabBar::SelectTab(tab_id id)
{
    size_t idx = IndexOfTab(id);
    if (idx < mTabs.size()) {
        SwitchTab(idx);
    }
}

tab_id TabBar::ActiveTabId() const
{
    if (mActiveIndex < mTabs.size())
        return mTabs[mActiveIndex].id;
    return 0;
}

size_t TabBar::TabCount() const
{
    return mTabs.size();
}

void TabBar::RebuildTabButtons()
{
    el::htile_composite buttons;

    for (size_t i = 0; i < mTabs.size(); ++i) {
        bool is_active = (i == mActiveIndex);
        auto body_color = is_active ? tab_active_color : tab_normal_color;
        size_t tab_index = i;
        tab_id tid = mTabs[i].id;

        auto tab_btn = el::share(
            el::momentary_button(
                el::button_styler{mTabs[i].label}
                    .body_color(body_color)
                    .text_color(text_color)
                    .corner_radius(1)
            )
        );

        tab_btn->on_click = [w = weak_from_this(), tab_index](bool) {
            if (auto t = w.lock()) t->SwitchTab(tab_index);
        };

        if (mTabs.size() > 1) {
            auto close_btn = el::share(
                el::momentary_button(
                    el::margin({2, 2, 4, 2},
                        //el::label("✕").font_size(12)
                        el::icon(el::icons::cancel, 1.0f)
                    )
                )
            );

            close_btn->on_click = [w = weak_from_this(), tid](bool) {
                if (auto t = w.lock(); t && t->onTabClosed) t->onTabClosed(tid);
            };

            buttons.push_back(el::share(
                el::layer(
                    el::halign(1.0, el::valign(0.5, el::hold(close_btn))),
                    el::hold(tab_btn)
                )
            ));
        } else {
            buttons.push_back(tab_btn);
        }
    }

    if (mPlusButton) {
        buttons.push_back(mPlusButton);
    }

    auto new_row = el::share(
        el::layer(
            el::align_left(
                el::htile(std::move(buttons))
            ),
            el::box(tab_normal_color)
        )
    );

    // Mutate mButtonDeck in place so the tree reference stays valid
    mButtonDeck->clear();
    mButtonDeck->push_back(new_row);
    mButtonDeck->select(0);
}

void TabBar::SwitchTab(size_t index)
{
    if (index >= mTabs.size()) return;
    mActiveIndex = index;
    mPageDeck->select(mActiveIndex + 1); // +1: index 0 is placeholder
    RebuildTabButtons();
    auto v = View();
    v->layout();
    v->refresh();

    if (onTabSelected)
        onTabSelected(mTabs[mActiveIndex].id);
}

size_t TabBar::IndexOfTab(tab_id id) const
{
    for (size_t i = 0; i < mTabs.size(); ++i) {
        if (mTabs[i].id == id) return i;
    }
    return mTabs.size();
}

} // namespace scratcher::elements
