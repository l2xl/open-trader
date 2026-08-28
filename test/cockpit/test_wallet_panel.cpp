// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>

#include <deque>

#include "wallet_panel.hpp"

using namespace scratcher;
using namespace scratcher::cockpit;

namespace {

struct HeadlessWalletPanel : WalletPanel
{
    void Refresh() override {}
};

bybit::CoinBalance Coin(const std::string& coin, const std::string& balance, const std::string& usd, std::optional<std::string> upl)
{
    bybit::CoinBalance c;
    c.coin = coin;
    c.walletBalance = currency<uint64_t>{balance};
    c.usdValue = currency<uint64_t>{usd};
    if (upl) c.unrealisedPnl = currency<int64_t>{*upl};
    return c;
}

bybit::WalletBalance SampleWallet()
{
    bybit::WalletBalance w;
    w.accountType = bybit::AccountType::UNIFIED;
    w.totalEquity = currency<uint64_t>{"96450.75"};
    w.totalWalletBalance = currency<uint64_t>{"95247.65"};
    w.totalAvailableBalance = currency<uint64_t>{"52110"};
    w.totalPerpUPL = currency<int64_t>{"-1203.1"};
    w.coin.push_back(Coin("ETH", "0.35000000", "1320.10", "-102"));
    w.coin.push_back(Coin("BTC", "0.52310000", "50020.10", "812.4"));
    w.coin.push_back(Coin("USDT", "45110.55000000", "45110.55", std::nullopt));
    return w;
}

uint32_t Rgb(const uint32_t* pixels, uint32_t stride, uint32_t x, uint32_t y)
{
    return pixels[y * stride + x] & 0x00ffffffu;
}

}

TEST_CASE("Wallet view-model formats totals with thousands grouping, fixed decimals and a signed PnL", "[wallet_panel]")
{
    const auto model = MakeWalletViewModel(SampleWallet(), time_point{});

    CHECK(model.equity == "96,450.75");
    CHECK(model.wallet_balance == "95,247.65");
    CHECK(model.available == "52,110.00");
    CHECK(model.unrealised_pnl == "−1,203.10");
    CHECK(model.pnl_sign == -1);
    CHECK(model.status.starts_with("UNIFIED · "));
}

TEST_CASE("Wallet view-model rows sort by USD value and trim quantity zeros down to two decimals", "[wallet_panel]")
{
    const auto model = MakeWalletViewModel(SampleWallet(), time_point{});

    REQUIRE(model.rows.size() == 3);
    CHECK(model.rows[0].coin == "BTC");
    CHECK(model.rows[0].balance == "0.5231");
    CHECK(model.rows[0].usd_value == "50,020.10");
    CHECK(model.rows[0].unrealised_pnl == "+812.40");
    CHECK(model.rows[0].pnl_sign == 1);

    CHECK(model.rows[1].coin == "USDT");
    CHECK(model.rows[1].balance == "45,110.55");
    CHECK(model.rows[1].unrealised_pnl == "—");
    CHECK(model.rows[1].pnl_sign == 0);

    CHECK(model.rows[2].coin == "ETH");
    CHECK(model.rows[2].balance == "0.35");
    CHECK(model.rows[2].unrealised_pnl == "−102.00");
    CHECK(model.rows[2].pnl_sign == -1);
}

TEST_CASE("Wallet panel binds the SVG template and renders the form headlessly", "[wallet_panel]")
{
    HeadlessWalletPanel panel;
    panel.OnWallet(datahub::update_kind::snapshot, std::deque<bybit::WalletBalance>{SampleWallet()});

    constexpr uint32_t w = 420, h = 320;
    panel.AllocatePixelBuffer(w, h);
    const auto painted = panel.Render();
    CHECK(painted.w == w);
    CHECK(painted.h == h);

    const uint32_t* px = panel.PixelBufferData();
    // Template background stretched to the whole canvas, card stretched to the width.
    CHECK(Rgb(px, w, 0, 0) == 0x1e1e23u);
    CHECK(Rgb(px, w, w - 1, h - 1) == 0x1e1e23u);
    CHECK(Rgb(px, w, 200, 100) == 0x28282du);
    // First cloned row background sits at the rows offset (y = 192) inside the left inset.
    CHECK(Rgb(px, w, 12, 200) == 0x232328u);
    // Third row exists too (two row pitches further down).
    CHECK(Rgb(px, w, 12, 248) == 0x232328u);

    // The hero equity text leaves non-background pixels inside the card.
    size_t lit = 0;
    for (uint32_t y = 44; y < 72; ++y)
        for (uint32_t x = 20; x < 200; ++x)
            if (Rgb(px, w, x, y) != 0x28282du) ++lit;
    CHECK(lit > 200);
}
