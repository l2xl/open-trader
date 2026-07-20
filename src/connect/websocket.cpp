// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "websocket.hpp"
#include <iostream>
#include <chrono>

namespace scratcher::connect {

using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::use_awaitable;

namespace this_coro =  boost::asio::this_coro;


websock_connection::websock_connection(std::shared_ptr<context> ctx, const std::string& url)
    : m_context(ctx)
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

void websock_connection::set_heartbeat(std::chrono::seconds seconds, std::function<std::string(size_t number)> heartbeat_generator)
{
    m_make_heartbeat_mesage = std::move(heartbeat_generator);
    m_heartbeat_interval = seconds;
    m_heartbeat_timer->expires_after(seconds);
}

void websock_connection::operator()(std::string message)
{
    co_spawn(m_strand, co_enqueue(shared_from_this(), std::move(message)), m_common_handler);
}

boost::asio::awaitable<void> websock_connection::co_enqueue(std::shared_ptr<websock_connection> self, std::string payload)
{
    try {
        co_await self->m_send_channel.async_send(boost::system::error_code{}, std::move(payload), use_awaitable);
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

boost::asio::awaitable<void> websock_connection::co_heartbeat_loop(std::weak_ptr<websock_connection> ref)
{
    std::shared_ptr<boost::asio::steady_timer> heartbeat_timer;
    if (auto self = ref.lock()) {
        heartbeat_timer = self->m_heartbeat_timer;
    }
    else co_return;

    while (true) {
        try {
            co_await heartbeat_timer->async_wait(use_awaitable);

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

boost::asio::awaitable<void> websock_connection::co_send_loop(std::weak_ptr<websock_connection> ref)
{
    namespace this_coro = boost::asio::this_coro;

    while (true) {
        std::string payload;
        try {
            send_channel_t* channel = nullptr;
            if (auto self = ref.lock()) {
                if (self->m_status == status::STALE) co_return;
                channel = &self->m_send_channel;
            }
            else co_return;

            auto [ec, msg] = co_await channel->async_receive(boost::asio::as_tuple(use_awaitable));
            if (ec) co_return;
            payload = std::move(msg);
        }
        catch (...) {
            co_return;
        }

        // Wait for the websocket to come up before draining the first payload.
        for (;;) {
            auto self = ref.lock();
            if (!self) co_return;
            if (self->m_status == status::STALE) co_return;
            if (self->m_status == status::READY) break;

            boost::asio::steady_timer wait(co_await this_coro::executor, std::chrono::milliseconds(50));
            self.reset();
            co_await wait.async_wait(use_awaitable);
        }

        try {
            std::shared_ptr<websocket_stream> stream;
            if (auto self = ref.lock()) {
                std::clog << "WebSocket write: " << payload << " ... " << std::flush;
                stream = self->m_websocket;
            }
            else co_return;

            co_await stream->async_write(boost::asio::buffer(payload), use_awaitable);

            if (auto self = ref.lock()) {
                std::clog << "ok" << std::endl;
                self->m_last_heartbeat = std::chrono::steady_clock::now();
            }
            else co_return;
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

boost::asio::awaitable<void> websock_connection::co_exec_loop(std::weak_ptr<websock_connection> ref)
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
            co_await boost::asio::steady_timer(co_await this_coro::executor, std::chrono::milliseconds(250)).async_wait(use_awaitable);
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

boost::asio::awaitable<void> websock_connection::co_open(std::shared_ptr<websock_connection> self)
{
    if (auto context = self->m_context.lock())
    {
        // Resolve host
        auto resolved_endpoints = co_await context::co_resolve(context, self->m_host, self->m_port);

        if (self->m_websocket) {
            if (self->m_websocket->is_open()) {
                throw std::runtime_error("WebSocket already open");
            }
            self->m_websocket.reset();
        }

        auto websock = std::make_shared<websocket_stream>(co_await this_coro::executor, context->ssl());

        get_lowest_layer(*websock).expires_after(std::chrono::seconds(30));

        auto connect_result = co_await get_lowest_layer(*websock).async_connect(resolved_endpoints, use_awaitable);

        // Set SNI hostname for SSL
        if (!SSL_set_tlsext_host_name(websock->next_layer().native_handle(), self->m_host.c_str())) {
            throw std::runtime_error("Failed to set SNI Hostname");
        }

        // Set timeout for SSL handshake
        get_lowest_layer(*websock).expires_after(std::chrono::seconds(30));

        // Perform SSL handshake
        co_await websock->next_layer().async_handshake(ssl::stream_base::client, use_awaitable);

        // Turn off the timeout on the tcp_stream, because the websocket stream has its own timeout system
        get_lowest_layer(*websock).expires_never();

        // Set suggested timeout settings for the websocket
        websock->set_option(boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client));

        // Set user agent
        websock->set_option(boost::beast::websocket::stream_base::decorator(
            [](boost::beast::websocket::request_type& req)
            {
                req.set(boost::beast::http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) + " websocket-client-async-ssl");
            }));

        std::string host_port = self->m_host + ":" + std::to_string(connect_result.port());

        std::clog << "WebSocket handshake: " << host_port << " " << self->m_path_query << std::endl;

        // Perform WebSocket handshake
        co_await websock->async_handshake(host_port, self->m_path_query, use_awaitable);

        self->m_websocket = std::move(websock);
        self->m_last_heartbeat = std::chrono::steady_clock::now();
        self->m_status = status::READY;

        std::clog << "WebSocket connection established" << std::endl;
    }
}

boost::asio::awaitable<std::string> websock_connection::co_read(std::weak_ptr<websock_connection> ref)
{
    boost::beast::flat_buffer buffer;

    std::shared_ptr<websocket_stream> websocket;
    for (;;) {
        if (auto self = ref.lock()) {
            if (self->m_status != status::READY)
                break;
            websocket = self->m_websocket;
        }

        co_await websocket->async_read(buffer, use_awaitable);

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
