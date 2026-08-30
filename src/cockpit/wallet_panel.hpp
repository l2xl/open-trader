// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bybit/entities/wallet.hpp"
#include "data_controller.hpp"
#include "svg_template.hpp"
#include "timedef.hpp"
#include "tvg_ptr.hpp"
#include "vector_scene_panel.hpp"

namespace scratcher::cockpit {

// Display-ready projection of one wallet record: every number is already formatted (thousands
// grouping, fixed decimals, U+2212 minus), so the panel binds strings and sign classes only.
struct WalletViewModel
{
    struct Row
    {
        std::string coin;
        std::string balance;
        std::string usd_value;
        std::string unrealised_pnl;
        int pnl_sign = 0;
    };

    std::string status;
    std::string equity;
    std::string unrealised_pnl;
    int pnl_sign = 0;
    std::string wallet_balance;
    std::string available;
    std::vector<Row> rows;
};

WalletViewModel MakeWalletViewModel(const bybit::WalletBalance& wallet, time_point received_at);
WalletViewModel WaitingWalletViewModel();

// Form-style panel rendering the account balance through the wallet_panel.svg template: the SVG
// carries the styling, the panel injects the view-model into the template's texts and clones the
// row template per coin. Wallet snapshots arrive on the data thread via OnWallet; the template is
// re-bound under mDataMutex in DoLayout on the next heartbeat or paint.
class WalletPanel : public VectorScenePanel
{
public:
    using wallet_feed_type = IDataController::wallet_feed_type;

    WalletPanel();

    CanvasExtent MinimalCanvasSize() const override { return CanvasExtent{300, 240}; }

    void OnWallet(datahub::update_kind kind, const wallet_feed_type::cache_type& wallets);

    void SetWalletSubscription(std::shared_ptr<wallet_feed_type::subscription_type> sub)
    { mWalletSubscription = std::move(sub); }

protected:
    bool DoLayout() override;

private:
    struct AnchoredText
    {
        tvg_ptr<tvg::Text> text;
        float origin_x = 0.0f;
    };

    void ApplyModel(float canvas_width);
    void RebuildRows(float canvas_width);

    svg_template mTemplate;
    tvg_ptr<tvg::Scene> mLayer;
    tvg_ptr<tvg::Shape> mBackground;
    tvg_ptr<tvg::Shape> mCard;
    tvg_ptr<tvg::Text> mStatus;
    tvg_ptr<tvg::Text> mEquity;
    tvg_ptr<tvg::Text> mUnrealisedPnl;
    tvg_ptr<tvg::Text> mWalletBalance;
    tvg_ptr<tvg::Text> mAvailable;
    tvg_ptr<tvg::Scene> mRows;
    tvg_ptr<tvg::Scene> mRowTemplate;
    std::vector<AnchoredText> mAnchored;
    std::vector<float> mRowColumnOrigins;
    std::vector<tvg_ptr<tvg::Scene>> mRowClones;

    WalletViewModel mModel = WaitingWalletViewModel();
    bool mModelDirty = true;
    uint32_t mLaidOutWidth = 0;
    uint32_t mLaidOutHeight = 0;

    std::shared_ptr<wallet_feed_type::subscription_type> mWalletSubscription;
};

} // namespace scratcher::cockpit
