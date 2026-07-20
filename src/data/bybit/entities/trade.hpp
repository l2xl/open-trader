// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef BYBIT_TRADE_HPP
#define BYBIT_TRADE_HPP

#include <string>
#include <deque>
#include <optional>
#include "enums.hpp"
#include "currency.hpp"

namespace scratcher::bybit {

using scratcher::currency;

// Trade execution information from ByBit Trade/TradeHistory API. Monetary/quantity
// fields are currency<uint64_t>; feeCurrency stays a string (a coin name, not a value)
// and seq stays a number.
struct Trade {
    std::string symbol;                         // Symbol name
    std::string orderId;                        // Order ID
    std::optional<std::string> orderLinkId;     // User customized order ID
    OrderSide side;                             // Side (Buy/Sell)
    currency<uint64_t> orderPrice;              // Order price
    currency<uint64_t> orderQty;                // Order qty
    std::optional<currency<uint64_t>> leavesQty;       // The remaining qty not executed
    std::optional<std::string> createType;      // Order create type
    OrderType orderType;                         // Order type. Market, Limit
    std::optional<StopOrderType> stopOrderType;  // Stop order type
    currency<uint64_t> execFee;                 // Executed trading fee
    std::optional<currency<uint64_t>> execFeeV2;       // Spot leg transaction fee, only works for execType=FutureSpread
    std::string execId;                         // Execution ID
    currency<uint64_t> execPrice;               // Execution price
    currency<uint64_t> execQty;                 // Execution qty
    ExecType execType;                          // Executed type
    std::optional<currency<uint64_t>> execValue;       // Executed order value
    std::string execTime;                       // Executed timestamp (ms)
    std::optional<std::string> feeCurrency;     // Spot trading fee currency
    bool isMaker;                               // Is maker order. true: maker, false: taker
    std::optional<currency<uint64_t>> feeRate;         // Trading fee rate
    std::optional<currency<uint64_t>> tradeIv;         // Implied volatility. Valid for option
    std::optional<currency<uint64_t>> markIv;          // Implied volatility of mark price. Valid for option
    std::optional<currency<uint64_t>> markPrice;       // The mark price of the symbol when executing
    std::optional<currency<uint64_t>> indexPrice;      // The index price of the symbol when executing. Valid for option only
    std::optional<currency<uint64_t>> underlyingPrice; // The underlying price of the symbol when executing. Valid for option
    std::optional<std::string> blockTradeId;    // Paradigm block trade ID
    std::optional<currency<uint64_t>> closedSize;      // Closed position size
    std::optional<long> seq;                    // Cross sequence, used to associate each fill and each position update
    std::optional<std::string> extraFees;       // Trading fee rate information
};



} // namespace scratcher::bybit

#endif // BYBIT_TRADE_HPP
