// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#include "instrument_panel.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
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

tvg_ptr<tvg::Scene> MakeRetainedScene()
{
    tvg_ptr<tvg::Scene> scene{tvg::Scene::gen()};
    scene->ref();
    return scene;
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

InstrumentContentPanel::InstrumentContentPanel(PanelType type, std::chrono::seconds candle_period, uint32_t candle_width_pixels)
    : ContentPanel(type)
    , mRuntime{}
    , mCanvas{tvg::SwCanvas::gen()}
    , mRootScene{MakeRetainedScene()}
    , mUiScene{MakeRetainedScene()}
    , mLogicalCanvasScene{MakeRetainedScene()}
    , mLogicalScene{MakeRetainedScene()}
    , mCandlePeriod{candle_period}
    , mCandleWidthPixels{candle_width_pixels}
{
    LoadDefaultFont();

    mLogicalCanvasScene->add(mLogicalScene.get());
    mRootScene->add(mLogicalCanvasScene.get());
    mRootScene->add(mUiScene.get());
    mCanvas->add(mRootScene.get());

    if (Type() == PanelType::MarketGraph) {
        AddScratcher(std::make_shared<TimeRuler>());
        AddScratcher(std::make_shared<PriceRuler>());
        mQuoteScratcher = std::make_shared<QuoteScratcher>(std::chrono::milliseconds(mCandlePeriod));
        AddScratcher(mQuoteScratcher);
    }

    const uint64_t period_ms = static_cast<uint64_t>(mCandlePeriod.count()) * 1000ull;
    const uint64_t now_ms = get_timestamp(std::chrono::utc_clock::now());
    mSceneFloor.time_ms = period_ms > 0 ? (now_ms / period_ms) * period_ms : now_ms;

    const float px_per_ms = period_ms > 0
        ? static_cast<float>(mCandleWidthPixels) / static_cast<float>(period_ms)
        : 0.0f;
    mLogicalScene->transform(tvg::Matrix{px_per_ms, 0.0f, 0.0f,
                                          0.0f,      0.0f, 0.0f,
                                          0.0f,      0.0f, 1.0f});
}

InstrumentContentPanel::~InstrumentContentPanel() = default;

void InstrumentContentPanel::SetSceneFloor(SceneFloor floor)
{
    mSceneFloor = floor;
}

void InstrumentContentPanel::ApplyOuterSceneTransforms()
{
    const float w = static_cast<float>(std::max(0, mCanvasWidth));
    const float h = static_cast<float>(std::max(0, mCanvasHeight));

    auto* root_clip = tvg::Shape::gen();
    root_clip->appendRect(0.0f, 0.0f, w, h);
    mRootScene->clip(root_clip);

    const tvg::Matrix identity{1.0f, 0.0f, 0.0f,
                               0.0f, 1.0f, 0.0f,
                               0.0f, 0.0f, 1.0f};
    mRootScene->transform(identity);
    mUiScene->transform(identity);

    mLogicalCanvasScene->transform(tvg::Matrix{1.0f, 0.0f, 0.0f,
                                                0.0f, -1.0f, h,
                                                0.0f, 0.0f, 1.0f});
}

void InstrumentContentPanel::ApplyLogicalSceneTransform()
{
    const float outer_h = static_cast<float>(std::max(0, mCanvasHeight));
    const float inner_left = static_cast<float>(mInnerDataRect.left);
    const float inner_bottom = static_cast<float>(mInnerDataRect.bottom);

    const tvg::Matrix cur = mLogicalScene->transform();
    mLogicalScene->transform(tvg::Matrix{cur.e11, 0.0f,    inner_left,
                                          0.0f,    cur.e22, outer_h - inner_bottom,
                                          0.0f,    0.0f,    1.0f});
}

void InstrumentContentPanel::SetTarget(uint32_t* buffer, uint32_t stride, uint32_t width, uint32_t height)
{
    mCanvas->target(buffer, stride, width, height, tvg::ColorSpace::ARGB8888);
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

    ApplyLogicalSceneTransform();

    mUiScene->remove();
    mLogicalScene->remove();

    {
        std::shared_lock lock(mScratcherMutex);
        for (const auto& s : mScratchers) s->EmitChanges(*this);
    }
}

void InstrumentContentPanel::Render()
{
    mCanvas->update();
    mCanvas->draw(true);
    mCanvas->sync();
}

void InstrumentContentPanel::AddScratcher(std::shared_ptr<Scratcher> scratcher)
{
    std::unique_lock lock(mScratcherMutex);
    mScratchers.emplace_back(std::move(scratcher));
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