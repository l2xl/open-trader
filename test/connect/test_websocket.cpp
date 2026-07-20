// XCockpit
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <string>
#include <future>
#include <chrono>

#include "scheduler.hpp"
#include "connect/websocket.hpp"
#include "connect/connection_context.hpp"
#include <glaze/glaze.hpp>

using namespace scratcher;
using namespace scratcher::connect;

TEST_CASE("subscribepublic trades", "[connect][websocket][CONNECT-021]")
{
    // Create scheduler
    auto scheduler = scheduler::create(1);
    
    // Create connection context
    auto context = context::create(scheduler->io());
    
    // Create promise/future for synchronization
    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();
    
    // Create response handlers
    auto data_handler = [&response_promise](std::string message) {
        std::cout << "Received WebSocket message: " << message << std::endl;
        response_promise.set_value(message);
    };

    auto error_handler = [&response_promise](std::exception_ptr e) {
        response_promise.set_exception(e);
    };

    // Create WebSocket connection to ByBit public stream
    auto connection = websock_connection::create(context, "wss://stream.bybit.com/v5/public/spot", data_handler, error_handler);

    // Subscribe to BTCUSDT public trades
    // ByBit WebSocket subscription message format
    std::string subscription_message = R"({"op":"subscribe","args":["publicTrade.BTCUSDT"]})";

    // Setup subscription
    REQUIRE_NOTHROW((*connection)(subscription_message));
    
    // Wait for first message with timeout
    auto status = response_future.wait_for(std::chrono::seconds(25));
    REQUIRE(status == std::future_status::ready);

    // Get the response
    std::string response_body = response_future.get();

    // Verify we got a response
    REQUIRE_FALSE(response_body.empty());
}
