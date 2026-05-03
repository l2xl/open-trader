// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <vector>

#include "bybit/entities/instrument.hpp"
#include "content_panel.hpp"
#include "pixel_rect.hpp"
#include "scratcher.hpp"
#include "scratchers/quote_scratcher.hpp"
#include "tvg_ptr.hpp"

namespace scratcher::cockpit {

// SceneFloor is a STATIC local epoch chosen so that fixed-point ms values cast to
// float for the rendering pipeline keep usable precision: float has 24 mantissa
// bits, so an absolute Unix-epoch ms of order 1.7e12 has ulp ~131s and renders
// adjacent ms values as the same coordinate. Casting (t - floor) keeps the float
// magnitude small and the ulp tight.
//
// Strategy: floor is at least one year before now (snapped to the candle period).
// At a 1-year offset, max float ulp is ~2s — fine for second-or-larger candles,
// marginal for sub-second zooms. Once quote data flows in, scratchers can call
// SetSceneFloor() to push the floor back to the actual data series start (still
// snapped). The floor MUST NOT change with view scrolling or window resize; it
// changes only when the data series boundary changes.
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

    // Two-scene layered model: mHudScene carries pixel-space widgets (rulers, labels)
    // in HUD-Y-up coords (Y=0 at canvas bottom), achieved via a Y-flip-about-canvas_h on
    // the scene itself. mLogicalScene nests inside and adds M_view · scale · -floor to
    // map (timestamp_ms, price_points) into the inner data rect; HUD's flip carries it
    // down to canvas pixels. Both scenes live for the panel's lifetime.
    tvg::Scene& HudScene() const { return *mHudScene; }
    tvg::Scene& LogicalScene() const { return *mLogicalScene; }

    PixelRect OuterCanvasRect() const { return PixelRect{0, 0, mCanvasWidth, mCanvasHeight}; }
    PixelRect& MutableInnerDataRect() { return mInnerDataRect; }
    const PixelRect& InnerDataRect() const { return mInnerDataRect; }

    const SceneFloor& GetSceneFloor() const { return mSceneFloor; }
    // Adjust the static precision epoch. Scratchers (e.g. QuoteScratcher when a
    // longer data series arrives) call this to move the floor further into the
    // past. Triggers a LogicalScene transform refresh on the next OnSize.
    void SetSceneFloor(SceneFloor floor);

    // Time at the leftmost pixel of InnerDataRect for the current view. Live mode
    // (default) derives it from wall-clock now anchored at `right - mRightPadPx`;
    // pinned mode (tests and future scrolling) returns the value last passed to
    // SetViewLeftTimeMs. Snapped to the candle period.
    int64_t ViewLeftTimeMs() const;
    void SetViewLeftTimeMs(std::optional<int64_t> t_ms);

    const char* DefaultFontName() const;
    float DefaultFontSize() const { return mFontSize; }

    seconds CandlePeriod() const { return mCandlePeriod; }
    uint32_t CandleWidth() const { return mCandleWidthPixels; }

    void AddScratcher(std::shared_ptr<Scratcher> scratcher);
    std::shared_ptr<QuoteScratcher> QuoteScratcherInstance() const { return mQuoteScratcher; }

    // Subscriptions. Callers receive a SubscriptionId from Subscribe* and use it to
    // Unsubscribe. SubscribeSize fires when the canvas (and inner data rect) change;
    // SubscribeView fires when the view's pan/zoom transform on mLogicalScene changes
    // (currently only invoked via SetViewLeftTimeMs; pan/zoom UI is Phase 4 work).
    using SizeCallback = std::function<void()>;
    using ViewCallback = std::function<void()>;
    using SubscriptionId = uint64_t;
    SubscriptionId SubscribeSize(SizeCallback cb);
    SubscriptionId SubscribeView(ViewCallback cb);
    void Unsubscribe(SubscriptionId id);

    // Damage tracking. Scratchers call this BEFORE mutating a paint; the panel captures
    // the paint's pre-mutation bounds (valid from the previous frame's sync) and adds
    // them to the damage union for the next Render(). If bounds() fails — e.g. on the
    // very first frame, or after a full redraw — the panel falls back to full-canvas
    // redraw on the next Render(), so missing pre-bounds never produces a stale frame.
    void MarkDirty(tvg::Paint* paint);

    // HUD-X projection of a model timestamp under the current view transform. Read
    // directly from mLogicalScene's matrix so callers get the live composition of
    // scale + view offset without duplicating any state. The inverse is TimeOfHudX,
    // used for hit-testing and reverse mouse-pick.
    float HudXOfTime(int64_t time_ms) const;
    int64_t TimeOfHudX(float hud_x) const;

    // `buffer` is the pixel framebuffer ThorVG draws into. Caller owns the storage and
    // must keep it alive at least as long as Render() / Canvas::draw() may run; we hold
    // a non-owning view. `stride` is the row stride in pixels (may exceed `width` for
    // alignment padding); `buffer.size()` must therefore equal `stride * height`.
    void SetTarget(std::span<uint32_t> buffer, uint32_t stride, uint32_t width, uint32_t height);
    void OnSize(int width, int height);

    // Damage-tracked render. Returns the rect that was repainted, in canvas-pixel coords;
    // an empty rect means no draw happened this frame and the host should NOT call
    // cairo_surface_mark_dirty_rectangle (the buffer is unchanged from the previous frame).
    PixelRect Render();

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
    void EnsureViewAnchor();

    std::string mSymbol;
    std::vector<std::string> mInstrumentList;
    std::optional<bybit::InstrumentInfo> mInstrument;
    on_user_symbol_selection_t mOnUserSymbolSelection;

    ThorvgRuntimeRef mRuntime;
    std::unique_ptr<tvg::SwCanvas> mCanvas;
    tvg_ptr<tvg::Scene> mHudScene;
    tvg_ptr<tvg::Scene> mLogicalScene;

    PixelRect mInnerDataRect{};
    int mCanvasWidth = 0;
    int mCanvasHeight = 0;

    SceneFloor mSceneFloor{};
    int  mRightPadPx = -1;                       // captured on first OnSize as 5% of inner width
    std::optional<int64_t> mPinnedViewLeftMs;    // set: pinned (tests / scrolling); unset: live wall clock

    std::deque<std::shared_ptr<Scratcher>> mScratchers;
    std::shared_ptr<QuoteScratcher> mQuoteScratcher;
    mutable std::shared_mutex mScratcherMutex;

    // Subscription registry. SizeCallback and ViewCallback share the same id space so a
    // single Unsubscribe(id) suffices regardless of which channel registered the id.
    struct Subscription
    {
        SizeCallback on_size;
        ViewCallback on_view;
    };
    std::map<SubscriptionId, Subscription> mSubscriptions;
    SubscriptionId mNextSubscriptionId = 1;

    // Damage-tracking state.
    struct DirtyEntry
    {
        tvg_ptr<tvg::Paint> paint;
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    };
    std::vector<DirtyEntry> mDirtyPaints;
    bool mLayoutDirty = true;  // true on first frame and after every OnSize

    float mFontSize = 12.0f;
    const seconds mCandlePeriod;
    const uint32_t mCandleWidthPixels;
};

} // namespace scratcher::cockpit
