// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "vector_scene_panel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>

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

const std::string kDefaultFontName = "OpenSans";

std::filesystem::path ExecutableDirectory()
{
    std::error_code ec;
    auto exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path{} : exe.parent_path();
}

bool LoadDefaultFont()
{
    static bool attempted = false;
    static bool loaded = false;
    if (attempted) return loaded;
    attempted = true;

    auto path = VectorScenePanel::FindResource("OpenSans-Regular.ttf");
    if (path.empty()) path = "/usr/share/fonts/truetype/open-sans/OpenSans-Regular.ttf";
    if (!std::filesystem::exists(path)) return false;

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

VectorScenePanel::ThorvgRuntimeRef::ThorvgRuntimeRef()
{
    ThorvgRuntime::Instance().Acquire();
}

VectorScenePanel::ThorvgRuntimeRef::~ThorvgRuntimeRef()
{
    ThorvgRuntime::Instance().Release();
}

std::filesystem::path VectorScenePanel::FindResource(const std::string& file_name)
{
    namespace fs = std::filesystem;
    const std::array<fs::path, 3> candidates = {
        fs::path{"resources"} / file_name,
        fs::path{file_name},
        ExecutableDirectory() / "resources" / file_name,
    };
    for (const auto& candidate : candidates) {
        if (!candidate.empty() && fs::exists(candidate)) return candidate;
    }
    return {};
}

VectorScenePanel::VectorScenePanel(PanelType type)
    : ContentPanel(type)
    , mRuntime{}
    , mCanvas{tvg::SwCanvas::gen()}
    , mHudScene{tvg::Scene::gen()}      // gen() returns refCnt=0; tvg_ptr ctor calls ref() → 1, Canvas::add ref()s → 2
{
    LoadDefaultFont();
    mCanvas->add(mHudScene.get());
}

VectorScenePanel::~VectorScenePanel() = default;

void VectorScenePanel::MarkDirty(tvg::Paint* paint)
{
    if (!paint || mLayoutDirty) return;

    DirtyEntry e{tvg_ptr<tvg::Paint>{paint}};
    if (paint->bounds(&e.x, &e.y, &e.w, &e.h) != tvg::Result::Success) {
        mLayoutDirty = true;
        mDirtyPaints.clear();
        return;
    }
    mDirtyPaints.push_back(std::move(e));
}

void VectorScenePanel::ApplyOuterSceneTransforms()
{
    const float w = static_cast<float>(mCanvasWidth);
    const float h = static_cast<float>(mCanvasHeight);

    tvg_ptr<tvg::Shape> hud_clip{tvg::Shape::gen()};
    hud_clip->appendRect(0.0f, 0.0f, w, h);
    mHudScene->clip(hud_clip.get());

    // HUD-local (x, y_hud) → canvas (x, h - y_hud): children work in HUD-Y-up coordinates.
    mHudScene->transform(tvg::Matrix{1.0f, 0.0f, 0.0f,
                                     0.0f, -1.0f, h,
                                     0.0f, 0.0f, 1.0f});
}

void VectorScenePanel::AllocatePixelBuffer(uint32_t width, uint32_t height)
{
    std::lock_guard lock(mDataMutex);

    // Bind the canvas to the new storage BEFORE the move frees the old buffer.
    std::vector<uint32_t> new_pixels(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
    mCanvas->target(new_pixels.data(), width, width, height, tvg::ColorSpace::ARGB8888);
    mPixels = std::move(new_pixels);

    mCanvasWidth = width;
    mCanvasHeight = height;
    mLayoutDirty = true;
    mDirtyPaints.clear();
    DoUpdate();
}

void VectorScenePanel::Update()
{
    std::lock_guard lock(mDataMutex);
    DoUpdate();
}

void VectorScenePanel::OnUpdate()
{
    const int64_t now = MonotonicNs();
    if (mUpdateThrottleNs > 0 &&
        (now - mLastUpdateNs.load(std::memory_order_acquire)) < mUpdateThrottleNs)
        return;

    std::unique_lock lock(mDataMutex, std::try_to_lock);
    if (!lock.owns_lock()) return;

    DoUpdate();
}

void VectorScenePanel::DoUpdate()
{
    ApplyOuterSceneTransforms();

    if (DoLayout()) {
        mLayoutDirty = true;
        mDirtyPaints.clear();
    }

    mLastUpdateNs.store(MonotonicNs(), std::memory_order_release);
}

Rectangle VectorScenePanel::Render()
{
    // Whole-Render lock: ThorVG's draw()/sync() are not guaranteed to read solely from the command
    // buffer built by update(); a worker mutating paints between unlock and sync() leaves
    // first-frame artefacts.
    std::lock_guard lock(mDataMutex);

    if (mLayoutDirty) {
        // Reset viewport to full canvas — a previous incremental render may have narrowed it, and
        // ThorVG keeps the last-set viewport across draw cycles unless the target is reset. Viewport
        // must precede update; see Phase 0 findings in time_ruler_partial_redraw.md.
        mCanvas->viewport(0, 0, mCanvasWidth, mCanvasHeight);
        mCanvas->update();
        mLayoutDirty = false;
        mDirtyPaints.clear();

        mCanvas->draw(true);
        mCanvas->sync();
        return Rectangle{0, 0, mCanvasWidth, mCanvasHeight};
    }

    if (mDirtyPaints.empty()) return Rectangle{};

    // Damage union from captured pre-bounds, inflated to the pixel grid and clamped to the canvas.
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
    const int r = std::min(static_cast<int>(mCanvasWidth),  static_cast<int>(std::ceil(max_x)));
    const int b = std::min(static_cast<int>(mCanvasHeight), static_cast<int>(std::ceil(max_y)));

    mDirtyPaints.clear();
    if (r <= x || b <= y) return Rectangle{};

    const Rectangle dmg{x, y, r - x, b - y};
    mCanvas->viewport(static_cast<int32_t>(dmg.x), static_cast<int32_t>(dmg.y), static_cast<int32_t>(dmg.w), static_cast<int32_t>(dmg.h));
    mCanvas->update();
    mCanvas->draw(false);
    mCanvas->sync();
    return dmg;
}

const std::string& VectorScenePanel::DefaultFontName() const
{
    return kDefaultFontName;
}

} // namespace scratcher::cockpit
