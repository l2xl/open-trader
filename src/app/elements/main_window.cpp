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

#include "main_window.hpp"
#include "instrument_panel_element.hpp"

namespace scratcher::elements {

namespace el = cycfi::elements;
using cockpit::PanelType;
using cockpit::PanelTypeName;
using cockpit::panel_id;

namespace {

bool ContainsNode(PanelNode* root, PanelNode* target)
{
    if (root == target) return true;
    if (root->IsLeaf()) return false;
    auto* split = static_cast<SplitPanelNode*>(root);
    return ContainsNode(split->First().get(), target)
        || ContainsNode(split->Second().get(), target);
}

class ElementsContentPanel : public cockpit::ContentPanel, public std::enable_shared_from_this<ElementsContentPanel>
{
public:
    ElementsContentPanel(cockpit::PanelType type, std::weak_ptr<cycfi::elements::view> view, std::shared_ptr<cycfi::elements::deck_composite> overlayDeck)
    : ContentPanel(type) , mView(std::move(view)) , mOverlayDeck(std::move(overlayDeck))
    {}

    void SetDataReady(bool ready) override
    {
        auto ref = weak_from_this();
        auto v = mView.lock();
        if (!v) return;

        v->post([=]() {
            if (auto self = ref.lock()) {
                if (auto v = self->mView.lock()) {
                    if (self->mOverlayDeck && self->mOverlayDeck->size() >= 2) {
                        self->mOverlayDeck->select(ready ? 1 : 0);
                        v->layout();
                        v->refresh();
                    }
                }
            }
        });
    }

private:
    std::weak_ptr<cycfi::elements::view> mView;
    std::shared_ptr<cycfi::elements::deck_composite> mOverlayDeck;
};

} // anonymous namespace

MainWindow::MainWindow(UiBuilder& builder)
    : mApp("Exchange Scratchpad")
    , mWindow(mApp.name())
    , mBuilder(builder)
{
    mWindow.on_close = [this]() { mApp.stop(); };
    mView = std::make_shared<el::view>(mWindow);

    SetupContent();
}

MainWindow::~MainWindow()
{
    mTabRoots.clear();
}

int MainWindow::Run()
{
    PanelType initialType = PanelType::Empty;
    if (mDefaultPanelTypeAccessor)
        initialType = mDefaultPanelTypeAccessor();
    OnNewTab(initialType);

    mApp.run();
    return 0;
}

void MainWindow::SetOnPanelCreated(on_panel_created_t handler)
{
    mOnPanelCreated = std::move(handler);
}

void MainWindow::SetOnPanelClosed(on_panel_closed_t handler)
{
    mOnPanelClosed = std::move(handler);
}

void MainWindow::SetDefaultPanelTypeAccessor(default_panel_type_accessor_t accessor)
{
    mDefaultPanelTypeAccessor = std::move(accessor);
}

void MainWindow::SetInstrumentPanelDefaultsAccessor(instrument_panel_defaults_accessor_t accessor)
{
    mInstrumentPanelDefaultsAccessor = std::move(accessor);
}

void MainWindow::SetupContent()
{
    mTabBar = std::make_unique<TabBar>(mView);

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

std::shared_ptr<LeafPanelNode> MainWindow::OnNewTab(PanelType type)
{
    auto leaf = MakeLeaf(type);

    auto slot = std::make_shared<el::deck_composite>();
    slot->push_back(leaf->GetElement());
    slot->select(0);

    tab_id tid = mTabBar->AddTab(PanelTypeName(type), el::share(el::hold(slot)));
    mTabRoots[tid] = TabRoot{leaf, slot};
    return leaf;
}

std::shared_ptr<LeafPanelNode> MainWindow::MakeLeaf(PanelType type)
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

    std::shared_ptr<cockpit::ContentPanel> panel;
    el::element_ptr element;

    const bool instrumentBased = (type == PanelType::MarketGraph || type == PanelType::OrderBook);

    if (instrumentBased) {
        auto widgets = mBuilder.MakeInstrumentPanel(type, std::move(onClose), std::move(onSplit));
        element = widgets.root;
        const auto defaults = mInstrumentPanelDefaultsAccessor ? mInstrumentPanelDefaultsAccessor() : InstrumentPanelDefaults{};
        panel = InstrumentPanelElement::Create(type, defaults.candle_period, defaults.candle_width_pixels, mView, std::move(widgets));
    } else {
        auto [elem, deck] = mBuilder.MakePanel(type, std::move(onChangeType), std::move(onClose), std::move(onSplit));
        element = std::move(elem);
        panel = std::make_shared<ElementsContentPanel>(type, mView, std::move(deck));
    }

    panel_id pid = 0;
    if (mOnPanelCreated)
        pid = mOnPanelCreated(std::move(panel));

    leaf->Initialize(std::move(element), pid, [this, pid]() {
        if (mOnPanelClosed) mOnPanelClosed(pid);
    });

    return leaf;
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
                auto emptyLeaf = MakeLeaf(PanelType::Empty);
                ReplaceNode(node, emptyLeaf);
            }
            return;
        }
    }

    for (auto& [tid, root] : mTabRoots) {
        if (!root.node->IsLeaf() && ContainsNode(root.node.get(), node.get())) {
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
        if (!root.node->IsLeaf() && ContainsNode(root.node.get(), oldNode.get())) {
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

} // namespace scratcher::elements
