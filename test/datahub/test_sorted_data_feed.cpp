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

struct Trade {
    std::string id;
    int seq{};  // sort key: monotonically increasing sequence number
};

using TradeFeed = sorted_data_feed<Trade, &Trade::seq, &Trade::id>;

struct Update {
    update_kind kind;
    std::vector<int> seqs;
};

std::vector<int> snapshot_seqs(const TradeFeed& feed)
{
    std::vector<int> result;
    for (auto& t : feed.get_snapshot())
        result.push_back(t.seq);
    return result;
}

// Subscribes to feed and appends each notification to log. Caller must keep the returned shared_ptr alive.
auto subscribe_log(std::shared_ptr<TradeFeed> feed, std::vector<Update>& log)
{
    auto sub = make_subscription<std::deque<Trade>>(
        [&log](update_kind kind, const std::deque<Trade>& /*full*/, auto first, auto last) {
            Update u{kind, {}};
            for (auto it = first; it != last; ++it) u.seqs.push_back(it->seq);
            log.push_back(std::move(u));
        });
    feed->subscribe(sub);
    return sub;
}

} // namespace

// Reproduces the bug: incoming [3, 7, 12] merged into cache [5, 10, 20].
// Only element 3 is inserted; 7 and 12 are silently dropped because cache_it
// advances past them without re-examining the same incoming element.
TEST_CASE("sorted_data_feed: merge drops elements between cache entries", "[datahub][feed][merge][DATAHUB-031]")
{
    auto feed = TradeFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Trade>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    // Seed cache with [5, 10, 20]
    acceptor(std::deque<Trade>{{"a", 5}, {"b", 10}, {"c", 20}});
    REQUIRE(feed->get_snapshot().size() == 3);
    REQUIRE(log.size() == 1);
    REQUIRE(log[0].kind == update_kind::snapshot);
    REQUIRE(log[0].seqs == std::vector<int>{5, 10, 20});

    // Merge in [3, 7, 12] — all three are new and fit between existing entries
    acceptor(std::deque<Trade>{{"d", 3}, {"e", 7}, {"f", 12}});

    // Middle inserts cause a snapshot of the full re-ordered cache
    REQUIRE(feed->get_snapshot().size() == 6);
    REQUIRE(snapshot_seqs(*feed) == std::vector<int>{3, 5, 7, 10, 12, 20});
    REQUIRE(log.size() == 2);
    REQUIRE(log[1].kind == update_kind::snapshot);
    REQUIRE(log[1].seqs == std::vector<int>{3, 5, 7, 10, 12, 20});
}

// Appending elements beyond the last cache entry must still work.
TEST_CASE("sorted_data_feed: merge appends elements past cache tail", "[datahub][feed][merge][DATAHUB-032]")
{
    auto feed = TradeFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Trade>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    acceptor(std::deque<Trade>{{"a", 5}, {"b", 10}});
    REQUIRE(log.size() == 1);
    REQUIRE(log[0].kind == update_kind::snapshot);
    REQUIRE(log[0].seqs == std::vector<int>{5, 10});

    acceptor(std::deque<Trade>{{"c", 15}, {"d", 20}});

    // Pure tail append: subscribers get an increment with only the new items
    REQUIRE(feed->get_snapshot().size() == 4);
    REQUIRE(snapshot_seqs(*feed) == std::vector<int>{5, 10, 15, 20});
    REQUIRE(log.size() == 2);
    REQUIRE(log[1].kind == update_kind::increment);
    REQUIRE(log[1].seqs == std::vector<int>{15, 20});
}

// Duplicates (same seq + same id) must be deduplicated.
TEST_CASE("sorted_data_feed: merge deduplicates by key", "[datahub][feed][merge][DATAHUB-033]")
{
    auto feed = TradeFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Trade>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    acceptor(std::deque<Trade>{{"a", 5}, {"b", 10}, {"c", 20}});
    REQUIRE(log.size() == 1);
    REQUIRE(log[0].kind == update_kind::snapshot);

    // re-send the same three trades plus one new one
    acceptor(std::deque<Trade>{{"a", 5}, {"b", 10}, {"c", 20}, {"d", 30}});

    // Only "d" is new and appended at the tail: increment with just [30]
    REQUIRE(feed->get_snapshot().size() == 4);
    REQUIRE(feed->get_snapshot().back().seq == 30);
    REQUIRE(log.size() == 2);
    REQUIRE(log[1].kind == update_kind::increment);
    REQUIRE(log[1].seqs == std::vector<int>{30});
}

// Two trades with the same seq value but different ids must both be kept.
TEST_CASE("sorted_data_feed: same sort key different id both inserted", "[datahub][feed][merge][DATAHUB-034]")
{
    auto feed = TradeFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Trade>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    acceptor(std::deque<Trade>{{"a", 10}, {"b", 20}});
    REQUIRE(log.size() == 1);
    REQUIRE(log[0].kind == update_kind::snapshot);
    REQUIRE(log[0].seqs == std::vector<int>{10, 20});

    // "c" has same seq as "a" but a different id — inserted before "a", causes reorder snapshot
    acceptor(std::deque<Trade>{{"c", 10}});

    REQUIRE(feed->get_snapshot().size() == 3);
    REQUIRE(log.size() == 2);
    REQUIRE(log[1].kind == update_kind::snapshot);
    REQUIRE(log[1].seqs == std::vector<int>{10, 10, 20});
}

// A subscriber added after data is already present receives an immediate snapshot.
TEST_CASE("sorted_data_feed: late subscriber receives current snapshot", "[datahub][feed][subscribe][DATAHUB-035]")
{
    auto feed = TradeFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Trade>>();

    acceptor(std::deque<Trade>{{"a", 5}, {"b", 10}, {"c", 20}});

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    // Subscribing to a non-empty feed fires an immediate snapshot — no further accept needed
    REQUIRE(log.size() == 1);
    REQUIRE(log[0].kind == update_kind::snapshot);
    REQUIRE(log[0].seqs == std::vector<int>{5, 10, 20});
}
