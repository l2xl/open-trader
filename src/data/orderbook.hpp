// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_ORDERBOOK_HPP
#define SCRATCHER_ORDERBOOK_HPP

#include <deque>
#include <memory>
#include <algorithm>
#include <ranges>
#include <functional>

#include "entities/orderbook_level.hpp"

namespace scratcher {

// In-memory orderbook model for data_provider parameterization
// Maintains sorted levels (price descending) and merges snapshot/delta updates
// After every merge, pushes the full current state to the registered acceptor
class OrderBook : public std::enable_shared_from_this<OrderBook>
{
public:
    using entity_type = OrderBookLevel;
    using cache_type = std::deque<entity_type>;
    using acceptor_type = std::function<void(cache_type&&)>;

private:
    cache_type mLevels;
    acceptor_type m_acceptor;

    struct EnsurePrivate {};

public:
    explicit OrderBook(acceptor_type acceptor, EnsurePrivate)
        : m_acceptor(std::move(acceptor))
    {}

    static std::shared_ptr<OrderBook> Create(acceptor_type acceptor)
    { return std::make_shared<OrderBook>(std::move(acceptor), EnsurePrivate{}); }

    const cache_type& Levels() const { return mLevels; }

    template <std::ranges::input_range Range>
    requires std::convertible_to<std::ranges::range_value_t<Range>, OrderBookLevel>
    void accept(Range&& entities)
    {
        if (std::ranges::empty(entities)) {
            mLevels.clear();
            return; // snapshot reset — acceptor not called, delta follows immediately
        }
        if (mLevels.empty()) {
            // Fast path for initial snapshot load: input is already sorted asc
            std::ranges::move(std::forward<Range>(entities), std::back_inserter(mLevels));
        }
        else {
            // Two-pointer O(n+m) merge — both sequences sorted ascending by price
            auto e = mLevels.begin();
            auto u = std::ranges::begin(entities);
            const auto u_end = std::ranges::end(entities);

            cache_type result;

            while (e != mLevels.end() && u != u_end) {
                if (e->price < u->price) {
                    result.push_back(*e++);          // existing level at lower price, keep
                } else if (u->price < e->price) {
                    if (u->size.raw() != 0) result.push_back(*u);  // new level, insert
                    ++u;
                } else {
                    if (u->size.raw() != 0) result.push_back(*u);  // update or delete
                    ++e; ++u;
                }
            }
            while (e != mLevels.end()) result.push_back(*e++);
            while (u != u_end) { if (u->size.raw() != 0) result.push_back(*u); ++u; }

            mLevels = std::move(result);
        }
        if (m_acceptor)
            m_acceptor(cache_type(mLevels.begin(), mLevels.end()));
    }
};

} // namespace scratcher

#endif // SCRATCHER_ORDERBOOK_HPP
