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

#include "trade_cockpit.hpp"

#include <chrono>
#include <optional>
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

    self->mInstrumentSub = datahub::make_data_subscription<std::deque<bybit::InstrumentInfo>>(
        [weak = std::weak_ptr(self)](auto /*update*/) {
            if (auto s = weak.lock())
                s->OnInstrumentsLoaded();
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
    if (mInstrumentsReady) panel->Update();
    mPanels[pid] = panel;
    return pid;
}

panel_id TradeCockpit::RegisterInstrumentPanel(const std::string& symbol, std::shared_ptr<InstrumentPanel> panel)
{
    // If the snapshot has not arrived yet (early registration race) the panel is
    // bound to an empty info; OnInstrumentsLoaded re-binds via the regular Update().
    bybit::InstrumentInfo info;
    {
        std::lock_guard lock(mMutex);
        for (const auto& i : mInstrumentsSnapshot) {
            if (i.symbol == symbol) { info = i; break; }
        }
    }
    panel->SetInstrumentInfo(std::move(info));
    return RegisterPanel(std::move(panel));
}

namespace {
std::vector<std::string> SymbolList(const std::vector<bybit::InstrumentInfo>& snapshot)
{
    std::vector<std::string> out;
    out.reserve(snapshot.size());
    for (const auto& i : snapshot) out.push_back(i.symbol);
    return out;
}
} // anonymous namespace

TradeCockpit::subscription_id TradeCockpit::SubscribeInstruments(InstrumentsCallback cb)
{
    std::vector<std::string> snapshot;
    subscription_id id;
    bool ready;
    {
        std::lock_guard lock(mMutex);
        id = mNextSubId++;
        mInstrumentSubscribers[id] = cb;
        ready = mInstrumentsReady;
        if (ready) snapshot = SymbolList(mInstrumentsSnapshot);
    }
    if (ready && cb) cb(snapshot);
    return id;
}

void TradeCockpit::UnsubscribeInstruments(subscription_id id)
{
    std::lock_guard lock(mMutex);
    mInstrumentSubscribers.erase(id);
}

void TradeCockpit::OnInstrumentsLoaded()
{
    const auto& feed = mDataManager->getInstrumentsFeed().get_snapshot();

    std::vector<bybit::InstrumentInfo> snapshot(feed.begin(), feed.end());
    auto symbols = SymbolList(snapshot);

    std::vector<InstrumentsCallback> subs;
    {
        std::lock_guard lock(mMutex);
        mInstrumentsSnapshot = std::move(snapshot);
        mInstrumentsReady = true;
        subs.reserve(mInstrumentSubscribers.size());
        for (auto& [id, cb] : mInstrumentSubscribers) subs.push_back(cb);

        for (auto& [pid, wpanel] : mPanels) {
            if (auto p = wpanel.lock()) p->Update();
        }
    }

    for (auto& cb : subs) if (cb) cb(symbols);
}

} // namespace scratcher::cockpit
