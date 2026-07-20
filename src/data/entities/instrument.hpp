// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef DATA_ENTITIES_INSTRUMENT_HPP
#define DATA_ENTITIES_INSTRUMENT_HPP

#include <string>
#include "currency.hpp"

namespace scratcher::data {

struct Instrument {
    std::string symbol;                 // Symbol name (BTCUSDT)
    std::string base_coin;              // Base coin (BTC)
    std::string quote_coin;             // Quote coin (USDT)
    
    // Common trading parameters
    currency<uint64_t> price_point;
    currency<uint64_t> price_precision;
    currency<uint64_t> volume_point;
    currency<uint64_t> volume_precision;
    currency<uint64_t> min_volume;   // Minimum order quantity
    currency<uint64_t> max_volume;   // Maximum order quantity
    currency<uint64_t> min_amount;   // Minimum order amount
    currency<uint64_t> max_amount;   // Maximum order amount
    
    // Equality operators for comparison
    bool operator==(const Instrument&) const = default;
    bool operator!=(const Instrument&) const = default;
};

} // namespace scratcher::data

#endif // DATA_ENTITIES_INSTRUMENT_HPP
