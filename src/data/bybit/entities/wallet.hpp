// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef BYBIT_WALLET_HPP
#define BYBIT_WALLET_HPP

#include <string>
#include <deque>
#include <optional>
#include "enums.hpp"
#include "currency.hpp"

namespace scratcher::bybit {

using scratcher::currency;

// PnL fields are signed (currency<int64_t>); balances/equity are non-negative.
struct CoinBalance {
    std::string coin;
    std::optional<currency<uint64_t>> equity;
    std::optional<currency<uint64_t>> usdValue;
    std::optional<currency<uint64_t>> walletBalance;
    std::optional<currency<uint64_t>> availableToWithdraw;
    std::optional<currency<int64_t>> unrealisedPnl;
    std::optional<currency<int64_t>> cumRealisedPnl;
};

struct WalletBalance {
    AccountType accountType;
    std::optional<currency<uint64_t>> totalEquity;
    std::optional<currency<uint64_t>> totalWalletBalance;
    std::optional<currency<uint64_t>> totalAvailableBalance;
    std::optional<currency<int64_t>> totalPerpUPL;
    std::deque<CoinBalance> coin;
};

} // namespace scratcher::bybit

#endif // BYBIT_WALLET_HPP
