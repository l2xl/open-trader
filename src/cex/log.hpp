// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#pragma once

#include <atomic>
#include <iostream>
#include <ostream>

namespace cex::log {

enum class level { error, log, trace };
using enum level;

namespace detail {
inline std::atomic<bool> verbose_enabled = false;
inline std::ostream stub{nullptr};
}

inline void set_verbose(bool on) { detail::verbose_enabled.store(on, std::memory_order_relaxed); }
inline bool verbose() { return detail::verbose_enabled.load(std::memory_order_relaxed); }

inline std::ostream& at(level l)
{
    switch (l) {
    case level::error: return std::cerr;
    case level::log:   return std::clog;
    case level::trace: return verbose() ? std::clog : detail::stub;
    }
    return detail::stub;
}

} // namespace cex::log
