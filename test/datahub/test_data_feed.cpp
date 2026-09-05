// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_template_test_macros.hpp>

#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>
#include <SQLiteCpp/SQLiteCpp.h>

#include "scheduler.hpp"
#include "datahub/data_model.hpp"
#include "datahub/data_sink.hpp"
#include "datahub/data_feed.hpp"
#include "datahub/data_subscription.hpp"

using datahub::data_condition;
using datahub::data_model;
using datahub::data_sink;
using datahub::data_subscription;
using datahub::keyed_snapshot_data_feed;
using datahub::make_data_sink;
using datahub::make_subscription;
using datahub::sorted_data_feed;
using datahub::sorted_snapshot_data_feed;
using datahub::update_kind;

namespace data_feed_test {

struct Trade {
    std::string id;
    int seq{};
};

} // namespace data_feed_test

namespace {

using data_feed_test::Trade;
using TradeModel = data_model<Trade, &Trade::id>;
using TradeCondition = data_condition<Trade>;
using SortedFeed = sorted_data_feed<Trade, &Trade::seq, &Trade::id>;
using SortedSnapshotFeed = sorted_snapshot_data_feed<Trade, &Trade::seq, &Trade::id>;
using KeyedFeed = keyed_snapshot_data_feed<Trade, &Trade::id>;

// Test vector shared by every case: the mid-range condition matches two seed trades and one tail trade.
const std::deque<Trade> seed{{"a", 5}, {"b", 10}, {"c", 20}};
const std::deque<Trade> tail{{"d", 30}, {"e", 40}};

TradeCondition mid_range()
{ return TradeCondition(TradeCondition::greater_or_equal<&Trade::seq>(10), TradeCondition::less_or_equal<&Trade::seq>(30)); }

// What a subscriber has seen so far, independent of the feed's dispatch shape:
// a snapshot replaces the view, an increment extends it.
struct View {
    std::vector<update_kind> kinds;
    std::vector<int> seqs;

    template<typename It>
    void record(update_kind kind, It first, It last)
    {
        if (kind == update_kind::snapshot) seqs.clear();
        for (auto it = first; it != last; ++it) seqs.push_back(it->seq);
        kinds.push_back(kind);
    }
};

template<typename Feed>
auto subscribe_view(std::shared_ptr<Feed> feed, View& view, TradeCondition condition)
{
    using cache_type = typename Feed::cache_type;
    if constexpr (std::is_same_v<typename Feed::subscription_type, data_subscription<cache_type>>) {
        auto sub = make_subscription<cache_type>(
            [&view](update_kind kind, const cache_type& full) { view.record(kind, full.begin(), full.end()); });
        feed->subscribe(sub, std::move(condition));
        return sub;
    }
    else {
        auto sub = make_subscription<cache_type>(
            [&view](update_kind kind, const cache_type&, auto first, auto last) { view.record(kind, first, last); });
        feed->subscribe(sub, std::move(condition));
        return sub;
    }
}

struct Storage {
    std::shared_ptr<SQLite::Database> db = std::make_shared<SQLite::Database>(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    std::shared_ptr<scratcher::scheduler> sched = scratcher::scheduler::create(1);
    TradeModel::strand_type strand = boost::asio::make_strand(sched->io().get_executor());

    std::shared_ptr<TradeModel> model() { return TradeModel::create(db, strand); }
};

template<typename Feed>
struct Pipeline {
    std::shared_ptr<Feed> feed;
    std::shared_ptr<data_sink<TradeModel>> sink;
};

template<typename Feed>
Pipeline<Feed> attach(std::shared_ptr<TradeModel> model)
{
    Pipeline<Feed> pipeline;
    pipeline.feed = Feed::create();
    pipeline.sink = make_data_sink(std::move(model), pipeline.feed->template data_acceptor<std::deque<Trade>>(), [](std::exception_ptr) {});
    return pipeline;
}

// Ingests the seed through a throwaway pipeline and drops it, leaving only the persisted rows.
template<typename Feed>
Storage persist_seed()
{
    Storage storage;
    attach<Feed>(storage.model()).sink->accept(seed);
    return storage;
}

} // namespace

// SQLiteCpp's debug assertion hook dictates the const char* signature.
namespace SQLite {
void assertion_failed(const char* file, int line, const char* func, const char* expr, const char* msg)
{
    std::cerr << "SQLite assertion failed: " << file << ":" << line << " in " << func << "() - " << expr;
    if (msg) std::cerr << " (" << msg << ")";
    std::cerr << std::endl;
    std::abort();
}
} // namespace SQLite

TEMPLATE_TEST_CASE("persisted feed", "[datahub][DATAHUB-023]", SortedFeed, SortedSnapshotFeed, KeyedFeed)
{
    using Feed = TestType;
    auto storage = persist_seed<Feed>();
    auto model = storage.model();
    REQUIRE(model->count() == seed.size());

    View view;
    auto pipeline = attach<Feed>(model);
    auto sub = subscribe_view(pipeline.feed, view, TradeCondition{});

    SECTION("replayed snapshot")
    {
        REQUIRE(view.kinds == std::vector{update_kind::snapshot});
        REQUIRE(view.seqs == std::vector<int>{5, 10, 20});
    }

    SECTION("runtime tail after replay")
    {
        pipeline.sink->accept(tail);
        REQUIRE(view.kinds.size() == 2);
        REQUIRE(view.seqs == std::vector<int>{5, 10, 20, 30, 40});
    }
}

TEMPLATE_TEST_CASE("subscription condition", "[datahub][DATAHUB-029]", SortedFeed, SortedSnapshotFeed, KeyedFeed)
{
    using Feed = TestType;
    View narrow, wide;

    SECTION("runtime records")
    {
        auto feed = Feed::create();
        auto acceptor = feed->template data_acceptor<std::deque<Trade>>();
        auto narrow_sub = subscribe_view(feed, narrow, mid_range());
        auto wide_sub = subscribe_view(feed, wide, TradeCondition{});

        acceptor(std::deque<Trade>(seed));
        REQUIRE(narrow.kinds == std::vector{update_kind::snapshot});
        REQUIRE(narrow.seqs == std::vector<int>{10, 20});
        REQUIRE(wide.kinds == std::vector{update_kind::snapshot});
        REQUIRE(wide.seqs == std::vector<int>{5, 10, 20});

        acceptor(std::deque<Trade>(tail));
        REQUIRE(narrow.kinds.size() == 2);
        REQUIRE(narrow.seqs == std::vector<int>{10, 20, 30});
        REQUIRE(wide.kinds.size() == 2);
        REQUIRE(wide.seqs == std::vector<int>{5, 10, 20, 30, 40});

        acceptor(std::deque<Trade>{{"f", 50}});
        REQUIRE(narrow.seqs == std::vector<int>{10, 20, 30});
        REQUIRE(wide.seqs == std::vector<int>{5, 10, 20, 30, 40, 50});
    }

    SECTION("cached records")
    {
        auto feed = Feed::create();
        auto acceptor = feed->template data_acceptor<std::deque<Trade>>();
        acceptor(std::deque<Trade>(seed));

        auto narrow_sub = subscribe_view(feed, narrow, mid_range());
        auto wide_sub = subscribe_view(feed, wide, TradeCondition{});
        REQUIRE(narrow.kinds == std::vector{update_kind::snapshot});
        REQUIRE(narrow.seqs == std::vector<int>{10, 20});
        REQUIRE(wide.kinds == std::vector{update_kind::snapshot});
        REQUIRE(wide.seqs == std::vector<int>{5, 10, 20});

        acceptor(std::deque<Trade>(tail));
        REQUIRE(narrow.seqs == std::vector<int>{10, 20, 30});
        REQUIRE(wide.seqs == std::vector<int>{5, 10, 20, 30, 40});
    }

    SECTION("persisted records")
    {
        auto storage = persist_seed<Feed>();
        auto pipeline = attach<Feed>(storage.model());
        auto narrow_sub = subscribe_view(pipeline.feed, narrow, mid_range());
        auto wide_sub = subscribe_view(pipeline.feed, wide, TradeCondition{});
        REQUIRE(narrow.kinds == std::vector{update_kind::snapshot});
        REQUIRE(narrow.seqs == std::vector<int>{10, 20});
        REQUIRE(wide.kinds == std::vector{update_kind::snapshot});
        REQUIRE(wide.seqs == std::vector<int>{5, 10, 20});

        pipeline.sink->accept(tail);
        REQUIRE(narrow.seqs == std::vector<int>{10, 20, 30});
        REQUIRE(wide.seqs == std::vector<int>{5, 10, 20, 30, 40});
    }
}
