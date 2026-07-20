// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef BYBIT_FEE_RATE_HPP
#define BYBIT_FEE_RATE_HPP

#include <string>
#include <optional>
#include "currency.hpp"

namespace scratcher::bybit {

using scratcher::currency;

// Fee rate information from ByBit Account/GetFeeRate API
struct FeeRate {
    std::string symbol;  // Symbol name (e.g., "BTCUSDT", empty for options)
    std::optional<std::string> baseCoin; // Base coin (e.g., "BTC", "ETH", empty for spot)
    currency<uint64_t> takerFeeRate;    // Taker fee rate (e.g., "0.0006")
    currency<uint64_t> makerFeeRate;    // Maker fee rate (e.g., "0.0001")
};



} // namespace scratcher::bybit

#endif // BYBIT_FEE_RATE_HPP
