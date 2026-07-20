// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "common.hpp"
#include <stdexcept>

namespace scratcher::data {

// Define static array of instances
const std::array<OrderSide, 2> OrderSide::instances_ = {{
    OrderSide{"Buy", 0},
    OrderSide{"Sell", 1}
}};

// Define named references
const OrderSide& OrderSide::BUY = instances_[0];
const OrderSide& OrderSide::SELL = instances_[1];

std::string to_string(const OrderSide& side) {
    return std::string(side.name());
}

std::string to_string(OrderType type) {
    switch (type) {
        case OrderType::Market: return "Market";
        case OrderType::Limit: return "Limit";
    }
    throw std::invalid_argument("Invalid OrderType");
}

OrderType order_type_from_string(const std::string& str) {
    if (str == "Market") return OrderType::Market;
    if (str == "Limit") return OrderType::Limit;
    throw std::invalid_argument("Invalid OrderType string: " + str);
}

} // namespace scratcher::data
