// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include <chrono>
#include <cstdint>
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
#include "pixel_rect.hpp"
#include "scratcher.hpp"
#include "scratchers/quote_scratcher.hpp"

namespace scratcher::cockpit {

namespace tvg_detail {
struct PaintReleaser
{
    void operator()(tvg::Paint* p) const noexcept { tvg::Paint::rel(p); }
};
}

template<typename T>
using tvg_ptr = std::unique_ptr<T, tvg_detail::PaintReleaser>;

struct SceneFloor
{
    uint64_t time_ms = 0;
    uint64_t price_points = 0;
};

class InstrumentContentPanel : public ContentPanel
{
public:
    InstrumentContentPanel(PanelType type, std::chrono::seconds candle_period, uint32_t candle_width_pixels);
    ~InstrumentContentPanel() override;

    void SetSymbol(std::string symbol);
    void SetInstrumentList(std::vector<std::string> symbols);
    void SetInstrumentInfo(std::optional<bybit::InstrumentInfo> info);

    const std::string& Symbol() const { return mSymbol; }
    const std::vector<std::string>& InstrumentList() const { return mInstrumentList; }
    const std::optional<bybit::InstrumentInfo>& Instrument() const { return mInstrument; }

    tvg::Scene* RootScene() const { return mRootScene.get(); }
    tvg::Scene* UiScene() const { return mUiScene.get(); }
    tvg::Scene* LogicalCanvasScene() const { return mLogicalCanvasScene.get(); }
    tvg::Scene* LogicalScene() const { return mLogicalScene.get(); }

    PixelRect OuterCanvasRect() const { return PixelRect{0, 0, mCanvasWidth, mCanvasHeight}; }
    PixelRect& MutableInnerDataRect() { return mInnerDataRect; }
    const PixelRect& InnerDataRect() const { return mInnerDataRect; }

    const SceneFloor& GetSceneFloor() const { return mSceneFloor; }
    void SetSceneFloor(SceneFloor floor);

    const char* DefaultFontName() const;
    float DefaultFontSize() const { return mFontSize; }

    seconds CandlePeriod() const { return mCandlePeriod; }
    uint32_t CandleWidth() const { return mCandleWidthPixels; }

    void AddScratcher(std::shared_ptr<Scratcher> scratcher);
    std::shared_ptr<QuoteScratcher> QuoteScratcherInstance() const { return mQuoteScratcher; }

    void SetTarget(uint32_t* buffer, uint32_t stride, uint32_t width, uint32_t height);
    void OnSize(int width, int height);
    void Render();

    using on_user_symbol_selection_t = std::function<void(std::string)>;
    void SetOnUserSymbolSelection(on_user_symbol_selection_t handler) { mOnUserSymbolSelection = std::move(handler); }

    virtual void PostToUi(std::function<void()> fn) = 0;

protected:
    void EmitUserSymbolSelection(std::string symbol);

    virtual void OnSymbolChanged(const std::string& /*symbol*/) {}
    virtual void OnInstrumentListChanged(const std::vector<std::string>& /*symbols*/) {}
    virtual void OnInstrumentInfoChanged(const std::optional<bybit::InstrumentInfo>& /*info*/) {}

private:
    struct ThorvgRuntimeRef
    {
        ThorvgRuntimeRef();
        ~ThorvgRuntimeRef();
        ThorvgRuntimeRef(const ThorvgRuntimeRef&) = delete;
        ThorvgRuntimeRef& operator=(const ThorvgRuntimeRef&) = delete;
    };

    void ApplyOuterSceneTransforms();
    void ApplyLogicalSceneTransform();

    std::string mSymbol;
    std::vector<std::string> mInstrumentList;
    std::optional<bybit::InstrumentInfo> mInstrument;
    on_user_symbol_selection_t mOnUserSymbolSelection;

    ThorvgRuntimeRef mRuntime;
    std::unique_ptr<tvg::SwCanvas> mCanvas;
    tvg_ptr<tvg::Scene> mRootScene;
    tvg_ptr<tvg::Scene> mUiScene;
    tvg_ptr<tvg::Scene> mLogicalCanvasScene;
    tvg_ptr<tvg::Scene> mLogicalScene;

    PixelRect mInnerDataRect{};
    int mCanvasWidth = 0;
    int mCanvasHeight = 0;

    SceneFloor mSceneFloor{};

    std::deque<std::shared_ptr<Scratcher>> mScratchers;
    std::shared_ptr<QuoteScratcher> mQuoteScratcher;
    mutable std::shared_mutex mScratcherMutex;

    float mFontSize = 12.0f;
    const seconds mCandlePeriod;
    const uint32_t mCandleWidthPixels;
};

} // namespace scratcher::cockpit
