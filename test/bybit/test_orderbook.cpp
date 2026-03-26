// Scratcher project
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
//
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b25tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7

#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <vector>
#include <string>
#include <ranges>
#include <iostream>
#include <thread>
#include <atomic>
#include <future>

#include <glaze/glaze.hpp>

#include "scheduler.hpp"
#include "connect/websocket.hpp"
#include "connect/connection_context.hpp"
#include "data/bybit/entities/response.hpp"
#include "data/bybit/entities/orderbookdata.hpp"
#include "data/orderbook.hpp"
#include "datahub/data_sink.hpp"

using namespace scratcher;
using namespace datahub;

namespace {

struct ExpectedLevel {
    std::string price;
    std::string size;   // expected size string after conversion (negative for asks)
};

struct TestStep {
    std::string json;
    size_t expected_level_count;
    std::vector<ExpectedLevel> expected_present;
    std::vector<std::string> expected_absent;
};

const std::vector<TestStep> test_vector = {
    // Step 0: Initial snapshot — 50 bids + 50 asks = 100 levels
    {
        R"({"topic":"orderbook.50.BTCUSDT","ts":1773748102871,"type":"snapshot","data":{"s":"BTCUSDT","b":[["74139.5","0.775301"],["74139.4","0.120764"],["74137.8","0.17199"],["74137","0.17909"],["74136","0.001597"],["74135.8","0.006743"],["74135.7","0.060329"],["74135.6","0.131283"],["74135.2","0.001"],["74134.6","0.001"],["74134.4","0.005"],["74134.1","0.031754"],["74134","0.024327"],["74133.2","0.001"],["74132.6","0.041177"],["74132.5","0.064321"],["74132.4","0.092624"],["74132.1","0.10955"],["74131.8","0.024327"],["74131.1","0.057718"],["74130.3","0.001358"],["74129.6","0.012978"],["74129.5","0.024327"],["74128.7","0.016216"],["74127.9","0.000102"],["74127.8","0.001"],["74127.1","0.040539"],["74126.5","0.03"],["74126.2","0.000638"],["74126.1","0.002"],["74126","0.00198"],["74124.3","0.004183"],["74123.4","0.050294"],["74123.3","0.040539"],["74123.2","0.009755"],["74122.7","0.0945"],["74120.5","0.000102"],["74120.3","0.03"],["74120.2","0.013487"],["74119.8","0.000638"],["74117.9","0.29539"],["74117.7","0.032754"],["74115.7","0.029086"],["74115.6","0.00868"],["74115.5","0.008598"],["74115.4","0.000674"],["74115.2","0.256174"],["74114.9","0.141799"],["74114.8","0.739667"],["74114.7","0.03576"]],"a":[["74139.6","0.25941"],["74139.7","0.001"],["74139.8","0.002662"],["74140","0.013791"],["74141","0.000102"],["74141.7","0.001"],["74142.2","0.001"],["74142.4","0.000773"],["74142.5","0.000329"],["74142.8","0.000195"],["74143.1","0.145131"],["74144","0.000252"],["74144.2","0.000855"],["74144.5","0.005"],["74144.9","0.008109"],["74145.1","0.040385"],["74145.2","0.129393"],["74145.4","0.000638"],["74146.2","0.000262"],["74146.7","0.000418"],["74146.9","0.008107"],["74147","0.0005"],["74148.4","0.000102"],["74148.6","0.053652"],["74148.7","0.149251"],["74149.2","0.0001"],["74149.6","0.002288"],["74150","0.002461"],["74150.9","0.02564"],["74151.8","0.000638"],["74151.9","0.000396"],["74152.2","0.029247"],["74152.3","0.05058"],["74152.4","0.000396"],["74152.9","0.005"],["74153.5","0.012978"],["74154","0.001357"],["74155.2","0.009971"],["74155.8","0.000102"],["74156.2","0.000272"],["74156.7","0.005"],["74157","0.013478"],["74157.9","0.000283"],["74158.2","0.00174"],["74159.1","0.013493"],["74159.2","0.000362"],["74159.5","0.03452"],["74159.7","0.000294"],["74159.8","0.000254"],["74160.1","0.10126"]],"u":17340415,"seq":102433698596},"cts":1773748102870})",
        100,
        {
            {"74139.5", "0.775301"},        // best bid
            {"74114.7", "0.03576"},          // worst bid
            {"74139.6", "-0.25941"},         // best ask (negative = ask)
            {"74160.1", "-0.10126"},         // worst ask
            {"74137.8", "0.17199"},          // inner bid
            {"74143.1", "-0.145131"},        // inner ask
            {"74124.3", "0.004183"},         // bid that will be removed in step 2
            {"74150.9", "-0.02564"},         // inner ask
        },
        {}
    },

    // Step 1: Delta — update single bid level
    {
        R"({"topic":"orderbook.50.BTCUSDT","ts":1773748102891,"type":"delta","data":{"s":"BTCUSDT","b":[["74139.5","0.767931"]],"a":[],"u":17340416,"seq":102433698608},"cts":1773748102885})",
        100,
        {
            {"74139.5", "0.767931"},         // updated bid
            {"74139.4", "0.120764"},         // neighbor unchanged
            {"74139.6", "-0.25941"},         // ask side unchanged
        },
        {}
    },

    // Step 2: Delta — update + remove + insert
    {
        R"({"topic":"orderbook.50.BTCUSDT","ts":1773748102910,"type":"delta","data":{"s":"BTCUSDT","b":[["74139.5","0.772943"],["74124.3","0"],["74114.6","0.0945"]],"a":[],"u":17340417,"seq":102433698621},"cts":1773748102909})",
        100,
        {
            {"74139.5", "0.772943"},         // updated
            {"74114.6", "0.0945"},           // newly inserted
            {"74126", "0.00198"},            // neighbor of removed, unchanged
            {"74123.4", "0.050294"},         // neighbor of removed, unchanged
            {"74114.7", "0.03576"},          // neighbor of inserted, unchanged
            {"74114.8", "0.739667"},         // neighbor of inserted, unchanged
        },
        {"74124.3"}                          // removed
    },
};

bool has_level(const std::vector<OrderBookLevel>& levels, const std::string& price_str)
{
    currency<int64_t> price(price_str);
    for (const auto& l : levels)
        if (l.price == price) return true;
    return false;
}

OrderBookLevel find_level(const std::vector<OrderBookLevel>& levels, const std::string& price_str)
{
    currency<int64_t> price(price_str);
    for (const auto& l : levels)
        if (l.price == price) return l;
    FAIL("Price level " + price_str + " not found in orderbook");
    return {};
}

} // anonymous namespace

TEST_CASE("Deserialize orderbook JSON samples", "[bybit][orderbook]")
{
    using bybit::WsApiPayload;
    using bybit::OrderBookData;

    for (size_t step = 0; step < test_vector.size(); ++step) {
        INFO("step " << step);
        const auto& tv = test_vector[step];

        WsApiPayload<OrderBookData> payload;
        auto ec = glz::read<glz::opts{.error_on_unknown_keys = false}>(payload, tv.json);
        REQUIRE_FALSE(ec);

        CHECK(payload.topic == "orderbook.50.BTCUSDT");
        CHECK(payload.ts > 0);

        if (step == 0) {
            CHECK(payload.type == "snapshot");
            CHECK(payload.data.b.size() == 50);
            CHECK(payload.data.a.size() == 50);
        } else {
            CHECK(payload.type == "delta");
        }

        CHECK(payload.data.s == "BTCUSDT");
        CHECK(payload.data.u > 0);
        CHECK(payload.data.seq > 0);
        CHECK(payload.cts > 0);

        for (const auto& level : payload.data.b) {
            CHECK(level.price.raw() > 0);
            CHECK(level.size.raw() >= 0);
        }
        for (const auto& level : payload.data.a) {
            CHECK(level.price.raw() > 0);
            CHECK(level.size.raw() >= 0);
        }
    }
}

TEST_CASE("Orderbook pipeline processes snapshot and deltas", "[bybit][orderbook]")
{
    using bybit::WsApiPayload;
    using bybit::OrderBookData;

    auto sched = scheduler::create(1);
    auto book = OrderBook::Create();

    auto ob_acceptor = book->data_acceptor();

    auto adapter = make_data_adapter<WsApiPayload<OrderBookData>>(
        [ob_acceptor](WsApiPayload<OrderBookData>&& payload) {
            std::ranges::for_each(payload.data.a, [](auto& ask){ ask.size.negate(); });

            if (payload.type == "snapshot")
                ob_acceptor(std::deque<OrderBookLevel>{});

            if (!payload.data.b.empty()) ob_acceptor(std::move(payload.data.b));
            if (!payload.data.a.empty()) ob_acceptor(std::move(payload.data.a));
            return true;
        });

    auto dispatcher = make_data_dispatcher(sched->io().get_executor(), std::move(adapter));

    for (size_t step = 0; step < test_vector.size(); ++step) {
        const auto& tv = test_vector[step];
        INFO("step " << step);

        dispatcher(tv.json);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        const auto& levels = book->Levels();

        REQUIRE(levels.size() == tv.expected_level_count);

        // Price-descending order invariant
        for (size_t i = 1; i < levels.size(); ++i) {
            REQUIRE(levels[i].price < levels[i - 1].price);
        }

        for (const auto& [price, size] : tv.expected_present) {
            INFO("  expected present: price=" << price << " size=" << size);
            REQUIRE(has_level(levels, price));
            CHECK(find_level(levels, price).size.to_string() == size);
        }

        for (const auto& price : tv.expected_absent) {
            INFO("  expected absent: price=" << price);
            CHECK_FALSE(has_level(levels, price));
        }
    }
}

TEST_CASE("Receive raw orderbook payloads from ByBit WebSocket", "[bybit][orderbook][integration]")
{
    constexpr size_t MESSAGES_TO_CAPTURE = 3;

    auto sched = scheduler::create(2);
    auto ctx = connect::context::create(sched->io());

    std::promise<std::deque<std::string>> promise;
    auto future = promise.get_future();
    auto captured = std::make_shared<std::deque<std::string>>();
    auto done = std::make_shared<std::atomic<bool>>(false);

    auto ws = connect::websock_connection::create(ctx, "wss://stream.bybit.com:443/v5/public/spot",
        [captured, done, &promise](std::string message) {
            if (done->load()) return;
            if (message.find("orderbook") != std::string::npos) {
                std::clog << "--- orderbook payload [" << captured->size() << "] ---\n" << message << "\n" << std::endl;
                captured->push_back(std::move(message));
                if (captured->size() >= MESSAGES_TO_CAPTURE) {
                    done->store(true);
                    promise.set_value(*captured);
                }
            }
        },
        [done, &promise](std::exception_ptr e) {
            if (!done->exchange(true)) {
                promise.set_exception(e);
            }
        });

    (*ws)(R"({"op":"subscribe","args":["orderbook.50.BTCUSDT"]})");

    auto status = future.wait_for(std::chrono::seconds(30));
    REQUIRE(status == std::future_status::ready);

    auto payloads = future.get();
    REQUIRE(payloads.size() == MESSAGES_TO_CAPTURE);
}
