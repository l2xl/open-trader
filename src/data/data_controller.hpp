// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
//
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b21tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

#ifndef DATA_PROVIDER_HPP
#define DATA_PROVIDER_HPP

#include <functional>
#include <deque>
#include <string>
#include <memory>

#include "datahub/data_subscription.hpp"
#include "datahub/data_feed.hpp"
#include "orderbook.hpp"
#include "bybit/entities/instrument.hpp"
#include "bybit/entities/public_trade.hpp"
#include "bybit/entities/order.hpp"
#include "bybit/entities/trade.hpp"
#include "bybit/entities/wallet.hpp"

namespace scratcher {

// Caller creates subscription with make_data_subscription(handler), then passes weak_ptr to the controller.
// Dropping the shared_ptr = unsubscribe (RAII).
struct IDataController
{
    virtual ~IDataController() = default;
    virtual const std::string& Name() const = 0;

    virtual void SubscribeInstrumentList(std::weak_ptr<datahub::data_subscription<std::deque<bybit::InstrumentInfo>>> sub) = 0;
    virtual void SubscribeInstrument(std::string symbol, std::weak_ptr<datahub::data_subscription<std::deque<OrderBookLevel>>> ob_sub, std::weak_ptr<datahub::data_subscription<std::deque<bybit::PublicTrade>>> pt_sub) = 0;
    virtual void SubscribeOrders(std::weak_ptr<datahub::data_subscription<std::deque<bybit::Order>>> sub) = 0;
    virtual void SubscribeTrades(std::weak_ptr<datahub::data_subscription<std::deque<bybit::Trade>>> sub) = 0;

    virtual const datahub::keyed_snapshot_data_feed<bybit::InstrumentInfo, &bybit::InstrumentInfo::symbol>& getInstrumentsFeed() const = 0;

    // Per-symbol public-trade feed shared across all consumers. The feed is
    // materialised by SubscribeInstrument(symbol, ...) — callers that only need
    // read access (e.g. the cockpit handing the feed to a panel) still call
    // SubscribeInstrument with empty weak_ptr subs to trigger creation, then read
    // the live feed via this accessor. Returns nullptr for symbols that have not
    // yet been subscribed. The returned shared_ptr keeps the feed alive at least
    // as long as the caller retains it; the data manager retains its own copy.
    using public_trades_feed_type = datahub::sorted_data_feed<bybit::PublicTrade, &bybit::PublicTrade::time, &bybit::PublicTrade::execId>;
    virtual std::shared_ptr<const public_trades_feed_type> getPublicTradesFeed(const std::string& symbol) const = 0;

    virtual void PlaceOrder(bybit::OrderRequest request, std::function<void(std::string orderId)> callback) = 0;
    virtual void CancelOrder(const std::string& orderId, const std::string& symbol) = 0;
};

} // scratcher

#endif //DATA_PROVIDER_HPP
