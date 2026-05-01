// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

#include <thorvg.h>

#include "bybit/entities/instrument.hpp"
#include "content_panel.hpp"
#include "data_controller.hpp"
#include "datahub/data_subscription.hpp"
#include "pixel_rect.hpp"
#include "scratcher.hpp"

namespace scratcher::cockpit {

class InstrumentContentPanel : public ContentPanel
{
public:
    InstrumentContentPanel(PanelType type, std::weak_ptr<IDataController> controller);
    ~InstrumentContentPanel() override;

    void SelectSymbol(std::string symbol);
    const std::string& Symbol() const { return mSymbol; }
    const std::optional<bybit::InstrumentInfo>& Instrument() const { return mInstrument; }

    tvg::Scene* RootScene() const { return mRootScene; }
    tvg::Scene* UiScene() const { return mUiScene; }
    tvg::Scene* LogicalCanvasScene() const { return mLogicalCanvasScene; }
    tvg::Scene* LogicalScene() const { return mLogicalScene; }

    PixelRect& MutableClientRect() { return mClientRect; }
    const PixelRect& GetClientRect() const { return mClientRect; }

    const char* DefaultFontName() const;
    float DefaultFontSize() const { return mFontSize; }

    void AddScratcher(std::shared_ptr<Scratcher> scratcher);

    void SetTarget(uint32_t* buffer, uint32_t stride, uint32_t width, uint32_t height);
    void OnSize(int width, int height);
    void Render();

protected:
    void InitInstrumentSubscription(std::weak_ptr<InstrumentContentPanel> self);
    void InitScratchers();

    virtual void PostToUi(std::function<void()> fn) = 0;
    virtual void OnInstrumentsReady(std::vector<std::string> symbols) = 0;
    virtual void OnSymbolSelected(const std::string& symbol) = 0;

    std::weak_ptr<IDataController> mController;

private:
    void BuildSceneTree();
    void ApplySceneTransforms();
    void ResolveInstrument();

    std::shared_ptr<datahub::data_subscription<std::deque<bybit::InstrumentInfo>>> mListSub;
    std::string mSymbol;
    std::optional<bybit::InstrumentInfo> mInstrument;

    tvg::SwCanvas* mCanvas = nullptr;
    tvg::Scene* mRootScene = nullptr;
    tvg::Scene* mUiScene = nullptr;
    tvg::Scene* mLogicalCanvasScene = nullptr;
    tvg::Scene* mLogicalScene = nullptr;

    PixelRect mClientRect{};
    int mCanvasWidth = 0;
    int mCanvasHeight = 0;

    std::deque<std::shared_ptr<Scratcher>> mScratchers;
    mutable std::shared_mutex mScratcherMutex;

    float mFontSize = 12.0f;
};

} // namespace scratcher::cockpit
