// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "scheduler.hpp"
#include "connect/http_query.hpp"
#include "connect/connection_context.hpp"
#include "tls_test_server.hpp"

using namespace scratcher;
using namespace scratcher::connect;
using scratcher::test::tls_test_server;

namespace {

constexpr char ok_body[] = R"({"ok":true})";
constexpr char order_body[] = R"({"symbol":"BTCUSDT","qty":"0.01"})";

const std::string token_header = "X-Test-Token";
const std::string target_header = "X-Test-Target";

tls_test_server::response_type json_response(const std::string& body)
{
    tls_test_server::response_type response{boost::beast::http::status::ok, 11};
    response.set(boost::beast::http::field::content_type, "application/json");
    response.body() = body;
    return response;
}

class request_log
{
    std::mutex m_mutex;
    std::vector<tls_test_server::request_type> m_requests;

public:
    tls_test_server::response_type record(const tls_test_server::request_type& request)
    {
        std::lock_guard lock(m_mutex);
        m_requests.push_back(request);
        return json_response(ok_body);
    }

    size_t size()
    {
        std::lock_guard lock(m_mutex);
        return m_requests.size();
    }

    tls_test_server::request_type at(size_t index)
    {
        std::lock_guard lock(m_mutex);
        return m_requests.at(index);
    }
};

class response_log
{
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::vector<std::string> m_responses;
    std::exception_ptr m_error;

public:
    void operator()(std::string response)
    {
        { std::lock_guard lock(m_mutex); m_responses.push_back(std::move(response)); }
        m_ready.notify_all();
    }

    void operator()(std::exception_ptr error)
    {
        { std::lock_guard lock(m_mutex); m_error = error; }
        m_ready.notify_all();
    }

    std::string wait(size_t index)
    {
        std::unique_lock lock(m_mutex);
        m_ready.wait_for(lock, std::chrono::seconds(5), [&] { return m_error || m_responses.size() > index; });
        if (m_error) std::rethrow_exception(m_error);
        if (m_responses.size() <= index) throw std::runtime_error("response timeout");
        return m_responses[index];
    }
};

std::set<std::string> header_names(const tls_test_server::request_type& request)
{
    std::set<std::string> names;
    for (const auto& field : request)
        names.insert(std::string(field.name_string()));
    return names;
}

struct token_policy {
    std::string token;
    std::shared_ptr<std::atomic<size_t>> invocations = std::make_shared<std::atomic<size_t>>(0);

    template<typename Query>
    void operator()(Query&, http_request& request) const
    {
        ++*invocations;
        request.headers.emplace_back(token_header, token);
        request.headers.emplace_back(target_header, request.target);
    }
};

using token_query = http_query<token_policy>;

} // namespace

TEST_CASE("default policy issues an untouched request", "[connect][http][policy][CONNECT-031]")
{
    request_log log;
    tls_test_server server([&log](const auto& request) { return log.record(request); });

    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());
    auto responses = std::make_shared<response_log>();

    auto query = http_query<>::create(context, server.http_url("/time"), [responses](std::string r) { (*responses)(std::move(r)); }, [responses](std::exception_ptr e) { (*responses)(e); });

    REQUIRE_NOTHROW((*query)());
    REQUIRE(responses->wait(0) == ok_body);

    REQUIRE(log.size() == 1);
    auto request = log.at(0);
    CHECK(request.method() == boost::beast::http::verb::get);
    CHECK(request.target() == "/time");
    CHECK(request.body().empty());
    CHECK(header_names(request) == std::set<std::string>{"Host"});
}

TEST_CASE("request policy is invoked once per request and its headers reach the server", "[connect][http][policy][CONNECT-032]")
{
    request_log log;
    tls_test_server server([&log](const auto& request) { return log.record(request); });

    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());
    auto responses = std::make_shared<response_log>();

    token_policy policy{"token-A"};
    auto query = token_query::create(context, boost::beast::http::verb::get, server.http_url("/private"), policy, [responses](std::string r) { (*responses)(std::move(r)); }, [responses](std::exception_ptr e) { (*responses)(e); });

    REQUIRE_NOTHROW((*query)("symbol=BTCUSDT"));
    REQUIRE(responses->wait(0) == ok_body);
    REQUIRE_NOTHROW((*query)("symbol=ETHUSDT"));
    REQUIRE(responses->wait(1) == ok_body);

    CHECK(policy.invocations->load() == 2);
    REQUIRE(log.size() == 2);
    for (size_t i = 0; i < 2; ++i) {
        auto request = log.at(i);
        CHECK(request[token_header] == "token-A");
        CHECK(request[target_header] == request.target());
    }
    CHECK(log.at(0).target() == "/private?symbol=BTCUSDT");
    CHECK(log.at(1).target() == "/private?symbol=ETHUSDT");
}

TEST_CASE("policy value passed to create is the one invoked", "[connect][http][policy][CONNECT-033]")
{
    request_log log;
    tls_test_server server([&log](const auto& request) { return log.record(request); });

    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());
    auto responses = std::make_shared<response_log>();

    token_policy first{"token-first"};
    token_policy second{"token-second"};
    auto data_handler = [responses](std::string r) { (*responses)(std::move(r)); };
    auto error_handler = [responses](std::exception_ptr e) { (*responses)(e); };

    auto first_query = token_query::create(context, boost::beast::http::verb::get, server.http_url("/first"), first, data_handler, error_handler);
    auto second_query = token_query::create(context, boost::beast::http::verb::get, server.http_url("/second"), second, data_handler, error_handler);

    REQUIRE_NOTHROW((*first_query)());
    REQUIRE(responses->wait(0) == ok_body);
    REQUIRE_NOTHROW((*second_query)());
    REQUIRE(responses->wait(1) == ok_body);

    REQUIRE(log.size() == 2);
    auto first_request = log.at(0);
    auto second_request = log.at(1);
    CHECK(first_request.target() == "/first");
    CHECK(first_request[token_header] == "token-first");
    CHECK(second_request.target() == "/second");
    CHECK(second_request[token_header] == "token-second");
    CHECK(first.invocations->load() == 1);
    CHECK(second.invocations->load() == 1);
}

TEST_CASE("query string goes to the target and body to the payload", "[connect][http][policy][CONNECT-034]")
{
    request_log log;
    tls_test_server server([&log](const auto& request) { return log.record(request); });

    auto scheduler = scheduler::create(1);
    auto context = context::create(scheduler->io());
    auto responses = std::make_shared<response_log>();

    auto query = http_query<>::create(context, boost::beast::http::verb::post, server.http_url("/v5/order/create"), [responses](std::string r) { (*responses)(std::move(r)); }, [responses](std::exception_ptr e) { (*responses)(e); });

    REQUIRE_NOTHROW((*query)("category=spot&symbol=BTCUSDT", order_body));
    REQUIRE(responses->wait(0) == ok_body);

    REQUIRE(log.size() == 1);
    auto request = log.at(0);
    CHECK(request.method() == boost::beast::http::verb::post);
    CHECK(request.target() == "/v5/order/create?category=spot&symbol=BTCUSDT");
    CHECK(request.body() == order_body);
    CHECK(request[boost::beast::http::field::content_type] == "application/json");
}
