// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>

#include "bybit/entities/instrument.hpp"
#include "data_controller.hpp"
#include "data_rectangle.hpp"
#include "scratcher.hpp"
#include "scratchers/quote_scratcher.hpp"
#include "tvg_ptr.hpp"
#include "vector_scene_panel.hpp"

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

// One canvas pixel measured in a scene's local coordinates, decomposed per axis.
// For axis-aligned scene transforms (e12 = e21 = 0, which holds for both HudScene
// and LogicalScene) this is simply the inverse of |e11| and |e22| of the composed
// transform from local → canvas. Scratchers consult this when they need geometry
// whose visible dimension is pixel-stable under a scene whose local units are not
// pixels — for example a candle's diamond body whose height must remain N px even
// as the price-axis scale changes.
struct ScenePixelSize
{
    float x;
    float y;
};

class InstrumentPanel : public VectorScenePanel
{
public:
    InstrumentPanel(PanelType type, seconds candle_period, uint32_t candle_width_pixels);
    ~InstrumentPanel() override;

    const bybit::InstrumentInfo& Instrument() const { return mInstrument; }
    const std::string& Symbol() const { return mInstrument.symbol; }

    // Two-scene layered model: HudScene() carries pixel-space widgets (rulers, labels) in
    // HUD-Y-up coords. mLogicalScene nests inside and adds M_view · scale · -floor to map
    // (timestamp_ms, price_points) into the inner data rect; HUD's flip carries it down to
    // canvas pixels. Both scenes live for the panel's lifetime.
    tvg::Scene& LogicalScene() const { return *mLogicalScene; }

    Rectangle& MutableInnerDataRect() { return mInnerDataRect; }
    const Rectangle& InnerDataRect() const { return mInnerDataRect; }

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

    seconds CandlePeriod() const { return mCandlePeriod; }
    uint32_t CandleWidth() const { return mCandleWidthPixels; }

    void AddScratcher(std::shared_ptr<Scratcher> scratcher);
    std::shared_ptr<QuoteScratcher> QuoteScratcherInstance() const { return mQuoteScratcher; }

    void SetInstrument(bybit::InstrumentInfo info);

    std::size_t PriceDecimals() const
    { return mPriceDecimals; }

    std::size_t SizeDecimals() const
    { return mSizeDecimals; }

    void OnPublicTrades(datahub::update_kind kind, IDataController::public_trades_feed_type::const_iterator first, IDataController::public_trades_feed_type::const_iterator last);

    void SetTradeSubscription(std::shared_ptr<IDataController::public_trades_feed_type::subscription_type> sub)
    { mTradeSubscription = std::move(sub); }

    // HUD-X projection of a model timestamp under the current view transform. Read
    // directly from mLogicalScene's matrix so callers get the live composition of
    // scale + view offset without duplicating any state. The inverse is TimeOfHudX,
    // used for hit-testing and reverse mouse-pick.
    float HudXOfTime(int64_t time_ms) const;
    int64_t TimeOfHudX(float hud_x) const;

    // HUD-Y projection of a model price (in scene-grid points) under the current view
    // transform — the price-axis twin of HudXOfTime. Reads mLogicalScene's matrix (e22 =
    // px_per_point, e23 = HUD-y of the floor price) so callers get the live price→pixel mapping
    // without duplicating the autoscale state. Returns the HUD-local (Y-up) coordinate, ready to
    // draw into HudScene. PriceOfHudY is the inverse, used by the price ruler to recover the
    // visible price band at the inner-rect edges.
    float HudYOfPrice(uint64_t price_points) const;
    uint64_t PriceOfHudY(float hud_y) const;

    // Returns the canvas-pixel size measured in `scene`'s local coordinates. Resolves
    // HudScene → (1, 1) (HUD applies only a Y-flip-about-canvas_h, no axis scaling)
    // and mLogicalScene → (1/|e11|, 1/|e22|) from the live LogicalScene transform.
    // Sub-scenes added directly under either as identity inherit the parent's value;
    // any unknown scene falls back to (1, 1) so callers never silently scale by zero.
    ScenePixelSize PixelSizeOf(const tvg::Scene& scene) const;

    CanvasExtent MinimalCanvasSize() const override { return CanvasExtent{380, 240}; }

protected:
    // Reseeds the inner rect to the full canvas (each ruler's CalculateSize subtracts its strip),
    // runs the scratcher layout phases around the logical-scene transform refresh. Always a full
    // redraw: the live view edge moves with wall-clock time on every tick.
    bool DoLayout() override;

private:
    void ApplyLogicalSceneTransform();
    void EnsureViewAnchor();

    bybit::InstrumentInfo mInstrument;

    tvg_ptr<tvg::Scene> mLogicalScene;

    Rectangle mInnerDataRect{};

    SceneFloor mSceneFloor{};
    int  mRightPadPx = 0;                        // live-edge right inset; recomputed each DoLayout in EnsureViewAnchor
    std::optional<int64_t> mPinnedViewLeftMs;    // set: pinned (tests / scrolling); unset: live wall clock

    std::size_t mPriceDecimals = 0;
    std::size_t mSizeDecimals = 0;

    std::deque<std::shared_ptr<Scratcher>> mScratchers;
    std::shared_ptr<QuoteScratcher> mQuoteScratcher;
    mutable std::shared_mutex mScratcherMutex;

    std::shared_ptr<IDataController::public_trades_feed_type::subscription_type> mTradeSubscription;

    const seconds mCandlePeriod;
    const uint32_t mCandleWidthPixels;
};

} // namespace scratcher::cockpit
