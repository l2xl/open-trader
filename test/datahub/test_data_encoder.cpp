// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glaze/glaze.hpp>

#include "engine/currency.hpp"
#include "datahub/data_encoder.hpp"

namespace encoder_test {

enum class Side { Buy, Sell };

struct Order {
    Side side;
    std::string symbol;
    scratcher::currency<uint64_t> qty;
    std::optional<scratcher::currency<uint64_t>> price;
    std::optional<int> limit;
    std::optional<bool> reduceOnly;
    std::optional<std::string> note;
};

struct Filter {
    std::optional<std::string> symbol;
    std::optional<int> limit;
};

struct Leg {
    std::string symbol;
    int qty;
};

struct Shaped {
    std::string id;
    std::string internal;
    Leg leg;
};

struct OrderView {
    std::string symbol;
    std::string qty;
};

} // namespace encoder_test

template<>
struct glz::meta<encoder_test::Side> {
    using enum encoder_test::Side;
    static constexpr auto value = enumerate("Buy", Buy, "Sell", Sell);
};

template<>
struct glz::meta<encoder_test::Shaped> {
    using T = encoder_test::Shaped;
    static constexpr auto value = object("orderId", &T::id, "leg", &T::leg);
};

namespace {

using namespace encoder_test;
using scratcher::currency;

struct request_log {
    std::vector<std::pair<std::string, std::string>> calls;
    void operator()(std::string query, std::string body) { calls.emplace_back(std::move(query), std::move(body)); }
};

Order full_order()
{
    return Order{Side::Sell, "BTCUSDT", currency<uint64_t>("0.00100"), currency<uint64_t>("100.5"), 42, true, "x"};
}

Order sparse_order()
{
    return Order{Side::Buy, "ETHUSDT", currency<uint64_t>("1"), std::nullopt, std::nullopt, std::nullopt, std::nullopt};
}

template<typename T>
std::string json_scalar(const T& value)
{
    std::string text = glz::write_json(value).value();
    if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
        return text.substr(1, text.size() - 2);
    return text;
}

} // namespace

TEST_CASE("json body encoder emits the entity as body with empty query", "[datahub][encoder][DATAHUB-061]")
{
    request_log log;
    auto encoder = datahub::make_json_body_encoder<Order>(std::ref(log));

    encoder(full_order());

    REQUIRE(log.calls.size() == 1);
    CHECK(log.calls[0].first.empty());
    CHECK(log.calls[0].second == R"({"side":"Sell","symbol":"BTCUSDT","qty":"0.00100","price":"100.5","limit":42,"reduceOnly":true,"note":"x"})");
}

TEST_CASE("json body encoder omits empty optionals", "[datahub][encoder][DATAHUB-062]")
{
    request_log log;
    auto encoder = datahub::make_json_body_encoder<Order>(std::ref(log));

    encoder(sparse_order());

    REQUIRE(log.calls.size() == 1);
    CHECK(log.calls[0].second == R"({"side":"Buy","symbol":"ETHUSDT","qty":"1"})");
    CHECK(log.calls[0].second.find("price") == std::string::npos);
    CHECK(log.calls[0].second.find("null") == std::string::npos);
}

TEST_CASE("json body encoder serialises the projection result", "[datahub][encoder][DATAHUB-063]")
{
    request_log log;
    auto encoder = datahub::make_json_body_encoder<Order>(std::ref(log), [](const Order& order) {
        return OrderView{order.symbol, order.qty.to_string()};
    });

    encoder(full_order());

    REQUIRE(log.calls.size() == 1);
    CHECK(log.calls[0].first.empty());
    CHECK(log.calls[0].second == R"({"symbol":"BTCUSDT","qty":"0.00100"})");
}

TEST_CASE("json body encoder follows glz::meta selection, renaming and nesting", "[datahub][encoder][DATAHUB-064]")
{
    request_log log;
    auto encoder = datahub::make_json_body_encoder<Shaped>(std::ref(log));

    encoder(Shaped{"o-1", "hidden", Leg{"BTCUSDT", 3}});

    REQUIRE(log.calls.size() == 1);
    CHECK(log.calls[0].second == R"({"orderId":"o-1","leg":{"symbol":"BTCUSDT","qty":3}})");
}

TEST_CASE("url query encoder emits key=value pairs in declaration order", "[datahub][encoder][DATAHUB-065]")
{
    request_log log;
    auto encoder = datahub::make_url_query_encoder<Order>(std::ref(log));

    encoder(full_order());

    REQUIRE(log.calls.size() == 1);
    CHECK(log.calls[0].first == "side=Sell&symbol=BTCUSDT&qty=0.00100&price=100.5&limit=42&reduceOnly=true&note=x");
    CHECK(log.calls[0].second.empty());
}

TEST_CASE("url query encoder omits empty optionals", "[datahub][encoder][DATAHUB-066]")
{
    request_log log;
    auto encoder = datahub::make_url_query_encoder<Order>(std::ref(log));

    encoder(sparse_order());

    REQUIRE(log.calls.size() == 1);
    CHECK(log.calls[0].first == "side=Buy&symbol=ETHUSDT&qty=1");
}

TEST_CASE("url query values carry the JSON scalar text without quotes", "[datahub][encoder][DATAHUB-067]")
{
    request_log log;
    auto encoder = datahub::make_url_query_encoder<Order>(std::ref(log));

    const Order order = full_order();
    encoder(Order{order});

    const std::string expected = "side=" + json_scalar(order.side)
                               + "&symbol=" + json_scalar(order.symbol)
                               + "&qty=" + json_scalar(order.qty)
                               + "&price=" + json_scalar(*order.price)
                               + "&limit=" + json_scalar(*order.limit)
                               + "&reduceOnly=" + json_scalar(*order.reduceOnly)
                               + "&note=" + json_scalar(*order.note);

    REQUIRE(log.calls.size() == 1);
    CHECK(log.calls[0].first == expected);
    CHECK(json_scalar(order.side) == "Sell");
    CHECK(json_scalar(order.qty) == "0.00100");
}

TEST_CASE("url query encoder percent-encodes reserved characters", "[datahub][encoder][DATAHUB-068]")
{
    request_log log;
    auto encoder = datahub::make_url_query_encoder<Order>(std::ref(log));

    encoder(Order{Side::Buy, "BTC-USDT_1.0~", currency<uint64_t>("1"), std::nullopt, std::nullopt, std::nullopt, "a b&c=d/e?f+g#h"});

    REQUIRE(log.calls.size() == 1);
    CHECK(log.calls[0].first == "side=Buy&symbol=BTC-USDT_1.0~&qty=1&note=a%20b%26c%3Dd%2Fe%3Ff%2Bg%23h");
}

TEST_CASE("url query encoder yields an empty request for an all-empty entity", "[datahub][encoder][DATAHUB-069]")
{
    request_log log;
    auto encoder = datahub::make_url_query_encoder<Filter>(std::ref(log));

    encoder(Filter{});

    REQUIRE(log.calls.size() == 1);
    CHECK(log.calls[0].first.empty());
    CHECK(log.calls[0].second.empty());
}

TEST_CASE("encoders invoke the acceptor once per entity with (query, body)", "[datahub][encoder][DATAHUB-070]")
{
    request_log body_log;
    request_log query_log;
    auto body_encoder = datahub::make_json_body_encoder<Filter>(std::ref(body_log));
    auto query_encoder = datahub::make_url_query_encoder<Filter>(std::ref(query_log));

    body_encoder(Filter{"BTCUSDT", std::nullopt});
    body_encoder(Filter{std::nullopt, 5});
    query_encoder(Filter{"BTCUSDT", std::nullopt});
    query_encoder(Filter{std::nullopt, 5});

    REQUIRE(body_log.calls.size() == 2);
    CHECK(body_log.calls[0] == std::pair<std::string, std::string>{"", R"({"symbol":"BTCUSDT"})"});
    CHECK(body_log.calls[1] == std::pair<std::string, std::string>{"", R"({"limit":5})"});

    REQUIRE(query_log.calls.size() == 2);
    CHECK(query_log.calls[0] == std::pair<std::string, std::string>{"symbol=BTCUSDT", ""});
    CHECK(query_log.calls[1] == std::pair<std::string, std::string>{"limit=5", ""});
}
