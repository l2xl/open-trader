// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "trade_cockpit.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include "config_helper.hpp"
#include "trade_cockpit_config.hpp"
#include "data/bybit/bybit_config.hpp"

namespace scratcher::cockpit {

TradeCockpit::TradeCockpit(std::shared_ptr<scheduler> sched, CLI::App& app, std::shared_ptr<SQLite::Database> db, EnsurePrivate)
    : mScheduler(std::move(sched))
    , mDefaultsConfig(config::get_subcommand(app, config_keys::section))
{
    mDataManager = bybit::ByBitDataManager::Create(mScheduler, config::get_subcommand(app, bybit::config_keys::section), std::move(db));
}

std::shared_ptr<TradeCockpit> TradeCockpit::Create(std::shared_ptr<scheduler> sched, CLI::App& app, std::shared_ptr<SQLite::Database> db)
{
    auto self = std::make_shared<TradeCockpit>(std::move(sched), app, std::move(db), EnsurePrivate{});

    self->mInstrumentSub = datahub::make_subscription<IDataController::instrument_container_type>(
        [weak = std::weak_ptr(self)](datahub::update_kind /*kind*/, const IDataController::instrument_container_type& cache) {
            if (auto s = weak.lock())
                s->OnInstrumentsLoaded(cache);
        });

    self->mDataManager->SubscribeInstrumentList(self->mInstrumentSub);

    boost::asio::co_spawn(self->mScheduler->io(), coUpdate(self), detached);

    return self;
}

boost::asio::awaitable<void> TradeCockpit::coUpdate(std::weak_ptr<TradeCockpit> ref)
{
    while (true) {
        try {
            std::optional<boost::asio::steady_timer> timer;
            if (auto self = ref.lock()) {
                std::vector<std::shared_ptr<ContentPanel>> panels;
                {
                    std::lock_guard lock(self->mMutex);
                    for (auto it = self->mPanels.begin(); it != self->mPanels.end(); ) {
                        if (auto p = it->second.lock())
                            { panels.push_back(std::move(p)); ++it; }
                        else
                            it = self->mPanels.erase(it);
                    }
                }
                for (auto& p : panels) p->Update();
                timer.emplace(self->mScheduler->io(), milliseconds(25));
            } else {
                break;
            }
            if (timer) {
                co_await timer->async_wait(use_awaitable);
            } else {
                break;
            }
        }
        catch (boost::system::error_code& e) {
            if (e.value() == boost::asio::error::operation_aborted) break;
        }
        catch (const std::exception&) {}
    }
}

PanelType TradeCockpit::GetDefaultContentPanelType() const
{
    auto sym = config::get_option<std::string>(mDefaultsConfig, config_keys::default_instrument);
    if (sym && !sym->empty())
        return PanelType::MarketGraph;
    return PanelType::Empty;
}

std::string TradeCockpit::GetDefaultSymbol() const
{
    return config::get_option<std::string>(mDefaultsConfig, config_keys::default_instrument).value_or("");
}

seconds TradeCockpit::GetDefaultCandlePeriod() const
{
    return seconds(mDefaultsConfig.get_option(config_keys::default_candle_period)->as<int>());
}

uint32_t TradeCockpit::GetDefaultCandleWidth() const
{
    return static_cast<uint32_t>(mDefaultsConfig.get_option(config_keys::default_candle_width)->as<int>());
}

panel_id TradeCockpit::RegisterPanel(std::shared_ptr<ContentPanel> panel)
{
    std::lock_guard lock(mMutex);
    panel_id pid = mNextPanelId++;
    if (!mDataManager->getInstrumentsFeed().get_snapshot().empty())
        panel->Update();
    mPanels[pid] = panel;
    return pid;
}

panel_id TradeCockpit::RegisterInstrumentPanel(const std::string& symbol, std::shared_ptr<InstrumentPanel> panel)
{
    // If the snapshot has not arrived yet (early registration race) the panel is
    // bound to an empty info; OnInstrumentsLoaded re-binds via the regular Update().
    // The feed cache is boost::container::stable_vector — references stay valid across the seldom updates
    // the keyed_snapshot_data_feed performs, so this find_if needs no copy of the cache and no shadow snapshot.
    bybit::InstrumentInfo info;
    {
        const auto& cache = mDataManager->getInstrumentsFeed().get_snapshot();
        auto it = std::ranges::find_if(cache, [&](const bybit::InstrumentInfo& i) { return i.symbol == symbol; });
        if (it != cache.end()) info = *it;
    }

    // Bind the panel's instrument (derives the fixed-point decimals used at render time), then
    // build the public-trade subscription the panel OWNS and hand a weak_ptr to the data manager.
    // The subscription's callable pins the panel via its weak_ptr and forwards the feed's native
    // PublicTrade cache subrange straight to OnPublicTrades (snapshot or increment) — no copy, no
    // adapter record. The panel owning the subscription means dropping the panel unsubscribes by
    // RAII. This is the sole trade-ingestion path; the heartbeat tick only advances the clock.
    panel->SetInstrument(std::move(info));

    using pubtrade_feed = IDataController::public_trades_feed_type;
    auto trade_sub = datahub::make_subscription<pubtrade_feed::cache_type>(
        [weak_panel = std::weak_ptr(panel)](datahub::update_kind kind, const pubtrade_feed::cache_type& /*full*/, pubtrade_feed::const_iterator first, pubtrade_feed::const_iterator last) {
            if (auto p = weak_panel.lock()) p->OnPublicTrades(kind, first, last);
        });
    panel->SetTradeSubscription(trade_sub);
    mDataManager->SubscribeInstrument(symbol, trade_sub);

    return RegisterPanel(std::move(panel));
}

TradeCockpit::subscription_id TradeCockpit::SubscribeInstruments(InstrumentsCallback cb)
{
    subscription_id id;
    {
        std::lock_guard lock(mMutex);
        id = mNextSubId++;
        mInstrumentSubscribers[id] = cb;
    }
    // Late-subscriber synchronous delivery uses the live feed cache directly.
    // by_const_ref is safe to hand through the callback for std::list — refs
    // remain valid for as long as the data manager owns the feed.
    if (cb) {

        const auto& cache = mDataManager->getInstrumentsFeed().get_snapshot();
        if (!cache.empty()) cb(cache);
    }
    return id;
}

void TradeCockpit::UnsubscribeInstruments(subscription_id id)
{
    std::lock_guard lock(mMutex);
    mInstrumentSubscribers.erase(id);
}

void TradeCockpit::OnInstrumentsLoaded(const IDataController::instrument_container_type& cache)
{
    std::vector<InstrumentsCallback> subs;
    {
        std::lock_guard lock(mMutex);
        subs.reserve(mInstrumentSubscribers.size());
        for (auto& [id, cb] : mInstrumentSubscribers) subs.push_back(cb);

        for (auto& [pid, wpanel] : mPanels) {
            if (auto p = wpanel.lock()) p->Update();
        }
    }

    for (auto& cb : subs) if (cb) cb(cache);
}

} // namespace scratcher::cockpit
