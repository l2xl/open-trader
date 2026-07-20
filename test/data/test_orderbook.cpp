// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>

#include <vector>
#include <string>
#include <optional>

#include "orderbook.hpp"
#include "currency.hpp"

using namespace scratcher;

namespace {

// Build an OrderBookLevel directly from price/size strings.
// Negative size string = ask side (e.g. "-1.5"), zero = delete.
OrderBookLevel lvl(const char* price, const char* size)
{
    return {currency<int64_t>(price), currency<int64_t>(size)};
}

std::optional<OrderBookLevel> find_level(const OrderBook::cache_type& levels, const char* price_str)
{
    currency<int64_t> p(price_str);
    for (const auto& l : levels)
        if (l.price == p) return l;
    return std::nullopt;
}

struct Step {
    bool            snapshot_reset; // call accept({}) before update (signals new snapshot)
    std::vector<OrderBookLevel> update; // sorted ascending by price, as data_manager produces
    size_t          expected_count;
    struct Check { std::string price, size; };
    std::vector<Check>       present; // must exist with exact size
    std::vector<std::string> absent;  // must not exist (deleted)
};

// Update vectors mirror the data_manager format:
//   [bids reversed -> asc by price (positive sizes), asks -> asc by price (negative sizes)]
// No bid/ask price overlap -- exchange invariant.
const std::vector<Step> steps = {
    // Step 0: Initial snapshot -- 5 bids @ 96..100, 5 asks @ 101..105
    {
        true,
        {
            lvl( "96",  "0.5"),
            lvl( "97", "0.75"),
            lvl( "98",  "1.0"),
            lvl( "99",  "1.5"),
            lvl("100",  "2.0"),
            lvl("101", "-1.0"),
            lvl("102", "-1.5"),
            lvl("103", "-2.0"),
            lvl("104", "-0.5"),
            lvl("105", "-1.0"),
        },
        10,
        {
            { "96",  "0.5"},
            { "97", "0.75"},
            { "98",  "1.0"},
            { "99",  "1.5"},
            {"100",  "2.0"},
            {"101", "-1.0"},
            {"102", "-1.5"},
            {"103", "-2.0"},
            {"104", "-0.5"},
            {"105", "-1.0"},
        },
        {}
    },

    // Step 1: Delta -- delete bid(99) + ask(103), update bid(100->3.0) + ask(102->-2.5)
    {
        false,
        {
            lvl( "99",   "0"),   // delete bid   99
            lvl("100",  "3.0"),  // update bid  100
            lvl("102", "-2.5"),  // update ask  102
            lvl("103",   "0"),   // delete ask  103
        },
        8,
        {
            {"100", "3.0"}, {"102", "-2.5"},   // updated
            {"101", "-1.0"}, {"98", "1.0"},    // neighbours unchanged
        },
        {"103", "99"}
    },

    // Step 2: Delta -- delete bids(97,96) + asks(105,104), insert bid(99.5) + ask(103.5)
    {
        false,
        {
            lvl( "96",    "0"),   // delete bid   96
            lvl( "97",    "0"),   // delete bid   97
            lvl( "99.5",  "1.2"), // insert bid   99.5
            lvl("103.5", "-1.8"), // insert ask  103.5
            lvl("104",    "0"),   // delete ask  104
            lvl("105",    "0"),   // delete ask  105
        },
        6,
        {
            { "98",  "1.0"}, { "99.5",  "1.2"}, {"100",   "3.0"},   // bid side
            {"101", "-1.0"}, {"102",   "-2.5"}, {"103.5", "-1.8"},   // ask side
        },
        {"105", "104", "97", "96"}
    },

    // Step 3: New snapshot -- 2 bids + 2 asks, entirely different prices
    {
        true,
        {
            lvl("190",  "5.0"), lvl("200", "10.0"),
            lvl("210", "-3.0"), lvl("220",  "-2.0"),
        },
        4,
        {
            {"190",  "5.0"}, {"200", "10.0"},
            {"210", "-3.0"}, {"220",  "-2.0"},
        },
        {}
    },
};

auto require_level(const OrderBook::cache_type& levels, const char* price, const char* expected_size)
{
    auto found = find_level(levels, price);
    REQUIRE(found.has_value());
    CHECK(found->size.to_string() == expected_size);
}

} // anonymous namespace

TEST_CASE("OrderBook: price level side transition (bid<->ask at same price)", "[orderbook]")
{
    size_t call_count = 0;
    auto book = OrderBook::Create([&](OrderBook::cache_type&&) { ++call_count; });

    // Step 1: initial snapshot — bids @ 99, 100; asks @ 101, 102
    book->accept(std::vector<OrderBookLevel>{});
    book->accept(std::vector<OrderBookLevel>{
        lvl( "99",  "1.0"),
        lvl("100",  "2.0"),
        lvl("101", "-1.0"),
        lvl("102", "-0.5"),
    });
    REQUIRE(call_count == 1);
    REQUIRE(book->Levels().size() == 4);

    // Step 2: delta — highest bid (100) flips to ask at same price
    book->accept(std::vector<OrderBookLevel>{ lvl("100", "-1.5") });
    REQUIRE(call_count == 2);
    {
        const auto& levels = book->Levels();
        REQUIRE(levels.size() == 4);
        for (size_t j = 1; j < levels.size(); ++j)
            CHECK(levels[j - 1].price < levels[j].price);
        require_level(levels,  "99",  "1.0");
        require_level(levels, "100", "-1.5");  // was bid, now ask
        require_level(levels, "101", "-1.0");
        require_level(levels, "102", "-0.5");
    }

    // Step 3: delta — lowest ask (100) flips back to bid at same price
    book->accept(std::vector<OrderBookLevel>{ lvl("100", "2.5") });
    REQUIRE(call_count == 3);
    {
        const auto& levels = book->Levels();
        REQUIRE(levels.size() == 4);
        for (size_t j = 1; j < levels.size(); ++j)
            CHECK(levels[j - 1].price < levels[j].price);
        require_level(levels,  "99",  "1.0");
        require_level(levels, "100",  "2.5");  // was ask, bid again
        require_level(levels, "101", "-1.0");
        require_level(levels, "102", "-0.5");
    }
}

TEST_CASE("OrderBook merge: snapshot and delta sequence", "[orderbook]")
{
    size_t acceptor_call_count = 0;
    size_t acceptor_last_size  = 0;

    auto book = OrderBook::Create([&](OrderBook::cache_type&& snapshot) {
        ++acceptor_call_count;
        acceptor_last_size = snapshot.size();
    });

    for (size_t i = 0; i < steps.size(); ++i) {
        INFO("step " << i);
        const auto& s = steps[i];

        if (s.snapshot_reset)
            book->accept(std::vector<OrderBookLevel>{}); // resets; acceptor NOT called

        book->accept(s.update);

        const auto& levels = book->Levels();

        // Correct total count
        REQUIRE(levels.size() == s.expected_count);

        // Ascending price order invariant
        for (size_t j = 1; j < levels.size(); ++j)
            CHECK(levels[j - 1].price < levels[j].price);

        // Present with exact sizes
        for (const auto& [price, size] : s.present) {
            INFO("  present: " << price << " @ " << size);
            auto found = find_level(levels, price.c_str());
            REQUIRE(found.has_value());
            CHECK(found->size.to_string() == size);
        }

        // Deleted levels are gone
        for (const auto& price : s.absent) {
            INFO("  absent: " << price);
            CHECK_FALSE(find_level(levels, price.c_str()).has_value());
        }

        // Acceptor called exactly once per step (reset does NOT trigger it)
        REQUIRE(acceptor_call_count == i + 1);
        CHECK(acceptor_last_size == levels.size());
    }
}
