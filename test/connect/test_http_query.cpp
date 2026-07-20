// XCockpit
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>
#include <iostream>
#include <string>
#include <future>
#include <chrono>

#include "scheduler.hpp"
#include "connect/http_query.hpp"
#include "connect/connection_context.hpp"
#include "connect/connection_errors.hpp"
#include "data/bybit/entities/response.hpp"
#include <glaze/glaze.hpp>

using namespace scratcher;
using namespace scratcher::connect;
using namespace scratcher::bybit;

TEST_CASE("http_query", "[connect][http][CONNECT-011]")
{
    // Create scheduler
    auto scheduler = scheduler::create(1);
    
    // Create connection context
    auto context = context::create(scheduler->io());
    
    // Create promise/future for synchronization
    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();
    
    // Create response handlers
    auto data_handler = [&response_promise](std::string response) {
        std::cout << "Received response: " << response << std::endl;
        response_promise.set_value(response);
    };

    auto error_handler = [&response_promise](std::exception_ptr e) {
        response_promise.set_exception(e);
    };

    // Create JsonRpcConnection with handlers
    auto query = http_query::create(context, "https://api.bybit.com/v5/market/time", data_handler, error_handler);

    // Execute ping request using full URL
    REQUIRE_NOTHROW((*query)());

    // Wait for response with timeout
    auto status = response_future.wait_for(std::chrono::seconds(5));
    REQUIRE(status == std::future_status::ready);

    // Get the response
    std::string response_body = response_future.get();

    // Verify we got a response
    REQUIRE_FALSE(response_body.empty());
}

TEST_CASE("http_query bad host", "[connect][http][CONNECT-012]")
{
    // Create scheduler
    auto scheduler = scheduler::create(1);

    // Create connection context
    auto context = context::create(scheduler->io());

    // Create promise/future for synchronization
    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();

    // Create response handlers
    auto data_handler = [&response_promise](std::string response) {
        std::cout << "Received response: " << response << std::endl;
        response_promise.set_value(response);
    };

    auto error_handler = [&response_promise](std::exception_ptr e) {
        response_promise.set_exception(e);
    };

    // Create http_query with invalid host (.invalid TLD is reserved by RFC 2606 and guaranteed to never resolve)
    auto query = http_query::create(context, "https://api.bybit.invalid/v5/market/time", data_handler, error_handler);

    // Execute request using invalid URL
    REQUIRE_NOTHROW((*query)());

    // Wait for response with timeout (DNS resolution failure is fast)
    auto status = response_future.wait_for(std::chrono::seconds(5));
    REQUIRE(status == std::future_status::ready);

    // Should throw domain_error for host resolution failure
    REQUIRE_THROWS_AS(response_future.get(), domain_error);

}

TEST_CASE("http_query_404", "[connect][http][CONNECT-013]")
{
    // Create scheduler
    auto scheduler = scheduler::create(1);

    // Create connection context
    auto context = context::create(scheduler->io());

    // Create promise/future for synchronization
    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();

    // Create response handlers
    auto data_handler = [&response_promise](std::string response) {
        std::cout << "Received response: " << response << std::endl;
        response_promise.set_value(response);
    };

    auto error_handler = [&response_promise](std::exception_ptr e) {
        try {
            std::rethrow_exception(e);
        }
        catch (const std::exception& ex) {
            std::cerr << ex.what() << std::endl;
        }
        response_promise.set_exception(e);
    };

    // Create http_query with invalid path that returns 404
    auto query = http_query::create(context, "https://api.bybit.com/v0/market/time", data_handler, error_handler);

    // Execute request using invalid URL path
    REQUIRE_NOTHROW((*query)());

    // Wait for response with timeout
    auto status = response_future.wait_for(std::chrono::seconds(5));
    REQUIRE(status == std::future_status::ready);

    // Should throw http_client_error for 404 response
    REQUIRE_THROWS_AS(response_future.get(), http_client_error);
}

TEST_CASE("http_query_bybit_trades", "[connect][http][CONNECT-014]")
{
    // Create scheduler
    auto scheduler = scheduler::create(1);

    // Create connection context
    auto context = context::create(scheduler->io());

    // Create promise/future for synchronization
    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();

    // Create response handlers
    auto data_handler = [&response_promise](std::string response) {
        std::cout << "Received trades response: " << response << std::endl;
        response_promise.set_value(response);
    };

    auto error_handler = [&response_promise](std::exception_ptr e) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& ex) {
            std::cout << "Error: " << ex.what() << std::endl;
        }
        response_promise.set_exception(e);
    };

    // Same URL as data sink test
    auto query = http_query::create(context, "https://api.bybit.com/v5/market/recent-trade?category=spot&symbol=BTCUSDC", data_handler, error_handler);

    REQUIRE_NOTHROW((*query)());

    auto status = response_future.wait_for(std::chrono::seconds(10));
    REQUIRE(status == std::future_status::ready);

    std::string response_body = response_future.get();
    REQUIRE_FALSE(response_body.empty());
}
