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

    boost::asio::co_spawn(self->mScheduler->io(), coUpdate(self), boost::asio::detached);

    return self;
}

boost::asio::awaitable<void> TradeCockpit::coUpdate(std::weak_ptr<TradeCockpit> ref)
{
    using namespace std::chrono;
    while (true) {
        try {
            std::optional<boost::asio::steady_timer> timer;
            if (auto self = ref.lock()) {
                for (auto& [pid, panel] : self->mPanels) panel->Update();
                timer.emplace(self->mScheduler->io(), milliseconds(1000));
            } else {
                break;
            }
            if (timer) {
                timer->expires_after(milliseconds(100));
                co_await timer->async_wait(boost::asio::use_awaitable);
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

std::chrono::seconds TradeCockpit::GetDefaultCandlePeriod() const
{
    return std::chrono::seconds(mDefaultsConfig.get_option(config_keys::default_candle_period)->as<int>());
}

uint32_t TradeCockpit::GetDefaultCandleWidth() const
{
    return static_cast<uint32_t>(mDefaultsConfig.get_option(config_keys::default_candle_width)->as<int>());
}

panel_id TradeCockpit::RegisterPanel(std::shared_ptr<ContentPanel> panel)
{
    panel_id pid = mNextPanelId++;

    if (mDataReady)
        panel->SetDataReady(true);

    if (auto ipanel = std::dynamic_pointer_cast<InstrumentContentPanel>(panel)) {
        WireInstrumentPanel(ipanel, pid);
    }

    mPanels[pid] = std::move(panel);
    return pid;
}

void TradeCockpit::UnregisterPanel(panel_id pid)
{
    mPanels.erase(pid);
    mPendingSymbols.erase(pid);
}

void TradeCockpit::WireInstrumentPanel(std::shared_ptr<InstrumentContentPanel> ipanel, panel_id pid)
{
    if (mDataReady)
        ipanel->SetInstrumentList(mInstruments);

    std::weak_ptr<TradeCockpit> self = weak_from_this();
    ipanel->SetOnUserSymbolSelection([self, pid](std::string sym) {
        if (auto s = self.lock())
            s->HandleUserSymbolSelection(pid, std::move(sym));
    });

    if (!mFirstInstrumentPanelHandled) {
        mFirstInstrumentPanelHandled = true;
        auto default_symbol = config::get_option<std::string>(mDefaultsConfig, config_keys::default_instrument).value_or("");
        if (!default_symbol.empty()) {
            mPanels[pid] = ipanel;
            HandleUserSymbolSelection(pid, std::move(default_symbol));
        }
    }
}

void TradeCockpit::HandleUserSymbolSelection(panel_id pid, std::string symbol)
{
    auto it = mPanels.find(pid);
    if (it == mPanels.end()) return;
    auto ipanel = std::dynamic_pointer_cast<InstrumentContentPanel>(it->second);
    if (!ipanel) return;

    ipanel->SetSymbol(symbol);

    std::optional<bybit::InstrumentInfo> info;
    const auto& snapshot = mDataManager->getInstrumentsFeed().get_snapshot();
    for (const auto& inst : snapshot) {
        if (inst.symbol == symbol) {
            info = inst;
            break;
        }
    }

    if (info) {
        ipanel->SetInstrumentInfo(std::move(info));
        mPendingSymbols.erase(pid);
    } else {
        mPendingSymbols[pid] = std::move(symbol);
    }
}

void TradeCockpit::OnInstrumentsLoaded()
{
    const auto& snapshot = mDataManager->getInstrumentsFeed().get_snapshot();

    mInstruments.clear();
    mInstruments.reserve(snapshot.size());
    for (const auto& inst : snapshot)
        mInstruments.emplace_back(inst.symbol);

    mDataReady = true;

    for (auto& [pid, panel] : mPanels) {
        panel->SetDataReady(true);
        if (auto ipanel = std::dynamic_pointer_cast<InstrumentContentPanel>(panel))
            ipanel->SetInstrumentList(mInstruments);
    }

    auto pending = std::move(mPendingSymbols);
    mPendingSymbols.clear();
    for (auto& [pid, sym] : pending)
        HandleUserSymbolSelection(pid, std::move(sym));
}

} // namespace scratcher::cockpit
