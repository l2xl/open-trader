// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef BYBIT_ORDERBOOK_HPP
#define BYBIT_ORDERBOOK_HPP

#include <deque>

#include "entities/orderbook_level.hpp"

namespace scratcher::bybit {

// ByBit WebSocket orderbook snapshot/delta payload
struct OrderBookData {
    std::string s;                        // Symbol
    std::vector<OrderBookLevel> b;        // Bids (positive size)
    std::vector<OrderBookLevel> a;        // Asks (positive size from wire, negate for domain)
    uint64_t u{0};                        // Update ID
    uint64_t seq{0};                      // Cross sequence
};

} // namespace scratcher::bybit

#endif // BYBIT_ORDERBOOK_HPP
