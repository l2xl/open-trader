// XCockpit
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <future>
#include <chrono>

#include "scheduler.hpp"
#include "connect/http_query.hpp"
#include "connect/connection_context.hpp"
#include "connect/connection_errors.hpp"
#include "tls_test_server.hpp"

using namespace scratcher;
using namespace scratcher::connect;
using scratcher::test::tls_test_server;

namespace {

constexpr char time_body[] = R"({"ok":true})";
constexpr char trades_body[] = R"({"retCode":0,"result":{"list":[{"execId":"1","price":"100.5"}]}})";

tls_test_server::response_type json_response(const std::string& body)
{
    tls_test_server::response_type response{boost::beast::http::status::ok, 11};
    response.set(boost::beast::http::field::content_type, "application/json");
    response.body() = body;
    return response;
}

} // namespace

TEST_CASE("http_query", "[connect][http][CONNECT-011]")
{
    tls_test_server server([](const auto& request) {
        return request.target() == "/time" ? json_response(time_body)
                                           : tls_test_server::response_type{boost::beast::http::status::not_found, 11};
    });

    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());

    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();

    auto data_handler = [&response_promise](std::string response) {
        response_promise.set_value(std::move(response));
    };
    auto error_handler = [&response_promise](std::exception_ptr e) {
        response_promise.set_exception(e);
    };

    auto query = http_query::create(context, server.http_url("/time"), data_handler, error_handler);

    REQUIRE_NOTHROW((*query)());

    auto status = response_future.wait_for(std::chrono::seconds(5));
    REQUIRE(status == std::future_status::ready);
    REQUIRE(response_future.get() == time_body);
}

TEST_CASE("http_query bad host", "[connect][http][CONNECT-012]")
{
    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());

    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();

    auto data_handler = [&response_promise](std::string response) {
        response_promise.set_value(std::move(response));
    };
    auto error_handler = [&response_promise](std::exception_ptr e) {
        response_promise.set_exception(e);
    };

    // .invalid TLD is reserved by RFC 2606 and guaranteed to never resolve
    auto query = http_query::create(context, "https://api.bybit.invalid/v5/market/time", data_handler, error_handler);

    REQUIRE_NOTHROW((*query)());

    auto status = response_future.wait_for(std::chrono::seconds(5));
    REQUIRE(status == std::future_status::ready);
    REQUIRE_THROWS_AS(response_future.get(), domain_error);
}

TEST_CASE("http_query_404", "[connect][http][CONNECT-013]")
{
    tls_test_server server({});

    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());

    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();

    auto data_handler = [&response_promise](std::string response) {
        response_promise.set_value(std::move(response));
    };
    auto error_handler = [&response_promise](std::exception_ptr e) {
        response_promise.set_exception(e);
    };

    auto query = http_query::create(context, server.http_url("/no/such/path"), data_handler, error_handler);

    REQUIRE_NOTHROW((*query)());

    auto status = response_future.wait_for(std::chrono::seconds(5));
    REQUIRE(status == std::future_status::ready);
    REQUIRE_THROWS_AS(response_future.get(), http_client_error);
}

TEST_CASE("http_query_bybit_trades", "[connect][http][CONNECT-014]")
{
    tls_test_server server([](const auto& request) {
        return request.target() == "/v5/market/recent-trade?category=spot&symbol=BTCUSDC"
                   ? json_response(trades_body)
                   : tls_test_server::response_type{boost::beast::http::status::not_found, 11};
    });

    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());

    std::promise<std::string> response_promise;
    auto response_future = response_promise.get_future();

    auto data_handler = [&response_promise](std::string response) {
        response_promise.set_value(std::move(response));
    };
    auto error_handler = [&response_promise](std::exception_ptr e) {
        response_promise.set_exception(e);
    };

    auto query = http_query::create(context, server.http_url("/v5/market/recent-trade?category=spot&symbol=BTCUSDC"), data_handler, error_handler);

    REQUIRE_NOTHROW((*query)());

    auto status = response_future.wait_for(std::chrono::seconds(5));
    REQUIRE(status == std::future_status::ready);
    REQUIRE(response_future.get() == trades_body);
}
