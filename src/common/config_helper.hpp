// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include <optional>
#include <stdexcept>
#include <string>

#include "cli11/CLI11.hpp"

namespace scratcher::config {

inline CLI::App& get_subcommand(CLI::App& parent, const char* group_name)
{
    auto* sub = parent.get_subcommand(group_name);
    if (!sub) throw std::runtime_error(std::string("Unknown config section: ") + group_name);
    return *sub;
}

template <typename T>
std::optional<T> get_option(const CLI::App& sub, const char* name)
{
    const auto* opt = sub.get_option(name);
    if (!opt || opt->empty()) return std::nullopt;
    return opt->as<T>();
}

} // namespace scratcher::config
