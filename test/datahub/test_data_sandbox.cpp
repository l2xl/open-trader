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

#include <deque>
#include <string>

#include "datahub/data_feed.hpp"
#include "data/entities/iso_time.hpp"

using namespace datahub;

namespace {

struct TestTrade {
    std::string execId;
    std::string time;
    std::string price;
};

struct TestState {
    std::string id;
    std::string value;
};

} // anonymous namespace

TEST_CASE("sorted_data_feed: load and dedup", "[datahub][feed]")
{
    auto feed = sorted_data_feed<TestTrade, &TestTrade::time, &TestTrade::execId>::create();
    auto acceptor = feed->data_acceptor<std::deque<TestTrade>>();

    std::deque<TestTrade> received;
    update_kind last_kind{};

    auto sub = make_data_subscription<TestTrade>(
        [&](update_kind kind, const std::deque<TestTrade>& data) {
            last_kind = kind;
            received.assign(data.begin(), data.end());
        });
    feed->subscribe(sub);

    acceptor(std::deque<TestTrade>{{"e1", "1000", "100.0"}, {"e2", "1001", "101.0"}, {"e3", "1002", "102.0"}});

    REQUIRE(received.size() == 3);
    REQUIRE(last_kind == update_kind::increment);
    REQUIRE(received[0].time == "1000");
    REQUIRE(received[2].time == "1002");

    received.clear();

    acceptor(std::deque<TestTrade>{{"e2", "1001", "dup"}, {"e4", "1003", "103.0"}, {"e5", "0999", "99.0"}});

    REQUIRE(received.size() == 2);
    REQUIRE(feed->get_snapshot().size() == 5);
    REQUIRE(feed->get_snapshot().front().time == "0999");
    REQUIRE(feed->get_snapshot().back().time == "1003");
}

TEST_CASE("sorted_data_feed: filter at construction", "[datahub][feed]")
{
    using feed_type = sorted_data_feed<TestTrade, &TestTrade::time, &TestTrade::execId>;

    std::deque<TestTrade> source;
    for (int i = 0; i < 20; ++i)
        source.push_back({"e" + std::to_string(i), std::to_string(1000 + i), "price"});

    SECTION("no filter") {
        auto feed = feed_type::create();
        auto acceptor = feed->data_acceptor<std::deque<TestTrade>>();
        acceptor(std::deque(source));

        std::deque<TestTrade> received;
        auto sub = make_data_subscription<TestTrade>(
            [&](update_kind, const std::deque<TestTrade>& data) { received = data; });
        feed->subscribe(sub);

        REQUIRE(received.size() == 20);
    }

    SECTION("filter [1000,1010)") {
        auto feed = feed_type::create([](const TestTrade& t) { return t.time >= "1000" && t.time < "1010"; });
        auto acceptor = feed->data_acceptor<std::deque<TestTrade>>();
        acceptor(std::deque(source));

        REQUIRE(feed->get_snapshot().size() == 10);
    }
}

TEST_CASE("sorted_data_feed: incremental with filter", "[datahub][feed]")
{
    auto feed = sorted_data_feed<TestTrade, &TestTrade::time, &TestTrade::execId>::create(
        [](const TestTrade& t) { return t.time < "1005"; });
    auto acceptor = feed->data_acceptor<std::deque<TestTrade>>();

    std::deque<TestTrade> received;
    update_kind last_kind{};

    auto sub = make_data_subscription<TestTrade>(
        [&](update_kind kind, const std::deque<TestTrade>& data) {
            received.assign(data.begin(), data.end());
            last_kind = kind;
        });
    feed->subscribe(sub);

    std::deque<TestTrade> initial;
    for (int i = 0; i < 10; ++i)
        initial.push_back({"e" + std::to_string(i), std::to_string(1000 + i), "price"});
    acceptor(std::move(initial));

    REQUIRE(received.size() == 5);
    REQUIRE(last_kind == update_kind::increment);

    acceptor(std::deque<TestTrade>{{"e10", "1003", "p"}, {"e11", "1007", "p"}, {"e12", "1004", "p"}});

    REQUIRE(received.size() == 2);
    REQUIRE(feed->get_snapshot().size() == 7);  // only filtered items stored
}

TEST_CASE("snapshot_data_feed: latest state replacement", "[datahub][feed]")
{
    auto feed = snapshot_data_feed<TestState>::create();
    auto acceptor = feed->data_acceptor<std::deque<TestState>>();

    std::deque<TestState> received;

    auto sub = make_data_subscription<TestState>(
        [&](update_kind, const std::deque<TestState>& data) { received = data; });
    feed->subscribe(sub);

    acceptor(std::deque<TestState>{{"a1", "v1"}, {"a2", "v2"}});
    REQUIRE(received.size() == 2);

    acceptor(std::deque<TestState>{{"b1", "v3"}});
    REQUIRE(received.size() == 1);
    REQUIRE(received[0].id == "b1");
}

TEST_CASE("subscription RAII: drop shared_ptr stops delivery", "[datahub][feed]")
{
    auto feed = snapshot_data_feed<TestState>::create();
    auto acceptor = feed->data_acceptor<std::deque<TestState>>();
    size_t call_count = 0;

    {
        auto sub = make_data_subscription<TestState>(
            [&](update_kind, const std::deque<TestState>&) { ++call_count; });
        feed->subscribe(sub);
        REQUIRE(call_count == 1);  // initial snapshot

        acceptor(std::deque<TestState>{{"x", "y"}});
        REQUIRE(call_count == 2);
    }

    acceptor(std::deque<TestState>{{"z", "w"}});
    REQUIRE(call_count == 2);
}

TEST_CASE("iso_time comparison", "[datahub][iso_time]")
{
    scratcher::iso_time t1("2026-03-23T14:00:00.000Z");
    scratcher::iso_time t2("2026-03-23T14:30:00.000Z");

    REQUIRE(t1 < t2);
    REQUIRE(t1 == scratcher::iso_time("2026-03-23T14:00:00.000Z"));
    REQUIRE(t1 != t2);
    REQUIRE(scratcher::iso_time{}.empty());
}
