// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef EXSCRATCHER_HEX_HPP
#define EXSCRATCHER_HEX_HPP
#include <array>
#include <cassert>
#include <cstdint>
#include <string>

namespace scratcher {

extern std::array<std::array<char, 2>, 256> byte_to_hex;

template<unsigned N>
std::string hex(const unsigned char (&s)[N])
{
    std::string res(N * 2, '\0');

    char* it = res.data();
    for (uint8_t v : s) {
        *it = byte_to_hex[v][0];
        ++it;
        *it = byte_to_hex[v][1];
        ++it;
    }

    assert(it == res.data() + res.size());
    return res;
}

template<typename SPAN>
std::string hex(const SPAN& s)
{
    std::string res(s.size() * 2, '\0');

    char* it = res.data();
    for (uint8_t v : s) {
        *it = byte_to_hex[v][0];
        ++it;
        *it = byte_to_hex[v][1];
        ++it;
    }

    assert(it == res.data() + res.size());
    return res;
}

}

#endif //EXSCRATCHER_HEX_HPP
