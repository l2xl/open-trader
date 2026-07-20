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
    int seq{};
};

// sorted_snapshot_data_feed always fires snapshot regardless of where new items land
using TradeFeed = sorted_snapshot_data_feed<Trade, &Trade::seq, &Trade::id>;

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

auto subscribe_log(std::shared_ptr<TradeFeed> feed, std::vector<Update>& log)
{
    auto sub = make_subscription<std::deque<Trade>>(
        [&log](update_kind kind, const std::deque<Trade>& full) {
            Update u{kind, {}};
            for (const auto& t : full) u.seqs.push_back(t.seq);
            log.push_back(std::move(u));
        });
    feed->subscribe(sub);
    return sub;
}

} // namespace

TEST_CASE("sorted_snapshot_data_feed: middle insert fires full snapshot", "[datahub][feed][snapshot][DATAHUB-041]")
{
    auto feed = TradeFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Trade>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    acceptor(std::deque<Trade>{{"a", 5}, {"b", 10}, {"c", 20}});
    REQUIRE(log.size() == 1);
    REQUIRE(log[0].kind == update_kind::snapshot);
    REQUIRE(log[0].seqs == std::vector<int>{5, 10, 20});

    // Items inserted in the middle: always snapshot of the full sorted cache
    acceptor(std::deque<Trade>{{"d", 3}, {"e", 7}, {"f", 12}});

    REQUIRE(feed->get_snapshot().size() == 6);
    REQUIRE(snapshot_seqs(*feed) == std::vector<int>{3, 5, 7, 10, 12, 20});
    REQUIRE(log.size() == 2);
    REQUIRE(log[1].kind == update_kind::snapshot);
    REQUIRE(log[1].seqs == std::vector<int>{3, 5, 7, 10, 12, 20});
}

TEST_CASE("sorted_snapshot_data_feed: tail append fires full snapshot", "[datahub][feed][snapshot][DATAHUB-042]")
{
    auto feed = TradeFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Trade>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    acceptor(std::deque<Trade>{{"a", 5}, {"b", 10}});
    REQUIRE(log[0].kind == update_kind::snapshot);
    REQUIRE(log[0].seqs == std::vector<int>{5, 10});

    // Pure tail append: unlike sorted_data_feed, still fires a snapshot of the full cache
    acceptor(std::deque<Trade>{{"c", 15}, {"d", 20}});

    REQUIRE(feed->get_snapshot().size() == 4);
    REQUIRE(snapshot_seqs(*feed) == std::vector<int>{5, 10, 15, 20});
    REQUIRE(log.size() == 2);
    REQUIRE(log[1].kind == update_kind::snapshot);
    REQUIRE(log[1].seqs == std::vector<int>{5, 10, 15, 20});
}

TEST_CASE("sorted_snapshot_data_feed: deduplicates by key, fires snapshot with new tail", "[datahub][feed][snapshot][DATAHUB-043]")
{
    auto feed = TradeFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Trade>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    acceptor(std::deque<Trade>{{"a", 5}, {"b", 10}, {"c", 20}});
    REQUIRE(log[0].kind == update_kind::snapshot);

    // Re-send same three plus one new: dedup skips a/b/c, appends d
    acceptor(std::deque<Trade>{{"a", 5}, {"b", 10}, {"c", 20}, {"d", 30}});

    REQUIRE(feed->get_snapshot().size() == 4);
    REQUIRE(feed->get_snapshot().back().seq == 30);
    REQUIRE(log.size() == 2);
    // snapshot_data_feed always fires snapshot — full cache, not just the new tail
    REQUIRE(log[1].kind == update_kind::snapshot);
    REQUIRE(log[1].seqs == std::vector<int>{5, 10, 20, 30});
}

TEST_CASE("sorted_snapshot_data_feed: same sort key different id both inserted", "[datahub][feed][snapshot][DATAHUB-044]")
{
    auto feed = TradeFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Trade>>();

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    acceptor(std::deque<Trade>{{"a", 10}, {"b", 20}});
    REQUIRE(log[0].kind == update_kind::snapshot);
    REQUIRE(log[0].seqs == std::vector<int>{10, 20});

    acceptor(std::deque<Trade>{{"c", 10}});

    REQUIRE(feed->get_snapshot().size() == 3);
    REQUIRE(log.size() == 2);
    REQUIRE(log[1].kind == update_kind::snapshot);
    REQUIRE(log[1].seqs == std::vector<int>{10, 10, 20});
}

TEST_CASE("sorted_snapshot_data_feed: late subscriber receives current snapshot", "[datahub][feed][subscribe][DATAHUB-045]")
{
    auto feed = TradeFeed::create();
    auto acceptor = feed->data_acceptor<std::deque<Trade>>();

    acceptor(std::deque<Trade>{{"a", 5}, {"b", 10}, {"c", 20}});

    std::vector<Update> log;
    auto sub = subscribe_log(feed, log);

    REQUIRE(log.size() == 1);
    REQUIRE(log[0].kind == update_kind::snapshot);
    REQUIRE(log[0].seqs == std::vector<int>{5, 10, 20});
}
