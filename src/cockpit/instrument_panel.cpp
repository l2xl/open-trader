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

#include "instrument_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <ranges>

#include "scratchers/price_indicator.hpp"
#include "scratchers/price_ruler.hpp"
#include "scratchers/time_ruler.hpp"
#include "timedef.hpp"

namespace scratcher::cockpit {

namespace {

inline int64_t MonotonicNs() noexcept
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

class ThorvgRuntime
{
public:
    static ThorvgRuntime& Instance()
    {
        static ThorvgRuntime instance;
        return instance;
    }

    void Acquire()
    {
        std::lock_guard lock(mMutex);
        if (mRefCount++ == 0)
            tvg::Initializer::init(0);
    }

    void Release()
    {
        std::lock_guard lock(mMutex);
        if (mRefCount > 0 && --mRefCount == 0)
            tvg::Initializer::term();
    }

private:
    ThorvgRuntime() = default;
    std::mutex mMutex;
    int mRefCount = 0;
};

constexpr const std::string kDefaultFontName = "OpenSans";

std::filesystem::path FindDefaultFontPath()
{
    namespace fs = std::filesystem;
    const std::array<const char*, 3> candidates = {
        "resources/OpenSans-Regular.ttf",
        "OpenSans-Regular.ttf",
        "/usr/share/fonts/truetype/open-sans/OpenSans-Regular.ttf",
    };
    for (const char* candidate : candidates) {
        fs::path p{candidate};
        if (fs::exists(p)) return p;
    }
    return {};
}

bool LoadDefaultFont()
{
    static bool attempted = false;
    static bool loaded = false;
    if (attempted) return loaded;
    attempted = true;

    auto path = FindDefaultFontPath();
    if (path.empty()) return false;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;

    const auto size = static_cast<std::streamsize>(file.tellg());
    file.seekg(0, std::ios::beg);

    static std::vector<char> buffer;
    buffer.resize(static_cast<size_t>(size));
    if (!file.read(buffer.data(), size)) return false;

    auto result = tvg::Text::load(kDefaultFontName.c_str(), buffer.data(), static_cast<uint32_t>(buffer.size()), "ttf", false);
    loaded = (result == tvg::Result::Success);
    return loaded;
}

}

InstrumentPanel::ThorvgRuntimeRef::ThorvgRuntimeRef()
{
    ThorvgRuntime::Instance().Acquire();
}

InstrumentPanel::ThorvgRuntimeRef::~ThorvgRuntimeRef()
{
    ThorvgRuntime::Instance().Release();
}

InstrumentPanel::InstrumentPanel(PanelType type, seconds candle_period, uint32_t candle_width_pixels)
    : ContentPanel(type)
    , mRuntime{}
    , mCanvas{tvg::SwCanvas::gen()}
    , mHudScene{tvg::Scene::gen()}              // gen() returns refCnt=0; tvg_ptr ctor calls ref()
    , mLogicalScene{tvg::Scene::gen()}          // → refCnt=1. Scene::add() below ref()s again
    , mCandlePeriod{candle_period}              // → refCnt=2 (wrapper + parent). On teardown the
    , mCandleWidthPixels{candle_width_pixels}   // wrapper's unref() drops it back to 1, and the
                                                // parent's cascade unref() takes it to 0 → freed.
{
    LoadDefaultFont();

    mHudScene->add(mLogicalScene.get());
    mCanvas->add(mHudScene.get());

    if (Type() == PanelType::MarketGraph) {
        AddScratcher(std::make_shared<TimeRuler>());
        AddScratcher(std::make_shared<PriceRuler>());
        mQuoteScratcher = std::make_shared<QuoteScratcher>(milliseconds(mCandlePeriod));
        AddScratcher(mQuoteScratcher);
        // Last after the quote scratcher: its HUD overlay (last-price line + label box) draws
        // on top of the candles and the rulers, and its OnLayout reads mQuoteScratcher live.
        AddScratcher(std::make_shared<PriceIndicator>());
    }

    using namespace std::chrono;
    const auto period = duration_cast<milliseconds>(mCandlePeriod);

    // Snap the precision floor to one calendar year before today. Kept in the system_clock
    // (Unix ms, leap-second-free) frame so it shares the wire-trade / buoy / view time frame —
    // utc_clock would offset it ~27 s. A fixed 365-day step drifts ~5h45m/year on average vs.
    // the Gregorian calendar, so we walk the calendar with year_month_day; a Feb 29 source folds
    // to Feb 28 of the prior year via the year_month_day_last fallback.
    const auto today = floor<days>(system_clock::now());
    auto floor_ymd = year_month_day{today} - years{1};
    if (!floor_ymd.ok())
        floor_ymd = floor_ymd.year() / floor_ymd.month() / last;
    const auto floor_unaligned = duration_cast<milliseconds>(sys_days{floor_ymd}.time_since_epoch());
    const auto floor_aligned = period.count() > 0 ? floor_unaligned - (floor_unaligned % period) : floor_unaligned;

    mSceneFloor.time_ms = static_cast<uint64_t>(floor_aligned.count());

    const float px_per_ms = period.count() > 0 ? static_cast<float>(mCandleWidthPixels) / static_cast<float>(period.count()) : 0.0f;
    mLogicalScene->transform(tvg::Matrix{px_per_ms, 0.0f, 0.0f,
                                          0.0f,      0.0f, 0.0f,
                                          0.0f,      0.0f, 1.0f});
}

InstrumentPanel::~InstrumentPanel()
{
    // Detach scratchers in reverse-attach order so their dependents (e.g. quote
    // scratcher depending on rulers' inner-rect work) tear down before their
    // dependencies. mDataMutex serialises against any in-flight worker Update();
    // mScratcherMutex protects the deque iteration. Remaining members (scene
    // wrappers, canvas, pixel buffer) tear down via their member destructors in
    // reverse declaration order; mPixels is declared before mCanvas so the canvas
    // dies before its target buffer is freed.
    std::lock_guard data_lock(mDataMutex);
    std::unique_lock lock(mScratcherMutex);
    for (auto it = mScratchers.rbegin(); it != mScratchers.rend(); ++it) {
        (*it)->OnDetach(*this);
    }
    mScratchers.clear();
}

void InstrumentPanel::SetSceneFloor(SceneFloor floor)
{
    mSceneFloor = floor;
}

void InstrumentPanel::SetViewLeftTimeMs(std::optional<int64_t> t_ms)
{
    mPinnedViewLeftMs = t_ms;
}

void InstrumentPanel::MarkDirty(tvg::Paint* paint)
{
    if (!paint || mLayoutDirty) return;

    DirtyEntry e{tvg_ptr<tvg::Paint>{paint}};
    if (paint->bounds(&e.x, &e.y, &e.w, &e.h) != tvg::Result::Success) {
        // Pre-mutation bounds unavailable — typically the paint hasn't been update()d yet
        // (e.g. first frame, or just-attached). Fall back to full canvas redraw on the
        // next Render(); cheaper than risking a stale frame from an undersized damage rect.
        mLayoutDirty = true;
        mDirtyPaints.clear();
        return;
    }
    mDirtyPaints.push_back(std::move(e));
}

float InstrumentPanel::HudXOfTime(int64_t time_ms) const
{
    // The LogicalScene matrix maps (t - floor) → HUD-X via e11*(t-floor) + e13, with
    // e13 = inner_left - e11*(view_left-floor). For a 1-year floor offset, (t-floor) is
    // ~3e10 ms; cast to FP32 it has ulp ~2 s. The composition is the catastrophic-cancel
    // pattern e11*X1 + (inner_left - e11*X2) where X1 ≈ X2, so the result inherits noise
    // proportional to e11·ulp — for a 25 px/s scale that is ~50 px of jitter, enough to
    // collide adjacent 1 s ticks and rip prior-frame label remnants through the strip.
    //
    // Solve the bias by computing the X delta directly from (t - view_left) in int64
    // (~tens of thousands of ms, exact in FP32) and applying the scale in double. The
    // matrix retains the (t-floor) form for ThorVG's render of the LogicalScene; only
    // the HUD-pixel projection that the scratchers consume is re-derived precisely.
    const tvg::Matrix m = mLogicalScene->transform();
    const int64_t dt_ms = time_ms - ViewLeftTimeMs();
    return static_cast<float>(mInnerDataRect.left) + static_cast<float>(static_cast<double>(m.e11) * static_cast<double>(dt_ms));
}

int64_t InstrumentPanel::TimeOfHudX(float hud_x) const
{
    const tvg::Matrix m = mLogicalScene->transform();
    if (m.e11 == 0.0f) return ViewLeftTimeMs();
    const double dx = static_cast<double>(hud_x) - static_cast<double>(mInnerDataRect.left);
    return ViewLeftTimeMs() + static_cast<int64_t>(std::llround(dx / static_cast<double>(m.e11)));
}

float InstrumentPanel::HudYOfPrice(uint64_t price_points) const
{
    // LogicalScene maps (price - floor) → HUD-y via e22*(price-floor) + e23, with e22 the
    // pixels-per-point price scale and e23 = canvas_h - inner_bottom anchoring the floor price at
    // the inner-rect bottom. The floor tracks the visible bottom price, so (price - floor) is a
    // small in-window offset with no catastrophic-cancel term — the int64 delta is applied in
    // double purely for parity with HudXOfTime.
    const tvg::Matrix m = mLogicalScene->transform();
    const int64_t dp = static_cast<int64_t>(price_points) - static_cast<int64_t>(mSceneFloor.price_points);
    return static_cast<float>(static_cast<double>(m.e22) * static_cast<double>(dp)) + m.e23;
}

uint64_t InstrumentPanel::PriceOfHudY(float hud_y) const
{
    const tvg::Matrix m = mLogicalScene->transform();
    if (std::abs(m.e22) < 1e-9f) return mSceneFloor.price_points;
    const double dp = (static_cast<double>(hud_y) - static_cast<double>(m.e23)) / static_cast<double>(m.e22);
    const int64_t pts = static_cast<int64_t>(mSceneFloor.price_points) + std::llround(dp);
    return pts > 0 ? static_cast<uint64_t>(pts) : 0;
}

ScenePixelSize InstrumentPanel::PixelSizeOf(const tvg::Scene& scene) const
{
    // LogicalScene: composed local → canvas scale is |e11| on X (HUD parent is identity
    // on X) and |e22| on Y (HUD's Y-flip just negates, magnitude preserved). Invert per
    // axis to express one canvas pixel back in local units. Guard near-zero scale so
    // an early frame with an unset matrix never returns +inf.
    if (&scene == mLogicalScene.get()) {
        const tvg::Matrix m = mLogicalScene->transform();
        const float ax = std::abs(m.e11);
        const float ay = std::abs(m.e22);
        return ScenePixelSize{
            ax > 1e-9f ? 1.0f / ax : 1.0f,
            ay > 1e-9f ? 1.0f / ay : 1.0f,
        };
    }
    // HudScene (or unknown): HUD applies only a Y-flip-about-canvas_h, no scaling, so
    // one canvas pixel equals one HUD-local unit on either axis.
    return ScenePixelSize{1.0f, 1.0f};
}

int64_t InstrumentPanel::ViewLeftTimeMs() const
{
    if (mPinnedViewLeftMs) return *mPinnedViewLeftMs;

    const auto period = duration_cast<milliseconds>(mCandlePeriod);
    if (period.count() <= 0 || mCandleWidthPixels == 0)
        return static_cast<int64_t>(mSceneFloor.time_ms);

    const double ms_per_px     = static_cast<double>(period.count()) / static_cast<double>(mCandleWidthPixels);
    // system_clock (Unix ms, leap-second-free) so the live view edge shares the wire-trade /
    // buoy time frame. utc_clock::now() would sit ~27 s ahead, parking the newest buoy that far
    // left of the right edge and skewing the visible-buoy span PriceAutoscale scans.
    const auto   now_ms        = duration_cast<milliseconds>(std::chrono::system_clock::now().time_since_epoch());
    const int    right_pad     = std::max(0, mRightPadPx);
    const int    now_anchor_px = std::max(0, mInnerDataRect.width() - right_pad);
    const auto   time_at_left  = now_ms - milliseconds{static_cast<int64_t>(static_cast<double>(now_anchor_px) * ms_per_px)};
    return time_at_left.count();
}

void InstrumentPanel::EnsureViewAnchor()
{
    // Live-edge right inset, recomputed every DoUpdate from the FINAL inner rect: by this phase
    // every ruler has reserved its strip in CalculateSize, so mInnerDataRect.width() is the true
    // data width. The previous code latched this once; a worker Update() landing before the first
    // real layout() captured a zero pad against a 0×0 canvas and never refreshed it, pinning the
    // live edge onto the price-ruler boundary.
    //
    // The inset is at least one candle width because the active buoy spans the current period and
    // reaches a full candle into the FUTURE (right of "now"); a smaller inset pushes its right half
    // behind the price ruler. The 5%-of-width term adds breathing room at the live edge on wider
    // panels, where it dominates the candle-width floor.
    const int inner_w = std::max(0, mInnerDataRect.width());
    mRightPadPx = std::max<int>(static_cast<int>(mCandleWidthPixels), inner_w * 5 / 100);
}

void InstrumentPanel::ApplyOuterSceneTransforms()
{
    const float w = static_cast<float>(std::max(0, mCanvasWidth));
    const float h = static_cast<float>(std::max(0, mCanvasHeight));

    tvg_ptr<tvg::Shape> hud_clip{tvg::Shape::gen()};
    hud_clip->appendRect(0.0f, 0.0f, w, h);
    mHudScene->clip(hud_clip.get());

    // HUD applies Y-flip about canvas_h: HUD-local (x, y_hud) → canvas (x, h - y_hud).
    // Children of mHudScene therefore work in HUD-Y-up coordinates (y=0 at canvas bottom).
    mHudScene->transform(tvg::Matrix{1.0f, 0.0f, 0.0f,
                                     0.0f, -1.0f, h,
                                     0.0f, 0.0f, 1.0f});
}

void InstrumentPanel::ApplyLogicalSceneTransform()
{
    const float outer_h = static_cast<float>(std::max(0, mCanvasHeight));
    const float inner_left = static_cast<float>(mInnerDataRect.left);
    const float inner_bottom = static_cast<float>(mInnerDataRect.bottom);

    // LogicalScene input X is (t - floor) in float ms; output X = e11*X + e13.
    // For pixel-at-view-left to land at inner_left, e13 absorbs the (view_left - floor)
    // offset so the floor pixel itself sits off-canvas to the left:
    //   inner_left = e11*(view_left - floor) + e13  ⇒  e13 = inner_left - e11*(view_left - floor).
    // e23 maps logical Y=0 (price=floor) onto HUD-y = (canvas_h - inner_bottom) so the floor
    // price renders at the bottom of the inner data rect after HUD's Y-flip carries it down.
    const tvg::Matrix cur = mLogicalScene->transform();
    const float view_offset_ms = static_cast<float>(ViewLeftTimeMs() - static_cast<int64_t>(mSceneFloor.time_ms));
    const float e13 = inner_left - cur.e11 * view_offset_ms;

    mLogicalScene->transform(tvg::Matrix{cur.e11, 0.0f,    e13,
                                          0.0f,    cur.e22, outer_h - inner_bottom,
                                          0.0f,    0.0f,    1.0f});

    // Inner-rect clip on LogicalScene is processed in the parent (HUD) coordinate space,
    // not LogicalScene's local space — see tvgPaint.cpp Paint::Impl::update which calls
    // pclip->update(renderer, pm, ...) with the parent matrix, NOT pm * tr.m. Convert the
    // canvas-pixel inner rect to HUD-Y-up: bottom-left at (left, canvas_h - bottom).
    const int w = std::max(0, mInnerDataRect.width());
    const int h = std::max(0, mInnerDataRect.height());
    if (w > 0 && h > 0) {
        tvg_ptr<tvg::Shape> logical_clip{tvg::Shape::gen()};
        logical_clip->appendRect(static_cast<float>(mInnerDataRect.left),
                                 outer_h - static_cast<float>(mInnerDataRect.bottom),
                                 static_cast<float>(w), static_cast<float>(h));
        mLogicalScene->clip(logical_clip.get());
    }
}

void InstrumentPanel::AllocatePixelBuffer(int width, int height)
{
    // Runs on the UI thread (Cycfi size-allocate, or directly from tests). Take
    // mDataMutex blockingly to serialise against any in-flight worker Update() and
    // satisfy DoUpdate's precondition.
    std::lock_guard lock(mDataMutex);

    // Allocate the new buffer first, bind it as the canvas target, THEN move-assign
    // onto mPixels. The rebind happens BEFORE mPixels' old storage is freed by the
    // move, so the canvas never holds a pointer into deallocated memory. After the
    // move, mPixels owns the storage the canvas now points at.
    std::vector<uint32_t> new_pixels(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    mCanvas->target(new_pixels.data(),
                    static_cast<uint32_t>(width),   // tight ARGB8888 stride in pixels
                    static_cast<uint32_t>(width),
                    static_cast<uint32_t>(height),
                    tvg::ColorSpace::ARGB8888);
    mPixels = std::move(new_pixels);

    mCanvasWidth = width;
    mCanvasHeight = height;
    DoUpdate();
}

void InstrumentPanel::Update()
{
    // Circuit B: blocking lock for worker threads. The element-layer override
    // follows up with Refresh() to schedule the UI redraw.
    std::lock_guard lock(mDataMutex);
    DoUpdate();
}

void InstrumentPanel::OnUpdate()
{
    // Circuit A: UI-thread paint hook. Two cheap gates before we commit to layout work:
    //   1) Throttle. If the scene was DoUpdate'd within the throttle window, paints
    //      arriving in a tight burst skip recomputation entirely.
    //   2) try_lock. If a worker is mid-DoUpdate we don't stall the paint thread; we
    //      proceed with the previously-published scene state and Render() picks up
    //      whatever the worker commits when it releases the lock.
    const int64_t now = MonotonicNs();
    if (mUpdateThrottleNs > 0 &&
        (now - mLastUpdateNs.load(std::memory_order_acquire)) < mUpdateThrottleNs)
        return;

    std::unique_lock lock(mDataMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;

    DoUpdate();
}

void InstrumentPanel::DoUpdate()
{
    // PRECONDITION: mDataMutex held. Each ruler's CalculateSize subtracts its reserved
    // strip from the inner rect, so the rect must be reseeded to the full canvas at
    // every DoUpdate — otherwise repeated heartbeat ticks keep shrinking it and the
    // layout collapses toward the upper-left corner.
    mInnerDataRect = PixelRect{0, 0, mCanvasWidth, mCanvasHeight};

    ApplyOuterSceneTransforms();

    {
        std::shared_lock lock(mScratcherMutex);
        for (const auto& s : mScratchers) s->CalculateSize(*this);
    }

    EnsureViewAnchor();
    ApplyLogicalSceneTransform();

    {
        std::shared_lock lock(mScratcherMutex);
        for (const auto& s : mScratchers) s->OnLayout(*this);
    }

    // Resize forces a full redraw: pre-bounds from before the resize are now meaningless
    // (the canvas target itself may have been retargeted), so the damage-tracking path
    // would produce a stale frame. The next Render() will clear and redraw everything.
    mLayoutDirty = true;
    mDirtyPaints.clear();

    mLastUpdateNs.store(MonotonicNs(), std::memory_order_release);
}

PixelRect InstrumentPanel::Render()
{
    // Whole-Render lock. ThorVG's draw()/sync() are not guaranteed to read solely from
    // the command buffer built by update() — empirically a worker mutating paints between
    // unlock and sync() leaves first-frame artefacts (a gray strip along the canvas edge
    // that disappears once the next worker tick forces a full redraw). Holding mDataMutex
    // through sync() costs at most one frame's rasterisation latency to off-UI Update()
    // callers, which is comfortably absorbed by the 100 ms coUpdate cadence.
    std::lock_guard lock(mDataMutex);

        if (mLayoutDirty) {
            // Reset viewport to full canvas — a previous incremental render may have
            // narrowed it, and ThorVG keeps the last-set viewport across draw cycles
            // unless the target is reset. Call sequence is fixed: viewport must
            // precede update; see Phase 0 findings in time_ruler_partial_redraw.md.
            mCanvas->viewport(0, 0, mCanvasWidth, mCanvasHeight);
            mCanvas->update();
            mLayoutDirty = false;
            mDirtyPaints.clear();

            mCanvas->draw(true);
            mCanvas->sync();
            return PixelRect{0, 0, mCanvasWidth, mCanvasHeight};
        }

        if (mDirtyPaints.empty()) return PixelRect{};

            // Damage union from captured pre-bounds. Inflate-to-pixel-grid keeps every
            // dirtied sub-pixel covered: floor() the upper-left, ceil() the lower-right,
            // then clamp to the canvas extent so the viewport call cannot reject an
            // out-of-range rect.
            float min_x =  std::numeric_limits<float>::infinity();
            float min_y =  std::numeric_limits<float>::infinity();
            float max_x = -std::numeric_limits<float>::infinity();
            float max_y = -std::numeric_limits<float>::infinity();
            for (const auto& d : mDirtyPaints) {
                min_x = std::min(min_x, d.x);
                min_y = std::min(min_y, d.y);
                max_x = std::max(max_x, d.x + d.w);
                max_y = std::max(max_y, d.y + d.h);
            }

            const int x = std::max(0, static_cast<int>(std::floor(min_x)));
            const int y = std::max(0, static_cast<int>(std::floor(min_y)));
            const int r = std::min(mCanvasWidth,  static_cast<int>(std::ceil(max_x)));
            const int b = std::min(mCanvasHeight, static_cast<int>(std::ceil(max_y)));

            PixelRect dmg{x, y, r, b};
    mDirtyPaints.clear();
    if (dmg.empty()) return PixelRect{};

    mCanvas->viewport(dmg.left, dmg.top, dmg.width(), dmg.height());
    mCanvas->update();
    mCanvas->draw(false);
    mCanvas->sync();
    return dmg;
}

void InstrumentPanel::AddScratcher(std::shared_ptr<Scratcher> scratcher)
{
    std::unique_lock lock(mScratcherMutex);
    auto& slot = mScratchers.emplace_back(std::move(scratcher));
    slot->OnAttach(*this);
}

const std::string& InstrumentPanel::DefaultFontName() const
{
    return kDefaultFontName;
}

void InstrumentPanel::SetInstrument(bybit::InstrumentInfo info)
{
    mInstrument = std::move(info);
    // tickSize / basePrecision arrive already parsed as currency, so the fractional
    // scale is just the value's own decimal count — no string scan needed.
    mPriceDecimals = mInstrument.tickSize.decimals();
    // Volume scale comes from basePrecision (e.g. "0.000001" → 6 decimals for BTC).
    // Fallback to 8 (typical max precision) when basePrecision is unset so the
    // currency rescale doesn't truncate sub-cent fractional sizes.
    const std::size_t base_decimals = mInstrument.basePrecision.decimals();
    mSizeDecimals = base_decimals > 0 ? base_decimals : 8;
}

void InstrumentPanel::OnPublicTrades(datahub::update_kind kind,
                                    IDataController::public_trades_feed_type::const_iterator first,
                                    IDataController::public_trades_feed_type::const_iterator last)
{
    // Data path entry point (data/worker thread): take the data mutex — the same lock DoUpdate
    // and Render hold — so the series mutation is serialised against the UI-thread readers, then
    // hand the feed's native PublicTrade subrange [first,last) straight to the quote scratcher
    // for ingestion + price autoscale (no copy; the scratcher reads price/size as currency and
    // only converts to scene points at the ThorVG boundary). Geometry re-emission and the UI
    // redraw ride the next 25 ms heartbeat, so nothing is posted here.
    std::lock_guard lock(mDataMutex);
    if (mQuoteScratcher)
        mQuoteScratcher->IngestAndScale(*this, kind, std::ranges::subrange(first, last));
}

} // namespace scratcher::cockpit
