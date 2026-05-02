// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
//
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b25tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>
#include <future>
#include <iostream>

#include "scheduler.hpp"
#include "cli11/CLI11.hpp"
#include "data/bybit/data_manager.hpp"
#include "data/bybit/bybit_config.hpp"

using namespace scratcher;
using namespace scratcher::bybit;

namespace {

struct TestConfig {
    CLI::App app;
    CLI::App* bybit;
    std::string http_host;
    std::string http_port;
    std::string stream_host;
    std::string stream_port;
    std::string api_key;
    std::string api_secret;

    TestConfig()
    {
        namespace keys = scratcher::bybit::config_keys;
        bybit = app.add_subcommand(keys::section);
        bybit->add_option(keys::http_host,   http_host)->default_val("api.bybit.com");
        bybit->add_option(keys::http_port,   http_port)->default_val("443");
        bybit->add_option(keys::stream_host, stream_host)->default_val("stream.bybit.com");
        bybit->add_option(keys::stream_port, stream_port)->default_val("443");
        bybit->add_option(keys::api_key,     api_key);
        bybit->add_option(keys::api_secret,  api_secret);

        const char* argv[] = {"test"};
        app.parse(1, argv);
    }
};

} // anonymous namespace

struct ByBitDataManagerFixture {
    TestConfig cfg;
    std::shared_ptr<scheduler> scheduler;
    std::shared_ptr<SQLite::Database> db;
    std::shared_ptr<ByBitDataManager> manager;

    ByBitDataManagerFixture()
        : scheduler(scheduler::create(2))
        , db(std::make_shared<SQLite::Database>(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE))
        , manager(ByBitDataManager::Create(scheduler, *cfg.bybit, db))
    {}
};

static ByBitDataManagerFixture fixture;

TEST_CASE("ByBitDataManager receives instrument list", "[bybit][integration]")
{
    std::promise<size_t> promise;
    auto future = promise.get_future();

    auto sub = datahub::make_data_subscription<std::deque<InstrumentInfo>>(
        [&promise](auto data) {
            static bool fired = false;
            if (!fired) {
                fired = true;
                const auto count = std::ranges::distance(data.second);
                std::clog << "Received " << count << " instruments" << std::endl;
                promise.set_value(static_cast<size_t>(count));
            }
        });

    fixture.manager->SubscribeInstrumentList(sub);

    auto status = future.wait_for(std::chrono::seconds(5));
    CHECK(status == std::future_status::ready);
    CHECK(future.get() > 0);
}

namespace SQLite {
    void assertion_failed(const char* apFile, int apLine, const char* apFunc, const char* apExpr, const char* apMsg) {
        std::cerr << "SQLite assertion failed: " << apFile << ":" << apLine << " in " << apFunc << "() - " << apExpr;
        if (apMsg) std::cerr << " (" << apMsg << ")";
        std::cerr << std::endl;
        std::abort();
    }
}
