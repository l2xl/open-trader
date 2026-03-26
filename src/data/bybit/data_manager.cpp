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

#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ranges>

#include <glaze/glaze.hpp>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include "data_manager.hpp"
#include "datahub/data_sink.hpp"
#include "entities/orderbookdata.hpp"
#include "common/hex.hpp"

namespace scratcher::bybit {
namespace {
    constexpr auto STREAM_PUBLIC_SPOT = "/v5/public/spot";
    constexpr auto STREAM_PRIVATE     = "/v5/private";
    constexpr auto API_INSTRUMENTS    = "/v5/market/instruments-info?category=spot";
    constexpr auto API_RECENT_TRADE   = "/v5/market/recent-trade?category=spot&limit=60";

    std::string ping_message(size_t counter)
    {
        std::ostringstream buf;
        buf << R"({"req_id":")" << counter << R"(","op":"ping"})";
        return buf.str();
    }

    std::string subscribe_message(const std::string& topic)
    {
        return R"({"op":"subscribe","args":[")" + topic + R"("]})";
    }

    std::string unsubscribe_message(const std::string& topic)
    {
        return R"({"op":"unsubscribe","args":[")" + topic + R"("]})";
    }

    std::string extract_symbol(const std::string& topic)
    {
        auto pos = topic.rfind('.');
        return (pos != std::string::npos) ? topic.substr(pos + 1) : topic;
    }

    std::string current_timestamp_ms()
    {
        auto now = std::chrono::system_clock::now();
        return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
    }

    std::string hmac_sha256(const std::string& key, const std::string& data)
    {
        unsigned char result[EVP_MAX_MD_SIZE];
        unsigned int result_len = 0;
        HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()), reinterpret_cast<const unsigned char*>(data.data()), data.size(), result, &result_len);
        return hex(std::span(result, result_len));
    }

    connect::http_headers sign_rest_request(const std::string& api_key, const std::string& api_secret, const std::string& recv_window, const std::string& payload)
    {
        std::string timestamp = current_timestamp_ms();
        std::string signature = hmac_sha256(api_secret, timestamp + api_key + recv_window + payload);
        return {
            {"X-BAPI-API-KEY",     api_key},
            {"X-BAPI-TIMESTAMP",   timestamp},
            {"X-BAPI-SIGN",        signature},
            {"X-BAPI-RECV-WINDOW", recv_window}
        };
    }

    std::string ws_auth_message(const std::string& api_key, const std::string& api_secret)
    {
        auto now        = std::chrono::system_clock::now();
        auto expires_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() + 10000;
        std::string expires   = std::to_string(expires_ms);
        std::string signature = hmac_sha256(api_secret, "GET/realtime" + expires);
        std::ostringstream msg;
        msg << R"({"op":"auth","args":[")" << api_key << R"(",)" << expires << R"(,")" << signature << R"("]})";
        return msg.str();
    }

} // anonymous namespace

// Glaze-reflected types must live at namespace scope
struct PlaceOrderResult   { std::string orderId; };
struct PlaceOrderResponse { int retCode{0}; PlaceOrderResult result; };
struct CancelOrderRequest { std::string category; std::string symbol; std::string orderId; };

const std::string ByBitDataManager::BYBIT = "ByBit";

ByBitDataManager::ByBitDataManager(std::shared_ptr<scheduler> scheduler, std::shared_ptr<IExchangeConfig> config, std::shared_ptr<SQLite::Database> db, ensure_private)
    : m_context(connect::context::create(scheduler->io()))
    , m_db(std::move(db))
    , m_config(std::move(config))
    , m_instrument_feed(instrument_feed_type::create())
    , m_private_order_feed(private_order_feed_type::create())
    , m_private_trade_feed(private_trade_feed_type::create())
    { }

std::shared_ptr<ByBitDataManager> ByBitDataManager::Create(std::shared_ptr<scheduler> scheduler, std::shared_ptr<IExchangeConfig> config, std::shared_ptr<SQLite::Database> db)
{
    auto self = std::make_shared<ByBitDataManager>(scheduler, std::move(config), std::move(db), ensure_private{});
    std::weak_ptr<ByBitDataManager> ref = self;

    auto error_cb = [ref](std::exception_ptr e){ if (auto s = ref.lock()) s->HandleError(e); };

    self->m_instrument_sink = datahub::make_data_sink<InstrumentInfo, &InstrumentInfo::symbol>(self->m_db, self->m_instrument_feed->data_acceptor<std::deque<InstrumentInfo>>(), error_cb);
    self->m_private_order_sink = datahub::make_data_sink<Order, &Order::orderId>(self->m_db, self->m_private_order_feed->data_acceptor<std::deque<Order>>(), error_cb);
    self->m_private_trade_sink = datahub::make_data_sink<Trade, &Trade::execId>(self->m_db, self->m_private_trade_feed->data_acceptor<std::deque<Trade>>(), error_cb);

    self->SetupInstrumentDataSource();
    self->SetupPublicDataSource();
    self->SetupPrivateDataSource();

    return self;
}

void ByBitDataManager::HandleError(std::exception_ptr eptr)
{
    try {
        std::rethrow_exception(eptr);
    } catch (const std::exception& ex) {
        std::cerr << "ByBit data error: " << ex.what() << std::endl;
    }
}

void ByBitDataManager::SetupInstrumentDataSource()
{
    std::string url = "https://" + m_config->HttpHost() + ":" + m_config->HttpPort() + API_INSTRUMENTS;
    std::clog << "setupInstrumentDataSource: " << url << std::endl;

    auto data_sink = m_instrument_sink->data_acceptor<std::deque<InstrumentInfoAPI>>();

    auto resp_adapter = datahub::make_data_adapter<ApiResponse<ListResult<InstrumentInfoAPI>>>(
        [data_sink = std::move(data_sink)](ApiResponse<ListResult<InstrumentInfoAPI>>&& response) mutable {
            std::clog << "Received " << response.result.list.size() << " instruments from server" << std::endl;
            data_sink(std::move(response.result.list));
            return true;
        }
    );

    auto ref = weak_from_this();
    auto dispatcher = datahub::make_data_dispatcher(m_context->io().get_executor(), std::move(resp_adapter));

    m_instruments_query = connect::http_query::create(m_context, url, std::move(dispatcher),
        [ref](std::exception_ptr e) {
            if (auto self = ref.lock())
                self->HandleError(e);
        }
    );
}

void ByBitDataManager::SetupPublicDataSource()
{
    auto ref = weak_from_this();
    auto error_cb = [ref](std::exception_ptr e){ if (auto s = ref.lock()) s->HandleError(e); };

    m_public_stream = connect::websock_connection::create(m_context,
        "wss://" + m_config->StreamHost() + ":" + m_config->StreamPort() + STREAM_PUBLIC_SPOT,
        datahub::make_data_dispatcher(m_context->io().get_executor(),

            datahub::make_data_adapter<WsApiPayload<std::deque<WsPublicTrade>>>(
                [ref](WsApiPayload<std::deque<WsPublicTrade>>&& payload) {
                    if (auto self = ref.lock()) {
                        auto symbol = extract_symbol(payload.topic);
                        if (auto it = self->m_pubdata_accept.find(symbol); it != self->m_pubdata_accept.end()) {
                            auto& [ob_sink, ob_feed, pt_sink, pt_feed] = it->second;
                            if (pt_sink) {
                                pt_sink->accept(payload.data);
                                return true;
                            }
                        }
                    }
                    return false;
                }),

            datahub::make_data_adapter<WsApiPayload<OrderBookData>>(
                [ref](WsApiPayload<OrderBookData>&& payload) {
                    if (auto self = ref.lock()) {
                        auto symbol = extract_symbol(payload.topic);
                        if (auto it = self->m_pubdata_accept.find(symbol); it != self->m_pubdata_accept.end()) {
                            auto& [ob_sink, ob_feed, pt_sink, pt_feed] = it->second;
                            if (ob_sink) {
                                for (auto& ask : payload.data.a)
                                    ask.size = -ask.size;

                                if (payload.type == "snapshot")
                                    ob_sink->accept(std::vector<OrderBookLevel>{});

                                if (!payload.data.b.empty()) ob_sink->accept(std::move(payload.data.b));
                                if (!payload.data.a.empty()) ob_sink->accept(std::move(payload.data.a));
                                return true;
                            }
                        }
                    }
                    return false;
                })),

                error_cb);
    m_public_stream->set_heartbeat(std::chrono::seconds(20), ping_message);
}

void ByBitDataManager::SetupPrivateDataSource()
{
    if (!m_config->HasApiCredentials()) {
        std::clog << "No API credentials, skipping private stream" << std::endl;
        return;
    }

    auto ref = weak_from_this();
    auto error_cb = [ref](std::exception_ptr e){ if (auto s = ref.lock()) s->HandleError(e); };

    auto order_acceptor = m_private_order_sink->data_acceptor<std::deque<Order>>();
    auto trade_acceptor = m_private_trade_sink->data_acceptor<std::deque<Trade>>();

    m_private_stream = connect::websock_connection::create(m_context,
        "wss://" + m_config->StreamHost() + ":" + m_config->StreamPort() + STREAM_PRIVATE,
        datahub::make_data_dispatcher(m_context->io().get_executor(),
            datahub::make_data_adapter<WsApiPayload<std::deque<Order>>>([order_acceptor = std::move(order_acceptor)](WsApiPayload<std::deque<Order>>&& payload) mutable
                { order_acceptor(std::move(payload.data)); return true; }),
            datahub::make_data_adapter<WsApiPayload<std::deque<Trade>>>([trade_acceptor = std::move(trade_acceptor)](WsApiPayload<std::deque<Trade>>&& payload) mutable
                { trade_acceptor(std::move(payload.data)); return true; })),
        error_cb);

    (*m_private_stream)(ws_auth_message(m_config->ApiKey(), m_config->ApiSecret()));
    (*m_private_stream)(subscribe_message("order"));
    (*m_private_stream)(subscribe_message("execution"));
    m_private_stream->set_heartbeat(std::chrono::seconds(20), ping_message);
}

// ─── IDataController subscriptions ───────────────────────────────────────────

void ByBitDataManager::SubscribeInstrumentList(std::weak_ptr<datahub::data_subscription<InstrumentInfo>> sub)
{
    m_instrument_feed->subscribe(std::move(sub));
    (*m_instruments_query)();
}

void ByBitDataManager::SubscribeInstrument(std::string symbol, std::weak_ptr<datahub::data_subscription<OrderBookLevel>> ob_sub, std::weak_ptr<datahub::data_subscription<PublicTrade>> pt_sub)
{
    auto& [ob_sink, ob_feed, pt_sink, pt_feed] = m_pubdata_accept[symbol];
    auto ref = weak_from_this();
    auto error_cb = [ref](std::exception_ptr e){ if (auto s = ref.lock()) s->HandleError(e); };

    if (!ob_feed) {
        ob_feed = orderbook_feed_type::create();
        ob_sink = datahub::make_data_sink(OrderBook::Create(), ob_feed->template data_acceptor<std::vector<OrderBookLevel>>(), error_cb);
        (*m_public_stream)(subscribe_message("orderbook.50." + symbol));
    }
    ob_feed->subscribe(std::move(ob_sub));

    if (!pt_feed) {
        pt_feed = pubtrade_feed_type::create();
        auto model = datahub::data_model<PublicTrade, &PublicTrade::execId>::create(m_db, "_" + symbol);
        pt_sink = datahub::make_data_sink(std::move(model), pt_feed->data_acceptor<std::deque<PublicTrade>>(), std::move(error_cb));
        (*m_public_stream)(subscribe_message("publicTrade." + symbol));
    }
    pt_feed->subscribe(std::move(pt_sub));
}

void ByBitDataManager::SubscribeOrders(std::weak_ptr<datahub::data_subscription<Order>> sub)
{
    m_private_order_feed->subscribe(std::move(sub));
}

void ByBitDataManager::SubscribeTrades(std::weak_ptr<datahub::data_subscription<Trade>> sub)
{
    m_private_trade_feed->subscribe(std::move(sub));
}

// ─── Order management ─────────────────────────────────────────────────────────

void ByBitDataManager::PlaceOrder(OrderRequest request, std::function<void(std::string orderId)> callback)
{
    if (!m_config->HasApiCredentials()) {
        std::cerr << "PlaceOrder: no API credentials configured" << std::endl;
        return;
    }

    std::string body  = glz::write_json(request).value_or("{}");
    auto headers      = sign_rest_request(m_config->ApiKey(), m_config->ApiSecret(), "5000", body);
    auto ref          = weak_from_this();

    auto query = connect::http_query::create(m_context, boost::beast::http::verb::post,
        "https://" + m_config->HttpHost() + ":" + m_config->HttpPort() + "/v5/order/create",
        [callback](std::string&& response_json) {
            PlaceOrderResponse resp;
            if (!glz::read<glz::opts{.error_on_unknown_keys = false}>(resp, response_json) && resp.retCode == 0) {
                if (callback) callback(std::move(resp.result.orderId));
            } else {
                std::cerr << "PlaceOrder failed: " << response_json << std::endl;
            }
        },
        [ref](std::exception_ptr e){ if (auto s = ref.lock()) s->HandleError(e); });
    (*query)({}, std::move(headers), std::move(body));
}

void ByBitDataManager::CancelOrder(const std::string& orderId, const std::string& symbol)
{
    if (!m_config->HasApiCredentials()) {
        std::cerr << "CancelOrder: no API credentials configured" << std::endl;
        return;
    }

    CancelOrderRequest req{.category = "spot", .symbol = symbol, .orderId = orderId};
    std::string body  = glz::write_json(req).value_or("{}");
    auto headers      = sign_rest_request(m_config->ApiKey(), m_config->ApiSecret(), "5000", body);
    auto ref          = weak_from_this();

    auto query = connect::http_query::create(m_context, boost::beast::http::verb::post,
        "https://" + m_config->HttpHost() + ":" + m_config->HttpPort() + "/v5/order/cancel",
        [](std::string&& response_json) { std::clog << "CancelOrder response: " << response_json << std::endl; },
        [ref](std::exception_ptr e){ if (auto s = ref.lock()) s->HandleError(e); });
    (*query)({}, std::move(headers), std::move(body));
}

} // scratcher::bybit
