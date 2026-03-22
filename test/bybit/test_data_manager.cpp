// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
//
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b21tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <future>
#include <iostream>

#include "scheduler.hpp"
#include "exchange_config.hpp"
#include "data/bybit/data_manager.hpp"

using namespace scratcher;
using namespace scratcher::bybit;

namespace {

struct MockExchangeConfig : IExchangeConfig {
    std::string mHttpHost    = "api.bybit.com";
    std::string mHttpPort    = "443";
    std::string mStreamHost  = "stream.bybit.com";
    std::string mStreamPort  = "443";
    std::string mApiKey;
    std::string mApiSecret;

    const std::string& HttpHost() const override { return mHttpHost; }
    const std::string& HttpPort() const override { return mHttpPort; }
    const std::string& StreamHost() const override { return mStreamHost; }
    const std::string& StreamPort() const override { return mStreamPort; }
    const std::string& ApiKey() const override { return mApiKey; }
    const std::string& ApiSecret() const override { return mApiSecret; }
    bool HasApiCredentials() const override { return false; }
};

} // anonymous namespace

struct ByBitDataManagerFixture {
    std::shared_ptr<IExchangeConfig> config;
    std::shared_ptr<scheduler> scheduler;
    std::shared_ptr<SQLite::Database> db;
    std::shared_ptr<ByBitDataManager> manager;

    ByBitDataManagerFixture()
        : config(std::make_shared<MockExchangeConfig>())
        , scheduler(scheduler::create(2))
        , db(std::make_shared<SQLite::Database>(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE))
        , manager(ByBitDataManager::Create(scheduler, config, db))
    {}
};

static ByBitDataManagerFixture fixture;

TEST_CASE("ByBitDataManager receives instrument list", "[bybit][integration]")
{
    std::promise<size_t> promise;
    auto future = promise.get_future();

    fixture.manager->SubscribeInstrumentList([&promise](const std::deque<InstrumentInfo>& instruments) {
        static bool fired = false;
        if (!fired) {
            fired = true;
            std::clog << "Received " << instruments.size() << " instruments" << std::endl;
            promise.set_value(instruments.size());
        }
    });

    auto status = future.wait_for(std::chrono::seconds(10));
    REQUIRE(status == std::future_status::ready);
    REQUIRE(future.get() > 0);
}

TEST_CASE("ByBitDataManager receives public trades", "[bybit][integration]")
{
    std::promise<size_t> promise;
    auto future = promise.get_future();

    fixture.manager->SubscribePublicTrades("BTCUSDC", [&promise](const std::deque<PublicTrade>& trades) {
        static bool fired = false;
        if (!fired) {
            fired = true;
            std::clog << "Received " << trades.size() << " public trades" << std::endl;
            promise.set_value(trades.size());
        }
    });

    auto status = future.wait_for(std::chrono::seconds(25));
    REQUIRE(status == std::future_status::ready);
    REQUIRE(future.get() > 0);
}

TEST_CASE("ByBitDataManager receives orderbook", "[bybit][integration]")
{
    std::promise<size_t> promise;
    auto future = promise.get_future();

    fixture.manager->SubscribeOrderBook("BTCUSDC", [&promise](const std::vector<OrderBookLevel>& levels) {
        static bool fired = false;
        if (!fired) {
            fired = true;
            std::clog << "Received " << levels.size() << " orderbook levels" << std::endl;
            promise.set_value(levels.size());
        }
    });

    auto status = future.wait_for(std::chrono::seconds(25));
    REQUIRE(status == std::future_status::ready);
    REQUIRE(future.get() > 0);
}

namespace SQLite {
    void assertion_failed(const char* apFile, int apLine, const char* apFunc, const char* apExpr, const char* apMsg) {
        std::cerr << "SQLite assertion failed: " << apFile << ":" << apLine << " in " << apFunc << "() - " << apExpr;
        if (apMsg) std::cerr << " (" << apMsg << ")";
        std::cerr << std::endl;
        std::abort();
    }
}
