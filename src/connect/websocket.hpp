// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_WEBSOCK_CONNECTION
#define SCRATCHER_WEBSOCK_CONNECTION

#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <chrono>
#include <iostream>
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

struct no_session_policy {
    template<typename Conn>
    static boost::asio::awaitable<void> co_open(std::weak_ptr<Conn>) { co_return; }

    template<typename Conn>
    void operator()(Conn&, std::string&) const {}
};

/**
 * @brief Persistent WebSocket subscription transport parameterised by a SessionPolicy
 *
 * - Single connection instance is shared between multiple DataSinks
 * - Connection opens after first create() call
 * - Each operator() call sends a subscription message
 * - Connection remains open and distributes data to the handler
 *
 * SessionPolicy hooks:
 * - `co_open(weak_ptr<Conn>)` runs after the websocket handshake and before READY, so whatever it writes precedes every queued message
 * - `operator()(Conn&, std::string&)` runs before every write, including heartbeats
 */
template<typename SessionPolicy = no_session_policy>
class websock_connection : public std::enable_shared_from_this<websock_connection<SessionPolicy>>
{
    friend SessionPolicy;

    using websocket_stream = boost::beast::websocket::stream<boost::beast::ssl_stream<boost::beast::tcp_stream>>;

    enum class status {INIT, READY, STALE};

    SessionPolicy m_policy;
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

    std::function<std::string(size_t)> m_make_heartbeat_mesage = [](size_t){ return std::string{}; };
    std::shared_ptr<boost::asio::steady_timer> m_heartbeat_timer;
    std::chrono::seconds m_heartbeat_interval{0};
    std::chrono::steady_clock::time_point m_last_heartbeat;
    std::atomic<size_t> m_request_counter = 0;

    static boost::asio::awaitable<void> co_heartbeat_loop(std::weak_ptr<websock_connection>);
    static boost::asio::awaitable<void> co_exec_loop(std::weak_ptr<websock_connection>);
    static boost::asio::awaitable<void> co_open(std::shared_ptr<websock_connection>);
    static boost::asio::awaitable<std::string> co_read(std::weak_ptr<websock_connection>);
    static boost::asio::awaitable<void> co_send_loop(std::weak_ptr<websock_connection>);
    static boost::asio::awaitable<void> co_write(std::weak_ptr<websock_connection>, std::string payload);
    static boost::asio::awaitable<void> co_enqueue(std::shared_ptr<websock_connection>, std::string payload);

public:
    explicit websock_connection(std::shared_ptr<context> ctx, const std::string& url, SessionPolicy policy = {});
    virtual ~websock_connection() = default;

    template<typename DataAcceptor, typename ErrorHandler>
    static std::shared_ptr<websock_connection> create(std::shared_ptr<context> ctx, const std::string& url, SessionPolicy policy, DataAcceptor&& data_handler, ErrorHandler&& error_handler)
    {
        auto ws = std::static_pointer_cast<websock_connection>(
            std::make_shared<generic_handler<std::string, websock_connection, DataAcceptor, ErrorHandler, std::shared_ptr<context>, const std::string&, SessionPolicy>>(
                std::forward<DataAcceptor>(data_handler),
                std::forward<ErrorHandler>(error_handler),
                std::move(ctx), url, std::move(policy)));

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

    template<typename DataAcceptor, typename ErrorHandler>
    static std::shared_ptr<websock_connection> create(std::shared_ptr<context> ctx, const std::string& url, DataAcceptor&& data_handler, ErrorHandler&& error_handler)
    {
        return create(std::move(ctx), url, SessionPolicy{}, std::forward<DataAcceptor>(data_handler), std::forward<ErrorHandler>(error_handler));
    }

    virtual void handle_data(std::string&& data) = 0;
    virtual void handle_error(std::exception_ptr eptr) = 0;

    void set_heartbeat(std::chrono::seconds seconds, std::function<std::string(size_t number)> heartbeat_generator)
    {
        m_make_heartbeat_mesage = std::move(heartbeat_generator);
        m_heartbeat_interval = seconds;
        m_heartbeat_timer->expires_after(seconds);
    }

    void operator()(std::string message)
    {
        boost::asio::co_spawn(m_strand, co_enqueue(this->shared_from_this(), std::move(message)), m_common_handler);
    }
};


template<typename SessionPolicy>
websock_connection<SessionPolicy>::websock_connection(std::shared_ptr<context> ctx, const std::string& url, SessionPolicy policy)
    : m_policy(std::move(policy))
    , m_context(ctx)
    , m_strand(make_strand(ctx->io()))
    , m_send_channel(m_strand, 64)
    , m_heartbeat_timer(std::make_shared<boost::asio::steady_timer>(m_strand, std::chrono::steady_clock::time_point::max()))
    , m_last_heartbeat(std::chrono::steady_clock::time_point::min())
{
    try {
        auto parsed_url = boost::urls::parse_uri(url);

        std::string scheme = parsed_url.value().scheme();
        m_host = parsed_url.value().host();
        m_port = parsed_url.value().port();
        m_path_query = parsed_url.value().path();

        if (parsed_url.value().has_query())
            m_path_query += ("?" + parsed_url.value().query());

        if (m_port.empty()) {
            if (scheme == "wss")
                m_port = "443";
            else
                throw std::invalid_argument("Unsupported scheme: " + scheme);
        }
    }
    catch (...) {
        std::throw_with_nested(std::invalid_argument("Invalid URL: " + url));
    }
}

template<typename SessionPolicy>
boost::asio::awaitable<void> websock_connection<SessionPolicy>::co_enqueue(std::shared_ptr<websock_connection> self, std::string payload)
{
    try {
        co_await self->m_send_channel.async_send(boost::system::error_code{}, std::move(payload), boost::asio::use_awaitable);
    }
    catch (boost::system::system_error& e) {
        self->m_status = status::STALE;
        std::cerr << "WebSocket send-channel failed: " << e.what() << std::endl;
    }
    catch (std::exception& e) {
        self->m_status = status::STALE;
        std::cerr << "WebSocket send-channel failed: " << e.what() << std::endl;
    }
    catch (...) {
        self->m_status = status::STALE;
        std::cerr << "WebSocket send-channel failed (unknown error)" << std::endl;
    }
}

template<typename SessionPolicy>
boost::asio::awaitable<void> websock_connection<SessionPolicy>::co_heartbeat_loop(std::weak_ptr<websock_connection> ref)
{
    std::shared_ptr<boost::asio::steady_timer> heartbeat_timer;
    if (auto self = ref.lock()) {
        heartbeat_timer = self->m_heartbeat_timer;
    }
    else co_return;

    while (true) {
        try {
            co_await heartbeat_timer->async_wait(boost::asio::use_awaitable);

            if (auto self = ref.lock()) {
                if (self->m_status == status::STALE) co_return;

                std::string ping = self->m_make_heartbeat_mesage(++self->m_request_counter);
                if (!ping.empty()) {
                    co_await co_enqueue(self, std::move(ping));
                }
                if (self->m_heartbeat_interval.count() > 0) {
                    heartbeat_timer->expires_after(self->m_heartbeat_interval);
                }
            }
            else co_return;
        }
        catch (boost::system::system_error& e) {
            std::cerr << "Heartbeat error: " << e.what() << std::endl;
        }
        catch (std::exception& e) {
            std::cerr << "Heartbeat error: " << e.what() << std::endl;
        }
        catch (...) {
            std::cerr << "Heartbeat unknown error" << std::endl;
        }
    }
}

template<typename SessionPolicy>
boost::asio::awaitable<void> websock_connection<SessionPolicy>::co_write(std::weak_ptr<websock_connection> ref, std::string payload)
{
    std::shared_ptr<websocket_stream> stream;
    if (auto self = ref.lock()) {
        std::clog << "WebSocket write: " << payload << " ... " << std::flush;
        stream = self->m_websocket;
    }
    else co_return;

    co_await stream->async_write(boost::asio::buffer(payload), boost::asio::use_awaitable);

    if (auto self = ref.lock()) {
        std::clog << "ok" << std::endl;
        self->m_last_heartbeat = std::chrono::steady_clock::now();
    }
}

template<typename SessionPolicy>
boost::asio::awaitable<void> websock_connection<SessionPolicy>::co_send_loop(std::weak_ptr<websock_connection> ref)
{
    namespace this_coro = boost::asio::this_coro;

    while (true) {
        std::string payload;
        try {
            std::shared_ptr<websock_connection> self = ref.lock();
            if (!self) co_return;
            if (self->m_status == status::STALE) co_return;
            send_channel_t& channel = self->m_send_channel;
            self.reset();

            auto [ec, msg] = co_await channel.async_receive(boost::asio::as_tuple(boost::asio::use_awaitable));
            if (ec) co_return;
            payload = std::move(msg);
        }
        catch (...) {
            co_return;
        }

        for (;;) {
            auto self = ref.lock();
            if (!self) co_return;
            if (self->m_status == status::STALE) co_return;
            if (self->m_status == status::READY) break;

            boost::asio::steady_timer wait(co_await this_coro::executor, std::chrono::milliseconds(50));
            self.reset();
            co_await wait.async_wait(boost::asio::use_awaitable);
        }

        try {
            if (auto self = ref.lock()) {
                self->m_policy(*self, payload);
            }
            else co_return;

            co_await co_write(ref, std::move(payload));
        }
        catch (boost::system::error_code& ec) {
            if (auto self = ref.lock()) {
                self->m_status = status::STALE;
                std::cerr << "WebSocket write error: " << ec.message() << std::endl;
            }
            co_return;
        }
        catch (std::exception& e) {
            if (auto self = ref.lock()) {
                self->m_status = status::STALE;
                std::cerr << "WebSocket write unknown error: " << e.what() << std::endl;
            }
            co_return;
        }
    }
}

template<typename SessionPolicy>
boost::asio::awaitable<void> websock_connection<SessionPolicy>::co_exec_loop(std::weak_ptr<websock_connection> ref)
{
    namespace this_coro = boost::asio::this_coro;

    try {
        for (;;) {
            try {
                if (auto self = ref.lock()) {
                    co_await co_open(self);
                    break;
                }
                co_return;
            }
            catch (boost::system::error_code& ec) {
                std::cerr << "Connection error: " << ec.message() << std::endl;
            }
            catch (std::exception& e) {
                std::cerr << "Connection error: " << e.what() << std::endl;
            }
            co_await boost::asio::steady_timer(co_await this_coro::executor, std::chrono::milliseconds(250)).async_wait(boost::asio::use_awaitable);
        }

        for (;;) {
            std::string message = co_await co_read(ref);
            if (auto self = ref.lock()) {
                self->handle_data(std::move(message));
            }
            else {
                co_return;
            }
        }
    }
    catch (boost::system::error_code& ec) {
        if (auto self = ref.lock()) {
            self->m_status = status::STALE;
            std::cerr << "WebSocket error: " << ec.message() << std::endl;
            std::rethrow_exception(std::current_exception());
        }
    }
    catch (std::exception& e) {
        if (auto self = ref.lock()) {
            self->m_status = status::STALE;
            std::cerr << "WebSocket unknown error: " << e.what() << std::endl;
            std::rethrow_exception(std::current_exception());
        }
    }
    catch (...) {
        if (auto self = ref.lock()) {
            self->m_status = status::STALE;
            std::cerr << "WebSocket unknown error" << std::endl;
            throw std::runtime_error("WebSocket unknown error");
        }
    }
}

template<typename SessionPolicy>
boost::asio::awaitable<void> websock_connection<SessionPolicy>::co_open(std::shared_ptr<websock_connection> self)
{
    namespace this_coro = boost::asio::this_coro;

    if (auto ctx = self->m_context.lock())
    {
        auto resolved_endpoints = co_await context::co_resolve(ctx, self->m_host, self->m_port);

        if (self->m_websocket) {
            if (self->m_websocket->is_open()) {
                throw std::runtime_error("WebSocket already open");
            }
            self->m_websocket.reset();
        }

        auto websock = std::make_shared<websocket_stream>(co_await this_coro::executor, ctx->ssl());

        get_lowest_layer(*websock).expires_after(std::chrono::seconds(30));

        auto connect_result = co_await get_lowest_layer(*websock).async_connect(resolved_endpoints, boost::asio::use_awaitable);

        if (!SSL_set_tlsext_host_name(websock->next_layer().native_handle(), self->m_host.c_str())) {
            throw std::runtime_error("Failed to set SNI Hostname");
        }

        get_lowest_layer(*websock).expires_after(std::chrono::seconds(30));

        co_await websock->next_layer().async_handshake(boost::asio::ssl::stream_base::client, boost::asio::use_awaitable);

        // The websocket stream has its own timeout system; the tcp_stream timeout must be off from here on.
        get_lowest_layer(*websock).expires_never();

        websock->set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client));

        websock->set_option(boost::beast::websocket::stream_base::decorator(
            [](boost::beast::websocket::request_type& req)
            {
                req.set(boost::beast::http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-async-ssl");
            }));

        std::string host_port = self->m_host + ":" + std::to_string(connect_result.port());

        std::clog << "WebSocket handshake: " << host_port << " " << self->m_path_query << std::endl;

        co_await websock->async_handshake(host_port, self->m_path_query, boost::asio::use_awaitable);

        self->m_websocket = std::move(websock);
        self->m_last_heartbeat = std::chrono::steady_clock::now();

        co_await SessionPolicy::co_open(std::weak_ptr<websock_connection>{self});

        self->m_status = status::READY;

        std::clog << "WebSocket connection established" << std::endl;
    }
}

template<typename SessionPolicy>
boost::asio::awaitable<std::string> websock_connection<SessionPolicy>::co_read(std::weak_ptr<websock_connection> ref)
{
    boost::beast::flat_buffer buffer;

    std::shared_ptr<websocket_stream> websocket;
    for (;;) {
        if (auto self = ref.lock()) {
            if (self->m_status != status::READY)
                break;
            websocket = self->m_websocket;
        }

        co_await websocket->async_read(buffer, boost::asio::use_awaitable);

        if (buffer.size() != 0) {
            std::string data = boost::beast::buffers_to_string(buffer.data());
            buffer.clear();

            co_return data;
        }
    }

    if (auto self = ref.lock(); self && self->m_status != status::STALE) {
        status s = self->m_status;
        throw std::runtime_error("Stream has wrong status: " + std::to_string((int)s));
    }

    co_return std::string{};
}

} // namespace scratcher::connect

#endif // SCRATCHER_WEBSOCK_CONNECTION
