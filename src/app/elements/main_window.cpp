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

#include "main_window.hpp"

#include <utility>

#include "instrument_panel_element.hpp"
#include "trade_cockpit.hpp"

namespace scratcher::elements {

namespace el = cycfi::elements;
using cockpit::PanelType;
using cockpit::PanelTypeName;
using cockpit::panel_id;

namespace {

bool ContainsNode(const std::shared_ptr<PanelNode>& root, const std::shared_ptr<PanelNode>& target)
{
    if (root == target) return true;
    if (root->IsLeaf()) return false;
    auto split = std::static_pointer_cast<SplitPanelNode>(root);
    return ContainsNode(split->First(), target) || ContainsNode(split->Second(), target);
}

} // anonymous namespace

MainWindow::MainWindow(UiBuilder& builder, std::shared_ptr<cockpit::TradeCockpit> cockpit)
    : mApp("Exchange Scratchpad")
    , mWindow(mApp.name())
    , mBuilder(builder)
    , mCockpit(std::move(cockpit))
{
    mWindow.on_close = [this]() { mApp.stop(); };
    mView = std::make_shared<el::view>(mWindow);

    SetupContent();
}

MainWindow::~MainWindow()
{
    if (mInstrumentSubId && mCockpit) mCockpit->UnsubscribeInstruments(mInstrumentSubId);
    mTabRoots.clear();
}

int MainWindow::Run()
{
    // Subscribe BEFORE creating the initial leaf — if instruments are already loaded
    // (raced past startup), the synchronous current-snapshot delivery fills mInstruments
    // before MakeLeaf consults it.
    std::weak_ptr<cycfi::elements::view> wview = mView;
    mInstrumentSubId = mCockpit->SubscribeInstruments([this, wview](const std::vector<std::string>& list) {
            // Marshal off the data thread. View::post is thread-safe.
            if (auto v = wview.lock()) {
                auto copy = list;
                v->post([this, copy = std::move(copy)]() mutable {
                    OnSymbolsArrived(std::move(copy));
                });
            }
        });

    OnNewTab(mCockpit->GetDefaultContentPanelType());

    mApp.run();
    return 0;
}

void MainWindow::SetupContent()
{
    mTabBar = TabBar::Create(mView);

    mTabBar->SetPlusButton(mBuilder.MakePanelTypeSelector(
        cycfi::elements::icons::plus, [this](PanelType type) { OnNewTab(type); }
    ));

    mTabBar->onTabClosed = [this](tab_id tid) {
        mTabRoots.erase(tid);
        mTabBar->RemoveTab(tid);
    };

    auto tab_bar_element = mTabBar->Build();

    auto menu_items = mBuilder.MakeMenuItems([this]() { mApp.stop(); });
    auto app_bar = mBuilder.MakeAppBar(
        el::share(el::hstretch(1.0, el::element{})),
        menu_items,
        [this](bool state) {
            mMenuVisible = state;
            mView->refresh();
        });

    mView->content(
        el::vtile(
            el::hold(app_bar),
            el::vstretch(1.0, el::hold(tab_bar_element))
        )
    );
}

std::string MainWindow::ResolveDefaultSymbol() const
{
    if (mSymbols.empty()) return {};
    auto sym = mCockpit->GetDefaultSymbol();
    if (!sym.empty()) {
        for (const auto& s : mSymbols) if (s == sym) return s;
    }
    return mSymbols.front();
}

void MainWindow::PushSymbolListTo(std::shared_ptr<InstrumentPanelNode> leaf)
{
    auto onSelect = [this, w = std::weak_ptr(leaf)](std::string symbol) {
        if (auto node = w.lock()) InstallChart(std::move(node), std::move(symbol));
    };
    leaf->SetInstruments(mSymbols, std::move(onSelect));
}

std::shared_ptr<LeafPanelNode> MainWindow::OnNewTab(PanelType type)
{
    auto leaf = MakeLeaf(type);

    auto slot = std::make_shared<el::deck_composite>();
    slot->push_back(leaf->GetElement());
    slot->select(0);

    tab_id tid = mTabBar->AddTab(PanelTypeName(type), el::share(el::hold(slot)));
    mTabRoots[tid] = TabRoot{leaf, slot};

    // Instrument leaves born before the instruments list arrives sit on a waiting
    // indicator. Once data is ready (which may already be the case), install the chart
    // immediately so the very first paint shows the chart instead of the spinner.
    if (auto ileaf = std::dynamic_pointer_cast<InstrumentPanelNode>(leaf)) {
        auto sym = ResolveDefaultSymbol();
        if (!sym.empty()) InstallChart(ileaf, std::move(sym));
    }
    return leaf;
}

std::shared_ptr<LeafPanelNode> MainWindow::MakeLeaf(PanelType type)
{
    switch(type) {
    case PanelType::MarketGraph:
    case PanelType::OrderBook:
        return MakeInstrumentLeaf(type);
    default:
        return MakeGenericLeaf(type);
    }
}

std::shared_ptr<LeafPanelNode> MainWindow::MakeGenericLeaf(PanelType type)
{
    auto leaf = std::make_shared<LeafPanelNode>(mView, type);

    auto onChangeType = [this, w = std::weak_ptr(leaf)](PanelType newType) {
        if (auto n = w.lock()) HandleChangeType(n, newType);
    };
    auto onSplit = [this, w = std::weak_ptr(leaf)](PanelType newType, SplitDirection dir) {
        if (auto n = w.lock()) HandleSplit(n, newType, dir);
    };
    auto onClose = [this, w = std::weak_ptr(leaf)]() {
        if (auto n = w.lock()) HandleClose(n);
    };

    auto element = mBuilder.MakePanel(type, std::move(onChangeType), std::move(onClose), std::move(onSplit));
    leaf->Initialize(std::move(element), 0);
    return leaf;
}

std::shared_ptr<InstrumentPanelNode> MainWindow::MakeInstrumentLeaf(PanelType type)
{
    // Chrome callbacks need to resolve back to the leaf, but UiBuilder constructs
    // the leaf inside MakeInstrumentPanel — so callbacks bind via a shared holder
    // that we fill in once the node is back.
    auto leaf_holder = std::make_shared<std::shared_ptr<InstrumentPanelNode>>();

    auto onSplit = [this, leaf_holder](PanelType newType, SplitDirection dir) {
        if (auto n = *leaf_holder) HandleSplit(n, newType, dir);
    };
    auto onClose = [this, leaf_holder]() {
        if (auto n = *leaf_holder) HandleClose(n);
    };

    auto leaf = mBuilder.MakeInstrumentPanel(mView, type, std::move(onClose), std::move(onSplit));
    *leaf_holder = leaf;

    PushSymbolListTo(leaf);
    return leaf;
}

void MainWindow::InstallChart(std::shared_ptr<InstrumentPanelNode> leaf, std::string symbol)
{
    auto panel = InstrumentPanelElement::Create(leaf->Type(), mCockpit->GetDefaultCandlePeriod(), mCockpit->GetDefaultCandleWidth(), mView);
    panel_id pid = mCockpit->RegisterInstrumentPanel(symbol, panel);
    leaf->SetTitle(symbol);
    leaf->InstallChart(el::share(el::hold(panel)), pid);
}

void MainWindow::HandleChangeType(std::shared_ptr<LeafPanelNode> node, PanelType newType)
{
    auto newLeaf = MakeLeaf(newType);
    ReplaceNode(node, newLeaf);
}

void MainWindow::HandleSplit(std::shared_ptr<LeafPanelNode> node, PanelType newType, SplitDirection dir)
{
    auto newLeaf = MakeLeaf(newType);
    auto split = std::make_shared<SplitPanelNode>(mView, dir, node, newLeaf);
    ReplaceNode(node, split);
}

void MainWindow::HandleClose(std::shared_ptr<LeafPanelNode> node)
{
    for (auto& [tid, root] : mTabRoots) {
        if (root.node == node) {
            if (mTabBar->TabCount() > 1) {
                mTabRoots.erase(tid);
                mTabBar->RemoveTab(tid);
            } else {
                auto emptyLeaf = MakeGenericLeaf(PanelType::Empty);
                ReplaceNode(node, emptyLeaf);
            }
            return;
        }
    }

    for (auto& [tid, root] : mTabRoots) {
        if (!root.node->IsLeaf() && ContainsNode(root.node, node)) {
            std::function<std::shared_ptr<SplitPanelNode>(std::shared_ptr<PanelNode>)> findParent;
            findParent = [&](std::shared_ptr<PanelNode> current) -> std::shared_ptr<SplitPanelNode> {
                if (current->IsLeaf()) return nullptr;
                auto split = std::static_pointer_cast<SplitPanelNode>(current);
                if (split->First() == node || split->Second() == node) return split;
                if (auto found = findParent(split->First())) return found;
                return findParent(split->Second());
            };
            if (auto parent = findParent(root.node)) {
                auto sibling = (parent->First() == node) ? parent->Second() : parent->First();
                ReplaceNode(parent, sibling);
                return;
            }
        }
    }
}

void MainWindow::ReplaceNode(std::shared_ptr<PanelNode> oldNode, std::shared_ptr<PanelNode> newNode)
{
    for (auto& [tid, root] : mTabRoots) {
        if (root.node == oldNode) {
            root.node = newNode;
            root.slot->clear();
            root.slot->push_back(newNode->GetElement());
            root.slot->select(0);
            auto v = mView;
            v->layout();
            v->refresh();
            return;
        }
    }

    for (auto& [tid, root] : mTabRoots) {
        if (!root.node->IsLeaf() && ContainsNode(root.node, oldNode)) {
            std::function<std::shared_ptr<SplitPanelNode>(std::shared_ptr<PanelNode>)> findParent;
            findParent = [&](std::shared_ptr<PanelNode> current) -> std::shared_ptr<SplitPanelNode> {
                if (current->IsLeaf()) return nullptr;
                auto split = std::static_pointer_cast<SplitPanelNode>(current);
                if (split->First() == oldNode || split->Second() == oldNode)
                    return split;
                if (auto found = findParent(split->First())) return found;
                return findParent(split->Second());
            };

            if (auto parent = findParent(root.node)) {
                parent->ReplaceChild(oldNode, newNode);
                auto v = mView;
                v->layout();
                v->refresh();
                return;
            }
        }
    }
}

void MainWindow::OnSymbolsArrived(std::vector<std::string> symbols)
{
    mSymbols = std::move(symbols);
    mInstrumentsReady = true;

    // Push the latest list to every instrument leaf's dropdown, and install a chart
    // for any leaf still showing the waiting indicator. Both happen on the same leaf —
    // no leaf replacement, so cycfi's layout cache stays valid (this used to cause a
    // gray strip along the left edge until the next manual resize forced re-layout).
    auto defSym = ResolveDefaultSymbol();

    ForEachInstrumentLeaf([this, &defSym](std::shared_ptr<InstrumentPanelNode> leaf) {
        PushSymbolListTo(leaf);
        if (!leaf->HasChart() && !defSym.empty())
            InstallChart(leaf, defSym);
    });
}

void MainWindow::ForEachInstrumentLeaf(const std::function<void(std::shared_ptr<InstrumentPanelNode>)>& fn)
{
    std::function<void(std::shared_ptr<PanelNode>)> visit;
    visit = [&](std::shared_ptr<PanelNode> n) {
        if (!n) return;
        if (n->IsLeaf()) {
            if (auto ileaf = std::dynamic_pointer_cast<InstrumentPanelNode>(n))
                fn(ileaf);
            return;
        }
        auto split = std::static_pointer_cast<SplitPanelNode>(n);
        visit(split->First());
        visit(split->Second());
    };
    for (auto& [tid, root] : mTabRoots) visit(root.node);
}

} // namespace scratcher::elements
