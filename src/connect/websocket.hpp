// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_WEBSOCK_CONNECTION
#define SCRATCHER_WEBSOCK_CONNECTION

#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <stdexcept>
#include <boost/asio.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/url.hpp>


#include "connection_context.hpp"
#include "generic_handler.hpp"

namespace scratcher::connect {

/**
 * @brief WebSocketConnection implements persistent WebSocket subscriptions
 *
 * This connection type is designed for persistent subscriptions where:
 * - Single connection instance is shared between multiple DataSinks
 * - Connection opens after first create() call
 * - Each operator() call sends a subscription message
 * - Connection remains open and distributes data to the handler
 *
 * Handler callback types:
 * - std::function<void(std::string)> - receives JSON messages
 * - std::function<void(std::exception_ptr)> - receives errors
 */
class websock_connection : public std::enable_shared_from_this<websock_connection>
{
    using websocket_stream = boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;

    enum class status {INIT, READY, STALE};

    std::atomic<status> m_status = status::INIT;
    std::weak_ptr<context> m_context;
    boost::asio::strand<websocket_stream::executor_type> m_strand;

    std::function<void(std::exception_ptr)> m_common_handler;

    std::string m_host;
    std::string m_port;
    std::string m_path_query;

    std::shared_ptr<websocket_stream> m_websocket;

    // Outbound payload channel — single producer-consumer rendezvous between message
    // producers (operator() / heartbeat) and the lone send loop. Boost.Beast forbids
    // overlapping write_some_op initiations on the same stream, so all writes flow
    // through co_send_loop, which drains this channel one item at a time.
    using send_channel_t = boost::asio::experimental::channel<void(boost::system::error_code, std::string)>;
    send_channel_t m_send_channel;

    // Heartbeat management
    std::function<std::string(size_t)> m_make_heartbeat_mesage = [](size_t){ return std::string{}; };
    std::shared_ptr<boost::asio::steady_timer> m_heartbeat_timer;
    std::chrono::seconds m_heartbeat_interval{0};
    std::chrono::steady_clock::time_point m_last_heartbeat;
    std::atomic<size_t> m_request_counter = 0;

    // Async operations
    static boost::asio::awaitable<void> co_heartbeat_loop(std::weak_ptr<websock_connection>);
    static boost::asio::awaitable<void> co_exec_loop(std::weak_ptr<websock_connection>);
    static boost::asio::awaitable<void> co_open(std::shared_ptr<websock_connection>);
    static boost::asio::awaitable<std::string> co_read(std::weak_ptr<websock_connection>);
    static boost::asio::awaitable<void> co_send_loop(std::weak_ptr<websock_connection>);
    static boost::asio::awaitable<void> co_enqueue(std::shared_ptr<websock_connection>, std::string payload);

public:
    explicit websock_connection(std::shared_ptr<context> context, const std::string& url);
    virtual ~websock_connection() = default;

    /**
     * @brief Create WebSocketConnection instance with generic callable handlers
     * @tparam DataAcceptor Callable accepting std::string (e.g., data_dispatcher, data_adapter, lambda)
     * @tparam ErrorHandler Callable accepting std::exception_ptr
     * @param context Shared connection context with host resolution
     * @param url Full URL to request (e.g., "wss://api.bybit.com/v5/public/spot")
     * @param data_handler Handler that receives JSON messages (data_dispatcher, data_adapter, etc.)
     * @param error_handler Handler that receives errors
     */
    template<typename DataAcceptor, typename ErrorHandler>
    static std::shared_ptr<websock_connection> create(std::shared_ptr<context> ctx, const std::string& url, DataAcceptor&& data_handler, ErrorHandler&& error_handler)
    {
        auto ws = std::static_pointer_cast<websock_connection>(
            std::make_shared<generic_handler<std::string, websock_connection, DataAcceptor, ErrorHandler, std::shared_ptr<context>, const std::string&>>(
                std::forward<DataAcceptor>(data_handler),
                std::forward<ErrorHandler>(error_handler),
                std::move(ctx), url));

        std::weak_ptr<websock_connection> ref = ws;
        ws->m_common_handler = [ref](std::exception_ptr e) {
            if (e) {
                if (auto self = ref.lock()) {
                    self->handle_error(e);
                }
            }
        };

        boost::asio::co_spawn(ws->m_strand, co_exec_loop(ws), ws->m_common_handler);
        boost::asio::co_spawn(ws->m_strand, co_send_loop(ws), ws->m_common_handler);
        boost::asio::co_spawn(ws->m_strand, co_heartbeat_loop(ws), ws->m_common_handler);

        return ws;
    }

    virtual void handle_data(std::string&& data) = 0;
    virtual void handle_error(std::exception_ptr eptr) = 0;

    void set_heartbeat(std::chrono::seconds seconds, std::function<std::string(size_t number)>);

    void operator()(std::string message);
};

} // namespace scratcher::connect

#endif // SCRATCHER_WEBSOCK_CONNECTION
