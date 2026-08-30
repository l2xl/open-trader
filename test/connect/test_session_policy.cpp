// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "scheduler.hpp"
#include "connect/websocket.hpp"
#include "connect/connection_context.hpp"
#include "tls_test_server.hpp"

using namespace scratcher;
using namespace scratcher::connect;
using scratcher::test::tls_test_server;

namespace {

constexpr char trade_message[] = R"({"topic":"publicTrade.BTCUSDT","data":[{"p":"100.5","v":"0.1"}]})";
constexpr char subscribe_message[] = R"({"op":"subscribe","args":["publicTrade.BTCUSDT"]})";
constexpr char second_subscribe_message[] = R"({"op":"subscribe","args":["publicTrade.ETHUSDT"]})";
constexpr char greeting_message[] = R"({"op":"auth","args":["key","expires","sign"]})";

class frame_log
{
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::vector<std::string> m_frames;

public:
    std::string record(const std::string& frame, const std::string& reply = {})
    {
        { std::lock_guard lock(m_mutex); m_frames.push_back(frame); }
        m_ready.notify_all();
        return reply;
    }

    std::vector<std::string> wait_for(size_t count, std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        m_ready.wait_for(lock, timeout, [&] { return m_frames.size() >= count; });
        return m_frames;
    }
};

struct greeting_policy {
    std::string greeting;

    template<typename Conn>
    static boost::asio::awaitable<void> co_open(std::weak_ptr<Conn> ref)
    {
        std::string greeting;
        if (auto self = ref.lock())
            greeting = self->m_policy.greeting;
        else
            co_return;
        co_await Conn::co_write(ref, std::move(greeting));
    }

    template<typename Conn>
    void operator()(Conn&, std::string&) const {}
};

struct tagging_policy {
    std::string tag;

    template<typename Conn>
    static boost::asio::awaitable<void> co_open(std::weak_ptr<Conn>) { co_return; }

    template<typename Conn>
    void operator()(Conn&, std::string& payload) const { payload = tag + payload; }
};

} // namespace

TEST_CASE("session opening hook message precedes every queued message", "[connect][websocket][policy][CONNECT-041]")
{
    frame_log log;
    tls_test_server server({}, [&log](const std::string& message) { return log.record(message); });

    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());

    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();
    auto data_handler = [&response_promise](std::string message) { response_promise.set_value(std::move(message)); };
    auto error_handler = [&response_promise](std::exception_ptr e) { response_promise.set_exception(e); };

    auto connection = websock_connection<greeting_policy>::create(context, server.ws_url("/private"), greeting_policy{greeting_message}, data_handler, error_handler);

    REQUIRE_NOTHROW((*connection)(subscribe_message));
    REQUIRE_NOTHROW((*connection)(second_subscribe_message));

    auto frames = log.wait_for(3, std::chrono::seconds(5));
    REQUIRE(frames == std::vector<std::string>{greeting_message, subscribe_message, second_subscribe_message});
}

TEST_CASE("default session policy sends nothing after the handshake", "[connect][websocket][policy][CONNECT-042]")
{
    frame_log log;
    tls_test_server server({}, [&log](const std::string& message) {
        return log.record(message, message.find("subscribe") != std::string::npos ? std::string{trade_message} : std::string{});
    });

    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());

    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();
    auto data_handler = [&response_promise](std::string message) { response_promise.set_value(std::move(message)); };
    auto error_handler = [&response_promise](std::exception_ptr e) { response_promise.set_exception(e); };

    auto connection = websock_connection<no_session_policy>::create(context, server.ws_url("/stream"), no_session_policy{}, data_handler, error_handler);

    REQUIRE_NOTHROW((*connection)(subscribe_message));

    REQUIRE(response_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    REQUIRE(response_future.get() == trade_message);

    auto frames = log.wait_for(2, std::chrono::milliseconds(300));
    REQUIRE(frames == std::vector<std::string>{subscribe_message});
}

TEST_CASE("session policy alters every outgoing payload including heartbeats", "[connect][websocket][policy][CONNECT-043]")
{
    frame_log log;
    tls_test_server server({}, [&log](const std::string& message) { return log.record(message); });

    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());

    std::promise<std::string> response_promise;
    auto data_handler = [&response_promise](std::string message) { response_promise.set_value(std::move(message)); };
    auto error_handler = [&response_promise](std::exception_ptr e) { response_promise.set_exception(e); };

    auto connection = websock_connection<tagging_policy>::create(context, server.ws_url("/stream"), tagging_policy{"[tagged]"}, data_handler, error_handler);
    connection->set_heartbeat(std::chrono::seconds(1), [](size_t number) { return "ping-" + std::to_string(number); });

    REQUIRE_NOTHROW((*connection)(subscribe_message));

    auto frames = log.wait_for(2, std::chrono::seconds(5));
    REQUIRE(frames.size() >= 2);
    CHECK(frames[0] == std::string("[tagged]") + subscribe_message);
    CHECK(frames[1] == "[tagged]ping-1");
}

TEST_CASE("default policy subscribes and delivers stream messages", "[connect][websocket][policy][CONNECT-044]")
{
    frame_log log;
    tls_test_server server({}, [&log](const std::string& message) {
        return log.record(message, message.find("subscribe") != std::string::npos ? std::string{trade_message} : std::string{});
    });

    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());

    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();
    auto data_handler = [&response_promise](std::string message) { response_promise.set_value(std::move(message)); };
    auto error_handler = [&response_promise](std::exception_ptr e) { response_promise.set_exception(e); };

    auto connection = websock_connection<>::create(context, server.ws_url("/stream"), data_handler, error_handler);

    REQUIRE_NOTHROW((*connection)(subscribe_message));

    REQUIRE(response_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    REQUIRE(response_future.get() == trade_message);
    REQUIRE(log.wait_for(1, std::chrono::seconds(1)) == std::vector<std::string>{subscribe_message});
}
