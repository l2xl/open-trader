// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef CORE_COMMON_HPP
#define CORE_COMMON_HPP

#include <cstdint>

namespace scratcher {

struct Rectangle
{
    uint64_t x = 0, y = 0, w = 0, h = 0;
    uint64_t x_start() const { return x; }
    uint64_t x_end() const { return x + w; }
    uint64_t y_start() const { return y; }
    uint64_t y_end() const { return y + h; }
};

}


#endif //CORE_COMMON_HPP
