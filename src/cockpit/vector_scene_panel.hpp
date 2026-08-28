// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <thorvg.h>

#include "content_panel.hpp"
#include "data_rectangle.hpp"
#include "tvg_ptr.hpp"

namespace scratcher::cockpit {

struct CanvasExtent
{
    uint32_t w = 0;
    uint32_t h = 0;
};

// Offscreen ThorVG host shared by every vector-scene panel: the runtime reference, the ARGB render
// buffer bound as the SwCanvas target, the HUD root scene, the two update circuits and the
// damage-tracked render loop. Subclasses attach their content under HudScene() and lay it out in
// DoLayout().
class VectorScenePanel : public ContentPanel
{
public:
    ~VectorScenePanel() override;

    // HUD root carries pixel-space content in HUD-Y-up coords (Y=0 at canvas bottom), achieved via a
    // Y-flip-about-canvas_h on the scene itself. Lives for the panel's lifetime.
    tvg::Scene& HudScene() const { return *mHudScene; }

    Rectangle OuterCanvasRect() const { return Rectangle{0, 0, mCanvasWidth, mCanvasHeight}; }
    uint32_t CanvasWidth() const { return mCanvasWidth; }
    uint32_t CanvasHeight() const { return mCanvasHeight; }

    // Smallest canvas the panel's scene lays out sensibly; the host widget reports it as its size limit.
    virtual CanvasExtent MinimalCanvasSize() const = 0;

    const std::string& DefaultFontName() const;
    float DefaultFontSize() const { return mFontSize; }

    // Damage tracking. Callers invoke this BEFORE mutating a paint; the panel captures the paint's
    // pre-mutation bounds (valid from the previous frame's sync) and adds them to the damage union
    // for the next Render(). If bounds() fails — e.g. on the very first frame, or after a full
    // redraw — the panel falls back to full-canvas redraw on the next Render(), so missing
    // pre-bounds never produces a stale frame.
    void MarkDirty(tvg::Paint* paint);

    // Allocate (or reallocate) the render buffer at the given dimensions and bind it as the ThorVG
    // canvas target as a single action. The canvas is rebound to the fresh storage BEFORE the
    // previous buffer is freed, so the canvas target never points at freed memory. The layout is
    // recomputed for the new size. ARGB8888 with a tight stride of `width` pixels per row.
    void AllocatePixelBuffer(uint32_t width, uint32_t height);

    // Raw access to the render buffer for callers that wrap it in a platform-specific surface (e.g. a
    // cairo image surface). Returns nullptr until AllocatePixelBuffer has been called; the wrapper
    // must not outlive the panel.
    uint32_t* PixelBufferData() noexcept { return mPixels.data(); }

    // Circuit B (worker-safe, blocking): take the data lock, run DoUpdate, release. Subclasses post
    // the UI redraw via Refresh().
    void Update() override;

    // Circuit A (UI-thread paint hook): cheap try-lock + frame-throttle gate. If the throttle window
    // has not elapsed OR another thread holds the data lock, returns immediately and the caller
    // proceeds to Render() with the previously published scene state.
    void OnUpdate();

    // Damage-tracked render. Returns the repainted rect in canvas-pixel coords; an empty rect means
    // no draw happened this frame. Holds mDataMutex across the whole viewport→update→draw→sync
    // sequence — workers wait one frame's rasterisation latency.
    Rectangle Render();

    // Resolves a runtime asset: ./resources/<name>, ./<name>, then <executable dir>/resources/<name>.
    // Returns an empty path when nothing exists.
    static std::filesystem::path FindResource(const std::string& file_name);

protected:
    explicit VectorScenePanel(PanelType type);

    // Subclass layout pass, run by DoUpdate after the HUD clip/flip has been refreshed for the
    // current canvas size. PRECONDITION: caller holds mDataMutex. Returns true when the scene changed
    // in a way that invalidates the damage list and needs a full redraw.
    virtual bool DoLayout() = 0;

    // PRECONDITION: caller holds mDataMutex.
    void DoUpdate();

    // Tunable; default 16 ms (~60 Hz). 0 disables throttling (deterministic single-frame tests).
    void SetUpdateThrottle(std::chrono::nanoseconds dt) noexcept { mUpdateThrottleNs = dt.count(); }

    // Serialises Circuit A (UI paint hook) and Circuit B (worker Update) against each other and
    // against Render()'s viewport+canvas->update() phase; covers the whole DoUpdate critical
    // section AND the ThorVG scene-tree walk that follows in Render().
    mutable std::mutex mDataMutex;

private:
    struct ThorvgRuntimeRef
    {
        ThorvgRuntimeRef();
        ~ThorvgRuntimeRef();
        ThorvgRuntimeRef(const ThorvgRuntimeRef&) = delete;
        ThorvgRuntimeRef& operator=(const ThorvgRuntimeRef&) = delete;
    };

    void ApplyOuterSceneTransforms();

    ThorvgRuntimeRef mRuntime;

    // Declared BEFORE mCanvas so that reverse-of-declaration member destruction tears down the canvas
    // (which holds mPixels.data() as its target pointer) BEFORE the buffer storage is released.
    std::vector<uint32_t> mPixels;

    std::unique_ptr<tvg::SwCanvas> mCanvas;
    tvg_ptr<tvg::Scene> mHudScene;

    uint32_t mCanvasWidth = 0;
    uint32_t mCanvasHeight = 0;

    // Monotonic ns of the last DoUpdate completion; read by OnUpdate() to gate the throttle without
    // locking. int64_t over std::atomic guarantees is_always_lock_free on every supported platform.
    std::atomic<int64_t> mLastUpdateNs{0};
    int64_t mUpdateThrottleNs = 16'000'000;

    struct DirtyEntry
    {
        tvg_ptr<tvg::Paint> paint;
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    };
    std::vector<DirtyEntry> mDirtyPaints;
    bool mLayoutDirty = true;

    float mFontSize = 12.0f;
};

} // namespace scratcher::cockpit
