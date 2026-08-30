// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "wallet_panel.hpp"

#include <algorithm>
#include <concepts>
#include <format>
#include <functional>
#include <mutex>
#include <optional>
#include <ranges>
#include <string_view>

namespace scratcher::cockpit {

namespace {

constexpr float kTemplateWidth = 400.0f;
constexpr float kInsetX = 8.0f;
constexpr float kCardTop = 8.0f;
constexpr float kCardHeight = 100.0f;
constexpr float kCardRadius = 6.0f;
constexpr float kRowHeight = 22.0f;
constexpr float kRowPitch = 24.0f;

constexpr std::string_view kMinusSign = "−";
constexpr std::string_view kNoValue = "—";

struct Rgb
{
    uint8_t r, g, b;
};

constexpr Rgb kPositive{46, 204, 113};
constexpr Rgb kNegative{231, 76, 60};
constexpr Rgb kMuted{128, 128, 128};

Rgb SignColour(int sign)
{
    return sign > 0 ? kPositive : sign < 0 ? kNegative : kMuted;
}

std::string GroupThousands(std::string_view digits)
{
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    const size_t head = digits.size() % 3;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && i >= head && (i - head) % 3 == 0) out.push_back(',');
        out.push_back(digits[i]);
    }
    return out;
}

template<std::integral T>
std::string FormatScaled(T scaled, size_t decimals, size_t min_decimals)
{
    bool negative = false;
    uint64_t magnitude = 0;
    if constexpr (std::is_signed_v<T>) {
        negative = scaled < 0;
        magnitude = negative ? static_cast<uint64_t>(-(scaled + 1)) + 1 : static_cast<uint64_t>(scaled);
    }
    else {
        magnitude = static_cast<uint64_t>(scaled);
    }

    std::string digits = std::to_string(magnitude);
    if (digits.size() <= decimals) digits.insert(0, decimals + 1 - digits.size(), '0');
    const std::string integer = digits.substr(0, digits.size() - decimals);
    std::string fraction = digits.substr(digits.size() - decimals);
    while (fraction.size() > min_decimals && fraction.back() == '0') fraction.pop_back();

    std::string out = negative ? std::string{kMinusSign} : std::string{};
    out += GroupThousands(integer);
    if (!fraction.empty()) {
        out += '.';
        out += fraction;
    }
    return out;
}

std::string FormatUsd(const std::optional<currency<uint64_t>>& value)
{
    if (!value) return std::string{kNoValue};
    return FormatScaled(value->raw_at(2), 2, 2);
}

std::string FormatSignedUsd(const std::optional<currency<int64_t>>& value)
{
    if (!value) return std::string{kNoValue};
    std::string out = FormatScaled(value->raw_at(2), 2, 2);
    if (value->raw() > 0) out.insert(0, "+");
    return out;
}

std::string FormatQuantity(const std::optional<currency<uint64_t>>& value)
{
    if (!value) return std::string{kNoValue};
    const size_t decimals = std::min<size_t>(value->decimals(), 8);
    return FormatScaled(value->raw_at(decimals), decimals, 2);
}

int SignOf(const std::optional<currency<int64_t>>& value)
{
    if (!value || value->raw() == 0) return 0;
    return value->raw() < 0 ? -1 : 1;
}

uint64_t UsdCents(const bybit::CoinBalance& coin)
{
    return coin.usdValue ? coin.usdValue->raw_at(2) : 0;
}

std::string_view AccountTypeName(bybit::AccountType type)
{
    switch (type) {
    case bybit::AccountType::UNIFIED:  return "UNIFIED";
    case bybit::AccountType::CONTRACT: return "CONTRACT";
    }
    return "ACCOUNT";
}

void SetFill(tvg::Text& text, Rgb colour)
{
    text.fill(colour.r, colour.g, colour.b);
}

// Shape::reset() drops the path AND the fill/stroke set by the template's CSS, so both are read
// back and re-applied around the new rectangle.
void ResizeRect(tvg::Shape& rect, float x, float y, float w, float h, float radius)
{
    uint8_t r = 0, g = 0, b = 0, a = 0;
    const bool filled = rect.fill(&r, &g, &b, &a) == tvg::Result::Success;
    uint8_t sr = 0, sg = 0, sb = 0, sa = 0;
    const float stroke_width = rect.strokeWidth();
    const bool stroked = stroke_width > 0.0f && rect.strokeFill(&sr, &sg, &sb, &sa) == tvg::Result::Success;

    rect.reset();
    rect.appendRect(x, y, w, h, radius, radius);
    if (filled) rect.fill(r, g, b, a);
    if (stroked) {
        rect.strokeWidth(stroke_width);
        rect.strokeFill(sr, sg, sb, sa);
    }
}

// Template x positions are laid out for kTemplateWidth; texts anchored to the right part of the
// form follow the canvas width proportionally.
void PlaceAtScaledX(tvg::Text& text, float origin_x, float canvas_width)
{
    tvg::Matrix m = text.transform();
    m.e13 = origin_x * canvas_width / kTemplateWidth;
    text.transform(m);
}

}

WalletViewModel MakeWalletViewModel(const bybit::WalletBalance& wallet, time_point received_at)
{
    WalletViewModel model;
    model.status = std::format("{} · {:%H:%M:%S} UTC", AccountTypeName(wallet.accountType), std::chrono::floor<std::chrono::seconds>(received_at));
    model.equity = FormatUsd(wallet.totalEquity);
    model.unrealised_pnl = FormatSignedUsd(wallet.totalPerpUPL);
    model.pnl_sign = SignOf(wallet.totalPerpUPL);
    model.wallet_balance = FormatUsd(wallet.totalWalletBalance);
    model.available = FormatUsd(wallet.totalAvailableBalance);

    std::vector<std::reference_wrapper<const bybit::CoinBalance>> coins(wallet.coin.begin(), wallet.coin.end());
    std::ranges::stable_sort(coins, [](const bybit::CoinBalance& a, const bybit::CoinBalance& b) { return UsdCents(a) > UsdCents(b); });

    model.rows.reserve(coins.size());
    for (const bybit::CoinBalance& coin : coins) {
        model.rows.push_back(WalletViewModel::Row{
            .coin = coin.coin,
            .balance = FormatQuantity(coin.walletBalance),
            .usd_value = FormatUsd(coin.usdValue),
            .unrealised_pnl = FormatSignedUsd(coin.unrealisedPnl),
            .pnl_sign = SignOf(coin.unrealisedPnl),
        });
    }
    return model;
}

WalletViewModel WaitingWalletViewModel()
{
    WalletViewModel model;
    model.status = "Waiting for wallet data";
    model.equity = std::string{kNoValue};
    model.unrealised_pnl = std::string{kNoValue};
    model.wallet_balance = std::string{kNoValue};
    model.available = std::string{kNoValue};
    return model;
}

WalletPanel::WalletPanel()
    : VectorScenePanel(PanelType::Wallet)
    , mTemplate(svg_template::load_file(FindResource("wallet_panel.svg")))
    , mLayer{tvg::Scene::gen()}
    , mBackground(mTemplate.get<tvg::Shape>("bg"))
    , mCard(mTemplate.get<tvg::Shape>("card"))
    , mStatus(mTemplate.get<tvg::Text>("status"))
    , mEquity(mTemplate.get<tvg::Text>("equity"))
    , mUnrealisedPnl(mTemplate.get<tvg::Text>("upl"))
    , mWalletBalance(mTemplate.get<tvg::Text>("wallet_balance"))
    , mAvailable(mTemplate.get<tvg::Text>("available"))
    , mRows(mTemplate.get<tvg::Scene>("rows"))
    , mRowTemplate(mTemplate.get<tvg::Scene>("row_tpl"))
{
    for (const auto id : {"status", "available_label", "available", "h_balance", "h_usd", "h_upl"}) {
        auto text = mTemplate.get<tvg::Text>(id);
        mAnchored.push_back(AnchoredText{text, text->transform().e13});
    }
    for (const auto id : {"status", "h_balance", "h_usd", "h_upl"})
        mTemplate.get<tvg::Text>(id)->align(1.0f, 0.0f);

    for (auto paint : mRowTemplate->paints())
        if (paint->type() == tvg::Type::Text) mRowColumnOrigins.push_back(paint->transform().e13);
    mRowTemplate->visible(false);

    // The template is authored Y-down; HudScene is Y-up, so the layer flips back about canvas_h
    // (refreshed on every resize in DoLayout).
    mLayer->add(mTemplate.root().get());
    HudScene().add(mLayer.get());
}

void WalletPanel::OnWallet(datahub::update_kind /*kind*/, const wallet_feed_type::cache_type& wallets)
{
    auto it = std::ranges::find(wallets, bybit::AccountType::UNIFIED, &bybit::WalletBalance::accountType);
    if (it == wallets.end()) it = wallets.begin();
    if (it == wallets.end()) return;

    auto model = MakeWalletViewModel(*it, std::chrono::system_clock::now());
    {
        std::lock_guard lock(mDataMutex);
        mModel = std::move(model);
        mModelDirty = true;
    }
    Refresh();
}

bool WalletPanel::DoLayout()
{
    const uint32_t w = CanvasWidth();
    const uint32_t h = CanvasHeight();
    if (w == 0 || h == 0) return false;

    const bool resized = w != mLaidOutWidth || h != mLaidOutHeight;
    if (!resized && !mModelDirty) return false;

    const float fw = static_cast<float>(w);
    const float fh = static_cast<float>(h);
    if (resized) {
        mLayer->transform(tvg::Matrix{1.0f, 0.0f, 0.0f,
                                      0.0f, -1.0f, fh,
                                      0.0f, 0.0f, 1.0f});
        ResizeRect(*mBackground, 0.0f, 0.0f, fw, fh, 0.0f);
        ResizeRect(*mCard, kInsetX, kCardTop, fw - 2.0f * kInsetX, kCardHeight, kCardRadius);
        for (auto& anchored : mAnchored) PlaceAtScaledX(*anchored.text, anchored.origin_x, fw);
        mLaidOutWidth = w;
        mLaidOutHeight = h;
    }

    ApplyModel(fw);
    mModelDirty = false;
    return true;
}

void WalletPanel::ApplyModel(float canvas_width)
{
    mStatus->text(mModel.status.c_str());
    mEquity->text(mModel.equity.c_str());
    mUnrealisedPnl->text(mModel.unrealised_pnl.c_str());
    SetFill(*mUnrealisedPnl, SignColour(mModel.pnl_sign));
    mWalletBalance->text(mModel.wallet_balance.c_str());
    mAvailable->text(mModel.available.c_str());
    RebuildRows(canvas_width);
}

void WalletPanel::RebuildRows(float canvas_width)
{
    for (auto& row : mRowClones) mRows->remove(row.get());
    mRowClones.clear();

    float y = 0.0f;
    for (const auto& row : mModel.rows) {
        tvg_ptr<tvg::Scene> clone{static_cast<tvg::Scene*>(mRowTemplate->duplicate())};
        clone->visible(true);
        clone->transform(tvg::Matrix{1.0f, 0.0f, 0.0f,
                                     0.0f, 1.0f, y,
                                     0.0f, 0.0f, 1.0f});

        size_t column = 0;
        for (auto paint : clone->paints()) {
            if (paint->type() == tvg::Type::Shape) {
                ResizeRect(static_cast<tvg::Shape&>(*paint), kInsetX, 0.0f, canvas_width - 2.0f * kInsetX, kRowHeight, 0.0f);
                continue;
            }
            if (paint->type() != tvg::Type::Text) continue;

            auto& text = static_cast<tvg::Text&>(*paint);
            const std::string& value = column == 0 ? row.coin
                                     : column == 1 ? row.balance
                                     : column == 2 ? row.usd_value
                                     : row.unrealised_pnl;
            text.text(value.c_str());
            if (column > 0) {
                text.align(1.0f, 0.0f);
                PlaceAtScaledX(text, mRowColumnOrigins[column], canvas_width);
            }
            if (column == 3) SetFill(text, SignColour(row.pnl_sign));
            ++column;
        }

        mRows->add(clone.get());
        mRowClones.push_back(std::move(clone));
        y += kRowPitch;
    }
}

} // namespace scratcher::cockpit
