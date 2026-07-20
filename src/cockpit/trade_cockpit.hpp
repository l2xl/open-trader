// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/container/flat_map.hpp>

#include "cli11/CLI11.hpp"
#include "scheduler.hpp"
#include "bybit/data_manager.hpp"
#include "content_panel.hpp"
#include "instrument_panel.hpp"

namespace SQLite {
class Database;
}

namespace scratcher::cockpit {

class TradeCockpit : public std::enable_shared_from_this<TradeCockpit>
{
public:
    using InstrumentsCallback = std::function<void(const IDataController::instrument_container_type&)>;
    using subscription_id = uint64_t;

private:
    std::shared_ptr<scheduler> mScheduler;
    CLI::App& mDefaultsConfig;
    std::shared_ptr<IDataController> mDataManager;
    std::shared_ptr<IDataController::instruments_feed_type::subscription_type> mInstrumentSub;

    mutable std::mutex mMutex;
    boost::container::flat_map<panel_id, std::weak_ptr<ContentPanel>> mPanels;
    boost::container::flat_map<subscription_id, InstrumentsCallback> mInstrumentSubscribers;
    panel_id mNextPanelId = 1;
    subscription_id mNextSubId = 1;

    struct EnsurePrivate {};

    void OnInstrumentsLoaded(const IDataController::instrument_container_type& cache);

    static boost::asio::awaitable<void> coUpdate(std::weak_ptr<TradeCockpit> ref);

public:
    TradeCockpit(std::shared_ptr<scheduler> sched, CLI::App& app, std::shared_ptr<SQLite::Database> db, EnsurePrivate);

    static std::shared_ptr<TradeCockpit> Create(std::shared_ptr<scheduler> sched, CLI::App& app, std::shared_ptr<SQLite::Database> db);

    // Lifecycle. Instrument-bearing panels go through RegisterInstrumentPanel — the
    // cockpit binds the instrument and (in future) wires per-symbol data subscriptions.
    // Other panel types use the generic RegisterPanel.
    panel_id RegisterPanel(std::shared_ptr<ContentPanel> panel);
    panel_id RegisterInstrumentPanel(const std::string& symbol, std::shared_ptr<InstrumentPanel> panel);

    // Reactive instrument-list channel. Delivery happens on the data-manager thread
    // (caller marshals to UI). The current snapshot, if any, is delivered synchronously
    // at subscription time — a late subscriber never deadlocks waiting for the next event.
    subscription_id SubscribeInstruments(InstrumentsCallback cb);
    void UnsubscribeInstruments(subscription_id id);

    PanelType GetDefaultContentPanelType() const;
    std::string GetDefaultSymbol() const;
    seconds GetDefaultCandlePeriod() const;
    uint32_t GetDefaultCandleWidth() const;

    std::shared_ptr<IDataController> GetDataController() const { return mDataManager; }
};

} // namespace scratcher::cockpit
