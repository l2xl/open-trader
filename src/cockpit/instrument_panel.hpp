// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
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

class InstrumentPanel : public ContentPanel
{
public:
    InstrumentPanel(PanelType type, seconds candle_period, uint32_t candle_width_pixels);
    ~InstrumentPanel() override;

    // Bind the panel to its instrument. Called once by the cockpit at registration time;
    // the instrument never mutates within a panel's lifetime — symbol re-selection is a
    // panel-replace operation in MainWindow, not an in-place rebind.
    void SetInstrumentInfo(bybit::InstrumentInfo info);

    const bybit::InstrumentInfo& Instrument() const { return mInstrument; }
    const std::string& Symbol() const { return mInstrument.symbol; }

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

    const std::string& DefaultFontName() const;
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

    // Allocate (or reallocate) the render buffer at the given dimensions and bind it
    // as the ThorVG canvas target as a single action. The new buffer replaces any
    // previous one atomically: the canvas is rebound to the fresh storage BEFORE the
    // previous buffer is freed, so the canvas target never points at freed memory.
    // After allocation the layout is recomputed for the new size. The buffer is laid
    // out as ARGB8888 with a tight stride of `width` pixels per row.
    void AllocatePixelBuffer(int width, int height);

    // Raw access to the render buffer for callers that need to wrap it in a
    // platform-specific surface (e.g. a cairo image surface for cycfi/elements).
    // Returns nullptr until AllocatePixelBuffer has been called. The wrapper must
    // not outlive the panel — the panel owns the storage.
    uint32_t* PixelBufferData() noexcept { return mPixels.data(); }

    // Circuit B (worker-safe, blocking): take the data lock, run DoUpdate, stamp the
    // monotonic timestamp, release. Subclasses post the UI redraw via Refresh().
    void Update() override;

    // Circuit A (UI-thread paint hook): cheap try-lock + frame-throttle gate. If the
    // throttle window has not elapsed OR another thread holds the data lock, returns
    // immediately and the caller proceeds to Render() with the previously published
    // scene state. Otherwise runs DoUpdate() and stamps the timestamp.
    void OnUpdate();

    // Damage-tracked render. Returns the rect that was repainted, in canvas-pixel coords;
    // an empty rect means no draw happened this frame and the host should NOT call
    // cairo_surface_mark_dirty_rectangle (the buffer is unchanged from the previous frame).
    // Holds mDataMutex across the whole viewport→update→draw→sync sequence — workers
    // wait one frame's rasterisation latency, which the 100 ms worker tick absorbs.
    PixelRect Render();

    //virtual void PostToUi(std::function<void()> fn) = 0;

protected:
    // Pure scene/scratcher recalculation. PRECONDITION: caller holds mDataMutex.
    void DoUpdate();

    // Tunable; default 16 ms (~60 Hz). Set to 0 to disable throttling (useful in tests
    // where deterministic single-frame rendering is required).
    void SetUpdateThrottle(std::chrono::nanoseconds dt) noexcept { mUpdateThrottleNs = dt.count(); }

    virtual void OnInstrumentInfoChanged(const bybit::InstrumentInfo& /*info*/) {}

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

    bybit::InstrumentInfo mInstrument;

    ThorvgRuntimeRef mRuntime;

    // Render buffer; declared BEFORE mCanvas so that reverse-of-declaration member
    // destruction tears down the canvas (which holds mPixels.data() as its target
    // pointer) BEFORE the buffer storage is released.
    std::vector<uint32_t> mPixels;

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

    // Serialises Circuit A (UI paint hook) and Circuit B (worker Update) against each
    // other and against Render()'s viewport+canvas->update() phase. Distinct from
    // mScratcherMutex (which only protects the scratcher deque structure) — the data
    // mutex covers the whole DoUpdate critical section AND the ThorVG scene-tree walk
    // that follows in Render().
    mutable std::mutex mDataMutex;

    // Monotonic ns-since-steady_clock-epoch of the last DoUpdate completion. Read by
    // OnUpdate() to gate the throttle without locking. int64_t over std::atomic guarantees
    // is_always_lock_free on every supported platform; std::atomic<time_point> does not.
    std::atomic<int64_t> mLastUpdateNs{0};

    // 16 ms = 60 Hz. Updated via SetUpdateThrottle(); 0 disables throttling.
    int64_t mUpdateThrottleNs = 16'000'000;

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
