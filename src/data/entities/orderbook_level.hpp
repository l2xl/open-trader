// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_ORDERBOOK_LEVEL_HPP
#define SCRATCHER_ORDERBOOK_LEVEL_HPP

#include "currency.hpp"

namespace scratcher {

// Domain orderbook level: positive size = bid, negative size = ask, zero = remove
struct OrderBookLevel {
    currency<int64_t> price;
    currency<int64_t> size;
};

} // namespace scratcher

// Glaze: OrderBookLevel is a JSON array ["price", "size"]; each currency<int64_t>
// reads/writes as a quoted decimal string via the shared codec in currency_glaze.hpp
template<>
struct glz::meta<scratcher::OrderBookLevel> {
    using T = scratcher::OrderBookLevel;
    static constexpr auto value = array(&T::price, &T::size);
};

#endif // SCRATCHER_ORDERBOOK_LEVEL_HPP
