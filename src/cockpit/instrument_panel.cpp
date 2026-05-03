// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#include "instrument_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>

#include "scratchers/price_ruler.hpp"
#include "scratchers/time_ruler.hpp"
#include "timedef.hpp"

namespace scratcher::cockpit {

namespace {

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

constexpr const char* kDefaultFontName = "OpenSans";

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

    auto result = tvg::Text::load(kDefaultFontName, buffer.data(), static_cast<uint32_t>(buffer.size()), "ttf", false);
    loaded = (result == tvg::Result::Success);
    return loaded;
}

}

InstrumentContentPanel::ThorvgRuntimeRef::ThorvgRuntimeRef()
{
    ThorvgRuntime::Instance().Acquire();
}

InstrumentContentPanel::ThorvgRuntimeRef::~ThorvgRuntimeRef()
{
    ThorvgRuntime::Instance().Release();
}

InstrumentContentPanel::InstrumentContentPanel(PanelType type, seconds candle_period, uint32_t candle_width_pixels)
    : ContentPanel(type)
    , mRuntime{}
    , mCanvas{tvg::SwCanvas::gen()}
    , mHudScene{tvg::Scene::gen()}              // tvg_ptr ctor bumps ref → refCnt = 2 (gen + wrapper);
    , mLogicalScene{tvg::Scene::gen()}          // when added to a parent, parent claims one slot,
    , mCandlePeriod{candle_period}              // wrapper keeps the other until ~tvg_ptr.
    , mCandleWidthPixels{candle_width_pixels}
{
    LoadDefaultFont();

    mHudScene->add(mLogicalScene.get());
    mCanvas->add(mHudScene.get());

    if (Type() == PanelType::MarketGraph) {
        AddScratcher(std::make_shared<TimeRuler>());
        AddScratcher(std::make_shared<PriceRuler>());
        mQuoteScratcher = std::make_shared<QuoteScratcher>(milliseconds(mCandlePeriod));
        AddScratcher(mQuoteScratcher);
    }

    using namespace std::chrono;
    const auto period = duration_cast<milliseconds>(mCandlePeriod);

    // Snap the precision floor to one calendar year before today (UTC). A fixed
    // 365-day step drifts ~5h45m/year on average vs. the Gregorian calendar, so
    // we walk the calendar with year_month_day; a Feb 29 source folds to Feb 28
    // of the prior year via the year_month_day_last fallback.
    const auto today = floor<days>(utc_clock::to_sys(utc_clock::now()));
    auto floor_ymd = year_month_day{today} - years{1};
    if (!floor_ymd.ok())
        floor_ymd = floor_ymd.year() / floor_ymd.month() / last;
    const auto floor_unaligned = duration_cast<milliseconds>(utc_clock::from_sys(sys_days{floor_ymd}).time_since_epoch());
    const auto floor_aligned = period.count() > 0 ? floor_unaligned - (floor_unaligned % period) : floor_unaligned;

    mSceneFloor.time_ms = static_cast<uint64_t>(floor_aligned.count());

    const float px_per_ms = period.count() > 0 ? static_cast<float>(mCandleWidthPixels) / static_cast<float>(period.count()) : 0.0f;
    mLogicalScene->transform(tvg::Matrix{px_per_ms, 0.0f, 0.0f,
                                          0.0f,      0.0f, 0.0f,
                                          0.0f,      0.0f, 1.0f});
}

InstrumentContentPanel::~InstrumentContentPanel()
{
    // Detach scratchers in reverse-attach order so their dependents (e.g. quote scratcher
    // depending on rulers' inner-rect work) tear down before their dependencies.
    std::unique_lock lock(mScratcherMutex);
    for (auto it = mScratchers.rbegin(); it != mScratchers.rend(); ++it) {
        (*it)->OnDetach(*this);
    }
    mScratchers.clear();
}

void InstrumentContentPanel::SetSceneFloor(SceneFloor floor)
{
    mSceneFloor = floor;
    // Floor change shifts logical-scene offset, which is a view change — trigger any
    // observers (TimeRuler in Phase 3) so they re-anchor their tick positions.
    for (auto& [id, sub] : mSubscriptions) {
        if (sub.on_view) sub.on_view();
    }
}

void InstrumentContentPanel::SetViewLeftTimeMs(std::optional<int64_t> t_ms)
{
    mPinnedViewLeftMs = t_ms;
    for (auto& [id, sub] : mSubscriptions) {
        if (sub.on_view) sub.on_view();
    }
}

InstrumentContentPanel::SubscriptionId InstrumentContentPanel::SubscribeSize(SizeCallback cb)
{
    const SubscriptionId id = mNextSubscriptionId++;
    mSubscriptions[id].on_size = std::move(cb);
    return id;
}

InstrumentContentPanel::SubscriptionId InstrumentContentPanel::SubscribeView(ViewCallback cb)
{
    const SubscriptionId id = mNextSubscriptionId++;
    mSubscriptions[id].on_view = std::move(cb);
    return id;
}

void InstrumentContentPanel::Unsubscribe(SubscriptionId id)
{
    mSubscriptions.erase(id);
}

void InstrumentContentPanel::MarkDirty(tvg::Paint* paint)
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

float InstrumentContentPanel::HudXOfTime(int64_t time_ms) const
{
    // mLogicalScene's matrix is the live source of truth: e11 carries scale (px / ms)
    // and e13 carries the (inner_left - e11 * (view_left - floor)) offset. Reading from
    // it preserves the single-source-of-truth invariant — no parallel cached scale.
    const tvg::Matrix m = mLogicalScene->transform();
    const float t_offset = static_cast<float>(time_ms - static_cast<int64_t>(mSceneFloor.time_ms));
    return m.e11 * t_offset + m.e13;
}

int64_t InstrumentContentPanel::TimeOfHudX(float hud_x) const
{
    const tvg::Matrix m = mLogicalScene->transform();
    if (m.e11 == 0.0f) return static_cast<int64_t>(mSceneFloor.time_ms);
    const float t_offset = (hud_x - m.e13) / m.e11;
    return static_cast<int64_t>(mSceneFloor.time_ms) + static_cast<int64_t>(std::llround(t_offset));
}

int64_t InstrumentContentPanel::ViewLeftTimeMs() const
{
    if (mPinnedViewLeftMs) return *mPinnedViewLeftMs;

    const auto period = duration_cast<milliseconds>(mCandlePeriod);
    if (period.count() <= 0 || mCandleWidthPixels == 0)
        return static_cast<int64_t>(mSceneFloor.time_ms);

    const double ms_per_px     = static_cast<double>(period.count()) / static_cast<double>(mCandleWidthPixels);
    const auto   now_ms        = duration_cast<milliseconds>(utc_clock::now().time_since_epoch());
    const int    right_pad     = std::max(0, mRightPadPx);
    const int    now_anchor_px = std::max(0, mInnerDataRect.width() - right_pad);
    const auto   time_at_left  = now_ms - milliseconds{static_cast<int64_t>(static_cast<double>(now_anchor_px) * ms_per_px)};
    const auto   snapped       = time_at_left - (time_at_left % period);
    return snapped.count();
}

void InstrumentContentPanel::EnsureViewAnchor()
{
    if (mRightPadPx >= 0) return;
    mRightPadPx = std::max(0, mInnerDataRect.width() * 5 / 100);
}

void InstrumentContentPanel::ApplyOuterSceneTransforms()
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

void InstrumentContentPanel::ApplyLogicalSceneTransform()
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

void InstrumentContentPanel::SetTarget(std::span<uint32_t> buffer, uint32_t stride, uint32_t width, uint32_t height)
{
    mCanvas->target(buffer.data(), stride, width, height, tvg::ColorSpace::ARGB8888);
}

void InstrumentContentPanel::OnSize(int width, int height)
{
    mCanvasWidth = width;
    mCanvasHeight = height;
    mInnerDataRect = PixelRect{0, 0, width, height};

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

    for (auto& [id, sub] : mSubscriptions) {
        if (sub.on_size) sub.on_size();
    }

    // Resize forces a full redraw: pre-bounds from before the resize are now meaningless
    // (the canvas target itself may have been retargeted), so the damage-tracking path
    // would produce a stale frame. The next Render() will clear and redraw everything.
    mLayoutDirty = true;
    mDirtyPaints.clear();
}

PixelRect InstrumentContentPanel::Render()
{
    if (mLayoutDirty) {
        // Reset viewport to full canvas — a previous incremental render may have narrowed
        // it, and ThorVG keeps the last-set viewport across draw cycles unless the target
        // is reset. Call sequence is fixed: viewport must precede update; see Phase 0
        // findings in time_ruler_partial_redraw.md.
        mCanvas->viewport(0, 0, mCanvasWidth, mCanvasHeight);
        mCanvas->update();
        mCanvas->draw(true);
        mCanvas->sync();
        mLayoutDirty = false;
        mDirtyPaints.clear();
        return PixelRect{0, 0, mCanvasWidth, mCanvasHeight};
    }

    if (mDirtyPaints.empty()) return PixelRect{};

    // Damage union from captured pre-bounds. Inflate-to-pixel-grid keeps every dirtied
    // sub-pixel covered: floor() the upper-left, ceil() the lower-right, then clamp to
    // the canvas extent so the viewport call cannot reject an out-of-range rect.
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

void InstrumentContentPanel::AddScratcher(std::shared_ptr<Scratcher> scratcher)
{
    std::unique_lock lock(mScratcherMutex);
    auto& slot = mScratchers.emplace_back(std::move(scratcher));
    slot->OnAttach(*this);
}

const char* InstrumentContentPanel::DefaultFontName() const
{
    return kDefaultFontName;
}

void InstrumentContentPanel::SetSymbol(std::string symbol)
{
    mSymbol = std::move(symbol);
    OnSymbolChanged(mSymbol);
}

void InstrumentContentPanel::SetInstrumentList(std::vector<std::string> symbols)
{
    mInstrumentList = std::move(symbols);
    OnInstrumentListChanged(mInstrumentList);
}

void InstrumentContentPanel::SetInstrumentInfo(std::optional<bybit::InstrumentInfo> info)
{
    if (info) mSymbol = info->symbol;
    mInstrument = std::move(info);
    OnInstrumentInfoChanged(mInstrument);
}

void InstrumentContentPanel::EmitUserSymbolSelection(std::string symbol)
{
    if (mOnUserSymbolSelection)
        mOnUserSymbolSelection(std::move(symbol));
}

} // namespace scratcher::cockpit
