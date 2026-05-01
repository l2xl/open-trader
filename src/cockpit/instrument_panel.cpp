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

InstrumentContentPanel::InstrumentContentPanel(PanelType type, std::weak_ptr<IDataController> controller)
    : ContentPanel(type)
    , mController(std::move(controller))
{
    ThorvgRuntime::Instance().Acquire();
    LoadDefaultFont();

    mCanvas = tvg::SwCanvas::gen();
    BuildSceneTree();
    InitScratchers();
}

InstrumentContentPanel::~InstrumentContentPanel()
{
    if (mCanvas) {
        delete mCanvas;
    }
    ThorvgRuntime::Instance().Release();
}

void InstrumentContentPanel::BuildSceneTree()
{
    mRootScene = tvg::Scene::gen();
    mUiScene = tvg::Scene::gen();
    mLogicalCanvasScene = tvg::Scene::gen();
    mLogicalScene = tvg::Scene::gen();

    mLogicalCanvasScene->add(mLogicalScene);
    mRootScene->add(mLogicalCanvasScene);
    mRootScene->add(mUiScene);

    mCanvas->add(mRootScene);
}

void InstrumentContentPanel::ApplySceneTransforms()
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
    mLogicalScene->transform(identity);

    mLogicalCanvasScene->transform(tvg::Matrix{1.0f, 0.0f, 0.0f,
                                                0.0f, -1.0f, h,
                                                0.0f, 0.0f, 1.0f});
}

void InstrumentContentPanel::SetTarget(uint32_t* buffer, uint32_t stride, uint32_t width, uint32_t height)
{
    if (!mCanvas) return;
    mCanvas->target(buffer, stride, width, height, tvg::ColorSpace::ARGB8888);
}

void InstrumentContentPanel::OnSize(int width, int height)
{
    mCanvasWidth = width;
    mCanvasHeight = height;
    mClientRect = PixelRect{0, 0, width, height};

    ApplySceneTransforms();

    {
        std::shared_lock lock(mScratcherMutex);
        for (const auto& s : mScratchers) s->CalculateSize(*this);
    }

    if (mUiScene) mUiScene->remove();
    if (mLogicalScene) mLogicalScene->remove();

    {
        std::shared_lock lock(mScratcherMutex);
        for (const auto& s : mScratchers) s->EmitChanges(*this);
    }
}

void InstrumentContentPanel::Render()
{
    if (!mCanvas) return;
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

void InstrumentContentPanel::InitScratchers()
{
    if (Type() == PanelType::MarketGraph) {
        AddScratcher(std::make_shared<TimeRuler>());
        AddScratcher(std::make_shared<PriceRuler>());
    }
}

void InstrumentContentPanel::InitInstrumentSubscription(std::weak_ptr<InstrumentContentPanel> self)
{
    mListSub = datahub::make_data_subscription<std::deque<bybit::InstrumentInfo>>(
        [self](auto /*update*/) {
            auto p = self.lock();
            if (!p) return;
            p->PostToUi([self]() {
                auto sp = self.lock();
                if (!sp) return;
                auto ctl = sp->mController.lock();
                if (!ctl) return;
                const auto& snapshot = ctl->getInstrumentsFeed().get_snapshot();
                std::vector<std::string> symbols;
                symbols.reserve(snapshot.size());
                for (const auto& inst : snapshot)
                    symbols.emplace_back(inst.symbol);
                sp->OnInstrumentsReady(std::move(symbols));
            });
        });

    if (auto ctl = mController.lock())
        ctl->SubscribeInstrumentList(mListSub);
}

void InstrumentContentPanel::ResolveInstrument()
{
    mInstrument.reset();
    auto ctl = mController.lock();
    if (!ctl) return;
    const auto& snapshot = ctl->getInstrumentsFeed().get_snapshot();
    for (const auto& info : snapshot) {
        if (info.symbol == mSymbol) {
            mInstrument = info;
            return;
        }
    }
}

void InstrumentContentPanel::SelectSymbol(std::string symbol)
{
    mSymbol = std::move(symbol);
    ResolveInstrument();
    OnSymbolSelected(mSymbol);
}

} // namespace scratcher::cockpit
