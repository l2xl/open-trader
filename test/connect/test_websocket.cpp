// XCockpit
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <future>
#include <chrono>

#include "scheduler.hpp"
#include "connect/websocket.hpp"
#include "connect/connection_context.hpp"
#include "tls_test_server.hpp"

using namespace scratcher;
using namespace scratcher::connect;
using scratcher::test::tls_test_server;

namespace {

constexpr char trade_message[] = R"({"topic":"publicTrade.BTCUSDT","data":[{"p":"100.5","v":"0.1"}]})";

} // namespace

TEST_CASE("subscribepublic trades", "[connect][websocket][CONNECT-021]")
{
    tls_test_server server({}, [](const std::string& message) {
        return message.find("subscribe") != std::string::npos ? std::string{trade_message} : std::string{};
    });

    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());

    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();

    auto data_handler = [&response_promise](std::string message) {
        response_promise.set_value(std::move(message));
    };
    auto error_handler = [&response_promise](std::exception_ptr e) {
        response_promise.set_exception(e);
    };

    auto connection = websock_connection::create(context, server.ws_url("/stream"), data_handler, error_handler);

    REQUIRE_NOTHROW((*connection)(R"({"op":"subscribe","args":["publicTrade.BTCUSDT"]})"));

    auto status = response_future.wait_for(std::chrono::seconds(5));
    REQUIRE(status == std::future_status::ready);
    REQUIRE(response_future.get() == trade_message);
}
