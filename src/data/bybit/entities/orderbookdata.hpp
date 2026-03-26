// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b21tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

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
