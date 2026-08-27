// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <iostream>
#include <sstream>
#include <array>
#include <algorithm>
#include <random>
#include <ranges>

#include <glaze/glaze.hpp>

#include "data_manager.hpp"
#include "bybit_config.hpp"
#include "datahub/data_sink.hpp"
#include "entities/orderbookdata.hpp"
#include "common/hex.hpp"

namespace scratcher::bybit {
namespace {
    constexpr auto STREAM_PUBLIC_SPOT = "/v5/public/spot";
    constexpr auto STREAM_PRIVATE     = "/v5/private";
    constexpr auto API_INSTRUMENTS    = "/v5/market/instruments-info?category=spot";
    constexpr auto API_ORDER_CREATE   = "/v5/order/create";
    constexpr auto API_ORDER_CANCEL   = "/v5/order/cancel";
    constexpr auto API_ORDER_LIST     = "/v5/order/realtime";
    constexpr auto API_EXECUTION_LIST = "/v5/execution/list";
    constexpr auto API_WALLET_BALANCE = "/v5/account/wallet-balance";

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

    std::string extract_symbol(const std::string& topic)
    {
        auto pos = topic.rfind('.');
        return (pos != std::string::npos) ? topic.substr(pos + 1) : topic;
    }

    std::string http_base(CLI::App& config)
    {
        return "https://" + config.get_option(config_keys::http_host)->as<std::string>() + ":" + config.get_option(config_keys::http_port)->as<std::string>();
    }

    std::string stream_base(CLI::App& config)
    {
        return "wss://" + config.get_option(config_keys::stream_host)->as<std::string>() + ":" + config.get_option(config_keys::stream_port)->as<std::string>();
    }

    std::optional<credentials> read_credentials(CLI::App& config)
    {
        auto keyfile = config.get_option(config_keys::api_keyfile)->as<std::string>();
        if (keyfile.empty()) return std::nullopt;
        return load_credentials(keyfile);
    }

    std::string generate_order_link_id()
    {
        std::random_device entropy;
        std::array<unsigned char, 16> bytes;
        std::ranges::generate(bytes, [&entropy] { return static_cast<unsigned char>(entropy()); });
        return hex(bytes);
    }

} // anonymous namespace

const std::string ByBitDataManager::BYBIT = "ByBit";

ByBitDataManager::ByBitDataManager(std::shared_ptr<scheduler> scheduler, CLI::App& config, std::shared_ptr<SQLite::Database> db, ensure_private)
    : m_context(connect::context::create(scheduler->io()))
    , m_db(std::move(db))
    , m_config(config)
    , m_credentials(read_credentials(config))
    , m_db_strand(boost::asio::make_strand(m_context->io().get_executor()))
    , m_instrument_feed(instrument_feed_type::create())
    , m_private_order_feed(private_order_feed_type::create())
    , m_private_trade_feed(private_trade_feed_type::create())
    , m_order_ack_feed(order_ack_feed_type::create())
    , m_wallet_feed(wallet_feed_type::create())
    { }

std::shared_ptr<ByBitDataManager> ByBitDataManager::Create(std::shared_ptr<scheduler> scheduler, CLI::App& config, std::shared_ptr<SQLite::Database> db)
{
    auto self = std::make_shared<ByBitDataManager>(scheduler, config, std::move(db), ensure_private{});
    std::weak_ptr<ByBitDataManager> ref = self;

    auto error_cb = [ref](std::exception_ptr e){ HandleError(ref, e); };

    auto instrument_model = datahub::data_model<InstrumentInfo, &InstrumentInfo::symbol>::create(self->m_db, self->m_db_strand, {});
    self->m_instrument_sink = datahub::make_data_sink(std::move(instrument_model), self->m_instrument_feed->data_acceptor<std::deque<InstrumentInfo>>(), error_cb);

    auto order_model = datahub::data_model<Order, &Order::orderId>::create(self->m_db, self->m_db_strand, {});
    self->m_private_order_sink = datahub::make_data_sink(std::move(order_model), self->m_private_order_feed->data_acceptor<std::deque<Order>>(), error_cb);

    auto trade_model = datahub::data_model<Trade, &Trade::execId>::create(self->m_db, self->m_db_strand, {});
    self->m_private_trade_sink = datahub::make_data_sink(std::move(trade_model), self->m_private_trade_feed->data_acceptor<std::deque<Trade>>(), error_cb);

    self->SetupInstrumentDataSource();
    self->SetupPublicDataSource();
    self->SetupPrivateDataSource();

    return self;
}

void ByBitDataManager::HandleError(std::weak_ptr<ByBitDataManager> ref, std::exception_ptr eptr)
{
    try {
        std::rethrow_exception(eptr);
    } catch (const std::exception& ex) {
        std::cerr << "ByBit data error: " << ex.what() << std::endl;
    }
}

void ByBitDataManager::SetupInstrumentDataSource()
{
    const std::string url = http_base(m_config) + API_INSTRUMENTS;
    std::clog << "setupInstrumentDataSource: " << url << std::endl;

    auto data_sink = m_instrument_sink->data_acceptor<std::deque<InstrumentInfoAPI>>();

    auto resp_adapter = datahub::make_data_adapter<ApiResponse<ListResult<InstrumentInfoAPI>>>(
        [data_sink = std::move(data_sink)](ApiResponse<ListResult<InstrumentInfoAPI>>&& response) mutable {
            std::clog << "Received " << response.result.list.size() << " instruments from server" << std::endl;
            data_sink(std::move(response.result.list));
        }
    );

    auto ref = weak_from_this();
    auto dispatcher = datahub::make_data_dispatcher(m_context->io().get_executor(), std::move(resp_adapter));

    m_instruments_query = connect::http_query<>::create(m_context, url, std::move(dispatcher),
        [ref](std::exception_ptr e) { HandleError(ref, e); }
    );
}

void ByBitDataManager::SetupPublicDataSource()
{
    auto ref = weak_from_this();
    auto error_cb = [ref](std::exception_ptr e){ HandleError(ref, e); };

    m_public_stream = connect::websock_connection<>::create(m_context, stream_base(m_config) + STREAM_PUBLIC_SPOT,
        datahub::make_data_dispatcher(m_context->io().get_executor(),

            datahub::make_data_adapter<WsApiPayload<std::deque<WsPublicTrade>>>(
                [ref](WsApiPayload<std::deque<WsPublicTrade>>&& payload) {
                    if (auto self = ref.lock()) {
                        auto symbol = extract_symbol(payload.topic);
                        if (auto it = self->m_pubdata_accept.find(symbol); it != self->m_pubdata_accept.end()) {
                            auto& pt_sink = it->second.pt_sink;
                            if (!pt_sink) throw std::runtime_error("No public trade sink for " + symbol);

                            pt_sink->accept(payload.data);
                        }
                    }
                }),

            datahub::make_data_adapter<WsApiPayload<OrderBookData>>(
                [ref](WsApiPayload<OrderBookData>&& payload) {
                    if (auto self = ref.lock()) {
                        auto symbol = extract_symbol(payload.topic);
                        if (auto it = self->m_pubdata_accept.find(symbol); it != self->m_pubdata_accept.end()) {
                            auto& ob_sink = it->second.ob_sink;
                            if (!ob_sink) throw std::runtime_error("No orderbook sink for " + symbol);

                            if (payload.type == "snapshot")
                                ob_sink->accept(std::vector<OrderBookLevel>{});

                            // Build single asc-sorted update: bids reversed (low→high) then asks (low→high)
                            // Bid prices < ask prices is an exchange invariant, so concatenation is sorted
                            std::vector<OrderBookLevel> update;
                            update.reserve(payload.data.b.size() + payload.data.a.size());
                            std::ranges::copy(std::views::reverse(payload.data.b), std::back_inserter(update));
                            std::ranges::transform(payload.data.a, std::back_inserter(update),
                                [](OrderBookLevel level) { level.size = -level.size; return level; });
                            ob_sink->accept(std::move(update));
                        }
                    }
                }),

            datahub::make_data_adapter<WsOpResponse>(
                [](WsOpResponse&& resp) {
                    std::clog << "WebSocket [" << resp.conn_id << "] op=" << resp.op << " " << resp.ret_msg.value_or("") << std::endl;
                })),

                error_cb);
    m_public_stream->set_heartbeat(std::chrono::seconds(20), ping_message);
}

void ByBitDataManager::SetupPrivateDataSource()
{
    if (!m_credentials) {
        std::clog << "No API credentials, skipping private pipeline" << std::endl;
        return;
    }

    auto ref = weak_from_this();
    auto error_cb = [ref](std::exception_ptr e){ HandleError(ref, e); };
    auto executor = m_context->io().get_executor();
    const std::string api_base = http_base(m_config);

    auto order_acceptor  = m_private_order_sink->data_acceptor<std::deque<Order>>();
    auto trade_acceptor  = m_private_trade_sink->data_acceptor<std::deque<Trade>>();
    auto wallet_acceptor = m_wallet_feed->data_acceptor<std::deque<WalletBalance>>();
    auto ack_acceptor    = m_order_ack_feed->data_acceptor<std::deque<OrderAck>>();

    m_place_order.emplace(datahub::make_json_body_encoder<OrderRequest>(
        signed_query::create(m_context, connect::http::verb::post, api_base + API_ORDER_CREATE, rest_signer{*m_credentials},
            datahub::make_data_dispatcher(executor,
                datahub::make_data_adapter<ApiResponse<PlaceOrderResult>>([ack_acceptor](ApiResponse<PlaceOrderResult>&& response) mutable {
                    OrderAck ack{.orderLinkId = response.result.orderLinkId.value_or(std::string{}), .orderId = std::move(response.result.orderId), .retCode = response.retCode, .retMsg = std::move(response.retMsg)};
                    ack_acceptor(std::deque<OrderAck>{std::move(ack)});
                })),
            error_cb)));

    m_cancel_order.emplace(datahub::make_json_body_encoder<CancelOrderRequest>(
        signed_query::create(m_context, connect::http::verb::post, api_base + API_ORDER_CANCEL, rest_signer{*m_credentials},
            [](std::string&& response_json) { std::clog << "CancelOrder response: " << response_json << std::endl; },
            error_cb)));

    m_query_open_orders.emplace(datahub::make_url_query_encoder<OrderFilter>(
        signed_query::create(m_context, connect::http::verb::get, api_base + API_ORDER_LIST, rest_signer{*m_credentials},
            datahub::make_data_dispatcher(executor,
                datahub::make_data_adapter<ApiResponse<ListResult<Order>>>([order_acceptor](ApiResponse<ListResult<Order>>&& response) mutable {
                    order_acceptor(std::move(response.result.list));
                })),
            error_cb)));

    m_query_executions.emplace(datahub::make_url_query_encoder<ExecutionFilter>(
        signed_query::create(m_context, connect::http::verb::get, api_base + API_EXECUTION_LIST, rest_signer{*m_credentials},
            datahub::make_data_dispatcher(executor,
                datahub::make_data_adapter<ApiResponse<ListResult<Trade>>>([trade_acceptor](ApiResponse<ListResult<Trade>>&& response) mutable {
                    trade_acceptor(std::move(response.result.list));
                })),
            error_cb)));

    m_query_wallet.emplace(datahub::make_url_query_encoder<WalletFilter>(
        signed_query::create(m_context, connect::http::verb::get, api_base + API_WALLET_BALANCE, rest_signer{*m_credentials},
            datahub::make_data_dispatcher(executor,
                datahub::make_data_adapter<ApiResponse<ListResult<WalletBalance>>>([wallet_acceptor](ApiResponse<ListResult<WalletBalance>>&& response) mutable {
                    wallet_acceptor(std::move(response.result.list));
                })),
            error_cb)));

    m_private_stream = private_stream_type::create(m_context, stream_base(m_config) + STREAM_PRIVATE, ws_authenticator{*m_credentials},
        datahub::make_data_dispatcher(executor,
            datahub::make_data_adapter<WsPrivatePayload<std::deque<Order>>>([order_acceptor](WsPrivatePayload<std::deque<Order>>&& payload) mutable
                { order_acceptor(std::move(payload.data)); }),
            datahub::make_data_adapter<WsPrivatePayload<std::deque<Trade>>>([trade_acceptor](WsPrivatePayload<std::deque<Trade>>&& payload) mutable
                { trade_acceptor(std::move(payload.data)); }),
            datahub::make_data_adapter<WsPrivatePayload<std::deque<WalletBalance>>>([wallet_acceptor](WsPrivatePayload<std::deque<WalletBalance>>&& payload) mutable
                { wallet_acceptor(std::move(payload.data)); }),
            datahub::make_data_adapter<WsOpResponse>([](WsOpResponse&& resp) {
                std::clog << "Private WebSocket [" << resp.conn_id << "] op=" << resp.op << " success=" << resp.success << " " << resp.ret_msg.value_or("") << std::endl;
            })),
        error_cb);

    (*m_private_stream)(subscribe_message("order"));
    (*m_private_stream)(subscribe_message("execution"));
    (*m_private_stream)(subscribe_message("wallet"));
    m_private_stream->set_heartbeat(std::chrono::seconds(20), ping_message);
}

// ─── IDataController subscriptions ───────────────────────────────────────────

void ByBitDataManager::SubscribeInstrumentList(std::weak_ptr<IDataController::instruments_feed_type::subscription_type> sub)
{
    m_instrument_feed->subscribe(std::move(sub));
    (*m_instruments_query)();
}

void ByBitDataManager::SubscribeInstrument(std::string symbol, std::weak_ptr<public_trades_feed_type::subscription_type> trade_sub)
{
    auto& streams = m_pubdata_accept[symbol];
    auto ref = weak_from_this();
    auto error_cb = [ref](std::exception_ptr e){ HandleError(ref, e); };

    // Materialise the order-book stream once per symbol and attach a manager-owned (TBD-handler)
    // consumer built the same way as the public-trade subscription — a datahub::make_subscription
    // over the feed's native cache. The feed keeps only a weak_ptr, so the manager holds the shared.
    if (!streams.ob_feed) {
        streams.ob_feed = orderbook_feed_type::create();
        streams.ob_sink = datahub::make_data_sink(
            OrderBook::Create(streams.ob_feed->template data_acceptor<std::deque<OrderBookLevel>>()),
            [](orderbook_sink_type::cache_type&&) {},
            error_cb);
        streams.ob_consumer = datahub::make_subscription<orderbook_feed_type::cache_type>(
            [](datahub::update_kind, const orderbook_feed_type::cache_type&) { /* TBD order-book consumption */ });
        streams.ob_feed->subscribe(streams.ob_consumer);
        (*m_public_stream)(subscribe_message("orderbook.50." + symbol));
    }

    // Materialise the public-trade stream once per symbol.
    if (!streams.pt_feed) {
        streams.pt_feed = pubtrade_feed_type::create();
        auto model = datahub::data_model<PublicTrade, &PublicTrade::execId>::create(m_db, m_db_strand, "_" + symbol);
        streams.pt_sink = datahub::make_data_sink(std::move(model), streams.pt_feed->data_acceptor<std::deque<PublicTrade>>(), std::move(error_cb));
        (*m_public_stream)(subscribe_message("publicTrade." + symbol));
    }

    // Wire the caller-owned subscription to the public-trade feed. The feed keeps a weak_ptr and
    // delivers an immediate snapshot if its cache is already populated; the subscriber (panel)
    // owns the shared_ptr, so dropping it unsubscribes. An already-expired sub is simply ignored.
    streams.pt_feed->subscribe(std::move(trade_sub));
}

void ByBitDataManager::SubscribeOrders(std::weak_ptr<IDataController::private_orders_feed_type::subscription_type> sub)
{
    m_private_order_feed->subscribe(std::move(sub));
    if (m_query_open_orders) (*m_query_open_orders)(OrderFilter{.category = Category::Spot});
}

void ByBitDataManager::SubscribeTrades(std::weak_ptr<IDataController::private_trades_feed_type::subscription_type> sub)
{
    m_private_trade_feed->subscribe(std::move(sub));
    if (m_query_executions) (*m_query_executions)(ExecutionFilter{.category = Category::Spot});
}

void ByBitDataManager::SubscribeOrderAcks(std::weak_ptr<order_ack_feed_type::subscription_type> sub)
{
    m_order_ack_feed->subscribe(std::move(sub));
}

void ByBitDataManager::SubscribeWallet(std::weak_ptr<wallet_feed_type::subscription_type> sub)
{
    m_wallet_feed->subscribe(std::move(sub));
    if (m_query_wallet) (*m_query_wallet)(WalletFilter{.accountType = AccountType::UNIFIED});
}

// ─── Order management ─────────────────────────────────────────────────────────

std::string ByBitDataManager::PlaceOrder(OrderRequest request)
{
    if (!m_place_order) throw std::runtime_error("PlaceOrder: no API credentials configured");

    if (!request.orderLinkId) request.orderLinkId = generate_order_link_id();
    std::string order_link_id = *request.orderLinkId;
    (*m_place_order)(std::move(request));
    return order_link_id;
}

void ByBitDataManager::CancelOrder(std::string orderId, std::string symbol)
{
    if (!m_cancel_order) throw std::runtime_error("CancelOrder: no API credentials configured");

    (*m_cancel_order)(CancelOrderRequest{.category = "spot", .symbol = std::move(symbol), .orderId = std::move(orderId)});
}

} // scratcher::bybit
