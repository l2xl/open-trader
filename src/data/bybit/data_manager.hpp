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

#ifndef DATA_COLLECTOR_HPP
#define DATA_COLLECTOR_HPP

#include <string>
#include <memory>
#include <functional>
#include <boost/container/flat_map.hpp>

#include "data_controller.hpp"
#include "orderbook.hpp"
#include "scheduler.hpp"
#include "bybit/entities/response.hpp"
#include "bybit/entities/instrument.hpp"
#include "bybit/entities/public_trade.hpp"
#include "bybit/entities/order.hpp"
#include "bybit/entities/trade.hpp"
#include "bybit/entities/wallet.hpp"
#include "connection_context.hpp"
#include "http_query.hpp"
#include "websocket.hpp"
#include "datahub/data_sink.hpp"
#include "datahub/data_feed.hpp"
#include "exchange_config.hpp"

namespace scratcher {
namespace bybit {

class ByBitDataManager : public IDataController, public std::enable_shared_from_this<ByBitDataManager>
{
public:
    using instrument_sink_type = datahub::data_sink<datahub::data_model<InstrumentInfo, &InstrumentInfo::symbol>>;
    using orderbook_sink_type  = datahub::data_sink<OrderBook>;
    using orderbook_sink_ptr   = std::shared_ptr<orderbook_sink_type>;
    using pubtrade_sink_type   = datahub::data_sink<datahub::data_model<PublicTrade, &PublicTrade::execId>>;
    using pubtrade_sink_ptr    = std::shared_ptr<pubtrade_sink_type>;
    using private_order_sink_type = datahub::data_sink<datahub::data_model<Order, &Order::orderId>>;
    using private_trade_sink_type = datahub::data_sink<datahub::data_model<Trade, &Trade::execId>>;

    using instrument_feed_type = datahub::snapshot_data_feed<InstrumentInfo>;

    using orderbook_feed_type      = datahub::snapshot_data_feed<OrderBookLevel>;
    using orderbook_feed_ptr       = std::shared_ptr<orderbook_feed_type>;
    using pubtrade_feed_type       = datahub::sorted_data_feed<PublicTrade, &PublicTrade::time, &PublicTrade::execId>;
    using pubtrade_feed_ptr        = std::shared_ptr<pubtrade_feed_type>;
    using private_order_feed_type  = datahub::sorted_data_feed<Order, &Order::updatedTime, &Order::orderId>;
    using private_trade_feed_type  = datahub::sorted_data_feed<Trade, &Trade::execTime, &Trade::execId>;

private:
    static const std::string BYBIT;

    std::shared_ptr<connect::context>          m_context;
    std::shared_ptr<SQLite::Database>          m_db;
    std::shared_ptr<IExchangeConfig>           m_config;

    std::shared_ptr<instrument_feed_type>        m_instrument_feed;
    std::shared_ptr<private_order_feed_type>     m_private_order_feed;
    std::shared_ptr<private_trade_feed_type>     m_private_trade_feed;

    std::shared_ptr<connect::http_query>         m_instruments_query;
    std::shared_ptr<instrument_sink_type>        m_instrument_sink;

    std::shared_ptr<connect::websock_connection> m_public_stream;
    boost::container::flat_map<std::string, std::tuple<orderbook_sink_ptr, orderbook_feed_ptr, pubtrade_sink_ptr, pubtrade_feed_ptr>> m_pubdata_accept;

    std::shared_ptr<private_order_sink_type>     m_private_order_sink;
    std::shared_ptr<private_trade_sink_type>     m_private_trade_sink;
    std::shared_ptr<connect::websock_connection> m_private_stream;

    void SetupInstrumentDataSource();
    void SetupPublicDataSource();
    void SetupPrivateDataSource();

    struct ensure_private {};
public:
    ByBitDataManager(std::shared_ptr<scheduler> scheduler, std::shared_ptr<IExchangeConfig> config, std::shared_ptr<SQLite::Database> db, ensure_private);
    ~ByBitDataManager() override = default;

    static std::shared_ptr<ByBitDataManager> Create(std::shared_ptr<scheduler> scheduler, std::shared_ptr<IExchangeConfig> config, std::shared_ptr<SQLite::Database> db);

    const std::string& Name() const override { return BYBIT; }

    void HandleError(std::exception_ptr eptr);

    void SubscribeInstrumentList(std::weak_ptr<datahub::data_subscription<InstrumentInfo>> sub) override;
    void SubscribeInstrument(std::string symbol, std::weak_ptr<datahub::data_subscription<OrderBookLevel>> ob_sub, std::weak_ptr<datahub::data_subscription<PublicTrade>> pt_sub) override;
    void SubscribeOrders(std::weak_ptr<datahub::data_subscription<Order>> sub) override;
    void SubscribeTrades(std::weak_ptr<datahub::data_subscription<Trade>> sub) override;

    void PlaceOrder(OrderRequest request, std::function<void(std::string orderId)> callback) override;
    void CancelOrder(const std::string& orderId, const std::string& symbol) override;
};

} // scratcher::bybit
}

#endif //DATA_COLLECTOR_HPP
