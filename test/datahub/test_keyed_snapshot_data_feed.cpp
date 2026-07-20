// XCockpit
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>

#include <deque>
#include <string>
#include <vector>

#include "datahub/data_feed.hpp"
#include "datahub/data_subscription.hpp"

using namespace datahub;

namespace {

struct Item {
    std::string id;  // key
    int value{};
};

using ItemFeed = keyed_snapshot_data_feed<Item, &Item::id>;

struct Update {
    update_kind kind;
    std::vector<std::string> ids;
    std::vector<int> values;
};

auto subscribe_log(std::shared_ptr<ItemFeed> feed, std::vector<Update>& log)
{
    auto sub = make_subscription<std::deque<Item>>(
        [&log](update_kind kind, const std::deque<Item>& full) {
            Update u{kind, {}, {}};
            for (const auto& item : full) { u.ids.push_back(item.id); u.values.push_back(item.value); }
            log.push_back(std::move(u));
        });
    feed->subscribe(sub);
    return sub;
}

} // namespace

TEST_CASE("keyed_snapshot_data_feed: first accept inserts all and fires snapshot", "[datahub][feed][keyed][DATAHUB-051]")
{
    auto feed = ItemFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Item>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    acceptor(std::deque<Item>{{"a", 1}, {"b", 2}, {"c", 3}});

    REQUIRE(feed->get_snapshot().size() == 3);
    REQUIRE(log.size() == 1);
    REQUIRE(log[0].kind == update_kind::snapshot);
    REQUIRE(log[0].ids == std::vector<std::string>{"a", "b", "c"});
    REQUIRE(log[0].values == std::vector<int>{1, 2, 3});
}

TEST_CASE("keyed_snapshot_data_feed: new keys append to cache and fire snapshot", "[datahub][feed][keyed][DATAHUB-052]")
{
    auto feed = ItemFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Item>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    acceptor(std::deque<Item>{{"a", 1}});
    REQUIRE(log[0].kind == update_kind::snapshot);
    REQUIRE(log[0].ids == std::vector<std::string>{"a"});

    acceptor(std::deque<Item>{{"b", 2}});

    REQUIRE(feed->get_snapshot().size() == 2);
    REQUIRE(log.size() == 2);
    REQUIRE(log[1].kind == update_kind::snapshot);
    REQUIRE(log[1].ids == std::vector<std::string>{"a", "b"});
}

TEST_CASE("keyed_snapshot_data_feed: existing key updates value in place", "[datahub][feed][keyed][DATAHUB-053]")
{
    auto feed = ItemFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Item>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    acceptor(std::deque<Item>{{"a", 1}, {"b", 2}});
    REQUIRE(log[0].kind == update_kind::snapshot);

    // "a" already exists: update its value; "b" already exists: update too
    acceptor(std::deque<Item>{{"a", 99}});

    // Cache still has 2 items; "a" value updated in place
    REQUIRE(feed->get_snapshot().size() == 2);
    REQUIRE(feed->get_snapshot()[0].id == "a");
    REQUIRE(feed->get_snapshot()[0].value == 99);
    REQUIRE(log.size() == 2);
    REQUIRE(log[1].kind == update_kind::snapshot);
    REQUIRE(log[1].ids == std::vector<std::string>{"a", "b"});
    REQUIRE(log[1].values == std::vector<int>{99, 2});
}

TEST_CASE("keyed_snapshot_data_feed: mixed new and existing keys in one batch", "[datahub][feed][keyed][DATAHUB-054]")
{
    auto feed = ItemFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Item>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    acceptor(std::deque<Item>{{"a", 1}, {"b", 2}});

    // "b" updated, "c" is new — order: a at 0, b at 1 (updated in place), c appended
    acceptor(std::deque<Item>{{"b", 20}, {"c", 3}});

    REQUIRE(feed->get_snapshot().size() == 3);
    REQUIRE(log.size() == 2);
    REQUIRE(log[1].kind == update_kind::snapshot);
    REQUIRE(log[1].ids == std::vector<std::string>{"a", "b", "c"});
    REQUIRE(log[1].values == std::vector<int>{1, 20, 3});
}

TEST_CASE("keyed_snapshot_data_feed: no update fired when incoming is empty", "[datahub][feed][keyed][DATAHUB-055]")
{
    auto feed = ItemFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Item>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    acceptor(std::deque<Item>{{"a", 1}});
    REQUIRE(log.size() == 1);

    acceptor(std::deque<Item>{});

    // Empty batch: no change, no notification
    REQUIRE(log.size() == 1);
}

TEST_CASE("keyed_snapshot_data_feed: late subscriber receives current snapshot", "[datahub][feed][subscribe][DATAHUB-056]")
{
    auto feed = ItemFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Item>>();

    acceptor(std::deque<Item>{{"a", 1}, {"b", 2}, {"c", 3}});

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    REQUIRE(log.size() == 1);
    REQUIRE(log[0].kind == update_kind::snapshot);
    REQUIRE(log[0].ids == std::vector<std::string>{"a", "b", "c"});
}
