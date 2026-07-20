// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef DATA_PROVIDER_HPP
#define DATA_PROVIDER_HPP

#include <functional>
#include <deque>
#include <string>
#include <memory>

#include <boost/container/stable_vector.hpp>

#include "datahub/data_subscription.hpp"
#include "datahub/data_feed.hpp"
#include "orderbook.hpp"
#include "bybit/entities/instrument.hpp"
#include "bybit/entities/public_trade.hpp"
#include "bybit/entities/order.hpp"
#include "bybit/entities/trade.hpp"
#include "bybit/entities/wallet.hpp"

namespace scratcher {

// Callers build a subscription with datahub::make_subscription<Range>(callable) — the
// factory statically picks the matching subscription<Range, Extra...> spec from the
// callable's arity. The controller takes weak_ptr<...subscription_type>; dropping the
// shared_ptr = unsubscribe (RAII). The instrument feed uses
// boost::container::stable_vector so const-refs handed to the callback survive the
// feed's later in-place updates and appends — safe to hold past the callback.
struct IDataController
{
    using instruments_feed_type = datahub::keyed_snapshot_data_feed<bybit::InstrumentInfo, &bybit::InstrumentInfo::symbol, boost::container::stable_vector>;
    using instrument_container_type = instruments_feed_type::cache_type;

    using orderbook_feed_type    = datahub::sorted_snapshot_data_feed<OrderBookLevel, &OrderBookLevel::price, &OrderBookLevel::price>;
    using public_trades_feed_type = datahub::sorted_data_feed<bybit::PublicTrade, &bybit::PublicTrade::time, &bybit::PublicTrade::execId>;
    using private_orders_feed_type = datahub::sorted_data_feed<bybit::Order, &bybit::Order::updatedTime, &bybit::Order::orderId>;
    using private_trades_feed_type = datahub::sorted_data_feed<bybit::Trade, &bybit::Trade::execTime, &bybit::Trade::execId>;

    virtual ~IDataController() = default;
    virtual const std::string& Name() const = 0;

    virtual void SubscribeInstrumentList(std::weak_ptr<instruments_feed_type::subscription_type> sub) = 0;

    // Subscribe a caller-owned public-trade subscription to a symbol. The controller materialises
    // the per-symbol feeds + WS streams on the first call (and an order-book subscription whose
    // consumption is still TBD), then wires `trade_sub` to the public-trade feed — which keeps
    // only a weak_ptr, so the subscriber owns the shared_ptr and dropping it unsubscribes (RAII,
    // exactly like SubscribeInstrumentList). The feed delivers an immediate snapshot if its cache
    // is already populated. Trades arrive as the feed's native bybit::PublicTrade cache subrange.
    virtual void SubscribeInstrument(std::string symbol, std::weak_ptr<public_trades_feed_type::subscription_type> trade_sub) = 0;
    virtual void SubscribeOrders(std::weak_ptr<private_orders_feed_type::subscription_type> sub) = 0;
    virtual void SubscribeTrades(std::weak_ptr<private_trades_feed_type::subscription_type> sub) = 0;

    virtual const instruments_feed_type& getInstrumentsFeed() const = 0;

    // Per-symbol public-trade feed shared across all consumers, materialised by
    // SubscribeInstrument(symbol, ...). Returns nullptr for symbols that have not yet been
    // subscribed. The returned shared_ptr keeps the feed alive at least as long as the caller
    // retains it; the data manager retains its own copy. Most consumers receive trades via the
    // subscription sink instead and never need this accessor.
    virtual std::shared_ptr<const public_trades_feed_type> getPublicTradesFeed(const std::string& symbol) const = 0;

    virtual void PlaceOrder(bybit::OrderRequest request, std::function<void(std::string orderId)> callback) = 0;
    virtual void CancelOrder(const std::string& orderId, const std::string& symbol) = 0;
};

} // scratcher

#endif //DATA_PROVIDER_HPP
