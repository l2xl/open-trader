// Scratcher project
// Copyright (c) 2025 l2xl (l2xl/at/proton.me)
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

#include <memory>
#include <chrono>
#include <deque>
#include <iostream>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "scheduler.hpp"
#include "data/bybit/entities/instrument.hpp"
#include "data/bybit/entities/response.hpp"
#include "connect/connection_context.hpp"
#include "connect/http_query.hpp"
#include "datahub/data_sink.hpp"


using namespace scratcher;
using namespace datahub;

static std::string http_samples[] = {
  R"({"retCode":0,"retMsg":"OK","result":{"category":"spot","list":[{"symbol":"BTCUSDC","baseCoin":"BTC","quoteCoin":"USDC","symbolType":"normal","innovation":"0","status":"Trading","marginTrading":"none","stTag":"0","priceFilter":{"tickSize":"0.5"},"lotSizeFilter":{"basePrecision":"0.000001","quotePrecision":"0.01","minOrderQty":"0.00001","maxOrderQty":"10000","minOrderAmt":"10","maxOrderAmt":"500000","maxLimitOrderQty":"10000","maxMarketOrderQty":"5000","postOnlyMaxLimitOrderSize":"10000"},"riskParameters":{"priceLimitRatioX":"0.05","priceLimitRatioY":"0.05"}}]},"retExtInfo":{},"time":1761520794180})",
};

struct DataModelTestFixture {
    std::string url = "https://api.bybit.com/v5/market/recent-trade?category=spot&symbol=BTCUSDC";
    std::shared_ptr<SQLite::Database> db;
    std::shared_ptr<scheduler> scheduler;
    std::shared_ptr<connect::context> ctx;

    // Use in-memory database that persists for the lifetime of this object
    DataModelTestFixture() : db(std::make_shared<SQLite::Database>(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE))
        , scheduler(scheduler::create(1))
        , ctx(connect::context::create(scheduler->io()))
    {}

    ~DataModelTestFixture() = default;
};

// Global fixture instance that persists between tests
static DataModelTestFixture fixture;

TEST_CASE("Create model with suffix", "[bybit][instruments]")
{
    auto model = data_model<bybit::InstrumentInfo, &bybit::InstrumentInfo::symbol>::create(fixture.db, "bybit");

    CHECK(model->name().ends_with("bybit"));

    auto results = model->query();
    CHECK(results.empty());

    model->drop_table();
}

TEST_CASE("Write first", "[bybit][instruments]")
{
    auto model = data_model<bybit::InstrumentInfo, &bybit::InstrumentInfo::symbol>::create(fixture.db);

    // Container to collect results
    std::deque<bybit::InstrumentInfo> cache = model->query();

    auto sink = make_data_sink(model, [](const std::exception_ptr&) {});

    REQUIRE(sink != nullptr);

    sink->subscribe([&cache](const std::deque<bybit::InstrumentInfo>& entities) {
        std::ranges::copy(entities, std::back_inserter(cache));
    });

    auto entity_acceptor = sink->data_acceptor<std::deque<bybit::InstrumentInfo>>();

    auto resp_adapter = make_data_adapter<bybit::ApiResponse<bybit::ListResult<bybit::InstrumentInfoAPI>>>(
            [entity_acceptor](auto&& resp) mutable {
                std::cout << "Adapter: Processing response with " << resp.result.list.size() << " instruments" << std::endl;
                std::deque<bybit::InstrumentInfo> converted;
                for (const auto& item : resp.result.list) { converted.emplace_back(item); }
                entity_acceptor(std::move(converted));
            }
        );
    auto dispatcher = make_data_dispatcher(fixture.scheduler->io().get_executor(), resp_adapter);

    // auto query = connect::http_query::create(fixture.ctx, "https://api.bybit.com/v5/market/instruments-info",
    //     dispatcher,
    //     [&](std::exception_ptr e){
    //         if (e) {
    //             std::cout << "✗ HTTP query error" << std::endl;
    //             try {
    //                 completion_promise.set_exception(e);
    //             } catch (...) {}
    //         }
    //     }
    // );

    std::cout << "Dispatching JSON sample..." << std::endl;
    dispatcher(http_samples[0]);

    // Wait for async processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Verify cache results
    REQUIRE(cache.size() == 1);

    const auto& cached = cache.front();
    CHECK(cached.symbol == "BTCUSDC");
    CHECK(cached.baseCoin == "BTC");
    CHECK(cached.quoteCoin == "USDC");
    CHECK(cached.symbolType == "normal");
    CHECK(cached.innovation == "0");
    CHECK(cached.status == bybit::InstrumentStatus::Trading);
    CHECK(cached.marginTrading == "none");
    CHECK(cached.stTag == "0");
    CHECK(cached.tickSize == "0.5");
    CHECK(cached.basePrecision == "0.000001");
    CHECK(cached.quotePrecision == "0.01");
    CHECK(cached.minOrderQty == "0.00001");
    CHECK(cached.maxOrderQty == "10000");
    CHECK(cached.minOrderAmt == "10");
    CHECK(cached.maxOrderAmt == "500000");
    CHECK(cached.maxLimitOrderQty == "10000");
    CHECK(cached.maxMarketOrderQty == "5000");
    CHECK(cached.postOnlyMaxLimitOrderSize == "10000");
    CHECK(cached.priceLimitRatioX == "0.05");
    CHECK(cached.priceLimitRatioY == "0.05");

    // Query back from the model and verify field values
    auto queried = model->query();
    REQUIRE(queried.size() == 1);

    const auto& inst = queried.front();
    CHECK(inst.symbol == "BTCUSDC");
    CHECK(inst.baseCoin == "BTC");
    CHECK(inst.quoteCoin == "USDC");
    CHECK(inst.symbolType == "normal");
    CHECK(inst.innovation == "0");
    CHECK(inst.status == bybit::InstrumentStatus::Trading);
    CHECK(inst.marginTrading == "none");
    CHECK(inst.stTag == "0");
    CHECK(inst.tickSize == "0.5");
    CHECK(inst.basePrecision == "0.000001");
    CHECK(inst.quotePrecision == "0.01");
    CHECK(inst.minOrderQty == "0.00001");
    CHECK(inst.maxOrderQty == "10000");
    CHECK(inst.minOrderAmt == "10");
    CHECK(inst.maxOrderAmt == "500000");
    CHECK(inst.maxLimitOrderQty == "10000");
    CHECK(inst.maxMarketOrderQty == "5000");
    CHECK(inst.postOnlyMaxLimitOrderSize == "10000");
    CHECK(inst.priceLimitRatioX == "0.05");
    CHECK(inst.priceLimitRatioY == "0.05");
}

namespace SQLite {
    void assertion_failed(const char* apFile, int apLine, const char* apFunc, const char* apExpr, const char* apMsg) {
        std::cerr << "SQLite assertion failed: " << apFile << ":" << apLine << " in " << apFunc << "() - " << apExpr;
        if (apMsg) std::cerr << " (" << apMsg << ")";
        std::cerr << std::endl;
        std::abort(); // Or throw exception if you prefer
    }
}
