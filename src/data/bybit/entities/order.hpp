// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef BYBIT_ORDER_HPP
#define BYBIT_ORDER_HPP

#include <string>
#include <optional>
#include "enums.hpp"
#include "currency.hpp"

namespace scratcher::bybit {

using scratcher::currency;

// Order information from ByBit Trade/OrderHistory API. Monetary/quantity fields are
// currency<uint64_t>; ByBit sends unset optional decimals as "" (e.g. triggerPrice),
// which the currency codec maps to a zero value rather than failing the parse.
struct Order {
    std::string orderId;                        // Order ID
    std::optional<std::string> orderLinkId;     // User customised order ID
    std::optional<std::string> blockTradeId;    // Block trade ID
    std::string symbol;                         // Symbol name
    currency<uint64_t> price;                   // Order price
    currency<uint64_t> qty;                     // Order qty
    OrderSide side;                             // Side (Buy/Sell)
    std::optional<std::string> isLeverage;      // Whether to borrow. Unified spot only. "0": false, "1": true
    std::optional<int> positionIdx;             // Position index. Used to identify positions in different position modes
    OrderStatus orderStatus;                    // Order status
    std::optional<std::string> createType;      // Order create type (only for category=linear or inverse)
    std::optional<std::string> cancelType;      // Cancel type
    std::optional<std::string> rejectReason;    // Reject reason
    currency<uint64_t> avgPrice;                // Average filled price
    std::optional<currency<uint64_t>> leavesQty;       // The remaining qty not executed
    std::optional<currency<uint64_t>> leavesValue;     // The estimated value not executed
    currency<uint64_t> cumExecQty;              // Cumulative executed order qty
    std::optional<currency<uint64_t>> cumExecValue;    // Cumulative executed order value
    std::optional<currency<uint64_t>> cumExecFee;      // Cumulative executed trading fee
    TimeInForce timeInForce;                    // Time in force
    OrderType orderType;                        // Order type. Market, Limit
    std::optional<StopOrderType> stopOrderType; // Stop order type
    std::optional<currency<uint64_t>> orderIv;  // Implied volatility
    std::optional<std::string> marketUnit;      // The unit for qty when create Spot market orders for UTA account
    std::optional<std::string> slippageToleranceType; // Spot and Futures market order slippage tolerance type
    std::optional<currency<uint64_t>> slippageTolerance; // Slippage tolerance value
    std::optional<currency<uint64_t>> triggerPrice;    // Trigger price
    std::optional<currency<uint64_t>> takeProfit;      // Take profit price
    std::optional<currency<uint64_t>> stopLoss;        // Stop loss price
    std::optional<std::string> tpslMode;        // TP/SL mode
    std::optional<std::string> ocoTriggerBy;    // The trigger type of Spot OCO order
    std::optional<currency<uint64_t>> tpLimitPrice;    // The limit order price when take profit price is triggered
    std::optional<currency<uint64_t>> slLimitPrice;    // The limit order price when stop loss price is triggered
    std::optional<std::string> tpTriggerBy;     // The price type to trigger take profit
    std::optional<std::string> slTriggerBy;     // The price type to trigger stop loss
    std::optional<int> triggerDirection;        // Trigger direction. 1: rise, 2: fall
    std::optional<TriggerBy> triggerBy;          // The price type of trigger price
    std::optional<currency<uint64_t>> lastPriceOnCreated; // Last price when place the order
    std::optional<currency<uint64_t>> basePrice;       // Last price when place the order, Spot has this field only
    std::optional<bool> reduceOnly;             // Reduce only. true means reduce position size
    std::optional<bool> closeOnTrigger;         // Close on trigger
    std::optional<std::string> placeType;       // Place type, option used. "iv", "price"
    std::optional<std::string> smpType;         // SMP execution type
    std::optional<int> smpGroup;                // Smp group ID. If the UID has no group, it is 0 by default
    std::optional<std::string> smpOrderId;      // The counterparty's orderID which triggers this SMP execution
    std::string createdTime;                    // Order created timestamp (ms)
    std::string updatedTime;                    // Order updated timestamp (ms)
    std::optional<std::string> extraFees;       // Trading fee rate information
};



struct OrderRequest {
    Category category;
    std::string symbol;
    OrderSide side;
    OrderType orderType;
    currency<uint64_t> qty;
    std::optional<currency<uint64_t>> price;
    std::optional<TimeInForce> timeInForce;
    std::optional<currency<uint64_t>> triggerPrice;
    std::optional<currency<uint64_t>> takeProfit;
    std::optional<currency<uint64_t>> stopLoss;
    std::optional<currency<uint64_t>> tpLimitPrice;
    std::optional<currency<uint64_t>> slLimitPrice;
    std::optional<std::string> orderLinkId;
    std::optional<bool> reduceOnly;
};


struct PlaceOrderResult
{
    std::string orderId;
};
struct CancelOrderRequest
{
    std::string category;
    std::string symbol;
    std::string orderId;
};


} // namespace scratcher::bybit

#endif // BYBIT_ORDER_HPP
