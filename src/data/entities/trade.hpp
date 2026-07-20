// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef SCRATCHER_ENTITY_TRADE_HPP
#define SCRATCHER_ENTITY_TRADE_HPP

#include <string>

#include "timedef.hpp"
#include "entities/common.hpp"

namespace scratcher::data {

struct PublicTrade
{
    std::string id;
    time_point trade_time;

    uint64_t price_points;
    uint64_t volume_points;

    OrderSide side;
};

}


#endif //SCRATCHER_ENTITY_TRADE_HPP
