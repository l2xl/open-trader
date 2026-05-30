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

#include <iostream>
#include <vector>
#include <chrono>

#include <catch2/catch_test_macros.hpp>

#include "buoy_candle.hpp"
#include "data_rectangle.hpp"

using namespace scratcher;


// Mock trade structure for testing
struct MockTrade {
    time_point trade_time;
    uint64_t price_points;
    uint64_t volume_points;
};

TEST_CASE("SingleBuoyAppend")
{
    BuoyCandleQuotes quotes(1000); // 1 second candle duration

    REQUIRE(quotes.buoy_duration() == 1000);
    
    // Create mock trade data
    std::vector<MockTrade> trades;
    time_point base_time = time_point();
    
    // Add some sample trades
    trades.push_back({base_time, 100, 10});
    trades.push_back({base_time + milliseconds(1000), 100, 10});
    trades.push_back({base_time + milliseconds(2000), 1000, 100});

    quotes.AppendTrades(trades, 2500, 50);
    
    // Check that data was processed
    //CHECK(quotes.first_buoy_timestamp() <= duration_cast<milliseconds>(base_time.time_since_epoch()).count());
    CHECK(quotes.quotes().size() == 2);

    CHECK(quotes.quotes().front().volume == 10);
    CHECK(quotes.quotes().front().min == 100);  // opening trade sets min=max=mean=price; the seeded prev close (50) is not memorised
    CHECK(quotes.quotes().front().max == 100);
    CHECK(quotes.quotes().front().mean == 100);
    CHECK(quotes.quotes().front().close == 100);

    CHECK(quotes.quotes().back().volume == 10);
    CHECK(quotes.quotes().back().min == 100);
    CHECK(quotes.quotes().back().max == 100);
    CHECK(quotes.quotes().back().mean == 100);
    CHECK(quotes.quotes().back().close == 100);

    CHECK(quotes.active_candle().volume == 100);
    CHECK(quotes.active_candle().min == 1000);  // lone trade in this period: zero-extent diamond at its own price
    CHECK(quotes.active_candle().max == 1000);
    CHECK(quotes.active_candle().mean == 1000);
    CHECK(quotes.active_candle().close == 1000);
}

TEST_CASE("TwoTradesAppend")
{
    BuoyCandleQuotes quotes(1000); // 1 second candle duration

    // Create mock trade data
    std::vector<MockTrade> trades;
    time_point base_time = time_point();

    // Add some sample trades
    trades.push_back({base_time + milliseconds(1000), 200, 10});
    trades.push_back({base_time + milliseconds(1500), 300, 100});

    quotes.AppendTrades(trades, 2000, 100);

    // Check that data was processed
    CHECK(quotes.quotes().size() == 1);

    CHECK(quotes.quotes().back().volume == 110);
    CHECK(quotes.quotes().back().min == 200);  // opens at the first trade (200); the carried prev close (100) no longer pulls min down
    CHECK(quotes.quotes().back().max == 300);
    CHECK(quotes.quotes().back().mean == 290);
    CHECK(quotes.quotes().back().close == 300);

    CHECK(quotes.active_candle().volume == 0);
    CHECK(quotes.active_candle().min == 300);
    CHECK(quotes.active_candle().max == 300);
    CHECK(quotes.active_candle().mean == 300);
    CHECK(quotes.active_candle().close == 300);  // empty buoy carries the previous close
}

TEST_CASE("SimpleActiveCandle")
{
    BuoyCandleQuotes quotes(1000); // 1 second candle duration

    std::vector<MockTrade> trades;
    time_point base_time = time_point() + milliseconds(1000);

    // Add some sample trades
    trades.push_back({base_time, 100, 10});
    trades.push_back({base_time + milliseconds(1000), 200, 20});
    trades.push_back({base_time + milliseconds(2000), 300, 30});


    quotes.AppendTrades(trades, 2000, 100);

    CHECK(quotes.first_buoy_timestamp() == 1000);
    CHECK(quotes.first_trade_timestamp() == 1000);
    CHECK(quotes.last_trade_timestamp() == 3000);

    CHECK(quotes.quotes().size() == 2);

    CHECK(quotes.quotes().front().volume == 10);
    CHECK(quotes.quotes().front().min == 100);
    CHECK(quotes.quotes().front().max == 100);
    CHECK(quotes.quotes().front().mean == 100);
    CHECK(quotes.quotes().front().close == 100);

    CHECK(quotes.quotes().back().volume == 20);
    CHECK(quotes.quotes().back().min == 200);  // opening trade (200) sets the floor; carried prev close (100) is not memorised
    CHECK(quotes.quotes().back().max == 200);
    CHECK(quotes.quotes().back().mean == 200);
    CHECK(quotes.quotes().back().close == 200);

    CHECK(quotes.active_candle().volume == 30);
    CHECK(quotes.active_candle().min == 300);  // lone trade in this period: zero-extent diamond at its own price
    CHECK(quotes.active_candle().max == 300);
    CHECK(quotes.active_candle().mean == 300);
    CHECK(quotes.active_candle().close == 300);
}

